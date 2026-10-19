// ===========================================================================
//  ResizePreview.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  用户对「虚线预览缩放 + 液态玻璃填充动画」的要求，能离线证明的都在这里：
//    · 两种模式的定义与状态机六个状态（IDLE / PRESS_PENDING / RESIZE_ACTIVE /
//      PREVIEW_DRAG / COMMIT_ANIMATING / RECOVERING）的分类规则；
//    · 虚线是**按弧长**排布的圆角矩形轮廓（圆角处不会变短 ⇒ 不会毛糙）、
//      容量不足时安全截断、半径为 0 时退化成矩形；
//    · 松手后的弹簧过渡：用真实帧时间推进、不同帧率同一结果、到位后**精确**
//      等于预览尺寸（"动画结束时新尺寸与预览尺寸一致"）、兜底时限不会状态锁死；
//    · 填充包络：新区域的"未定型"强度与边缘流动都从 1 → 0 单调收敛，
//      且松手瞬间边缘流动为 0（不许硬跳）；
//    · 虚线框解体：几何走到阈值之前保持实心，到位时正好消失（不许"松手就消失"）；
//    · 填充前沿几何：只有贴着移动前沿的一条带、两条带互不重叠、
//      前沿刚越过老边界时内边缘夹在老边界上（不许伸进已定型的玻璃）。
// ===========================================================================

#include "ResizePreview.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

using aurora::DashSegment;
using aurora::ResizeFillEnvelope;
using aurora::ResizeMode;
using aurora::ResizePreviewParams;
using aurora::ResizeSessionState;

static int g_groups = 0;

// ---------------------------------------------------------------------------
// 模式与状态机
// ---------------------------------------------------------------------------
static void TestModeDefaults() {
    // 默认必须是实时缩放（用户要求"默认保留当前实时缩放模式"）
    aurora::ResizeSettings& settings = aurora::ResizeSettingsRef();
    assert(settings.mode == static_cast<int>(ResizeMode::Live));
    assert(aurora::CurrentResizeMode() == ResizeMode::Live);
    assert(std::string(aurora::ResizeModeName(ResizeMode::Live)) == "实时缩放");
    assert(std::string(aurora::ResizeModeName(ResizeMode::DashedPreview)) == "虚线预览缩放");

    // 越界/垃圾值必须回落到实时缩放（不许出现第三种语义）
    settings.mode = 99;
    assert(aurora::CurrentResizeMode() == ResizeMode::Live);
    settings.mode = -3;
    assert(aurora::CurrentResizeMode() == ResizeMode::Live);
    settings.mode = static_cast<int>(ResizeMode::DashedPreview);
    assert(aurora::CurrentResizeMode() == ResizeMode::DashedPreview);
    settings.mode = static_cast<int>(ResizeMode::Live);
    ++g_groups;
}

static void TestSessionStateMachine() {
    using aurora::ClassifyResizeSession;
    const ResizeMode live = ResizeMode::Live;
    const ResizeMode preview = ResizeMode::DashedPreview;

    // IDLE：没按、没动画、恢复弹簧也归零了
    assert(ClassifyResizeSession(false, false, false, false, 0.0f, live)
           == ResizeSessionState::Idle);
    // PRESS_PENDING：按住了但还没到阈值（还在数时间）
    assert(ClassifyResizeSession(true, false, false, false, 0.0f, live)
           == ResizeSessionState::PressPending);
    // RESIZE_ACTIVE：长按达成
    assert(ClassifyResizeSession(true, true, false, false, 1.0f, live)
           == ResizeSessionState::ResizeActive);
    // 模式 A 拖动时仍然是 RESIZE_ACTIVE（实时改尺寸）
    assert(ClassifyResizeSession(true, true, true, false, 1.0f, live)
           == ResizeSessionState::ResizeActive);
    // 模式 B 拖动 ⇒ PREVIEW_DRAG
    assert(ClassifyResizeSession(true, true, true, false, 1.0f, preview)
           == ResizeSessionState::PreviewDrag);
    // COMMIT_ANIMATING：松手后的过渡动画（哪怕恢复弹簧还有值也必须优先）
    assert(ClassifyResizeSession(false, false, false, true, 1.0f, preview)
           == ResizeSessionState::CommitAnimating);
    // RECOVERING：动画结束、手柄/边框还在收回
    assert(ClassifyResizeSession(false, false, false, false, 0.5f, preview)
           == ResizeSessionState::Recovering);
    // 名字表齐全（日志与验收都按它念）
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::Idle)) == "IDLE");
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::PressPending))
           == "PRESS_PENDING");
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::ResizeActive))
           == "RESIZE_ACTIVE");
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::PreviewDrag))
           == "PREVIEW_DRAG");
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::CommitAnimating))
           == "COMMIT_ANIMATING");
    assert(std::string(aurora::ResizeSessionStateName(ResizeSessionState::Recovering))
           == "RECOVERING");
    ++g_groups;
}

