#include "LiquidGlassRuntime.h"
#include "glass/DebugHighlight.h"

#include "ScreenRecorder.h"   // 录屏开关（内置 screenrecord，见该头文件顶部的理由）
#include "GlassWorkbench.h"   // ★ Liquid Glass Inspector 调试工作台
#include "GlassTargets.h"     // ★ 组件注册表（每帧无条件注册，二级窗口关掉也在）
#include "glass/GlassRegistry.h"  // ★ 缩放方式等参数走注册表 ⇒ 持久化/撤销/预设白拿
#include "CornerResizeOverlay.h"  // ★ 右下角长按缩放（与二级窗口共用同一套）
#include "ImeAvoid.h"            // ★ 输入法弹层避让（纯逻辑，有离线回归）
#include "Renderer.h"            // ★ 取输入法弹层矩形（同帧、同线程）
#include "imgui.h"
#include "imgui_internal.h"   // ImGuiWindow / GetCurrentWindow / SetWindowPos（窗口夹取用）
#include "third_party/liquidglass/secondary_imgui.h"
#include "third_party/liquidglass/ytbl/GlassUI.h"

// M-09 动效实验场（控制中心进出场）—— ytbl 里唯一没有原生实现的一条动效。
// 它独占主题窗口的第三个标签页，原因见下面 BeginTabItem("动效实验场") 的注释。
#include "MotionLab.h"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cmath>     // std::sqrt（界面上算 ζ / 预计时长）
#include <ctime>
#include <mutex>
#include <string>

