// ===========================================================================
//  ResizeGesture.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  用户对"长按缩放"的要求，能离线证明的都在这里：
//    · 450ms 才激活；未达阈值松手 ⇒ 不激活（不能误触）
//    · **按住时长用真实毫秒**（不是 DeltaTime 累加）⇒ 帧率/掉帧不影响阈值
//    · 长按期间轻微位移允许，超出容差 ⇒ 取消
//    · 判定区之外起手 ⇒ 永远不激活（普通点击/滚动不触发缩放）
//    · 激活后**不用松手**就能继续拖
//    · 单帧坐标跳变（首帧陈旧位置）⇒ 重新同步，不许把窗口撑到屏幕外
//    · 尺寸夹紧（最小/最大/屏幕余量），左上角固定（只改尺寸不改位置）
//    · 阻尼弹簧：可中途打断、可反向、最终收敛、掉帧不炸、时间步长正确
// ===========================================================================

#include "ResizeGesture.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ResizeHold;using aurora::ResizeHoldParams;
using aurora::ResizePhase;

static int g_groups = 0;

static const float kTol = 40.0f;        // 14dp × 3 = 42px，取 40 便于算
static const float kRebase = 200.0f;    // 120dp × 1.67 ≈ 200px

static void TestLongPressTiming() {
    ResizeHoldParams p;
    // 用户指定的统一常量：120ms（且只有一个来源 LONG_PRESS_TRIGGER_MS）
    // 沿革：450 → 330（"太慢了"）→ 150（"响应再快一点"）→ 120（本轮用户指定）。
    // ★ 两种缩放方式（实时 / 虚线预览）共用这一个阈值。
    assert(aurora::LONG_PRESS_TRIGGER_MS == 120.0f);
    assert(p.longPressMs == aurora::LONG_PRESS_TRIGGER_MS);
    assert(p.zoneDp >= 32.0f && p.zoneDp <= 48.0f);

    ResizeHold h;
    // heldMs 是"自按下起算的真实毫秒"：每帧 +10ms，120ms 必须恰好激活
    bool armed = false;
    float held = 0.0f;
    int frames = 0;
    while (frames < 200) {
        const bool just = h.Update(true, true, 100.0f, 100.0f, held, p, kTol, kRebase);
        ++frames;
        if (just) { armed = true; break; }
        held += 10.0f;
        assert(!h.Active());
    }
    assert(armed);
    assert(h.phase == ResizePhase::Armed);
    // 第一帧只是起手，第 13 帧（held = 120）激活 —— 帧数由阈值算出来，
    // 不写死：阈值再改时这条不会变成假失败。
    const int expectedFrames =
        static_cast<int>(aurora::LONG_PRESS_TRIGGER_MS / 10.0f) + 1;
    assert(frames == expectedFrames);
    ++g_groups;
}

// ★ 真机踩过的坑：DeltaTime 被夹在 0.05s，卡顿时长按会变慢（实测按到 ~1.0s 才激活）。
//   现在 heldMs 由宿主用真实时钟给 ⇒ 与帧率无关。
static void TestLongPressIndependentOfFrameRate() {
    ResizeHoldParams p;
    const float frameTimes[5] = {4.0f, 8.3f, 16.7f, 50.0f, 100.0f};
    for (float dtMs : frameTimes) {
        ResizeHold h;
        float held = 0.0f;
        bool armed = false;
        for (int i = 0; i < 1000; ++i) {
            if (h.Update(true, true, 10.0f, 10.0f, held, p, kTol, kRebase)) { armed = true; break; }
            held += dtMs;
        }
        assert(armed);
        assert(held >= p.longPressMs - 1e-3f);
        assert(held <= p.longPressMs + dtMs + 1e-3f);   // 最多晚一帧
    }
    ++g_groups;
}

