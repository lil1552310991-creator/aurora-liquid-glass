#pragma once

// ===========================================================================
// 采集帧的后处理管线（纯函数，可离线单测）
//
//   screencap 原始 RGBA（自上而下，sw x sh）
//     → ① 盒式降采样            → dw x dh，仍自上而下
//     → ② 抹除界面区域 + 环填充  → 按**屏幕坐标**抹（所以必须在翻转之前）
//     → ③ 行翻转                → 变成自下而上，交给 GPU
//
// ★★ 为什么要把这三步抽成一个函数：**顺序错了会以非常隐蔽的方式表现出来，
//    而我已经错过两次**：
//
//   ① 把「抹除」插到「行翻转」之后 ⇒ 缓冲已自下而上，抹除却用自上而下的屏幕
//      坐标 ⇒ **区域被垂直镜像**：实际抹掉的是屏幕 y∈[216,2376)，而
//      y∈[0,216)（二级窗口标题栏那一段）没抹，那里仍残留自己的图层继续反馈。
//      真机现象是「上面还残留一点镜像」，极易误判成"根本没修好"。
//
//   ② 「行翻转」这一步本身也不能省 —— 不翻的话玻璃里折射出来的是**上下镜像**
//      的桌面（`GlesBackend.cpp:406-408` 写明翻转是上游生产者的责任）。
//
//   两条都只能靠"看着有点怪"来发现。抽成纯函数后就能逐像素断言
//   （tests/CapturePipelineTest.cpp）：**在最终缓冲里，被抹除的区域必须落在
//   它该在的位置** —— 这一条断言同时覆盖了①②两个坑。
// ===========================================================================

#include "SelfFeedbackErase.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace aurora {

struct CapturePostParams final {
    int downscale = 2;                 // 盒式降采样倍数（>=1）
    int eraseX = 0;                    // 抹除区域，**原始分辨率的屏幕坐标**
    int eraseY = 0;
    int eraseW = 0;                    // 0 表示不抹
    int eraseH = 0;
};

// 返回 false 表示参数非法（尺寸 <= 0 等）。
//
// src   : 原始 RGBA8888，sw x sh，**自上而下**（screencap raw 就是）
// out   : 输出缓冲，dw x dh，**自下而上**（给 GPU 的投影纹理用）
// outW/H: 输出尺寸
inline bool PostProcessCaptureFrame(const std::uint8_t* src, int sw, int sh,
                                    const CapturePostParams& p, std::vector<std::uint8_t>* out,
                                    int* outW, int* outH) {
    if (src == nullptr || out == nullptr || sw <= 0 || sh <= 0) return false;
    const int ds = std::max(1, p.downscale);
    const int dw = std::max(1, sw / ds);
    const int dh = std::max(1, sh / ds);

    // ---- ① 盒式降采样（仍自上而下）----
    out->assign(static_cast<size_t>(dw) * dh * 4u, 0);
    if (ds == 1) {
        std::memcpy(out->data(), src, out->size());
    } else {
        for (int y = 0; y < dh; ++y) {
            for (int x = 0; x < dw; ++x) {
                int r = 0, g = 0, b = 0, a = 0, n = 0;
                for (int dy = 0; dy < ds; ++dy) {
                    const int sy = y * ds + dy;
                    if (sy >= sh) break;
                    for (int dx = 0; dx < ds; ++dx) {
                        const int sx = x * ds + dx;
                        if (sx >= sw) break;
                        const std::uint8_t* q = src + (static_cast<size_t>(sy) * sw + sx) * 4u;
                        r += q[0]; g += q[1]; b += q[2]; a += q[3]; ++n;
                    }
                }
                if (n == 0) n = 1;
                std::uint8_t* d = out->data() + (static_cast<size_t>(y) * dw + x) * 4u;
                d[0] = static_cast<std::uint8_t>(r / n);
                d[1] = static_cast<std::uint8_t>(g / n);
                d[2] = static_cast<std::uint8_t>(b / n);
                d[3] = static_cast<std::uint8_t>(a / n);
            }
        }
    }

    // ---- ② 抹除（★ 必须在③之前：这里用的是自上而下的屏幕坐标）----
    if (p.eraseW > 0 && p.eraseH > 0) {
        const int ex0 = std::max(0, p.eraseX / ds);
        const int ey0 = std::max(0, p.eraseY / ds);
        const int ex1 = std::min(dw, (p.eraseX + p.eraseW) / ds);
        const int ey1 = std::min(dh, (p.eraseY + p.eraseH) / ds);
        EraseRectWithRingFill(out->data(), dw, dh, ex0, ey0, ex1, ey1);
    }

    // ---- ③ 行翻转（之后缓冲变成自下而上）----
    {
        const size_t stride = static_cast<size_t>(dw) * 4u;
        std::vector<std::uint8_t> tmp(stride);
        for (int y = 0; y < dh / 2; ++y) {
            std::uint8_t* rowA = out->data() + static_cast<size_t>(y) * stride;
            std::uint8_t* rowB = out->data() + static_cast<size_t>(dh - 1 - y) * stride;
            std::memcpy(tmp.data(), rowA, stride);
            std::memcpy(rowA, rowB, stride);
            std::memcpy(rowB, tmp.data(), stride);
        }
    }

    if (outW) *outW = dw;
    if (outH) *outH = dh;
    return true;
}

}  // namespace aurora