namespace aurora {
namespace {

constexpr char kLogTag[] = "Aurora";

bool g_initialized = false;
bool g_stylePrepared = false;
std::string g_recordNote;   // 录屏开关的一行反馈（成功=路径，失败=原因）
std::atomic<bool> g_visible{false};
std::mutex g_regionMutex;
TouchRegion g_region{};
// Backdrop source logging: state changes only, so logcat shows which input the
// glass actually sampled without spamming one line per frame.
bool g_backdropLogged = false;
BackdropOrigin g_backdropOrigin = BackdropOrigin::None;
std::uintptr_t g_backdropTexture = 0;
int g_backdropMappingWidth = 0;
int g_backdropMappingHeight = 0;
// 主窗口「主题」的矩形（物理像素；0 表示窗口当前不存在）。
// 只由渲染线程读写（DrawLiquidGlassShowcase 与 UpdateLiquidGlassDiagnostics 在同一线程）。
float g_themeRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};

// ---- 输入法避让（adjustPan）用的状态（都只在渲染线程读写）------------------
//   g_imeRect      : 当前键盘矩形（物理像素；全 0 = 未知/已收起），来自 Java 的 WindowInsets
//   g_themeWindowPos/Size : 上一帧主题窗口**实际**的位置与尺寸（含已施加的避让位移）
//   g_themeImeShift: 已经施加给主题窗口的上移量（还原逻辑位置要用它）
float g_imeRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
float g_themeWindowPos[2] = {16.0f, 730.0f};
float g_themeWindowSize[2] = {760.0f, 1400.0f};
float g_themeImeShift = 0.0f;

// ===========================================================================
//  主题窗口的**触摸滚动** —— ★ 已按用户要求**整段删除**
// ===========================================================================
//  用户原话（2026-10-14）：
//    「把主题标签页窗口的滑动空白区域翻页功能删掉，它只需要窗口右边那个滑动条
//      来控制上下翻动，不需要滑动空白区域」
//
//  ⇒ 主题窗口的滚动只由**右侧滚动条**负责（ImGui 自带：拖动滑块 / 点空白页）。
//    空白处滑动**不再**滚动，也就不会再出现"想在空白处滚一下、结果内容动了"
//    这类与长按缩放抢手势的模糊地带。
//
//  ★ 同时被删掉的还有那套 `ThemeScrollRuntime` + 惯性推进 + `[滚动]` 日志探针
//    （它们是这条功能的一部分，留着就是死代码）。
//  ★ 二级窗口**不受影响**：它按设计没有滚动条（NoScrollbar），内容拖动是它唯一的
//    滚动方式（`RunSecondaryWindowGestures` 在 secondary_imgui.cpp 里，没动）。
//  ★ 主题窗口的"空白处拖动 = 移动窗口"在更早的一轮里已经关掉了
//    （Renderer.cpp 的 `ConfigWindowsMoveFromTitleBarOnly = true`），
//    所以现在空白处滑动**什么都不会发生** —— 这正是用户要的行为。
// ===========================================================================

// ★ 喂给调试工作台诊断面板的运行时事实（更新在 UpdateLiquidGlassDiagnostics，
//   读取在 BeginLiquidGlassFrame；两者都在渲染线程上，因此不需要加锁）。
double g_lastDiagnosticFps = 0.0;
double g_lastDiagnosticWorstMs = 0.0;
unsigned long long g_lastDiagnosticFrames = 0;
float g_lastContentMaskRadius = -1.0f;
const char* g_shaderError = "";
const char* g_lastEffectChain = "";

// ★★ 「用户意图」：二级窗口是否显示。**必须与 secondary_imgui 内部的 s_Visible 分开。**
//
// 为什么必须分开（用户实测报的 bug）：二级窗口标题栏上那个 × 本来就是 ImGui 的
// 标准关闭按钮，点击**确实**把 s_Visible 置成了 false —— 但本函数曾经每帧无条件
// `secondary_imgui::SetVisible(true)`，下一帧又把它打开了。用户看到的现象就是
// 「那个叉点了没有任何效果」。现在 × 的结果被读回来当作用户意图保存。
bool g_secondaryOpen = true;

// ★★★ 主题窗口标题栏的 × = **退出整个程序**，不是"收起窗口"。
//
// 用户明确纠正过一次：他点 × 要的就是「直接关闭这个程序」。
// 我第一版理解成了"关掉这个窗口"，于是加了回收栏让他能再点回来 —— 那是错的，
// 他要的是这个进程结束（他可以自己关，不用叫我）。
//
// 这里只"请求"退出：真正退出由 aurora_overlay.cpp 的主循环看到
// LiquidGlassQuitRequested() 之后走**正常收尾路径**（停采集、收键盘、拆图层、删临时目录），
// 退出码 0 —— 和 --max-seconds 到期完全同一条路，不做硬退。
std::atomic<bool> g_quitRequested{false};

// 主题窗口是否还在画。正常情况恒为 true（它是主界面，只有"退出程序"没有"关闭窗口"）；
// 用户在 × 上按下之后、主循环真正退出之前的这几帧里置 false，免得窗口还闪一下。
bool g_themeDrawn = true;

// ★ 主窗口（主题）自己的"右下角长按缩放"运行时。
//   与二级窗口走的是**同一个模块**（CornerResizeOverlay）：同一套长按内核、
//   同一条同心圆弧手柄、同一个"只震一次"的触觉门 —— 两个窗口手感不可能走偏。
aurora::CornerResizeRuntime g_themeResize;
// 「运行时」标签页要展示最近一次喂进来的 HostInfo（含活动触点数与手势量）。
secondary_imgui::HostInfo g_lastHostInfo{};

// ===========================================================================
//  「主题 → 窗口缩放」设置页
// ===========================================================================
//  用户要求（逐条对应）：
//   · 提供「实时缩放 / 虚线预览缩放」两个选项，控件与现有 UI 一致（单选按钮）；
//   · 默认保留实时缩放；
//   · 选择结果持久化，重启后恢复；
//   · 切换模式**不改变当前窗口宽高**（这里只写一个枚举，绝不碰任何窗口尺寸）；
//   · 手势进行中切换不会让状态错乱（模式在模块里按会话锁定，见 CornerResizeOverlay）；
//   · 新增模式的动画参数有配置入口（弹簧刚度/阻尼/填充模糊/边缘流动…）。
//
//  ★ 持久化走**注册表**：这些值与 Liquid Glass Inspector 里的
//    「窗口缩放方式」目标是同一块内存，写入注册表会自动进入撤销栈并触发
//    去抖自动保存（/data/local/tmp/aurora_glass_params.json，原子写 + 版本化 +
//    重启恢复）。注册表里没有这个目标时退回直接写，保证界面永远不"点了没反应"。
namespace {

constexpr char kResizeTarget[] = "secondary_window.resize";

bool ResizeTargetExists() {
    return aurora::glass::Global().FindTarget(kResizeTarget) != nullptr;
}

void StoreResizeMode(int mode) {
    aurora::ResizeSettingsRef().mode = mode;   // 主存储（界面每帧从这里读）
    if (ResizeTargetExists()) {
        aurora::glass::Global().SetI(kResizeTarget, "mode", mode,
                                     aurora::glass::ChangeOrigin::Ui);
    }
}

void StoreResizeFloat(const char* id, float value, float* ref) {
    if (ResizeTargetExists()) {
        aurora::glass::Global().SetF(kResizeTarget, id, value,
                                     aurora::glass::ChangeOrigin::Ui);
        return;   // 注册表是绑定的指针 ⇒ 值已经写进 *ref（并且已经排进保存队列）
    }
    *ref = value;
}

void StoreResizeBool(const char* id, bool value, bool* ref) {
    if (ResizeTargetExists()) {
        aurora::glass::Global().SetB(kResizeTarget, id, value,
                                     aurora::glass::ChangeOrigin::Ui);
        return;
    }
    *ref = value;
}

// 一行"参数 = 滑块 + 数值 + 恢复默认"。控件形态与「液态玻璃」页保持一致
// （那边也是 ImGui 的滑块/复选/下拉，只是由注册表自动生成）。
bool ResizeSlider(const char* label, const char* id, float* ref, float lo, float hi,
                  const char* unit, const char* help) {
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("0000.0").x
                            - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    const bool changed = ImGui::SliderFloat(label, ref, lo, hi,
                                            (unit != nullptr && unit[0] != '\0') ? "%.2f" : "%.2f");
    if (changed) StoreResizeFloat(id, *ref, ref);
    if (help != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("%s", help);
    }
    ImGui::PopID();
    return changed;
}

void DrawResizeSettingsPage() {
    aurora::ResizeSettings& settings = aurora::ResizeSettingsRef();
    aurora::ResizePreviewParams& p = settings.preview;

    ImGui::TextUnformatted("窗口缩放方式");
    ImGui::TextDisabled("两个窗口（主题窗口 / 二级液态玻璃窗口）共用这一套设置");
    ImGui::Spacing();

    // ---- 两个模式：单选按钮（与现有 UI 一致的控件）----
    const bool live = settings.mode == static_cast<int>(aurora::ResizeMode::Live);
    if (ImGui::RadioButton("实时缩放", live)) {
        StoreResizeMode(static_cast<int>(aurora::ResizeMode::Live));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("拖动时窗口宽高实时改变（原有交互）");
    }
    const bool preview = settings.mode == static_cast<int>(aurora::ResizeMode::DashedPreview);
    if (ImGui::RadioButton("虚线预览缩放", preview)) {
        StoreResizeMode(static_cast<int>(aurora::ResizeMode::DashedPreview));
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("拖动时只调整虚线预览框；松手后窗口才通过液态玻璃填充动画\n"
                          "过渡到预览尺寸（左上角固定）");
    }

    ImGui::Spacing();
    if (live) {
        ImGui::TextDisabled("当前：长按右下角 %.0fms 后拖动，窗口宽高实时跟手；松手即定尺寸。",
                            static_cast<double>(aurora::LONG_PRESS_TRIGGER_MS));
    } else {
        ImGui::TextDisabled("当前：长按右下角 %.0fms 后拖动，只有虚线框变化；松手后窗口以"
                            "填充动画过渡。",
                            static_cast<double>(aurora::LONG_PRESS_TRIGGER_MS));
    }
    ImGui::TextDisabled("两种方式共用：长按阈值、判定区、尺寸上下限、手柄/边框/震动反馈。");
    ImGui::TextDisabled("手势或动画进行中切换不会打断当前这次操作，新模式从下一次手势开始生效。");

    ImGui::Separator();
    ImGui::TextUnformatted("虚线预览框");
    ResizeSlider("线宽", "dashWidthDp", &p.lineWidthDp, 0.5f, 4.0f, "dp",
                 "前景层圆角虚线；下面垫了一层略粗的暗线，亮/暗背景下都看得见");
    ResizeSlider("虚线段长度", "dashLengthDp", &p.dashLengthDp, 2.0f, 24.0f, "dp",
                 "按弧长排布 → 圆角处不会变短（看起来不会毛糙）");
    ResizeSlider("虚线间隔", "dashGapDp", &p.dashGapDp, 1.0f, 20.0f, "dp", nullptr);
    ResizeSlider("圆角增量", "cornerAddDp", &p.cornerRadiusAddDp, -16.0f, 32.0f, "dp",
                 "预览框圆角 = 窗口圆角 + 增量（0 = 与窗口几何规则完全一致）");
    ImGui::Checkbox("显示预计宽高", &p.showSizeLabel);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        StoreResizeBool("sizeLabel", p.showSizeLabel, &p.showSizeLabel);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("数值画在预览框外面，不遮挡窗口内部的 Demo");
    }
    ResizeSlider("虚线框解体时机", "dissolveFrom", &p.previewDissolveFrom, 0.0f, 0.95f, "进度",
                 "几何走到这个进度之前虚线框保持实心，之后平滑淡出（不允许松手就消失）");