// ---------------------------------------------------------------------------
// 虚线：圆角矩形轮廓
// ---------------------------------------------------------------------------
static float SegmentLength(const DashSegment& s) {
    const float dx = s.x1 - s.x0;
    const float dy = s.y1 - s.y0;
    return std::sqrt(dx * dx + dy * dy);
}

// 点是否落在圆角矩形的**轮廓**上（到轮廓的距离 ≤ tol）
static bool OnOutline(float x, float y, float l, float t, float r, float b, float radius,
                      float tol) {
    // 圆角矩形的有符号距离：>0 在外面，<0 在里面
    const float halfW = 0.5f * (r - l);
    const float halfH = 0.5f * (b - t);
    const float cx = 0.5f * (l + r);
    const float cy = 0.5f * (t + b);
    const float qx = std::fabs(x - cx) - (halfW - radius);
    const float qy = std::fabs(y - cy) - (halfH - radius);
    const float outside = std::sqrt(std::fmax(qx, 0.0f) * std::fmax(qx, 0.0f)
                                    + std::fmax(qy, 0.0f) * std::fmax(qy, 0.0f));
    const float inside = std::fmin(std::fmax(qx, qy), 0.0f);
    const float sdf = outside + inside - radius;
    return std::fabs(sdf) <= tol;
}

static void TestDashOutlineStaysOnPath() {
    DashSegment segs[aurora::kMaxDashSegments];
    const float l = 100.0f, t = 50.0f, r = 700.0f, b = 550.0f, radius = 56.0f;
    const float dash = 16.0f, gap = 10.0f;
    const int count = aurora::BuildDashedRoundedRect(l, t, r, b, radius, dash, gap, 0.0f, segs,
                                                     aurora::kMaxDashSegments);
    assert(count > 8);                       // 一圈当然不止几段
    for (int i = 0; i < count; ++i) {
        // 每个端点都必须**落在轮廓上**（±0.35px：圆弧是按弦细分的，
        // 弦到弧的距离就是那个 0.12px 的弦高误差 + 浮点）
        assert(OnOutline(segs[i].x0, segs[i].y0, l, t, r, b, radius, 0.35f));
        assert(OnOutline(segs[i].x1, segs[i].y1, l, t, r, b, radius, 0.35f));
        // 单段长度不超过"虚线段长度"（圆弧细分只会更短），也不出现零长段
        assert(SegmentLength(segs[i]) <= dash + 1.0e-3f);
        assert(SegmentLength(segs[i]) > 0.0f);
    }
    ++g_groups;
}

// 虚线的总长度 ≈ 周长 × 占空比（± 一段），这证明"按弧长排布"是真的
static void TestDashDutyCycle() {
    DashSegment segs[aurora::kMaxDashSegments];
    const float l = 100.0f, t = 50.0f, r = 700.0f, b = 550.0f, radius = 56.0f;
    const float dash = 16.0f, gap = 10.0f;
    const int count = aurora::BuildDashedRoundedRect(l, t, r, b, radius, dash, gap, 0.0f, segs,
                                                     aurora::kMaxDashSegments);
    float total = 0.0f;
    for (int i = 0; i < count; ++i) total += SegmentLength(segs[i]);
    const float w = r - l, h = b - t;
    const float perimeter = 2.0f * (w - 2.0f * radius) + 2.0f * (h - 2.0f * radius)
                          + 2.0f * aurora::resize_detail::kResizePi * radius;
    const float expected = perimeter * (dash / (dash + gap));
    assert(std::fabs(total - expected) < dash + gap);
    ++g_groups;
}

