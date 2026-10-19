// 采集帧后处理管线的单元测试。
//
// 纯逻辑、零依赖，PC 上跑（tests/run_host_tests.ps1）。
//
// ★ 这个测试存在的唯一理由：**管线顺序错了会以非常隐蔽的方式表现出来，
//   而我已经错过两次** ——
//     ① 抹除插在行翻转之后 ⇒ 区域垂直镜像，屏幕上边缘残留自己的图层
//     ② 漏掉行翻转       ⇒ 玻璃里折射出上下镜像的桌面
//   两条都只能靠"看着有点怪"发现。这里的核心断言是：
//     **在最终（自下而上）的缓冲里，被抹除的区域必须落在它该在的位置。**
//   这一条同时覆盖①②。
//
// 构造思路：用"每像素唯一"的图（像素值由坐标决定），这样任何错位都能被识别；
// 再配合"区域外一个像素都不许动"的断言，翻转漏做/做反也会被抓到。

#include "CapturePipeline.h"

#include <cassert>
#include <cstdio>
#include <vector>

using aurora::CapturePostParams;
using aurora::PostProcessCaptureFrame;

namespace {

// 每像素唯一：R = x 低 8 位, G = y 低 8 位, B = (x^y) 低 8 位, A = 255
std::vector<std::uint8_t> MakeImage(int w, int h) {
    std::vector<std::uint8_t> img(static_cast<size_t>(w) * h * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = img.data() + (static_cast<size_t>(y) * w + x) * 4u;
            p[0] = static_cast<std::uint8_t>(x & 0xFF);
            p[1] = static_cast<std::uint8_t>(y & 0xFF);
            p[2] = static_cast<std::uint8_t>((x ^ y) & 0xFF);
            p[3] = 255;
        }
    }
    return img;
}

std::uint8_t* At(std::vector<std::uint8_t>& v, int w, int x, int y) {
    return v.data() + (static_cast<size_t>(y) * w + x) * 4u;
}
const std::uint8_t* At(const std::vector<std::uint8_t>& v, int w, int x, int y) {
    return v.data() + (static_cast<size_t>(y) * w + x) * 4u;
}
bool Same(const std::uint8_t* a, const std::uint8_t* b) {
    for (int c = 0; c < 4; ++c)
        if (a[c] != b[c]) return false;
    return true;
}

}  // namespace

