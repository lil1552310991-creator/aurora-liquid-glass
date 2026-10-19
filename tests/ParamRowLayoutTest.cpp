// ===========================================================================
//  ParamRowLayout.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  这一组测试就是「调试器参数行」的验收依据 —— 它把用户报的两个现象
//  变成可断言的命题：
//    · 滑块**必须**随窗口变宽（旧实现用 ImGui 表格 + WidthFixed，
//      列宽只在表格第一帧生效 ⇒ 窗口拉宽滑块纹丝不动）；
//    · 「重置」按钮**永远**在可视区内（旧实现在窄窗口下把列宽之和算得比
//      可用宽度还大 ⇒ 最后一列被表格裁剪矩形切掉，而且因为列宽被锁死，
//      之后把窗口拉宽也不会恢复）。
//
//  覆盖用户点名的三档宽度：320dp / 480dp / 720dp（本工程密度 2.0 ⇒ 640 / 960 / 1440 px），
//  再补一档"比最小还窄"和"比屏幕还宽"的极端值。
// ===========================================================================

#include "ParamRowLayout.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ComputeParamRowLayout;
using aurora::ParamRowLayout;
using aurora::ParamRowMetrics;

static int g_groups = 0;

// 本工程真机上的真实度量（2 倍缩放 + 内嵌中文字体，量自真机截图）：
//   "重置" 两个汉字 ≈ 66px；FramePadding.x = 8（4×2）；ItemSpacing.x = 16（8×2）。
static ParamRowMetrics RealMetrics() {
    ParamRowMetrics m;
    m.nameText = 230.0f;    // 例如 "兜底色跟随主题" 这种 7 字参数名
    m.valueText = 96.0f;    // 例如 "24.000"
    m.resetText = 66.0f;    // "重置"
    m.framePaddingX = 8.0f;
    m.spacing = 16.0f;
    return m;
}

// 不变量检查：任何宽度、任何度量下都必须成立
static void CheckInvariants(const ParamRowLayout& layout, const ParamRowMetrics& m) {
    const float spacing = m.spacing;
    // ① 第一行永远不溢出（名称 + 数值 + 重置 + 两个间距）
    const float firstLine = layout.name + layout.value + layout.reset + spacing * 2.0f;
    assert(firstLine <= layout.available + 0.01f);
    // ② 单行：滑块不小于下限，且整行不超过可用宽度
    if (!layout.twoLine) {
        assert(layout.slider >= m.minSlider - 0.01f);
        assert(layout.lineWidth <= layout.available + 0.01f);
    } else {
        // ③ 双行：滑块占满整行
        assert(std::fabs(layout.slider - layout.available) < 0.01f);
    }
    // ④ 所有宽度必须有限且非负
    assert(std::isfinite(layout.name) && layout.name >= 0.0f);
    assert(std::isfinite(layout.slider) && layout.slider > 0.0f);
    assert(std::isfinite(layout.value) && layout.value >= 0.0f);
    assert(std::isfinite(layout.reset) && layout.reset > 0.0f);
}

// ★ 核心命题：窗口变宽 ⇒ 滑块变宽（旧实现就是在这里失效的）
static void TestSliderGrowsWithWindow() {
    const ParamRowMetrics m = RealMetrics();
    // 本工程密度 2.0：320 / 480 / 720 dp ⇒ 640 / 960 / 1440 px
    // ★ 三档都落在**单行**模式里（320dp 这一档刚好还容得下 150px 的滑块），
    //   所以可以直接断言"严格变宽"。窄到切换双行的情况见下一组。
    const float widths[3] = {640.0f, 960.0f, 1440.0f};
    float previous = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const ParamRowLayout layout = ComputeParamRowLayout(widths[i], m);
        CheckInvariants(layout, m);
        assert(!layout.twoLine);
        assert(layout.slider > previous);            // 严格变宽
        previous = layout.slider;
    }
    // 720dp 那一档必须明显比 320dp 宽（不是"多几个像素"那种假响应）
    const ParamRowLayout narrow = ComputeParamRowLayout(640.0f, m);
    const ParamRowLayout wide = ComputeParamRowLayout(1440.0f, m);
    assert(wide.slider - narrow.slider > 700.0f);
    // 旧实现在这里是什么样：列宽只在表格第一帧锁死 ⇒ 三档拿到的滑块宽度**完全相同**。
    // 这一条断言就是"回归防护"：任何人把响应式布局换回 WidthFixed 表格，它立刻红。
    assert(narrow.slider != wide.slider);
    ++g_groups;
}