// 半径 0 ⇒ 退化成矩形四边；半径为负/超大都要安全
static void TestDashDegenerateRadii() {
    DashSegment segs[aurora::kMaxDashSegments];
    const int square = aurora::BuildDashedRoundedRect(0.0f, 0.0f, 200.0f, 100.0f, 0.0f,
                                                      10.0f, 6.0f, 0.0f, segs,
                                                      aurora::kMaxDashSegments);
    assert(square > 4);
    for (int i = 0; i < square; ++i) {
        // 直角矩形：所有点都在四条边上
        const bool onEdge = std::fabs(segs[i].x0) < 0.01f || std::fabs(segs[i].x0 - 200.0f) < 0.01f
                         || std::fabs(segs[i].y0) < 0.01f || std::fabs(segs[i].y0 - 100.0f) < 0.01f;
        assert(onEdge);
    }
    // 半径远超短边 ⇒ 夹到短边一半（= 胶囊），不能越界/NaN
    const int capsule = aurora::BuildDashedRoundedRect(0.0f, 0.0f, 200.0f, 100.0f, 500.0f,
                                                       10.0f, 6.0f, 0.0f, segs,
                                                       aurora::kMaxDashSegments);
    assert(capsule > 4);
    for (int i = 0; i < capsule; ++i) {
        assert(std::isfinite(segs[i].x0) && std::isfinite(segs[i].y0));
        assert(segs[i].x0 >= -0.5f && segs[i].x0 <= 200.5f);
        assert(segs[i].y0 >= -0.5f && segs[i].y0 <= 100.5f);
    }
    // 负半径按 0 处理（不返回垃圾）
    const int negative = aurora::BuildDashedRoundedRect(0.0f, 0.0f, 200.0f, 100.0f, -30.0f,
                                                        10.0f, 6.0f, 0.0f, segs,
                                                        aurora::kMaxDashSegments);
    assert(negative > 4);
    // 退化矩形（宽或高为 0）⇒ 不画
    assert(aurora::BuildDashedRoundedRect(0.0f, 0.0f, 0.0f, 100.0f, 4.0f, 10.0f, 6.0f, 0.0f,
                                          segs, aurora::kMaxDashSegments) == 0);
    ++g_groups;
}

// 容量不足时必须安全截断（宿主缓冲固定大小，绝不能越界写）
static void TestDashCapacitySafety() {
    DashSegment small[8];
    for (int i = 0; i < 8; ++i) small[i] = DashSegment{-1.0f, -1.0f, -1.0f, -1.0f};
    const int written = aurora::BuildDashedRoundedRect(0.0f, 0.0f, 400.0f, 300.0f, 20.0f,
                                                       8.0f, 8.0f, 0.0f, small, 8);
    assert(written <= 8);
    for (int i = 0; i < written; ++i) assert(small[i].x0 >= -1.0f);
    // 容量 0 / 空指针都不许崩
    assert(aurora::BuildDashedRoundedRect(0.0f, 0.0f, 400.0f, 300.0f, 20.0f, 8.0f, 8.0f, 0.0f,
                                          small, 0) == 0);
    assert(aurora::BuildDashedRoundedRect(0.0f, 0.0f, 400.0f, 300.0f, 20.0f, 8.0f, 8.0f, 0.0f,
                                          nullptr, 8) == 0);
    // 非法虚线长度（<=0）⇒ 不画（避免死循环）
    assert(aurora::BuildDashedRoundedRect(0.0f, 0.0f, 400.0f, 300.0f, 20.0f, 0.0f, 8.0f, 0.0f,
                                          small, 8) == 0);
    ++g_groups;
}

// 相位 0 ⇒ 第一段虚线正好从"上边左端"（左上角圆弧结束处）开始
static void TestDashPhaseAnchor() {
    DashSegment segs[aurora::kMaxDashSegments];
    const float l = 100.0f, t = 50.0f, r = 700.0f, b = 550.0f, radius = 56.0f;
    const int count = aurora::BuildDashedRoundedRect(l, t, r, b, radius, 16.0f, 10.0f, 0.0f, segs,
                                                     aurora::kMaxDashSegments);
    assert(count > 0);
    assert(std::fabs(segs[0].x0 - (l + radius)) < 0.01f);
    assert(std::fabs(segs[0].y0 - t) < 0.01f);
    // 起点固定在左上角 ⇒ 窗口变尺寸时虚线不会整体游走
    const int bigger = aurora::BuildDashedRoundedRect(l, t, r + 120.0f, b + 80.0f, radius, 16.0f,
                                                      10.0f, 0.0f, segs, aurora::kMaxDashSegments);
    assert(bigger > 0);
    assert(std::fabs(segs[0].x0 - (l + radius)) < 0.01f);
    assert(std::fabs(segs[0].y0 - t) < 0.01f);
    ++g_groups;
}

