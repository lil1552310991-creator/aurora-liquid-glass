// M-15 液体玻璃按钮「液滴跟随」的单元测试。
//
// 纯逻辑、无 Android 依赖，编成 aarch64 直接在设备上跑：
//   clang++ --target=aarch64-linux-android28 -std=c++20 -O1 -UNDEBUG \
//           -I app/src/main/cpp -o LiquidButtonTest tests/LiquidButtonTest.cpp
// ★ 不加 -DNDEBUG。

#include "LiquidMorph.h"
#include "TouchHighlight.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ButtonTranslation;
using aurora::ComputeButtonMorph;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
constexpr float kDt = 1.0f / 60.0f;
}  // namespace

int main() {
    // ---------------------------------------------------- 1. ★ 位移饱和：初始导数 0.05、上限 = 短边
    //
    // 这是「液滴」手感的核心。少了 tanh 就退化成普通线性拖拽。
    {
        const float minDim = 200.0f;
        // 小位移处应近似线性，斜率 = 0.05
        const float d = 1.0f;
        const float slope = ButtonTranslation(d, minDim) / d;
        assert(near(slope, 0.05f, 1e-3f));

        // 位移 0 → 0
        assert(near(ButtonTranslation(0.0f, minDim), 0.0f));
        // 负方向对称
        assert(near(ButtonTranslation(-40.0f, minDim), -ButtonTranslation(40.0f, minDim)));

        // ★ 饱和：无论位移多大，绝对值**不超过**短边。
        //   注意必须写 `<=` 而不是 `<` —— float 的 tanh 在 |x|>~9 时就精确等于 1.0f，
        //   所以极大位移下结果**恰好**等于 minDim（这个边界被测试抓过一次）。
        for (float big : {1000.0f, 10000.0f, 1e6f}) {
            assert(std::abs(ButtonTranslation(big, minDim)) <= minDim);
            assert(std::abs(ButtonTranslation(-big, minDim)) <= minDim);
        }
        assert(std::abs(ButtonTranslation(1e6f, minDim)) == minDim);   // 已经饱和到边界
        // 已经非常接近上限
        assert(ButtonTranslation(1e6f, minDim) > minDim * 0.999f);

        // 单调递增
        float prev = -1.0f;
        for (int i = 0; i < 50; ++i) {
            const float v = ButtonTranslation(static_cast<float>(i) * 10.0f, minDim);
            assert(v > prev);
            prev = v;
        }
        // 除零保护
        assert(near(ButtonTranslation(50.0f, 0.0f), 0.0f));
    }

    // ---------------------------------------------------- 2. 静止时（无按压、无位移）：不变形
    {
        const auto m = ComputeButtonMorph(0.0f, 0.0f, 0.0f, 300.0f, 80.0f, 8.0f);
        assert(near(m.translationX, 0.0f));
        assert(near(m.translationY, 0.0f));
        assert(near(m.scaleX, 1.0f));
        assert(near(m.scaleY, 1.0f));
    }

    // ---------------------------------------------------- 3. 按压放大：1 + 4dp/height
    {
        // 高 80px、4dp = 8px → 放大到 1 + 8/80 = 1.1
        const auto m = ComputeButtonMorph(1.0f, 0.0f, 0.0f, 300.0f, 80.0f, 8.0f);
        assert(near(m.scaleX, 1.1f));
        assert(near(m.scaleY, 1.1f));

        // ★ 越矮的按钮放大比例越大（分母是自身高度）
        const auto tall = ComputeButtonMorph(1.0f, 0.0f, 0.0f, 300.0f, 160.0f, 8.0f);
        assert(near(tall.scaleX, 1.05f));
        assert(m.scaleX > tall.scaleX);

        // 半按 → 一半
        const auto half = ComputeButtonMorph(0.5f, 0.0f, 0.0f, 300.0f, 80.0f, 8.0f);
        assert(near(half.scaleX, 1.05f));
    }

    // ---------------------------------------------------- 4. 拖拽拉伸的方向性
    //
    // 纯水平拖 → X 变大、Y 几乎不变（因为 sin(0)=0）
    {
        const float w = 300.0f, h = 80.0f, dp4 = 8.0f;
        const auto m = ComputeButtonMorph(0.0f, 60.0f, 0.0f, w, h, dp4);
        // maxDragScale = 8/80 = 0.1；maxDim = 300
        // scaleX = 1 + 0.1 * |1*60/300| * min(300/80,1)=1  → 1 + 0.1*0.2 = 1.02
        assert(near(m.scaleX, 1.02f));
        // scaleY = 1 + 0.1 * |sin(0)*0/300| * min(80/300,1) = 1
        assert(near(m.scaleY, 1.0f));
        assert(m.scaleX > m.scaleY);

        // 纯竖直拖 → Y 变大、X 不变
        const auto v = ComputeButtonMorph(0.0f, 0.0f, 60.0f, w, h, dp4);
        assert(near(v.scaleX, 1.0f));
        assert(v.scaleY > 1.0f);
        // heightBias = min(80/300, 1) = 0.2667 → scaleY = 1 + 0.1*(60/300)*0.2667
        assert(near(v.scaleY, 1.0f + 0.1f * 0.2f * (80.0f / 300.0f), 1e-4f));
    }

    // ---------------------------------------------------- 5. ★ 宽高比修正：扁按钮压制竖直拉伸
    {
        const float dp4 = 8.0f;
        // 扁按钮 400x80
        const auto wide = ComputeButtonMorph(0.0f, 0.0f, 60.0f, 400.0f, 80.0f, dp4);
        // 方按钮 120x120
        const auto square = ComputeButtonMorph(0.0f, 0.0f, 60.0f, 120.0f, 120.0f, dp4);
        // 同样的竖直位移，方按钮的 scaleY 增量应更大（heightBias = 1）
        assert((square.scaleY - 1.0f) > (wide.scaleY - 1.0f));
        // 方按钮：maxDragScale = 8/120，heightBias = 1，|sin(90°)*60/120| = 0.5
        assert(near(square.scaleY, 1.0f + (8.0f / 120.0f) * 0.5f, 1e-4f));
    }

    // ---------------------------------------------------- 6. 与 M-14 串联：真的接得上
    //
    // 正确用法是 TouchHighlight 先跑、再把它的 offset 喂给 M-15。
    // 这里演一遍完整链路，确认按下 → 拖动 → 松手全过程数值都合理。
    {
        aurora::TouchHighlight hl;
        const float w = 300.0f, h = 80.0f, dp4 = 8.0f;

        hl.onDragStart(100.0f, 100.0f);
        for (int i = 0; i < 40; ++i) hl.tick(kDt);
        assert(hl.pressProgress() > 0.9f);

        // 按下不动：offset = 0 → 只有按压放大
        auto m0 = ComputeButtonMorph(hl.pressProgress(), hl.offsetX(), hl.offsetY(), w, h, dp4);
        assert(m0.scaleX > 1.05f);
        assert(near(m0.translationX, 0.0f));

        // 拖走：offset 变大 → 出现位移与拉伸
        hl.onDrag(160.0f, 100.0f);
        hl.tick(kDt);
        assert(near(hl.offsetX(), 60.0f));
        const auto m1 = ComputeButtonMorph(hl.pressProgress(), hl.offsetX(), hl.offsetY(), w, h, dp4);
        assert(m1.translationX > 0.0f);
        // 位移远小于手指位移（只有 ~5% 起步）
        assert(m1.translationX < hl.offsetX() * 0.2f);
        assert(m1.scaleX > m0.scaleX);   // 被拉长了

        // 松手后位置弹簧回起点 → offset 收敛到 0 → 形变退回纯按压态
        hl.onDragEnd();
        for (int i = 0; i < 400; ++i) hl.tick(kDt);
        assert(std::abs(hl.offsetX()) < 0.5f);
        assert(hl.pressProgress() < 0.01f);
        const auto m2 = ComputeButtonMorph(hl.pressProgress(), hl.offsetX(), hl.offsetY(), w, h, dp4);
        assert(near(m2.translationX, 0.0f, 0.6f));
        assert(near(m2.scaleX, 1.0f, 0.01f));   // 完全复原
    }

    // ---------------------------------------------------- 7. 退化输入不崩
    {
        const auto z = ComputeButtonMorph(0.5f, 10.0f, 10.0f, 0.0f, 80.0f, 8.0f);
        assert(near(z.scaleX, 1.0f));   // 宽为 0 → 直接返回默认值
        const auto z2 = ComputeButtonMorph(0.5f, 10.0f, 10.0f, 300.0f, 0.0f, 8.0f);
        assert(near(z2.scaleY, 1.0f));
        // 极端位移也不出 NaN/Inf
        const auto big = ComputeButtonMorph(1.0f, 1e7f, -1e7f, 300.0f, 80.0f, 8.0f);
        assert(std::isfinite(big.translationX) && std::isfinite(big.translationY));
        assert(std::isfinite(big.scaleX) && std::isfinite(big.scaleY));
    }

    // ---------------------------------------------------- 8. 静止玻璃配方常量
    {
        assert(near(aurora::kButtonBlurDp, 2.0f));
        assert(near(aurora::kButtonLensHeightDp, 12.0f));
        assert(near(aurora::kButtonLensAmountDp, 24.0f));
        assert(near(aurora::kButtonDragDerivative, 0.05f));
        assert(near(aurora::kButtonTintOverlayAlpha, 0.75f));
    }

    std::printf("LiquidButtonTest: 全部断言通过（8 组）\n");
    return 0;
}