// 起手晚于按下（先按在别处、后来才滑进判定区）：不许"一进来就立刻激活"
static void TestLateEntryStillWaitsFullDuration() {
    ResizeHoldParams p;
    ResizeHold h;
    float held = 3000.0f;                 // 手指已经在屏上 3 秒了
    assert(!h.Update(true, false, 10.0f, 10.0f, held, p, kTol, kRebase));   // 区外
    assert(h.phase == ResizePhase::Idle);
    assert(!h.Update(true, true, 10.0f, 10.0f, held, p, kTol, kRebase));    // 刚进区：起手
    assert(h.phase == ResizePhase::Pending);
    // 只等到"阈值的一半"仍然不许激活（用阈值本身算，别再写死毫秒数：
    // 阈值从 330 改到 150 时，写死的 300 会变成"已经超时"从而假失败）
    assert(!h.Update(true, true, 10.0f, 10.0f, held + aurora::LONG_PRESS_TRIGGER_MS * 0.5f,
                     p, kTol, kRebase));
    assert(h.phase == ResizePhase::Pending);
    assert(h.Update(true, true, 10.0f, 10.0f, held + aurora::LONG_PRESS_TRIGGER_MS, p, kTol,
                    kRebase));
    ++g_groups;
}

static void TestReleaseBeforeThresholdDoesNotArm() {
    ResizeHoldParams p;
    ResizeHold h;
    const int pendingFrames = static_cast<int>(aurora::LONG_PRESS_TRIGGER_MS / 10.0f) - 1;
    for (int i = 0; i < pendingFrames; ++i) {
        h.Update(true, true, 10.0f, 10.0f, i * 10.0f, p, kTol, kRebase);
    }
    assert(h.phase == ResizePhase::Pending);
    h.Update(false, true, 10.0f, 10.0f, pendingFrames * 10.0f, p, kTol, kRebase);   // 提前松手
    assert(h.phase == ResizePhase::Idle);
    assert(!h.Active());
    ++g_groups;
}

static void TestOutsideZoneNeverArms() {
    ResizeHoldParams p;
    ResizeHold h;
    // 不在判定区里按再久也不激活（普通点击/滚动不能触发缩放）
    for (int i = 0; i < 200; ++i) {
        h.Update(true, false, 50.0f, 50.0f, i * 10.0f, p, kTol, kRebase);
    }
    assert(h.phase == ResizePhase::Idle);
    assert(!h.Active());
    ++g_groups;
}

static void TestMoveTolerance() {
    ResizeHoldParams p;
    ResizeHold h;
    // ★ 帧数由阈值算出来，不写死：阈值从 330 改到 150 时，
    //   "跑 20 帧（=190ms）应该还在 Pending" 这句话就不再成立。
    const int pendingFrames = static_cast<int>(aurora::LONG_PRESS_TRIGGER_MS / 10.0f) - 1;
    float held = 0.0f;
    // 轻微位移（<容差）⇒ 继续计时
    for (int i = 0; i < pendingFrames; ++i) {
        h.Update(true, true, 100.0f + i * 1.0f, 100.0f, held, p, kTol, kRebase);
        held += 10.0f;
    }
    assert(h.phase == ResizePhase::Pending);
    // 超容差 ⇒ 立刻取消
    h.Update(true, true, 100.0f + kTol + 5.0f, 100.0f, held, p, kTol, kRebase);
    assert(h.phase == ResizePhase::Idle);
    // 取消之后即使回到区内也要**重新起手**
    h.Update(true, true, 100.0f, 100.0f, held + 10.0f, p, kTol, kRebase);
    assert(h.phase == ResizePhase::Pending);
    ++g_groups;
}

