#pragma once

// ===========================================================================
//  Corner Resize Handle —— 窗口右下角的「缩放弧」（几何 + 动画，纯逻辑）
// ===========================================================================
//
//  参考物（用户给的图一）：iOS 控制中心编辑态里，磁贴右下角那段**白色粗圆弧**。
//  它是一条**连续曲线**，不是"横向一根 + 纵向一根拼出来的 L"。
//
//  像素实测（research/handle_redesign/measure_arc.py：连通域 → Zhang-Suen 细化 →
//  最小二乘拟合圆；可视化复核图 mask_camera.png）：
//    · 弧**中心线**半径 ≈ 62 px，线宽 ≈ 28–31 px（≈ 0.45 × 半径），角跨度 ≈ 108°
//    · 拟合圆心与磁贴圆心基本重合 ⇒ 弧是**与圆角同心**的一段圆环
//    · 两端是**圆端帽**；弧的 bbox ≈ 89×92 px（磁贴半径 ≈ 71 px）
//  ⇒ 本组件把同一套几何适配到窗口的**右下圆角**上：
//      圆心   = 圆角圆心（右下角往内缩一个圆角半径）
//      半径   = 圆角半径 + 间距 + 半个线宽   （贴着圆角外侧，不悬空、不压进内容）
//      跨度   ≈ 96°，中心方向 = 右下对角线（45°）
//      端帽   = 半圆，与弧同宽
//
//  为什么这样定义（对应任务里列的每一条）：
//    · 「单条连续曲线 / 转折完全平滑 / 不出现尖角与接缝」
//      ⇒ 整条弧用**一个多边形**（外弧 + 端帽 + 内弧 + 端帽）一次填充，
//        而不是"描边折线 + 两个圆帽"三条图元叠出来 —— 后者在端帽与折线交界处
//        会有 AA 重叠（半透明时会显出一道接缝），本项目要求高视觉质量，所以避开。
//    · 「参数化 / 不写死截图像素」
//      ⇒ 全部用 dp 或"相对圆角半径的比例"表达；像素换算由宿主传 pxPerDp。
//    · 「几何与动画解耦」
//      ⇒ ComputeCornerHandle() 只吃一个 progress 标量，完全不认识弹簧；
//        CornerHandleAnimation 只产出 progress，完全不认识窗口。
//
//  离线性：本文件零 ImGui / 零 Android 依赖，回归见 tests/CornerHandleGeometryTest.cpp。

#include <cmath>

#include "ResizeGesture.h"   // ResizeSpring / ClampResize（阻尼弹簧，可中途打断与反向）

namespace aurora {

// 弧度/角度换算（本地一份，避免和别处的宏撞名）。
inline constexpr float kHandlePi = 3.14159265358979323846f;
inline constexpr float kHandleDeg2Rad = kHandlePi / 180.0f;

// ---------------------------------------------------------------------------
//  几何参数：默认值就是"参考图的形状比例"，只是把长度量换成了 dp / 圆角半径比例
// ---------------------------------------------------------------------------
struct CornerHandleParams {
    // 线宽 / 圆角半径。参考图实测 ≈0.45；这里取 0.40 —— 参考图是圆形磁贴（圆角=半径），
    // 本窗口的圆角只占窗口很小一角，同比例会显得偏重，0.40 是"同样饱满但不笨"的值。
    float thicknessRatio = 0.40f;
    // 线宽绝对上下限（dp）：窗口很小/很大、密度不同时都不失控。
    float thicknessMinDp = 8.0f;
    float thicknessMaxDp = 16.0f;
    // 弧内缘与窗口边缘之间的间距 / 圆角半径（参考图里弧外缘几乎贴着磁贴边缘）。
    float gapRatio = 0.08f;
    // 角跨度（度）。参考图 ≈108°；这里 96° —— 更紧凑，也更贴合"四分之一圆弧"的要求。
    float spanDeg = 96.0f;
    // 弧的中心方向（度，屏幕坐标：+x 向右、+y 向下 ⇒ 45° 就是右下对角线）。
    float centerDeg = 45.0f;

