// M-11 / M-12「液滴铺开」形变的单元测试。
//
// 纯逻辑、无 Android 依赖，可编成 aarch64 直接在设备上跑：
//   clang++ --target=aarch64-linux-android28 -std=c++20 -O1 -UNDEBUG \
//           -I app/src/main/cpp -o LiquidMorphTest tests/LiquidMorphTest.cpp
//
// ★ 必须**不加 -DNDEBUG**，否则 assert 失效。
//
// 重点：把源码里的**两个差异**（scaleY 上限、速度除数）和**两条易错写法**钉死：
//   1. 铺开是 `lerp(2/3, span, pp)`，不是从 0 开始
//   2. 速度拉伸是 `sx /= (1 - clamp(...))`，**除法**，不是乘法

#include "LiquidMorph.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ComputeDropletMorph;
using aurora::DropletKind;

namespace {
bool near(float a, float b, float eps = 1e-5f) { return std::abs(a - b) < eps; }
}  // namespace

int main() {
    // pxPerDp = 1 简化；blur 的 dp→px 换算本身另有测试意义，但这里只关心系数
    constexpr float kPx = 1.0f;

    // ------------------------------------------------------- 1. 未按下：完全收扁
    {
        const auto t = ComputeDropletMorph(DropletKind::Toggle, 0.0f, 0.0f, kPx);
        assert(near(t.scaleX, 2.0f / 3.0f));   // 起始 scaleX = 2/3（**不是 0**）
        assert(near(t.scaleY, 0.0f));          // 起始 scaleY = 0（完全压扁）
        assert(near(t.blurPx, 8.0f));          // 未按下时最模糊 = 8dp

        const auto s = ComputeDropletMorph(DropletKind::Slider, 0.0f, 0.0f, kPx);
        assert(near(s.scaleX, 2.0f / 3.0f));   // 两种组件的起始值相同
        assert(near(s.scaleY, 0.0f));
        assert(near(s.blurPx, 8.0f));
    }

    // ------------------------------------------------------- 2. 完全按下：铺开到各自的上限
    {
        const auto t = ComputeDropletMorph(DropletKind::Toggle, 1.0f, 0.0f, kPx);
        assert(near(t.scaleX, 0.75f));   // ★ Toggle 上限 0.75
        assert(near(t.scaleY, 0.75f));
        assert(near(t.blurPx, 0.0f));    // 完全按下 = 完全清晰

        const auto s = ComputeDropletMorph(DropletKind::Slider, 1.0f, 0.0f, kPx);
        assert(near(s.scaleX, 1.0f));    // ★ Slider 上限 1.0 —— 这是两者唯一的数值差异之一
        assert(near(s.scaleY, 1.0f));
        assert(near(s.blurPx, 0.0f));
    }

    // ------------------------------------------------------- 3. 中点：线性插值
    {
        const auto t = ComputeDropletMorph(DropletKind::Toggle, 0.5f, 0.0f, kPx);
        // lerp(2/3, 0.75, 0.5) = (0.666667 + 0.75) / 2
        assert(near(t.scaleX, (2.0f / 3.0f + 0.75f) * 0.5f));
        assert(near(t.scaleY, 0.375f));   // lerp(0, 0.75, 0.5)
        assert(near(t.blurPx, 4.0f));     // 8 * 0.5

        const auto s = ComputeDropletMorph(DropletKind::Slider, 0.5f, 0.0f, kPx);
        assert(near(s.scaleY, 0.5f));     // lerp(0, 1, 0.5)
    }

    // ------------------------------------------------------- 4. ★ 速度拉伸的除法语义
    //
    // 源码是 `scaleX /= 1f - (velocity * 0.75f).fastCoerceIn(-0.2f, 0.2f)`。
    // 写成乘法会得到相反的效果 —— 这条断言就是为了钉死方向。
    {
        const float v = 5.0f;   // Toggle 的除数是 50 → v_norm = 0.1
        const auto t = ComputeDropletMorph(DropletKind::Toggle, 1.0f, v, kPx);
        // v = 5/50 = 0.1；t = 0.1*0.75 = 0.075；1-t = 0.925
        // scaleX = 0.75 / 0.925 ≈ 0.8108（**变大**）
        assert(near(t.scaleX, 0.75f / 0.925f, 1e-4f));
        assert(t.scaleX > 0.75f);   // ★ 正速度让 X 变长（拉伸），不是变短
        // scaleY = 0.75 * (1 - 0.1*0.25) = 0.75 * 0.975
        assert(near(t.scaleY, 0.75f * 0.975f, 1e-4f));
        assert(t.scaleY < 0.75f);   // Y 反而被压缩 —— 这就是"液体被拉长"的观感
    }

    // ------------------------------------------------------- 5. ★ 速度除数的差异（50 vs 10）
    //
    // 同一个速度值，Slider 的反应应当比 Toggle 强 5 倍。
    {
        const float v = 10.0f;
        const auto t = ComputeDropletMorph(DropletKind::Toggle, 1.0f, v, kPx);  // 10/50 = 0.2
        const auto s = ComputeDropletMorph(DropletKind::Slider, 1.0f, v, kPx);  // 10/10 = 1.0
        // Toggle：t = 0.2*0.75 = 0.15 → scaleX = 0.75/0.85
        assert(near(t.scaleX, 0.75f / 0.85f, 1e-4f));
        // Slider：t = 1.0*0.75 = 0.75 → 被夹到 0.2 → scaleX = 1.0/0.8
        assert(near(s.scaleX, 1.0f / 0.8f, 1e-4f));
        // 相对变化量：Slider 明显更大
        assert((s.scaleX - 1.0f) > (t.scaleX - 0.75f));
    }

    // ------------------------------------------------------- 6. ★ 夹取到 ±0.2，永不发散
    //
    // 极端速度下分母必须落在 [0.8, 1.2]，否则会出现翻转/爆掉。
    {
        for (float v : {-1e6f, -1000.0f, -50.0f, 0.0f, 50.0f, 1000.0f, 1e6f}) {
            for (auto kind : {DropletKind::Toggle, DropletKind::Slider}) {
                const auto m = ComputeDropletMorph(kind, 1.0f, v, kPx);
                assert(std::isfinite(m.scaleX));
                assert(std::isfinite(m.scaleY));
                assert(m.scaleX > 0.0f);   // 不能翻负
                assert(m.scaleY >= 0.0f);
                // 分母 ∈ [0.8,1.2] ⇒ scaleX 最多放大 1/0.8 = 1.25 倍
                assert(m.scaleX <= 1.0f / 0.8f + 1e-4f);
                assert(m.scaleX >= 0.75f / 1.2f - 1e-4f);
            }
        }
    }

    // ------------------------------------------------------- 7. dp→px 换算与单调性
    {
        const auto m2 = ComputeDropletMorph(DropletKind::Toggle, 0.25f, 0.0f, 2.0f);
        assert(near(m2.blurPx, 8.0f * 2.0f * 0.75f));   // 8dp * 2px/dp * (1-0.25)

        // blur 随 pressProgress 单调下降
        float prev = 1e9f;
        for (int i = 0; i <= 10; ++i) {
            const auto m = ComputeDropletMorph(DropletKind::Slider, static_cast<float>(i) / 10.0f,
                                               0.0f, kPx);
            assert(m.blurPx <= prev + 1e-6f);
            prev = m.blurPx;
        }
        assert(near(prev, 0.0f));   // i=10 → pp=1 → blur=0
    }

    // ------------------------------------------------------- 8. 边光常量对齐 ytbl（不是 Kyant 库默认）
    {
        // ytbl/GlassConfig.h:84-85
        assert(near(aurora::kEdgeAmbientWidthDp, 0.33333334f, 1e-6f));
        assert(near(aurora::kEdgeAmbientSoftnessDp, 0.16666667f, 1e-6f));
        // 并且确认它**不是** Kyant 的库默认值 0.5 / 0.25（那个是陷阱，见文件头注释）
        assert(!near(aurora::kEdgeAmbientWidthDp, 0.5f, 1e-3f));
        assert(!near(aurora::kEdgeAmbientSoftnessDp, 0.25f, 1e-3f));
    }

    // ======================================================= M-13 标签栏胶囊迁移
    // （编目标注的「弹窗形变母版」）

    // ------------------------------------------------------- 9. 按下缩放是 78/56，不是 1.5
    {
        assert(near(aurora::kTabsPressedScale, 78.0f / 56.0f, 1e-6f));
        assert(near(aurora::kTabsPressedScale, 1.392857f, 1e-5f));
        // ★ 与 M-11/M-12 的 pressedScale = 1.5 **不同** —— 别混用
        assert(!near(aurora::kTabsPressedScale, 1.5f, 1e-3f));

        // lerp(1, 1.2, pressProgress)（源码 :192）
        const float pp = 0.5f;
        const float s = aurora::Lerp(1.0f, aurora::kTabsPressScaleSpan, pp);
        assert(near(s, 1.1f));
        assert(near(aurora::Lerp(1.0f, aurora::kTabsPressScaleSpan, 0.0f), 1.0f));
        assert(near(aurora::Lerp(1.0f, aurora::kTabsPressScaleSpan, 1.0f), 1.2f));
    }

    // ------------------------------------------------------- 10. 选中胶囊放大：随胶囊宽度反比
    //
    // 源码 :176 是 `1 + 16dp/width` —— 关键在**分母是胶囊宽度**。
    // 胶囊越窄，放大比例越大（保持"视觉上多出 16dp"这条设计意图）。
    {
        const float dp16 = 32.0f;   // 16dp @ 2px/dp
        // 窄胶囊（80px）：1 + 32/80 = 1.4
        assert(near(aurora::TabsSelectedPillScale(1.0f, dp16, 80.0f), 1.4f, 1e-5f));
        // 宽胶囊（320px）：1 + 32/320 = 1.1
        assert(near(aurora::TabsSelectedPillScale(1.0f, dp16, 320.0f), 1.1f, 1e-5f));
        // 窄的放大倍数必须更大
        assert(aurora::TabsSelectedPillScale(1.0f, dp16, 80.0f) >
               aurora::TabsSelectedPillScale(1.0f, dp16, 320.0f));
        // progress = 0 时恒为 1
        assert(near(aurora::TabsSelectedPillScale(0.0f, dp16, 80.0f), 1.0f));
        // 线性：中点 = 半量
        assert(near(aurora::TabsSelectedPillScale(0.5f, dp16, 80.0f), 1.2f, 1e-5f));
        // 除零保护
        assert(near(aurora::TabsSelectedPillScale(1.0f, dp16, 0.0f), 1.0f));
    }

    // ------------------------------------------------------- 11. ★ 越界跟手：永远不超过 4dp
    {
        const float dp4 = 8.0f;     // 4dp @ 2px/dp
        const float w = 1000.0f;

        // 未越界 → 偏移为 0
        assert(near(aurora::TabsOverscrollOffsetPx(0.0f, w, dp4), 0.0f));
        // 正向、刚好一个宽度 → EaseOut(1) = 1 → 恰好 4dp
        assert(near(aurora::TabsOverscrollOffsetPx(w, w, dp4), dp4, 1e-4f));
        // 负向对称
        assert(near(aurora::TabsOverscrollOffsetPx(-w, w, dp4), -dp4, 1e-4f));
        // ★ 关键性质：即使拖到 100 倍宽度，也**不超过 4dp**（fraction 被夹到 ±1）
        assert(std::abs(aurora::TabsOverscrollOffsetPx(100.0f * w, w, dp4)) <= dp4 + 1e-4f);
        assert(std::abs(aurora::TabsOverscrollOffsetPx(-100.0f * w, w, dp4)) <= dp4 + 1e-4f);
        // EaseOut 前段斜率大：半程已超过 4dp 的一半（非线性，不是 lerp）
        assert(aurora::TabsOverscrollOffsetPx(w * 0.5f, w, dp4) > dp4 * 0.5f);
    }

    // ------------------------------------------------------- 12. M-13 的速度拉伸：除数 10，方向与 M-11 一致
    {
        float sx = 1.0f, sy = 1.0f;
        aurora::ApplyTabsVelocityStretch(sx, sy, 10.0f);   // v = 10/10 = 1
        // t = clamp(1*0.75, ±0.2) = 0.2 → sx = 1/0.8 = 1.25
        assert(near(sx, 1.25f, 1e-4f));
        // sy = 1 * (1 - clamp(1*0.25,±0.2)) = 1 - 0.2 = 0.8
        assert(near(sy, 0.8f, 1e-4f));

        // 与 Toggle 的除数（50）对比：同一个速度在 Toggle 侧被除以 50，反应弱得多。
        // ⚠️ 必须比**相对变化量** —— 两者基准 scaleX 不同（Toggle 是 0.75、Tabs 是 1.0），
        //    直接比绝对变化会得出相反结论（我第一版就是这么写错的，被这条断言抓出来）。
        const auto tg = ComputeDropletMorph(DropletKind::Toggle, 1.0f, 10.0f, kPx);
        const float toggleRel = tg.scaleX / 0.75f - 1.0f;   // 0.75/0.85 → +17.6%
        const float tabsRel = sx / 1.0f - 1.0f;             // 1.0/0.8   → +25%
        assert(near(toggleRel, 1.0f / 0.85f - 1.0f, 1e-4f));
        assert(near(tabsRel, 0.25f, 1e-4f));
        assert(tabsRel > toggleRel);   // ★ Tabs 的除数更小（10 < 50）⇒ 反应更强

        // 极端速度不发散
        for (float v : {-1e6f, -500.0f, 0.0f, 500.0f, 1e6f}) {
            float a = 1.0f, b = 1.0f;
            aurora::ApplyTabsVelocityStretch(a, b, v);
            assert(std::isfinite(a) && std::isfinite(b));
            assert(a > 0.0f && b >= 0.0f);
        }
    }

    // ------------------------------------------------------- 13. 索引弹簧参数
    {
        auto s = aurora::MakeTabsIndexSpring(2.0f);
        assert(near(s.value(), 2.0f));
        // 阈值必须是 0.5（源码 :118）—— 这是它与其他弹簧不同的地方
        s.setTarget(5.0f);
        for (int i = 0; i < 600; ++i) s.tick(1.0f / 60.0f);
        assert(std::abs(s.value() - 5.0f) < 0.5f);   // 落点在阈值内即算到位
    }

    std::printf("LiquidMorphTest: 全部断言通过（13 组）\n");
    return 0;
}