    ImGui::Separator();
    ImGui::TextUnformatted("液态玻璃填充动画");
    ResizeSlider("弹簧刚度 k", "springStiffness", &p.springStiffness, 20.0f, 900.0f, "",
                 "ω = √k：越大越快");
    ResizeSlider("弹簧阻尼 c", "springDamping", &p.springDamping, 4.0f, 120.0f, "",
                 "ζ = c / (2√k)：默认 38.5 → ζ≈0.94，几乎看不到过冲");
    {
        const float omega = std::sqrt(p.springStiffness > 1.0f ? p.springStiffness : 1.0f);
        const float zeta = p.springDamping / (2.0f * omega);
        ImGui::TextDisabled("ζ = %.2f ｜ 预计到位 %.0f ms（真实时长由帧时间推进的弹簧决定）",
                            static_cast<double>(zeta),
                            static_cast<double>(aurora::EstimateCommitDurationMs(p)));
    }
    ResizeSlider("前沿带宽", "fillRimWidthDp", &p.fillRimWidthDp, 4.0f, 120.0f, "dp",
                 "只有贴着移动前沿的一条带参与填充（整块新区域会在窗口中间留下矩形接缝）");
    ResizeSlider("前沿不透明度", "fillAlpha", &p.fillAlpha, 0.0f, 1.0f, "×",
                 "前沿那层玻璃的额外不透明度，随动画收敛到 0");
    ResizeSlider("前沿模糊差", "fillBlurDiffDp", &p.fillBlurDiffDp, 0.0f, 8.0f, "dp",
                 "过渡期间临时降低窗口整体模糊、前沿保持配置值 → 前沿真的更糊"
                 "（窗口模糊默认 10dp 已是 ytbl 上限，加不上去）");
    ResizeSlider("前沿折射增益", "fillRefractGain", &p.fillRefractGain, 1.0f, 2.0f, "×", nullptr);
    ResizeSlider("前沿色散增益", "fillSpectralGain", &p.fillSpectralGain, 1.0f, 2.0f, "×", nullptr);
    ResizeSlider("前沿柔光", "fillGlowAlpha", &p.fillGlowAlpha, 0.0f, 1.0f, "×",
                 "用 ImGui 多色矩形做的平滑渐隐（零 GPU 面板成本）");
    ResizeSlider("边缘流动", "edgeFlowGain", &p.edgeFlowGain, 0.0f, 3.0f, "×",
                 "动画期间窗口边缘的柔化与折射增强（0 → 峰值 → 0）");
    ResizeSlider("边缘柔化量", "edgeFlowSoftnessDp", &p.edgeFlowSoftnessDp, 0.0f, 3.0f, "dp",
                 "缩小时「收缩边缘」的柔性来自这里");
    ImGui::Checkbox("启用玻璃填充层", &p.fillOn);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        StoreResizeBool("fillOn", p.fillOn, &p.fillOn);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
        ImGui::SetTooltip("关掉只剩几何过渡（诊断/省电用）——几何动画仍然是真窗口尺寸变化");
    }

