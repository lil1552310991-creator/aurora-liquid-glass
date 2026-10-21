// =============================================================================
//  组件注册表实现（参数清单见 GlassTargets.h 顶部的三条原则）
//
//  分页顺序（order）：整窗口 0 → 卡片 100 → 按钮 200.. → 开关 300.. →
//  进度条 400.. → 导航栏 500 → 芯片 600.. → 弹出菜单 700 → 弹窗 800 → 实验场 900
//  子层（.track / .thumb / .background / .indicator）紧跟在父目标之后。
// =============================================================================
#include "GlassTargets.h"

#include "ResizePreview.h"   // ★ 窗口缩放方式 + 虚线预览/填充动画参数（纯逻辑层）

#include "glass/GlassRegistry.h"
#include "glass/GlassScene.h"

#include <cstdio>

namespace aurora {
namespace glass {
namespace {

constexpr char kYtblUI[] = "app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h";
constexpr char kConfig[] = "app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h";
constexpr char kSceneSrc[] = "app/src/main/cpp/glass/GlassScene.cpp";

// Rgba（ytbl 的上游类型，成员 r/g/b/a）→ 可当作 float[4] 用的指针。
//  ★ 调试层的颜色参数绑定的是一个**数组**，所以这里必须显式确认 Rgba 就是
//    4 个连续 float（下面两条 static_assert 会在任何平台上先把这件事钉死，
//    而不是靠"反正编译器会排成这样"的假设）。
// 边光的模式名表（ImGui::Combo 用 '\0' 分隔）
constexpr char kEdgeModes[] = "跟随动画\0Gradient\0Ambient\0Plain\0Contour\0关闭\0";
constexpr char kSurfaceModes[] = "无\0Hue\0Overlay\0";

// -----------------------------------------------------------------------------
// 边光参数（所有带边光的组件共用这一段，避免复制粘贴出错）
// -----------------------------------------------------------------------------
void BindEdge(TargetBuilder& b, EdgeTune& edge, int maxMode) {
    b.Group("边光");
    b.Enum("edgeMode", "边光模式", "edgeMode", &edge.mode, 0, maxMode, kEdgeModes,
           "0=跟随组件的按压动画（此时强度是增益）；1=Gradient 2=Ambient 3=Plain "
           "4=Contour；5=关闭",
           ParamCost::Uniform).Source(kYtblUI, "EdgeTune::mode");
    b.Float("edgeWidthDp", "边光宽度", "width", &edge.widthDp, 0.0f, 6.0f, "dp",
            "Highlight.width；Kyant/Gradient 默认 0.5dp，Ambient 用 0.3333dp，"
            "Contour 用 0.6dp（GlassConfig.h 的 EDGE_*_WIDTH）",
            0.05f).Source(kConfig, "EDGE_*_WIDTH");
    b.Float("edgeSoftnessDp", "边光柔度", "blurRadius", &edge.softnessDp, 0.0f, 4.0f, "dp",
            "Highlight.blurRadius；默认 0.25dp = 宽度的一半",
            0.05f).Source(kConfig, "EDGE_*_SOFTNESS");
    b.Float("edgeAlpha", "边光强度", "alpha", &edge.alpha, 0.0f, 1.0f, "×",
            "模式为 0 时是**增益**（1.0 = 原配方）；其它模式是绝对 alpha",
            0.01f).Source(kYtblUI, "EdgeLight::alpha");
    b.Float("edgeAngleDegrees", "光照角度", "angle", &edge.angleDegrees, -180.0f, 180.0f,
            "deg",
            "45° 方向光。★ Ambient 分支的 shader 根本不读这个 uniform，"
            "所以选 Ambient 时它不生效（工作台会灰显说明）",
            0.5f).Source(kYtblUI, "EdgeLight::angleDegrees");
    b.Float("edgeFalloff", "光照衰减", "falloff", &edge.falloff, 0.0f, 8.0f, "×",
            "pow(|dot(grad,L)|, falloff) 的指数；Ambient 同样不读",
            0.05f).Source(kYtblUI, "EdgeLight::falloff");
    b.Bool("edgePresetTint", "用主题预设色", "presetTint", &edge.usePresetTint,
           "打开时用 ytbl 的预设色（随主题自适应）；关掉才使用下面的颜色")
        .Source(kYtblUI, "EdgeLight::color");
    b.Color("edgeTint", "边光颜色", "color", edge.tint,
            "RGBA。★ 只有关掉「用主题预设色」后才会被渲染读取")
        .Source(kYtblUI, "EdgeLight::color");
}

// -----------------------------------------------------------------------------
// 通用面板配方（卡片 / 6 枚芯片 / 实验场）
//   withFallbackTheme: 是否登记"兜底色跟随主题"开关（窗口/卡片/芯片都有）
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// 整窗口
// -----------------------------------------------------------------------------
void BindWindowTarget(Registry& registry, GlassScene& scene) {
    TargetDesc desc;
    desc.id = "secondary_window";
    desc.name = "整窗口";
    desc.type = "Window";
    desc.window = "secondary";
    desc.order = 0;
    desc.note = "二级窗口的外壳玻璃：整块圆角面板 + 投影。★ 它的参数**不会**复制给子组件";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    WindowTune& tune = scene.window;

    b.Group("外观");
    b.Bool("glassEnabled", "整窗液态玻璃", "glassEnabled", &tune.glassEnabled,
           "关掉后窗口不提交玻璃面板（只剩内容）", ParamCost::Pipeline)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_WindowGlassEnabled");
    b.Bool("opacityOn", "调节窗口透明度", "adjustOpacity", &tune.opacityOn,
           "原实现：开关**关闭**时表面 alpha 取 0（= 最透明）")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                "s_WindowOpacityEnabled");
    b.Float("opacity", "窗口透明度", "opacity", &tune.opacity, 0.05f, 0.85f, "×",
            "仅在开启上面那个开关时生效", 0.01f, ParamCost::Uniform,
            "仅在开启「调节窗口透明度」时生效")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_WindowOpacity");
    b.Float("alpha", "整体 alpha", "alpha", &tune.alpha, 0.0f, 1.0f, "×",
            "PanelStyle::alpha（会乘到边光/内阴影/表面上）", 0.01f)
        .Source(kYtblUI, "PanelStyle::alpha");

