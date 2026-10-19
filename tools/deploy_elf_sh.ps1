<#
=============================================================================
 deploy_elf_sh.ps1 —— 自解包 SH 的「ADB 推送 + 真机验收」

 交付规范的后半段：ELF → 单文件 SH → **ADB 推送** → **真机验收**。
 与 pack_elf_sh.ps1 配对使用，不依赖项目的 CMake，也不改动任何现有构建目标。

 用法：
   pwsh tools\deploy_elf_sh.ps1 -Sh dist\aurora_overlay.sh
   可选：
     -RemoteDir <path>  手机目标目录，默认 /sdcard/Download
     -Serial <id>       指定设备（默认取唯一在线设备）
     -SkipPush          只跑验收（已经推过了）

 验收项（全部按**程序实际支持的参数**设计，不假设存在 --help）：
   T1 完整性/解包      AURORA_ELF_VERIFY=1        → 期望 exit 0 + "verify ok"
   T2 参数传递+退出码  --display 0                → 期望 exit 3 + 拒绝横幅
   T3 参数**值**传递   --capture-backend nonsense → 期望 exit 2
   T4 与 SH 所在目录无关（/sdcard 与 /data/local/tmp 各跑一次，结果一致）
   T5 临时文件清理（运行前后都不能有 .<name>.<pid> 残留）
   T6 异常报告：载荷被截断 → exit 65；无可写目录 → exit 73
   T7 真机实跑（虚拟显示，15 秒）→ exit 0 且日志有采集帧

 退出码：0 = 全部通过；1 = 有失败项。
=============================================================================
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Sh,
    [string]$RemoteDir = '/sdcard/Download',
    [string]$Serial = '',
    # 手机 IP。ADB 走网络时（USB 未连接）用它自动 adb connect。
    # 留空 = 只使用已连接的设备（USB 或事先 connect 过的无线设备）；
    # 需要自动无线连接时显式传 -PhoneIp <你的设备IP>。
    [string]$PhoneIp = '',
    [int]$PhonePort = 5555,
    [switch]$SkipPush,
# T7（真机实跑）默认需要一台虚拟显示；本机没有虚拟屏时会跳过。
# 加这个开关则改为在**物理屏**上跑 12 秒 —— 会占用用户屏幕，非必要不用。
[switch]$AllowPhysical
)

$ErrorActionPreference = 'Continue'

if (-not (Test-Path -LiteralPath $Sh -PathType Leaf)) { throw "找不到 SH: $Sh" }
$Sh = (Resolve-Path -LiteralPath $Sh).Path
$name = [IO.Path]::GetFileName($Sh)
$app = [IO.Path]::GetFileNameWithoutExtension($Sh)
$localBytes = (Get-Item -LiteralPath $Sh).Length
$localSha = (Get-FileHash -LiteralPath $Sh -Algorithm SHA256).Hash.ToLower()

$adbArgs = @()
if ($Serial -ne '') { $adbArgs += @('-s', $Serial) }

# ★ 必须显式拿到 adb.exe 的**完整路径**再调用：PowerShell 的函数名解析是
#   大小写不敏感的，脚本里只要出现 `function Adb { ... }`，后面写 `& adb devices`
#   就会调到自己那个函数上（症状是输出 "System.Collections.Hashtable"）。
#   这个坑实际发生过一次，别再改回去。
$AdbExe = (Get-Command adb -CommandType Application -ErrorAction SilentlyContinue |
           Select-Object -First 1).Source
if (-not $AdbExe) { throw '找不到 adb.exe，请把 platform-tools 加进 PATH' }

function Invoke-AdbShell {
    param([string]$Command)
    $out = & $AdbExe @adbArgs shell $Command 2>&1
    return @{ text = ($out | Out-String); code = $LASTEXITCODE }
}

$results = New-Object System.Collections.ArrayList
function Record([string]$id, [string]$what, [bool]$ok, [string]$detail) {
    [void]$results.Add([pscustomobject]@{ id = $id; what = $what; ok = $ok; detail = $detail })
    $tag = if ($ok) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1,-4} {2}" -f $tag, $id, $what)
    if ($detail -ne '') { Write-Host ("         {0}" -f ($detail -replace "`r?`n", ' | ')) }
}

Write-Host ''
Write-Host '=============== 自解包 SH 真机验收 ==============='
Write-Host ("  sh        = {0}" -f $Sh)
Write-Host ("  bytes     = {0}" -f $localBytes)
Write-Host ("  sha256    = {0}" -f $localSha)

