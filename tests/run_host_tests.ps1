# ===========================================================================
#  动效模块的**宿主机**回归（不需要手机）
#
#  这批模块（Motion.h / LiquidMorph.h / TouchHighlight.h / ControlCenter.h）
#  是纯逻辑、零 Android 依赖，可以直接在本机编译运行。
#  设备电量紧张、设备被拿走、或 外部工具不可用时，用这个脚本做回归。
#
#  用法:  pwsh -File tests\run_host_tests.ps1
#  退出码: 0 = 全部通过；1 = 有失败项
#
#  ★ 不加 -DNDEBUG —— 测试全靠 assert，定义了它断言会全部消失。
#  ★ -ffp-contract=off 与设备端编译参数保持一致，避免 FMA 收缩让浮点结果
#    与真机不同（本项目为此类差异排查过）。
#  ★★ 产物必须输出到**纯 ASCII 路径**：本仓库根目录是 `D:\纯c图形基座`，
#     MinGW 的 ld.exe 建不出中文路径下的输出文件（报 "cannot open output file"，
#     路径显示成乱码）。源码路径中文没问题，只有输出路径不行。
# ===========================================================================

$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$cxx = 'g++'
$out = Join-Path $env:TEMP 'aurora-host-tests'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$flags = @('-std=c++20', '-O1', '-ffp-contract=off', '-Wall', '-Wextra',
           '-Iapp\src\main\cpp',
           # DisplayPick.h 住在 root-host\elf（和 aurora_overlay.cpp 同目录）。
           # ★ 它是有意做成"纯字符串逻辑、零 Android 依赖"的 —— 就是为了能进这套离线回归。
           '-Iroot-host\elf', '-Iapp\src\main\cpp\third_party\imgui',
           # ytbl 的组件实现（Toggle/Slider/PillButton/TabBar/…）在
           # third_party/liquidglass/ytbl 下，头文件按 "GlassInternal.h" 直接互相包含。
           '-Iapp\src\main\cpp\third_party\liquidglass',
           '-Iapp\src\main\cpp\third_party\liquidglass\ytbl')

$tests = @('DampedDragTest', 'LiquidMorphTest', 'TouchHighlightTest',
           'LiquidButtonTest', 'ControlCenterTest', 'MouseEdgeSequencerTest',
           'SelfFeedbackEraseTest', 'CapturePipelineTest',
           'InputRouterTest', 'GestureRouterTest', 'OverlayTouchRegionTest',
           'AndroidStrongReferenceTest', 'SceneCompositeTest', 'RenderBackendTest',
           'DisplayPickTest', 'DisplayGeometryTest', 'UiLayoutTest', 'NativeWindowProbeTest', 'RenderDamageTest', 'PanelCallbackBatchTest',
           # 崩溃取证的 maps 解析：远端机器（朋友的手机）崩了只有这段信息能带回来，
           # 解析错会给出**错误的模块名** —— 比没有更糟，所以必须有离线回归。
           'CrashReportTest',
           # 录屏开关的参数拼装与默认文件名（纯逻辑；fork/exec 那半在设备上验）
           'ScreenRecorderTest',
           # 二级窗口手势内核：点击 vs 拖动阈值、归属优先级、滚动夹紧、惯性
           'PanelGesturesTest',
           # 右下角长按缩放：120ms 阈值、隐形判定区、位移容差、尺寸夹紧、阻尼弹簧
           'ResizeGestureTest',
           # ★ 虚线预览缩放（模式 B）：状态机六态、圆角虚线按弧长排布、
           #   松手后的弹簧过渡（真实帧时间/精确落地/兜底时限）、填充包络与前沿几何
           'ResizePreviewTest',
           # ★ 调试器参数行的**响应式布局**：滑块随窗口变宽、窄屏换双行、
           #   重置按钮永远完整可见（"重置被裁切"的回归防护就在这里）
           'ParamRowLayoutTest',
           # ★ 输入法弹层避让（adjustPan 语义）：多帧稳定不飘、屏幕顶夹紧、
           #   键盘收起后必须回原位
           'ImeAvoidTest',
           # ★ 输入法回填换算：正常打字必须是"纯追加 0 退格"、退格必须真的产生
           #   退格次数（用户报的"只能打字、不能删除"就是这条缺了）、
           #   中文/emoji 按 UTF-8 字符数退而不是按字节退
           'ImeTextDiffTest',
           # ★ 二级窗口 Demo 的派生量公式与同行判据（本轮回归修复的防护）：
           #   实验场圆角必须带 0.5（少了就变成"接近正圆"）、
           #   按钮同行判据必须用真实右边界（用 GetContentRegionAvail 会恒真）
           'GlassDemoDeriveTest',
           # 角部缩放手柄的几何与动画：连续圆弧（不是 L 形长杆）、圆端帽、
           # 密度无关、展开/收回可中断、掉帧安全
           'CornerHandleGeometryTest',
           # 触觉反馈的纯逻辑：只在"未激活→激活"放行一次、等待/拖动/取消都不震、
           # 没有平台实现时安全降级（平台实现是 Binder，只能在真机验）
           'HapticsTest',
           # ★★ Liquid Glass Inspector（液态玻璃实时调试工作台）的五项回归：
           #   1) 注册表与参数总线：动态注册/注销、目标隔离、值域夹紧、合并队列、订阅
           #   2) 参数持久化：版本化 JSON、v1 迁移、非法值夹紧与报告、原子保存、多预设
           #   3) 撤销/重做：拖一次滑块 = 一步、批处理 = 一步、回放不产生新记录
           #   4) 配方（参数 → PanelStyle）：默认值复现原常量、改一项只改对应字段
           #   5) 绑定（注册表 → 真实渲染字段）：写按钮 1 不动按钮 2、
           #      整窗口不覆盖子组件、帧边界生效、父子层隔离
           'GlassRegistryTest', 'GlassPresetTest', 'GlassHistoryTest',
           'GlassRecipeTest', 'GlassBindingTest',
           # ★ 采样区留白（性能优化）：普通面板只为模糊留白（折射恒朝内）、
           #   背景拉伸按自己需要的量、液滴保持原样；并断言"至少覆盖 3σ+1"
           'CapturePaddingTest',
          # ★ 调度优化的设备拓扑归纳（纯逻辑）：凭什么把渲染线程绑到那几个核 ——
          #   用真实机型（测试机 / 骁龙 8 Elite 的 6×3.53G+2×4.32G）当输入做断言
          'ThreadTuningTest')