static void TestPreviewCornerRadius() {
    ResizePreviewParams p;
    // 与窗口圆角一致（增量 0）
    assert(aurora::PreviewCornerRadius(600.0f, 500.0f, 56.0f, p, 2.0f) == 56.0f);
    // 增量按密度换算
    p.cornerRadiusAddDp = 8.0f;
    assert(aurora::PreviewCornerRadius(600.0f, 500.0f, 56.0f, p, 2.0f) == 72.0f);
    // 负增量也不许出现负半径
    p.cornerRadiusAddDp = -100.0f;
    assert(aurora::PreviewCornerRadius(600.0f, 500.0f, 56.0f, p, 2.0f) == 0.0f);
    // 窗口被缩到极小时夹到短边一半（与外壳同一条规则）
    p.cornerRadiusAddDp = 0.0f;
    assert(aurora::PreviewCornerRadius(80.0f, 300.0f, 56.0f, p, 2.0f) == 40.0f);
    ++g_groups;
}

// ---------------------------------------------------------------------------
// 松手后的提交动画
// ---------------------------------------------------------------------------
static void TestCommitGeometryExact() {
    ResizePreviewParams p;
    aurora::ResizeCommitAnim anim;
    // 长按之后没拖（尺寸没变）⇒ 不进入动画
    assert(!anim.Start(760.0f, 700.0f, 760.0f, 700.0f, p));
    assert(!anim.active);
    // 非法尺寸 ⇒ 拒绝
    assert(!anim.Start(760.0f, 700.0f, 0.0f, 700.0f, p));
    assert(!anim.Start(0.0f, 0.0f, 500.0f, 500.0f, p));

    // 放大：起点就是**真实窗口当前尺寸**（预览期间它一直没变）
    assert(anim.Start(760.0f, 700.0f, 1000.0f, 900.0f, p));
    assert(anim.active);
    assert(anim.CurrentWidth() == 760.0f);      // 第一帧还没推进 ⇒ 仍是旧尺寸
    assert(anim.CurrentHeight() == 700.0f);
    int frames = 0;
    while (anim.Step(1.0f / 120.0f) && frames < 2000) ++frames;
    assert(frames < 2000);                      // 一定会结束（不许状态锁死）
    // ★ 结束时**精确**等于预览尺寸（不是"差一点点"）
    assert(!anim.active);
    assert(anim.CurrentWidth() == 1000.0f);
    assert(anim.CurrentHeight() == 900.0f);
    assert(anim.Progress() == 1.0f);
    // 默认参数下到位时间落在用户给的初始调试目标 300–450ms 内
    assert(anim.settleMs >= 250.0f && anim.settleMs <= 520.0f);
    ++g_groups;
}

// 缩小走的是同一条路径（不是另一套逻辑），并且同样精确落地
static void TestCommitShrinkSamePath() {
    ResizePreviewParams p;
    aurora::ResizeCommitAnim anim;
    assert(anim.Start(500.0f, 700.0f, 350.0f, 450.0f, p));
    int frames = 0;
    float minWidth = 1.0e9f;
    float maxWidth = -1.0e9f;
    while (anim.Step(1.0f / 120.0f) && frames < 2000) {
        const float w = anim.CurrentWidth();
        assert(w >= 349.0f && w <= 501.0f);      // 不许跑到区间外面去（过冲也要很轻微）
        if (w < minWidth) minWidth = w;
        if (w > maxWidth) maxWidth = w;
        ++frames;
    }
    assert(frames < 2000);
    assert(anim.CurrentWidth() == 350.0f);
    assert(anim.CurrentHeight() == 450.0f);
    assert(minWidth <= 350.0f + p.settleEpsilonPx + 0.5f);   // 真的走到目标附近（≤1px）
    assert(maxWidth <= 501.0f);
    ++g_groups;
}