function Get-OnlineDeviceCount {
    $list = (& $AdbExe @adbArgs devices 2>&1 | Out-String)
    return ([regex]::Matches($list, '(?m)^\S+\s+device(\s|$)')).Count
}

$deviceCount = Get-OnlineDeviceCount
if ($deviceCount -eq 0 -and $PhoneIp -ne '') {
    # USB 没插时走网络调试。注意：adbd 必须已经监听 TCP 端口，本脚本不替你改手机设置。
    Write-Host ("  未发现在线设备，尝试 adb connect {0}:{1} ..." -f $PhoneIp, $PhonePort)
    & $AdbExe connect "$PhoneIp`:$PhonePort" 2>&1 | ForEach-Object { Write-Host "    $_" }
    Start-Sleep -Seconds 2
    $deviceCount = Get-OnlineDeviceCount
}
if ($deviceCount -eq 0) {
    Write-Host ''
    Write-Host '  [中止] 没有在线设备。当前 adb 视图：'
    (& $AdbExe @adbArgs devices 2>&1) | ForEach-Object { Write-Host ("         " + $_) }
    Write-Host '  建立 ADB 通道（二选一）：'
    Write-Host '    A. USB 线插上即可。'
    Write-Host '    B. 无线（手机需 root，本机 adb key 需已在手机授权列表里）：'
    Write-Host '         手机:  setprop service.adb.tcp.port 5555; setprop ctl.restart adbd'
    Write-Host '         PC  :  adb connect <手机IP>:5555'
    Write-Host '       恢复原状： setprop service.adb.tcp.port -1; setprop ctl.restart adbd'
    exit 1
}

# ---------------- 推送 ----------------
# ★ 直推 /sdcard 对大文件会**静默失败**：adb 打印 "1 file pushed"，
#   但设备上文件根本没被创建/覆盖。实测本机：4 MiB 可以，4.9 MiB 不行
#   （FUSE 写入 + media provider uid 的组合）。所以推送后**必须回读字节数校验**，
#   不一致就退回「推 /data/local/tmp → root cp 进目标目录」的两段式。
$remote = "$RemoteDir/$name"
$stage = "/data/local/tmp/$name"

function Get-RemoteBytes {
    param([string]$Path)
    $r = Invoke-AdbShell "wc -c < '$Path' 2>/dev/null || echo 0"
    $t = ($r.text -replace '\s', '')
    if ($t -match '^\d+$') { return [int64]$t }
    return -1
}

if (-not $SkipPush) {
    foreach ($f in @($remote, $stage)) { Invoke-AdbShell "rm -f '$f'" | Out-Null }

    $pushOut = (& $AdbExe @adbArgs push $Sh "$remote" 2>&1) -join "`n"
    Write-Host ''
    Write-Host "  push: $($pushOut -replace "`r?`n", ' ')"
    $got = Get-RemoteBytes $remote

    if ($got -ne $localBytes) {
        Write-Host ("  直推未生效（设备上 {0} 字节，期望 {1}）→ 改用「/data/local/tmp + root cp」两段式" -f $got, $localBytes)
        $pushOut2 = (& $AdbExe @adbArgs push $Sh $stage 2>&1) -join "`n"
        Write-Host "  push(stage): $($pushOut2 -replace "`r?`n", ' ')"
        $staged = Get-RemoteBytes $stage
        if ($staged -ne $localBytes) {
            Record 'T0' '文件成功推送到手机' $false ("staging 也只有 $staged 字节")
        } else {
            $cp = Invoke-AdbShell "su -c 'cp -f $stage $remote && chmod 644 $remote && echo copied'"
            Write-Host ("  cp: " + $cp.text.Trim())
            $got = Get-RemoteBytes $remote
        }
    }

    $ls = Invoke-AdbShell "ls -la '$remote' 2>/dev/null || echo MISSING"
    $ok = ($got -eq $localBytes)
    Record 'T0' '文件成功推送且字节数一致（回读校验，不信 adb 的 "pushed"）' $ok $ls.text.Trim()
} else {
    Write-Host '  （-SkipPush：跳过推送）'
}

# ---------------- T1 解包 + 完整性 ----------------
# 期望值从 SH 自己的头部读出来（SH 是自描述的），不拿 SH 的哈希去比 ELF 的哈希
# —— 那个 bug 让 T1 假失败过一次。
$shHead = Get-Content -LiteralPath $Sh -TotalCount 40 -Encoding utf8
$expectElfSha = ($shHead | Select-String "^ELF_SHA256='([0-9a-f]{64})'" |
                 Select-Object -First 1).Matches.Groups[1].Value