// ★ 首帧坐标陈旧：跳变被识别为"坐标重新同步"，位移保持 0，不许把窗口撑大
static void TestStaleFirstPositionIsRebased() {
    ResizeHoldParams p;
    ResizeHold h;
    // 第一帧拿到的是上一次的位置 (0,0)
    h.Update(true, true, 0.0f, 0.0f, 0.0f, p, kTol, kRebase);
    assert(h.phase == ResizePhase::Pending);
    // 下一帧真实位置到达：(760,700) —— 跳变 760px ≫ rebase 阈值
    h.Update(true, true, 760.0f, 700.0f, 16.0f, p, kTol, kRebase);
    assert(std::fabs(h.dx()) < 1e-3f);          // ★ 不许变成 760
    assert(std::fabs(h.dy()) < 1e-3f);
    assert(h.phase == ResizePhase::Pending);    // 也不该被当成"移动超容差"而取消
    // 之后手指真实移动 30px：dx 就是 30
    h.Update(true, true, 790.0f, 700.0f, 32.0f, p, kTol, kRebase);
    assert(std::fabs(h.dx() - 30.0f) < 1e-3f);
    ++g_groups;
}

static void TestInvalidPositionIgnored() {
    ResizeHoldParams p;
    ResizeHold h;
    h.Update(true, true, 500.0f, 500.0f, 0.0f, p, kTol, kRebase);
    const float nan = std::nanf("");
    h.Update(true, true, nan, nan, 16.0f, p, kTol, kRebase);     // 无效坐标
    assert(std::fabs(h.dx()) < 1e-3f);                            // 不被污染
    h.Update(true, true, 520.0f, 500.0f, 32.0f, p, kTol, kRebase);
    assert(std::fabs(h.dx() - 20.0f) < 1e-3f);
    ++g_groups;
}

static void TestDragWithoutRelease() {
    ResizeHoldParams p;
    ResizeHold h;
    for (int i = 0; i < 60 && h.phase != ResizePhase::Armed; ++i) {
        h.Update(true, true, 500.0f, 500.0f, i * 10.0f, p, kTol, kRebase);
    }
    assert(h.phase == ResizePhase::Armed);
    // 不松手，直接移动 ⇒ 进入拖动
    h.Update(true, true, 540.0f, 560.0f, 700.0f, p, kTol, kRebase);
    assert(h.phase == ResizePhase::Dragging);
    assert(h.Active());
    assert(std::fabs(h.dx() - 40.0f) < 1e-3f);
    assert(std::fabs(h.dy() - 60.0f) < 1e-3f);
    // 松手 ⇒ 结束
    h.Update(false, true, 540.0f, 560.0f, 720.0f, p, kTol, kRebase);
    assert(h.phase == ResizePhase::Idle);
    ++g_groups;
}

static void TestCornerZone() {
    // 窗口 (16,16)-(776,716)，判定区 88px
    assert(aurora::CornerResizeZoneContains(16, 16, 776, 716, 88, 760, 700));
    assert(aurora::CornerResizeZoneContains(16, 16, 776, 716, 88, 688, 628));   // 边界上
    assert(!aurora::CornerResizeZoneContains(16, 16, 776, 716, 88, 687, 700));  // 区外（左）
    assert(!aurora::CornerResizeZoneContains(16, 16, 776, 716, 88, 760, 627));  // 区外（上）
    assert(!aurora::CornerResizeZoneContains(16, 16, 776, 716, 88, 800, 700));  // 窗口外
    // 窗口比判定区还小时不越界
    assert(aurora::CornerResizeZoneContains(0, 0, 40, 40, 88, 20, 20));
    assert(!aurora::CornerResizeZoneContains(0, 0, 40, 40, 88, -1, 20));
    ++g_groups;
}