int main() {
    constexpr int SW = 64, SH = 48;   // 原始尺寸

    // ===================================================== 1. 降采样正确（ds=1 恒等）
    {
        auto src = MakeImage(SW, SH);
        CapturePostParams p;
        p.downscale = 1;
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == SW && dh == SH);
        assert(out.size() == src.size());

        // ds=1 且不抹除 ⇒ 只剩"行翻转"。所以 out 的第 y 行 == src 的第 (SH-1-y) 行。
        // 这条断言就是"翻转确实做了、且只做了一次"的证据。
        for (int y = 0; y < SH; ++y) {
            const std::uint8_t* a = At(out, dw, 0, y);
            const std::uint8_t* b = At(src, SW, 0, SH - 1 - y);
            assert(std::memcmp(a, b, static_cast<size_t>(SW) * 4u) == 0);
        }
    }

    // ===================================================== 2. ★ 核心：抹除区域在最终缓冲里的落点
    //
    // 这是抓「抹除插在翻转之后」那个 bug 的断言。
    //
    // 设原始 64x48、ds=2 ⇒ 输出 32x24（自下而上）。
    // 抹除屏幕左上角 (0,0)-(32,24) ⇒ 降采样后是输出坐标 (0,0)-(16,12)，**自上而下**。
    // 翻转之后，那块应该落在**输出的下半部分**：行 [24-12, 24) = [12,24)、列 [0,16)。
    {
        auto src = MakeImage(SW, SH);
        const auto srcBefore = src;
        CapturePostParams p;
        p.downscale = 2;
        p.eraseX = 0; p.eraseY = 0; p.eraseW = 32; p.eraseH = 24;   // 屏幕左上四分之一
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == 32 && dh == 24);

        // 先算出"没抹除时"的基准（同一个图、同一个 ds、不抹除）
        CapturePostParams q;
        q.downscale = 2;
        std::vector<std::uint8_t> ref;
        int rw = 0, rh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, q, &ref, &rw, &rh));
        assert(rw == dw && rh == dh);

        // ① 下半部分（行 12..24、列 0..16）**必须**被改过（不能与基准相同）
        int survivors = 0;
        for (int y = 12; y < 24; ++y)
            for (int x = 0; x < 16; ++x)
                if (Same(At(out, dw, x, y), At(ref, dw, x, y))) ++survivors;
        assert(survivors == 0);   // ★ 一个都不许剩 —— 剩了就是"没抹干净"

        // ② ★★ 其余地方**一个像素都不许动**。
        //    这一条会抓住"区域被垂直镜像"：如果抹除插在翻转之后，
        //    被改的就会是上半部分 [0,12) 而不是下半部分 [12,24)，
        //    于是这里会统计到大量 changed。
        int changed = 0;
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                const bool inErased = (y >= 12 && y < 24 && x >= 0 && x < 16);
                if (inErased) continue;
                if (!Same(At(out, dw, x, y), At(ref, dw, x, y))) ++changed;
            }
        assert(changed == 0);

        // ③ 源缓冲不许被改（纯函数不该有副作用）
        assert(src == srcBefore);
    }

    // ===================================================== 3. ★ 翻转确实生效（非对称图）
    //
    // 用一个"上黑下白"的图：ds=1、不抹除 ⇒ 输出应该是"上白下黑"。
    // 漏做翻转会得到"上黑下白"，做两次会回到"上黑下白" —— 两种错法都能抓到。
    {
        std::vector<std::uint8_t> src(static_cast<size_t>(SW) * SH * 4u, 0);
        for (int y = 0; y < SH; ++y) {
            for (int x = 0; x < SW; ++x) {
                std::uint8_t* p = At(src, SW, x, y);
                const std::uint8_t v = (y < SH / 2) ? 0 : 255;   // 上半黑、下半白
                p[0] = p[1] = p[2] = v;
                p[3] = 255;
            }
        }
        CapturePostParams p;
        p.downscale = 1;
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));

        // 输出第 0 行应来自源的最后一行 ⇒ 白
        assert(At(out, dw, 0, 0)[0] == 255);
        // 输出最后一行应来自源的第 0 行 ⇒ 黑
        assert(At(out, dw, 0, dh - 1)[0] == 0);
        // 中点两侧
        assert(At(out, dw, 0, dh / 2 - 1)[0] == 255);
        assert(At(out, dw, 0, dh / 2)[0] == 0);
    }

    // ===================================================== 4. 抹除区域贴右下角
    {
        auto src = MakeImage(SW, SH);
        CapturePostParams p;
        p.downscale = 2;
        // 屏幕右下角一块
        p.eraseX = SW - 16; p.eraseY = SH - 12; p.eraseW = 16; p.eraseH = 12;
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == 32 && dh == 24);

        CapturePostParams q;
        q.downscale = 2;
        std::vector<std::uint8_t> ref;
        int rw = 0, rh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, q, &ref, &rw, &rh));

        // 屏幕右下 ⇒ 翻转后落在**输出的右上**（行镜像、列不镜像）。
        //
        // ⚠️ 我第一版把这里写成"输出的左上"（x 也镜像），被这条断言抓出来了：
        //    翻转只翻**行**，列是不动的。擦除区在屏幕右侧 ⇒ 输出里仍在右侧。
        const int fx0 = 24, fx1 = 32, fy0 = 0, fy1 = 6;   // 输出坐标（自下而上）
        int survivors = 0;
        for (int y = fy0; y < fy1; ++y)
            for (int x = fx0; x < fx1; ++x)
                if (Same(At(out, dw, x, y), At(ref, dw, x, y))) ++survivors;
        assert(survivors == 0);
        // 其余不动
        int changed = 0;
        for (int y = 0; y < dh; ++y)
            for (int x = 0; x < dw; ++x) {
                if (y >= fy0 && y < fy1 && x >= fx0 && x < fx1) continue;
                if (!Same(At(out, dw, x, y), At(ref, dw, x, y))) ++changed;
            }
        assert(changed == 0);
    }

    // ===================================================== 5. 不抹除时输出 = 降采样 + 翻转
    {
        auto src = MakeImage(SW, SH);
        CapturePostParams p;
        p.downscale = 2;
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == 32 && dh == 24);
        // 输出 (x,y) 对应源块 (2x, 2y)..(2x+1,2y+1)，但**行要翻**：
        // 输出第 y 行来自源的 2*(dh-1-y) 行起
        const int sy = 2 * (dh - 1 - 0);
        const std::uint8_t* sp = At(src, SW, 0, sy);
        const std::uint8_t* op = At(out, dw, 0, 0);
        assert(op[0] == sp[0]);   // R = x，块内 x 都是 0
        assert(op[1] == sp[1]);   // G = y
    }

    // ===================================================== 6. 非法参数 / 边界不崩
    {
        std::vector<std::uint8_t> out;
        int dw = 0, dh = 0;
        CapturePostParams p;
        assert(!PostProcessCaptureFrame(nullptr, SW, SH, p, &out, &dw, &dh));
        assert(!PostProcessCaptureFrame(nullptr, 0, 0, p, &out, &dw, &dh));
        auto src = MakeImage(SW, SH);
        assert(!PostProcessCaptureFrame(src.data(), 0, SH, p, &out, &dw, &dh));
        assert(!PostProcessCaptureFrame(src.data(), SW, -5, p, &out, &dw, &dh));

        // downscale 非法值被夹到 >=1
        p.downscale = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == SW && dh == SH);
        p.downscale = -3;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &out, &dw, &dh));
        assert(dw == SW && dh == SH);

        // 抹除区域完全在画面外 ⇒ 等同于不抹
        p.downscale = 2;
        p.eraseX = 10000; p.eraseY = 10000; p.eraseW = 10; p.eraseH = 10;
        std::vector<std::uint8_t> a;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, p, &a, &dw, &dh));
        CapturePostParams q;
        q.downscale = 2;
        std::vector<std::uint8_t> b;
        int bw = 0, bh = 0;
        assert(PostProcessCaptureFrame(src.data(), SW, SH, q, &b, &bw, &bh));
        assert(a == b);
    }

    // ===================================================== 7. 输出尺寸 = 输入/降采样（向下取整）
    {
        auto src = MakeImage(100, 60);
        for (int ds : {1, 2, 3, 4, 7}) {
            CapturePostParams p;
            p.downscale = ds;
            std::vector<std::uint8_t> out;
            int dw = 0, dh = 0;
            assert(PostProcessCaptureFrame(src.data(), 100, 60, p, &out, &dw, &dh));
            assert(dw == std::max(1, 100 / ds));
            assert(dh == std::max(1, 60 / ds));
            assert(out.size() == static_cast<size_t>(dw) * dh * 4u);
        }
    }

    std::printf("CapturePipelineTest: 全部断言通过（7 组）\n");
    return 0;
}
