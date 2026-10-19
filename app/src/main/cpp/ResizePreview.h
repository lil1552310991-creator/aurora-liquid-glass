#pragma once

// ===========================================================================
//  「虚线预览缩放」（模式 B）+「松手后的液态玻璃填充动画」（纯逻辑内核）
// ===========================================================================
//
//  用户要求（照抄要点，逐条对应到下面的实现）：
//   · 长按触发与模式 A **完全相同**（同一个 LONG_PRESS_TRIGGER_MS、同一套状态机）；
//   · 拖动期间**真实窗口一个像素都不变**，只更新圆角虚线预览框；
//   · 左上角固定；右/下变大、左/上变小、斜向同时改；实时跟手；支持最小/最大约束；
//   · 松手之后窗口才真正改变尺寸，并且是**液态玻璃填充动画**：
//       几何从旧边界连续插值到目标边界（真窗口、真 shader，不是截图拉伸），
//       新扩展区域带模糊/折射/透明度的渐变，边缘有轻微的流动柔化；
//   · 放大与缩小走**同一套**机制；
//   · 动画用**实际帧时间**推进（不是固定帧数），弹簧可中途打断/反向；
//   · 动画参数（弹簧刚度、阻尼、填充模糊、边缘流动…）必须可配置。
//
//  分层（谁也不越界）：
//     ResizeGesture.h    长按状态机 / 判定区 / 尺寸夹紧 / 阻尼弹簧（已有）
//     本文件             模式、参数、虚线路径、提交动画、填充包络、会话状态分类
//                        —— **零 ImGui / 零 Android 依赖**，所以能进宿主离线回归
//     CornerResizeOverlay.h/.cpp   ImGui 接线（两个窗口共用）
//
//  ★ 为什么虚线要自己按**弧长**生成，而不是"每 N 像素画一小段线"：
//    后者在圆角处会把虚线段切得忽长忽短（曲率越大越明显），看起来毛糙；
//    用户明确要求"虚线边框应细腻、清晰，不能粗糙"。所以这里把圆角矩形拆成
//    8 个图元（4 直线 + 4 圆弧），按**真实弧长**排布虚线段，圆弧内部再按
//    弦高误差细分 —— 虚线段长度处处相等，圆角处平滑。
//
//  ★ 为什么填充动画用弹簧而不是"固定时长的 lerp"：
//    · 速度连续 ⇒ 中途被打断（用户在动画期间又长按）时能接续，不会跳；
//    · 时间步长固定小步积分（ResizeSpring 内部就是）⇒ 掉帧不炸、不同帧率同一结果；
//    · 阻尼比可调 ⇒ 需要"自然"就给它接近临界阻尼，需要"活一点"就降阻尼比。
//      默认值刻意取 ζ≈0.94（几乎看不到过冲）——用户明确不要夸张弹跳。

#include "ResizeGesture.h"

#include <cmath>
#include <cstdio>

