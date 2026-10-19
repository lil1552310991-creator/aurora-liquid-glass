#pragma once

// ===========================================================================
//  二级窗口 Demo 的**派生量公式**与**同行判据**（纯逻辑，零 ImGui / 零 Android）
// ===========================================================================
//  为什么把它们抽出来：这两处都是"上一轮改坏、本轮恢复"的地方，而它们的错
//  在真机上只表现为"看着不对"（方块变圆了 / 两枚按钮不见了），
//  没有断言就一定会再犯。抽成纯函数之后，默认值可以直接在宿主回归里钉死。
//
//  ① 实验场派生量：上游 `jni/secondary_imgui.cpp:652` 是
//        corners = lgx::Corners::rounded(objectSize * 0.5f * s_PlaygroundRadius);
//     上一轮漏掉 `* 0.5f` ⇒ 默认半径 256*0.5 = 128dp（上游 64dp），
//     方块半径已达短边一半、看着像圆，而且滑杆 0.5→1.0 区间视觉上没区别。
//     同一个目标里另两处派生量（lensHeight = h*size*0.5、lensAmount = a*size）
//     都带着各自的系数 —— 三者的系数**都是上游原文**，不许"顺手统一"。
//
//  ② 按钮行的"能否同行"判据：旧代码用 `ImGui::GetContentRegionAvail().x`，
//     而按钮是**自绘**的（不产生 ImGui item、不推进光标）⇒ 那个 API 返回的是
//     **整行宽度**、判据恒真 ⇒ 四枚按钮 + 两段计数被塞进一行，超出的被裁掉
//     （真机上「表面色」和「+」整个看不见）。正确判据必须用**上一个 item 的真实
//     右边界**。本项目自己记过这个坑：docs/LIQUID_GLASS_INSPECTOR.md:346。
// ===========================================================================

namespace aurora {

// 上游系数：实验场（playground）的三条派生量
inline constexpr float kPlaygroundCornerScale = 0.5f;   // 圆角 = size * 0.5 * ratio
inline constexpr float kPlaygroundLensHeightScale = 0.5f;  // 高  = ratio * size * 0.5
inline constexpr float kPlaygroundLensAmountScale = 1.0f;  // 强  = ratio * size

//  默认值（size=256、圆角比例 0.5、高度比例 0.2、强度比例 0.2）⇒ 64 / 25.6 / 51.2
inline float PlaygroundCornerDp(float sizeDp, float radiusRatio) {
    return sizeDp * kPlaygroundCornerScale * radiusRatio;
}
inline float PlaygroundLensHeightDp(float heightRatio, float sizeDp) {
    return heightRatio * sizeDp * kPlaygroundLensHeightScale;
}
inline float PlaygroundLensAmountDp(float amountRatio, float sizeDp) {
    return amountRatio * sizeDp * kPlaygroundLensAmountScale;
}

/**
 * 下一个控件能否与上一个**并排在同一行**。
 *
 *   lastItemRight : 上一个控件的**真实右边界**（屏幕坐标）
 *   gap           : 两控件之间的间距（ImGuiStyle::ItemSpacing.x）
 *   nextWidth     : 下一个控件的宽度
 *   contentRight  : 内容区右边界（屏幕坐标）
 *
 *  ★ 绝不要用 `GetContentRegionAvail()` 当这里的输入：自绘组件（我们的玻璃按钮、
 *    开关、滑块全是自绘的）不推进 ImGui 光标，那个 API 会返回整行宽度 ⇒ 恒真。
 */
inline bool FitsOnSameLine(float lastItemRight, float gap, float nextWidth, float contentRight) {
    if (!(nextWidth > 0.0f)) return false;
    if (!(contentRight > 0.0f) || !(lastItemRight >= 0.0f)) return false;
    return lastItemRight + gap + nextWidth <= contentRight;
}

/** 只有文字时的便捷重载。 */
inline bool TextFitsOnSameLine(float lastItemRight, float gap, float textWidth,
                               float contentRight) {
    return FitsOnSameLine(lastItemRight, gap, textWidth, contentRight);
}

}  // namespace aurora