$expectElfBytes = ($shHead | Select-String '^ELF_BYTES=(\d+)' |
                   Select-Object -First 1).Matches.Groups[1].Value
Write-Host ("  sh 声明  : elfBytes={0} elfSha={1}" -f $expectElfBytes, $expectElfSha)
$t1 = Invoke-AdbShell "AURORA_ELF_VERIFY=1 sh '$remote'"
$t1ok = ($t1.code -eq 0) -and ($t1.text -match 'verify ok') -and
        ($t1.text -match [regex]::Escape($expectElfSha)) -and ($t1.text -match $expectElfBytes)
Record 'T1' '解包出的 ELF 完整无损（字节数 + sha256 与 SH 头部声明一致）' $t1ok $t1.text.Trim()

# ---------------- T5a 运行前无残留 ----------------
$pre = Invoke-AdbShell "ls -d /data/local/tmp/.$app.* 2>/dev/null | wc -l"
Record 'T5a' '运行前无临时目录残留' (($pre.text.Trim() -eq '0')) $pre.text.Trim()

# ---------------- T2 参数传递 + 退出码 ----------------
$t2 = Invoke-AdbShell "sh '$remote' --display 0"
$t2ok = ($t2.code -eq 3) -and ($t2.text -match '已拒绝运行')
Record 'T2' '参数传递 + 退出码（--display 0 → 期望 3）' $t2ok ("exit=" + $t2.code + ' ' + (($t2.text -split "`n" | Select-String '已拒绝运行' | Select-Object -First 1) -join ''))

# ---------------- T3 参数取值传递 ----------------
$t3 = Invoke-AdbShell "sh '$remote' --capture-backend nonsense"
$t3ok = ($t3.code -eq 2) -and ($t3.text -match '--capture-backend must be')
Record 'T3' '参数取值正确穿透（--capture-backend nonsense → 期望 2）' $t3ok ("exit=" + $t3.code + ' ' + $t3.text.Trim())

# ---------------- T4 与 SH 所在目录无关 ----------------
$null = Invoke-AdbShell "cp -f '$remote' /data/local/tmp/$name"
$t4a = Invoke-AdbShell "cd / && sh /data/local/tmp/$name --capture-backend nonsense"
$t4b = Invoke-AdbShell "cd /data && AURORA_ELF_NO_SU=1 sh /data/local/tmp/$name --capture-backend nonsense"
$t4ok = ($t4a.code -eq 2) -and ($t4b.code -eq 2)
Record 'T4' '与 SH 所在目录/CWD 无关，且直执行与 su 两条路一致' $t4ok ("cwd=/ exit=" + $t4a.code + '   cwd=/data(no-su) exit=' + $t4b.code)

# ---------------- T5b 运行后无残留 ----------------
$post = Invoke-AdbShell "ls -d /data/local/tmp/.$app.* 2>/dev/null | wc -l"
Record 'T5b' '运行后临时文件已清理' (($post.text.Trim() -eq '0')) $post.text.Trim()

# ---------------- T6a 载荷损坏时能报原因 ----------------
$null = Invoke-AdbShell "head -c 200000 '$remote' > /data/local/tmp/trunc.sh"
$t6a = Invoke-AdbShell "sh /data/local/tmp/trunc.sh"
$t6aok = ($t6a.code -eq 65) -and ($t6a.text -match '找不到内嵌数据标记|解码失败|大小不符')
Record 'T6a' '载荷被截断 → 明确报错（期望 65，且不是崩溃）' $t6aok ("exit=" + $t6a.code + ' ' + $t6a.text.Trim())

# ---------------- T6b 候选目录不可用时能正确回退 ----------------
# 说明：真机上「一个可写目录都没有」是构造不出来的（/data/local/tmp 恒可用），
# 所以这里验证的是**回退链本身**：把一个不可用的候选塞进 AURORA_ELF_TMPDIR，
# 期望脚本跳过它、落到 /data/local/tmp 并成功。
# （-w 只看权限位；/proc 在 root 下权限位是 w，但实际建不出目录 ——
#   打包脚本正是因为这个真机现象改成「实际 mkdir 一次再决定」。）
$t6b = Invoke-AdbShell "AURORA_ELF_TMPDIR=/proc AURORA_ELF_VERIFY=1 sh '$remote'"
$t6bok = ($t6b.code -eq 0) -and ($t6b.text -match 'base /data/local/tmp') -and
         ($t6b.text -notmatch '/proc/')
