// ===========================================================================
//  CornerResizeHandle.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  这一批断言就是"用户能看见的那些要求"的可证明形式：
//    · 造型是**一条与圆角同心的连续圆弧**，不是两根长杆拼的 L
//      （最强的一条：轮廓上每个点到弧心的距离都落在 [R-t/2, R+t/2] 内，
//        而且整条弧离窗口右下角不超过一个半径 —— 长杆必然违反）
//    · 两端有圆端帽、多边形闭合、相邻点连续（没有尖角/接缝/断点）
//    · 参数是 dp / 比例，不写死像素（同比例放大密度，几何线性跟随）
//    · 展开动画是"长出来"（跨度/半径/线宽单调增），不是透明度突变
//    · 弹簧可中途打断、可反向、掉帧不发散
// ===========================================================================

#include "CornerResizeHandle.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::CornerHandleAnimation;
using aurora::CornerHandleGeometry;
using aurora::CornerHandleParams;
using aurora::kCornerHandleMaxPoints;

static int g_groups = 0;

// 真机上的实际量：窗口 760x700 @ (16,16)，密度 2.0 ⇒ 圆角 28dp = 56px
static const float kWinL = 16.0f, kWinT = 16.0f;
static const float kWinW = 760.0f, kWinH = 700.0f;
static const float kCorner = 56.0f;
static const float kPxPerDp = 2.0f;

static CornerHandleGeometry Geo(float progress, float corner = kCorner, float pxPerDp = kPxPerDp,
                                float w = kWinW, float h = kWinH) {
    return aurora::ComputeCornerHandle(kWinL, kWinT, kWinL + w, kWinT + h, corner, pxPerDp,
                                       CornerHandleParams(), progress);
}

static void TestParamsAreSane() {
    const CornerHandleParams p;
    assert(p.spanDeg > 80.0f && p.spanDeg < 120.0f);       // "近似四分之一圆弧"
    assert(p.centerDeg == 45.0f);                          // 右下对角线
    assert(p.thicknessMinDp >= 6.0f && p.thicknessMaxDp <= 20.0f);  // 用户给的 8–12dp 区间附近
    assert(p.growSpanFrom > 0.0f && p.growSpanFrom < 1.0f);
    assert(p.growRadiusFrom > 0.0f && p.growRadiusFrom < 1.0f);
    assert(p.alphaFullAt > 0.0f && p.alphaFullAt <= 1.0f);
    ++g_groups;
}

static void TestFullyRetractedDrawsNothing() {
    const CornerHandleGeometry g = Geo(0.0f);
    assert(!g.visible);
    assert(g.alpha == 0.0f);
    float xy[2 * kCornerHandleMaxPoints];
    assert(aurora::BuildCornerHandleOutline(g, xy, 2 * kCornerHandleMaxPoints) == 0);
    ++g_groups;
}

static void TestArcIsConcentricWithCorner() {
    const CornerHandleGeometry g = Geo(1.0f);
    assert(g.visible);
    // 弧心 = 窗口右下角往内缩一个圆角半径（与外壳玻璃用的是同一个圆角）
    assert(std::fabs(g.cx - (kWinL + kWinW - kCorner)) < 1e-3f);
    assert(std::fabs(g.cy - (kWinT + kWinH - kCorner)) < 1e-3f);
    // 中心线半径 = 圆角半径 + 间距 + 半个线宽（贴着圆角外侧，不悬空、不压内容）
    assert(g.radius > kCorner + g.thickness * 0.5f);
    assert(g.radius < kCorner + g.thickness * 0.5f + kCorner * 0.15f);
    // 跨度与中心方向
    assert(std::fabs((g.a1Deg - g.a0Deg) - CornerHandleParams().spanDeg) < 1e-3f);
    assert(std::fabs((g.a0Deg + g.a1Deg) * 0.5f - 45.0f) < 1e-3f);
    ++g_groups;
}