    // ---- 展开动画的几何修正（progress 0 → 1）----------------------------
    // 「沿圆角轮廓自然生长」：不是整体放大，而是**跨度从圆角处往外长**，
    // 同时半径略微向外推、线宽略微变饱满。三者一起动，看起来才是"长出来"。
    //
    // ★ 这几个比例是**真机录屏抽帧量出来**再调的（research/handle_redesign/
    //   anim_timing.py + retract_montage.png）：
    //   第一版 growSpanFrom=0.45 / alphaFullAt=0.55 时，收起过程主要是"变淡"
    //   （形状几乎不缩，亮度一路降下去）—— 看起来像淡出，不像"沿原路缩回去"。
    //   现在把几何缩得更彻底（跨度收到 16%、线宽收到 55%），
    //   并且把"完全不透明"的门槛降到 0.28 ⇒ 收起时**先沿轮廓缩成一个小弯钩，
    //   最后才快速淡掉**；展开时反过来：小弯钩先出现，再一路长满。
    float growSpanFrom = 0.16f;     // 起始跨度比例
    float growRadiusFrom = 0.92f;   // 起始半径比例
    float growThickFrom = 0.55f;    // 起始线宽比例
    float alphaFullAt = 0.28f;      // progress 到这里就完全不透明（之后再长形状）
};

// ---------------------------------------------------------------------------
//  几何结果：宿主只拿它去画，不含任何动画状态
// ---------------------------------------------------------------------------
struct CornerHandleGeometry {
    float cx = 0.0f;          // 弧心（= 窗口右下圆角的圆心）
    float cy = 0.0f;
    float radius = 0.0f;      // 中心线半径
    float thickness = 0.0f;   // 线宽
    float a0Deg = 0.0f;       // 起始角（屏幕坐标，角度增大 = 顺时针）
    float a1Deg = 0.0f;       // 结束角
    float alpha = 0.0f;       // 0..1
    bool visible = false;
};

// 画弧需要多少段：按"弦高误差 ≤ maxSagittaPx"算（比固定段数更稳：
// 半径大时自动加密，半径小时不浪费顶点）。AA 之外还有这层几何精度，
// 圆弧边缘才不会有可见的多边形棱。
inline int ArcSegmentCount(float radiusPx, float sweepRad, float maxSagittaPx) {
    if (radiusPx <= 0.01f || sweepRad <= 0.0f || maxSagittaPx <= 0.0f) return 2;
    const float step = std::sqrt(8.0f * maxSagittaPx / radiusPx);
    int n = static_cast<int>(std::ceil(sweepRad / (step > 1e-4f ? step : 1e-4f)));
    if (n < 3) n = 3;
    if (n > 96) n = 96;
    return n;
}

inline float SmoothStep01(float x) {
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    return x * x * (3.0f - 2.0f * x);
}

// ---------------------------------------------------------------------------
//  核心：由**窗口实时几何** + 圆角半径 + 动画进度 算出这条弧
//
//  窗口矩形每一帧都可能变（用户正在缩放 / 拖动 / 屏幕旋转），所以这里不接受
//  "缓存的右下角坐标" —— 只接受当前矩形，保证手柄永远贴着当前圆角。
// ---------------------------------------------------------------------------
inline CornerHandleGeometry ComputeCornerHandle(float winLeft, float winTop,
                                                float winRight, float winBottom,
                                                float cornerRadiusPx, float pxPerDp,
                                                const CornerHandleParams& p,
                                                float progress) {
    CornerHandleGeometry g;
    if (!(winRight > winLeft) || !(winBottom > winTop)) return g;
    if (!(pxPerDp > 0.0f)) pxPerDp = 1.0f;

    const float pr = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    if (pr <= 0.0f) return g;                       // 完全收起 ⇒ 一个顶点都不产生

    // 圆角半径不能超过窗口短边的一半（窗口被缩到极小时仍要画得出来）。
    const float halfShort = 0.5f * ((winRight - winLeft) < (winBottom - winTop)
                                    ? (winRight - winLeft) : (winBottom - winTop));
    float radiusCorner = cornerRadiusPx;
    if (radiusCorner > halfShort) radiusCorner = halfShort;
    if (radiusCorner < 0.0f) radiusCorner = 0.0f;

    // 线宽：与圆角半径成比例，再夹到 dp 上下限。
    float thickness = p.thicknessRatio * radiusCorner;
    const float tMin = p.thicknessMinDp * pxPerDp;
    const float tMax = p.thicknessMaxDp * pxPerDp;
    thickness = ClampResize(thickness, tMin, tMax);

    const float gap = p.gapRatio * radiusCorner;
    const float span = p.spanDeg * (p.growSpanFrom + (1.0f - p.growSpanFrom) * pr);
    const float growR = p.growRadiusFrom + (1.0f - p.growRadiusFrom) * pr;
    const float growT = p.growThickFrom + (1.0f - p.growThickFrom) * pr;

    g.cx = winRight - radiusCorner;
    g.cy = winBottom - radiusCorner;
    g.radius = (radiusCorner + gap + thickness * 0.5f) * growR;
    g.thickness = thickness * growT;
    g.a0Deg = p.centerDeg - span * 0.5f;
    g.a1Deg = p.centerDeg + span * 0.5f;
    g.alpha = SmoothStep01(pr / (p.alphaFullAt > 0.01f ? p.alphaFullAt : 0.01f));
    g.visible = pr > 0.0f && g.radius > g.thickness * 0.5f;
    return g;
}

// ---------------------------------------------------------------------------
//  激活反馈：整个窗口的液态玻璃边框"加厚 + 提亮 + 增强折射"
//
//  用户要求（逐条对应）：
//   · 长按达阈值成功进入缩放状态时，**四周边框同步加厚**，保持原有圆角轮廓；
//   · 非线性弹簧、不瞬间切换；
//   · 边框略微提高亮度、边缘高光和折射强度 —— 明确但克制；
//   · **只改视觉**：窗口实际宽高、内容布局都不许变；
//   · 缩放期间维持，松手后平滑恢复；
//   · 厚度/亮度/折射强度都是**可配置动画参数**，不分散硬编码。
//
//  ★ 实现方式：这些量全部落在 ytbl 的 `PanelStyle`（edge light / optics / inset）上，
//    面板每帧重建 style ⇒ 改参数不影响窗口几何，也天然是"同一套圆角 SDF"，
//    不会出现直角、断裂、突兀白描边或矩形遮罩。
//  ★ 为什么不是"画一条更粗的白色描边"：那会破坏液态玻璃的折射/边光配方，
//    用户明确要求"不破坏原有的液态玻璃 Shader、模糊、折射和高光"。
// ---------------------------------------------------------------------------
struct ResizeActivationParams {
    // ---- 边框（EdgeLight）----
    // 窗口常态 edge.widthDp = 0.5dp（见 secondary_imgui.cpp 的 windowStyle）⇒
    // 激活时 +1.5dp = 2.0dp，肉眼是"明显变厚"但不糊成一圈白边。
    float edgeWidthAddDp = 1.5f;
    float edgeSoftnessAddDp = 0.30f;    // 柔边一起加，避免厚边显得"硬"
    // 边光颜色里那个 alpha 字节（常态 0x80）的乘性增益 ⇒ 0xC0，边缘高光更亮。
    float edgeTintAlphaGain = 1.5f;
    // ---- 折射 / 光学（克制）----
    float lensHeightGain = 1.30f;       // 折射高度
    float lensAmountGain = 1.25f;       // 折射强度
    float dispersionGain = 1.35f;       // 色散
    // ---- 内侧柔边（让"变厚"读得出来：外圈更亮 + 内圈一圈极淡的暗边）----
    bool insetOn = true;
    float insetReachAddDp = 2.2f;
    float insetAlpha = 0.10f;
    // ---- 均匀激活描边（★ 只有它才能做到"**四周**同步加厚"）----
    //  为什么需要它：ytbl 的 EdgeLight::gradient() 是 **45° 方向光** ——
    //  真机实测（诊断模式关掉背景采样后的四边剖面）：
    //      右边亮带 2px / 下边峰值 +78.7 灰阶，而上边与左边**完全没有可见边光**
    //      （剖面恒等于底色 15.0）。
    //  也就是说只调 gradient 的话，用户看到的会是"只有右下两条边变厚"。
    //  所以激活时在原配方**之上**再叠一层：同样的圆角 SDF、均匀边光（plain）、
    //  没有背景采样/模糊/投影 ⇒ 四条边一起加厚发亮，且不改变原玻璃配方一个参数。
    float rimWidthDp = 1.1f;        // 激活描边的额外宽度（dp）
    float rimAlpha = 0.55f;         // 激活描边的最大不透明度（克制）
    // ---- 弹簧（与手柄共用同一个进度，见下）----
    float springStiffness = 420.0f;
    float springDamping = 34.0f;        // ζ≈0.83
};

// 某进度下要往 PanelStyle 上施加的增量/增益（纯数值，宿主负责映射到 ytbl 类型）。
struct ResizeActivationStyle {
    float edgeWidthAddDp = 0.0f;
    float edgeSoftnessAddDp = 0.0f;
    float edgeTintAlphaGain = 1.0f;     // 乘性：1.0 = 不变
    float lensHeightGain = 1.0f;
    float lensAmountGain = 1.0f;
    float dispersionGain = 1.0f;
    float insetReachAddDp = 0.0f;
    float insetAlpha = 0.0f;
    bool insetOn = false;
    float rimWidthDp = 0.0f;            // 均匀激活描边的额外宽度（0 = 不画这层）
    float rimAlpha = 0.0f;
    float progress = 0.0f;
};

inline ResizeActivationStyle ComputeResizeActivation(const ResizeActivationParams& p,
                                                     float progress) {
    ResizeActivationStyle out;
    const float t = progress < 0.0f ? 0.0f : (progress > 1.0f ? 1.0f : progress);
    out.progress = t;
    out.edgeWidthAddDp = p.edgeWidthAddDp * t;
    out.edgeSoftnessAddDp = p.edgeSoftnessAddDp * t;
    out.edgeTintAlphaGain = 1.0f + (p.edgeTintAlphaGain - 1.0f) * t;
    out.lensHeightGain = 1.0f + (p.lensHeightGain - 1.0f) * t;
    out.lensAmountGain = 1.0f + (p.lensAmountGain - 1.0f) * t;
    out.dispersionGain = 1.0f + (p.dispersionGain - 1.0f) * t;
    out.insetReachAddDp = p.insetReachAddDp * t;
    out.insetAlpha = p.insetAlpha * t;
    out.insetOn = p.insetOn && t > 0.001f;
    out.rimWidthDp = p.rimWidthDp * t;
    out.rimAlpha = p.rimAlpha * t;
    return out;
}

// ---------------------------------------------------------------------------
//  统一的"缩放激活状态"（放在 CornerHandleAnimation 之后，因为它要用到那个弹簧）
//
//  ★ 用户要求：手柄展开、边框加厚、震动这三件事必须由**同一个状态转换**驱动，
//    不许各自计时。这里的做法是：
//      · 状态本身 = 长按状态机的 Active()（ResizeHold）；
//      · 一个弹簧进度 progress 同时喂给**手柄几何**与**边框样式**（严格同步）；
//      · 震动由 HapticGate 在同一个转换上放行一次（不参与动画）。
// ---------------------------------------------------------------------------

// 画这条弧最多需要多少个点（宿主按它准备缓冲；够用即可，超了会被裁剪）。
inline constexpr int kCornerHandleMaxPoints = 4 * 96 + 8;

// ---------------------------------------------------------------------------
//  轮廓：把"月牙"（外弧 + 端帽半圆 + 内弧 + 端帽半圆）生成成一个**闭合多边形**
//
//  返回点数；每个点写两个 float（x, y）。绕向全程一致（屏幕坐标下顺时针），
//  所以宿主可以直接用 ImDrawList::AddConcavePolyFilled() 一次填充 ——
//  单一图元 ⇒ 没有折线接头、没有端帽叠色、AA 由 ImGui 的 fringe 一次算好。
// ---------------------------------------------------------------------------
inline int BuildCornerHandleOutline(const CornerHandleGeometry& g, float* outXY,
                                    int capacityFloats) {
    if (outXY == nullptr || !g.visible) return 0;
    const float ro = g.radius + g.thickness * 0.5f;
    const float ri = g.radius - g.thickness * 0.5f;
    if (ri <= 0.5f) return 0;

    const float a0 = g.a0Deg * kHandleDeg2Rad;
    const float a1 = g.a1Deg * kHandleDeg2Rad;
    const float sweep = a1 - a0;
    if (sweep <= 0.0f) return 0;

    // 几何精度：弦高误差 ≤ 0.15px（肉眼不可辨），端帽用同一角步长。
    const int segArc = ArcSegmentCount(ro, sweep, 0.15f);
    const int segCap = ArcSegmentCount(g.thickness * 0.5f, kHandlePi, 0.15f);
    const int need = (segArc + 1) * 2 + (segCap - 1) * 2;
    if (capacityFloats < need * 2) return 0;

    const float half = g.thickness * 0.5f;
    const float ca0 = std::cos(a0), sa0 = std::sin(a0);
    const float ca1 = std::cos(a1), sa1 = std::sin(a1);
    // 端帽圆心 = 中心线在端点上的位置
    const float tip0x = g.cx + ca0 * g.radius, tip0y = g.cy + sa0 * g.radius;
    const float tip1x = g.cx + ca1 * g.radius, tip1y = g.cy + sa1 * g.radius;

    int n = 0;
    auto push = [&](float x, float y) {
        outXY[n * 2 + 0] = x;
        outXY[n * 2 + 1] = y;
        ++n;
    };

    // ① 外弧 a0 → a1
    for (int i = 0; i <= segArc; ++i) {
        const float a = a0 + sweep * (static_cast<float>(i) / static_cast<float>(segArc));
        push(g.cx + std::cos(a) * ro, g.cy + std::sin(a) * ro);
    }
    // ② 末端圆帽：绕端点从 a1 转到 a1+180°（落在内弧起点上）
    for (int i = 1; i < segCap; ++i) {
        const float a = a1 + kHandlePi * (static_cast<float>(i) / static_cast<float>(segCap));
        push(tip1x + std::cos(a) * half, tip1y + std::sin(a) * half);
    }
    // ③ 内弧 a1 → a0
    for (int i = 0; i <= segArc; ++i) {
        const float a = a1 - sweep * (static_cast<float>(i) / static_cast<float>(segArc));
        push(g.cx + std::cos(a) * ri, g.cy + std::sin(a) * ri);
    }
    // ④ 起点圆帽：绕起点从 a0+180° 转到 a0+360°
    for (int i = 1; i < segCap; ++i) {
        const float a = a0 + kHandlePi + kHandlePi * (static_cast<float>(i) / static_cast<float>(segCap));
        push(tip0x + std::cos(a) * half, tip0y + std::sin(a) * half);
    }
    return n;
}

// ---------------------------------------------------------------------------
//  动画：展开进度 0 → 1 的阻尼弹簧
//
//  为什么不是线性插值 / 不是"透明度突变"：
//    · 弹簧的**速度是连续的** ⇒ 中途反向（收起一半又按下）能平滑接续，
//      不会出现"重新从头弹一遍"的跳帧；
//    · 阻尼比 ζ≈0.85 ⇒ 收尾干脆、几乎没有可见过冲（用户明确不要夸张弹跳）；
//    · dt 分步（Step）⇒ 掉帧时不会数值发散，120Hz 下时间步长也是对的。
// ---------------------------------------------------------------------------
struct CornerHandleAnimation {
    ResizeSpring reveal;
    // k=420 / c=34 ⇒ ζ≈0.83：约 250ms 到位、几乎看不到过冲（用户不要夸张弹跳）。
    // ★ 300/29（≈380ms）在真机录屏里偏慢（抽帧量到展开用了 ~380ms），调到这一组。
    float stiffness = 420.0f;
    float damping = 34.0f;      // ζ = c / (2√k) ≈ 0.83
    bool initialized = false;