// 单调性：available 从小到大扫一遍，滑块不许忽大忽小（双行切换点允许跳变到整行宽）
static void TestMonotonic() {
    const ParamRowMetrics m = RealMetrics();
    float previousSlider = 0.0f;
    bool previousTwoLine = true;
    for (float w = 200.0f; w <= 2200.0f; w += 7.0f) {
        const ParamRowLayout layout = ComputeParamRowLayout(w, m);
        CheckInvariants(layout, m);
        if (!layout.twoLine && !previousTwoLine) {
            assert(layout.slider >= previousSlider - 0.01f);   // 同模式内单调不减
        }
        previousSlider = layout.slider;
        previousTwoLine = layout.twoLine;
    }
    ++g_groups;
}

// ★ 窄屏必须自动切双行（用户要求的窄屏布局），并且滑块拿到整行宽度
static void TestNarrowSwitchesToTwoLines() {
    const ParamRowMetrics m = RealMetrics();
    // 760px 是本工程主题窗口的默认宽度；再窄下去（约 620px 可用宽度以下）就该换行
    const ParamRowLayout veryNarrow = ComputeParamRowLayout(360.0f, m);
    assert(veryNarrow.twoLine);
    assert(std::fabs(veryNarrow.slider - 360.0f) < 0.01f);
    CheckInvariants(veryNarrow, m);
    // 双行模式下：名称 + 数值 + 重置 仍然要放得下（否则重置还是会被裁）
    assert(veryNarrow.name + veryNarrow.value + veryNarrow.reset + m.spacing * 2.0f
           <= 360.0f + 0.01f);
    // 宽窗口不该换行
    const ParamRowLayout wide = ComputeParamRowLayout(1440.0f, m);
    assert(!wide.twoLine);
    // 切换点判据：名称 + 数值 + 重置 + 滑块下限 + 间距 刚好放得下 ⇒ 必须还是单行
    const float justFits = m.nameText + m.spacing                     // 名称（未触顶时）
                         + m.minSlider + m.valueText + m.framePaddingX * 2.0f + m.valueGap
                         + m.resetText + m.framePaddingX * 2.0f + m.resetSlack
                         + m.spacing * 3.0f + 1.0f;
    assert(!ComputeParamRowLayout(justFits, m).twoLine);
    // 再窄 1px（且窄到名称已被 ratio 封顶的区间）⇒ 切成双行
    assert(ComputeParamRowLayout(600.0f, m).twoLine);
    ++g_groups;
}

// ★ 重置按钮永远完整可见：极窄窗口下也不许被挤出去
static void TestResetAlwaysFits() {
    const ParamRowMetrics m = RealMetrics();
    const float natural = m.resetText + m.framePaddingX * 2.0f;   // 按钮的自然宽度
    // 只要可用宽度容得下"重置 + 两个间距"，重置就必须拿到自然宽度（完整可见）
    const float floorWidth = natural + m.spacing * 2.0f;
    for (float w = 80.0f; w <= 2000.0f; w += 13.0f) {
        const ParamRowLayout layout = ComputeParamRowLayout(w, m);
        CheckInvariants(layout, m);
        if (w >= floorWidth) assert(layout.reset >= natural);
        // 内容右边界不许越过可用宽度（越界就是被裁）
        assert(layout.name + layout.value + layout.reset + m.spacing * 2.0f
               <= layout.available + 0.01f);
    }
    // 本工程主题窗口的**最小宽度**是 680px（340dp）⇒ 内容区约 608px，
    // 这里必须给出"重置完整可见 + 滑块不小于下限"的布局
    const ParamRowLayout minWindow = ComputeParamRowLayout(608.0f, m);
    assert(minWindow.reset >= natural);
    assert(minWindow.twoLine);
    assert(minWindow.slider >= m.minSlider);
    ++g_groups;
}