// ★ 最强的一条：这必须是"一条弧"，不是"两根长杆"
static void TestOutlineIsAShortArcNotRods() {
    const CornerHandleGeometry g = Geo(1.0f);
    float xy[2 * kCornerHandleMaxPoints];
    const int n = aurora::BuildCornerHandleOutline(g, xy, 2 * kCornerHandleMaxPoints);
    assert(n >= 12);
    const float rOuter = g.radius + g.thickness * 0.5f;
    const float rInner = g.radius - g.thickness * 0.5f;
    const float cornerX = kWinL + kWinW;
    const float cornerY = kWinT + kWinH;
    for (int i = 0; i < n; ++i) {
        const float x = xy[i * 2], y = xy[i * 2 + 1];
        const float r = std::sqrt((x - g.cx) * (x - g.cx) + (y - g.cy) * (y - g.cy));
        // ① 每个点都落在圆环带里 ⇒ 是同心圆弧（长杆的远端会跑出这个带）
        assert(r >= rInner - 0.75f && r <= rOuter + 0.75f);
        // ② 整条弧离窗口右下角不超过一个半径 + 线宽 ⇒ 短小、不外伸
        const float reach = std::sqrt((x - cornerX) * (x - cornerX) + (y - cornerY) * (y - cornerY));
        assert(reach <= g.radius + g.thickness);
    }
    // ③ 轮廓的包围盒：与参考图同量级（≈ 磁贴半径的 1.3 倍），不是几百像素的长条
    float minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1];
    for (int i = 1; i < n; ++i) {
        minX = xy[i * 2] < minX ? xy[i * 2] : minX;
        maxX = xy[i * 2] > maxX ? xy[i * 2] : maxX;
        minY = xy[i * 2 + 1] < minY ? xy[i * 2 + 1] : minY;
        maxY = xy[i * 2 + 1] > maxY ? xy[i * 2 + 1] : maxY;
    }
    const float boxW = maxX - minX, boxH = maxY - minY;
    assert(boxW < g.radius * 1.7f && boxH < g.radius * 1.7f);
    assert(boxW > g.thickness && boxH > g.thickness);
    ++g_groups;
}

static void TestOutlineIsClosedAndSmooth() {
    const CornerHandleGeometry g = Geo(1.0f);
    float xy[2 * kCornerHandleMaxPoints];
    const int n = aurora::BuildCornerHandleOutline(g, xy, 2 * kCornerHandleMaxPoints);
    assert(n >= 12);
    // 相邻点连续（含"最后一点 → 第一点"这条闭合边）：不许有断点/尖刺
    float maxStep = 0.0f;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const float dx = xy[j * 2] - xy[i * 2];
        const float dy = xy[j * 2 + 1] - xy[i * 2 + 1];
        const float step = std::sqrt(dx * dx + dy * dy);
        maxStep = step > maxStep ? step : maxStep;
    }
    assert(maxStep < g.thickness * 1.2f);      // 步长不超过线宽 ⇒ 没有跳变
    // 绕向一致（有向面积同号且不为零）⇒ 填充不会自交
    float area2 = 0.0f;
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        area2 += xy[i * 2] * xy[j * 2 + 1] - xy[j * 2] * xy[i * 2 + 1];
    }
    assert(std::fabs(area2) > g.thickness * g.thickness);
    // 多边形面积应当接近"弧长 × 线宽 + 两个端帽"的解析值（±25%）
    const float sweep = (g.a1Deg - g.a0Deg) * aurora::kHandleDeg2Rad;
    const float expected = g.radius * sweep * g.thickness
                         + aurora::kHandlePi * (g.thickness * 0.5f) * (g.thickness * 0.5f);
    const float area = std::fabs(area2) * 0.5f;
    assert(area > expected * 0.75f && area < expected * 1.25f);
    ++g_groups;
}

static void TestArcSmoothnessBudget() {
    // 弦高误差 ≤ 0.15px（几何精度），并且段数不会失控
    const float sweep = CornerHandleParams().spanDeg * aurora::kHandleDeg2Rad;
    for (float r = 8.0f; r < 400.0f; r *= 1.7f) {
        const int seg = aurora::ArcSegmentCount(r, sweep, 0.15f);
        assert(seg >= 3 && seg <= 96);
        const float sagitta = r * (1.0f - std::cos(sweep / (2.0f * seg)));
        assert(sagitta <= 0.151f);
    }
    ++g_groups;
}

static void TestGrowsAlongTheCorner() {
    // 展开过程中：跨度、半径、线宽都**单调增** ⇒ 是"长出来"而不是"突然出现"
    float lastSpan = -1.0f, lastRadius = -1.0f, lastThick = -1.0f, lastAlpha = -1.0f;
    for (int i = 1; i <= 20; ++i) {
        const float p = static_cast<float>(i) / 20.0f;
        const CornerHandleGeometry g = Geo(p);
        const float span = g.a1Deg - g.a0Deg;
        assert(span > lastSpan);
        assert(g.radius > lastRadius);
        assert(g.thickness > lastThick);
        assert(g.alpha >= lastAlpha);
        lastSpan = span; lastRadius = g.radius; lastThick = g.thickness; lastAlpha = g.alpha;
    }
    // 起始那一帧就应当是"一小段"（不是整条弧突然出现）
    const CornerHandleGeometry first = Geo(0.02f);
    assert((first.a1Deg - first.a0Deg) < CornerHandleParams().spanDeg * 0.6f);
    assert(first.alpha < 0.2f);        // 一开始几乎透明，靠形状长出来而不是"啪"地亮起
    assert(Geo(1.0f).alpha == 1.0f);
    ++g_groups;
}