    ImGui::Spacing();
    if (ImGui::Button("动画参数恢复默认")) {
        settings.preview = aurora::ResizePreviewParams();
        // ★ 只重置**动画/预览**参数，不动「窗口缩放方式」本身 ——
        //   否则用户点一下"恢复默认"就被悄悄切回实时缩放，那是意外行为。
        if (aurora::glass::Target* target =
                aurora::glass::Global().MutableTarget(kResizeTarget)) {
            for (std::size_t i = 0; i < target->params.size(); ++i) {
                const std::string id = target->params[i].desc.id;
                if (id == "mode") continue;
                aurora::glass::Global().ResetParam(kResizeTarget, id,
                                                   aurora::glass::ChangeOrigin::Reset);
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("（参数与「液态玻璃」页的「窗口缩放方式」是同一份，改哪边都一样）");
}

}  // namespace

lgx::TextSize measureGlassText(const char* content, float size) {
    lgx::TextSize result;
    if (content == nullptr || ImGui::GetFont() == nullptr) {
        return result;
    }
    const ImVec2 measured = ImGui::GetFont()->CalcTextSizeA(size, 1.0e6f, 0.0f, content);
    result.width = measured.x;
    result.height = measured.y;
    return result;
}

void reportBackdropPlan(const BackdropPlan& plan, const BackendDescriptor& backend) {
    const bool changed = !g_backdropLogged || g_backdropOrigin != plan.origin ||
                         g_backdropTexture != plan.texture ||
                         g_backdropMappingWidth != plan.mappingWidth ||
                         g_backdropMappingHeight != plan.mappingHeight;
    if (!changed) {
        return;
    }
    g_backdropLogged = true;
    g_backdropOrigin = plan.origin;
    g_backdropTexture = plan.texture;
    g_backdropMappingWidth = plan.mappingWidth;
    g_backdropMappingHeight = plan.mappingHeight;
    // Nothing to sample on a translucent host means the glass falls back to its
    // own tint; flag it so the missing producer is visible in logcat.
    const int level = (plan.origin == BackdropOrigin::None && backend.hostSurfaceIsTransparent)
        ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
    __android_log_print(level, kLogTag,
                        "glass backdrop=%s texture=%u map=%dx%d transparent=%d projection=%d stale=%d neutral=%d",
                        BackdropOriginName(plan.origin),
                        static_cast<unsigned int>(plan.texture), plan.mappingWidth,
                        plan.mappingHeight, backend.hostSurfaceIsTransparent ? 1 : 0,
                        backend.backgroundSourceIsProjection ? 1 : 0,
                        backend.projectionSourceStale ? 1 : 0,
                        backend.neutralBackdropTextureHandle != 0 ? 1 : 0);
}

void bindBackdropPlan(const BackdropPlan& plan) {
    if (plan.texture != 0 && plan.mappingWidth > 0 && plan.mappingHeight > 0) {
        lgx::bindBackdropTexture(static_cast<unsigned int>(plan.texture), plan.mappingWidth,
                                 plan.mappingHeight);
    } else {
        lgx::releaseBackdropTexture();
    }
    // The demo re-enables this flag inside its own draw pass
    // (secondary_imgui.cpp), so the host must not fight it: the bound texture is
    // what selects the projection/neutral source over ytbl's own framebuffer
    // capture. Disabling the flag here would only desynchronize the two callers.
    lgx::setBackdropEnabled(true);
}

}  // namespace

void InitializeLiquidGlass() {
    if (g_initialized) {
        return;
    }
    secondary_imgui::InstallGlassBackend();
    // The supplied demo uses a 2x UI scale and physical-pixel ImGui display
    // coordinates. Keep that contract for the reused showcase.
    lgx::setScale(2.0f);
    lgx::setBackdropEnabled(true);
    lgx::setTextMeasurer(measureGlassText);
    g_initialized = lgx::startup();
}

void ShutdownLiquidGlass() {
    if (!g_initialized) {
        return;
    }
    // ★ 收尾前把调试工作台里未保存的参数落盘（原子写：先写临时文件再 rename）。
    //   用户的期望是"调完就自动记住"，而不是"必须记得点保存"。
    aurora::glass::FlushGlassWorkbench();
    lgx::shutdown();
    g_initialized = false;
    g_stylePrepared = false;
    g_visible.store(false, std::memory_order_release);
    std::lock_guard lock(g_regionMutex);
    g_region = {};
}

void BeginLiquidGlassFrame(float deltaSeconds, float width, float height,
                           float density, const InputSnapshot& input,
                           const BackendDescriptor& backend) {
    if (!g_initialized) {
        return;
    }
    // ★ 工作台的初始化（订阅历史、定位参数文件、首次从磁盘恢复）在这里做一次。
    //   放在 BeginFrame 里而不是构造期：那时注册表还没有任何目标，
    //   预设的"默认值"就无从捕获。
    aurora::glass::BeginGlassWorkbench();
    if (g_visible.load(std::memory_order_acquire) && !g_stylePrepared) {
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(2.0f);
        g_stylePrepared = true;
    }
    lgx::beginFrame(deltaSeconds);
    lgx::Surface surface;
    surface.width = width;
    surface.height = height;
    surface.pixelScaleX = std::max(0.1f, density);
    surface.pixelScaleY = std::max(0.1f, density);
    lgx::setSurface(surface);

    lgx::PointerState pointer;
    pointer.x = input.primary.x / std::max(0.1f, density);
    pointer.y = input.primary.y / std::max(0.1f, density);
    pointer.down = input.primaryDown;
    pointer.pressed = input.primaryPressed;
    pointer.released = input.primaryReleased;
    lgx::setPointer(pointer);

    // The glass samples exactly one backdrop. Selection and the pixel space it
    // must be addressed in live in PlanBackdrop (RenderBackend.h) so the wiring
    // is checked at compile time; a downscaled projection is addressed in host
    // screen pixels and therefore still covers the whole Surface.
    const BackdropPlan plan = PlanBackdrop(
        BackdropInputsFor(backend, static_cast<int>(width), static_cast<int>(height),
                          g_visible.load(std::memory_order_acquire)));
    reportBackdropPlan(plan, backend);
    bindBackdropPlan(plan);

    // ★ 把这一帧的背景采样事实喂给调试工作台的诊断面板。
    //   "液态玻璃到底采到了什么"是之前踩过坑最多的地方（忽略中间窗口、忽略轨道、
    //   忽略底层图标），所以它必须能被直接看到，而不是靠猜。
    aurora::glass::WorkbenchDiagnostics diagnostics;
    diagnostics.backdropOrigin = BackdropOriginName(plan.origin);
    diagnostics.backdropTexture = static_cast<unsigned int>(plan.texture);
    diagnostics.backdropMapWidth = plan.mappingWidth;
    diagnostics.backdropMapHeight = plan.mappingHeight;
    diagnostics.backdropTransparentHost = backend.hostSurfaceIsTransparent;
    diagnostics.fps = g_lastDiagnosticFps;
    diagnostics.worstFrameMs = g_lastDiagnosticWorstMs;
    diagnostics.frameCount = g_lastDiagnosticFrames;
    // ★ 用带锁的访问器读，不直接碰 g_region：
    //   诊断面板读的是"当前拦截区"，而这个矩形也可能被别的路径写
    //   （同一线程，但锁的成本可以忽略，而"忘记加锁"是会随重构爆的隐患）。
    const TouchRegion region = LiquidGlassRegion();
    diagnostics.clipLeft = region.left;
    diagnostics.clipTop = region.top;
    diagnostics.clipRight = region.right;
    diagnostics.clipBottom = region.bottom;
    diagnostics.contentMaskRadius = g_lastContentMaskRadius;
    diagnostics.shaderError = g_shaderError;
    diagnostics.effectChain = g_lastEffectChain;
    aurora::glass::SetWorkbenchDiagnostics(diagnostics);
}

void UpdateLiquidGlassDiagnostics(const HostCapabilities& host,
                                  const BackendDescriptor& backend,
                                  const BackendDescriptor& vulkanProbe,
                                  const FrameStats& frameStats,
                                  const InputSnapshot& input,
                                  const GestureFrame& gesture,
                                  bool textInputRequested) {
    secondary_imgui::HostInfo info;
    info.renderer = backend.name != nullptr ? backend.name : "unknown";
    info.host = host.mode == HostMode::Activity ? "Activity" :
                host.mode == HostMode::ApplicationOverlay ? "Overlay" : "RootNative";
    // ★ 键盘矩形：既缓存给主题窗口做避让，也转给二级窗口（它自己也要避让）。
    g_imeRect[0] = static_cast<float>(host.imeLeft);
    g_imeRect[1] = static_cast<float>(host.imeTop);
    g_imeRect[2] = static_cast<float>(host.imeRight);
    g_imeRect[3] = static_cast<float>(host.imeBottom);
    info.imeLeft = host.imeLeft;
    info.imeTop = host.imeTop;
    info.imeRight = host.imeRight;
    info.imeBottom = host.imeBottom;
    info.surfaceReady = host.hasSurface;
    info.imeAvailable = host.supportsIme;
    info.backgroundInput = backend.supportsBackgroundInput;
    info.textInputRequested = textInputRequested;
    info.frameCount = frameStats.frameCount;
    info.fps = frameStats.smoothedFps;
    info.worstFrameMilliseconds = frameStats.worstFrameMilliseconds;
    info.safeLeft = host.safeRegion.left;
    info.safeTop = host.safeRegion.top;
    info.safeRight = host.safeRegion.right;
    info.safeBottom = host.safeRegion.bottom;
    info.activePointers = input.activeCount;
    info.gesturePanX = gesture.panX;
    info.gesturePanY = gesture.panY;
    info.gestureScale = gesture.scale;
    info.gestureRotation = gesture.rotationRadians;
    info.vulkanDevices = vulkanProbe.physicalDeviceCount;
    info.surfaceFormats = vulkanProbe.surfaceFormatCount;
    info.presentModes = vulkanProbe.presentModeCount;
    info.vulkanDiagnostic = vulkanProbe.diagnostic;
    switch (vulkanProbe.state) {
    case BackendState::Ready: info.vulkanState = "ready"; break;
    case BackendState::SwapchainReady: info.vulkanState = "swapchain"; break;
    case BackendState::SurfaceReady: info.vulkanState = "surface"; break;
    case BackendState::Probed: info.vulkanState = "probed"; break;
    default: info.vulkanState = "unavailable"; break;
    }
    secondary_imgui::SetHostInfo(info);
    g_lastHostInfo = info;

    // 诊断面板要用的帧统计与宿主状态（同一线程，无需加锁）
    g_lastDiagnosticFps = static_cast<double>(frameStats.smoothedFps);
    g_lastDiagnosticWorstMs = static_cast<double>(frameStats.worstFrameMilliseconds);
    g_lastDiagnosticFrames = frameStats.frameCount;
    // 效果链摘要：把当前窗口玻璃真正生效的开关拼成一行，
    // 这样"我以为开了色散"和"渲染器真的在读色散"能一眼对上。
    {
        const aurora::glass::WorkbenchStats stats = aurora::glass::CurrentWorkbenchStats();
        (void)stats;
    }
}

void DrawLiquidGlassShowcase() {
    // ★★ 注册表 + 持久化泵：**无条件每帧跑**，与"展示窗口是否可见"无关。
    //
    //   这里曾经整个函数体都在 `if (g_visible)` 之内，于是：
    //     · 用户在主菜单里拖动调试滑块 → `registry.SetX()` 把新值写进编辑字段、
    //       并排进 pending_ 队列；
    //     · 而把 pending_ 落到渲染字段（applyRef）的 `Commit()` 只在展示窗口可见
    //       时才执行；
    //     ⇒ 展示窗口没开/被关掉时，**所有走 applyRef 的延迟参数永远不会生效**，
    //       表现就是"主菜单里的调试控件拖了完全没反应"（而少量直接写 ref 的参数
    //       看起来能用 —— 这正是"有的能调、有的调了没反应"的来源）。
    //
    //   Commit() 对空队列是零成本（`if (pending_.empty()) return 0;`），
    //   所以把它提到这里不会带来任何每帧开销。
    if (g_initialized) {
        aurora::glass::RegisterSecondaryTargets();       // 幂等
        aurora::glass::PumpGlassWorkbenchPersistence();  // 恢复 + Commit + 去抖保存
        // ★ 每帧清掉上一帧的高亮矩形（保留选中的目标 id，它由调试面板维护）。
        aurora::glass::debughl::BeginDebugFrame();
    }
    if (g_initialized && g_visible.load(std::memory_order_acquire)) {
        // The supplied demo selects Chinese whenever its embedded CJK font is
        // available. Aurora ships the same font asset, so preserve that UI
        // behavior instead of changing the demo's copy or layout.
        secondary_imgui::SetLanguage(true);
        // ★★ 注册表 + 持久化泵：**每帧无条件**跑（与"当前画的是哪一页"无关）。
        //   为什么必须在这里：
        //     · 二级窗口被用户关掉时（secondary_imgui::Draw 提前返回），里面的
        //       RegisterSecondaryTargets / Commit 都不会执行 ⇒ 参数目标会被 Sweep
        //       回收 ⇒ 界面上改的东西没地方存；
        //     · 用户停在「窗口缩放」页（不画「液态玻璃」页）时，恢复与自动保存
        //       原来也不会执行 ⇒ 改了模式却不会落盘。
        //   两条都属于"看起来能用、其实没记住"，必须堵掉。
        // （注册表与 Commit 已移到函数开头无条件执行，见上方说明。）

        // ★ 这里原来是一句无条件的 `secondary_imgui::SetVisible(true);` ——
        //   它就是"二级窗口的 × 点了没反应"的根因（每帧把用户的关闭结果盖回去）。
        //   现在可见性推迟到**画完主题窗口之后**再设：主题窗口里有「显示二级窗口」
        //   的开关，放在后面才能当帧生效。

        // ★ 主窗口「主题」必须先于二级窗口绘制：它是调试面板的宿主。
        //
        // 为什么必须补这一块：`secondary_imgui::DrawHostControls()` 在本仓库里
        // 声明了（secondary_imgui.h:51）、定义了（secondary_imgui.cpp:888），
        // 但**全仓没有任何调用点**（grep 只命中这两处）。而 showcase 模式下
        // Renderer 只调 DrawLiquidGlassShowcase() → secondary_imgui::Draw()，
        // 后者只画「二级液态玻璃」窗口（secondary_imgui.cpp:795-835）。
        // 结果就是：参数面板存在但没有任何窗口承载它，用户无从调节。
        //
        // 窗口标题/宽度沿用用户源码 main.cpp:154-155 的字面值（不自己发明）。
        //
        // ★ 位置必须避开二级窗口：`secondary_imgui::Draw()` 把二级窗口固定在
        //   (16,16)、尺寸 760x700（secondary_imgui.cpp:804-807），而且它在**之后**绘制
        //   ⇒ 永远盖在我们上面。原先把主题窗口放 (24,140) 会被完整遮住（真机截图确认过）。
        //   这里放到二级窗口正下方：y = 16+700+14 = 730。
        //
        // ★ 高度必须够大：调试面板有 12 个分组、上百个控件（每组件一组滑块/选择/取色器），
        //   用户源码的 600 只够显示第一组，其余全在折叠线以下需要反复滚动 ——
        //   真机截图确认过这个问题。屏幕高 2376，730+1400=2130 仍有余量。
        //   注意：改高度必须同步改 root-host/elf/aurora_overlay.cpp 里的 kUiBoxH
        //   （触摸拦截区要框住整个界面，否则下半部分点不动）。
        // ★ 宽度必须与二级窗口一致（760），不能更宽 —— 真机踩出来的。
        //
        //   触摸拦截区（aurora_overlay.cpp 的 kUiBoxW/H）是**一个矩形**，只能在
        //   attachInputWindow 时设一次，之后无法替换（Region 替换符号已按
        //   [未核实-2] 禁用）。所以它只能是两个窗口的**包络框**。
        //
        //   原先主题窗口宽 860、二级窗口宽 760，包络框要 900 宽才框得住 ⇒
        //   窗口右侧那 100+ 像素的**空白区也被拦截**，用户在空白处滑动没反应
        //   （他实测报告过：「空白的没窗口遮挡的地方我滑动不了」）。
        //
        //   把两个窗口宽度对齐到 760 后，并集就是一个规整矩形 (16,16)-(776,2130)，
        //   拦截区收到 800 宽即可 —— 右侧留出 280 像素的穿透带。
        ImGui::SetNextWindowSize(ImVec2(760.0f, 1400.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(16.0f, 730.0f), ImGuiCond_FirstUseEver);
        // ★ NoResize 是**真机踩出来的**：触摸没有 hover，ImGui 的窗口缩放判定
        //   依赖指针位置落在边框附近，而在密集面板里滑动滚动时很容易被误判成
        //   缩放 —— 实测把 1400 高的窗口拖成了约 370 高，而且每次滑动都缩一点。
        //   这个面板的尺寸是我们自己按屏幕调的，用户不需要缩放它，禁掉更稳。
        //
        //   注：二级窗口（secondary_imgui.cpp 的 Draw()）**没有**加这个标志 ——
        //   那个窗口是「照搬用户开源项目」的部分，不擅自改它的行为。
        //   它同样有误缩放问题，已如实记录在报告里。
        // ★ 用户要求：主题窗口要能自己关掉。原来第二参数传的是 nullptr
        //   ⇒ ImGui 根本不画关闭按钮。现在把 keepOpen 交给 ImGui 承接。
        //
        // ★★ 本仓库这份 ImGui 的契约（读源码确认，别想当然）：
        //   `ImGui::Begin()` **完全不看 `*p_open`** —— 全文件只有 BeginPopupModal
        //   读它。也就是说，关闭按钮只会把 `*p_open` 置 false，**窗口照样画**；
        //   "关掉"必须由**调用方自己停止调用 Begin** 来实现。
        //   第一版就是把 keepOpen 初值设成 true 然后照常 Begin ——
        //   真机上点 × 只闪一下高亮、窗口纹丝不动，正是这个原因。
        //   ★ 注意这里**没有** NoCollapse，是刻意的：标题栏左边那个 ▼/▶ 三角是
        //   "展开/收起窗口"，和右边那个 ×（"关掉整个程序"）是**两个不同的按钮**。
        //   我曾经为了防误触加了 NoCollapse，用户立刻发现「展开收起按钮怎么被删了」——
        //   收起窗口是他要的功能，别删。
        bool keepOpen = true;
        const bool beganCall = g_themeDrawn;   // Begin 是否被调用过（决定要不要配对 End）
        // ★★ 长按缩放算出的新尺寸必须在 Begin() **之前**应用（左上角固定、当帧生效）。
        aurora::CornerResizeApplyPending(g_themeResize);
        // ★★ NoResize 继续保留 —— 它关掉的是 **ImGui 自带的 resize grip 与边框拖拽判定**
        //    （那个才是"在密集面板里滑动滚动被误判成缩放、把 1400 高的窗口拖成 370"的
        //    元凶，见上面的注释）。用户现在要的"主窗口也能调长宽"由**右下角长按**
        //    提供：手指按住右下角 120ms 不动 ⇒ 白色同心圆弧展开 + 边框反馈 + 轻震，
        //    然后拖多远改多大。两者不是同一件事，不要用"去掉 NoResize"来实现缩放。
        // ★★ 滚动条加宽（用户要求"只靠右侧滚动条翻页"）。
        //   ImGui 的滚动条**命中区就是它的视觉宽度**，默认 14dp×2 倍缩放 = 28px，
        //   手指点上去偏窄、容易点空 ⇒ 这里给主题窗口单独加宽到 20dp（40px）。
        //   ★ 必须在 Begin() **之前** push：滚动条的宽度是在 Begin() 内部算进
        //     `window->ScrollbarSizes` 与 `InnerRect` 的，之后再改这一帧不生效。
        //   ★ 只作用于主题窗口（push/pop 包住它），二级窗口是 NoScrollbar，不受影响。
        // ★★ 输入法避让（adjustPan 语义）：键盘弹出来时把主题窗口整体上移，
        //    别让这层玻璃压住键盘（我们的图层 z 序在 WMS 子树之上）。
        //    位移由纯逻辑函数算（见 ImeAvoid.h，有离线回归）：它先用
        //    "当前实际位置 + 已施加位移"还原出逻辑位置，再决定移多少 ——
        //    否则每帧都会在上一次的结果上再叠一次，窗口会飞出屏幕。
        {
            int imeRect[4] = {0, 0, 0, 0};
            imeRect[0] = static_cast<int>(g_imeRect[0]);
            imeRect[1] = static_cast<int>(g_imeRect[1]);
            imeRect[2] = static_cast<int>(g_imeRect[2]);
            imeRect[3] = static_cast<int>(g_imeRect[3]);
            const float imeTopNow = imeRect[3] > imeRect[1] ? static_cast<float>(imeRect[1]) : 0.0f;
            const float want = aurora::ComputeImeAvoidShift(
                g_themeWindowPos[1], g_themeWindowSize[1], g_themeImeShift, imeTopNow, 0.0f);
            if (std::fabs(want - g_themeImeShift) > 0.01f) {
                const float y = aurora::ImeAvoidWindowY(g_themeWindowPos[1], g_themeImeShift, want);
                ImGui::SetNextWindowPos(ImVec2(g_themeWindowPos[0], y), ImGuiCond_Always);
                g_themeImeShift = want;
                __android_log_print(ANDROID_LOG_INFO, kLogTag,
                                    "输入法避让：主题窗口上移 %.0fpx（键盘顶边 %.0f）", want,
                                    imeTopNow);
            }
        }
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, lgx::px(20.0f));
        const bool began = beganCall &&
            ImGui::Begin("主题", &keepOpen, ImGuiWindowFlags_NoResize);
        ImGuiWindow* const themeWindow = began ? ImGui::GetCurrentWindow() : nullptr;
        // 滚动条宽度是 Begin() 内部读的（见上面的说明）⇒ Begin 之后立刻配对 pop。
        ImGui::PopStyleVar();
        // 记下这一帧真正用的位置/尺寸（下一帧避让要用它还原逻辑位置）
        if (themeWindow != nullptr) {
            g_themeWindowPos[0] = themeWindow->Pos.x;
            g_themeWindowPos[1] = themeWindow->Pos.y;
            g_themeWindowSize[0] = themeWindow->Size.x;
            g_themeWindowSize[1] = themeWindow->Size.y;
        }
        // 长按缩放：判定区里压着组件时"长按优先"，接管那一刻让组件让位。
        bool themeResizing = false;
        if (began) {
            aurora::CornerResizeFrameInput resizeInput;
            resizeInput.pointerDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            resizeInput.pointerX = ImGui::GetIO().MousePos.x;
            resizeInput.pointerY = ImGui::GetIO().MousePos.y;
            resizeInput.deltaSeconds = ImGui::GetIO().DeltaTime;
            // "指针压在本窗口的组件上吗" —— 只看**本帧**的 HoveredId（它只由
            // ButtonBehavior 设置，普通文字不算），与二级窗口同一个判据。
            ImGuiContext& g = *ImGui::GetCurrentContext();
            resizeInput.componentHovered =
                (g.HoveredWindow == themeWindow && g.HoveredId != 0) ||
                (g.ActiveId != 0 && g.ActiveIdWindow == themeWindow);
            resizeInput.cornerRadiusPx = ImGui::GetStyle().WindowRounding;
            resizeInput.label = "主题窗口";
            // ★ 与二级窗口同一个来源：两种缩放方式由「窗口缩放」页选择（默认实时缩放）。
            resizeInput.mode = aurora::CurrentResizeMode();
            // ★ 主窗口自己的尺寸参数（最小 340x260 dp = 680x520 px，比默认的
            //   760x1400 小 ⇒ 第一次长按不会跳尺寸）。这两个函数住在 ResizeGesture.h
            //   里是刻意的：那条"最小尺寸必须小于默认窗口尺寸"的约束由离线回归盯着。
            resizeInput.params = aurora::ResizeParamsForPanel();
            themeResizing = aurora::CornerResizeUpdate(g_themeResize, themeWindow, resizeInput);
        }
        (void)themeResizing;   // 缩放期间 ImGui 的控件已被 ClearActiveID 让位，无需再拦
        // ★ 触摸滚动空白区的接线已经**删除**（用户要求：只保留右侧滚动条）。
        //   原来这里有一句 `if (began && !themeResizing) RunThemeWindowScroll(themeWindow);`，
        //   连同 ThemeScrollRuntime / 惯性 / `[滚动]` 日志探针一起删掉了。
        if (began) {
            // ★ 用户要求：主题窗口可以自由决定二级窗口是否存在。
            //   放在标签栏**上方**（任何标签页都看得见）—— 二级窗口被关掉之后，
            //   用户还得能从这里把它找回来；埋在某个标签页里就找不到了。
            ImGui::Checkbox("显示二级液态玻璃窗口", &g_secondaryOpen);
            ImGui::SameLine();
            // ★ 文案必须与实际一致：二级窗口现在是 `NoTitleBar`（整块玻璃，用户要求删掉
            //   标题栏），**根本没有那个 ×**。原来这句"(它标题栏上的 × 也能关)"是失实的
            //   （上游 `main.cpp:185` 的原文是"勾选后打开二级液态玻璃窗口"）。
            ImGui::TextDisabled("(勾选显示 / 取消隐藏)");
            // ---- 录屏开关（内置：起系统自带的 screenrecord）----
            // ★ 为什么内置：手机自带录屏走的是 MediaProjection/镜像那条路，
            //   只镜像"显示根图层"那棵子树，而本窗口是它的**兄弟节点** ⇒ 录不到。
            //   screenrecord 走 SF 的合成路径，拍得到（真机抽帧验证过）。
            {
                aurora::ScreenRecorder& rec = aurora::recorder();
                if (rec.running()) {
                    // ★ 按钮文字只用**字体里确实有的**字符：本仓库的字体没有 U+25A0/U+25CF，
                    //   写成 "■/●" 会渲染成问号（真机实测踩到过）。
                    if (ImGui::Button("停止录屏")) {
                        std::string why;
                        if (!rec.stop(&why)) g_recordNote = why;
                        else g_recordNote = "已停止，文件已收尾";
                    }
                    ImGui::SameLine();
                    const int secs = rec.elapsedSeconds();
                    ImGui::Text("%02d:%02d  %.1f MB", secs / 60, secs % 60,
                                static_cast<double>(rec.bytesWritten()) / (1024.0 * 1024.0));
                } else {
                    if (ImGui::Button("开始录屏")) {
                        aurora::RecordRequest req;
                        req.outputPath = aurora::DefaultRecordPath(static_cast<long long>(::time(nullptr)));
                        std::string why;
                        if (!rec.start(req, &why)) g_recordNote = why;
                        else g_recordNote = req.outputPath;
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(录到 /sdcard/Download/，右上角显示时长)");
                }
                if (!g_recordNote.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", g_recordNote.c_str());
                }
            }
            ImGui::Separator();
            // ★ 这里曾经有一段"把窗口夹进触摸拦截区"的代码，**已撤销** ——
            //   它把拖动废掉了：拦截区 800 宽、窗口 760 宽 ⇒ 横向只剩 40 像素，
            //   一拖就被弹回（用户实测反馈「窗口拖动不了」）。
            //   现在允许自由拖动，改用"每次启动都从设计位置开始"来避免永久漂出框外
            //   （见 Renderer.cpp 里关掉 io.IniFilename 的注释）。
            //   拖到框外的部分点不动，是已知限制。
            if (ImGui::BeginTabBar("##tabs")) {
                if (ImGui::BeginTabItem("液态玻璃")) {
                    // ★★★ Liquid Glass Inspector —— 液态玻璃实时调试工作台。
                    //
                    // 这一页原来是 secondary_imgui::DrawHostControls()：一组
                    // **每个组件共用同一批 static 变量**的滑杆（五枚按钮共用一组光学
                    // 参数、开关/滑块/导航栏的配方根本够不着），既不能按组件隔离，
                    // 也无法保存/恢复。
                    //
                    // 现在这一页完全由**组件注册表**驱动：
                    //   · 分页栏的每一项都是一个真实注册的玻璃对象（整窗口 / 卡片 /
                    //     按钮 1..5 / 开关 1..3 / 进度条 1..2 / 导航栏 / 芯片 1..6 /
                    //     弹出菜单 / 弹窗 / 实验场 …）；
                    //   · 每个参数控件都绑定到渲染器真正读取的那个字段；
                    //   · 有子层的组件（开关的轨道/圆钮、进度条的轨道/滑杆、导航栏的
                    //     背景层/指示器）可以在页面内切换子层。
                    // 新增组件只需要在 GlassTargets.cpp 里多写一段绑定，分页栏会
                    // 自动出现 —— 这就是"动态发现组件"。
                    aurora::glass::DrawGlassWorkbench();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("窗口缩放")) {
                    // ★ 「窗口缩放方式」+ 虚线预览/填充动画参数。
                    //   放在标签页里（而不是埋在「液态玻璃」页几百个参数中间）：
                    //   它是**用户级**设置，不该和渲染参数混在一起。
                    DrawResizeSettingsPage();
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("运行时")) {
                    const secondary_imgui::HostInfo& h = g_lastHostInfo;
                    ImGui::Text("渲染器      %s", h.renderer.c_str());
                    ImGui::Text("宿主        %s", h.host.c_str());
                    ImGui::Text("表面        %s", h.surfaceReady ? "ready" : "lost");
                    ImGui::Text("输入法      %s", h.imeAvailable ? "available" : "unavailable");
                    ImGui::Text("帧数        %llu", static_cast<unsigned long long>(h.frameCount));
                    ImGui::Text("FPS         %.1f", h.fps);
                    ImGui::Text("最差帧      %.2f ms", h.worstFrameMilliseconds);
                    ImGui::Text("活动触点    %zu", h.activePointers);
                    ImGui::Text("Pan %.1f, %.1f  Pinch %.3f  Rotate %.3f rad",
                                h.gesturePanX, h.gesturePanY, h.gestureScale, h.gestureRotation);
                    ImGui::EndTabItem();
                }
                // ★ 第三个标签页：M-09 动效实验场
                //
                // 为什么必须是**独立标签页**（真机踩出来的，不是设计偏好）：
                //   ImGui 的窗口**不支持拖拽滚动** —— 只有滚轮和滚动条能滚。
                //   而「液态玻璃」那一页有上百个控件，几乎每处按下都会命中某个控件
                //   （滑块会被直接拖走），所以触摸既滚不动、又会误改参数。
                //   结果：实验场只要排在那页里，就永远落在折叠线以下，看不到也拖不到。
                //   实测把它放到那页最前面也不行 —— 光是它自己的控件就约 1160px，
                //   加上舞台就超出 1400px 的窗口高度。
                //   独占一页后，控件 + 舞台能完整放进窗口。
                if (ImGui::BeginTabItem("动效实验场")) {
                    aurora::DrawMotionLabTab();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            g_themeRect[0] = ImGui::GetWindowPos().x;
            g_themeRect[1] = ImGui::GetWindowPos().y;
            g_themeRect[2] = ImGui::GetWindowSize().x;
            g_themeRect[3] = ImGui::GetWindowSize().y;
        } else {
            g_themeRect[0] = g_themeRect[1] = g_themeRect[2] = g_themeRect[3] = 0.0f;
        }
        if (beganCall) ImGui::End();
        // 手柄画在**前景层**（窗口级 Overlay）：不被内容裁剪误伤，弧也能贴在圆角外侧。
        if (themeWindow != nullptr) {
            aurora::CornerResizeDrawHandle(g_themeResize, themeWindow,
                                           ImGui::GetStyle().WindowRounding);
            // 模式 B 的虚线预览框（与二级窗口同一套绘制：同一份虚线路径生成器）
            aurora::CornerResizeDrawPreview(g_themeResize, themeWindow,
                                            ImGui::GetStyle().WindowRounding);
        }
        // ★ × 被点了 ⇒ **退出整个程序**（用户明确要的就是这个，不是"收起窗口"）。
        //   这里只置请求位 —— 真正退出由主循环走正常收尾路径（退出码 0）。
        //   同时把 g_themeDrawn 置 false，让窗口在退出的那几帧里立刻停画，
        //   不出现"点了 × 但窗口还闪一下"的观感。
        if (beganCall && !keepOpen) {
            g_quitRequested.store(true, std::memory_order_release);
            g_themeDrawn = false;
        }

        // ---- 二级窗口 ----
        // 主题窗口已经画完 ⇒ 它里面的开关这一帧就能生效。
        secondary_imgui::SetVisible(g_secondaryOpen);
        secondary_imgui::Draw();
        // ★ 二级窗口画完再描高亮框：必须画在玻璃面板与内容之上，
        //   而且此时所有组件的矩形都已经在 Draw() 里登记完毕。
        aurora::glass::debughl::DrawDebugHighlight();
        // ★ 把二级窗口标题栏上那个 × 的点击结果读回来。
        //   不读回来，下一帧的 SetVisible(true) 又会把它打开 —— 这就是原来那个
        //   「点了叉没有任何效果」的 bug。
        if (g_secondaryOpen && !secondary_imgui::IsVisible()) {
            g_secondaryOpen = false;
        }

        // ★ 用户明确要求：**不要**那个"正在录屏"的常驻浮标，
        //   录制状态只在主题窗口里显示（按钮 + 时长 + 文件大小 + 路径）。
        //   （先前试过右上角/底部左侧的 REC 浮标，用户看过后要求去掉。）

        // ---- 触摸拦截区 = 当前**可见**的那些窗口的并集 ----
        // ★ 必须并上主题窗口，否则调试面板上的滑块会被判为"窗口外"，触摸直接
        //   穿透到后面的 App —— 用户根本拖不动。
        float rect[4] = {};
        secondary_imgui::GetTouchRect(rect);
        const float* const kParts[] = {rect, g_themeRect};
        bool any = false;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        for (const float* const part : kParts) {
            if (part[2] <= 0.0f || part[3] <= 0.0f) continue;
            if (!any) {
                any = true;
                left = part[0];
                top = part[1];
                right = part[0] + part[2];
                bottom = part[1] + part[3];
            } else {
                left = std::min(left, part[0]);
                top = std::min(top, part[1]);
                right = std::max(right, part[0] + part[2]);
                bottom = std::max(bottom, part[1] + part[3]);
            }
        }
        std::lock_guard lock(g_regionMutex);
        g_region = any ? TouchRegion{left, top, right, bottom} : TouchRegion{};
    } else {
        secondary_imgui::SetVisible(false);
        std::lock_guard lock(g_regionMutex);
        g_region = {};
    }
}

bool LiquidGlassWantsTextInput() {
    return g_initialized && g_visible.load(std::memory_order_acquire)
        && secondary_imgui::WantsTextInput();
}

bool LiquidGlassQuitRequested() {
    return g_quitRequested.load(std::memory_order_acquire);
}

void SetLiquidGlassVisible(bool visible) {
    g_visible.store(visible, std::memory_order_release);
    secondary_imgui::SetVisible(visible);
}

bool LiquidGlassVisible() {
    return g_visible.load(std::memory_order_acquire);
}

TouchRegion LiquidGlassRegion() {
    std::lock_guard lock(g_regionMutex);
    return g_region;
}

}  // namespace aurora