// ★ 必须用**实际帧时间**推进：不同帧率、同样长的真实时间 ⇒ 走到同一个位置
static void TestCommitTimeStepInvariance() {
    ResizePreviewParams p;
    aurora::ResizeCommitAnim fast, slow;
    assert(fast.Start(400.0f, 400.0f, 800.0f, 800.0f, p));
    assert(slow.Start(400.0f, 400.0f, 800.0f, 800.0f, p));
    for (int i = 0; i < 60; ++i) fast.Step(1.0f / 240.0f);    // 250ms @240Hz
    for (int i = 0; i < 15; ++i) slow.Step(1.0f / 60.0f);     // 250ms @60Hz
    assert(std::fabs(fast.CurrentWidth() - slow.CurrentWidth()) < 12.0f);
    // 掉帧（一帧 200ms）也不能发散
    aurora::ResizeCommitAnim jumpy;
    assert(jumpy.Start(400.0f, 400.0f, 800.0f, 800.0f, p));
    jumpy.Step(0.2f);
    assert(std::isfinite(jumpy.CurrentWidth()));
    assert(jumpy.CurrentWidth() <= 800.0f + 120.0f);
    ++g_groups;
}

// 兜底时限：把刚度调到极小（正常参数根本走不完）也必须结束
static void TestCommitTimeoutGuard() {
    ResizePreviewParams p;
    p.springStiffness = 20.0f;      // 极慢
    p.springDamping = 120.0f;       // 过阻尼 ⇒ 几乎不前进
    p.maxDurationMs = 200.0f;
    aurora::ResizeCommitAnim anim;
    assert(anim.Start(500.0f, 500.0f, 900.0f, 900.0f, p));
    int frames = 0;
    while (anim.Step(1.0f / 120.0f) && frames < 2000) ++frames;
    assert(frames < 2000);
    assert(anim.forced);            // 明确标记"这次是被兜底时限收尾的"
    assert(anim.CurrentWidth() == 900.0f);
    // dt = 0 / 负数 / NaN 都不许把状态搞坏
    assert(anim.Start(500.0f, 500.0f, 700.0f, 700.0f, p));
    assert(anim.Step(0.0f));
    assert(anim.Step(-1.0f));
    assert(std::isfinite(anim.CurrentWidth()));
    anim.Reset();
    assert(!anim.active);
    assert(!anim.Step(1.0f / 120.0f));       // 没在跑的时候 Step 返回 false
    ++g_groups;
}

// ---------------------------------------------------------------------------
// 填充包络
// ---------------------------------------------------------------------------
static void TestFillEnvelope() {
    const ResizeFillEnvelope start = aurora::ComputeResizeFill(0.0f);
    assert(start.geom == 0.0f);
    assert(std::fabs(start.fill - 1.0f) < 1.0e-6f);       // 刚出现 ⇒ 最"未定型"
    assert(start.edgeFlow == 0.0f);                       // ★ 松手瞬间不硬跳
    const ResizeFillEnvelope mid = aurora::ComputeResizeFill(0.5f);
    assert(mid.fill > 0.0f && mid.fill < 1.0f);
    assert(mid.edgeFlow > 0.5f);                          // 中段边缘流动最强
    const ResizeFillEnvelope end = aurora::ComputeResizeFill(1.0f);
    assert(end.fill == 0.0f);                             // 完全定型
    assert(end.edgeFlow == 0.0f);
    // 单调不增（否则会看到"又糊了一次"）
    float lastFill = 2.0f;
    for (int i = 0; i <= 40; ++i) {
        const ResizeFillEnvelope e = aurora::ComputeResizeFill(static_cast<float>(i) / 40.0f);
        assert(e.fill <= lastFill + 1.0e-6f);
        lastFill = e.fill;
        assert(e.fill >= 0.0f && e.fill <= 1.0f);
        assert(e.edgeFlow >= 0.0f && e.edgeFlow <= 1.0f);
    }
    // 异常输入（NaN / 越界）按"已完成"处理，不许卡住
    assert(aurora::ComputeResizeFill(std::nanf("")).fill == 0.0f);
    assert(aurora::ComputeResizeFill(-5.0f).fill == 1.0f);
    assert(aurora::ComputeResizeFill(9.0f).fill == 0.0f);
    ++g_groups;
}

