// ===========================================================================
//  GlassCapturePadding.h 的离线回归（纯逻辑，零 GL / 零 ImGui / 零 Android）
//
//  这一组断言回答的是性能优化里最危险的那个问题：
//      「把采样区留白从 94px 降到 43px，凭什么不会让边缘的折射/模糊露馅？」
//
//  三条可断言的命题（对应实现里的三条注释）：
//    ① 普通面板（非液滴 / 非背景拉伸）的留白**只需**覆盖模糊核触达距离
//       （3σ + 1px 盒式预滤波），因为折射位移恒朝内；
//    ② 背景拉伸（scale<1）必须按半对角线 × (1/scale − 1) 额外留白；
//    ③ A/B 开关（legacyLensPad）能逐值恢复旧行为，便于真机对照。
//
//  另外把"优化前后到底省了多少面积"写成数值断言 —— 这样以后有人把折射留白
//  加回来、或者把增益改错，测试会直接红。
// ===========================================================================

#include "third_party/liquidglass/ytbl/GlassCapturePadding.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using lgx::detail::BlurReachPx;
using lgx::detail::CapturePaddingPx;
using lgx::detail::CapturePadSpec;

static int g_groups = 0;

// 本工程真机的默认光学配方（二级窗口：10 / 18 / 30 / 0.70，密度 2.0）：
//   blur 10dp → 20px；lensAmount 30dp → 60px；spectral 0.70。
static CapturePadSpec WindowSpec() {
    CapturePadSpec spec;
    spec.basePx = 3.0f;
    spec.maxPx = 640.0f;
    spec.blurPx = 20.0f;
    spec.blurGain = 2.0f;
    spec.lensAmountPx = 60.0f;
    spec.spectral = 0.70f;
    spec.boxWidth = 760.0f;
    spec.boxHeight = 1500.0f;
    return spec;
}

static void TestOrdinaryPanelOnlyPaysForBlur() {
    const CapturePadSpec spec = WindowSpec();
    const float pad = CapturePaddingPx(spec);
    // ① 必须覆盖模糊触达（这是硬下界）
    const float reach = BlurReachPx(spec.blurPx);
    assert(pad >= reach);
    // ② 不能再把折射量算进来：旧的 94px 里有一半是折射
    const float legacy = CapturePaddingPx([&] {
        CapturePadSpec s = spec;
        s.legacyLensPad = true;
        return s;
    }());
    assert(legacy > pad + 40.0f);
    // ③ 采样面积的实际收益：窗口 760×1500
    const float panelArea = 760.0f * 1500.0f;
    const float newArea = (760.0f + 2.0f * pad) * (1500.0f + 2.0f * pad);
    const float oldArea = (760.0f + 2.0f * legacy) * (1500.0f + 2.0f * legacy);
    assert(newArea < oldArea * 0.90f);
    std::printf("  [普通窗口] 留白 %.1fpx（旧 %.1fpx），采样面积 %.2fM → %.2fM（-%.0f%%）\n",
                static_cast<double>(pad), static_cast<double>(legacy),
                static_cast<double>(oldArea / 1.0e6), static_cast<double>(newArea / 1.0e6),
                static_cast<double>((1.0f - newArea / oldArea) * 100.0f));
    ++g_groups;
}

static void TestSmallPanelGainsMost() {
    // 小面板（按钮：2dp 模糊、12dp 折射量、24dp 折射位移）在旧公式下留白
    // 比面板本身还大，是"每帧白拷贝一整个按钮面积"的来源。
    CapturePadSpec spec;
    spec.basePx = 3.0f;
    spec.maxPx = 640.0f;
    spec.blurPx = 4.0f;          // BUTTON_BLUR_DP = 2 × 密度 2
    spec.blurGain = 2.0f;
    spec.lensAmountPx = 48.0f;   // BUTTON_LENS_AMOUNT_DP = 24 × 2
    spec.spectral = 0.70f;
    spec.boxWidth = 200.0f;
    spec.boxHeight = 100.0f;
    const float pad = CapturePaddingPx(spec);
    CapturePadSpec legacySpec = spec;
    legacySpec.legacyLensPad = true;
    const float legacy = CapturePaddingPx(legacySpec);
    assert(pad == 11.0f);        // 3 + 4×2
    assert(legacy > 90.0f);
    const float newArea = (200.0f + 2.0f * pad) * (100.0f + 2.0f * pad);
    const float oldArea = (200.0f + 2.0f * legacy) * (100.0f + 2.0f * legacy);
    assert(newArea < oldArea * 0.60f);
    // 模糊仍被覆盖（这是不可退让的下界）
    assert(pad >= BlurReachPx(spec.blurPx));
    std::printf("  [按钮]     留白 %.1fpx（旧 %.1fpx），采样面积 %.0fK → %.0fK（-%.0f%%）\n",
                static_cast<double>(pad), static_cast<double>(legacy),
                static_cast<double>(oldArea / 1.0e3), static_cast<double>(newArea / 1.0e3),
                static_cast<double>((1.0f - newArea / oldArea) * 100.0f));
    ++g_groups;
}