# 有几个测试不是纯头文件模块 —— 它们要链接对应的 .cpp 实现。
# 动效那 5 个是 header-only（纯逻辑），这两个不是。
$glassDir = 'app\src\main\cpp\glass'
$ytblDir = 'app\src\main\cpp\third_party\liquidglass\ytbl'
# Liquid Glass Inspector 的纯逻辑层（注册表 / 参数模型 / JSON / 预设 / 历史）
$glassCore = @(
    "$glassDir\GlassRegistry.cpp", "$glassDir\GlassParam.cpp", "$glassDir\GlassJson.cpp",
    "$glassDir\GlassPreset.cpp", "$glassDir\GlassHistory.cpp")
# 组件场景（参数存储 + 组件实例）与"参数 → PanelStyle"的配方。
# ★ 这几个会连带 ytbl 的组件实现，因此还需要 tests/host_glass_stubs.cpp
#   提供 GL 侧的空壳（详见该文件顶部：替身只满足链接，永远不会被执行）。
$glassScene = @(
    "$glassDir\GlassScene.cpp",
    "$ytblDir\GlassRecipes.cpp", "$ytblDir\Toggle.cpp", "$ytblDir\Slider.cpp",
    "$ytblDir\PillButton.cpp", "$ytblDir\TabBar.cpp", "$ytblDir\ColorAdapter.cpp",
    "$ytblDir\Runtime.cpp", "$ytblDir\SpringSim.cpp")
$glassFull = $glassScene + @(
    "$ytblDir\PopupMenu.cpp", "$ytblDir\Dialog.cpp", "$ytblDir\SpineSolver.cpp",
    "$ytblDir\BezierPath.cpp", "$ytblDir\Icons.cpp",
    'tests\host_glass_stubs.cpp')

