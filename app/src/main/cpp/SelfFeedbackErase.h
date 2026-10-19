#pragma once

// ===========================================================================
// 防自反馈：抹除界面区域 + 外圈环填充
//
// 背景（用户真机实测报告）：抓屏抓到的画面里**包含我们自己刚画上去的玻璃面板**。
// 直接拿它当 backdrop 去折射就形成正反馈回路，画面自我放大直至饱和 ——
// 用户看到的是「二级液态玻璃窗口无限镜像」，截图里是蓝/品红/橙/绿的失控递归色块。
//
// 解法照 docs/SOLVING_FIVE_ANDROID16_CHALLENGES.md 难题2「最难的一环：防自反馈」：
// 采集线程在把帧交给 GPU 之前，先在 CPU 侧**抹除面板所占区域**，
// 再用面板外圈的像素做**环填充**。
//
// ★ 为什么用环填充而不是填黑：玻璃会折射到这块区域，填黑会在玻璃边缘留下
//   一条突兀的暗边。用周围像素插值出来，视觉上就是"桌面本该有的样子"。
//
// ★ 为什么单独抽成纯函数：它是纯像素运算，但**画错了只表现为"边缘有暗带"
//   这种很主观的现象**，在真机上极难判断对错。抽出来就能在 PC 上逐像素断言
//   （tests/SelfFeedbackEraseTest.cpp）。
// ===========================================================================

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace aurora {

// 在**自上而下**的 RGBA8888 缓冲上，把 [x0,x1) x [y0,y1) 用外圈像素做环填充。
//
// rgba : 缓冲首地址，紧密排列，每行 dw*4 字节
// dw/dh: 缓冲宽高（像素）
// 越界的矩形会被夹到缓冲范围内；空矩形直接返回。
//
// 返回：实际被改写的像素数（便于调用方统计/断言）。
inline long long EraseRectWithRingFill(std::uint8_t* rgba, int dw, int dh, int x0, int y0,
                                        int x1, int y1) {
    if (rgba == nullptr || dw <= 0 || dh <= 0) return 0;

    // 夹到缓冲范围
    x0 = std::max(0, std::min(x0, dw));
    x1 = std::max(0, std::min(x1, dw));
    y0 = std::max(0, std::min(y0, dh));
    y1 = std::max(0, std::min(y1, dh));
    if (x1 <= x0 || y1 <= y0) return 0;

    const int w = x1 - x0;
    const int h = y1 - y0;
    const float wDen = static_cast<float>(std::max(1, w - 1));
    const float hDen = static_cast<float>(std::max(1, h - 1));

    auto at = [&](int x, int y) -> std::uint8_t* {
        return rgba + (static_cast<size_t>(y) * dw + x) * 4u;
    };

    // ---- 参考像素的取法（★ 这里有个真机级的坑）----
    //
    // 天真写法是 `lx = max(0, x0-1)` / `ty = max(0, y0-1)`。**但区域贴边时会退化**：
    // x0 == 0 时 lx 就是 0，**落在区域内部**，于是横向填充拿自己的第 0 列当参考，
    // 左边缘的原始像素被原样保留 ⇒ 自反馈断不干净。
    //
    // 而本项目的实际抹除区域是 (0,0)-(800,2160)，**正好贴着左上角** ——
    // 也就是说天真写法在真实用例上就是错的。（这个 bug 是 tests/SelfFeedbackEraseTest.cpp
    // 第 4 组断言抓出来的，真机上只会表现为"左边缘还残留一点镜像"。）
    //
    // 正确做法：优先取该侧紧邻区域外的像素；该侧不存在（贴边）就取**对侧**；
    // 两侧都不存在（区域横跨整幅）就说明没有外圈可用，跳过该方向。
    const bool hasLeft = (x0 > 0);
    const bool hasRight = (x1 < dw);
    const bool horizUsable = hasLeft || hasRight;
    const int lx = hasLeft ? (x0 - 1) : (x1 < dw ? x1 : 0);
    const int rx = hasRight ? x1 : (x0 > 0 ? (x0 - 1) : 0);

    // ---- 第一遍：水平插值（左边界外 ↔ 右边界外）----
    // 先做水平、再做垂直，两遍都在**原地**读写。
    // 第一遍只读区域外的列，第二遍只读区域外的行，所以互不干扰。
    if (horizUsable) {
        for (int y = y0; y < y1; ++y) {
            const std::uint8_t* lp = at(lx, y);
            const std::uint8_t* rp = at(rx, y);
            for (int x = x0; x < x1; ++x) {
                const float t = static_cast<float>(x - x0) / wDen;
                std::uint8_t* d = at(x, y);
                for (int c = 0; c < 4; ++c) {
                    d[c] = static_cast<std::uint8_t>(static_cast<float>(lp[c]) * (1.0f - t) +
                                                     static_cast<float>(rp[c]) * t + 0.5f);
                }
            }
        }
    }

    // ---- 第二遍：垂直插值（上边界外 ↔ 下边界外），与第一遍结果取平均 ----
    const bool hasTop = (y0 > 0);
    const bool hasBottom = (y1 < dh);
    const bool vertUsable = hasTop || hasBottom;
    const int ty = hasTop ? (y0 - 1) : (y1 < dh ? y1 : 0);
    const int by = hasBottom ? y1 : (y0 > 0 ? (y0 - 1) : 0);

    if (vertUsable) {
        for (int y = y0; y < y1; ++y) {
            const float t = static_cast<float>(y - y0) / hDen;
            const std::uint8_t* tp = at(0, ty);
            const std::uint8_t* bp = at(0, by);
            for (int x = x0; x < x1; ++x) {
                std::uint8_t* d = at(x, y);
                for (int c = 0; c < 4; ++c) {
                    const float vert = static_cast<float>(tp[x * 4 + c]) * (1.0f - t) +
                                       static_cast<float>(bp[x * 4 + c]) * t;
                    // 只有水平那遍跑过时才取平均；否则垂直值就是唯一来源。
                    d[c] = horizUsable
                               ? static_cast<std::uint8_t>((static_cast<float>(d[c]) + vert) * 0.5f +
                                                           0.5f)
                               : static_cast<std::uint8_t>(vert + 0.5f);
                }
            }
        }
    }

    // 两个方向都没有外圈可用（区域横跨整幅图）⇒ 没有参考可填，保持原样。
    // 这是诚实的退化行为：调用方不该把整幅图都当成"自己的界面"。
    return static_cast<long long>(w) * h;
}

}  // namespace aurora