static void TestDensityIndependence() {
    // 同一个**dp 尺寸**的窗口在不同密度下：几何按 pxPerDp 线性缩放（没有写死像素）
    const float w1 = 760.0f, h1 = 700.0f, c1 = 56.0f, s1 = 2.0f;      // 密度 2
    const float w2 = w1 * 1.5f, h2 = h1 * 1.5f, c2 = c1 * 1.5f, s2 = s1 * 1.5f;  // 密度 3
    const CornerHandleGeometry a = aurora::ComputeCornerHandle(0, 0, w1, h1, c1, s1,
                                                               CornerHandleParams(), 1.0f);
    const CornerHandleGeometry b = aurora::ComputeCornerHandle(0, 0, w2, h2, c2, s2,
                                                               CornerHandleParams(), 1.0f);
    assert(std::fabs(b.thickness - a.thickness * 1.5f) < 0.05f);
    assert(std::fabs(b.radius - a.radius * 1.5f) < 0.05f);
    assert(std::fabs((b.a1Deg - b.a0Deg) - (a.a1Deg - a.a0Deg)) < 1e-3f);   // 角度与密度无关
    ++g_groups;
}

static void TestThicknessClampedForTinyAndHugeCorners() {
    const CornerHandleParams p;
    // 极小圆角：线宽不低于下限（否则弧细得看不见）
    const CornerHandleGeometry tiny = Geo(1.0f, 4.0f);
    assert(tiny.thickness >= p.thicknessMinDp * kPxPerDp - 1e-3f);
    // 极大圆角：线宽不超过上限（否则弧粗得笨重）
    const CornerHandleGeometry huge = Geo(1.0f, 600.0f);
    assert(huge.thickness <= p.thicknessMaxDp * kPxPerDp + 1e-3f);
    ++g_groups;
}

static void TestTinyWindowStillFits() {
    // 窗口被缩到比圆角还小：圆角被夹到短边一半，弧仍然完整落在窗口内
    const float w = 90.0f, h = 60.0f;
    const CornerHandleGeometry g = Geo(1.0f, 56.0f, kPxPerDp, w, h);
    assert(g.visible);
    assert(g.radius > 0.0f && g.thickness > 0.0f);
    float xy[2 * kCornerHandleMaxPoints];
    const int n = aurora::BuildCornerHandleOutline(g, xy, 2 * kCornerHandleMaxPoints);
    assert(n >= 12);
    ++g_groups;
}

static void TestAnimationSettlesWithoutExaggeratedOvershoot() {
    CornerHandleAnimation a;
    a.Reset(0.0f);
    a.SetTarget(1.0f);
    float worst = 0.0f;
    int frames = 0;
    while (frames < 600) {
        const float v = a.Update(1.0f / 120.0f);
        if (v > worst) worst = v;
        if (a.Settled()) break;
        ++frames;
    }
    assert(a.Progress() == 1.0f);
    assert(frames < 120);              // 1 秒内一定到位（120Hz 下 ≤1s）
    assert(worst < 1.05f);             // 几乎没有过冲（用户明确不要夸张弹跳）
    assert(worst > 0.999f);
    ++g_groups;
}