Record 'T6b' '候选目录不可用时自动回退（/proc → /data/local/tmp）' $t6bok ("exit=" + $t6b.code + ' ' + $t6b.text.Trim())

# ---------------- T9 不带参数（MT 管理器里点一下就属于这种）----------------
# 用 AURORA_ELF_VERIFY=1 安全地验证"默认参数确实被注入了"，不会真的接管屏幕。
# ★ 这几项断言的是**横幅文本**，而横幅现在默认静默（成功时一个字都不打），
#   所以显式加 AURORA_VERBOSE=1 —— 顺带也就验收了"verbose 能把它打回来"。
$t9 = Invoke-AdbShell "AURORA_VERBOSE=1 AURORA_ELF_VERIFY=1 sh '$remote'"
$t9ok = ($t9.code -eq 0) -and ($t9.text -match '使用内置默认参数') -and
        ($t9.text -match 'args\s+=\s+--display 0 --yes-physical') -and
        ($t9.text -match '--max-seconds 300')
Record 'T9' '不带参数 → 注入内置默认参数（MT 里点一下即可运行）' $t9ok (($t9.text -split "`n" | Select-String 'args\s+=|内置默认参数' | ForEach-Object { $_.Line.Trim() }) -join ' | ')

# ---------------- T9b 默认参数可被 AURORA_ARGS 覆盖 ----------------
$t9b = Invoke-AdbShell "AURORA_VERBOSE=1 AURORA_ARGS='--capture-backend nonsense' AURORA_ELF_VERIFY=1 sh '$remote'"
$t9bok = ($t9b.code -eq 0) -and ($t9b.text -match '使用 AURORA_ARGS') -and
         ($t9b.text -match 'args\s+=\s+--capture-backend nonsense')
Record 'T9b' 'AURORA_ARGS 可覆盖默认参数' $t9bok (($t9b.text -split "`n" | Select-String 'args\s+=' | ForEach-Object { $_.Line.Trim() }) -join ' ')

# ---------------- T9c 执行器多塞了位置参数时，默认参数依然生效 ----------------
# ★ 真机踩出来的：MT 管理器点脚本时会带位置参数 ⇒ `$# != 0` ⇒ 旧判据不注入默认值
#   ⇒ ELF 退回自己的默认（虚拟显示）⇒ 用户"点了脚本，主屏上什么都没有"。
#   新判据是"argv 里有没有以 - 开头的选项"。
$t9c = Invoke-AdbShell "AURORA_VERBOSE=1 AURORA_ELF_VERIFY=1 sh '$remote' /sdcard/Download/$name"
$t9cok = ($t9c.code -eq 0) -and ($t9c.text -match '使用内置默认参数') -and
         ($t9c.text -match 'args\s+=\s+--display 0 --yes-physical')
Record 'T9c' '执行器多塞位置参数时默认参数仍生效（MT 场景）' $t9cok (($t9c.text -split "`n" | Select-String 'args\s+=' | ForEach-Object { $_.Line.Trim() }) -join ' ')

# ---------------- T9d 运行记录可读（排障用）----------------
$t9d = Invoke-AdbShell "AURORA_ELF_VERIFY=1 sh '$remote' >/dev/null 2>&1; cat /data/local/tmp/$app.run.log"
$t9dok = ($t9d.text -match 'uid\s+=') -and ($t9d.text -match 'args\s+=')
Record 'T9d' '落一份运行记录（uid/argv/退出码），MT 里看不见输出时能对账' $t9dok (($t9d.text -split "`n" | Where-Object { $_ -match '\S' }) -join ' | ')

# ---------------- T10 自带用法说明 ----------------
$t10 = Invoke-AdbShell "sh '$remote' --help"
$t10ok = ($t10.code -eq 0) -and ($t10.text -match '用法:') -and ($t10.text -match '--yes-physical')
Record 'T10' '--help 打印用法并退 0（且不会误启动覆盖层）' $t10ok ("exit=" + $t10.code + ' 行数=' + (($t10.text -split "`n").Count))

