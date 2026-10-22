# 构建 Aurora 输入法桥接辅助 APK。
#
# 产物：ime-bridge/build/aurora-ime.apk（已签名）
#
# 之后由 build-embedded-apk.ps1 转成 C++ 头文件嵌入可执行文件。
#
# 为什么把 APK 内嵌进 ELF 而不是随包分发：
#   纯 ELF 交付形态要求「不安装 APK、不依赖 dex 旁挂」。把 APK 作为字节
#   数组链接进可执行文件，运行时释放到 /data/adb/ 下的临时目录再安装，
#   对外仍然是一个单文件。
#
# 用法:
#   pwsh -File build-ime-apk.ps1
#   pwsh -File build-ime-apk.ps1 -Platform android-36 -BuildTools 36.0.0

param(
    [string]$SdkRoot = "<sdk>",
    [string]$JdkRoot = "C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot",
    [string]$Platform = "android-36",
    [string]$BuildTools = "36.0.0",
    [string]$Keystore = "$env:USERPROFILE\.android\debug.keystore"
)

$ErrorActionPreference = "Stop"

$here     = Split-Path -Parent $MyInvocation.MyCommand.Path
$srcDir   = Join-Path $here "src"
$outDir   = Join-Path $here "build"
$classDir = Join-Path $outDir "classes"
$dexDir   = Join-Path $outDir "dex"

$androidJar = Join-Path $SdkRoot "platforms\$Platform\android.jar"
$btDir      = Join-Path $SdkRoot "build-tools\$BuildTools"
$aapt2      = Join-Path $btDir "aapt2.exe"
$d8         = Join-Path $btDir "d8.bat"
$zipalign   = Join-Path $btDir "zipalign.exe"
$apksigner  = Join-Path $btDir "apksigner.bat"
$javac      = Join-Path $JdkRoot "bin\javac.exe"
$keytool    = Join-Path $JdkRoot "bin\keytool.exe"

function Require($path, $what) {
    if (-not (Test-Path $path)) { throw "$what 不存在: $path" }
}

Require $androidJar "android.jar"
Require $aapt2      "aapt2"
Require $d8         "d8"
Require $zipalign   "zipalign"
Require $apksigner  "apksigner"
Require $javac      "javac"

Write-Host "=== 构建 Aurora 输入法桥接 APK ===" -ForegroundColor Cyan
Write-Host "  platform   : $Platform"
Write-Host "  build-tools: $BuildTools"
Write-Host "  android.jar: $androidJar"
Write-Host ""

# 干净的输出目录
foreach ($d in @($classDir, $dexDir)) {
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}

# ---- 1. 编译 Java ----
Write-Host "[1/6] javac 编译（release 8，Android 只支持到 8 字节码）" -ForegroundColor Yellow
$sources = Get-ChildItem $srcDir -Recurse -Filter *.java | ForEach-Object { $_.FullName }
if ($sources.Count -eq 0) { throw "没有找到 Java 源文件" }
Write-Host "      源文件: $($sources.Count) 个"

& $javac -source 8 -target 8 -encoding UTF-8 `
    -classpath $androidJar `
    -d $classDir `
    $sources 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "javac 失败" }

$classFiles = Get-ChildItem $classDir -Recurse -Filter *.class
Write-Host "      生成 class: $($classFiles.Count) 个" -ForegroundColor Green

# ---- 2. dex ----
Write-Host "[2/6] d8 转 dex" -ForegroundColor Yellow
& $d8 --release --min-api 28 --lib $androidJar --output $dexDir `
    (Get-ChildItem $classDir -Recurse -Filter *.class | ForEach-Object { $_.FullName }) 2>&1 |
    ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "d8 失败" }

$dex = Join-Path $dexDir "classes.dex"
Require $dex "classes.dex"
Write-Host "      classes.dex: $((Get-Item $dex).Length) 字节" -ForegroundColor Green

# ---- 3. 链接资源与 manifest ----
Write-Host "[3/6] aapt2 link" -ForegroundColor Yellow
$unsigned = Join-Path $outDir "aurora-ime-unsigned.apk"
if (Test-Path $unsigned) { Remove-Item $unsigned -Force }

& $aapt2 link -o $unsigned `
    -I $androidJar `
    --manifest (Join-Path $here "AndroidManifest.xml") `
    --min-sdk-version 28 --target-sdk-version 36 `
    --version-code 1 --version-name "1.0" 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "aapt2 link 失败" }

# ---- 4. 塞入 classes.dex ----
Write-Host "[4/6] 加入 classes.dex" -ForegroundColor Yellow
Push-Location $dexDir
& jar uf $unsigned "classes.dex" 2>&1 | ForEach-Object { Write-Host "      $_" }
Pop-Location
if ($LASTEXITCODE -ne 0) { throw "jar uf 失败" }

# ---- 5. 对齐 ----
Write-Host "[5/6] zipalign" -ForegroundColor Yellow
$aligned = Join-Path $outDir "aurora-ime-aligned.apk"
if (Test-Path $aligned) { Remove-Item $aligned -Force }
& $zipalign -f 4 $unsigned $aligned 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "zipalign 失败" }

# ---- 6. 签名 ----
Write-Host "[6/6] apksigner 签名" -ForegroundColor Yellow
if (-not (Test-Path $Keystore)) {
    Write-Host "      debug.keystore 不存在，生成一个" -ForegroundColor Yellow
    & $keytool -genkeypair -v -keystore $Keystore -storepass android `
        -alias androiddebugkey -keypass android -keyalg RSA -keysize 2048 `
        -validity 10000 -dname "CN=Android Debug,O=Android,C=US" 2>&1 |
        ForEach-Object { Write-Host "      $_" }
}

$final = Join-Path $outDir "aurora-ime.apk"
if (Test-Path $final) { Remove-Item $final -Force }
& $apksigner sign --ks $Keystore --ks-pass pass:android `
    --ks-key-alias androiddebugkey --key-pass pass:android `
    --out $final $aligned 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "apksigner 失败" }

# 校验签名
& $apksigner verify --print-certs $final 2>&1 | ForEach-Object { Write-Host "      $_" }
if ($LASTEXITCODE -ne 0) { throw "签名校验失败" }

Remove-Item $unsigned, $aligned -Force -ErrorAction SilentlyContinue

$size = (Get-Item $final).Length
Write-Host ""
Write-Host "=== 完成 ===" -ForegroundColor Green
Write-Host "  $final"
Write-Host "  $size 字节 ($([math]::Round($size/1KB,1)) KB)"
Write-Host ""
Write-Host "下一步: pwsh -File build-embedded-apk.ps1" -ForegroundColor Cyan
