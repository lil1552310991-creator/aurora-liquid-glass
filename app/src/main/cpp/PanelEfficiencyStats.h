#pragma once

#include <atomic>
#include <cstdint>

namespace aurora::panel_efficiency {

// Diagnostic counters are written by the render thread and sampled by the
// overlay's control thread. A snapshot is approximate across fields, but each
// individual counter is safe to read without holding a render lock.
inline std::atomic<std::uint64_t> captureCallbacks{0};
inline std::atomic<std::uint64_t> paintCallbacks{0};
inline std::atomic<std::uint64_t> combinedCallbacks{0};
inline std::atomic<std::uint64_t> resetsSaved{0};
inline std::atomic<std::uint64_t> culledCaptures{0};
inline std::atomic<std::uint64_t> culledPaints{0};
inline std::atomic<std::uint64_t> culledShadows{0};
inline std::atomic<std::uint64_t> sceneComposes{0};
inline std::atomic<std::uint64_t> framebufferChecks{0};
inline std::atomic<std::uint64_t> framebufferFailures{0};
// 采样区/模糊目标的总像素数 —— 性能优化的主判据（留白越小，拷贝/合成/模糊
// 的像素越少）。按帧累加，由宿主按秒求平均。
inline std::atomic<std::uint64_t> capturedPixels{0};
inline std::atomic<std::uint64_t> gatheredPixels{0};
inline std::atomic<std::uint64_t> capturedPanels{0};
// 每帧真实跑了多少次 gather / 画了多少次面板 —— 用来抓「同一块玻璃被模糊两遍」
// 这类重复工作（低配/低频下模糊是最贵的一步）。
inline std::atomic<std::uint64_t> gatherRuns{0};
inline std::atomic<std::uint64_t> panelPaints{0};

struct Snapshot {
    std::uint64_t captureCallbacks = 0;
    std::uint64_t paintCallbacks = 0;
    std::uint64_t combinedCallbacks = 0;
    std::uint64_t resetsSaved = 0;
    std::uint64_t culledCaptures = 0;
    std::uint64_t culledPaints = 0;
    std::uint64_t culledShadows = 0;
    std::uint64_t sceneComposes = 0;
    std::uint64_t framebufferChecks = 0;
    std::uint64_t framebufferFailures = 0;
    std::uint64_t capturedPixels = 0;
    std::uint64_t gatheredPixels = 0;
    std::uint64_t capturedPanels = 0;
    std::uint64_t gatherRuns = 0;
    std::uint64_t panelPaints = 0;
};

inline Snapshot snapshot() {
    return {
        captureCallbacks.load(std::memory_order_relaxed),
        paintCallbacks.load(std::memory_order_relaxed),
        combinedCallbacks.load(std::memory_order_relaxed),
        resetsSaved.load(std::memory_order_relaxed),
        culledCaptures.load(std::memory_order_relaxed),
        culledPaints.load(std::memory_order_relaxed),
        culledShadows.load(std::memory_order_relaxed),
        sceneComposes.load(std::memory_order_relaxed),
        framebufferChecks.load(std::memory_order_relaxed),
        framebufferFailures.load(std::memory_order_relaxed),
        capturedPixels.load(std::memory_order_relaxed),
        gatheredPixels.load(std::memory_order_relaxed),
        capturedPanels.load(std::memory_order_relaxed),
        gatherRuns.load(std::memory_order_relaxed),
        panelPaints.load(std::memory_order_relaxed),
    };
}

}  // namespace aurora::panel_efficiency