    void Reset(float value = 0.0f) {
        reveal.Snap(value);          // 值/速度/目标一起归位（"回到某处并停住"）
        initialized = true;
    }
    void SetTarget(float target) { reveal.SetTarget(target); }

    // 返回当前进度（0=完全收起，1=完全展开）
    // ★ 首次调用只初始化**值与速度**，绝不动 target ——
    //   否则"先 SetTarget(1) 再 Update"的第一帧会把目标冲回 0，
    //   弹簧会先停一帧（离线回归抓到过：那一帧 Settled() 竟为真）。
    float Update(float dtSeconds) {
        if (!initialized) {
            reveal.value = 0.0f;
            reveal.velocity = 0.0f;
            initialized = true;
        }
        return reveal.Step(dtSeconds, stiffness, damping);
    }
    float Progress() const { return reveal.value; }
    bool Settled() const {
        return std::fabs(reveal.target - reveal.value) < 0.001f
            && std::fabs(reveal.velocity) < 0.01f;
    }
};

// ---------------------------------------------------------------------------
//  统一的"缩放激活状态"
//
//  ★ 用户要求：手柄展开、边框加厚、震动这三件事必须由**同一个状态转换**驱动，
//    不许各自计时。这里的做法是：
//      · 状态本身 = 长按状态机的 Active()（ResizeHold）；
//      · 一个弹簧进度 progress 同时喂给**手柄几何**与**边框样式**（严格同步）；
//      · 震动由 HapticGate 在同一个转换上放行一次（不参与动画）。
// ---------------------------------------------------------------------------
struct ResizeActivation {
    CornerHandleAnimation anim;   // 唯一的进度来源（手柄与边框共用）
    bool active = false;

    // 每帧推进一次：返回本帧的进度（0=完全未激活，1=完全激活）
    float Step(bool nowActive, float dtSeconds) {
        active = nowActive;
        anim.SetTarget(active ? 1.0f : 0.0f);
        return anim.Update(dtSeconds);
    }
    float Progress() const { return anim.Progress(); }
};

}  // namespace aurora