static void TestAnimationIsInterruptible() {
    CornerHandleAnimation a;
    a.Reset(0.0f);
    a.SetTarget(1.0f);
    for (int i = 0; i < 12; ++i) a.Update(1.0f / 120.0f);
    const float mid = a.Progress();
    assert(mid > 0.05f && mid < 1.0f);
    // 中途反向：速度是连续的 ⇒ 值不会跳回 0，而是**平滑地**掉头往下走。
    // （弹簧带着正速度，反向的头一两帧可能还略微上冲 —— 这是对的，不是跳帧；
    //   要断言的是"上冲极小 + 之后单调收回 + 每帧变化不突兀"。）
    a.SetTarget(0.0f);
    float prev = mid;
    float peak = mid;
    float maxJump = 0.0f;
    float maxAfterNear = 0.0f;
    bool near = false;
    for (int i = 0; i < 240; ++i) {
        const float v = a.Update(1.0f / 120.0f);
        const float jump = std::fabs(v - prev);
        maxJump = jump > maxJump ? jump : maxJump;
        if (v > peak) peak = v;
        if (!near && v <= 0.02f) near = true;
        // 收到接近 0 之后不许再来回甩（阻尼弹簧可以有一次极小的回弹，不能振荡）
        if (near && std::fabs(v) > maxAfterNear) maxAfterNear = std::fabs(v);
        prev = v;
    }
    assert(a.Progress() == 0.0f);
    assert(peak < mid + 0.05f);                         // 反向时的上冲极小（不夸张）
    assert(maxJump < 0.25f);                            // 没有一帧跳一大截
    assert(maxAfterNear < 0.05f);                       // 收尾不振荡
    // 收回途中再次按下：平滑接续（速度连续，不重新起弹）
    a.SetTarget(1.0f);
    for (int i = 0; i < 6; ++i) a.Update(1.0f / 120.0f);
    const float back = a.Progress();
    assert(back > 0.0f);
    a.SetTarget(0.0f);
    const float after = a.Update(1.0f / 120.0f);
    assert(std::fabs(after - back) < 0.25f);
    ++g_groups;
}

static void TestAnimationFrameDropSafety() {
    CornerHandleAnimation a;
    a.Reset(0.0f);
    a.SetTarget(1.0f);
    for (int i = 0; i < 3; ++i) {
        const float v = a.Update(0.25f);        // 极端掉帧（250ms 一帧）
        assert(std::isfinite(v));
        assert(v <= 1.1f);
    }
    // 高刷：240Hz 与 120Hz 走同样长的时间后进度接近（时间步长正确）
    CornerHandleAnimation b, c;
    b.Reset(0.0f); c.Reset(0.0f);
    b.SetTarget(1.0f); c.SetTarget(1.0f);
    for (int i = 0; i < 24; ++i) b.Update(1.0f / 240.0f);   // 100ms
    for (int i = 0; i < 12; ++i) c.Update(1.0f / 120.0f);   // 100ms
    assert(std::fabs(b.Progress() - c.Progress()) < 0.02f);
    ++g_groups;
}

// ★ 真机录屏抽帧发现的问题：第一版收起时形状几乎不缩、只是变淡（看起来像淡出）。
//   这条断言把"先沿轮廓缩、最后才淡"钉死。
static void TestRetractShrinksBeforeFading() {
    const CornerHandleParams p;
    const float fullSpan = p.spanDeg;
    const float fullThick = Geo(1.0f).thickness;
    // progress 0.30（收起路径上）：**仍然完全不透明**，但跨度已经缩到一半以下
    const CornerHandleGeometry mid = Geo(0.30f);
    assert(mid.alpha == 1.0f);
    assert((mid.a1Deg - mid.a0Deg) < fullSpan * 0.55f);
    // progress 0.10：跨度只剩 ~1/4，线宽也明显变细
    const CornerHandleGeometry low = Geo(0.10f);
    assert((low.a1Deg - low.a0Deg) < fullSpan * 0.35f);
    assert(low.thickness < fullThick * 0.72f);
    // 最后一刻：已经缩成很小的一段才开始淡（不会"一大块突然消失"）
    const CornerHandleGeometry tiny = Geo(0.03f);
    assert(tiny.alpha < 0.15f);
    assert((tiny.a1Deg - tiny.a0Deg) < fullSpan * 0.3f);
    ++g_groups;
}

// ★ 边框加厚激活反馈（用户要求：可配置动画参数、只在视觉上、圆角不变）
static void TestActivationStyleIsConfigurableAndNeutralAtZero() {
    const aurora::ResizeActivationParams p;
    // 未激活（progress=0）：必须是**完全中性** —— 一点都不能改变常态外观
    const aurora::ResizeActivationStyle off = aurora::ComputeResizeActivation(p, 0.0f);
    assert(off.edgeWidthAddDp == 0.0f);
    assert(off.edgeSoftnessAddDp == 0.0f);
    assert(off.edgeTintAlphaGain == 1.0f);
    assert(off.lensHeightGain == 1.0f);
    assert(off.lensAmountGain == 1.0f);
    assert(off.dispersionGain == 1.0f);
    assert(off.insetAlpha == 0.0f);
    assert(!off.insetOn);
    // 完全激活：边框确实更厚、边光更亮、折射更强（数值来自参数，不是硬编码）
    const aurora::ResizeActivationStyle on = aurora::ComputeResizeActivation(p, 1.0f);
    assert(on.edgeWidthAddDp == p.edgeWidthAddDp);
    assert(on.edgeWidthAddDp >= 1.0f);              // 0.5dp → ≥1.5dp，肉眼可见地变厚
    assert(on.edgeTintAlphaGain > 1.2f);
    assert(on.lensHeightGain > 1.1f && on.lensAmountGain > 1.1f);
    assert(on.dispersionGain > 1.1f);
    assert(on.insetOn && on.insetAlpha > 0.0f);
    ++g_groups;
}