    b.Group("几何");
    b.Float("cornerDp", "窗口圆角", "corner", &tune.cornerDp, 0.0f, 96.0f, "dp",
            "★ 这个值同时驱动三处：外壳面板圆角、**内容圆角遮罩**（SDF 裁剪）、"
            "触摸形状半径 —— 所以它必须始终是同一个来源",
            0.5f, ParamCost::EffectChain)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "kWindowCornerDp");

    b.Group("折射 / 模糊");
    b.Float("blurDp", "模糊半径", "blur", &tune.blurDp, 0.0f, 10.0f, "dp",
            "ytbl 上限 BLUR_RADIUS_MAX_DP = 10dp；★ 帧边界生效（会改变 gather 采样）",
            0.1f, ParamCost::Pipeline)
        .Source(kConfig, "BLUR_RADIUS_MAX_DP").Deferred(&tune.blurAppliedDp);
    b.Float("lensHeightDp", "折射高度", "lensHeight", &tune.lensHeightDp, 0.0f, 200.0f, "dp",
            "kyant 的 lens(refractionHeight)", 0.5f).Source(kConfig, "LENS_REACH_MAX");
    b.Float("lensAmountDp", "折射强度", "lensAmount", &tune.lensAmountDp, 0.0f, 200.0f, "dp",
            "kyant 的 lens(refractionAmount)", 0.5f).Source(kConfig, "LENS_REACH_MAX");
    b.Float("dispersion", "色散强度", "dispersion", &tune.dispersion, 0.0f, 2.0f, "×",
            "上限 SPECTRAL_MAX = 2.0", 0.01f, ParamCost::Uniform, "仅在启用色散时生效")
        .Source(kConfig, "SPECTRAL_MAX");
    b.Bool("chromaticOn", "启用色散", "chromatic", &tune.chromaticOn, "")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_WindowChromatic");
    b.Bool("depthOn", "景深折射", "depthEffect", &tune.depthOn,
           "原实现写死 depthAmount = 1.0（窗口是唯一默认开景深的组件）")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_WindowDepth");