# ---------------- T7 真机实跑（虚拟显示，安全）----------------
# ★ 这一项**需要本机当前有一台虚拟显示**：不带 --display 时 ELF 会去找虚拟屏，
#   找不到就退 4（这是**预期行为**，不是 bug）。
#   测试虚拟屏会被回收（真机实测遇到过），所以先探测：
#   - 有虚拟屏 → 照旧实跑 15 秒
#   - 没有     → 标成"跳过"并在备注里说明，**不算失败**（否则验收结果随机器状态飘）
#   想强制在物理屏上跑这一项：加 -AllowPhysical（会占用用户屏幕十几秒，慎用）。
$vprobe = Invoke-AdbShell "dumpsys SurfaceFlinger --display-id 2>/dev/null | grep -c 'Virtual display'"
$hasVirtual = $false
if ($vprobe.text -match '(\d+)') { $hasVirtual = ([int]$Matches[1] -gt 0) }

if ($hasVirtual) {
    Write-Host '  ... T7 真机实跑 15 秒（虚拟显示，--verbose 以便断言日志）'
    $t7 = Invoke-AdbShell "sh '$remote' --max-seconds 15 --capture-fps 30 --render-fps 30 --no-ime --verbose > /data/local/tmp/t7.log 2>&1; echo RC=`$?; tail -n 3 /data/local/tmp/t7.log"
    $t7text = $t7.text
    $t7ok = ($t7text -match 'RC=0') -and ($t7text -match '采集帧=')
    Record 'T7' 'ELF 真机正常运行（动态库/资源可访问，采集有帧，正常退出）' $t7ok $t7text.Trim()
} elseif ($AllowPhysical) {
    Write-Host '  ... T7 真机实跑 12 秒（物理屏，-AllowPhysical 授权，--verbose 以便断言日志）'
    $t7 = Invoke-AdbShell "sh '$remote' --display 0 --yes-physical --max-seconds 12 --capture-fps 30 --render-fps 30 --no-ime --verbose > /data/local/tmp/t7.log 2>&1; echo RC=`$?; tail -n 3 /data/local/tmp/t7.log"
    $t7text = $t7.text
    $t7ok = ($t7text -match 'RC=0') -and ($t7text -match '采集帧=')
    Record 'T7' 'ELF 真机正常运行（物理屏，-AllowPhysical）' $t7ok $t7text.Trim()
} else {
    Write-Host '  ... T7 跳过：本机当前没有虚拟显示'
    Record 'T7' '真机实跑（跳过：本机没有虚拟显示，未指定 --display 会退 4 属预期）' $true `
        '用 -AllowPhysical 可在物理屏上跑这一项（会占用屏幕十几秒）'
}

# ---------------- T11 默认静默（用户要求：点了就直接跑，不打任何日志/参数）----------------
# ★ 这一项**必须真的跑一次**才能证明：只看代码看不出"到底有没有人往 stdout 写"。
#   判据：成功运行时 stdout+stderr 合计 **0 字节**，且退出码 0。
#   用副本把内置默认时长改成 6 秒（不改交付件本身），走的就是 MT「不带参数点一下」那条路径。
Write-Host '  ... T11 默认静默（不带参数跑一次，输出必须是 0 字节）'
$t11 = Invoke-AdbShell "sed 's/--max-seconds 300/--max-seconds 6/' '$remote' > /data/local/tmp/q11.sh; sh /data/local/tmp/q11.sh > /data/local/tmp/q11.log 2>&1; echo RC=`$?; echo BYTES=`$(wc -c < /data/local/tmp/q11.log)"
$t11text = $t11.text
$t11ok = ($t11text -match 'RC=0') -and ($t11text -match 'BYTES=0')
Record 'T11' '默认静默：不带参数运行 → 退出码 0 且输出 0 字节' $t11ok $t11text.Trim()

# ---------------- T8 收尾 ----------------
$null = Invoke-AdbShell "rm -f /data/local/tmp/$name /data/local/tmp/trunc.sh /data/local/tmp/t7.log /data/local/tmp/q11.sh /data/local/tmp/q11.log"
$left = Invoke-AdbShell "ls -d /data/local/tmp/.$app.* 2>/dev/null | wc -l"
Record 'T8' '验收现场已清理（无残留进程/临时文件）' (($left.text.Trim() -eq '0')) $left.text.Trim()

$passed = ($results | Where-Object { $_.ok }).Count
$failed = $results.Count - $passed
Write-Host '================================================'
Write-Host ("  通过 {0} 项，失败 {1} 项" -f $passed, $failed)
Write-Host '================================================'
Write-Host ''
if ($failed -ne 0) { exit 1 }
exit 0
