// 防自反馈「抹除 + 环填充」的单元测试。
//
// 纯逻辑、零依赖，PC 上跑（tests/run_host_tests.ps1）。
//
// 为什么值得单测：这段代码在真机上画错了只表现为「玻璃边缘有一条暗带」，
// 是很主观的现象、极难判断对错。而它要保证的性质其实非常明确：
//   ① 区域内的原始像素**必须一个都不剩**（否则残留自己的玻璃 ⇒ 仍然反馈）
//   ② 区域外的像素**必须一个都不动**（否则会破坏桌面反射的正确性）
//   ③ 区域贴边 / 覆盖全图 / 空矩形都不能崩

#include "SelfFeedbackErase.h"

#include <cassert>
#include <cstdio>
#include <vector>

using aurora::EraseRectWithRingFill;

namespace {

// 造一张每像素可辨识的测试图：像素值 = 由坐标决定的唯一值。
std::vector<std::uint8_t> MakeImage(int w, int h) {
    std::vector<std::uint8_t> img(static_cast<size_t>(w) * h * 4u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = img.data() + (static_cast<size_t>(y) * w + x) * 4u;
            p[0] = static_cast<std::uint8_t>(x * 7 + 1);
            p[1] = static_cast<std::uint8_t>(y * 11 + 3);
            p[2] = static_cast<std::uint8_t>(x + y + 17);
            p[3] = 255;
        }
    }
    return img;
}

std::uint8_t* At(std::vector<std::uint8_t>& img, int w, int x, int y) {
    return img.data() + (static_cast<size_t>(y) * w + x) * 4u;
}

bool Same(const std::uint8_t* a, const std::uint8_t* b) {
    for (int c = 0; c < 4; ++c)
        if (a[c] != b[c]) return false;
    return true;
}

}  // namespace

int main() {
    constexpr int W = 64, H = 48;

    // ---------------------------------------------- 1. 区域内一个原始像素都不剩
    {
        auto img = MakeImage(W, H);
        const auto before = img;
        const int x0 = 10, y0 = 8, x1 = 40, y1 = 30;

        const long long n = EraseRectWithRingFill(img.data(), W, H, x0, y0, x1, y1);
        assert(n == static_cast<long long>(x1 - x0) * (y1 - y0));

        // ★ 关键断言：区域内不得有任何像素与"原始图"完全相同。
        //   只要还剩一个，那块就还是我们自己的玻璃 ⇒ 自反馈没有断干净。
        int survivors = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                if (Same(At(img, W, x, y), At(const_cast<std::vector<std::uint8_t>&>(before), W, x, y)))
                    ++survivors;
            }
        }
        assert(survivors == 0);
    }

    // ---------------------------------------------- 2. ★ 区域外一个像素都不动
    {
        auto img = MakeImage(W, H);
        const auto before = img;
        const int x0 = 10, y0 = 8, x1 = 40, y1 = 30;

        EraseRectWithRingFill(img.data(), W, H, x0, y0, x1, y1);

        int changed = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;   // 区域内跳过
                if (!Same(At(img, W, x, y), At(const_cast<std::vector<std::uint8_t>&>(before), W, x, y)))
                    ++changed;
            }
        }
        assert(changed == 0);
    }

    // ---------------------------------------------- 3. 填充值确实来自"外圈"
    //
    // 造一张纯色图：外圈是什么色，填出来就该是什么色（不该是黑）。
    {
        auto img = MakeImage(W, H);
        // 全图刷成同一个颜色
        for (size_t i = 0; i < img.size(); i += 4) {
            img[i] = 90; img[i + 1] = 140; img[i + 2] = 200; img[i + 3] = 255;
        }
        EraseRectWithRingFill(img.data(), W, H, 5, 5, 30, 25);
        for (int y = 5; y < 25; ++y) {
            for (int x = 5; x < 30; ++x) {
                const std::uint8_t* p = At(img, W, x, y);
                assert(p[0] == 90 && p[1] == 140 && p[2] == 200 && p[3] == 255);
            }
        }
    }

    // ---------------------------------------------- 4. 区域贴左上角（外圈不存在）
    {
        auto img = MakeImage(W, H);
        const long long n = EraseRectWithRingFill(img.data(), W, H, 0, 0, 20, 15);
        assert(n == 20 * 15);
        // 不崩、且区域内不再是原值
        int survivors = 0;
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < 20; ++x) {
                const std::uint8_t* p = At(img, W, x, y);
                if (p[0] == static_cast<std::uint8_t>(x * 7 + 1)) ++survivors;
            }
        assert(survivors == 0);
    }

    // ---------------------------------------------- 5. 区域贴右下角
    {
        auto img = MakeImage(W, H);
        const long long n = EraseRectWithRingFill(img.data(), W, H, W - 10, H - 6, W, H);
        assert(n == 10 * 6);
    }

    // ---------------------------------------------- 6. 区域覆盖整图
    //
    // ★ 退化行为：两个方向都没有"区域外"的参考像素可用 ⇒ 没有东西可填，
    //   保持原样。这是诚实的 —— 调用方不该把整幅图都当成"自己的界面"。
    //   （实际用例是 (0,0)-(800,2160)，右下两侧都还有外圈，所以不走这条分支。）
    {
        auto img = MakeImage(W, H);
        const auto before = img;
        const long long n = EraseRectWithRingFill(img.data(), W, H, 0, 0, W, H);
        assert(n == static_cast<long long>(W) * H);
        assert(img == before);   // 无参考可填 ⇒ 一个字节都没动
    }

    // ---------------------------------------------- 7. 空矩形 / 越界矩形不崩、不改数据
    {
        auto img = MakeImage(W, H);
        const auto before = img;
        assert(EraseRectWithRingFill(img.data(), W, H, 10, 10, 10, 20) == 0);   // 宽 0
        assert(EraseRectWithRingFill(img.data(), W, H, 10, 10, 20, 10) == 0);   // 高 0
        assert(EraseRectWithRingFill(img.data(), W, H, 30, 30, 20, 20) == 0);   // 反的
        assert(EraseRectWithRingFill(img.data(), W, H, -50, -50, -10, -10) == 0);
        assert(EraseRectWithRingFill(nullptr, W, H, 0, 0, 10, 10) == 0);
        assert(EraseRectWithRingFill(img.data(), 0, 0, 0, 0, 10, 10) == 0);
        assert(img == before);   // 一个字节都没动

        // 越界矩形被夹进来，不越界写内存
        auto img2 = MakeImage(W, H);
        const long long n = EraseRectWithRingFill(img2.data(), W, H, -100, -100, 100, 100);
        assert(n == static_cast<long long>(W) * H);
    }

    // ---------------------------------------------- 8. 幂等性：连抹两次结果一致
    //
    // 这条保证"连续多帧都在抹同一块"不会越抹越偏 —— 真机上这是常态。
    {
        auto img = MakeImage(W, H);
        EraseRectWithRingFill(img.data(), W, H, 10, 8, 40, 30);
        auto once = img;
        EraseRectWithRingFill(img.data(), W, H, 10, 8, 40, 30);
        assert(img == once);
    }

    std::printf("SelfFeedbackEraseTest: 全部断言通过（8 组）\n");
    return 0;
}