$extraSources = @{
    'PanelCallbackBatchTest' = @('app\src\main\cpp\third_party\imgui\imgui.cpp',
                                'app\src\main\cpp\third_party\imgui\imgui_draw.cpp',
                                'app\src\main\cpp\third_party\imgui\imgui_tables.cpp',
                                'app\src\main\cpp\third_party\imgui\imgui_widgets.cpp')
    'InputRouterTest'   = @('app\src\main\cpp\InputRouter.cpp')
    'GestureRouterTest' = @('app\src\main\cpp\GestureRouter.cpp',
                            'app\src\main\cpp\InputRouter.cpp')
    'RenderBackendTest' = @('app\src\main\cpp\RenderBackend.cpp')
    # 触觉反馈的纯逻辑层有 .cpp 实现（平台实现是 Binder，在宿主 ELF 里）
    'HapticsTest'       = @('app\src\main\cpp\Haptics.cpp')
    'GlassRegistryTest' = $glassCore
    'GlassPresetTest'   = $glassCore
    'GlassHistoryTest'  = $glassCore
    # 配方回归只需要"参数 → PanelStyle"这一段：GlassRecipes.cpp + 主题色 + 运行时纯函数。
    # ★ 它是**唯一**不链接组件 draw() 的配方测试 —— 因此也不受 GL 空壳影响。
    #   （组 3 会**真的构造**两个 Toggle / PillButton 实例来验证参数互不影响，
    #     所以这两个组件的实现文件必须链进来；SpringSim 是它们的动效内核。）
    'GlassRecipeTest'   = @(
        "$ytblDir\GlassRecipes.cpp", "$ytblDir\ColorAdapter.cpp", "$ytblDir\Runtime.cpp",
        "$ytblDir\Toggle.cpp", "$ytblDir\PillButton.cpp", "$ytblDir\SpringSim.cpp",
        'tests\host_glass_stubs.cpp')
    # 绑定回归要把注册表与真实字段接起来（含 PopMenu / Modal 的构造）
    'GlassBindingTest'  = @('app\src\main\cpp\GlassTargets.cpp') + $glassCore + $glassFull
}

$passed = 0
$failed = 0
$skipped = 0

Write-Output ''
Write-Output '=============== 动效模块宿主回归 ==============='

foreach ($t in $tests) {
    $src = Join-Path $root "tests\$t.cpp"
    if (-not (Test-Path $src)) {
        Write-Output ("  [跳过] {0}（源文件不存在）" -f $t)
        $skipped++
        continue
    }
    $exe = Join-Path $out "$t.exe"

    $sources = @($src)
    if ($extraSources.ContainsKey($t)) {
        foreach ($e in $extraSources[$t]) {
            $full = Join-Path $root $e
            if (Test-Path $full) { $sources += $full }
            else { Write-Output ("      [警告] 缺少实现文件 {0}" -f $e) }
        }
    }

    $buildOut = & $cxx @flags -o $exe @sources 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Output ("  [编译失败] {0}" -f $t)
        $buildOut | Select-Object -First 12 | ForEach-Object { Write-Output ("      " + $_) }
        $failed++
        continue
    }

    $runOut = & $exe 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Output ("  [运行失败] {0}  exit={1}" -f $t, $LASTEXITCODE)
        $runOut | Select-Object -First 6 | ForEach-Object { Write-Output ("      " + $_) }
        $failed++
        continue
    }

    $line = ($runOut | Where-Object { $_ -match '\S' } | Select-Object -First 1)
    if (-not $line) { $line = '（无输出 ⇒ 全部断言通过）' }
    Write-Output ("  [通过] {0,-20} {1}" -f $t, $line)
    $passed++
}

# ---- UI 字符集检查（不需要编译器，但必须跑）--------------------------------
# 判据：**UI 字符串里出现的每个非 ASCII 字符，内嵌字体里都要有字形**。
# 为什么单列一项：ImGui 图集只烘焙申请过的码点，没申请的字符画出来就是问号 ——
# 真机上 `● ★ →` 就这么变成问号的（一点都不生僻，只是没人申请）。
# 图集范围已经由 UiGlyphSet.h 兜住；这一项守的是另一半：
# **字体文件里根本没有那个字形**（例如 ✓ ✗ ⇒）—— 那必须换字符，改范围也没用。
$genScript = Join-Path $root 'tools\gen_ui_glyphs.py'
if (Test-Path $genScript) {
    $glyphOut = & python $genScript --check 2>&1
    if ($LASTEXITCODE -eq 0) {
        $summary = ($glyphOut | Where-Object { $_ -match '字体里有' } | Select-Object -First 1)
        if ($summary -is [array]) { $summary = $summary -join ' ' }
        Write-Output ("  [通过] {0,-20} {1}" -f 'UiGlyphCheck', (("$summary") -replace '^\s+', ''))
        $passed++
    } else {
        Write-Output ("  [失败] {0,-20} UI 里有字符在字体中没有字形（必须换字符）" -f 'UiGlyphCheck')
        $glyphOut | Where-Object { $_ -match 'U\+' } | Select-Object -First 8 |
            ForEach-Object { Write-Output ("      " + $_.Trim()) }
        $failed++
    }
}

Write-Output '=============================================='
Write-Output ("  通过 {0} 项，失败 {1} 项，跳过 {2} 项" -f $passed, $failed, $skipped)
Write-Output '=============================================='
Write-Output ''

if ($failed -ne 0) { exit 1 }
exit 0