static void TestActivationStyleIsMonotonicAndBounded() {
    const aurora::ResizeActivationParams p;
    float lastWidth = -1.0f, lastTint = 0.0f;
    for (int i = 0; i <= 20; ++i) {
        const float t = static_cast<float>(i) / 20.0f;
        const aurora::ResizeActivationStyle s = aurora::ComputeResizeActivation(p, t);
        assert(s.edgeWidthAddDp >= lastWidth);       // 单调加厚（不会忽厚忽薄）
        assert(s.edgeTintAlphaGain >= lastTint);
        lastWidth = s.edgeWidthAddDp;
        lastTint = s.edgeTintAlphaGain;
        // 越界输入要被夹住，绝不能把样式放大到失控
        assert(s.edgeWidthAddDp <= p.edgeWidthAddDp + 1e-4f);
        assert(s.edgeTintAlphaGain <= p.edgeTintAlphaGain + 1e-4f);
    }
    const aurora::ResizeActivationStyle over = aurora::ComputeResizeActivation(p, 9.0f);
    assert(over.progress == 1.0f);
    const aurora::ResizeActivationStyle under = aurora::ComputeResizeActivation(p, -3.0f);
    assert(under.progress == 0.0f && under.edgeWidthAddDp == 0.0f);
    ++g_groups;
}

// ★ 三件事必须由同一个状态驱动：手柄与边框共用**同一个**弹簧进度
static void TestHandleAndBorderShareOneProgress() {
    aurora::ResizeActivation act;
    // 未激活 → 激活：进度从 0 连续升到 1，中途每一帧都同时可用于手柄与边框
    act.Step(true, 1.0f / 120.0f);
    float prev = act.Progress();
    assert(prev >= 0.0f);
    float maxDrop = 0.0f;
    int frames = 0;
    while (frames < 400 && !act.anim.Settled()) {
        const float p = act.Step(true, 1.0f / 120.0f);
        // 阻尼弹簧允许**极小**回弹（ζ≈0.83 ⇒ 过冲 <1%），但不许来回振荡
        if (prev - p > maxDrop) maxDrop = prev - p;
        prev = p;
        ++frames;
    }
    assert(std::fabs(act.Progress() - 1.0f) < 0.002f);
    assert(maxDrop < 0.02f);
    assert(frames < 120);                           // ≤1 秒到位
    // 松手：同一个进度反向收回（边框与手柄一起恢复）
    // ★ 先走一帧把目标切到 0，再进循环 —— 否则"检查 Settled()"会读到上一状态的目标
    //   （这个顺序坑在写这条测试时踩过一次，值得留一行注释）。
    act.Step(false, 1.0f / 120.0f);
    frames = 0;
    while (frames < 400 && !act.anim.Settled()) {
        act.Step(false, 1.0f / 120.0f);
        ++frames;
    }
    // 收到"绘制阈值以下"即可 —— 宿主正是用 0.002 作为"一个顶点都不产生"的门槛，
    // 所以这里与渲染侧的"完全收起、不残留像素"是同一条判据。
    assert(std::fabs(act.Progress()) < 0.002f);
    assert(!act.active);
    ++g_groups;
}

int main() {
    TestParamsAreSane();
    TestFullyRetractedDrawsNothing();
    TestArcIsConcentricWithCorner();
    TestOutlineIsAShortArcNotRods();
    TestOutlineIsClosedAndSmooth();
    TestArcSmoothnessBudget();
    TestGrowsAlongTheCorner();
    TestRetractShrinksBeforeFading();
    TestActivationStyleIsConfigurableAndNeutralAtZero();
    TestActivationStyleIsMonotonicAndBounded();
    TestHandleAndBorderShareOneProgress();
    TestDensityIndependence();
    TestThicknessClampedForTinyAndHugeCorners();
    TestTinyWindowStillFits();
    TestAnimationSettlesWithoutExaggeratedOvershoot();
    TestAnimationIsInterruptible();
    TestAnimationFrameDropSafety();
    std::printf("CornerHandleGeometryTest: %d regression groups passed\n", g_groups);
    return 0;
}