    b.Group("色彩 / 表面");
    b.Bool("colorBoost", "鲜艳度增强", "vibrancy", &tune.colorBoost,
           "原实现写死 true").Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                     "windowStyle");
    b.Float("brightness", "亮度", "brightness", &tune.brightness, -1.0f, 1.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::brightness");
    b.Float("contrast", "对比度", "contrast", &tune.contrast, 0.0f, 2.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::contrast");
    b.Float("saturation", "饱和度", "saturation", &tune.saturation, 0.0f, 3.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::saturation");
    b.Color("surfaceLight", "浅色主题表面色", "surfaceLight", tune.surfaceLight,
            "原实现：glassIsLight() 时用 (0.96,0.97,0.99)").Source(kSceneSrc, "WindowTune");
    b.Color("surfaceDark", "深色主题表面色", "surfaceDark", tune.surfaceDark,
            "原实现：深色时用 (0.10,0.11,0.14)").Source(kSceneSrc, "WindowTune");
    b.Enum("surfaceMode", "表面模式", "surfaceMode", &tune.surfaceMode, 0, 2, kSurfaceModes,
           "原实现写死 Overlay").Source(kYtblUI, "SurfaceMode");
    b.Bool("fallbackFollowTheme", "兜底色跟随主题", "followTheme", &tune.fallbackFollowTheme,
           "打开时用 ImGui 页面底色（工作台把下面的颜色标成只读）").Source(kSceneSrc, "WindowTune");
    b.Color("fallback", "兜底色", "fallback", tune.fallback,
            "没有背景可采时用的颜色").Source(kYtblUI, "PanelStyle::fallback");

    BindEdge(b, tune.edge, 5);

    b.Group("阴影");
    b.Bool("shadowOn", "启用投影", "shadowOn", &tune.shadowOn, "").Source(kSceneSrc, "WindowTune");
    b.Float("shadowRadiusDp", "投影半径", "radius", &tune.shadowRadiusDp, 0.0f, 96.0f, "dp",
            "原实现 DropShadow(24.0f, …)", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "DropShadow::radiusDp");
    b.Float("shadowOffsetXDp", "投影偏移 X", "offsetX", &tune.shadowOffsetXDp, -64.0f, 64.0f,
            "dp", "", 0.5f, ParamCost::EffectChain).Source(kYtblUI, "DropShadow::offsetXDp");
    b.Float("shadowOffsetYDp", "投影偏移 Y", "offsetY", &tune.shadowOffsetYDp, -64.0f, 64.0f,
            "dp", "原实现 = radius / 6 = 4dp", 0.5f, ParamCost::EffectChain)
        .Source(kConfig, "SHADOW_OFFSET_DIVISOR");
    b.Float("shadowAlpha", "投影强度", "alpha", &tune.shadowAlpha, 0.0f, 1.0f, "×",
            "原实现 0.14（写进 tint 的 alpha 字节）", 0.01f).Source(kSceneSrc, "WindowTune");
    b.Float("insetAlpha", "内阴影强度", "insetAlpha", &tune.insetAlpha, 0.0f, 1.0f, "×",
            "常态 0（缩放激活反馈时才由 ComputeResizeActivation 写入，见 "
            "docs/RESIZE_ACTIVATION_FEEDBACK.md）", 0.01f, ParamCost::EffectChain)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "ScopedPanelClip");
    b.Float("insetRadiusDp", "内阴影半径", "insetRadius", &tune.insetRadiusDp, 0.0f, 64.0f, "dp",
            "", 0.5f, ParamCost::EffectChain).Source(kYtblUI, "InsetShadow::radiusDp");

    b.Group("背景采样");
    b.Bool("backdropOn", "启用背景采样", "backdropOn", &tune.backdropOn,
           "整个窗口的背景来源开关（sceneOn）。关掉后窗口只剩表面色 + 边光",
           ParamCost::Pipeline)
        .Source(kYtblUI, "PanelStyle::backdropOn");

    b.Group("能力缺口（只读）");
    b.Unsupported("gapCombinedBackdrop", "多背景源合成", "CombinedBackdrop",
                  "G7：Kyant 的 rememberCombinedBackdrop 把多个来源按顺序画到一起。"
                  "本机是「进程内 Z 序合成 + 单一采集源」，见 docs/GLASS_ZORDER_COMPOSITOR.md；"
                  "要做到 Kyant 那样需要采集层支持多次采样");
    b.Unsupported("gapExposure", "曝光调整 exposureAdjustment", "exposureAdjustment",
                  "参考项目 **2.0.1 已移除**该 API（只在 1.0.6 存在：scale = 2^(ev/2.2) 的 "
                  "ColorMatrix）。本实现的 tone 控制只有 brightness/contrast/saturation → "
                  "不提供，免得做成「看起来能调、其实只是亮度」的假控件");
    b.Unsupported("gapGamma", "伽马调整 gammaAdjustment", "gammaAdjustment",
                  "同上：参考项目 2.0.1 已移除，只有 1.0.6 的 pow(color.rgb, power)。"
                  "接入需要在片元着色器里新增一个 uniform 与一次 pow");
}
// 按用户要求不注册进调试器（弹窗没有专属分页），但函数保留以便随时恢复。
[[maybe_unused]] void BindDialogTarget(Registry& registry, GlassScene& scene) {
    TargetDesc desc;
    desc.id = "secondary_window.dialog";
    desc.name = "液态弹窗";
    desc.type = "Modal";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 800;
    desc.note = "Modal 只公开 glassBlurDp 一个玻璃字段，其余配方写在 Dialog.cpp 里";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    b.Group("玻璃");
    b.Float("glassBlurDp", "弹窗玻璃模糊", "glassBlur", &scene.modal.glassBlurDp, 0.0f, 10.0f,
            "dp", "DIALOG_GLASS_BLUR_DP = 10（Modal 唯一公开的玻璃字段）", 0.1f,
            ParamCost::Pipeline).Source(kConfig, "DIALOG_GLASS_BLUR_DP");
    b.Group("触发按钮");
    b.Float("buttonWidthDp", "按钮宽度", "width", &scene.stage.dialogButtonWidthDp, 40.0f, 420.0f,
            "dp", "原 px(180)", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                "s_DialogButtonWidthDp");
    b.Float("buttonHeightDp", "按钮高度", "height", &scene.stage.dialogButtonHeightDp, 24.0f,
            160.0f, "dp", "原 px(48)", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                "s_DialogButtonHeightDp");
    b.Group("动效");
    b.Bool("animationFromButton", "从按钮展开", "fromButton", &scene.stage.dialogAnimationFromButton,
           "关掉 = 从中心玻璃球展开（原实现的两个复选框）")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                "s_DialogAnimationFromButton");
    b.Group("能力缺口（只读）");
    b.Unsupported("gapDialogRecipe", "弹窗内部玻璃配方", "dialogGlassRecipe",
                  "G14：透镜/闪光/边光/阴影/圆环/尺寸都是 Dialog.cpp 的编译期常量"
                  "（DIALOG_LENS_* / DIALOG_EDGE_* / DIALOG_SHADOW_* / DIALOG_RING_*），"
                  "Modal 没有对应字段");
}

void BindGapTarget(Registry& registry) {
    TargetDesc desc;
    desc.id = "secondary_window.capability_gaps";
    desc.name = "能力缺口总表";
    desc.type = "Gaps";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 1000;
    desc.note = "参考项目有、本实现没有（或不适合实时调）的能力 —— 只读 + 原因，不做假控件";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    b.Group("参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1）");
    b.Unsupported("gapCombinedBackdrop", "CombinedBackdrop 多背景源", "CombinedBackdrop",
                  "参考项目用 rememberCombinedBackdrop 把多个来源按顺序画进同一画布。"
                  "本机是「进程内 Z 序合成 + 单一采集源」（docs/GLASS_ZORDER_COMPOSITOR.md），"
                  "要做到等价需要在采集层支持多次采样并合并");
    b.Unsupported("gapLayerBlock", "layerBlock 任意变换", "layerBlock",
                  "参考项目的 layerBlock 支持 scale/rotationZ/transformOrigin 并会走"
                  "InverseLayerScope 逆变换；ytbl 的面板只接受轴对齐矩形 + uStretchScale");
    b.Unsupported("gapRuntimeShader", "runtimeShaderEffect 自定义着色器", "runtimeShaderEffect",
                  "参考项目要 API 33 的 RuntimeShader；本机是预编译 GLES 程序，"
                  "接入需要把 shader 源做成运行时可注入（当前 shaders 是编译期字符串常量）");
    b.Unsupported("gapExposure", "exposureAdjustment", "exposureAdjustment",
                  "只在参考项目 1.0.6 存在（2.0.1 已移除）：ColorMatrix scale = 2^(ev/2.2)。"
                  "本实现的 tone 只有 brightness/contrast/saturation");
    b.Unsupported("gapGamma", "gammaAdjustment", "gammaAdjustment",
                  "同上，只有 1.0.6：shader 里 pow(color.rgb, power)。"
                  "接入需要在片元着色器新增 uniform + 一次 pow（约 3 行）");
    b.Unsupported("gapHighlightBlend", "Highlight BlendMode", "blendMode",
                  "参考项目的 HighlightStyle 带 BlendMode.Plus / SrcOver；"
                  "ytbl 的边光是固定加法（uEdgeMode 只切换形状配方，不切换混合）");
    b.Unsupported("gapOnDrawSurface", "onDrawSurface 任意绘制回调", "onDrawSurface",
                  "参考项目允许在玻璃表面与内容之间插入任意 DrawScope 绘制；"
                  "ytbl 用 PanelStyle::surface + SurfaceMode 表达固定几种情况");
    b.Unsupported("gapSquircle", "连续曲率圆角（squircle）", "continuousCorner",
                  "参考项目用 com.kyant.shapes 的 ContinuousCapsule/RoundedRectangularShape；"
                  "ytbl 的圆角 SDF 是标准圆角，需要改 shader");
    b.Group("平台限制（不是缺口）");
    b.Unsupported("noteBlurCap", "模糊上限 10dp", "blurCap",
                  "BLUR_RADIUS_MAX_DP = 10dp 是 ytbl 的硬上限（不是没做，是会被截断）");
    b.Unsupported("noteAmbientAngle", "Ambient 的角度/衰减无效", "ambientAngle",
                  "shader 的 Ambient 分支只读 alpha/tint/band，不读 uEdgeAngle/uEdgeFalloff"
                  "（参考项目的 Ambient 也把 angle/falloff 硬编码成 45/1.0）");
    b.Unsupported("noteSpread", "阴影没有 spread", "spread",
                  "参考项目与本实现都没有 spread 参数");
}

// -----------------------------------------------------------------------------
// 窗口缩放方式 + 虚线预览/填充动画参数
//
//  ★ 为什么这些"交互参数"也要进注册表：
//    ① 用户要求"新增模式的动画参数应预留配置入口，方便以后通过主题调试器调整
//       动画时长、弹簧刚度、阻尼、边缘模糊强度" —— 注册表就是那个调试器；
//    ② 持久化是白拿的：它们跟着参数文件（版本化 JSON + 原子写 + 去抖自动保存）
//       一起落盘、重启恢复，与其它参数共用同一套预设/撤销/导入导出。
//  ★ 值域来自算法本身：
//    · 弹簧刚度/阻尼 → ResizeSpring 的稳定区间与 Σ 手感（ζ = c/(2√k)）；
//    · 虚线长/间隔/线宽 → 视觉下限（小于 2dp 就"毛"了）与上限（窗口再大也不至于几段）；
//    · 填充模糊差 → 它通过"临时降低窗口模糊"实现，而窗口模糊上限是 10dp。
// -----------------------------------------------------------------------------
constexpr char kResizeSrc[] = "app/src/main/cpp/ResizePreview.h";
constexpr char kResizeHost[] = "app/src/main/cpp/CornerResizeOverlay.cpp";

void BindResizeTarget(Registry& registry) {
    TargetDesc desc;
    desc.id = "secondary_window.resize";
    desc.name = "窗口缩放方式";
    desc.type = "Interaction";
    desc.window = "primary";     // 它由**主题窗口**的「窗口缩放」页驱动
    desc.order = 5;              // 紧跟在"整窗口"之后，容易找到
    desc.note = "两种缩放方式（实时 / 虚线预览）+ 松手后的液态玻璃填充动画参数。"
                "★ 这里的值与「主题 → 窗口缩放」页是**同一块内存**，改哪边都一样";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    aurora::ResizeSettings& settings = aurora::ResizeSettingsRef();
    aurora::ResizePreviewParams& p = settings.preview;

    b.Group("缩放方式");
    b.Enum("mode", "窗口缩放方式", "resizeMode", &settings.mode, 0, 1,
           "实时缩放\0虚线预览缩放\0",
           "0 = 实时缩放（拖动时窗口宽高实时改变，原实现）；"
           "1 = 虚线预览缩放（拖动时只动虚线框，松手后由弹簧把窗口过渡到预览尺寸，"
           "并在移动前沿做玻璃填充动画）。切换在**下一次**缩放手势生效")
        .Source(kResizeSrc, "ResizeMode");

    b.Group("虚线预览框");
    b.Float("dashWidthDp", "虚线线宽", "lineWidth", &p.lineWidthDp, 0.5f, 4.0f, "dp",
            "前景层画的圆角虚线；下面还有一层略粗的暗线保证亮背景下也看得见",
            0.1f).Source(kResizeHost, "CornerResizeDrawPreview");
    b.Float("dashLengthDp", "虚线段长度", "dashLength", &p.dashLengthDp, 2.0f, 24.0f, "dp",
            "按**弧长**排布（圆角处不会变短），所以这个值处处准确",
            0.5f).Source(kResizeSrc, "BuildDashedRoundedRect");
    b.Float("dashGapDp", "虚线间隔", "dashGap", &p.dashGapDp, 1.0f, 20.0f, "dp", "",
            0.5f).Source(kResizeSrc, "BuildDashedRoundedRect");
    b.Float("lineAlpha", "虚线不透明度", "lineAlpha", &p.lineAlpha, 0.0f, 1.0f, "×", "",
            0.01f).Source(kResizeHost, "CornerResizeDrawPreview");
    b.Float("underlayAlpha", "下衬暗线强度", "underlayAlpha", &p.underlayAlpha, 0.0f, 1.0f, "×",
            "白虚线压在亮背景上会看不见，这一层是它的底", 0.01f)
        .Source(kResizeHost, "CornerResizeDrawPreview");
    b.Float("cornerAddDp", "圆角增量", "cornerAdd", &p.cornerRadiusAddDp, -16.0f, 32.0f, "dp",
            "预览框圆角 = 窗口圆角 + 这个增量（0 = 与窗口几何规则完全一致）",
            0.5f).Source(kResizeSrc, "PreviewCornerRadius");
    b.Bool("sizeLabel", "显示预计宽高", "sizeLabel", &p.showSizeLabel,
           "画在预览框**外面**（不遮挡窗口内的 Demo）").Source(kResizeHost,
                                                              "CornerResizeDrawPreview");
    b.Float("labelOffsetDp", "数值外移量", "labelOffset", &p.labelOffsetDp, 0.0f, 48.0f, "dp", "",
            1.0f).Source(kResizeSrc, "ResizePreviewParams");
    b.Float("labelScale", "数值字号比例", "labelScale", &p.labelScale, 0.4f, 1.2f, "×", "",
            0.02f).Source(kResizeSrc, "ResizePreviewParams");
    b.Float("dissolveFrom", "虚线框解体时机", "dissolveFrom", &p.previewDissolveFrom, 0.0f, 0.95f,
            "进度",
            "几何走到这个进度之前虚线框保持实心，之后平滑淡出（不允许松手就消失）",
            0.01f).Source(kResizeSrc, "PreviewDissolveAlpha");

    b.Group("填充动画（几何）");
    b.Float("springStiffness", "弹簧刚度 k", "springStiffness", &p.springStiffness, 20.0f, 900.0f,
            "", "ω = √k；越大越快。默认 420 → 约 300~400ms 到位", 1.0f)
        .Source(kResizeSrc, "ResizePreviewParams::springStiffness");
    b.Float("springDamping", "弹簧阻尼 c", "springDamping", &p.springDamping, 4.0f, 120.0f, "",
            "ζ = c / (2√k)；默认 38.5 → ζ≈0.94（几乎看不到过冲）。调小会有回弹",
            0.5f).Source(kResizeSrc, "ResizePreviewParams::springDamping");
    b.Float("settleEpsilonPx", "到位阈值", "settleEpsilonPx", &p.settleEpsilonPx, 0.1f, 6.0f,
            "px", "剩余不到这么多像素就吸附到目标值（之后再精确对齐一次）", 0.1f)
        .Source(kResizeSrc, "ResizeCommitAnim::Step");
    b.Float("maxDurationMs", "兜底时限", "maxDurationMs", &p.maxDurationMs, 60.0f, 4000.0f, "ms",
            "任何参数下动画都必须结束（防止参数调坏之后状态锁死）", 10.0f)
        .Source(kResizeSrc, "ResizeCommitAnim::Step");

    b.Group("填充动画（玻璃）");
    b.Bool("fillOn", "启用玻璃填充", "fillOn", &p.fillOn,
           "关掉只剩几何过渡（诊断/省电用）").Source(kResizeSrc, "ResizePreviewParams::fillOn");
    b.Float("fillRimWidthDp", "前沿带宽", "rimWidth", &p.fillRimWidthDp, 4.0f, 120.0f, "dp",
            "只有贴着**移动前沿**的这一条带参与填充（不是整块新区域，"
            "否则窗口中间会留下矩形接缝）", 1.0f).Source(kResizeSrc, "ComputeResizeRims");
    b.Float("fillAlpha", "前沿不透明度", "fillAlpha", &p.fillAlpha, 0.0f, 1.0f, "×",
            "前沿玻璃的额外不透明度，随动画收敛到 0", 0.01f)
        .Source(kResizeSrc, "ResizePreviewParams::fillAlpha");
    b.Float("fillBlurDiffDp", "前沿模糊差", "blurDiff", &p.fillBlurDiffDp, 0.0f, 8.0f, "dp",
            "★ 实现方式是**临时降低窗口整体模糊**、前沿保持配置值 —— 因为窗口模糊默认 "
            "10dp 已经是 ytbl 上限（BLUR_RADIUS_MAX_DP），加上去会被夹掉",
            0.25f).Source(kResizeHost, "ScopedPanelClip");
    b.Float("fillRefractGain", "前沿折射增益", "refractGain", &p.fillRefractGain, 1.0f, 2.0f, "×",
            "lensHeight / lensAmount 的增益（上限 LENS_REACH_MAX）", 0.01f)
        .Source(kResizeHost, "lgx::drawPanel");
    b.Float("fillSpectralGain", "前沿色散增益", "spectralGain", &p.fillSpectralGain, 1.0f, 2.0f,
            "×", "spectral 的增益（ytbl 上限 SPECTRAL_MAX = 2.0）", 0.01f)
        .Source(kConfig, "SPECTRAL_MAX");
    b.Float("fillGlowAlpha", "前沿柔光强度", "glowAlpha", &p.fillGlowAlpha, 0.0f, 1.0f, "×",
            "用 ImGui 多色矩形画的平滑渐隐（零 GPU 面板成本），把前沿内边缘化开",
            0.01f).Source(kResizeHost, "lgx::drawPanel");
    b.Float("fillGlowWidthDp", "柔光宽度", "glowWidth", &p.fillGlowWidthDp, 0.0f, 120.0f, "dp", "",
            1.0f).Source(kResizeHost, "lgx::drawPanel");
    b.Float("edgeFlowGain", "边缘流动强度", "edgeFlow", &p.edgeFlowGain, 0.0f, 3.0f, "×",
            "动画期间窗口边缘的柔化 + 折射增强（0 → 峰值 → 0）", 0.05f)
        .Source(kResizeHost, "CornerResizeEdgeFlow");
    b.Float("edgeFlowSoftnessDp", "边缘柔化量", "flowSoftness", &p.edgeFlowSoftnessDp, 0.0f, 3.0f,
            "dp", "缩小时「收缩边缘」的柔性就来自这里", 0.05f)
        .Source(kResizeHost, "CornerResizeEdgeFlow");
}

}  // namespace

void RegisterSecondaryTargets() {
    Registry& registry = Global();
    GlassScene& scene = Scene();

    // ★★ 调试面板现在只暴露**窗口本身**的参数。
    //
    //   这里以前注册了 16 个目标：整窗口、缩放、卡片、PillButton×5、Toggle×3、
    //   Slider×2、导航标签、芯片×6、弹出菜单、玻璃实验场。那些组件全部来自
    //   「c++ 液态玻璃展示源码」的移植，已按用户要求删除 —— 组件没了，
    //   对应的调试分页也就没有意义（留着就是"拖了没反应"的假控件）。
    //
    //   保留：整窗口（窗口壳的玻璃配方）、窗口缩放、窗口间隙。
    BindWindowTarget(registry, scene);
    BindResizeTarget(registry);
    BindGapTarget(registry);
}

}  // namespace glass
}  // namespace aurora
