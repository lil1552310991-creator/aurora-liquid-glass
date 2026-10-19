#pragma once

#include "UiTouchGeometry.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aurora {

// Android Region uses half-open integer rectangles in the layer's local space.
struct TouchRect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    bool operator==(const TouchRect& r) const {
        return left == r.left && top == r.top && right == r.right && bottom == r.bottom;
    }
};

struct TouchShape {
    float left = 0, top = 0, right = 0, bottom = 0;
    float radius = 0;
    // TL=1, TR=2, BR=4, BL=8; match the corners actually drawn by the host.
    uint8_t roundedCorners = 15;
    float clipLeft = -std::numeric_limits<float>::infinity();
    float clipTop = -std::numeric_limits<float>::infinity();
    float clipRight = std::numeric_limits<float>::infinity();
    float clipBottom = std::numeric_limits<float>::infinity();
};

inline bool TouchRegionContains(const std::vector<TouchRect>& rects, int32_t x, int32_t y) {
    for (const auto& r : rects)
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
    return false;
}

// Canonical row bands preserve holes and disjoint windows. A bounding rectangle
// would capture the transparent space between them, even if ImGui ignores it.
inline std::vector<TouchRect> UnionTouchRects(const std::vector<TouchRect>& input) {
    std::vector<int32_t> ys;
    for (const auto& r : input) {
        if (r.left < r.right && r.top < r.bottom) {
            ys.push_back(r.top);
            ys.push_back(r.bottom);
        }
    }
    std::sort(ys.begin(), ys.end());
    ys.erase(std::unique(ys.begin(), ys.end()), ys.end());
    std::vector<TouchRect> result;
    size_t previousBegin = 0, previousCount = 0;
    for (size_t row = 1; row < ys.size(); ++row) {
        const int32_t top = ys[row - 1], bottom = ys[row];
        std::vector<TouchRect> intervals;
        for (const auto& r : input)
            if (r.left < r.right && r.top <= top && r.bottom >= bottom)
                intervals.push_back({r.left, top, r.right, bottom});
        std::sort(intervals.begin(), intervals.end(), [](const TouchRect& a, const TouchRect& b) {
            return a.left < b.left || (a.left == b.left && a.right < b.right);
        });
        std::vector<TouchRect> merged;
        for (const auto& r : intervals) {
            if (!merged.empty() && r.left <= merged.back().right)
                merged.back().right = std::max(merged.back().right, r.right);
            else merged.push_back(r);
        }
        bool same = merged.size() == previousCount && !merged.empty();
        for (size_t i = 0; same && i < merged.size(); ++i) {
            const auto& prev = result[previousBegin + i];
            same = prev.bottom == top && prev.left == merged[i].left && prev.right == merged[i].right;
        }
        if (same) {
            for (size_t i = 0; i < previousCount; ++i) result[previousBegin + i].bottom = bottom;
        } else {
            previousBegin = result.size();
            previousCount = merged.size();
            result.insert(result.end(), merged.begin(), merged.end());
        }
    }
    return result;
}

// Sample the same pixel centres used for rasterisation. Clipping is explicit so
// a freely dragged window never produces an unbounded/off-screen input region.
inline std::vector<TouchRect> BuildTouchShapeRegion(const std::vector<TouchShape>& shapes,
                                                     int32_t clipWidth, int32_t clipHeight) {
    std::vector<TouchRect> rows;
    if (clipWidth <= 0 || clipHeight <= 0) return rows;
    for (const auto& s : shapes) {
        if (!std::isfinite(s.left) || !std::isfinite(s.top) || !std::isfinite(s.right) ||
            !std::isfinite(s.bottom) || !std::isfinite(s.radius) ||
            s.right <= s.left || s.bottom <= s.top) continue;
        const float radius = std::max(0.0f, std::min(s.radius,
                                     0.5f * std::min(s.right - s.left, s.bottom - s.top)));
        if (std::isnan(s.clipLeft) || std::isnan(s.clipTop) || std::isnan(s.clipRight) ||
            std::isnan(s.clipBottom)) continue;
        const int32_t y0 = static_cast<int32_t>(std::clamp(std::ceil(std::max(s.top, s.clipTop) - 0.5f), 0.0f,
                                                        static_cast<float>(clipHeight)));
        const int32_t y1 = static_cast<int32_t>(std::clamp(std::ceil(std::min(s.bottom, s.clipBottom) - 0.5f), 0.0f,
                                                        static_cast<float>(clipHeight)));
        for (int32_t y = y0; y < y1; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            float inset = 0.0f;
            bool upper = false, lower = false;
            if (radius > 0 && py < s.top + radius) {
                const float dy = s.top + radius - py;
                inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
                upper = true;
            } else if (radius > 0 && py > s.bottom - radius) {
                const float dy = py - (s.bottom - radius);
                inset = radius - std::sqrt(std::max(0.0f, radius * radius - dy * dy));
                lower = true;
            }
            const bool roundLeft = (upper && (s.roundedCorners & 1)) ||
                                   (lower && (s.roundedCorners & 8));
            const bool roundRight = (upper && (s.roundedCorners & 2)) ||
                                    (lower && (s.roundedCorners & 4));
            const float left = std::max(s.clipLeft, s.left + (roundLeft ? inset : 0));
            const float right = std::min(s.clipRight, s.right - (roundRight ? inset : 0));
            const int32_t x0 = static_cast<int32_t>(std::clamp(std::ceil(left - 0.5f), 0.0f,
                                                            static_cast<float>(clipWidth)));
            const int32_t x1 = static_cast<int32_t>(std::clamp(std::ceil(right - 0.5f), 0.0f,
                                                            static_cast<float>(clipWidth)));
            if (x0 < x1) {
                if (!rows.empty() && rows.back().left == x0 && rows.back().right == x1 &&
                    rows.back().bottom == y) rows.back().bottom = y + 1;
                else rows.push_back({x0, y, x1, y + 1});
            }
        }
    }
    return UnionTouchRects(rows);
}

