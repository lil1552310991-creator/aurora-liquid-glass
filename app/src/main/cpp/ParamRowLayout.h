#pragma once

// ===========================================================================
//  参数行的**响应式布局**（纯逻辑，零 ImGui / 零 Android 依赖）
// ===========================================================================
//  为什么要有这一层：调试器里每一行参数都是「名称 | 滑块 | 数值 | 重置」，
//  它们必须**随窗口宽度实时重排**。第一版用的是 ImGui 的四列表格
//  （`BeginTable` + `TableSetupColumn(WidthFixed, w)`），看起来"按可用宽度算列宽"，
//  实际上是坏的，根因有两条（都已在 ImGui 源码里核实）：
//
//    ① `TableSetupColumn(..., WidthFixed, w)` 里的 w **只在表格第一次建立的那一帧
//       生效**：imgui_tables.cpp:1572-1585 那段被 `if (table->IsInitializing)` 包着，
//       而 `IsInitializing` 只在 `table->RawData == NULL`（首次创建）时置位
//       （imgui_tables.cpp:548），并在 BeginTable 结束时清掉（:1516）。
//       ⇒ 之后每帧传进去的新列宽**被静默忽略**：窗口拉宽，滑块纹丝不动。
//    ② 表格里没有 Stretch 列时（四列全是 WidthFixed），余量**不会**分给任何列
//       （imgui_tables.cpp:958-983 只对 WidthStretch 列分配剩余宽度）
//       ⇒ 加宽窗口只是右边多出一块空白。
//
//  ⇒ 结论：要"跟手"就必须**自己算每一行的宽度**，并且这一层要能被离线回归覆盖
//    （不同宽度下断言"滑块会变宽 / 重置永远在可视区内 / 空间不足换双行"）。
//
//  布局规则（用户要求，逐条对应）：
//    宽屏：  参数名称 | 可伸缩滑块 | 数值 | 重置
//    窄屏：  第一行 参数名称 | 数值 | 重置
//            第二行 占据整个可用宽度的滑块
//  ★ 空间不足时的**让位顺序**是固定的（从最先让位到最后让位）：
//      名称（可以裁） → 滑块（先缩到下限） → 数值（保持硬宽度） → 重置（最后才动）
//    这样"重置按钮被裁掉"从结构上不可能发生。
// ===========================================================================