// 长参数名 / 大数字：名称列有上限、数值列的硬宽度必须量得出来
static void TestLongNameAndBigValue() {
    ParamRowMetrics m = RealMetrics();
    m.nameText = 900.0f;    // 一个超长参数名
    m.valueText = 260.0f;   // 例如 "#RRGGBBAA" 或 "1234.567"
    const ParamRowLayout layout = ComputeParamRowLayout(960.0f, m);
    CheckInvariants(layout, m);
    // 名称列被 ratio 封顶（否则滑块会被挤成一条缝）
    assert(layout.name <= 960.0f * m.nameMaxRatio + 0.01f);
    assert(layout.slider >= m.minSlider);
    // 数值列的硬宽度必须容得下最长文本 + 内边距
    assert(layout.value >= m.valueText + m.framePaddingX * 2.0f);
    ++g_groups;
}

// 退化输入：0 / 负数 / NaN 都要给出**能用**的布局，不许崩、不许负数
static void TestDegenerateInputs() {
    const ParamRowMetrics m = RealMetrics();
    const float widths[4] = {0.0f, -100.0f, std::nanf(""), 1.0f};
    for (float w : widths) {
        const ParamRowLayout layout = ComputeParamRowLayout(w, m);
        assert(std::isfinite(layout.slider) && layout.slider > 0.0f);
        assert(std::isfinite(layout.reset) && layout.reset >= 0.0f);
        assert(std::isfinite(layout.name) && layout.name >= 0.0f);
        assert(std::isfinite(layout.value) && layout.value >= 0.0f);
    }
    // ★ 退化域里不变的仍然是"几何必须待在可视区内"（不变量①优先于宽度美观）：
    //   本工程窗口内容区最小也有 600px 以上，所以真正的可用域见上面几组测试。
    const ParamRowLayout tiny = ComputeParamRowLayout(40.0f, m);
    assert(tiny.name + tiny.value + tiny.reset + m.spacing * 2.0f <= 40.0f + 0.01f);
    // 度量为空（字体还没准备好）也不能出现负宽度或 NaN
    ParamRowMetrics empty;
    const ParamRowLayout layout = ComputeParamRowLayout(760.0f, empty);
    assert(std::isfinite(layout.slider) && layout.slider > 0.0f);
    assert(layout.name >= 0.0f && layout.reset > 0.0f);
    ++g_groups;
}

// 拖动精度：滑块长度直接决定"每像素多少参数值"（用户要求 7）
static void TestDragPrecisionFollowsWidth() {
    const ParamRowMetrics m = RealMetrics();
    const float range = 2.0f;                       // 例如 backdropScaleX 的 0..2
    const ParamRowLayout narrow = ComputeParamRowLayout(640.0f, m);
    const ParamRowLayout wide = ComputeParamRowLayout(1440.0f, m);
    const float perPixelNarrow = range / narrow.slider;
    const float perPixelWide = range / wide.slider;
    assert(perPixelWide < perPixelNarrow);          // 宽滑块每像素改得更少 = 更精细
    assert(perPixelNarrow / perPixelWide > 2.0f);   // 至少精细一倍以上
    ++g_groups;
}

int main() {
    TestSliderGrowsWithWindow();
    TestMonotonic();
    TestNarrowSwitchesToTwoLines();
    TestResetAlwaysFits();
    TestLongNameAndBigValue();
    TestDegenerateInputs();
    TestDragPrecisionFollowsWidth();
    std::printf("ParamRowLayoutTest: %d regression groups passed\n", g_groups);
    return 0;
}
