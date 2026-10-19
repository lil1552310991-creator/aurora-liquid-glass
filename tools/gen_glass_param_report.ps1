<#
  生成《液态玻璃参数清单》（docs/GLASS_INSPECTOR_PARAMETERS.md）

  ★ 为什么要有这个脚本：报告里的"完整参数清单"和"参数 ↔ 代码位置对照"必须是
    **从组件注册表导出的**，不能手写 —— 手写的那份从改第一行绑定开始就过期了。
    这个脚本每次重新编译那个宿主导出器并覆盖文档，清单永远等于当前代码。

  用法：pwsh -File tools/gen_glass_param_report.ps1
  产物：docs/GLASS_INSPECTOR_PARAMETERS.md
#>

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cxx = 'g++'
# ★ 产物必须落在纯 ASCII 路径：仓库根是中文路径，MinGW 的 ld 在那里建不出输出文件。
$out = Join-Path $env:TEMP 'aurora-glass-report'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$exe = Join-Path $out 'glass_param_report.exe'
$md  = Join-Path $root 'docs\GLASS_INSPECTOR_PARAMETERS.md'

$ytbl = 'app\src\main\cpp\third_party\liquidglass\ytbl'
$flags = @('-std=c++20', '-O1', '-ffp-contract=off', '-w',
           '-Iapp\src\main\cpp',
           '-Iapp\src\main\cpp\third_party\liquidglass',
           "-I$ytbl")

$sources = @(
    'tools\glass_param_report.cpp',
    'tests\host_glass_stubs.cpp',
    'app\src\main\cpp\GlassTargets.cpp',
    'app\src\main\cpp\glass\GlassRegistry.cpp',
    'app\src\main\cpp\glass\GlassParam.cpp',
    'app\src\main\cpp\glass\GlassScene.cpp',
    "$ytbl\GlassRecipes.cpp", "$ytbl\Toggle.cpp", "$ytbl\Slider.cpp",
    "$ytbl\PillButton.cpp", "$ytbl\TabBar.cpp", "$ytbl\PopupMenu.cpp",
    "$ytbl\Dialog.cpp", "$ytbl\SpineSolver.cpp", "$ytbl\BezierPath.cpp",
    "$ytbl\Icons.cpp", "$ytbl\ColorAdapter.cpp", "$ytbl\Runtime.cpp",
    "$ytbl\SpringSim.cpp")

Write-Output '编译参数清单导出器…'
$build = & $cxx @flags -o $exe @sources 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Output '编译失败：'
    $build | Select-Object -First 20 | ForEach-Object { Write-Output ('  ' + $_) }
    exit 1
}

Write-Output '导出清单…'
$report = & $exe 2>&1
if ($LASTEXITCODE -ne 0) {
    Write-Output "导出器运行失败（exit=$LASTEXITCODE）"
    $report | Select-Object -First 10 | ForEach-Object { Write-Output ('  ' + $_) }
    exit 1
}

# 统一成 LF：Markdown 在 Windows 上被写成 CRLF 会让 diff 噪音很大
$text = ($report -join "`n") + "`n"
[System.IO.File]::WriteAllText($md, $text, (New-Object System.Text.UTF8Encoding($false)))

$lines = ($text -split "`n").Count
Write-Output ("已写入 {0}（{1} 行）" -f $md, $lines)