static void TestResizeSizeClamp() {
    ResizeHoldParams p;                        // min 320x220dp, max 1600x2200dp
    const float pxPerDp = 2.0f;                // ⇒ 640x440 / 3200x4400
    // 正常跟手
    aurora::ResizeSize s = aurora::ResizeSizeFromDrag(760, 700, 120, 80, p, pxPerDp, 1080, 2376);
    assert(s.width == 880.0f && s.height == 780.0f);
    // 缩到最小
    s = aurora::ResizeSizeFromDrag(760, 700, -5000, -5000, p, pxPerDp, 1080, 2376);
    assert(s.width == 640.0f && s.height == 440.0f);
    // 屏幕余量优先（maxDp 很大也不能超出屏幕）★ 真机上出现过撑到 2347 高的坏状态
    s = aurora::ResizeSizeFromDrag(760, 700, 5000, 5000, p, pxPerDp, 1064, 2360);
    assert(s.width == 1064.0f && s.height == 2360.0f);
    // 屏幕余量比最小尺寸还小时，仍然不小于最小尺寸（不能返回越界值）
    s = aurora::ResizeSizeFromDrag(760, 700, 5000, 5000, p, pxPerDp, 100, 100);
    assert(s.width == 640.0f && s.height == 440.0f);
    ++g_groups;
}

static void TestClampResize() {
    assert(aurora::ClampResize(500.0f, 320.0f, 1600.0f) == 500.0f);
    assert(aurora::ClampResize(10.0f, 320.0f, 1600.0f) == 320.0f);
    assert(aurora::ClampResize(9999.0f, 320.0f, 1600.0f) == 1600.0f);
    // 反向区间也要安全（不返回越界值）
    assert(aurora::ClampResize(100.0f, 500.0f, 300.0f) == 500.0f);
    ++g_groups;
}

static void TestSpringConverges() {
    aurora::ResizeSpring s;
    s.Snap(0.0f);
    s.SetTarget(1.0f);
    float last = 0.0f;
    int frames = 0;
    bool overshot = false;
    while (frames < 600) {
        last = s.Step(1.0f / 120.0f, 260.0f, 22.0f);
        if (last > 1.0f + 1e-4f) overshot = true;      // 有回弹（过头）
        if (last == 1.0f && s.velocity == 0.0f) break;
        ++frames;
    }
    assert(last == 1.0f);          // 最终精确收敛
    assert(frames < 300);          // 不能永远晃
    assert(overshot);              // 阻尼弹簧应有轻微回弹（不是线性插值）
    ++g_groups;
}

static void TestSpringInterruptible() {
    aurora::ResizeSpring s;
    s.Snap(0.0f);
    s.SetTarget(1.0f);
    for (int i = 0; i < 20; ++i) s.Step(1.0f / 120.0f, 260.0f, 22.0f);
    const float mid = s.value;
    assert(mid > 0.0f && mid < 1.0f);
    // 中途反向（收起）
    s.SetTarget(0.0f);
    for (int i = 0; i < 400; ++i) s.Step(1.0f / 120.0f, 260.0f, 22.0f);
    assert(s.value == 0.0f);
    // 掉帧（一帧 1 秒）也不能发散成 NaN/无穷
    s.SetTarget(1.0f);
    const float v = s.Step(1.0f, 260.0f, 22.0f);
    assert(std::isfinite(v));
    assert(v <= 1.5f);
    ++g_groups;
}

// ★ 时间步长正确性：同样长的真实时间、不同帧率，弹簧走到同一个位置
static void TestSpringTimeStepInvariance() {
    aurora::ResizeSpring fast, slow;
    fast.Snap(0.0f);
    slow.Snap(0.0f);
    fast.SetTarget(1.0f);
    slow.SetTarget(1.0f);
    for (int i = 0; i < 24; ++i) fast.Step(1.0f / 240.0f, 300.0f, 29.0f);   // 100ms @240Hz
    for (int i = 0; i < 6; ++i) slow.Step(1.0f / 60.0f, 300.0f, 29.0f);     // 100ms @60Hz
    assert(std::fabs(fast.value - slow.value) < 0.02f);
    ++g_groups;
}


