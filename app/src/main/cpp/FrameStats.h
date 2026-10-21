#pragma once

#include <algorithm>
#include <cstdint>

namespace aurora {

// =============================================================================
// 帧统计（FrameStats）
//
// ★ 这一版重写了统计口径，起因是用户报的现象：
//     「我感觉到明显的掉帧，但是性能监测里面还显示高于 120 的帧率」
//
//   旧实现有两个致命缺陷，正好能制造这种矛盾：
//
//   ① smoothedFps 是 EMA 平滑（*0.92 + *0.08）。
//      一次 43ms 的长帧只把平均值拉低 0.08 份，紧接着几帧正常就完全淹没了。
//      用户实际感受到的**正是**这种偶发长帧，但平均帧率看不出来。
//
//   ② worstFrameMilliseconds 是 std::max 累积，**永不重置**。
//      它显示 43ms 可能是十分钟前的一次抖动，对"现在卡不卡"零信息量。
//
//   新口径同时给出三类信息，缺一不可：
//     · 平均帧率     —— 稳态吞吐（旧 smoothedFps 保留，但不再单独展示）
//     · 窗口内最差帧 —— 最近 1 秒真正的最差，会随时间滑走
//     · 长帧计数     —— 超过阈值的帧数，这是"卡顿感"的直接来源
//
//   阈值用 1.5 个刷新周期：120Hz ⇒ 8.33ms ⇒ 12.5ms 以上算一次长帧。
//   这样既不把正常抖动算进去，也不会漏掉用户能感知到的顿挫。
// =============================================================================
struct FrameStats final {
    std::uint64_t frameCount = 0;
    float lastDeltaSeconds = 0.0f;
    float smoothedFps = 0.0f;

    // 最近 1 秒内最差的帧时间（每秒重置）。
    float worstFrameMilliseconds = 0.0f;

    // 长帧统计：超过 jankThresholdMs 的帧。
    float jankThresholdMs = 12.5f;          // 1.5 × (1000/120)
    std::uint32_t jankFramesInWindow = 0;   // 当前窗口内长帧数
    float jankSecondsInWindow = 0.0f;       // 当前窗口时长（秒）
    float jankPerSecond = 0.0f;             // 每秒长帧数 —— 越小越顺
    std::uint64_t jankFramesTotal = 0;      // 累计（参考用，不是实时指标）

    void tick(float seconds) {
        lastDeltaSeconds = std::max(0.0f, seconds);
        if (lastDeltaSeconds > 0.0001f) {
            const float fps = 1.0f / lastDeltaSeconds;
            smoothedFps = smoothedFps == 0.0f ? fps : smoothedFps * 0.92f + fps * 0.08f;
        }

        const float ms = lastDeltaSeconds * 1000.0f;
        worstFrameMilliseconds = std::max(worstFrameMilliseconds, ms);

        if (ms > jankThresholdMs) {
            ++jankFramesInWindow;
            ++jankFramesTotal;
        }
        jankSecondsInWindow += lastDeltaSeconds;

        // 每 1 秒结算一次：把"这一秒里有几帧是卡的"算成每秒值，然后开新窗口。
        // 用 1 秒而不是更长，是为了让面板上的数字跟得上用户的手感变化。
        if (jankSecondsInWindow >= 1.0f) {
            jankPerSecond = static_cast<float>(jankFramesInWindow) / jankSecondsInWindow;
            jankFramesInWindow = 0;
            jankSecondsInWindow = 0.0f;
            // 最差帧也每秒重置：显示"最近这一秒的最差"，而不是历史最差。
            worstFrameMilliseconds = ms;
        }

        ++frameCount;
    }

    // "这一秒顺不顺"：每秒长帧少于半个才算真正流畅。
    bool Smooth() const { return jankPerSecond < 0.5f; }
};

}  // namespace aurora