// ---------------------------------------------------------------------------
// WindowInfo 布局：**偏移不能写死**（这一条换过两次机器都栽在上面）
// ---------------------------------------------------------------------------
// `WindowInfo` 里 Region **之前**的字段是 AOSP 标准前缀（运行时也会校验：
// dispatchingTimeout@56 / globalScaleFactor@92，对不上就直接拒绝），
// 而 Region **及其之后**的字段会因 OEM 插入而整体移动 ——
// 开发机（测试机 / Android 16）就比 AOSP 多 64 字节的 "OEM Transform"
// （AOSP 的 Region 在 184，本机在 248）。
//
// ★★ 参照必须统一：`aosp_glue.h` 里的 `winfo::kOffXxx` 常量**本身已经是开发机实测值**
//    （Region=248，后面那些字段的 +64 也已经算进去了）。所以位移只能相对**同一个参照**算：
//        shift = 设备实测 Region 偏移 − kReferenceWindowInfoRegionOffset
//    本机 shift = 0（常量原样可用）；纯 AOSP 布局 shift = −64（≥248 的常量整体前移，
//    于是 Region→184、inputConfig→328，正是 AOSP 的位置）。
//    若按 AOSP 的 184 当参照，本机就会被**重复位移一次**，反而写错。
inline constexpr size_t kReferenceWindowInfoRegionOffset = 248;  // 开发机实测（= AOSP 184 + 64）
inline constexpr size_t kAospWindowInfoRegionOffset = 184;       // 仅用于日志/文档对照
inline constexpr size_t kWindowInfoRegionOffsetMin = 104;        // alpha@96 之后才有 Region 的位置
inline constexpr size_t kWindowInfoRegionOffsetMax = 896;        // 还要留得下 Region 之后的尾部字段

/** 设备实测 Region 偏移相对**本文件常量参照**的位移（本机 0；纯 AOSP 布局 −64）。 */
inline ptrdiff_t WindowInfoLayoutShift(size_t decodedRegionOffset) {
    return static_cast<ptrdiff_t>(decodedRegionOffset) -
           static_cast<ptrdiff_t>(kReferenceWindowInfoRegionOffset);
}

/** 前缀字段（Region 之前）不动，Region 及其之后的字段整体位移。 */
inline size_t ShiftWindowInfoOffset(size_t offset, ptrdiff_t shift) {
    if (offset < kReferenceWindowInfoRegionOffset) return offset;
    const ptrdiff_t moved = static_cast<ptrdiff_t>(offset) + shift;
    return moved < 0 ? 0u : static_cast<size_t>(moved);
}

// Runtime verification for the OEM WindowInfo thunk before touching Region.
// Accept BTI + ADD x0,x0,#offset + B, or ADD+B.
//
// ★★ 这里**只校验形状与合理性，偏移取设备实测值**：
//    要求 `decoded == 248` 是这台开发机的值 —— 换台机器（Region 仍在 AOSP 的 184）
//    就会被整条拒绝，于是 `replaceTouchableRegionRects()` 第一行 return false，
//    拦截失效并走进失败清理路径（朋友的 另一台设备 上就是这样崩的）。
//    这正是"开发机上永远测不出来"的那类 bug。
inline bool DecodeWindowInfoRegionOffset(const uint32_t* code, size_t count, size_t* offset) {
    if (!code || !offset || count < 2) return false;
    size_t i = (code[0] == 0xd503245fU || code[0] == 0xd503249fU) ? 1 : 0;
    if (count < i + 2 || (code[i] & 0xffc003ffU) != 0x91000000U ||
        (code[i + 1] & 0xfc000000U) != 0x14000000U) return false;
    const size_t decoded = (code[i] >> 10) & 0xfffU;
    // Region 是 8 字节对齐的对象；太小会落进标量前缀，太大就装不进 WindowInfo。
    if ((decoded & 7u) != 0u) return false;
    if (decoded < kWindowInfoRegionOffsetMin || decoded > kWindowInfoRegionOffsetMax) return false;
    *offset = decoded;
    return true;
}

inline std::vector<TouchRect> BuildOverlayTouchRegion(const std::vector<VisibleUiShape>& shapes,
                                                     int32_t clipWidth, int32_t clipHeight) {
    std::vector<TouchShape> rasterShapes;
    rasterShapes.reserve(shapes.size());
    for (const auto& s : shapes) {
        rasterShapes.push_back({s.left,s.top,s.right,s.bottom,
            s.shape == VisibleUiShapeKind::RoundedRectangle ? s.radius : 0.0f,
            s.roundedCorners,s.clipLeft,s.clipTop,s.clipRight,s.clipBottom});
    }
    return BuildTouchShapeRegion(rasterShapes,clipWidth,clipHeight);
}

} // namespace aurora