namespace aurora {

// ---------------------------------------------------------------------------
//  两种缩放方式
// ---------------------------------------------------------------------------
enum class ResizeMode : int {
    Live = 0,            // 模式 A：拖动时真实窗口尺寸实时改变（原有实现）
    DashedPreview = 1,   // 模式 B：拖动时只更新虚线预览框，松手后做填充动画
};

inline const char* ResizeModeName(ResizeMode mode) {
    return mode == ResizeMode::DashedPreview ? "虚线预览缩放" : "实时缩放";
}

// ---------------------------------------------------------------------------
//  参数（全部可在主题调试器里实时调；默认值就是"出厂手感"）
// ---------------------------------------------------------------------------
struct ResizePreviewParams {
    // ---- 虚线预览框外观 ----
    float lineWidthDp = 1.4f;        // 线宽（dp）：细腻但不虚
    float dashLengthDp = 8.0f;       // 虚线段长度
    float dashGapDp = 5.0f;          // 间隔
    float lineAlpha = 0.94f;         // 主线不透明度（白色，前景层）
    float underlayAlpha = 0.42f;     // 下衬暗线：保证**亮背景下也看得见**
    float underlayExtraDp = 0.6f;    // 下衬比主线粗多少（"细微阴影提高辨识度"）
    float cornerRadiusAddDp = 0.0f;  // 预览框圆角相对窗口圆角的增量（0 = 完全一致）
    // ---- 拖动时的尺寸数值 ----
    bool showSizeLabel = true;
    float labelOffsetDp = 10.0f;     // 相对预览框右下角的外移量（画在框外，不压内容）
    float labelScale = 0.72f;        // 相对当前字体大小的缩放（小一号，不抢视觉）
    // ---- 松手后的液态玻璃填充动画 ----
    // 弹簧：ω=√k，ζ=c/(2√k)。默认 k=420 / c=38.5 ⇒ ζ≈0.94、ω≈20.5。
    // ★ 这组数是**量出来的**（120Hz、位移 800px 的实跑采样）：
    //     到 99% 用 300ms、99.5% 用 333ms、"只剩 1px"用 400ms，
    //     正好落在用户给的初始调试目标 300–450ms 里，且几乎看不到过冲。
    //   （上一版 190/26 的解析估计虽然也是 ~300ms，但弹簧真正收敛到
    //     |Δ|<0.1% 要 617ms —— 尾巴太长，虚线框会在窗口已经停下之后还挂着。）
    float springStiffness = 420.0f;
    float springDamping = 38.5f;
    float maxDurationMs = 1200.0f;   // 兜底：任何参数下动画都必须结束（不许状态锁死）
    // "可以认为已经到位"的剩余距离（px）。到这一步就吸附到目标值：
    //   · 最后那不到 1px 的位移肉眼不可辨；
    //   · 但必须**精确**落到预览尺寸（"动画结束时新尺寸与预览尺寸一致"）。
    float settleEpsilonPx = 1.0f;
    float settleSpeedPxPerSec = 30.0f;   // 同时要求剩余速度足够小（避免"滑着停下"）
    // "正在填充的那块玻璃" = 贴着**移动前沿**的一条带（不是整块新区域）：
    //   · 前沿才是玻璃正在流动的地方，它后面的区域早就定型了；
    //   · 只在前沿做效果 ⇒ 不会在窗口中间留下一条矩形接缝/残影。
    bool fillOn = true;              // 关掉只剩几何动画（诊断用，不是假控件）
    float fillRimWidthDp = 22.0f;    // 前沿玻璃带宽度
    float fillAlpha = 0.42f;         // 前沿玻璃的额外不透明度（1 → 0 收敛到普通玻璃）
    float fillRefractGain = 1.25f;   // 前沿折射增益（lensHeight / lensAmount）
    float fillSpectralGain = 1.35f;  // 前沿色散增益（ytbl 上限 SPECTRAL_MAX = 2.0）
    // 前沿与"已定型玻璃"之间的模糊差：实现方式是把**窗口整体**的模糊在过渡期间
    // 临时降低这么多，前沿那条带保持配置值 ⇒ 前沿真的更糊，且收尾时一起复位。
    //   ★ 为什么用"降窗口"而不是"加前沿"：窗口模糊默认 10dp 已经是 ytbl 的上限
    //     （BLUR_RADIUS_MAX_DP = 10，GlassConfig.h:69），加上去会被内部夹掉 ——
    //     那样就成了一个"转了没反应"的假控件。
    float fillBlurDiffDp = 2.5f;
    float fillGlowAlpha = 0.30f;     // 前沿内侧的柔光渐变（ImGui 画的，零 GPU 面板成本）
    float fillGlowWidthDp = 26.0f;
    // 边缘流动：动画期间窗口边缘的柔化 + 折射/高光增强（0 → 峰值 → 0）
    float edgeFlowGain = 0.85f;
    float edgeFlowSoftnessDp = 0.45f;
    // 虚线框在"窗口已经长到目标范围"之前的解体时机：
    //   0.55 ⇒ 几何走到 55% 才开始变淡，100% 时完全消失（不许"松手就消失"）
    float previewDissolveFrom = 0.55f;
};

// 全局设置：模式 + 上表参数。
// ★ 只在这里存一份 —— 主题窗口的「窗口缩放」页与 Liquid Glass Inspector 的
//   注册表都绑定到**同一块内存**，所以不存在"两个界面显示不一致"。
struct ResizeSettings {
    int mode = static_cast<int>(ResizeMode::Live);   // 默认保留实时缩放（用户要求）
    ResizePreviewParams preview{};
};

inline ResizeSettings& ResizeSettingsRef() {
    static ResizeSettings settings;
    return settings;
}

inline ResizeMode CurrentResizeMode() {
    return ResizeSettingsRef().mode == static_cast<int>(ResizeMode::DashedPreview)
        ? ResizeMode::DashedPreview : ResizeMode::Live;
}

// ---------------------------------------------------------------------------
//  会话状态（用户给的状态机命名，作为日志与测试的**唯一**真值）
//
//    IDLE           默认状态
//    PRESS_PENDING  等待长按时间达到阈值
//    RESIZE_ACTIVE  长按成功，进入缩放状态
//    PREVIEW_DRAG   模式 B 正在拖动虚线预览框
//    COMMIT_ANIMATING 模式 B 松手后执行尺寸及玻璃填充动画
//    RECOVERING     缩放结束，手柄与边框恢复普通状态
// ---------------------------------------------------------------------------
enum class ResizeSessionState : int {
    Idle = 0,
    PressPending,
    ResizeActive,
    PreviewDrag,
    CommitAnimating,
    Recovering,
};

inline const char* ResizeSessionStateName(ResizeSessionState state) {
    switch (state) {
        case ResizeSessionState::PressPending: return "PRESS_PENDING";
        case ResizeSessionState::ResizeActive: return "RESIZE_ACTIVE";
        case ResizeSessionState::PreviewDrag: return "PREVIEW_DRAG";
        case ResizeSessionState::CommitAnimating: return "COMMIT_ANIMATING";
        case ResizeSessionState::Recovering: return "RECOVERING";
        default: return "IDLE";
    }
}

// 单一分类函数：状态只由"事实"推出，不由各处各自维护的标志位拼出来。
//   holding / active / dragging  来自 ResizeHold（长按内核）
//   commitActive                 提交动画是否在跑
//   recoveryProgress             手柄/边框恢复弹簧的当前进度（>0 ⇒ 还在恢复）
inline ResizeSessionState ClassifyResizeSession(bool holding, bool active, bool dragging,
                                                bool commitActive, float recoveryProgress,
                                                ResizeMode mode) {
    if (commitActive) return ResizeSessionState::CommitAnimating;
    if (holding) {
        if (!active) return ResizeSessionState::PressPending;
        if (dragging && mode == ResizeMode::DashedPreview) return ResizeSessionState::PreviewDrag;
        return ResizeSessionState::ResizeActive;
    }
    if (recoveryProgress > 0.002f) return ResizeSessionState::Recovering;
    return ResizeSessionState::Idle;
}

// ===========================================================================
//  虚线圆角矩形
// ===========================================================================
struct DashSegment {
    float x0 = 0.0f, y0 = 0.0f;
    float x1 = 0.0f, y1 = 0.0f;
};

// 一条虚线最多需要多少段（宿主按它准备缓冲）。
//   8 个图元；即使 1px 的虚线间隔，圆角处每段也会被细分成若干小段。
//   按经验 + 容量上限算：圆弧每 3° 一段 ⇒ 整圈 120 段，四角 120 段；
//   直线段每段虚线一段；取 512 足够，超出的部分会被安全丢弃。
inline constexpr int kMaxDashSegments = 512;

namespace resize_detail {

// 圆角矩形的 8 个图元：4 条直线 + 4 段 90° 圆弧（屏幕坐标，y 向下，顺时针）
struct DashPrimitive {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;   // 直线
    float cx = 0.0f, cy = 0.0f, radius = 0.0f;          // 圆弧
    float a0 = 0.0f, a1 = 0.0f;                         // 圆弧起止角（弧度）
    float start = 0.0f, length = 0.0f;                  // 在整条路径上的起点与长度
    bool arc = false;
    bool empty() const { return length <= 1.0e-4f; }
};

inline constexpr float kResizePi = 3.14159265358979323846f;

// 把圆角矩形拆成图元并累计弧长。返回图元个数。
inline int BuildRoundedRectPrimitives(float left, float top, float right, float bottom,
                                      float radius, DashPrimitive* out, int capacity) {
    if (out == nullptr || capacity < 8) return 0;
    const float w = right - left;
    const float h = bottom - top;
    if (!(w > 0.0f) || !(h > 0.0f)) return 0;
    float r = radius;
    const float halfMin = 0.5f * (w < h ? w : h);
    if (r > halfMin) r = halfMin;
    if (r < 0.0f) r = 0.0f;

    const float halfPi = 0.5f * kResizePi;
    int n = 0;
    float cursor = 0.0f;
    auto pushLine = [&](float x0, float y0, float x1, float y1) {
        DashPrimitive p;
        p.arc = false;
        p.x0 = x0; p.y0 = y0; p.x1 = x1; p.y1 = y1;
        const float dx = x1 - x0, dy = y1 - y0;
        p.length = std::sqrt(dx * dx + dy * dy);
        if (p.length <= 1.0e-4f) return;
        p.start = cursor;
        cursor += p.length;
        out[n++] = p;
    };
    auto pushArc = [&](float cx, float cy, float rr, float a0, float a1) {
        DashPrimitive p;
        p.arc = true;
        p.cx = cx; p.cy = cy; p.radius = rr; p.a0 = a0; p.a1 = a1;
        p.x0 = cx + std::cos(a0) * rr; p.y0 = cy + std::sin(a0) * rr;
        p.x1 = cx + std::cos(a1) * rr; p.y1 = cy + std::sin(a1) * rr;
        p.length = rr * std::fabs(a1 - a0);
        if (p.length <= 1.0e-4f || rr <= 1.0e-4f) return;
        p.start = cursor;
        cursor += p.length;
        out[n++] = p;
    };

    // 从"上边左端"开始，顺时针一圈（与屏幕坐标一致：+y 向下）
    pushLine(left + r, top, right - r, top);
    pushArc(right - r, top + r, r, -halfPi, 0.0f);
    pushLine(right, top + r, right, bottom - r);
    pushArc(right - r, bottom - r, r, 0.0f, halfPi);
    pushLine(right - r, bottom, left + r, bottom);
    pushArc(left + r, bottom - r, r, halfPi, kResizePi);
    pushLine(left, bottom - r, left, top + r);
    pushArc(left + r, top + r, r, kResizePi, kResizePi + halfPi);
    return n;
}

inline float PrimitivePointAt(const DashPrimitive& p, float s, float* outX, float* outY) {
    if (p.length <= 1.0e-6f) {
        *outX = p.x0;
        *outY = p.y0;
        return 0.0f;
    }
    float t = (s - p.start) / p.length;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (p.arc) {
        const float a = p.a0 + (p.a1 - p.a0) * t;
        *outX = p.cx + std::cos(a) * p.radius;
        *outY = p.cy + std::sin(a) * p.radius;
    } else {
        *outX = p.x0 + (p.x1 - p.x0) * t;
        *outY = p.y0 + (p.y1 - p.y0) * t;
    }
    return t;
}

}  // namespace resize_detail

// 生成圆角矩形的虚线轮廓。
//   phase：整条路径上的相位偏移（0 = 第一段虚线正好从"上边左端"开始）。
//          起点固定在左上角 ⇒ 窗口变尺寸时虚线不会整体游走（看起来稳定）。
// 返回写入 out 的段数（容量不足时安全截断，不越界）。
inline int BuildDashedRoundedRect(float left, float top, float right, float bottom,
                                  float radius, float dashLength, float gapLength, float phase,
                                  DashSegment* out, int capacity) {
    using namespace resize_detail;
    if (out == nullptr || capacity <= 0) return 0;
    if (!(right > left) || !(bottom > top)) return 0;
    if (!(dashLength > 0.5f)) return 0;
    if (gapLength < 0.0f) gapLength = 0.0f;

    DashPrimitive prims[8];
    const int primCount = BuildRoundedRectPrimitives(left, top, right, bottom, radius, prims, 8);
    if (primCount <= 0) return 0;

    float total = 0.0f;
    for (int i = 0; i < primCount; ++i) {
        const float end = prims[i].start + prims[i].length;
        if (end > total) total = end;
    }
    if (!(total > 0.0f)) return 0;

    // 圆弧内部的细分步长：弦高误差 ≤ 0.12px（肉眼不可辨），下限 2°，上限 6°。
    auto arcStepRadians = [](float rr) {
        if (!(rr > 0.5f)) return 0.35f;
        const float step = 2.0f * std::acos(
            std::fmax(-1.0f, std::fmin(1.0f, 1.0f - 0.12f / rr)));
        return step < 0.0349f ? 0.0349f : (step > 0.1047f ? 0.1047f : step);
    };

    const float cycle = dashLength + gapLength;
    // 相位先夹到一个周期内（否则 k 的起点会算成负数以外的大值）
    float phaseMod = std::fmod(phase, cycle);
    if (phaseMod < 0.0f) phaseMod += cycle;

    int written = 0;
    auto emitDash = [&](float a, float b) {
        if (b - a <= 0.05f) return;
        for (int i = 0; i < primCount && written < capacity; ++i) {
            const DashPrimitive& p = prims[i];
            const float pStart = p.start;
            const float pEnd = p.start + p.length;
            const float segA = a > pStart ? a : pStart;
            const float segB = b < pEnd ? b : pEnd;
            if (segB - segA <= 0.05f) continue;
            if (!p.arc) {
                float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
                PrimitivePointAt(p, segA, &x0, &y0);
                PrimitivePointAt(p, segB, &x1, &y1);
                out[written++] = DashSegment{x0, y0, x1, y1};
                continue;
            }
            // 圆弧：按角步长细分，保证虚线是"沿着圆角"的，而不是切角弦
            const float step = arcStepRadians(p.radius);
            const float t0 = (segA - pStart) / p.length;
            const float t1 = (segB - pStart) / p.length;
            const float a0 = p.a0 + (p.a1 - p.a0) * t0;
            const float a1 = p.a0 + (p.a1 - p.a0) * t1;
            const float sweep = a1 - a0;
            int steps = static_cast<int>(std::ceil(std::fabs(sweep) / step));
            if (steps < 1) steps = 1;
            if (steps > 64) steps = 64;
            for (int k = 0; k < steps && written < capacity; ++k) {
                const float u0 = static_cast<float>(k) / static_cast<float>(steps);
                const float u1 = static_cast<float>(k + 1) / static_cast<float>(steps);
                const float aa = a0 + sweep * u0;
                const float ab = a0 + sweep * u1;
                out[written++] = DashSegment{
                    p.cx + std::cos(aa) * p.radius, p.cy + std::sin(aa) * p.radius,
                    p.cx + std::cos(ab) * p.radius, p.cy + std::sin(ab) * p.radius};
            }
        }
    };

    // 一段段地排虚线：第 k 段从 (k*cycle - phaseMod) 开始
    const float first = -phaseMod;
    const int maxDashes = static_cast<int>(total / (dashLength + 0.5f * (gapLength + 1.0f))) + 4;
    for (int k = 0; k < maxDashes && written < capacity; ++k) {
        const float dashStart = first + cycle * static_cast<float>(k);
        if (dashStart >= total) break;
        float a = dashStart < 0.0f ? 0.0f : dashStart;
        float b = dashStart + dashLength;
        if (b > total) b = total;
        emitDash(a, b);
    }
    return written;
}

// 圆角半径不能超过短边一半（窗口被缩到极小时仍要画得出来，与外壳同一规则）。
inline float PreviewCornerRadius(float width, float height, float windowCornerRadius,
                                 const ResizePreviewParams& params, float pxPerDp) {
    float r = windowCornerRadius + params.cornerRadiusAddDp * pxPerDp;
    if (r < 0.0f) r = 0.0f;
    const float halfMin = 0.5f * (width < height ? width : height);
    if (r > halfMin) r = halfMin;
    return r;
}

// ===========================================================================
//  松手后的提交动画（几何 + 玻璃填充 + 边缘流动）
// ===========================================================================

// 三种同步的变化都由"一个进度"推出来 —— 这是用户要求的动画模型：
//   A. Geometry：窗口实际可视尺寸从旧宽高连续插值到目标宽高；
//   B. Glass Fill：新扩展区域的模糊/透明度/折射按 fill 渐变（1 → 0）；
//   C. Edge：边缘折射/高光/柔化按 edgeFlow 变化（起手扬、随后落）。
// 三者共用同一个弹簧进度 ⇒ 结构上不可能不同步。
struct ResizeFillEnvelope {
    float geom = 0.0f;       // 几何进度（含轻微过冲，已夹到 [0, 1.05]）
    float fill = 0.0f;       // 新扩展区域的"未定型"强度（1 = 刚出现，0 = 已定型）
    float edgeFlow = 0.0f;   // 边缘流动强度（0 → 峰值 → 0）
};

// 局部一份平滑阶跃（★ 不叫 SmoothStep01：CornerResizeHandle.h 里已经有一个
// 同名 inline 函数，两个头文件同时被包含时会重定义）。
inline float ResizeSmoothStep01(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

inline float ClampResizeProgress(float value) {
    if (!(value == value)) return 1.0f;          // NaN ⇒ 当作已完成（不许卡住）
    if (value < 0.0f) return 0.0f;
    if (value > 1.05f) return 1.05f;
    return value;
}

inline ResizeFillEnvelope ComputeResizeFill(float progress) {
    ResizeFillEnvelope out;
    const float g = ClampResizeProgress(progress);
    out.geom = g;
    const float remaining = 1.0f - (g > 1.0f ? 1.0f : g);
    // 衰减曲线：幂次 < 1 ⇒ 前半段衰减慢一点，新区域的"玻璃还在流动"更耐看
    out.fill = std::pow(remaining, 0.75f);
    // 边缘流动：松手瞬间不要硬跳（前 12% 快速上扬），之后回落
    const float attack = g < 0.12f ? (g / 0.12f) : 1.0f;
    out.edgeFlow = attack * std::pow(remaining, 0.70f);
    return out;
}

// 提交动画：一个弹簧跑完 from → to 的尺寸过渡。
struct ResizeCommitAnim {
    bool active = false;
    float fromW = 0.0f, fromH = 0.0f;
    float toW = 0.0f, toH = 0.0f;
    float stiffness = 420.0f;
    float damping = 38.5f;
    float maxDurationMs = 1200.0f;
    float settleEpsilonPx = 1.0f;
    float settleSpeedPxPerSec = 30.0f;
    float elapsedMs = 0.0f;
    float settleMs = 0.0f;          // 上一次动画的真实到位耗时（诊断/日志）
    bool forced = false;            // 上一次是否被兜底时限强制收尾
    ResizeSpring progress;

    void Reset() {
        active = false;
        elapsedMs = 0.0f;
        forced = false;
        progress.Snap(0.0f);
    }

    // 开始一次过渡。尺寸没变（或非法）时返回 false 并且不进入动画。
    bool Start(float fromWidth, float fromHeight, float toWidth, float toHeight,
               const ResizePreviewParams& params) {
        Reset();
        if (!(fromWidth > 0.0f) || !(fromHeight > 0.0f)) return false;
        if (!(toWidth > 0.0f) || !(toHeight > 0.0f)) return false;
        fromW = fromWidth;
        fromH = fromHeight;
        toW = toWidth;
        toH = toHeight;
        if (std::fabs(toW - fromW) < 0.5f && std::fabs(toH - fromH) < 0.5f) return false;
        stiffness = params.springStiffness > 1.0f ? params.springStiffness : 420.0f;
        damping = params.springDamping > 0.0f ? params.springDamping : 38.5f;
        maxDurationMs = params.maxDurationMs > 30.0f ? params.maxDurationMs : 1200.0f;
        settleEpsilonPx = params.settleEpsilonPx > 0.0f ? params.settleEpsilonPx : 1.0f;
        settleSpeedPxPerSec =
            params.settleSpeedPxPerSec > 0.0f ? params.settleSpeedPxPerSec : 30.0f;
        progress.Snap(0.0f);
        progress.SetTarget(1.0f);
        active = true;
        return true;
    }

    // 用**实际帧时间**推进（固定小步长积分在 ResizeSpring 内部）。
    // 返回 false 表示本帧结束后动画已经结束（宿主该做收尾）。
    bool Step(float dtSeconds) {
        if (!active) return false;
        if (!(dtSeconds > 0.0f)) return true;
        if (dtSeconds > 0.25f) dtSeconds = 0.25f;
        elapsedMs += dtSeconds * 1000.0f;
        progress.Step(dtSeconds, stiffness, damping);
        // ★ 到位判据用**像素**表达（剩余多远、还剩多快），不是"进度到 1.000"：
        //   弹簧收敛到 0.1% 要很久（实测 617ms），那段时间窗口其实早就看不出在动，
        //   却会把"虚线框解体 / 状态转 RECOVERING"一起拖到后面。剩 1px 就吸附。
        const float scale =
            std::fmax(std::fabs(toW - fromW), std::fabs(toH - fromH));
        const float remainingPx = (1.0f - progress.value) * scale;
        const float sizeSpeed = std::fabs(progress.velocity) * scale;
        const bool settled = remainingPx <= settleEpsilonPx && sizeSpeed <= settleSpeedPxPerSec;
        const bool timeout = elapsedMs >= maxDurationMs;
        if (settled || timeout) {
            progress.Snap(1.0f);
            settleMs = elapsedMs;
            forced = timeout && !settled;
            active = false;
            return false;
        }
        return true;
    }

    float Progress() const { return ClampResizeProgress(progress.value); }
    // 当前窗口宽高（几何动画的**真实**尺寸，直接喂给 SetNextWindowSize）。
    // 到位时精确返回目标值 —— 保证"动画结束时新尺寸与预览尺寸一致"（不是差一点点）。
    float CurrentWidth() const {
        if (!active) return toW;
        return fromW + (toW - fromW) * Progress();
    }
    float CurrentHeight() const {
        if (!active) return toH;
        return fromH + (toH - fromH) * Progress();
    }
    float CurrentProgress() const { return active ? Progress() : 1.0f; }
    bool Changing() const { return active; }
};

// 虚线预览框的解体透明度：几何走到 previewDissolveFrom 之前**一直实心**，
// 之后平滑淡出，到位时正好为 0（"虚线框平滑消失"）。
//   ★ 反面做法（用户明确禁止）：松手瞬间把虚线框直接抹掉 ——
//     那样用户看不到"窗口正在长向刚才预示的范围"。
inline float PreviewDissolveAlpha(float progress, const ResizePreviewParams& params) {
    float from = params.previewDissolveFrom;
    if (!(from >= 0.0f) || from > 0.98f) from = 0.55f;
    const float g = ClampResizeProgress(progress);
    if (g <= from) return 1.0f;
    const float t = (g - from) / (1.0f - from);
    return 1.0f - ResizeSmoothStep01(t);
}

// ===========================================================================
//  "正在填充的那块玻璃"的几何：贴着移动前沿的一条带
//
//  松手时的窗口矩形 = 老边界（固定不动，左上角是锚点）；当前动画尺寸 = 前沿。
//  向右/向下长出来时，只有在**前沿附近 rimWidth 以内**的那一条带才需要"未定型"
//  处理（更糊、折射更强、更透），它后面的区域已经是定型玻璃了。
//
//  ★ 为什么不是"整块新区域"：整块区域会在窗口中间留下一条**矩形接缝**
//    （带内带外的材质不同、边界是直的）—— 用户明确禁止"矩形残影"。
//  ★ 两条带**互不重叠**：竖向带取到"横向带的内边缘"为止，
//    右下角那块由横向带负责（否则叠两层会双倍着色）。
//  ★ 前沿刚越过老边界时（带宽还没长满 rimWidth），带的内边缘**夹在老边界上**,
//    绝不允许伸进老窗口内部 —— 那里是已经定型的玻璃。
// ===========================================================================
struct ResizeRim {
    bool valid = false;
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    // 只有**贴着窗口真实轮廓**的那几个角才带圆角，其余一律直角（那是窗口内部）
    float cornerTopLeft = 0.0f;
    float cornerTopRight = 0.0f;
    float cornerBottomRight = 0.0f;
    float cornerBottomLeft = 0.0f;
};

struct ResizeRims {
    ResizeRim right;      // x 方向前沿（右边缘在往外走）
    ResizeRim bottom;     // y 方向前沿（下边缘在往下走）
    bool growingX = false;
    bool growingY = false;
    bool shrinkingX = false;
    bool shrinkingY = false;
};

inline ResizeRims ComputeResizeRims(float left, float top, float currentW, float currentH,
                                    float baseW, float baseH, float cornerRadius,
                                    float rimWidth) {
    ResizeRims out;
    const float rightX = left + currentW;
    const float bottomY = top + currentH;
    const float baseRightX = left + baseW;
    const float baseBottomY = top + baseH;
    float r = cornerRadius;
    if (r < 0.0f) r = 0.0f;
    float rim = rimWidth;
    if (!(rim > 1.0f)) rim = 1.0f;

    const float growX = currentW - baseW;
    const float growY = currentH - baseH;
    out.growingX = growX > 0.5f;
    out.growingY = growY > 0.5f;
    out.shrinkingX = growX < -0.5f;
    out.shrinkingY = growY < -0.5f;

    if (out.growingX) {
        float innerLeft = rightX - rim;
        if (innerLeft < baseRightX) innerLeft = baseRightX;   // 不许伸进老窗口
        out.right.valid = innerLeft < rightX - 0.5f;
        out.right.left = innerLeft;
        out.right.top = top;
        out.right.right = rightX;
        out.right.bottom = bottomY;
        out.right.cornerTopRight = r;
        out.right.cornerBottomRight = r;
    }
    if (out.growingY) {
        float innerTop = bottomY - rim;
        if (innerTop < baseBottomY) innerTop = baseBottomY;
        out.bottom.valid = innerTop < bottomY - 0.5f;
        out.bottom.left = left;
        out.bottom.top = innerTop;
        // 横向带负责右下角 ⇒ 竖向带在它内侧收住，避免重叠
        out.bottom.right = out.right.valid ? out.right.left : rightX;
        out.bottom.bottom = bottomY;
        out.bottom.cornerBottomLeft = r;
        out.bottom.cornerBottomRight = out.right.valid ? 0.0f : r;
    }
    return out;
}

// 到位时间估计（界面上显示的参考值；真实时长由弹簧 + 帧时间 + 到位判据决定）。
//   ζ<1：包络 ~ e^(-ζωt) ⇒ 到 0.5% 需要 t ≈ 6/(ζω)
//   ζ≥1：临界阻尼 ~ (1+ωt)e^(-ωt) ⇒ t ≈ 7.4/ω
// 默认参数（k=420 / c=38.5）⇒ 约 310ms，与实跑量的 300–400ms 吻合。
inline float EstimateCommitDurationMs(const ResizePreviewParams& params) {
    const float k = params.springStiffness > 1.0f ? params.springStiffness : 420.0f;
    const float c = params.springDamping > 0.0f ? params.springDamping : 38.5f;
    const float omega = std::sqrt(k);
    const float zeta = c / (2.0f * omega);
    const float seconds = zeta < 0.999f ? 6.0f / (zeta * omega) : 7.4f / omega;
    return seconds * 1000.0f;
}

// 尺寸数值文本（拖动时显示在预览框外，方便用户确认目标）
inline void FormatPreviewSizeLabel(float width, float height, char* out, int capacity) {
    if (out == nullptr || capacity <= 0) return;
    std::snprintf(out, static_cast<std::size_t>(capacity), "%.0f x %.0f", width, height);
}

}  // namespace aurora
