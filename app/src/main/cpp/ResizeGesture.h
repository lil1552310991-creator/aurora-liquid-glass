#pragma once

// ===========================================================================
//  二级窗口右下角"长按 + 拖动缩放"的手势内核（纯逻辑，零 ImGui/Android 依赖）
// ===========================================================================
//  用户要求（照抄要点）：
//   · 默认**不显示任何缩放图标**；旧的那个 ImGui resize grip 连同它的独立热区一起删掉；
//   · 右下角一块**扩大过的隐形判定区**（向窗口内延伸约 32–48dp），
//     长按 **120ms**（= LONG_PRESS_TRIGGER_MS，统一常量）才激活，期间允许**轻微位移**，
//     超出容差就取消（避免误触）；普通点击/页面滚动**不能**触发缩放；
//   · 判定区不能无条件抢占内部按钮：宿主那边先做组件命中判定，组件优先；
//   · 激活后**不用松手**就能继续拖：右/下增大、左/上减小、斜向同时改；
//   · 左上角固定、有最小/最大尺寸、实时跟手；
//   · 松手立刻定尺寸，白色手柄走**同一套**非线性弹簧收回。
//   ★ 两种缩放方式（模式 A 实时 / 模式 B 虚线预览）**复用这一套长按识别与
//     手势状态机**，只有"拖动期间改什么"不同 —— 见 ResizePreview.h。
//
//  这里只放"判定与数学"：长按状态机、判定区、尺寸夹紧、阻尼弹簧。
//  手柄的**几何与动画**在 CornerResizeHandle.h；虚线预览与提交动画在 ResizePreview.h；
//  ImGui 接线在 CornerResizeOverlay.cpp。
//
//  ★ 两个只有真机才暴露的坑，已经写进这一版的设计里（别再踩回去）：
//   1. **按住时长必须用真实时钟**，不能用 `io.DeltaTime` 累加 ——
//      Renderer 把 DeltaTime 夹在 [1/240, 0.05] 秒（掉帧保护），
//      真机卡顿（采集管线启动那几秒）时一帧真实 100ms 却只记 50ms ⇒
//      450ms 的长按实测要按到 ~1.0s 才激活（真机日志量到过）。
//      现在由宿主传**自按下起算的绝对毫秒数**，阈值就永远是 450ms。
//   2. **首帧坐标可能是陈旧的**：输入桥一帧才刷一次位置，DOWN 那一帧拿到的
//      可能还是上一次的位置 ⇒ dx/dy 会突然变成一个巨大的值（真机上直接把窗口
//      撑到屏幕外）。现在：单帧跳变超过 rebaseJumpDp 就判定为"坐标重新同步"，
//      把起手点一起平移过去（dx/dy 保持不变），而不是把它当成一次拖动。

#include <cmath>