static void TestBlurReachIsAlwaysCovered() {
    // 扫一遍模糊半径：每一档都必须满足 pad ≥ 3σ+1（gather 的真实触达）。
    for (float blur = 1.0f; blur <= 20.0f; blur += 1.0f) {
        CapturePadSpec spec;
        spec.basePx = 3.0f;
        spec.maxPx = 640.0f;
        spec.blurPx = blur;
        spec.blurGain = 2.0f;
        spec.boxWidth = 400.0f;
        spec.boxHeight = 200.0f;
        const float pad = CapturePaddingPx(spec);
        assert(pad >= BlurReachPx(blur));
    }
    ++g_groups;
}

static void TestBackdropStretchPaysForItsOwnReach() {
    // Toggle：backdropScaleY 最小 0.75、blend=1 ⇒ 采样点被推到面板外
    CapturePadSpec spec;
    spec.basePx = 3.0f;
    spec.maxPx = 640.0f;
    spec.blurPx = 2.0f;
    spec.blurGain = 2.0f;
    spec.backdropScaleY = 0.75f;
    spec.backdropScaleX = 1.0f;
    spec.backdropBlend = 1.0f;
    spec.boxWidth = 120.0f;
    spec.boxHeight = 60.0f;
    const float pad = CapturePaddingPx(spec);
    const float halfDiagonal = 0.5f * std::sqrt(120.0f * 120.0f + 60.0f * 60.0f);
    const float need = halfDiagonal * (1.0f / 0.75f - 1.0f);
    assert(pad >= need + 2.0f * spec.blurPx);
    // 拉伸量随 scale 变小而变大
    spec.backdropScaleY = 0.5f;
    assert(CapturePaddingPx(spec) > pad);
    std::printf("  [拉伸开关] 留白 %.1fpx（其中拉伸需 %.1fpx）\n",
                static_cast<double>(CapturePaddingPx(spec)),
                static_cast<double>(0.5f * std::sqrt(120.0f * 120.0f + 60.0f * 60.0f)));
    ++g_groups;
}

static void TestLiquidKeepsLegacyPadding() {
    // 液滴融合场在颈部可能向外取样 ⇒ 必须保留整份折射留白（宁大勿小）。
    CapturePadSpec spec = WindowSpec();
    spec.liquid = true;
    const float liquidPad = CapturePaddingPx(spec);
    CapturePadSpec plain = WindowSpec();
    const float plainPad = CapturePaddingPx(plain);
    assert(liquidPad > plainPad + 40.0f);
    assert(std::fabs(liquidPad - CapturePaddingPx([&] {
        CapturePadSpec s = plain;
        s.legacyLensPad = true;
        return s;
    }())) < 0.01f);
    ++g_groups;
}

static void TestDegenerateInputsAreSafe() {
    CapturePadSpec spec;
    spec.basePx = 3.0f;
    spec.maxPx = 640.0f;
    spec.blurPx = -5.0f;            // 负模糊 ⇒ 只留 base
    assert(CapturePaddingPx(spec) == spec.basePx);
    spec.blurPx = NAN;
    assert(CapturePaddingPx(spec) == spec.basePx);   // NaN 不传播
    spec.blurPx = 1.0e6f;           // 超大模糊 ⇒ 夹到上限
    assert(CapturePaddingPx(spec) == spec.maxPx);
    spec.blurPx = 0.0f;
    spec.backdropScaleY = 0.0f;     // 0 缩放不能除零
    spec.backdropBlend = 1.0f;
    assert(CapturePaddingPx(spec) == spec.basePx);
    ++g_groups;
}

int main() {
    std::printf("CapturePaddingTest\n");
    TestOrdinaryPanelOnlyPaysForBlur();
    TestSmallPanelGainsMost();
    TestBlurReachIsAlwaysCovered();
    TestBackdropStretchPaysForItsOwnReach();
    TestLiquidKeepsLegacyPadding();
    TestDegenerateInputsAreSafe();
    std::printf("CapturePaddingTest: %d regression groups passed\n", g_groups);
    return 0;
}
