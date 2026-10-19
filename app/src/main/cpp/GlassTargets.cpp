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
constexpr char kToggleSrc[] = "app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp";
constexpr char kSliderSrc[] = "app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp";
constexpr char kButtonSrc[] = "app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp";
constexpr char kTabsSrc[] = "app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp";
constexpr char kSceneSrc[] = "app/src/main/cpp/glass/GlassScene.cpp";

// Rgba（ytbl 的上游类型，成员 r/g/b/a）→ 可当作 float[4] 用的指针。
//  ★ 调试层的颜色参数绑定的是一个**数组**，所以这里必须显式确认 Rgba 就是
//    4 个连续 float（下面两条 static_assert 会在任何平台上先把这件事钉死，
//    而不是靠"反正编译器会排成这样"的假设）。
inline float* RgbaPtr(Rgba& color) {
    static_assert(sizeof(Rgba) == sizeof(float) * 4, "Rgba 必须是 4 个连续 float");
    static_assert(alignof(Rgba) == alignof(float), "Rgba 的对齐必须与 float 一致");
    return reinterpret_cast<float*>(&color);
}

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
void BindPanelTune(TargetBuilder& b, PanelTune& tune, const char* sourceFile,
                   bool withFallbackTheme) {
    b.Group("形状");
    b.Bool("capsule", "胶囊形状", "capsule", &tune.capsule,
           "打开 = 圆角自动取短边一半（CAPSULE_RADIUS 在绘制前被 clamp）",
           ParamCost::Uniform)
        .Source(kYtblUI, "Corners::capsule");
    b.Float("cornerTlDp", "左上圆角", "cornerTl", &tune.cornerTlDp, 0.0f, 96.0f, "dp",
            "仅在关闭「胶囊形状」时生效", 1.0f, ParamCost::Uniform,
            "关闭胶囊后才生效").Source(sourceFile, "corners()");
    b.Float("cornerTrDp", "右上圆角", "cornerTr", &tune.cornerTrDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(sourceFile, "corners()");
    b.Float("cornerBrDp", "右下圆角", "cornerBr", &tune.cornerBrDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(sourceFile, "corners()");
    b.Float("cornerBlDp", "左下圆角", "cornerBl", &tune.cornerBlDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(sourceFile, "corners()");

    b.Group("折射 / 模糊");
    b.Bool("spectralOn", "启用色散", "chromatic", &tune.spectralOn,
           "Kyant 的 lens(chromaticAberration)；ytbl 用的是 7 抽头棱镜权重表")
        .Source(kYtblUI, "Optics::spectral");
    b.Float("dispersion", "色散强度", "spectral", &tune.dispersion, 0.0f, 2.0f, "×",
            "ytbl 上限 SPECTRAL_MAX = 2.0（GlassConfig.h:98），再大也会被截断",
            0.01f, ParamCost::Uniform,
            "仅在启用色散时生效").Source(kConfig, "SPECTRAL_MAX");
    b.Float("lensHeightDp", "折射高度", "lensHeight", &tune.lensHeightDp, 0.0f, 64.0f, "dp",
            "Kyant 的 lens(refractionHeight)：边缘内侧的折射带宽。"
            "Kyant 把它限制在 [0, 最小圆角半径]，ytbl 的 LENS_REACH_MAX 是 128px",
            0.5f).Source(kConfig, "LENS_REACH_MAX");
    b.Float("lensAmountDp", "折射强度", "lensAmount", &tune.lensAmountDp, 0.0f, 96.0f, "dp",
            "Kyant 的 lens(refractionAmount)：最边缘处的采样位移量（参考实现按负值绑定 → 向内折射）",
            0.5f).Source(kConfig, "LENS_REACH_MAX");
    b.Float("blurDp", "模糊半径", "blur", &tune.blurDp, 0.0f, 10.0f, "dp",
            "★ ytbl 上限 BLUR_RADIUS_MAX_DP = 10dp（GlassConfig.h:69），超过会被截断。"
            "修改它会改变 gather pass 的采样步长 → 标记为帧边界生效",
            0.1f, ParamCost::Pipeline)
        .Source(kConfig, "BLUR_RADIUS_MAX_DP")
        .Deferred(&tune.blurAppliedDp);
    b.Float("depthAmount", "景深折射", "depthEffect", &tune.depthAmount, 0.0f, 1.0f, "×",
            "Kyant 的 lens(depthEffect: Boolean)：给法线场叠加一个径向穹顶项；"
            "Kyant 只有开关，ytbl 是 0..1 连续值", 0.05f).Source(kYtblUI, "Optics::depthAmount");
    b.Float("darkGuard", "暗部保底", "darkGuard", &tune.darkGuard, 0.0f, 1.0f, "×",
            "ytbl 独有：glass 变暗处抬一个下限。实际写进 uniform 时乘 DARK_GUARD_FLOOR = 0.045",
            0.05f).Source(kConfig, "DARK_GUARD_FLOOR");

    b.Group("色彩滤镜");
    b.Bool("colorBoost", "鲜艳度增强", "vibrancy", &tune.colorBoost,
           "Kyant 的 vibrancy()；在 ytbl 里就是 uToneBoost（colorControls(saturation=1.5) 的等价物）")
        .Source(kYtblUI, "Optics::colorBoost");
    b.Float("brightness", "亮度", "brightness", &tune.brightness, -1.0f, 1.0f, "×",
            "Kyant 的 colorControls(brightness)；0 = 不变",
            0.01f).Source(kYtblUI, "Optics::brightness");
    b.Float("contrast", "对比度", "contrast", &tune.contrast, 0.0f, 2.0f, "×",
            "Kyant 的 colorControls(contrast)；1 = 不变", 0.01f)
        .Source(kYtblUI, "Optics::contrast");
    b.Float("saturation", "饱和度", "saturation", &tune.saturation, 0.0f, 3.0f, "×",
            "Kyant 的 colorControls(saturation)；1 = 不变（vibrancy 等价于 1.5）", 0.01f)
        .Source(kYtblUI, "Optics::saturation");
    b.Bool("opacityOn", "启用面板透明度", "opacityOn", &tune.opacityOn,
           "Kyant 的 opacity(alpha) 是可选的；关掉时按 1.0 处理（= 原实现）")
        .Source(kYtblUI, "Optics::opacityOn");
    b.Float("opacity", "透明度", "opacity", &tune.opacity, 0.05f, 1.0f, "×",
            "Kyant 的 opacity(alpha)，范围 0..1", 0.01f, ParamCost::Uniform,
            "仅在启用面板透明度时生效").Source(kYtblUI, "Optics::opacity");
    b.Float("alpha", "整体 alpha", "alpha", &tune.alpha, 0.0f, 1.0f, "×",
            "PanelStyle::alpha：会乘到边光/内阴影/表面色上（面板级不透明度）", 0.01f)
        .Source(kYtblUI, "PanelStyle::alpha");

    b.Group("表面");
    b.Bool("surfaceOn", "启用表面色", "surfaceOn", &tune.surfaceOn,
           "Kyant 的 onDrawSurface/drawRect；关掉时 surfaceMode = None（= 原实现）")
        .Source(kYtblUI, "PanelStyle::surfaceMode");
    b.Color("surface", "表面色", "surface", tune.surface, "RGBA，与表面模式配合")
        .Source(kYtblUI, "PanelStyle::surface");
    b.Enum("surfaceMode", "表面模式", "surfaceMode", &tune.surfaceMode, 0, 2, kSurfaceModes,
           "Kyant 文档推荐的着色写法是「先用 Hue 再叠一层普通 drawRect」；"
           "ytbl 有 None / Hue / Overlay 三种混合模式")
        .Source(kYtblUI, "SurfaceMode");

    b.Group("阴影");
    b.Bool("shadowOn", "启用外阴影", "shadowOn", &tune.shadowOn, "Kyant 的 Shadow")
        .Source(kYtblUI, "PanelStyle::shadowOn");
    b.Float("shadowRadiusDp", "阴影半径", "radius", &tune.shadowRadiusDp, 0.0f, 96.0f, "dp",
            "Kyant 的 Shadow.radius，默认 24dp", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "DropShadow::radiusDp");
    b.Float("shadowXDp", "阴影偏移 X", "offsetX", &tune.shadowXDp, -64.0f, 64.0f, "dp", "",
            0.5f, ParamCost::EffectChain).Source(kYtblUI, "DropShadow::offsetXDp");
    b.Float("shadowYDp", "阴影偏移 Y", "offsetY", &tune.shadowYDp, -64.0f, 64.0f, "dp",
            "Kyant 的 Shadow.offset 默认是 (0, radius/6)", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "DropShadow::offsetYDp");
    b.Color("shadowColor", "阴影颜色", "color", tune.shadowColor, "RGBA")
        .Source(kYtblUI, "DropShadow::color");
    b.Float("shadowAlpha", "阴影强度", "alpha", &tune.shadowAlpha, 0.0f, 1.0f, "×", "", 0.01f)
        .Source(kYtblUI, "DropShadow::alpha");
    b.Bool("insetOn", "启用内阴影", "insetOn", &tune.insetOn,
           "Kyant 的 InnerShadow（在内容之上绘制；ytbl 走 uInset* uniform）")
        .Source(kYtblUI, "PanelStyle::insetOn");
    b.Float("insetRadiusDp", "内阴影半径", "insetRadius", &tune.insetRadiusDp, 0.0f, 64.0f,
            "dp", "Kyant 的 InnerShadow.radius 默认 24dp", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "InsetShadow::radiusDp");
    b.Float("insetXDp", "内阴影偏移 X", "insetOffsetX", &tune.insetXDp, -64.0f, 64.0f, "dp", "",
            0.5f).Source(kYtblUI, "InsetShadow::offsetXDp");
    b.Float("insetYDp", "内阴影偏移 Y", "insetOffsetY", &tune.insetYDp, -64.0f, 64.0f, "dp",
            "Kyant 里 offsetY 默认 = radius；ytbl 两者独立", 0.5f)
        .Source(kYtblUI, "InsetShadow::offsetYDp");
    b.Color("insetColor", "内阴影颜色", "insetColor", tune.insetColor, "RGBA")
        .Source(kYtblUI, "InsetShadow::color");
    b.Float("insetAlpha", "内阴影强度", "insetAlpha", &tune.insetAlpha, 0.0f, 1.0f, "×", "", 0.01f)
        .Source(kYtblUI, "InsetShadow::alpha");

    b.Group("背景采样");
    b.Bool("backdropOn", "启用背景采样", "backdropOn", &tune.backdropOn,
           "关掉后这个面板不采背景（sceneOn = 0），只剩表面色 + 边光。"
           "★ 它决定是否走采集/合成路径，属于帧边界生效的开关",
           ParamCost::Pipeline).Source(kYtblUI, "PanelStyle::backdropOn");
    b.Bool("opaqueBackdrop", "不透明背景", "opaqueBackdrop", &tune.opaqueBackdrop,
           "Kyant 里对应 backdrop node 的 clip/合成策略；ytbl 用 uSolidScene 表达")
        .Source(kYtblUI, "PanelStyle::opaqueBackdrop");
    b.Float("backdropScaleX", "背景缩放 X", "stretchX", &tune.backdropScaleX, 0.0f, 2.0f, "×",
            "Kyant 的 layerBlock scaleX → 会走逆变换把背景「反缩放」；"
            "ytbl 是把采样坐标按这个比例拉伸（uStretchScale）",
            0.01f).Source(kYtblUI, "PanelStyle::backdropScaleX");
    b.Float("backdropScaleY", "背景缩放 Y", "stretchY", &tune.backdropScaleY, 0.001f, 2.0f,
            "×", "下限 BACKDROP_SCALE_MIN = 0.001（GlassConfig.h:119）", 0.01f)
        .Source(kConfig, "BACKDROP_SCALE_MIN");
    b.Float("backdropBlend", "背景混合量", "stretchMix", &tune.backdropBlend, 0.0f, 1.0f, "×",
            "在「原始背景」与「拉伸后的背景」之间插值（uStretchMix）", 0.01f)
        .Source(kConfig, "BACKDROP_SCALE_MIN");

    if (withFallbackTheme) {
        b.Group("兜底色");
        b.Bool("fallbackFollowTheme", "兜底色跟随主题", "followTheme", &tune.fallbackFollowTheme,
               "打开时由渲染端每帧写入主题底色（工作台会把下面那个颜色标成只读）")
            .Source(kSceneSrc, "fallbackFollowTheme");
        b.Color("fallback", "兜底色", "fallback", tune.fallback,
                "没有背景可采时用的颜色（例如宿主表面是透明的）")
            .Source(kYtblUI, "PanelStyle::fallback");
    }

    b.Group("能力缺口（只读）");
    b.Unsupported("gapSquircle", "连续曲率圆角", "continuousCorner",
                  "G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现");
    b.Unsupported("gapBlurTile", "模糊边缘模式", "TileMode",
                  "G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；"
                  "ytbl 的 gather pass 没有对应 uniform");
    b.Unsupported("gapBlurTaps", "模糊抽头数", "blurTaps",
                  "G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform");
    b.Unsupported("gapLensProfile", "折射剖面曲线", "lensProfile",
                  "G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform");
    b.Unsupported("gapNormalGain", "法线场圆角增益", "gradRadiusGain",
                  "G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项");
    b.Unsupported("gapEdgeBlend", "边光混合模式", "edgeBlendMode",
                  "G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光");
    b.Unsupported("gapMultiBackdrop", "多背景源合成", "CombinedBackdrop",
                  "G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；"
                  "ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md）");
    b.Unsupported("gapBackdropTransform", "背景任意仿射变换", "onDrawBackdrop",
                  "G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放");
    b.Unsupported("gapRotation", "图层旋转", "rotationZ",
                  "G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形");
    b.Unsupported("gapRuntimeShader", "自定义 RuntimeShader", "runtimeShaderEffect",
                  "G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序");
    b.Unsupported("gapSpread", "阴影扩散 spread", "spread",
                  "Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口）");
}

// -----------------------------------------------------------------------------
// PillButton 的完整参数表（5 枚按钮共用同一段代码，但每枚绑自己的字段）
// -----------------------------------------------------------------------------
void BindButtonTarget(Registry& registry, GlassScene& scene, int index) {
    TargetDesc desc;
    desc.id = GlassScene::ButtonId(index);
    desc.name = GlassScene::ButtonName(index);
    desc.type = "PillButton";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 200 + index;
    desc.note = index == 1 ? "着色（Hue 混合）"
              : index == 2 ? "表面色（Overlay 混合）"
              : index == 3 ? "小尺寸「+」按钮"
              : index == 4 ? "液态弹窗的触发按钮"
                           : "主按钮（Capsule + 默认表面配方）";

    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    lgx::PillButton& button = scene.buttons[index];
    PillTune& tune = button.tune;
    StageTune& stage = scene.stage;

    b.Group("尺寸");
    b.Float("widthDp", "宽度", "width", &stage.buttonWidthDp[index], 24.0f, 420.0f, "dp",
            "按钮外框宽度（ImGui 布局里传给 draw() 的 Box）", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_ButtonWidthDp");
    b.Float("heightDp", "高度", "height", &stage.buttonHeightDp[index], 20.0f, 160.0f, "dp", "",
            1.0f).Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                         "s_ButtonHeightDp");

    b.Group("折射 / 模糊");
    b.Float("blurDp", "模糊半径", "blur", &button.blurDp, 0.0f, 10.0f, "dp",
            "Kyant 的 LiquidButton 用 blur(2.dp)；ytbl 上限 BLUR_RADIUS_MAX_DP = 10dp",
            0.1f, ParamCost::Pipeline).Source(kConfig, "BLUR_RADIUS_MAX_DP");
    b.Float("lensHeightDp", "折射高度", "lensHeight", &button.lensHeightDp, 0.0f, 40.0f, "dp",
            "Kyant 的 lens(12.dp, 24.dp)；GlassConfig 的 BUTTON_LENS_HEIGHT_DP = 12",
            0.5f).Source(kButtonSrc, "button.lensHeightDp");
    b.Float("lensAmountDp", "折射强度", "lensAmount", &button.lensAmountDp, 0.0f, 64.0f, "dp",
            "Kyant 的 lens 第二个参数；BUTTON_LENS_AMOUNT_DP = 24", 0.5f)
        .Source(kButtonSrc, "button.lensAmountDp");
    b.Float("spectral", "色散强度", "spectral", &button.spectral, 0.0f, 2.0f, "×",
            "PillButton::spectral，默认 0（原实现不设）；上限 SPECTRAL_MAX = 2.0",
            0.01f).Source(kButtonSrc, "button.spectral");
    b.Float("textSizeSp", "文字大小", "textSizeSp", &button.textSizeSp, 8.0f, 40.0f, "sp",
            "Kyant 的 LiquidButton 文字是 16sp（BUTTON_TEXT_SP）", 0.5f)
        .Source(kButtonSrc, "button.textSizeSp");
    b.Float("visualScale", "视觉缩放", "visualScale", &button.visualScale, 0.0f, 1.5f, "×",
            "绘制时的额外缩放；上限 BUTTON_VISUAL_SCALE_MAX = 1.5", 0.01f)
        .Source(kConfig, "BUTTON_VISUAL_SCALE_MAX");
    b.Float("alphaScale", "整体 alpha", "alphaScale", &button.alphaScale, 0.0f, 1.0f, "×",
            "PillButton::alphaScale，会乘到表面/文字/边光上", 0.01f)
        .Source(kButtonSrc, "button.alphaScale");
    b.Bool("glassOn", "启用玻璃", "glassOn", &button.glassOn,
           "关掉只画文字（不提交面板）").Source(kButtonSrc, "button.glassOn");
    b.Bool("textOn", "显示文字", "textOn", &button.textOn, "").Source(kButtonSrc, "button.textOn");

    b.Group("形状");
    b.Bool("capsule", "胶囊形状", "capsule", &tune.capsule,
           "Kyant 的 LiquidButton 用 Capsule()；关掉后可用四角独立圆角")
        .Source(kYtblUI, "PillTune::capsule");
    b.Float("cornerTlDp", "左上圆角", "cornerTl", &tune.cornerTlDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(kYtblUI, "PillTune::cornerTlDp");
    b.Float("cornerTrDp", "右上圆角", "cornerTr", &tune.cornerTrDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(kYtblUI, "PillTune::cornerTrDp");
    b.Float("cornerBrDp", "右下圆角", "cornerBr", &tune.cornerBrDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(kYtblUI, "PillTune::cornerBrDp");
    b.Float("cornerBlDp", "左下圆角", "cornerBl", &tune.cornerBlDp, 0.0f, 96.0f, "dp", "",
            1.0f, ParamCost::Uniform, "关闭胶囊后才生效").Source(kYtblUI, "PillTune::cornerBlDp");

    b.Group("色彩 / 表面");
    b.Bool("colorBoost", "鲜艳度增强", "vibrancy", &tune.colorBoost,
           "原实现写死 true（Kyant 的 LiquidButton 也调 vibrancy()）")
        .Source(kButtonSrc, "buttonStyle");
    b.Float("brightness", "亮度", "brightness", &tune.brightness, -1.0f, 1.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::brightness");
    b.Float("contrast", "对比度", "contrast", &tune.contrast, 0.0f, 2.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::contrast");
    b.Float("saturation", "饱和度", "saturation", &tune.saturation, 0.0f, 3.0f, "×", "", 0.01f)
        .Source(kYtblUI, "Optics::saturation");
    b.Bool("opacityOn", "启用面板透明度", "opacityOn", &tune.opacityOn, "")
        .Source(kYtblUI, "Optics::opacityOn");
    b.Float("opacity", "透明度", "opacity", &tune.opacity, 0.05f, 1.0f, "×", "", 0.01f,
            ParamCost::Uniform, "仅在启用面板透明度时生效").Source(kYtblUI, "Optics::opacity");
    b.Float("depthAmount", "景深折射", "depthEffect", &tune.depthAmount, 0.0f, 1.0f, "×",
            "原实现写死 0（Kyant 的 LiquidButton 用 lens(...) 的默认 depthEffect = false）", 0.05f)
        .Source(kButtonSrc, "buttonStyle");
    b.Float("darkGuard", "暗部保底", "darkGuard", &tune.darkGuard, 0.0f, 1.0f, "×", "", 0.05f)
        .Source(kConfig, "DARK_GUARD_FLOOR");
    b.Bool("tinted", "着色（Hue）", "tinted", &button.tinted,
           "Kyant 的 LiquidButton(tint=…)：用 Hue 混合把底色染成这个颜色")
        .Source(kButtonSrc, "button.tinted");
    b.Color("tint", "着色颜色", "tint", RgbaPtr(button.tint),
            "RGBA。★ 注意这里是 Rgba（4 个 float 连续存放）").Source(kButtonSrc, "button.tint");
    b.Bool("surfaced", "表面色（Overlay）", "surfaced", &button.surfaced,
           "Kyant 的 LiquidButton(surfaceColor=…)").Source(kButtonSrc, "button.surfaced");
    b.Color("surface", "表面颜色", "surface", RgbaPtr(button.surface), "RGBA")
        .Source(kButtonSrc, "button.surface");
    b.Enum("surfaceModeOverride", "强制表面模式", "surfaceMode", &tune.surfaceModeOverride, -1, 2,
           "自动\0无\0Hue\0Overlay\0",
           "-1 = 跟随 tinted/surfaced 的原逻辑；0..2 强制指定")
        .Source(kYtblUI, "PillTune::surfaceModeOverride");
    b.Float("fallbackR", "兜底色 R", "fallbackR", &tune.fallbackR, 0.0f, 1.0f, "×",
            "原实现 fallback = (0,0,0,0)", 0.01f).Source(kButtonSrc, "buttonStyle");
    b.Float("fallbackG", "兜底色 G", "fallbackG", &tune.fallbackG, 0.0f, 1.0f, "×", "", 0.01f);
    b.Float("fallbackB", "兜底色 B", "fallbackB", &tune.fallbackB, 0.0f, 1.0f, "×", "", 0.01f);
    b.Float("fallbackA", "兜底色 A", "fallbackA", &tune.fallbackA, 0.0f, 1.0f, "×", "", 0.01f);

    b.Group("光影");
    b.Bool("shadowOn", "外阴影", "shadowOn", &button.shadowOn,
           "原实现默认开（PillButton::shadowOn = true）").Source(kButtonSrc, "button.shadowOn");
    b.Float("shadowRadiusDp", "阴影半径", "radius", &tune.shadowRadiusDp, 0.0f, 96.0f, "dp",
            "原实现取 DropShadow 默认 24dp", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "DropShadow::radiusDp");
    b.Float("shadowOffsetXDp", "阴影偏移 X", "offsetX", &tune.shadowOffsetXDp, -64.0f, 64.0f, "dp",
            "", 0.5f, ParamCost::EffectChain).Source(kYtblUI, "DropShadow::offsetXDp");
    b.Float("shadowOffsetYDp", "阴影偏移 Y", "offsetY", &tune.shadowOffsetYDp, -64.0f, 64.0f, "dp",
            "原实现 4dp（DropShadow 默认 = radius/6）", 0.5f, ParamCost::EffectChain)
        .Source(kYtblUI, "DropShadow::offsetYDp");
    b.Color("shadowTint", "阴影颜色", "color", tune.shadowTint, "RGBA（原 0x1A000000）")
        .Source(kYtblUI, "DropShadow::color");
    b.Float("shadowAlpha", "阴影强度", "alpha", &tune.shadowAlpha, 0.0f, 1.0f, "×", "", 0.01f);
    b.Bool("insetOn", "启用内阴影", "insetOn", &tune.insetOn,
           "原实现不开；打开后用 InsetShadow 的默认配方").Source(kYtblUI, "PillTune::insetOn");
    b.Float("insetRadiusDp", "内阴影半径", "insetRadius", &tune.insetRadiusDp, 0.0f, 64.0f, "dp",
            "", 0.5f, ParamCost::EffectChain).Source(kYtblUI, "InsetShadow::radiusDp");
    b.Float("insetOffsetXDp", "内阴影偏移 X", "insetOffsetX", &tune.insetOffsetXDp, -64.0f, 64.0f,
            "dp", "", 0.5f).Source(kYtblUI, "InsetShadow::offsetXDp");
    b.Float("insetOffsetYDp", "内阴影偏移 Y", "insetOffsetY", &tune.insetOffsetYDp, -64.0f, 64.0f,
            "dp", "InsetShadow 默认 offsetY = radius", 0.5f)
        .Source(kYtblUI, "InsetShadow::offsetYDp");
    b.Color("insetTint", "内阴影颜色", "insetColor", tune.insetTint, "RGBA（原 0x26000000）")
        .Source(kYtblUI, "InsetShadow::color");
    b.Float("insetAlpha", "内阴影强度", "insetAlpha", &tune.insetAlpha, 0.0f, 1.0f, "×", "", 0.01f);
    b.Bool("contourEdge", "轮廓光", "contourEdge", &button.contourEdge,
           "把边光换成 ytbl 独有的 Contour（-90° 方向、alpha 随按压增强）")
        .Source(kButtonSrc, "button.contourEdge");
    b.Float("contourAlpha", "轮廓光基础强度", "contourAlpha", &tune.contourAlpha, 0.0f, 1.0f, "×",
            "EDGE_CONTOUR_ALPHA = 0.50", 0.01f).Source(kConfig, "EDGE_CONTOUR_ALPHA");
    b.Float("contourPressGain", "轮廓光按压增益", "contourPressGain", &tune.contourPressGain,
            0.0f, 1.0f, "×", "EDGE_CONTOUR_PRESS_GAIN = 0.34", 0.01f)
        .Source(kConfig, "EDGE_CONTOUR_PRESS_GAIN");
    b.Float("contourWidthDp", "轮廓光宽度", "contourWidth", &tune.contourWidthDp, 0.0f, 6.0f,
            "dp", "EDGE_CONTOUR_WIDTH = 0.6", 0.05f).Source(kConfig, "EDGE_CONTOUR_WIDTH");
    b.Float("contourSoftnessDp", "轮廓光柔度", "contourSoftness", &tune.contourSoftnessDp, 0.0f,
            4.0f, "dp", "EDGE_CONTOUR_SOFTNESS = 0.30", 0.05f)
        .Source(kConfig, "EDGE_CONTOUR_SOFTNESS");
    BindEdge(b, tune.edge, 5);

    b.Group("背景采样");
    b.Bool("backdropOn", "启用背景采样", "backdropOn", &tune.backdropOn,
           "关掉后按钮不采背景", ParamCost::Pipeline).Source(kYtblUI, "PanelStyle::backdropOn");
    b.Bool("opaqueBackdrop", "不透明背景", "opaqueBackdrop", &tune.opaqueBackdrop, "")
        .Source(kYtblUI, "PanelStyle::opaqueBackdrop");
    b.Float("backdropScaleX", "背景缩放 X", "stretchX", &tune.backdropScaleX, 0.0f, 2.0f, "×", "",
            0.01f).Source(kYtblUI, "PanelStyle::backdropScaleX");
    b.Float("backdropScaleY", "背景缩放 Y", "stretchY", &tune.backdropScaleY, 0.001f, 2.0f, "×",
            "", 0.01f).Source(kYtblUI, "PanelStyle::backdropScaleY");
    b.Float("backdropBlend", "背景混合量", "stretchMix", &tune.backdropBlend, 0.0f, 1.0f, "×", "",
            0.01f).Source(kYtblUI, "PanelStyle::backdropBlend");

    b.Group("交互");
    b.Float("glowStiffness", "按压缩放弹簧刚度", "pressStiffness", &tune.glowStiffness, 10.0f,
            1000.0f, "×", "Kyant 的 pressProgress 弹簧是 spring(1f, 1000f)。"
                          "yttbl 的 PRESS_STIFFNESS = 300", 5.0f)
        .Source(kConfig, "PRESS_STIFFNESS");
    b.Float("glowDamping", "按压弹簧阻尼", "pressDamping", &tune.glowDamping, 0.05f, 2.0f, "×",
            "PRESS_DAMPING = 0.5", 0.01f).Source(kConfig, "PRESS_DAMPING");
    b.Float("pointerStiffness", "光斑跟随刚度", "pointerStiffness", &tune.pointerStiffness, 10.0f,
            2000.0f, "×", "POINTER_STIFFNESS = 300（Kyant 的 position 弹簧也是 300）", 5.0f)
        .Source(kConfig, "POINTER_STIFFNESS");
    b.Float("pointerDamping", "光斑跟随阻尼", "pointerDamping", &tune.pointerDamping, 0.05f, 2.0f,
            "×", "POINTER_DAMPING = 0.5", 0.01f).Source(kConfig, "POINTER_DAMPING");

    b.Group("能力缺口（只读）");
    b.Unsupported("gapIconGap", "图标与文字间距", "iconGap",
                  "G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段");
    b.Unsupported("gapDragTanh", "拖动跟手度", "dragTanh",
                  "G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05）");
}

// -----------------------------------------------------------------------------
// 开关：父目标（状态/尺寸/交互）+ .track + .thumb
// -----------------------------------------------------------------------------
void BindToggleTargets(Registry& registry, GlassScene& scene, int index) {
    const int base = 300 + index * 10;
    char id[96];
    char name[96];

    lgx::Toggle& toggle = scene.toggles[index];
    ToggleTune& tune = toggle.tune;

    std::snprintf(id, sizeof(id), "secondary_window.toggle_%d", index + 1);
    std::snprintf(name, sizeof(name), "开关 %d", index + 1);
    TargetDesc parent;
    parent.id = id;
    parent.name = name;
    parent.type = "Toggle";
    parent.window = "secondary";
    parent.parent = "secondary_window";
    parent.order = base;
    parent.note = index == 1 ? "第二枚（红色强调色）"
               : index == 2 ? "第三枚（默认打开）"
                            : "第一枚（默认关闭）";
    {
        TargetBuilder b(registry, parent);
        if (b.created()) {
            b.Group("状态");
            b.Bool("on", "当前打开", "selected", &toggle.on,
                   "与组件**双向绑定**：手指点开关会把这个值写回来，主窗口立即同步")
                .Source(kToggleSrc, "Toggle::on");
            b.Group("颜色");
            b.Color("accentColor", "开启色", "accentColor", scene.stage.toggleAccent[index],
                    "写进轨道混色的目标色。★ 等于 TOGGLE_ACCENT 常量时会走 ytbl 的"
                    "浅色主题自适应分支（Toggle.cpp:134-139）")
                .Source(kToggleSrc, "accentColor");
            b.Group("交互");
            b.Float("scale", "整体倍率", "scale", &scene.stage.toggleScale[index], 0.3f, 2.5f,
                    "×", "ytbl 的 Toggle 内部尺寸是编译期常量，只能整体等比缩放"
                         "（见 secondary_imgui.cpp 的 ScopedGlassScale）", 0.01f)
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_ToggleScale");
            b.Float("dragScaleDp", "拖动换算", "dragScale", &tune.dragScaleDp, 2.0f, 100.0f, "dp",
                    "手指位移 ÷ 这个值 = 进度变化（TOGGLE_DRAG_SCALE_DP = 20）", 1.0f)
                .Source(kConfig, "TOGGLE_DRAG_SCALE_DP");
            b.Float("dragSlopDp", "拖动判定阈值", "dragSlop", &tune.dragSlopDp, 0.0f, 40.0f, "dp",
                    "超过它才算「拖动」而不是「点击」（TOGGLE_DRAG_SLOP_DP = 3）", 0.5f)
                .Source(kConfig, "TOGGLE_DRAG_SLOP_DP");
            b.Float("velocityGain", "速度增益", "velocityGain", &tune.velocityGain, 0.0f, 0.2f, "×",
                    "TOGGLE_VELOCITY_GAIN = 1/50（Kyant 的 toggle 也是 /50）", 0.001f)
                .Source(kConfig, "TOGGLE_VELOCITY_GAIN");
            b.Float("pressedScale", "按压缩放", "pressedScale", &tune.pressedScale, 0.5f, 3.0f,
                    "×", "DRAG_SCALE = 1.5（Kyant 的 LiquidToggle pressedScale 同为 1.5）",
                    0.01f).Source(kConfig, "DRAG_SCALE");
            b.Float("valueStiffness", "进度弹簧刚度", "valueStiffness", &tune.valueStiffness,
                    10.0f, 3000.0f, "×", "DRAG_VALUE_STIFFNESS = 1000（Kyant 同值）", 5.0f)
                .Source(kConfig, "DRAG_VALUE_STIFFNESS");
            b.Float("valueDamping", "进度弹簧阻尼", "valueDamping", &tune.valueDamping, 0.05f,
                    2.0f, "×", "DRAG_VALUE_DAMPING = 1.0", 0.01f)
                .Source(kConfig, "DRAG_VALUE_DAMPING");
        }
    }

    std::snprintf(id, sizeof(id), "secondary_window.toggle_%d.track", index + 1);
    std::snprintf(name, sizeof(name), "开关 %d · 轨道", index + 1);
    TargetDesc track = parent;
    track.id = id;
    track.name = name;
    track.type = "ToggleTrack";   // 子层有自己的类型：按类型检索时不会和父层混在一起
    track.parent = parent.id;
    track.order = base + 1;
    track.note = "绿色/红色底轨：直接填充（不是面板），只受尺寸与颜色影响";
    {
        TargetBuilder b(registry, track);
        if (b.created()) {
            b.Group("尺寸");
            b.Float("widthDp", "轨道宽度", "trackWidth", &tune.trackWidthDp, 8.0f, 200.0f, "dp",
                    "TOGGLE_TRACK_W_DP = 64（Kyant 的 LiquidToggle 同为 64dp）", 1.0f)
                .Source(kConfig, "TOGGLE_TRACK_W_DP");
            b.Float("heightDp", "轨道高度", "trackHeight", &tune.trackHeightDp, 4.0f, 120.0f, "dp",
                    "TOGGLE_TRACK_H_DP = 28", 1.0f).Source(kConfig, "TOGGLE_TRACK_H_DP");
            b.Float("radiusRatio", "圆角比例", "trackRadiusRatio", &tune.trackRadiusRatio, 0.0f,
                    1.0f, "×", "轨道圆角 = 高度 × 这个比例，0.5 = 胶囊", 0.01f)
                .Source(kToggleSrc, "trackRadius");
            b.Group("颜色");
            b.Color("trackColor", "轨道底色", "trackColor", scene.stage.toggleTrack,
                    "★ 等于 TOGGLE_TRACK 常量时会走 ytbl 的浅色主题替换分支")
                .Source(kToggleSrc, "trackColor");
        }
    }

    std::snprintf(id, sizeof(id), "secondary_window.toggle_%d.thumb", index + 1);
    std::snprintf(name, sizeof(name), "开关 %d · 圆钮", index + 1);
    TargetDesc thumb = parent;
    thumb.id = id;
    thumb.name = name;
    thumb.type = "ToggleThumb";
    thumb.parent = parent.id;
    thumb.order = base + 2;
    thumb.note = "玻璃圆钮：按下时模糊/折射/背景缩放一起变化（Kyant 的 LiquidToggle 同构）";
    {
        TargetBuilder b(registry, thumb);
        if (b.created()) {
            b.Group("尺寸");
            b.Float("widthDp", "圆钮宽度", "thumbWidth", &tune.thumbWidthDp, 8.0f, 200.0f, "dp",
                    "TOGGLE_THUMB_W_DP = 40", 1.0f).Source(kConfig, "TOGGLE_THUMB_W_DP");
            b.Float("heightDp", "圆钮高度", "thumbHeight", &tune.thumbHeightDp, 8.0f, 160.0f,
                    "dp", "TOGGLE_THUMB_H_DP = 24", 1.0f).Source(kConfig, "TOGGLE_THUMB_H_DP");
            b.Float("paddingDp", "起始内边距", "padding", &tune.paddingDp, 0.0f, 40.0f, "dp",
                    "TOGGLE_PADDING_DP = 2（Kyant: lerp(2.dp, 22.dp, fraction)）", 0.5f)
                .Source(kConfig, "TOGGLE_PADDING_DP");
            b.Float("travelDp", "行程", "travel", &tune.travelDp, 0.0f, 160.0f, "dp",
                    "TOGGLE_TRAVEL_DP = 20（Kyant 的 dragWidth 也是 20dp）", 0.5f)
                .Source(kConfig, "TOGGLE_TRAVEL_DP");
            b.Group("折射 / 模糊");
            b.Float("lensHeightPressDp", "按压折射高度", "lensHeight",
                    &tune.lensHeightPressDp, 0.0f, 40.0f, "dp",
                    "TOGGLE_LENS_HEIGHT_DP = 5；实际值 = 它 × press（Kyant 的 lens(5.dp*p, …)）",
                    0.5f).Source(kConfig, "TOGGLE_LENS_HEIGHT_DP");
            b.Float("lensAmountPressDp", "按压折射强度", "lensAmount",
                    &tune.lensAmountPressDp, 0.0f, 60.0f, "dp",
                    "TOGGLE_LENS_AMOUNT_DP = 10（Kyant 同为 10dp×p）", 0.5f)
                .Source(kConfig, "TOGGLE_LENS_AMOUNT_DP");
            b.Float("lensGain", "折射增益", "lensGain", &tune.lensGain, 0.0f, 3.0f, "×",
                    "乘在 press 上：1.0 = 原配方；调大可以在不按压时也看到折射", 0.01f)
                .Source(kConfig, "TOGGLE_LENS_HEIGHT_DP");
            b.Float("blurRestDp", "静止模糊", "glassBlur", &tune.blurRestDp, 0.0f, 10.0f, "dp",
                    "GLASS_BLUR_DP = 4（press = 0 时）。★ 原实现是 "
                    "lerp(FLOOR, GLASS, 1-p) → 静止最模糊、按下去更清晰"
                    "（Kyant 的 LiquidToggle 同样是 8dp × (1-p)）", 0.1f, ParamCost::Pipeline)
                .Source(kConfig, "GLASS_BLUR_DP");
            b.Float("blurPressDp", "按压模糊", "glassBlurFloor", &tune.blurPressDp, 0.0f, 10.0f,
                    "dp", "GLASS_BLUR_FLOOR_DP = 1（press = 1 时）", 0.1f, ParamCost::Pipeline)
                .Source(kConfig, "GLASS_BLUR_FLOOR_DP");
            b.Bool("spectralOn", "启用色散", "chromatic", &tune.spectralOn,
                   "原实现写死 spectral = 1.0（Kyant 的 LiquidToggle 也开 chromaticAberration）")
                .Source(kToggleSrc, "Toggle::draw");
            b.Float("darkGuard", "暗部保底", "darkGuard", &tune.darkGuard, 0.0f, 1.0f, "×",
                    "TOGGLE_DARK_GUARD = 1", 0.05f).Source(kConfig, "TOGGLE_DARK_GUARD");
            b.Group("背景采样");
            b.Float("backdropScaleXMin", "静止背景缩放 X", "stretchXMin",
                    &tune.backdropScaleXMin, 0.0f, 2.0f, "×",
                    "TOGGLE_BACKDROP_X_MIN = 2/3（Kyant: lerp(2/3, 0.75, p)）", 0.01f)
                .Source(kConfig, "TOGGLE_BACKDROP_X_MIN");
            b.Float("backdropScaleXMax", "按压背景缩放 X", "stretchXMax",
                    &tune.backdropScaleXMax, 0.0f, 2.0f, "×",
                    "TOGGLE_BACKDROP_X_MAX = 0.75（Kyant 同为 0.75）", 0.01f)
                .Source(kConfig, "TOGGLE_BACKDROP_X_MAX");
            b.Float("backdropScaleYMin", "静止背景缩放 Y", "stretchYMin",
                    &tune.backdropScaleYMin, 0.001f, 2.0f, "×",
                    "TOGGLE_BACKDROP_Y_MIN = 0.55（★ Kyant 是 lerp(0, 0.75, p)，下限为 0；"
                    "ytbl 把下限抬到 0.55 以免背景被压成一条线）", 0.01f)
                .Source(kConfig, "TOGGLE_BACKDROP_Y_MIN");
            b.Float("backdropScaleYMax", "按压背景缩放 Y", "stretchYMax",
                    &tune.backdropScaleYMax, 0.001f, 2.0f, "×", "TOGGLE_BACKDROP_Y_MAX = 0.75",
                    0.01f).Source(kConfig, "TOGGLE_BACKDROP_Y_MAX");
            b.Float("backdropBlend", "背景混合量", "stretchMix", &tune.backdropBlend, 0.0f, 1.0f,
                    "×", "原实现写死 1.0（完全用缩放后的采样坐标）", 0.01f)
                .Source(kToggleSrc, "backdropBlend");
            b.Group("表面 / 光影");
            b.Float("surfaceAlphaGain", "表面透明度增益", "surfaceAlphaGain",
                    &tune.surfaceAlphaGain, 0.0f, 2.0f, "×",
                    "原实现 surface.a = 1 - press；这里是它的增益", 0.01f)
                .Source(kToggleSrc, "Toggle::draw");
            b.Enum("surfaceMode", "表面模式", "surfaceMode", &tune.surfaceMode, 0, 2,
                   kSurfaceModes, "原实现写死 Overlay").Source(kToggleSrc, "surfaceMode");
            b.Float("edgeAlphaGain", "边光强度增益", "edgeAlphaGain", &tune.edgeAlphaGain, 0.0f,
                    2.0f, "×", "原实现 EdgeLight::ambient(press)；这是它的增益", 0.01f)
                .Source(kToggleSrc, "Toggle::draw");
            b.Float("insetRadiusDp", "内阴影半径", "insetRadius", &tune.insetRadiusDp, 0.0f, 32.0f,
                    "dp", "TOGGLE_INNER_DP = 4；实际 = 它 × max(press, REST_DP)", 0.5f,
                    ParamCost::EffectChain).Source(kConfig, "TOGGLE_INNER_DP");
            b.Float("insetRestAlpha", "内阴影静止强度", "insetRestAlpha", &tune.insetRestAlpha,
                    0.0f, 1.0f, "×", "TOGGLE_INNER_REST_ALPHA = 0.10", 0.01f)
                .Source(kConfig, "TOGGLE_INNER_REST_ALPHA");
            b.Float("insetRestRadius", "内阴影静止半径系数", "insetRestRadius",
                    &tune.insetRestRadius, 0.0f, 1.0f, "×", "TOGGLE_INNER_REST_DP = 0.25", 0.01f)
                .Source(kConfig, "TOGGLE_INNER_REST_DP");
            BindEdge(b, tune.edge, 5);
            b.Group("能力缺口（只读）");
            b.Unsupported("gapPressedScaleCurve", "按压曲线自定义", "pressCurve",
                          "G14：Kyant 用 spring(dampingRatio, stiffness) 描述，ytbl 只能调刚度/阻尼两个数");
        }
    }
}

// -----------------------------------------------------------------------------
// 进度条：父目标（值/量程/布局）+ .track + .thumb
// -----------------------------------------------------------------------------
void BindSliderTargets(Registry& registry, GlassScene& scene, int index) {
    const int base = 400 + index * 10;
    char id[96];
    char name[96];

    lgx::Slider& slider = scene.sliders[index];
    SliderTune& tune = slider.tune;

    std::snprintf(id, sizeof(id), "secondary_window.slider_%d", index + 1);
    std::snprintf(name, sizeof(name), "进度条 %d", index + 1);
    TargetDesc parent;
    parent.id = id;
    parent.name = name;
    parent.type = "Slider";
    parent.window = "secondary";
    parent.parent = "secondary_window";
    parent.order = base;
    parent.note = index == 1 ? "0~1 连续值" : "0~100 量程";
    {
        TargetBuilder b(registry, parent);
        if (b.created()) {
            b.Group("值与量程");
            // ★ 值域必须与**这一根滑块真实的量程**一致：第 1 根是 0~1（原 Slider 默认），
            //   第 2 根是 0~100（原示例的 setRange(0, 100)）。
            //   （离线回归抓到过这里写错：把第 2 根的上限写成 1.0，于是它的默认值 50
            //     会被夹成 1.0 —— 显示值和渲染值一起变成错的。）
            b.Float("value", "当前值", "value", &slider.value, 0.0f,
                    index == 0 ? 1.0f : 100.0f, "",
                    "与组件**双向绑定**：手指拖动滑杆会把这个值写回来", 0.01f,
                    ParamCost::Uniform,
                    "上限跟随组件的 maxValue；改量程请用下面的「量程上限」参数")
                .Source(kSliderSrc, "Slider::value");
            b.Float("minValue", "量程下限", "minValue", &slider.minValue, 0.0f, 100.0f, "",
                    "★ 改量程会把当前进度按比例重映射（见 Slider::setRange）", 0.5f)
                .Source(kSliderSrc, "Slider::minValue");
            b.Float("maxValue", "量程上限", "maxValue", &slider.maxValue, 1.0f, 1000.0f, "",
                    "第二根原来是 setRange(0, 100)", 1.0f).Source(kSliderSrc, "Slider::maxValue");
            b.Group("布局");
            b.Float("widthFraction", "宽度占比", "widthFraction",
                    &scene.stage.sliderWidthFraction[index], 0.2f, 1.0f, "×", "", 0.01f)
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_SliderWidthFraction");
            b.Float("heightDp", "外框高度", "heightDp", &scene.stage.sliderHeightDp[index], 16.0f,
                    80.0f, "dp", "原实现 px(24)", 1.0f)
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_SliderHeightDp");
            b.Float("scale", "整体倍率", "scale", &scene.stage.sliderScale[index], 0.3f, 2.5f,
                    "×", "", 0.01f).Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                           "s_SliderScale");
            b.Group("交互");
            b.Float("velocityGain", "速度增益", "velocityGain", &tune.velocityGain, 0.0f, 1.0f,
                    "×", "SLIDER_VELOCITY_GAIN = 0.1（Kyant 的 slider 是 /10）", 0.005f)
                .Source(kConfig, "SLIDER_VELOCITY_GAIN");
            b.Float("pressedScale", "按压缩放", "pressedScale", &tune.pressedScale, 0.5f, 3.0f,
                    "×", "DRAG_SCALE = 1.5（Kyant 的 LiquidSlider pressedScale 同为 1.5）",
                    0.01f).Source(kConfig, "DRAG_SCALE");
            b.Float("valueStiffness", "值弹簧刚度", "valueStiffness", &tune.valueStiffness, 10.0f,
                    3000.0f, "×", "DRAG_VALUE_STIFFNESS = 1000", 5.0f)
                .Source(kConfig, "DRAG_VALUE_STIFFNESS");
            b.Float("valueDamping", "值弹簧阻尼", "valueDamping", &tune.valueDamping, 0.05f, 2.0f,
                    "×", "DRAG_VALUE_DAMPING = 1.0", 0.01f).Source(kConfig, "DRAG_VALUE_DAMPING");
            b.Float("thumbEdgeMin", "滑杆左越界比", "thumbEdgeMin", &tune.thumbEdgeMin, 0.0f, 1.0f,
                    "×", "SLIDER_THUMB_EDGE_MIN = 0.25（Kyant: clamp(-w/2 + …, -w/4, …)）", 0.01f)
                .Source(kConfig, "SLIDER_THUMB_EDGE_MIN");
            b.Float("thumbEdgeMax", "滑杆右越界比", "thumbEdgeMax", &tune.thumbEdgeMax, 0.0f, 1.0f,
                    "×", "SLIDER_THUMB_EDGE_MAX = 0.75", 0.01f)
                .Source(kConfig, "SLIDER_THUMB_EDGE_MAX");
        }
    }

    std::snprintf(id, sizeof(id), "secondary_window.slider_%d.track", index + 1);
    std::snprintf(name, sizeof(name), "进度条 %d · 轨道", index + 1);
    TargetDesc track = parent;
    track.id = id;
    track.name = name;
    track.type = "SliderTrack";
    track.parent = parent.id;
    track.order = base + 1;
    track.note = "底轨 + 已填充段：直接填充绘制（不是玻璃面板）";
    {
        TargetBuilder b(registry, track);
        if (b.created()) {
            b.Group("尺寸");
            b.Float("heightDp", "轨道高度", "trackHeight", &tune.trackHeightDp, 2.0f, 40.0f, "dp",
                    "SLIDER_TRACK_H_DP = 6（Kyant 同为 6dp）", 0.5f)
                .Source(kConfig, "SLIDER_TRACK_H_DP");
            b.Float("radiusRatio", "轨道圆角比例", "trackRadiusRatio", &tune.trackRadiusRatio,
                    0.0f, 1.0f, "×", "0.5 = 胶囊", 0.01f).Source(kSliderSrc, "rail");
            b.Float("fillRadiusRatio", "填充圆角比例", "fillRadiusRatio",
                    &tune.fillRadiusRatio, 0.0f, 1.0f, "×", "0.5 = 胶囊", 0.01f)
                .Source(kSliderSrc, "fillRadius");
            b.Group("颜色");
            b.Color("accentColor", "填充色", "accentColor", scene.stage.sliderAccentDark,
                    "★ 等于 SLIDER_ACCENT 常量时会走 ytbl 的浅色主题替换分支")
                .Source(kSliderSrc, "accentColor");
            b.Color("trackColor", "轨道底色", "trackColor", scene.stage.sliderTrackColor,
                    "★ 等于 SLIDER_TRACK 常量时走主题替换").Source(kSliderSrc, "trackColor");
        }
    }

    std::snprintf(id, sizeof(id), "secondary_window.slider_%d.thumb", index + 1);
    std::snprintf(name, sizeof(name), "进度条 %d · 滑杆", index + 1);
    TargetDesc thumb = parent;
    thumb.id = id;
    thumb.name = name;
    thumb.type = "SliderThumb";
    thumb.parent = parent.id;
    thumb.order = base + 2;
    thumb.note = "玻璃滑杆：按下时模糊/折射/背景缩放一起变化";
    {
        TargetBuilder b(registry, thumb);
        if (b.created()) {
            b.Group("尺寸");
            b.Float("widthDp", "滑杆宽度", "thumbWidth", &tune.thumbWidthDp, 8.0f, 160.0f, "dp",
                    "SLIDER_THUMB_W_DP = 40（Kyant 同为 40dp）", 1.0f)
                .Source(kConfig, "SLIDER_THUMB_W_DP");
            b.Float("heightDp", "滑杆高度", "thumbHeight", &tune.thumbHeightDp, 8.0f, 120.0f,
                    "dp", "SLIDER_THUMB_H_DP = 24", 1.0f).Source(kConfig, "SLIDER_THUMB_H_DP");
            b.Group("折射 / 模糊");
            b.Float("lensHeightPressDp", "按压折射高度", "lensHeight",
                    &tune.lensHeightPressDp, 0.0f, 60.0f, "dp",
                    "SLIDER_LENS_HEIGHT_DP = 12（Kyant 同为 10dp×p，ytbl 略高）", 0.5f)
                .Source(kConfig, "SLIDER_LENS_HEIGHT_DP");
            b.Float("lensAmountPressDp", "按压折射强度", "lensAmount",
                    &tune.lensAmountPressDp, 0.0f, 80.0f, "dp",
                    "SLIDER_LENS_AMOUNT_DP = 14（Kyant 同为 14dp×p）", 0.5f)
                .Source(kConfig, "SLIDER_LENS_AMOUNT_DP");
            b.Float("lensGain", "折射增益", "lensGain", &tune.lensGain, 0.0f, 3.0f, "×",
                    "乘在 press 上，1.0 = 原配方", 0.01f).Source(kConfig, "SLIDER_LENS_HEIGHT_DP");
            b.Float("blurRestDp", "静止模糊", "glassBlur", &tune.blurRestDp, 0.0f, 10.0f, "dp",
                    "GLASS_BLUR_DP = 4（静止时模糊最大；按下去反而更清晰，"
                    "与 Kyant 的 8dp × (1-p) 同向）", 0.1f, ParamCost::Pipeline)
                .Source(kConfig, "GLASS_BLUR_DP");
            b.Float("blurPressDp", "按压模糊", "glassBlurFloor", &tune.blurPressDp, 0.0f, 10.0f,
                    "dp", "GLASS_BLUR_FLOOR_DP = 1", 0.1f, ParamCost::Pipeline)
                .Source(kConfig, "GLASS_BLUR_FLOOR_DP");
            b.Bool("spectralOn", "启用色散", "chromatic", &tune.spectralOn,
                   "原实现写死 spectral = 0（不开）；Kyant 的 LiquidSlider 是开的")
                .Source(kSliderSrc, "Slider::draw");
            b.Float("dispersion", "色散强度", "spectral", &tune.dispersion, 0.0f, 2.0f, "×",
                    "仅在上面打开后生效；上限 SPECTRAL_MAX = 2.0", 0.01f, ParamCost::Uniform,
                    "仅在启用色散时生效").Source(kConfig, "SPECTRAL_MAX");
            b.Float("depthAmount", "景深折射", "depthEffect", &tune.depthAmount, 0.0f, 1.0f, "×",
                    "原实现写死 0", 0.05f).Source(kSliderSrc, "Slider::draw");
            b.Float("darkGuard", "暗部保底", "darkGuard", &tune.darkGuard, 0.0f, 1.0f, "×", "",
                    0.05f).Source(kConfig, "DARK_GUARD_FLOOR");
            b.Group("背景采样");
            b.Bool("backdropOn", "启用背景采样", "backdropOn", &tune.backdropOn, "",
                   ParamCost::Pipeline).Source(kYtblUI, "PanelStyle::backdropOn");
            b.Float("backdropScaleXMin", "静止背景缩放 X", "stretchXMin",
                    &tune.backdropScaleXMin, 0.0f, 2.0f, "×",
                    "SLIDER_BACKDROP_X_MIN = 2/3（Kyant 同为 2/3）", 0.01f)
                .Source(kConfig, "SLIDER_BACKDROP_X_MIN");
            b.Float("backdropScaleXMax", "按压背景缩放 X", "stretchXMax",
                    &tune.backdropScaleXMax, 0.0f, 2.0f, "×", "SLIDER_BACKDROP_X_MAX = 1.0",
                    0.01f).Source(kConfig, "SLIDER_BACKDROP_X_MAX");
            b.Float("backdropScaleYMin", "静止背景缩放 Y", "stretchYMin",
                    &tune.backdropScaleYMin, 0.001f, 2.0f, "×",
                    "SLIDER_BACKDROP_Y_MIN = 0.55（★ Kyant 是 lerp(0,1,p)，下限为 0）", 0.01f)
                .Source(kConfig, "SLIDER_BACKDROP_Y_MIN");
            b.Float("backdropScaleYMax", "按压背景缩放 Y", "stretchYMax",
                    &tune.backdropScaleYMax, 0.001f, 2.0f, "×", "SLIDER_BACKDROP_Y_MAX = 1.0",
                    0.01f).Source(kConfig, "SLIDER_BACKDROP_Y_MAX");
            b.Float("backdropBlend", "背景混合量", "stretchMix", &tune.backdropBlend, 0.0f, 1.0f,
                    "×", "原实现写死 1.0", 0.01f).Source(kSliderSrc, "backdropBlend");
            b.Group("表面 / 光影");
            b.Float("surfaceAlphaGain", "表面透明度增益", "surfaceAlphaGain",
                    &tune.surfaceAlphaGain, 0.0f, 2.0f, "×", "原实现 surface.a = 1 - press", 0.01f)
                .Source(kSliderSrc, "Slider::draw");
            b.Enum("surfaceMode", "表面模式", "surfaceMode", &tune.surfaceMode, 0, 2,
                   kSurfaceModes, "原实现写死 Overlay").Source(kSliderSrc, "surfaceMode");
            b.Float("edgeAlphaGain", "边光强度", "edgeAlphaGain", &tune.edgeAlphaGain, 0.0f, 2.0f,
                    "×", "原实现 EdgeLight::ambient(1.0)", 0.01f).Source(kSliderSrc, "Slider::draw");
            b.Float("shadowRadiusDp", "外阴影半径", "radius", &tune.shadowRadiusDp, 0.0f, 48.0f,
                    "dp", "SLIDER_SHADOW_DP = 4", 0.5f, ParamCost::EffectChain)
                .Source(kConfig, "SLIDER_SHADOW_DP");
            b.Float("shadowOffsetYRatio", "阴影偏移比", "offsetYRatio", &tune.shadowOffsetYRatio,
                    0.0f, 1.0f, "×", "原实现 offsetY = radius/6", 0.01f)
                .Source(kConfig, "SHADOW_OFFSET_DIVISOR");
            b.Float("shadowAlpha", "阴影强度", "shadowAlpha", &tune.shadowAlpha, 0.0f, 1.0f, "×",
                    "", 0.01f);
            b.Float("insetRadiusDp", "内阴影半径", "insetRadius", &tune.insetRadiusDp, 0.0f, 40.0f,
                    "dp", "SLIDER_INNER_DP = 4；实际 = 它 × press", 0.5f, ParamCost::EffectChain)
                .Source(kConfig, "SLIDER_INNER_DP");
            b.Bool("insetOnPress", "按压时启用内阴影", "insetOnPress", &tune.insetOnPress,
                   "原实现 insetOn = press > ALPHA_MIN").Source(kSliderSrc, "insetOn");
            BindEdge(b, tune.edge, 5);
        }
    }
}

// -----------------------------------------------------------------------------
// 底部导航：父目标 + .background + .indicator
// -----------------------------------------------------------------------------
void BindNavigationTargets(Registry& registry, GlassScene& scene) {
    TargetDesc parent;
    parent.id = "secondary_window.navigation";
    parent.name = "底部导航栏";
    parent.type = "TabStrip";
    parent.window = "secondary";
    parent.parent = "secondary_window";
    parent.order = 500;
    parent.note = "液态玻璃底部标签栏（Kyant 的 LiquidBottomTabs）";
    TabStripTune& tune = scene.tabs.tune;
    {
        TargetBuilder b(registry, parent);
        if (b.created()) {
            b.Group("布局");
            b.Bool("visible", "显示导航栏", "visible", &scene.stage.tabsVisible,
                   "关掉后不提交任何面板（组件整体不绘制）").Source(kTabsSrc, "TabStrip::draw");
            b.Int("tabCount", "标签数量", "tabCount", &scene.tabs.tabCount, 1, lgx::TAB_LIMIT, "个",
                  "Kyant 的 LiquidBottomTabs(tabsCount)").Source(kTabsSrc, "TabStrip::tabCount");
            b.Int("activeIndex", "选中项（0 基）", "activeIndex", &scene.tabs.activeIndex, 0,
                  lgx::TAB_LIMIT - 1, "", "与组件**双向绑定**：点标签会写回这里")
                .Source(kTabsSrc, "TabStrip::activeIndex");
            b.Float("heightDp", "外框高度", "heightDp", &scene.stage.tabsHeightDp, 24.0f, 200.0f,
                    "dp", "面板本体是 TAB_PANEL_H_DP，改高度只改变垂直居中位置", 1.0f)
                .Source(kConfig, "TAB_PANEL_H_DP");
            b.Float("widthFraction", "宽度占比", "widthFraction", &scene.stage.tabsWidthFraction,
                    0.2f, 1.0f, "×", "", 0.01f)
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_TabsWidthFraction");
            b.Float("scale", "整体倍率", "scale", &scene.stage.tabsScale, 0.3f, 2.5f, "×", "",
                    0.01f).Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                  "s_TabsScale");
            b.Group("文字与图标");
            b.Float("iconSizeDp", "图标大小", "iconSize", &scene.tabs.iconSizeDp, 8.0f, 64.0f, "dp",
                    "TAB_ICON_SIZE_DP = 28（Kyant 的 LiquidBottomTab 用 28dp）", 1.0f)
                .Source(kConfig, "TAB_ICON_SIZE_DP");
            b.Float("labelSizeDp", "文字大小", "labelSize", &scene.tabs.labelSizeDp, 6.0f, 32.0f,
                    "dp", "TAB_LABEL_SIZE_DP = 12", 0.5f).Source(kConfig, "TAB_LABEL_SIZE_DP");
            b.Color("iconColor", "图标强制色", "iconColor", scene.stage.tabsIconColor,
                    "0（全透明）= 由 ytbl 自己按主题决定").Source(kTabsSrc, "iconColor");
            b.Group("交互");
            b.Float("slopDp", "拖动判定阈值", "slop", &tune.slopDp, 0.0f, 40.0f, "dp",
                    "TAB_SLOP_DP = 8", 0.5f).Source(kConfig, "TAB_SLOP_DP");
            b.Float("panelShiftDp", "拖动位移上限", "panelShift", &tune.panelShiftDp, 0.0f, 40.0f,
                    "dp", "TAB_PANEL_SHIFT_DP = 4（Kyant 同为 4dp）", 0.5f)
                .Source(kConfig, "TAB_PANEL_SHIFT_DP");
            b.Float("cellScale", "按压缩放", "cellScale", &tune.cellScale, 1.0f, 2.0f, "×",
                    "TAB_CELL_SCALE = 1.2（Kyant 的 LiquidBottomTabScale = 1.2）", 0.01f)
                .Source(kConfig, "TAB_CELL_SCALE");
            b.Float("panelGrowDp", "面板膨胀", "panelGrow", &tune.panelGrowDp, 0.0f, 64.0f, "dp",
                    "TAB_PANEL_GROW_DP = 16（Kyant 同为 16dp）", 1.0f)
                .Source(kConfig, "TAB_PANEL_GROW_DP");
            b.Float("velocityGain", "速度增益", "velocityGain", &tune.velocityGain, 0.0f, 1.0f,
                    "×", "TAB_VELOCITY_GAIN = 0.1", 0.005f).Source(kConfig, "TAB_VELOCITY_GAIN");
            b.Float("indicatorHeldScale", "指示器按压缩放", "heldScale", &tune.indicatorHeldScale,
                    1.0f, 2.0f, "×", "TAB_INDICATOR_HELD_SCALE = 78/56 ≈ 1.393"
                                     "（Kyant 的 pressedScale 同为 78f/56f）", 0.01f)
                .Source(kConfig, "TAB_INDICATOR_HELD_SCALE");
            b.Float("panelStiffness", "回位弹簧刚度", "stiffness", &tune.panelStiffness, 10.0f,
                    1000.0f, "×", "TAB_PANEL_STIFFNESS = 300（Kyant: spring(1f, 300f, 0.5f)）",
                    5.0f).Source(kConfig, "TAB_PANEL_STIFFNESS");
            b.Float("panelVisible", "弹簧静止阈值", "visibility", &tune.panelVisible, 0.01f, 2.0f,
                    "×", "TAB_PANEL_VISIBLE = 0.5（Kyant 的静止阈值同为 0.5）", 0.01f)
                .Source(kConfig, "TAB_PANEL_VISIBLE");
            b.Group("颜色");
            b.Color("accentDark", "深色主题强调色", "accentDark", scene.stage.tabsAccentDark, "")
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_TabsAccentDark");
            b.Color("accentLight", "浅色主题强调色", "accentLight", scene.stage.tabsAccentLight, "")
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_TabsAccentLight");
            b.Color("containerDark", "深色主题容器色", "containerDark",
                    scene.stage.tabsContainerDark, "Kyant: 0xFF121212 @ 0.4f")
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_TabsContainerDark");
            b.Color("containerLight", "浅色主题容器色", "containerLight",
                    scene.stage.tabsContainerLight, "Kyant: 0xFFFAFAFA @ 0.4f")
                .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                        "s_TabsContainerLight");
        }
    }

    TargetDesc background = parent;
    background.id = "secondary_window.navigation.background";
    background.name = "导航栏 · 背景层";
    background.type = "NavigationBackground";
    background.parent = parent.id;
    background.order = 501;
    background.note = "容器面板：模糊 + 折射 + 表面色 + 边光 + 投影 + 按压高光斑";
    {
        TargetBuilder b(registry, background);
        if (b.created()) {
            b.Group("折射 / 模糊");
            b.Float("blurDp", "模糊半径", "blur", &tune.blurDp, 0.0f, 10.0f, "dp",
                    "TAB_BLUR_DP = 8（Kyant 的 LiquidBottomTabs 也是 8dp）", 0.1f,
                    ParamCost::Pipeline).Source(kConfig, "TAB_BLUR_DP");
            b.Float("lensHeightDp", "折射高度", "lensHeight", &tune.lensHeightDp, 0.0f, 64.0f,
                    "dp", "TAB_LENS_HEIGHT_DP = 24（Kyant 同为 24dp）", 0.5f)
                .Source(kConfig, "TAB_LENS_HEIGHT_DP");
            b.Float("lensAmountDp", "折射强度", "lensAmount", &tune.lensAmountDp, 0.0f, 64.0f,
                    "dp", "TAB_LENS_AMOUNT_DP = 24（Kyant 同为 24dp）", 0.5f)
                .Source(kConfig, "TAB_LENS_AMOUNT_DP");
            b.Bool("colorBoost", "鲜艳度增强", "vibrancy", &tune.colorBoost,
                   "原实现写死 true（Kyant 的 LiquidBottomTabs 调 vibrancy()）")
                .Source(kTabsSrc, "TabStrip::draw");
            b.Float("depthAmount", "景深折射", "depthEffect", &tune.depthAmount, 0.0f, 1.0f, "×",
                    "原实现写死 0", 0.05f).Source(kTabsSrc, "TabStrip::draw");
            b.Group("表面 / 光影");
            b.Bool("shadowOn", "启用投影", "shadowOn", &tune.shadowOn, "")
                .Source(kTabsSrc, "TabStrip::draw");
            b.Float("shadowRadiusDp", "投影半径", "radius", &tune.shadowRadiusDp, 0.0f, 96.0f, "dp",
                    "原实现取 DropShadow 默认 24dp", 0.5f, ParamCost::EffectChain)
                .Source(kYtblUI, "DropShadow::radiusDp");
            b.Float("shadowOffsetYDp", "投影偏移 Y", "offsetY", &tune.shadowOffsetYDp, -64.0f,
                    64.0f, "dp", "原实现 = radius/6 = 4dp", 0.5f, ParamCost::EffectChain)
                .Source(kYtblUI, "DropShadow::offsetYDp");
            b.Color("shadowTint", "投影颜色", "color", tune.shadowTint, "RGBA（原 0x1A000000）")
                .Source(kYtblUI, "DropShadow::color");
            b.Float("shadowAlpha", "投影强度", "alpha", &tune.shadowAlpha, 0.0f, 1.0f, "×", "",
                    0.01f);
            b.Group("能力缺口（只读）");
            b.Unsupported("gapIconLayer", "图标层的独立玻璃效果", "iconLayer",
                          "G14：Kyant 的 LiquidBottomTabs 有独立的图标层（放大的背景 + accent 着色），"
                          "ytbl 的 TabStrip 只有背景+指示器两层，没有图标层");
        }
    }

    TargetDesc indicator = parent;
    indicator.id = "secondary_window.navigation.indicator";
    indicator.name = "导航栏 · 指示器";
    indicator.type = "NavigationIndicator";
    indicator.parent = parent.id;
    indicator.order = 502;
    indicator.note = "选中项滑块：按压时出现透镜折射 + 内阴影 + 投影";
    {
        TargetBuilder b(registry, indicator);
        if (b.created()) {
            b.Group("折射");
            b.Float("pressLensHeightDp", "按压折射高度", "lensHeight",
                    &tune.pressLensHeightDp, 0.0f, 40.0f, "dp",
                    "TAB_PRESS_LENS_HEIGHT_DP = 10（Kyant 的 thumb 同为 10dp×p）", 0.5f)
                .Source(kConfig, "TAB_PRESS_LENS_HEIGHT_DP");
            b.Float("pressLensAmountDp", "按压折射强度", "lensAmount",
                    &tune.pressLensAmountDp, 0.0f, 60.0f, "dp",
                    "TAB_PRESS_LENS_AMOUNT_DP = 14（Kyant 同为 14dp×p）", 0.5f)
                .Source(kConfig, "TAB_PRESS_LENS_AMOUNT_DP");
            b.Float("spectral", "色散强度", "spectral", &tune.spectral, 0.0f, 2.0f, "×",
                    "TAB_SPECTRAL = 1.0（★ 与 Toggle/Slider 不同，指示器的色散是常开的）", 0.01f)
                .Source(kConfig, "TAB_SPECTRAL");
            b.Group("表面 / 光影");
            b.Float("idleAlpha", "静止表面 alpha", "idleAlpha", &tune.idleAlpha, 0.0f, 1.0f, "×",
                    "TAB_IDLE_ALPHA = 0.10", 0.01f).Source(kConfig, "TAB_IDLE_ALPHA");
            b.Float("pressAlpha", "按压表面 alpha", "pressAlpha", &tune.pressAlpha, 0.0f, 1.0f,
                    "×", "TAB_PRESS_ALPHA = 0.03", 0.01f).Source(kConfig, "TAB_PRESS_ALPHA");
            b.Float("pressInnerDp", "按压内阴影半径", "pressInner", &tune.pressInnerDp, 0.0f, 40.0f,
                    "dp", "TAB_PRESS_INNER_DP = 8（Kyant 的 InnerShadow 也是 8dp×p）", 0.5f,
                    ParamCost::EffectChain).Source(kConfig, "TAB_PRESS_INNER_DP");
            b.Float("shadowDp", "投影半径", "shadowRadius", &tune.indicatorShadowDp, 0.0f, 64.0f,
                    "dp", "TAB_SHADOW_DP = 24（Kyant 用 Shadow() 默认 24dp）", 0.5f,
                    ParamCost::EffectChain).Source(kConfig, "TAB_SHADOW_DP");
            b.Float("shadowAlpha", "投影强度", "shadowAlpha", &tune.indicatorShadowAlpha, 0.0f,
                    1.0f, "×", "TAB_SHADOW_ALPHA = 0.10；实际 = 它 × press", 0.01f)
                .Source(kConfig, "TAB_SHADOW_ALPHA");
        }
    }
}

// -----------------------------------------------------------------------------
// 6 枚胶囊芯片 + 芯片行布局
// -----------------------------------------------------------------------------
void BindChipTargets(Registry& registry, GlassScene& scene) {
    for (int i = 0; i < GlassScene::kChipCount; ++i) {
        char id[96];
        char name[96];
        std::snprintf(id, sizeof(id), "secondary_window.chip_%d", i + 1);
        std::snprintf(name, sizeof(name), "胶囊 %d", i + 1);
        TargetDesc desc;
        desc.id = id;
        desc.name = name;
        desc.type = "GlassChip";
        desc.window = "secondary";
        desc.parent = "secondary_window";
        desc.order = 600 + i;
        desc.note = i < 3 ? "形状行：圆角各自独立（胶囊 / 统一圆角 / 四角独立）"
                          : "边光行：边光模式各自独立（Gradient / Ambient / Plain）";
        TargetBuilder b(registry, desc);
        if (!b.created()) continue;
        b.Group("尺寸");
        b.Float("heightDp", "芯片高度", "chipHeight", &scene.stage.chipHeightDp, 16.0f, 160.0f,
                "dp", "三枚芯片共用的行高（原 px(44)）", 1.0f)
            .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_ChipHeightDp");
        b.Float("gapDp", "列间距", "chipGap", &scene.stage.chipGapDp, 0.0f, 64.0f, "dp",
                "原 px(16)，三枚共用", 1.0f)
            .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_ChipGapDp");
        BindPanelTune(b, scene.chips[i], kSceneSrc, true);
    }
}

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

// -----------------------------------------------------------------------------
// 液态玻璃卡片 / 玻璃实验场 / 弹出菜单 / 弹窗 / 能力缺口总表
// -----------------------------------------------------------------------------
void BindCardTarget(Registry& registry, GlassScene& scene) {
    TargetDesc desc;
    desc.id = "secondary_window.card";
    desc.name = "液态玻璃卡片";
    desc.type = "GlassCard";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 100;
    desc.note = "折射测试卡片：下面垫了 6 色条纹，专门用来看折射/色散是否真的在采样背景";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    BindPanelTune(b, scene.card, kSceneSrc, true);
}

void BindPlaygroundTarget(Registry& registry, GlassScene& scene) {
    TargetDesc desc;
    desc.id = "secondary_window.playground";
    desc.name = "玻璃实验场";
    desc.type = "GlassPlayground";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 900;
    desc.note = "可拖动/缩放的圆形玻璃对象：圆角比例、尺寸、折射都按对象尺寸归一化";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    StageTune& stage = scene.stage;

    b.Group("几何");
    b.Float("radiusRatio", "圆角比例", "cornerRadius", &stage.playgroundRadius, 0.1f, 1.0f, "×",
            "圆角 = 对象尺寸 × 0.5 × 这个比例（Kyant 的 playground 默认 0.5）", 0.01f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundRadius");
    b.Float("sizeDp", "对象大小", "size", &stage.playgroundSizeDp, 96.0f, 256.0f, "dp",
            "上限 256dp 由本示例的拖拽手柄范围决定", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundSizeDp");
    b.Float("offsetX", "位置 X", "offsetX", &stage.playgroundOffsetX, -200.0f, 200.0f, "px",
            "相对于舞台中心（手指拖动会写回这里）", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundOffset");
    b.Float("offsetY", "位置 Y", "offsetY", &stage.playgroundOffsetY, -200.0f, 200.0f, "px", "",
            1.0f).Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                         "s_PlaygroundOffset");
    b.Bool("sheet", "显示内置参数面板", "showControls", &stage.playgroundSheet,
           "实验场自己那一小片滑杆（保留原示例的行为）")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundSheet");

    b.Group("折射 / 模糊");
    b.Bool("chromaticOn", "启用色散", "chromatic", &scene.playground.spectralOn,
           "与卡片共用 s_Chromatic 的语义，但这里是独立字段").Source(kSceneSrc, "PanelTune");
    b.Float("dispersion", "色散强度", "dispersion", &scene.playground.dispersion, 0.0f, 2.0f,
            "×", "原实现 s_PlaygroundDispersion = 0.90", 0.01f, ParamCost::Uniform,
            "仅在启用色散时生效").Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                         "s_PlaygroundDispersion");
    b.Float("blurDp", "模糊半径", "blur", &scene.playground.blurDp, 0.0f, 10.0f, "dp",
            "原实现 s_PlaygroundBlur = 0（Kyant 的 playground 默认也是 0）", 0.1f,
            ParamCost::Pipeline).Source(kConfig, "BLUR_RADIUS_MAX_DP")
        .Deferred(&scene.playground.blurAppliedDp);
    b.Float("heightRatio", "折射高度比例", "refractionHeight", &stage.playgroundHeight, 0.0f,
            1.0f, "×", "实际折射高度 = 比例 × 对象尺寸 × 0.5（Kyant 的 playground 同构）", 0.01f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundHeight");
    b.Float("amountRatio", "折射强度比例", "refractionAmount", &stage.playgroundAmount, 0.0f,
            1.0f, "×", "实际折射强度 = 比例 × 对象尺寸（Kyant 同构）", 0.01f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_PlaygroundAmount");
    b.Float("depthAmount", "景深折射", "depthEffect", &scene.playground.depthAmount, 0.0f, 1.0f,
            "×", "原实现写死 1.0（Kyant 的 playground 用 depthEffect = true）", 0.05f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "playgroundStyle");
    BindPanelTune(b, scene.playground, kSceneSrc, true);
}

void BindMenuTarget(Registry& registry, GlassScene& scene) {
    TargetDesc desc;
    desc.id = "secondary_window.popmenu";
    desc.name = "弹出菜单";
    desc.type = "PopMenu";
    desc.window = "secondary";
    desc.parent = "secondary_window";
    desc.order = 700;
    desc.note = "胶囊触发的液态弹出菜单（Kyant 的 PopMenu 移植）";
    TargetBuilder b(registry, desc);
    if (!b.created()) return;
    lgx::PopMenu& menu = scene.menu;
    StageTune& stage = scene.stage;

    b.Group("触发胶囊");
    b.Float("capWidthDp", "胶囊宽度", "capWidth", &stage.menuCapWidthDp, 40.0f, 420.0f, "dp",
            "原 px(180)", 1.0f).Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                       "s_MenuCapWidthDp");
    b.Float("capHeightDp", "胶囊高度", "capHeight", &stage.menuCapHeightDp, 24.0f, 160.0f, "dp",
            "原 px(44)", 1.0f)
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_MenuCapHeightDp");
    b.Float("capTextSp", "胶囊文字大小", "capTextSp", &menu.capTextSp, 8.0f, 32.0f, "sp",
            "MENU_CAP_TEXT_SP = 17", 0.5f).Source(kConfig, "MENU_CAP_TEXT_SP");
    b.Group("菜单面板配方");
    b.Float("widthDp", "面板宽度", "width", &menu.widthDp, 80.0f, 520.0f, "dp",
            "MENU_DEFAULT_W_DP = 200", 1.0f).Source(kConfig, "MENU_DEFAULT_W_DP");
    b.Float("cornerDp", "面板圆角", "corner", &menu.cornerDp, 0.0f, 96.0f, "dp",
            "MENU_DEFAULT_CORNER_DP = 32", 0.5f).Source(kConfig, "MENU_DEFAULT_CORNER_DP");
    b.Float("bandCornerDp", "高亮条圆角", "bandCorner", &menu.bandCornerDp, 0.0f, 96.0f, "dp",
            "MENU_DEFAULT_BAND_CORNER_DP = 24", 0.5f)
        .Source(kConfig, "MENU_DEFAULT_BAND_CORNER_DP");
    b.Float("threadDp", "连接线粗细", "thread", &menu.threadDp, 0.0f, 40.0f, "dp",
            "MENU_DEFAULT_THREAD_DP = 5.5", 0.1f).Source(kConfig, "MENU_DEFAULT_THREAD_DP");
    b.Float("gapDp", "胶囊与面板的间隙", "gap", &menu.gapDp, 0.0f, 160.0f, "dp",
            "MENU_DEFAULT_GAP_DP = 44", 0.5f).Source(kConfig, "MENU_DEFAULT_GAP_DP");
    b.Float("greetDp", "问候位移", "greet", &menu.greetDp, 0.0f, 96.0f, "dp",
            "MENU_DEFAULT_GREET_DP = 12", 0.5f).Source(kConfig, "MENU_DEFAULT_GREET_DP");
    b.Float("drainStart", "排水起点", "drainStart", &menu.drainStart, 0.0f, 1.0f, "×",
            "MENU_DRAIN_START = 0.10", 0.01f).Source(kConfig, "MENU_DRAIN_START");
    b.Float("drainSpan", "排水跨度", "drainSpan", &menu.drainSpan, 0.01f, 1.0f, "×",
            "MENU_DRAIN_SPAN = 0.80", 0.01f).Source(kConfig, "MENU_DRAIN_SPAN");
    b.Float("stretchGain", "拉伸增益", "stretchGain", &menu.stretchGain, 0.0f, 3.0f, "×", "",
            0.01f).Source(kConfig, "MENU_JELLY_MAX");
    b.Float("jelly", "果冻强度", "jelly", &menu.jelly, 0.0f, 3.0f, "×", "上限 MENU_JELLY_MAX = 2",
            0.01f).Source(kConfig, "MENU_JELLY_MAX");
    b.Bool("materialFlare", "材质闪光", "materialFlare", &menu.materialFlare,
           "Kyant 的 material flare（菜单展开末段的亮部）").Source(kConfig, "MENU_FLARE_SPECTRAL");
    b.Group("边光与高亮条");
    b.Float("rimDp", "边光宽度倍率", "rimDp", &menu.rimDp, 0.0f, 4.0f, "×",
            "MENU_RIM_WIDTH_DP = 1.0 的倍率", 0.05f).Source(kConfig, "MENU_RIM_WIDTH_DP");
    b.Float("rimAlpha", "边光强度", "rimAlpha", &menu.rimAlpha, 0.0f, 1.0f, "×",
            "MENU_RIM_ALPHA = 0.52 的倍率", 0.01f).Source(kConfig, "MENU_RIM_ALPHA");
    b.Color("bandColor", "高亮条颜色", "bandColor", scene.stage.menuBandColor,
            "MENU_BAND = 0x3DFFFFFF").Source(kConfig, "MENU_BAND");
    b.Group("行为");
    b.Enum("reach", "伸出方向", "reach", &scene.stage.menuReach, 0, 4,
           "自动\0右下\0左下\0右上\0左上\0", "MenuReach")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_MenuReach");
    b.Bool("clampToHost", "限制在宿主内", "clampToHost", &menu.clampToHost, "")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_MenuClampToHost");
    b.Bool("closeOnOutside", "点击外部关闭", "closeOnOutside", &menu.closeOnOutside, "")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                "s_MenuCloseOnOutside");
    b.Bool("expanded", "展开菜单", "expanded", &menu.expanded,
           "与组件双向绑定：点菜单外面关掉也会同步回来")
        .Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp", "s_MenuOpenRequest");
    b.Int("cursorRow", "光标行", "cursorRow", &menu.cursorRow, 0, lgx::MENU_ENTRY_LIMIT - 1, "",
          "指针悬停也会改它").Source("app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp",
                                     "s_MenuCursorRow");
    b.Group("能力缺口（只读）");
    b.Unsupported("gapMenuGlass", "菜单面板内部的玻璃配方", "menuPanelGlass",
                  "G14：菜单面板的模糊/透镜/边光是 PopupMenu.cpp 里的 MENU_CAP_* / MENU_LENS_* "
                  "编译期常量，PopMenu 没有对应字段（本次未改动该文件）");
}

void BindDialogTarget(Registry& registry, GlassScene& scene) {
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

    BindWindowTarget(registry, scene);
    BindResizeTarget(registry);
    BindCardTarget(registry, scene);
    // ★★ 调试注册与组件存在**解耦**（用户本轮任务书 §四）：
    //    按钮行的第 5 枚 = 「打开液态弹窗」触发按钮 —— 它**真实存在、可点、功能完整**，
    //    但**不注册进调试器**（用户要求："弹窗及其触发按钮不需要提供独立的主题调试功能"）。
    //    所以这里循环到 kButtonCount-1；`ButtonId(4)` 也刻意没有对应的目标 ID。
    //    ★ 这同时消掉了一处"死控件"隐患：从前 `pill_button_5` 绑的是
    //      `scene.buttons[4]`，而屏幕上画的是另一个对象（`dialogTrigger`），
    //      那一页 60+ 条参数改了也不会有任何可见效果。
    for (int i = 0; i < GlassScene::kButtonCount - 1; ++i) {
        BindButtonTarget(registry, scene, i);
    }
    for (int i = 0; i < GlassScene::kToggleCount; ++i) {
        BindToggleTargets(registry, scene, i);
    }
    for (int i = 0; i < GlassScene::kSliderCount; ++i) {
        BindSliderTargets(registry, scene, i);
    }
    BindNavigationTargets(registry, scene);
    BindChipTargets(registry, scene);
    BindMenuTarget(registry, scene);
    // ★★ 「液态弹窗」不再有专属调试分页（用户要求 §四.2-4）：
    //    弹窗的玻璃配方本来就是 `Dialog.cpp` 的编译期常量（只公开过 `glassBlurDp`），
    //    现在连那一条也不再暴露 ⇒ 不再有"无意义的弹窗专属调试参数"。
    //    ★ 弹窗本身（渲染、动画、事件）完全不受影响 —— 它不属于注册表驱动的组件。
    // BindDialogTarget(registry, scene);   ← 按用户要求移除入口，**不是**移除弹窗
    BindPlaygroundTarget(registry, scene);
    BindGapTarget(registry);
}

}  // namespace glass
}  // namespace aurora