// ===========================================================================
//  ★ 长按**不动**绝不能改变尺寸（真机踩出来的）
//
//  主窗口的最小宽度我按 dp 写了 420，心想"420 < 760 没问题"，
//  但密度是 2.0 ⇒ 最小宽度 = 840px，而窗口默认只有 760px
//  ⇒ 用户只是长按了一下右下角、什么都没拖，窗口就从 760 跳到 840。
//
//  这条约束（**最小尺寸必须小于窗口默认尺寸**，且 dp→px 要乘密度）
//  放在这里断言，就不用再上真机才发现。
// ===========================================================================
static void TestLongPressWithoutDragKeepsSize() {
    const float pxPerDp = 2.0f;                 // 本工程 lgx::setScale(2.0f)
    const float kDefaultW = 760.0f;             // 两个窗口的默认宽度
    const float kDefaultH = 1400.0f;            // 主窗口的默认高度

    struct Case { const char* name; aurora::ResizeHoldParams p; };
    const Case cases[] = {
        {"主窗口（调试面板）", aurora::ResizeParamsForPanel()},
        {"二级窗口",           aurora::ResizeParamsForSecondaryWindow()},
    };
    for (const Case& c : cases) {
        // ① 最小尺寸必须**小于**窗口默认尺寸，否则第一次长按就会跳
        assert(c.p.minWidthDp * pxPerDp < kDefaultW);
        assert(c.p.minHeightDp * pxPerDp < kDefaultH);
        // ② 长按不动（dx=dy=0）必须原样返回起始尺寸
        const aurora::ResizeSize same =
            aurora::ResizeSizeFromDrag(kDefaultW, kDefaultH, 0.0f, 0.0f, c.p, pxPerDp, 1064.0f, 2376.0f);
        assert(same.width == kDefaultW);
        assert(same.height == kDefaultH);
    }

    // ③ 反过来：起始尺寸**小于**最小值时必须被抬到最小值（这条是预期行为，
    //    不是 bug —— 它保证窗口不会被缩到看不见）
    const aurora::ResizeHoldParams panel = aurora::ResizeParamsForPanel();
    const aurora::ResizeSize raised =
        aurora::ResizeSizeFromDrag(200.0f, 150.0f, 0.0f, 0.0f, panel, pxPerDp, 1064.0f, 2376.0f);
    assert(raised.width == panel.minWidthDp * pxPerDp);
    assert(raised.height == panel.minHeightDp * pxPerDp);

    // ④ 拖拽方向与夹紧：往右/下拖变大，往左/上拖变小，且不超过屏幕余量
    const aurora::ResizeSize bigger =
        aurora::ResizeSizeFromDrag(kDefaultW, kDefaultH, 120.0f, 80.0f, panel, pxPerDp, 1064.0f, 2376.0f);
    assert(bigger.width == 880.0f);
    assert(bigger.height == 1480.0f);
    const aurora::ResizeSize capped =
        aurora::ResizeSizeFromDrag(kDefaultW, kDefaultH, 9999.0f, 9999.0f, panel, pxPerDp, 1064.0f, 2376.0f);
    assert(capped.width == 1064.0f);            // 屏幕余量优先于 maxWidthDp
    assert(capped.height == 2376.0f);
    ++g_groups;
}

int main() {
    TestLongPressTiming();
    TestLongPressIndependentOfFrameRate();
    TestLateEntryStillWaitsFullDuration();
    TestReleaseBeforeThresholdDoesNotArm();
    TestOutsideZoneNeverArms();
    TestMoveTolerance();
    TestLongPressWithoutDragKeepsSize();
    TestStaleFirstPositionIsRebased();
    TestInvalidPositionIgnored();
    TestDragWithoutRelease();
    TestCornerZone();
    TestResizeSizeClamp();
    TestClampResize();
    TestSpringConverges();
    TestSpringInterruptible();
    TestSpringTimeStepInvariance();
    std::printf("ResizeGestureTest: %d regression groups passed\n", g_groups);
    return 0;
}
