#pragma once

// 动效基础层：弹簧求解器 + 速度追踪。
//
// 参数取自 Kyant0 的 AndroidLiquidGlass / AMDL，规格见
// docs/AMDL_MOTION_CATALOG.md 第 1 章。两处关键差异值得留意：
//
//  1. Compose 的 `stiffness` **就是 k，不是角频率**。
//     spring(1f, 300f) 意味着 ωn = sqrt(300) ≈ 17.3 rad/s（周期约 363ms），
//     而不是 300 rad/s。这一点极易搞错。
//
//  2. 必须用固定子步长积分。stiffness = 1000 时 ωn ≈ 31.6 rad/s，
//     显式欧拉在 dt > 2/ωn ≈ 63ms 时发散；60fps 勉强安全，掉到 30fps 就炸。
//     子步到 240Hz 后可覆盖到 stiffness ≈ 10000。
//     本文件原先的实现是单步欧拉 + dt 上限 0.05s，属于会发散的写法，已改。
//
// 弹簧参数换算速查（ζ = dampingRatio，k = stiffness）：
//
//   spring(1.0, 1500f)   ωn 38.7   ≈171ms   Compose 默认，未指定 spec 时
//   spring(1.0, 1000f)   ωn 31.6   ≈210ms   DampedDragAnimation 主值
//   spring(1.0,  600f)   ωn 24.5   ≈271ms   Switch
//   spring(1.0,  500f)   ωn 22.4   ≈297ms   Indication
//   spring(1.0,  300f)   ωn 17.3   ≈384ms   ControlCenter
//   spring(0.5,  300f)   ωn 17.3   ≈531ms   会过冲约 16%
//   spring(0.6,  250f)   ωn 15.8   ≈485ms   scaleX
//   spring(0.7,  250f)   ωn 15.8   ≈415ms   scaleY
//
// 稳定时间公式：临界阻尼 t ≈ 6.64/ωn；欠阻尼 t ≈ 4.6/(ζ·ωn)。

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aurora {

class Spring final {
public:
    explicit Spring(float value = 0.0f) : value_(value), target_(value) {}

    // ---- Kyant0 / Compose 风格构造 ----
    // dampingRatio: ζ，1.0 = 临界阻尼（不过冲），0.5 = 有回弹
    // stiffness:    k，直接就是刚度，不是角频率
    static Spring FromStiffness(float value, float dampingRatio, float stiffness) {
        Spring s(value);
        s.dampingRatio_ = std::max(0.01f, dampingRatio);
        s.stiffness_ = std::max(1.0f, stiffness);
        s.useStiffness_ = true;
        return s;
    }

    // ---- 旧的 response/damping 参数化，保留以兼容既有调用 ----
    // response 是「振荡周期」（秒），换算成刚度：k = (2π/response)^2
    void setResponse(float response) {
        response_ = std::max(0.01f, response);
        if (useStiffness_) return;
        const float omega = 2.0f * 3.14159265358979f / response_;
        stiffness_ = omega * omega;
    }
    void setDamping(float damping) {
        damping_ = std::max(0.01f, damping);
        if (!useStiffness_) dampingRatio_ = damping_;
    }

    // ---- 直接设置 Compose 语义的参数 ----
    void setStiffness(float stiffness) {
        stiffness_ = std::max(1.0f, stiffness);
        useStiffness_ = true;
    }
    void setDampingRatio(float zeta) { dampingRatio_ = std::max(0.01f, zeta); }
    void setThreshold(float threshold) { threshold_ = std::max(1e-6f, threshold); }

    void setTarget(float target) { target_ = target; }
    void snap(float value) {
        value_ = value;
        target_ = value;
        velocity_ = 0.0f;
    }
    // 把当前速度设为给定值（到位移以速度作为初速传入的场景，如拖拽甩出）
    void setVelocity(float velocity) { velocity_ = velocity; }

    float value() const { return value_; }
    float target() const { return target_; }
    float velocity() const { return velocity_; }
    bool atRest() const {
        return std::abs(velocity_) < threshold_ && std::abs(value_ - target_) < threshold_;
    }

    // 返回当前值。已静止时返回目标值并清零速度。
    float tick(float deltaSeconds) {
        if (deltaSeconds <= 0.0f) return value_;
        // 掉帧保护：一帧最多前进 1/30 秒，避免子步数爆炸
        float dt = std::min(deltaSeconds, 1.0f / 30.0f);

        float k = stiffness_;
        if (!useStiffness_ && response_ > 0.0f) {
            const float omega = 2.0f * 3.14159265358979f / response_;
            k = omega * omega;
        }
        const float zeta = useStiffness_ ? dampingRatio_ : damping_;
        const float wn = std::sqrt(k);

        // 固定子步长 240Hz：显式欧拉在 dt > 2/wn 时发散，
        // stiffness 越大越危险，子步是唯一稳妥做法。
        constexpr float kSubStep = 1.0f / 240.0f;
        int steps = static_cast<int>(std::ceil(dt / kSubStep));
        steps = std::clamp(steps, 1, 16);
        const float sub = dt / static_cast<float>(steps);

        for (int i = 0; i < steps; ++i) {
            const float x = value_ - target_;
            const float a = -k * x - 2.0f * zeta * wn * velocity_;
            velocity_ += a * sub;      // 半隐式欧拉：先速度后位置，比显式稳定得多
            value_ += velocity_ * sub;
        }

        if (atRest()) {
            value_ = target_;
            velocity_ = 0.0f;
        }
        return value_;
    }

private:
    float value_ = 0.0f;
    float target_ = 0.0f;
    float velocity_ = 0.0f;
    float response_ = 0.28f;      // 旧参数化：周期（秒）
    float damping_ = 0.92f;       // 旧参数化：阻尼比
    float stiffness_ = 1500.0f;   // k
    float dampingRatio_ = 1.0f;   // ζ
    float threshold_ = 0.01f;     // Compose visibilityThreshold
    bool useStiffness_ = false;
};

// 拖拽松手时判定「甩出」用的速度估计。
//
// 参照 Compose 的 VelocityTracker：对最近 horizon 秒内的采样做拟合，
// 求当前时刻的导数。这里用首末差分（等价于一次线性拟合），对 UI 足够。
class VelocityTracker final {
public:
    static constexpr int kCapacity = 32;

    void reset() { count_ = 0; }

    void add(double time, float x, float y) {
        if (count_ == kCapacity) {
            std::memmove(buf_, buf_ + 1, sizeof(Sample) * (kCapacity - 1));
            --count_;
        }
        buf_[count_++] = Sample{time, x, y};
    }

    // 输出单位：px/秒
    void calc(float& vx, float& vy, double now, double horizon = 0.1) const {
        vx = 0.0f;
        vy = 0.0f;
        if (count_ < 2) return;
        int first = count_ - 1;
        while (first > 0 && now - buf_[first].t < horizon) --first;
        if (count_ - first < 2) return;
        const double dt = buf_[count_ - 1].t - buf_[first].t;
        if (dt <= 1e-6) return;
        vx = static_cast<float>((buf_[count_ - 1].x - buf_[first].x) / dt);
        vy = static_cast<float>((buf_[count_ - 1].y - buf_[first].y) / dt);
    }

private:
    struct Sample {
        double t;
        float x, y;
    };
    Sample buf_[kCapacity]{};
    int count_ = 0;
};

// Compose 的过拖阻尼曲线：sign(x) * (1 - e^{-|x|})
//
// 用途：拖拽超过边界时，位移越大越难继续拖，且永远不会拉长到无穷。
// 取自 ControlCenterContent.kt 的 ProgressConverter。
inline float ProgressConverter(float x) {
    const float a = std::abs(x);
    const float s = (x < 0.0f) ? -1.0f : 1.0f;
    return s * (1.0f - std::exp(-a));
}

// Compose 常用的三条缓动曲线。它们在 Compose 里是 CubicBezier 定义的，
// 所以这里直接解三次贝塞尔，而不是用多项式糊一个近似
// （近似会让「快出慢入」的手感与原实现有可感知的差别）。
//
// 核实过的常量（docs/AMDL_MOTION_CATALOG.md 第 1 章）：
//   EaseIn        = CubicBezier(0.42, 0,    1,    1)
//   EaseOut       = CubicBezier(0,    0,    0.58, 1)
//   FastOutSlowIn = CubicBezier(0.4,  0,    0.2,  1)
namespace easing_detail {

// 一维三次贝塞尔：B(t) = 3(1-t)^2 t·p1 + 3(1-t)t^2·p2 + t^3
inline float bezier1d(float t, float p1, float p2) {
    const float u = 1.0f - t;
    return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
}

inline float bezier1dDeriv(float t, float p1, float p2) {
    const float u = 1.0f - t;
    return 3.0f * u * u * p1 + 6.0f * u * t * (p2 - p1) + 3.0f * t * t * (1.0f - p2);
}

// 给定 x，反解参数 t（牛顿迭代 + 二分兜底），再求 y
inline float cubicBezier(float x, float x1, float y1, float x2, float y2) {
    x = std::clamp(x, 0.0f, 1.0f);
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;

    float t = x;  // 初值
    for (int i = 0; i < 8; ++i) {
        const float err = bezier1d(t, x1, x2) - x;
        if (std::abs(err) < 1e-6f) break;
        const float d = bezier1dDeriv(t, x1, x2);
        if (std::abs(d) < 1e-6f) break;
        t -= err / d;
        t = std::clamp(t, 0.0f, 1.0f);
    }
    // 牛顿不收敛时退回二分，保证单调有界
    if (std::abs(bezier1d(t, x1, x2) - x) > 1e-4f) {
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 24; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (bezier1d(mid, x1, x2) < x) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        t = 0.5f * (lo + hi);
    }
    return bezier1d(t, y1, y2);
}

}  // namespace easing_detail

inline float EaseIn(float t) { return easing_detail::cubicBezier(t, 0.42f, 0.0f, 1.0f, 1.0f); }
inline float EaseOut(float t) { return easing_detail::cubicBezier(t, 0.0f, 0.0f, 0.58f, 1.0f); }
inline float FastOutSlowIn(float t) {
    return easing_detail::cubicBezier(t, 0.4f, 0.0f, 0.2f, 1.0f);
}

// ===========================================================================
// M-10 通用阻尼拖拽动画（DampedDragAnimation）—— 所有交互件的引擎
//
// 逐字移植自 Kyant0/AndroidLiquidGlass 的
//   app/src/commonMain/kotlin/com/kyant/backdrop/catalog/utils/DampedDragAnimation.kt
// （137 行，本机源码已逐行核对，`docs/AMDL_MOTION_CATALOG.md` M-10 的参数表与源码一致）。
// LiquidToggle / LiquidSlider / LiquidBottomTabs 三处共用它。
//
// ★ 两个**漏了就废掉手感**的细节（源码 :93-106 与 :129-136）：
//
//   1. `release()` 是「**先落位，再松开手指**」：
//      松手时若主值还没到目标，**先等**主值进入目标 ±2.5% 区间，**才**开始把
//      pressProgress / scaleX / scaleY 收回去。少了这一步就会变成
//      「手指松了但东西还在飞」的割裂感。
//      注意源码里还有一次 `awaitFrame()`（:95）—— 等到下一帧才判条件。
//
//   2. 速度是**独立平滑**的，不是直接量出来的（:129-136）：
//      VelocityTracker 量到的速度先喂给一个 `spring(0.5, 300)` 的弹簧，
//      所以它天然带迟滞 —— 拿它去拉伸形变时不会抖。
//
// 五条 spring spec（源码 :31-40 逐字）：
//   value      spring(ζ=1.0, k=1000, thr=visibilityThreshold)
//   velocity   spring(ζ=0.5, k=300,  thr=visibilityThreshold*10)
//   press      spring(ζ=1.0, k=1000, thr=0.001)
//   scaleX     spring(ζ=0.6, k=250,  thr=0.001)
//   scaleY     spring(ζ=0.7, k=250,  thr=0.001)
// ===========================================================================
class DampedDrag final {
public:
    // 对应 Kotlin 的构造参数。默认值取 LiquidBottomTabs 的常用组合。
    void configure(float initialValue, float rangeLo, float rangeHi, float visibilityThreshold,
                   float initialScale = 1.0f, float pressedScale = 1.5f) {
        rangeStart_ = rangeLo;
        rangeEnd_ = rangeHi;
        initialValue_ = initialValue;
        initialScale_ = initialScale;
        pressedScale_ = pressedScale;
        visibilityThreshold_ = std::max(1e-6f, visibilityThreshold);

        value_ = Spring::FromStiffness(initialValue, 1.0f, 1000.0f);
        value_.setThreshold(visibilityThreshold_);

        velocity_ = Spring::FromStiffness(0.0f, 0.5f, 300.0f);
        velocity_.setThreshold(visibilityThreshold_ * 10.0f);

        press_ = Spring::FromStiffness(0.0f, 1.0f, 1000.0f);
        press_.setThreshold(0.001f);

        scaleX_ = Spring::FromStiffness(initialScale, 0.6f, 250.0f);
        scaleX_.setThreshold(0.001f);

        scaleY_ = Spring::FromStiffness(initialScale, 0.7f, 250.0f);
        scaleY_.setThreshold(0.001f);

        pendingRelease_ = false;
        pendingReleaseFrames_ = 0;
        tracker_.reset();
    }

    // ---- 只读量（对应源码的 value / progress / targetValue / ... ）----
    float value() const { return value_.value(); }
    float targetValue() const { return value_.target(); }
    float progress() const {
        const float span = rangeEnd_ - rangeStart_;
        return span == 0.0f ? 0.0f : (value_.value() - rangeStart_) / span;
    }
    float pressProgress() const { return press_.value(); }
    float scaleX() const { return scaleX_.value(); }
    float scaleY() const { return scaleY_.value(); }
    float velocity() const { return velocity_.value(); }
    bool atRest() const {
        return value_.atRest() && press_.atRest() && scaleX_.atRest() && scaleY_.atRest() &&
               !pendingRelease_;
    }

    // ---- 按下（源码 :84-91）----
    // 注意顺序：**先 resetTracking**，否则会把上一轮的手指速度带进来。
    void press() {
        tracker_.reset();
        press_.setTarget(1.0f);
        // 源码 :88-89 —— scaleX 与 scaleY 的 animateTo 目标**都是** pressedScale。
        scaleX_.setTarget(pressedScale_);
        scaleY_.setTarget(pressedScale_);
    }

    // ---- 松手（源码 :93-106）----
    // 只标记；真正的回落发生在 tick() 里条件满足之后。
    void release() {
        pendingRelease_ = true;
        pendingReleaseFrames_ = 1;  // 对应源码的 awaitFrame()：等一帧再判
    }

    // ---- 拖拽中直接改值（对应 updateValue，源码 :108-113）----
    // 目标被夹到 [rangeStart, rangeEnd]。
    void updateValue(float v) {
        value_.setTarget(std::clamp(v, rangeStart_, rangeEnd_));
    }

    // ---- 点击式跳转（对应 animateToValue，源码 :115-127）----
    // press() → 目标值 → 把速度收回 0 → release()
    //
    // ★ 这里有一个源码层面**无法单方面判定**的含糊点，我按它的明确意图做了确定性处理：
    //   源码 :120 与 :121-123 是**并发 launch** 的两件事 ——
    //       launch { valueAnimation.animateTo(target) { updateVelocity() } }   // 每帧改速度目标
    //       if (velocity != 0f) launch { velocityAnimation.animateTo(0f, ...) } // 一次性收到 0
    //   谁最终生效取决于 Compose 里 Animatable 的取消语义与协程调度顺序，是**竞态**。
    //   而 :121-123 这个分支的存在本身就说明意图是「点击跳转后速度应收敛到 0」。
    //   所以这里不让每帧追踪去覆盖它：设一个 pending 标志，
    //   等值落位后把速度目标收回 0，并在此后停止覆盖。
    //   （拖拽路径 updateValue() 不受影响 —— 那条路径残留速度是**有意**的，见 tick 注释。）
    void animateToValue(float v) {
        press();
        value_.setTarget(std::clamp(v, rangeStart_, rangeEnd_));
        velocityResetPending_ = true;
        release();
    }

    // ---- 每帧推进 ----
    // nowSeconds 用于速度追踪；dt 是帧间隔（秒）。
    void tick(float deltaSeconds, double nowSeconds) {
        // 1) 松手的「先落位再弹回」判定（源码 :96-101）
        if (pendingRelease_) {
            if (pendingReleaseFrames_ > 0) {
                --pendingReleaseFrames_;  // awaitFrame 等价物
            } else {
                const float span = rangeEnd_ - rangeStart_;
                const float threshold = span * 0.025f;  // ★ 区间的 2.5%
                const float diff = std::abs(value_.value() - value_.target());
                if (diff < threshold || value_.value() == value_.target()) {
                    press_.setTarget(0.0f);
                    scaleX_.setTarget(initialScale_);
                    scaleY_.setTarget(initialScale_);
                    pendingRelease_ = false;
                }
            }
        }

        // 2) 速度追踪：源码在 animateTo 的 block 里**每帧**调 updateVelocity()，
        //    所以这里只要主值还在动就更新（静止后再喂会污染拟合）。
        //    注意：拖拽路径（updateValue）结束后**速度不归零**是**有意**的 ——
        //    残留速度留给消费者做「液体拉伸」（M-11 的 v/50）。
        //    只有点击路径（animateToValue）才要求收敛到 0，见下面的 2b。
        if (!value_.atRest()) {
            tracker_.add(nowSeconds, value_.value(), 0.0f);
            float vx = 0.0f, vy = 0.0f;
            tracker_.calc(vx, vy, nowSeconds);
            const float span = rangeEnd_ - rangeStart_;
            if (span != 0.0f && !velocityResetPending_) velocity_.setTarget(vx / span);  // 源码 :134 归一化
        }

        // 2b) 点击路径的收尾：值一落位就把速度目标收回 0（源码 :121-123 的意图）
        if (velocityResetPending_ && value_.atRest()) {
            velocity_.setTarget(0.0f);
            velocityResetPending_ = false;
        }

        // 3) 推进五条弹簧
        value_.tick(deltaSeconds);
        velocity_.tick(deltaSeconds);
        press_.tick(deltaSeconds);
        scaleX_.tick(deltaSeconds);
        scaleY_.tick(deltaSeconds);
    }

private:
    Spring value_{};
    Spring velocity_{};
    Spring press_{};
    Spring scaleX_{};
    Spring scaleY_{};
    VelocityTracker tracker_{};

    float rangeStart_ = 0.0f;
    float rangeEnd_ = 1.0f;
    float initialValue_ = 0.0f;
    float initialScale_ = 1.0f;
    float pressedScale_ = 1.5f;
    float visibilityThreshold_ = 0.01f;

    bool pendingRelease_ = false;
    int pendingReleaseFrames_ = 0;
    bool velocityResetPending_ = false;   // animateToValue 要求的「速度收敛到 0」待办
};

}  // namespace aurora