namespace aurora {

// ★ 长按触发阈值（毫秒）—— 用户指定的统一常量，**只有这一个来源**。
//   为什么必须用真实时间：宿主传进来的是"自按下那一刻起算的绝对毫秒数"
//   （单调时钟算的），不是按帧累加 —— Renderer 把 io.DeltaTime 夹在
//   [1/240, 0.05] 秒（掉帧保护），真机卡顿时按帧累加会把阈值拖成两倍以上。
//   实测证据：450ms 那一版在采集管线启动那几秒里，日志量到"真实按住 ~1000ms"才激活。
//
//   ★ 阈值沿革（用户实测反馈驱动，别再自己改回去）：
//       450ms → 330ms（"太慢了"）→ 150ms（"响应再快一点"）→ **120ms**（本轮用户指定）
//   120ms 已经很接近"点击 vs 拖动"的分界，所以判定区必须继续满足两条硬约束，
//   否则右下角那些按钮会被长按误抢：
//     · 判定区只在窗口右下角 **44dp** 以内（zoneDp）；
//     · 宿主**先过组件命中**，按在按钮/滑条上根本不进长按状态机。
//   ★ 两种缩放方式（实时 / 虚线预览）**共用这一个阈值**，不许各自维护一套。
inline constexpr float LONG_PRESS_TRIGGER_MS = 120.0f;

struct ResizeHoldParams {
    float longPressMs = LONG_PRESS_TRIGGER_MS;   // 长按触发时间（ms）
    float zoneDp = 44.0f;            // 隐形判定区：自右下角向窗口内延伸（32–48dp）
    float moveToleranceDp = 14.0f;   // 长按期间允许的轻微位移，超出即取消
    float minWidthDp = 320.0f;       // 最小宽高（防止缩到看不见）
    float minHeightDp = 220.0f;
    float maxWidthDp = 1600.0f;      // 最大宽高（防止失控）
    float maxHeightDp = 2200.0f;
    // 单帧位置跳变超过它就当作"坐标重新同步"（见文件头 ★2）。取 0 表示关闭该保护。
    float rebaseJumpDp = 120.0f;
};

// 每个窗口自己的尺寸参数。
//
// ★★ 为什么必须放在这里，而不是让宿主各自现写：最小尺寸的单位是 **dp**，
//    而窗口的默认尺寸是 **px**（760x1400），中间隔着密度（本工程 2.0）。
//    我踩过一次：给主窗口写 `minWidthDp = 420` 心想"420 比 760 小"，
//    实际 420dp × 2 = **840px > 760px** ⇒ 长按放手后窗口从 760 直接跳到 840
//    （用户什么都没拖，窗口却变宽了）。
//    把这两个函数放在这个纯逻辑头里，"最小尺寸必须小于默认窗口尺寸"这条约束
//    就能被 `ResizeGestureTest` 直接断言，不用上真机才发现。
inline ResizeHoldParams ResizeParamsForPanel() {
    ResizeHoldParams p;
    // 调试面板控件密（四列参数表 + 一排按钮），太小就没法用；
    // 但**必须**留得比默认的 760px 窄，否则第一次长按就会跳尺寸。
    p.minWidthDp = 340.0f;    // 340dp × 2 = 680px < 760px ✓
    p.minHeightDp = 260.0f;   // 260dp × 2 = 520px < 1400px ✓
    return p;
}

inline ResizeHoldParams ResizeParamsForSecondaryWindow() {
    return ResizeHoldParams();   // 二级窗口用全局默认（320 / 220 dp）
}

// 右下角缩放的参数（用户要求触发时间等必须是可调整常量）。

enum class ResizePhase {
    Idle,      // 没按
    Pending,   // 按在判定区里，还在数长按时间
    Armed,     // 长按达成：手柄弹出、进入可缩放（还没拖）
    Dragging,  // 正在拖（Armed 之后只要手指动了就算）
};

// 长按状态机。只吃"是否按下 / 是否在判定区内 / 手指位置 / 已按住多久"这些事实。
struct ResizeHold {
    ResizePhase phase = ResizePhase::Idle;
    float startX = 0.0f, startY = 0.0f;     // 起手点（判定区的按下位置）
    float lastX = 0.0f, lastY = 0.0f;       // 最近一次位置
    float armBaseMs = 0.0f;                 // 进入 Pending 那一刻的 heldMs
    bool startedInZone = false;

    bool Active() const { return phase == ResizePhase::Armed || phase == ResizePhase::Dragging; }
    bool Holding() const { return phase != ResizePhase::Idle; }

    // 手指位移（自按下点起算）。
    float dx() const { return lastX - startX; }
    float dy() const { return lastY - startY; }

    void Reset() {
        phase = ResizePhase::Idle;
        startedInZone = false;
        armBaseMs = 0.0f;
    }