namespace aurora {

// 一行的度量输入（由宿主用真实字体量出来传进来，所以这一层不需要认识字体）
struct ParamRowMetrics {
    float nameText = 0.0f;        // 本目标里**最长**的参数名文本宽度
    float valueText = 0.0f;       // 本目标里**最长**的数值显示文本宽度
    float resetText = 0.0f;       // "重置" 的文本宽度
    float framePaddingX = 0.0f;   // 按钮/输入框的左右内边距（ImGuiStyle::FramePadding.x）
    float spacing = 0.0f;         // 控件之间的水平间距（ImGuiStyle::ItemSpacing.x）
    float minSlider = 150.0f;     // 滑块的最小可用宽度：低于它宁可换双行
                                  //   ★ 150px ≈ 75dp：再窄就点不准了；这个阈值决定
                                  //     "什么时候从单行切成双行"（见离线回归）
    float nameMaxRatio = 0.34f;   // 名称列最多占可用宽度的比例（防止长名字挤没滑块）
    float nameMin = 64.0f;        // 名称列的下限（实在不够就裁字，但不会负）
    float valueGap = 10.0f;       // 数值两侧的呼吸量
    float resetSlack = 6.0f;      // 重置按钮的余量（保证按钮的"自然宽度"永远装得下）
};

struct ParamRowLayout {
    bool twoLine = false;   // true = 空间不足，滑块换到第二行
    float available = 0.0f; // 本行的可用宽度（= 窗口内容区宽度）
    float name = 0.0f;      // 名称列宽
    float slider = 0.0f;    // 滑块宽度
    float value = 0.0f;     // 数值列宽（硬宽度，Selectable 用）
    float reset = 0.0f;     // 重置按钮宽
    float lineWidth = 0.0f; // 单行模式下第一行实际占用的宽度（<= available）
};

namespace param_row_detail {
inline float ClampF(float v, float lo, float hi) {
    if (hi < lo) hi = lo;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
}  // namespace param_row_detail

// 计算一行参数在给定可用宽度下的布局。
//   ★ 不变量（离线回归逐条断言）：
//     1. `name + value + reset + 2*spacing <= available`（第一行永远不溢出）
//     2. 单行模式：`slider >= minSlider`，且 `lineWidth <= available`
//     3. 双行模式：`slider == available`（滑块占满整行）
//     4. available 变大时 slider 不会变小（单调）
inline ParamRowLayout ComputeParamRowLayout(float available, const ParamRowMetrics& m) {
    using param_row_detail::ClampF;
    ParamRowLayout out;
    if (!(available > 0.0f)) available = 1.0f;
    out.available = available;

    const float spacing = m.spacing > 0.0f ? m.spacing : 0.0f;
    // ---- 固定宽度的两块：数值（硬宽度）与重置（永远完整可见）----
    out.value = m.valueText + m.framePaddingX * 2.0f + m.valueGap;
    out.reset = m.resetText + m.framePaddingX * 2.0f + (m.resetSlack > 0.0f ? m.resetSlack : 0.0f);
    // 极端窄窗口（连"数值 + 重置 + 两个间距"都放不下）：
    //   ★ 让位顺序是**从最不重要到最重要**：先把数值列压到 0（它的文本会被单元格裁掉），
    //     再压重置 —— 也就是说"重置按钮完整可见"这条优先级最高。
    //   ★ 只有连"重置 + 两个间距"都放不下（available < 约 120px，本工程窗口最小宽度
    //     680px 根本到不了）时，才等比缩重置。
    //   ★ 缩放的目标里**必须扣掉间距**：只缩 value/reset 而不缩 spacing，
    //     invariant ① 就会在这些宽度上被破坏（离线回归抓到过）。
    const float spacingTotal = spacing * 2.0f;
    if (out.value + out.reset + spacingTotal > available) {
        const float room = available - spacingTotal;
        out.value = room - out.reset;                 // 先牺牲数值列
        if (out.value < 0.0f) {
            out.value = 0.0f;
            out.reset = room > 0.0f ? room : 0.0f;    // 连重置都放不下才缩它
        }
    }

    // ---- 名称：按最长名字宽，再夹到 [nameMin, available*nameMaxRatio] ----
    float nameWant = m.nameText + spacing;
    const float nameCap = available * (m.nameMaxRatio > 0.05f ? m.nameMaxRatio : 0.34f);
    if (nameWant > nameCap) nameWant = nameCap;
    float nameFloor = m.nameMin;
    // 名称的下限不能把数值/重置挤出去
    const float nameRoom = available - out.value - out.reset - spacing * 2.0f;
    if (nameFloor > nameRoom) nameFloor = nameRoom > 0.0f ? nameRoom : 0.0f;
    out.name = ClampF(nameWant, nameFloor, nameRoom > 0.0f ? nameRoom : 0.0f);

    // ---- 单行放不下 ⇒ 换双行（滑块的第二个位置）----
    const float minSlider = m.minSlider > 1.0f ? m.minSlider : 1.0f;
    const float singleRoom = available - out.name - out.value - out.reset - spacing * 3.0f;
    if (singleRoom >= minSlider) {
        out.twoLine = false;
        out.slider = singleRoom;          // 余量**全部**给滑块（唯一伸缩的一列）
        out.lineWidth = out.name + out.slider + out.value + out.reset + spacing * 3.0f;
    } else {
        out.twoLine = true;
        out.slider = available;           // 第二行占满可用宽度
        out.lineWidth = out.name + out.value + out.reset + spacing * 2.0f;
    }
    return out;
}

}  // namespace aurora