static void TestPreviewDissolve() {
    ResizePreviewParams p;      // previewDissolveFrom = 0.55
    // 几何走到阈值之前**保持实心**（用户明确禁止"松手就消失"）
    assert(aurora::PreviewDissolveAlpha(0.0f, p) == 1.0f);
    assert(aurora::PreviewDissolveAlpha(0.55f, p) == 1.0f);
    // 之后平滑淡出，到位时正好为 0
    const float a = aurora::PreviewDissolveAlpha(0.8f, p);
    assert(a > 0.0f && a < 1.0f);
    assert(aurora::PreviewDissolveAlpha(1.0f, p) == 0.0f);
    // 单调不增
    float last = 2.0f;
    for (int i = 0; i <= 40; ++i) {
        const float v = aurora::PreviewDissolveAlpha(static_cast<float>(i) / 40.0f, p);
        assert(v <= last + 1.0e-6f);
        last = v;
    }
    // 参数被调坏（>=1）时回落到默认阈值，仍然会消失
    p.previewDissolveFrom = 5.0f;
    assert(aurora::PreviewDissolveAlpha(1.0f, p) == 0.0f);
    assert(aurora::PreviewDissolveAlpha(0.5f, p) == 1.0f);
    ++g_groups;
}

// ---------------------------------------------------------------------------
// 填充前沿几何
// ---------------------------------------------------------------------------
static void TestRimsGrowXOnly() {
    // 老窗口 (100,100)-(600,500)，当前 (100,100)-(800,500)：只往右长
    const aurora::ResizeRims rims =
        aurora::ComputeResizeRims(100.0f, 100.0f, 700.0f, 400.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(rims.growingX && !rims.growingY);
    assert(!rims.shrinkingX && !rims.shrinkingY);
    assert(rims.right.valid);
    assert(!rims.bottom.valid);
    // 带贴在**右前沿**上，宽度正好是 rimWidth（前沿已经走过 rimWidth 之后）
    assert(rims.right.right == 800.0f);
    assert(std::fabs((rims.right.right - rims.right.left) - 44.0f) < 1.0e-3f);
    assert(rims.right.top == 100.0f && rims.right.bottom == 500.0f);
    // 只有贴着窗口真实轮廓的两个角有圆角
    assert(rims.right.cornerTopRight == 56.0f);
    assert(rims.right.cornerBottomRight == 56.0f);
    assert(rims.right.cornerTopLeft == 0.0f);
    assert(rims.right.cornerBottomLeft == 0.0f);
    // 内边缘绝不允许伸进老窗口内部（那里是已经定型的玻璃）
    assert(rims.right.left >= 600.0f - 1.0e-3f);
    ++g_groups;
}

static void TestRimsClampAtSeam() {
    // 前沿刚越过老边界一点点（增长 10px < rimWidth 44）⇒ 内边缘夹在老边界上
    const aurora::ResizeRims rims =
        aurora::ComputeResizeRims(100.0f, 100.0f, 510.0f, 400.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(rims.growingX);
    assert(rims.right.valid);
    assert(rims.right.left == 600.0f);      // = 老右边，不是 610-44=566
    assert(rims.right.right == 610.0f);
    assert(rims.right.right - rims.right.left == 10.0f);
    ++g_groups;
}

static void TestRimsNoOverlap() {
    // 同时在右、下两个方向长大
    const aurora::ResizeRims rims =
        aurora::ComputeResizeRims(100.0f, 100.0f, 700.0f, 600.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(rims.growingX && rims.growingY);
    assert(rims.right.valid && rims.bottom.valid);
    // 横向带负责右下角 ⇒ 竖向带在它内侧收住，两块不重叠
    assert(rims.bottom.right <= rims.right.left + 1.0e-3f);
    assert(rims.bottom.cornerBottomRight == 0.0f);
    // 竖向带自己的左下角是窗口真实圆角
    assert(rims.bottom.cornerBottomLeft == 56.0f);
    // 横向带覆盖了整个右下角区域（x 到前沿、y 到下前沿）
    assert(rims.right.right == 800.0f);
    assert(rims.right.bottom == 700.0f);
    assert(rims.bottom.bottom == 700.0f);
    // 竖向带的内边缘也不许伸进老窗口
    assert(rims.bottom.top >= 500.0f - 1.0e-3f);
    ++g_groups;
}

static void TestRimsGrowYOnlyAndShrink() {
    // 只往下长：竖向带要自己带右下角圆角（没有横向带替它负责）
    const aurora::ResizeRims down =
        aurora::ComputeResizeRims(100.0f, 100.0f, 500.0f, 600.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(!down.growingX && down.growingY);
    assert(!down.right.valid && down.bottom.valid);
    assert(down.bottom.cornerBottomLeft == 56.0f);
    assert(down.bottom.cornerBottomRight == 56.0f);
    assert(down.bottom.right == 600.0f);            // 窗口右边

    // 缩小：没有"新玻璃"要填，两条带都不该出现（缩小的光学过渡由边缘流动负责）
    const aurora::ResizeRims shrink =
        aurora::ComputeResizeRims(100.0f, 100.0f, 350.0f, 300.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(shrink.shrinkingX && shrink.shrinkingY);
    assert(!shrink.growingX && !shrink.growingY);
    assert(!shrink.right.valid && !shrink.bottom.valid);

    // 一维放大一维缩小：只有放大的那一维有前沿
    const aurora::ResizeRims mixed =
        aurora::ComputeResizeRims(100.0f, 100.0f, 700.0f, 300.0f, 500.0f, 400.0f, 56.0f, 44.0f);
    assert(mixed.growingX && mixed.shrinkingY);
    assert(mixed.right.valid && !mixed.bottom.valid);
    ++g_groups;
}

// ---------------------------------------------------------------------------
// 参考时长与数值标签
// ---------------------------------------------------------------------------
static void TestEstimateDuration() {
    ResizePreviewParams p;
    const float ms = aurora::EstimateCommitDurationMs(p);
    // 用户给的初始调试目标是 300–450ms
    assert(ms >= 300.0f && ms <= 450.0f);
    // 刚度与阻尼**同比例**放大（ζ 不变）⇒ 更快。
    //   ★ 只加大刚度而不动阻尼的话，ζ 会跟着变小，而尾部包络的衰减率是 ζω = c/2
    //     —— 时间其实不变。这条正是"调参要一起看"的地方，写进断言免得误判。
    {
        ResizePreviewParams same = p;
        same.springStiffness = 900.0f;
        assert(std::fabs(aurora::EstimateCommitDurationMs(same) - ms) < 1.0f);
    }
    p.springStiffness = 900.0f;
    p.springDamping = 0.94f * 2.0f * std::sqrt(900.0f);
    assert(aurora::EstimateCommitDurationMs(p) < ms);
    // 参数被调坏也不能返回 0 / NaN / 负数
    p.springStiffness = 0.0f;
    p.springDamping = 0.0f;
    const float fallback = aurora::EstimateCommitDurationMs(p);
    assert(std::isfinite(fallback) && fallback > 1.0f);
    ++g_groups;
}

static void TestSizeLabelFormat() {
    char label[64];
    aurora::FormatPreviewSizeLabel(520.0f, 640.0f, label, static_cast<int>(sizeof(label)));
    assert(std::string(label) == "520 x 640");
    aurora::FormatPreviewSizeLabel(0.0f, 0.0f, label, static_cast<int>(sizeof(label)));
    assert(std::string(label) == "0 x 0");
    // 缓冲区为空 / 容量 0 不许崩
    aurora::FormatPreviewSizeLabel(1.0f, 2.0f, nullptr, 16);
    aurora::FormatPreviewSizeLabel(1.0f, 2.0f, label, 0);
    ++g_groups;
}

int main() {
    TestModeDefaults();
    TestSessionStateMachine();
    TestDashOutlineStaysOnPath();
    TestDashDutyCycle();
    TestDashDegenerateRadii();
    TestDashCapacitySafety();
    TestDashPhaseAnchor();
    TestPreviewCornerRadius();
    TestCommitGeometryExact();
    TestCommitShrinkSamePath();
    TestCommitTimeStepInvariance();
    TestCommitTimeoutGuard();
    TestFillEnvelope();
    TestPreviewDissolve();
    TestRimsGrowXOnly();
    TestRimsClampAtSeam();
    TestRimsNoOverlap();
    TestRimsGrowYOnlyAndShrink();
    TestEstimateDuration();
    TestSizeLabelFormat();
    std::printf("ResizePreviewTest: %d regression groups passed\n", g_groups);
    return 0;
}