    // 每帧推进。heldMs = **自本次按下起算的绝对毫秒数**（宿主用单调时钟算，别用 DeltaTime 累加）。
    // 返回**本帧是否刚刚激活**（true = 手柄该弹出了）。
    bool Update(bool down, bool inZone, float x, float y, float heldMs,
                const ResizeHoldParams& params, float moveTolerancePx,
                float rebaseJumpPx = 0.0f) {
        if (!down) {
            Reset();
            return false;
        }
        // 坐标必须是有限的：拿不到有效位置时**保持上一帧**，不要用垃圾值改尺寸。
        if (!(x == x) || !(y == y) || std::fabs(x) > 1.0e7f || std::fabs(y) > 1.0e7f) {
            return false;
        }

        if (phase == ResizePhase::Idle) {
            if (!inZone) return false;          // 只有按在判定区里才起手
            phase = ResizePhase::Pending;
            startX = lastX = x;
            startY = lastY = y;
            armBaseMs = heldMs;
            startedInZone = true;
            return false;
        }

        // ---- 坐标重新同步（★2）：跳变过大 ⇒ 平移起手点，dx/dy 不变 ----
        if (rebaseJumpPx > 0.0f
            && (std::fabs(x - lastX) > rebaseJumpPx || std::fabs(y - lastY) > rebaseJumpPx)) {
            startX += x - lastX;
            startY += y - lastY;
        }
        lastX = x;
        lastY = y;

        if (phase == ResizePhase::Pending) {
            // 超出容差 ⇒ 取消（这一次触摸不再可能变成缩放；宿主会退回去当滚动）
            const float ddx = dx();
            const float ddy = dy();
            if (std::sqrt(ddx * ddx + ddy * ddy) > moveTolerancePx) {
                Reset();
                return false;
            }
            if (heldMs - armBaseMs >= params.longPressMs) {
                phase = ResizePhase::Armed;     // ★ 激活：手柄弹出，且**不用松手**就能拖
                return true;
            }
            return false;
        }

        if (phase == ResizePhase::Armed) {
            // 激活之后只要手指动了就进入拖动（含斜向）。阈值取小一点，
            // 免得"轻微回弹"被当成拖动。
            if (std::fabs(dx()) > 1.0f || std::fabs(dy()) > 1.0f) phase = ResizePhase::Dragging;
            return false;
        }
        return false;   // Dragging：等松手
    }
};

// 长按判定区：窗口右下角 zonePx × zonePx，**只在窗口内**（不向窗口外扩，
// 免得"扩大无关区域的输入捕获范围"）。
inline bool CornerResizeZoneContains(float winLeft, float winTop, float winRight, float winBottom,
                                     float zonePx, float x, float y) {
    if (zonePx <= 0.0f) return false;
    if (winRight <= winLeft || winBottom <= winTop) return false;
    const float zoneW = zonePx < (winRight - winLeft) ? zonePx : (winRight - winLeft);
    const float zoneH = zonePx < (winBottom - winTop) ? zonePx : (winBottom - winTop);
    return x >= winRight - zoneW && x <= winRight && y >= winBottom - zoneH && y <= winBottom;
}

// 尺寸夹紧：最小/最大之间，别的原样返回。
inline float ClampResize(float value, float minValue, float maxValue) {
    if (maxValue < minValue) maxValue = minValue;
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

struct ResizeSize {
    float width = 0.0f;
    float height = 0.0f;
};

// 拖动 → 新尺寸：左上角固定，新尺寸 = 起手尺寸 + 手指位移，再夹到
// [minDp, min(maxDp, 屏幕余量)]。屏幕余量由宿主传进来（maxWidthPx / maxHeightPx），
// 这样窗口**永远不会被撑到屏幕外**（真机上出现过"撑到 2347 高"的坏状态）。
inline ResizeSize ResizeSizeFromDrag(float startWidth, float startHeight, float dx, float dy,
                                     const ResizeHoldParams& params, float pxPerDp,
                                     float maxWidthPx, float maxHeightPx) {
    if (!(pxPerDp > 0.0f)) pxPerDp = 1.0f;
    const float minW = params.minWidthDp * pxPerDp;
    const float minH = params.minHeightDp * pxPerDp;
    float capW = params.maxWidthDp * pxPerDp;
    float capH = params.maxHeightDp * pxPerDp;
    if (maxWidthPx > 0.0f && maxWidthPx < capW) capW = maxWidthPx;
    if (maxHeightPx > 0.0f && maxHeightPx < capH) capH = maxHeightPx;
    ResizeSize out;
    out.width = ClampResize(startWidth + dx, minW, capW);
    out.height = ClampResize(startHeight + dy, minH, capH);
    return out;
}

// 阻尼弹簧：Apple 控制中心那种"有回弹、可中途打断/反向"的手感。
// ★ 名字带 Resize 前缀：Motion.h 里已经有一个 aurora::Spring（Compose 语义弹簧），
//   两者语义不同（那个是 stiffness/dampingRatio 写法），不能同名。
struct ResizeSpring {
    float value = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;

    void Snap(float v) {
        value = v;
        target = v;
        velocity = 0.0f;
    }
    void SetTarget(float t) { target = t; }

    // stiffness: 劲度（越大越快）；damping: 阻尼（越小回弹越明显）
    // ★ 分步积分：单步上限 1/240 秒。掉帧时用大步长积分弹簧会**过冲甚至发散**
    //   （高刷新率下反而看不出来，掉一帧就露馅），所以这里按固定小步长走，
    //   保证"动画时间步长正确"这条要求在任何帧率下都成立。
    float Step(float dtSeconds, float stiffness, float damping) {
        if (!(dtSeconds > 0.0f)) return value;
        if (dtSeconds > 0.25f) dtSeconds = 0.25f;      // 极端卡顿保护
        constexpr float kMaxStep = 1.0f / 240.0f;
        int steps = static_cast<int>(std::ceil(dtSeconds / kMaxStep));
        if (steps < 1) steps = 1;
        if (steps > 60) steps = 60;
        const float h = dtSeconds / static_cast<float>(steps);
        for (int i = 0; i < steps; ++i) {
            const float accel = (target - value) * stiffness - velocity * damping;
            velocity += accel * h;
            value += velocity * h;
        }
        // 到位就吸附，避免永远抖在目标附近
        if (std::fabs(target - value) < 0.001f && std::fabs(velocity) < 0.01f) {
            value = target;
            velocity = 0.0f;
        }
        return value;
    }
};

}  // namespace aurora
