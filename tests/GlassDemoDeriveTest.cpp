// ===========================================================================
//  GlassDemoDerive.h 的离线回归
//
//  这两组断言各自钉住一个"上一轮改坏、本轮恢复"的真实 bug：
//    · 实验场默认圆角漏了 `* 0.5f` ⇒ 方块从"圆角方形"变成接近正圆；
//    · 按钮行的同行判据用了 `GetContentRegionAvail()`（自绘组件下恒为整行宽）
//      ⇒ 四枚按钮挤一行 ⇒ 后两枚被裁到窗口外。
//  断言里写的是**上游原文的系数**，所以以后谁"顺手简化"都会红。
// ===========================================================================

#include "GlassDemoDerive.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::FitsOnSameLine;
using aurora::PlaygroundCornerDp;
using aurora::PlaygroundLensAmountDp;
using aurora::PlaygroundLensHeightDp;
using aurora::TextFitsOnSameLine;

static int g_groups = 0;

// ---- ① 实验场派生量：默认值必须与上游公式一致 ----------------------------
static void TestPlaygroundDefaults() {
    // 默认：size=256、圆角比例 0.5、高度比例 0.2、强度比例 0.2
    assert(std::fabs(PlaygroundCornerDp(256.0f, 0.5f) - 64.0f) < 0.01f);
    assert(std::fabs(PlaygroundLensHeightDp(0.2f, 256.0f) - 25.6f) < 0.01f);
    assert(std::fabs(PlaygroundLensAmountDp(0.2f, 256.0f) - 51.2f) < 0.01f);

    // ★ 这一条就是那个 bug：少了 0.5 会得到 128
    const float wrong = 256.0f * 0.5f;                       // = 128，上一轮的错误算法
    assert(std::fabs(PlaygroundCornerDp(256.0f, 0.5f) - wrong) > 1.0f);

    // 系数本身：圆角与折射高度都是 0.5，强度是 1.0（不许"顺手统一"）
    assert(aurora::kPlaygroundCornerScale == 0.5f);
    assert(aurora::kPlaygroundLensHeightScale == 0.5f);
    assert(aurora::kPlaygroundLensAmountScale == 1.0f);

    // 单调性 + 端点：比例 0.1~1.0 ⇒ 半径 12.8~128（size=256）
    assert(PlaygroundCornerDp(256.0f, 0.1f) < PlaygroundCornerDp(256.0f, 1.0f));
    assert(std::fabs(PlaygroundCornerDp(256.0f, 1.0f) - 128.0f) < 0.01f);
    // 尺寸变化时按比例走（96~256）
    assert(std::fabs(PlaygroundCornerDp(96.0f, 0.5f) - 24.0f) < 0.01f);
    ++g_groups;
}

// ---- ② 同行判据：必须用真实右边界，且边界情况正确 ------------------------
static void TestSameLineRule() {
    const float gap = 16.0f;
    const float right = 700.0f;          // 内容右边界
    // 放得下 ⇒ 同行
    assert(FitsOnSameLine(500.0f, gap, 160.0f, right));      // 500+16+160 = 676 <= 700
    // 差一点点 ⇒ 不放
    assert(!FitsOnSameLine(525.0f, gap, 160.0f, right));     // 525+16+160 = 701 > 700
    // 正好贴边 ⇒ 放（用 <=）
    assert(FitsOnSameLine(524.0f, gap, 160.0f, right));      // 524+16+160 = 700
    // 退化输入
    assert(!FitsOnSameLine(0.0f, gap, 0.0f, right));         // 宽度 0
    assert(!FitsOnSameLine(0.0f, gap, 160.0f, 0.0f));        // 内容区未知
    assert(!FitsOnSameLine(-1.0f, gap, 160.0f, right));      // 还没有上一个 item
    // ★ 反证：如果误用"整行宽度"当输入（旧 bug 的等价形式），判据会恒真 ——
    //   这里用"整行宽 700"当 lastItemRight 模拟那种写法，它必然放不下 4 枚按钮里的后两枚
    const float contentWidth = 700.0f;
    float last = 0.0f;
    int placed = 0;
    const float widths[4] = {160.0f, 140.0f, 140.0f, 48.0f};
    for (int i = 0; i < 4; ++i) {
        if (i == 0) { last = widths[0]; ++placed; continue; }
        if (FitsOnSameLine(last, gap, widths[i], contentWidth)) { last += gap + widths[i]; ++placed; }
        else break;
    }
    // 正确判据下：160+16+140 = 316，+16+140 = 472，+16+48 = 536 ⇒ 四枚都放得下
    assert(placed == 4);
    // 但换成"整行宽"当输入（旧写法）时，第一枚之后就判到 700+16+140 > 700 ⇒ 恒假，
    // 于是代码会**永远** SameLine（旧实现的真正行为）—— 这里用断言把这个差异写清楚
    assert(!FitsOnSameLine(contentWidth, gap, widths[1], contentWidth));
    ++g_groups;
}

// ---- ③ 文字版判据（「点击 N」/ 小按钮计数用的就是它）---------------------
static void TestTextVariant() {
    assert(TextFitsOnSameLine(300.0f, 16.0f, 120.0f, 700.0f));   // 按钮右侧放得下"点击 0"
    assert(!TextFitsOnSameLine(600.0f, 16.0f, 120.0f, 700.0f));  // 靠右边缘时退回下一行
    ++g_groups;
}

int main() {
    TestPlaygroundDefaults();
    TestSameLineRule();
    TestTextVariant();
    std::printf("GlassDemoDeriveTest: %d regression groups passed\n", g_groups);
    return 0;
}
