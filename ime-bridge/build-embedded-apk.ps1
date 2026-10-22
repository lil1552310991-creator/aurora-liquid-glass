# 把签名好的 APK 转成 C++ 头文件，供 ELF 内嵌。
#
# 产物：ime-bridge/build/AuroraImeApk.h
#   static const unsigned char kAuroraImeApk[] = { ... };
#   static const unsigned int  kAuroraImeApkSize = N;
#
# 为什么内嵌而不是随包分发：
#   纯 ELF 交付形态要求「不安装 APK、不依赖 dex 旁挂」。把 APK 作为字节数组
#   链接进可执行文件，运行时释放到 /data/adb/ 下的临时目录再安装，
#   对外仍然是一个单文件。
#
# 用法:
#   pwsh -File build-embedded-apk.ps1

param(
    [string]$ApkPath = "",
    [string]$OutPath = ""
)

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ApkPath) { $ApkPath = Join-Path $here "build\aurora-ime.apk" }
if (-not $OutPath) { $OutPath = Join-Path $here "build\AuroraImeApk.h" }

if (-not (Test-Path $ApkPath)) {
    throw "APK 不存在: $ApkPath`n请先运行 build-ime-apk.ps1"
}

$bytes = [System.IO.File]::ReadAllBytes($ApkPath)
Write-Host "=== 内嵌 APK ===" -ForegroundColor Cyan
Write-Host "  输入: $ApkPath"
Write-Host "  大小: $($bytes.Length) 字节"

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("// 本文件由 build-embedded-apk.ps1 自动生成，请勿手工修改。")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// 源: aurora-ime.apk（Aurora 输入法桥接辅助包，已签名）")
[void]$sb.AppendLine("// 大小: $($bytes.Length) 字节")
[void]$sb.AppendLine("//")
[void]$sb.AppendLine("// 运行时由 InputBridge 释放到 /data/adb/aurora-ime-XXXXXX/aurora_ime.apk，")
[void]$sb.AppendLine("// 再通过 root app_process 加载。之所以内嵌，是为了让交付物保持单文件。")
[void]$sb.AppendLine("#pragma once")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("// 注意：本头文件不自带命名空间。它被 InputBridge.h 在 namespace aurora 内部")
[void]$sb.AppendLine("// 包含；若这里再开一层 aurora 会形成 aurora::aurora 嵌套。")
[void]$sb.AppendLine("static const unsigned char kAuroraImeApk[] = {")

$line = [System.Text.StringBuilder]::new()
for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($i % 16 -eq 0) {
        if ($line.Length -gt 0) {
            [void]$sb.AppendLine("    " + $line.ToString())
            [void]$line.Clear() | Out-Null
        }
    }
    [void]$line.AppendFormat("0x{0:x2},", $bytes[$i])
}
if ($line.Length -gt 0) {
    [void]$sb.AppendLine("    " + $line.ToString())
}

[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static const unsigned int kAuroraImeApkSize = $($bytes.Length)u;")

# 用无 BOM 的 UTF-8 写出（BOM 会让某些编译器把首行当成非法字符）
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($OutPath, $sb.ToString(), $utf8NoBom)

$outSize = (Get-Item $OutPath).Length
Write-Host "  输出: $OutPath"
Write-Host "  头文件: $outSize 字节" -ForegroundColor Green
Write-Host ""
Write-Host "完成。编译时把 ime-bridge/build 加入 include 路径即可。" -ForegroundColor Cyan
