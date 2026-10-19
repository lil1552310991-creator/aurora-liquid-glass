// ===========================================================================
//  PanelGestures.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  用户要求（二级窗口的滚动与拖动）里，能被"离线证明"的部分全在这里：
//    · 点击 vs 拖动靠**移动阈值**区分（标签点击不能被拖动吃掉）
//    · 归属优先级：标签栏拖动 > 组件 > 空白滚动（一次定终身，不许中途改）
//    · 滚动夹紧：到顶/到底就停住，不许越界继续跑
//    · 惯性：衰减、撞边界立刻清零、掉帧保护
//    · 速度采样：低通，不被单帧抖动带飞
//  真正需要真机的部分（ImGui 命中、跟手、组件不被抢）在设备上验收。
// ===========================================================================

#include "PanelGestures.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::PanelGesture;
using aurora::PanelScrollInertia;

static int g_groups = 0;

static void TestThreshold() {
    const float touch = aurora::PanelGestureThreshold(true);
    const float mouse = aurora::PanelGestureThreshold(false);
    assert(touch > mouse);            // 手指比鼠标抖，阈值必须更宽
    assert(touch >= 18.0f);

    // 阈值内 = 还是点击（标签切换必须保留）
    assert(!aurora::PanelGestureExceeds(0.0f, 0.0f, touch));
    assert(!aurora::PanelGestureExceeds(3.0f, -4.0f, touch));      // 5px
    assert(!aurora::PanelGestureExceeds(touch - 0.5f, 0.0f, touch));
    // 达到/超过阈值 = 升级成拖动
    assert(aurora::PanelGestureExceeds(touch, 0.0f, touch));
    assert(aurora::PanelGestureExceeds(0.0f, -touch, touch));
    assert(aurora::PanelGestureExceeds(30.0f, 30.0f, touch));
    // 斜向也算（欧氏距离，不是"只看某一个轴"）
    const float diag = touch * 0.75f;
    assert(aurora::PanelGestureExceeds(diag, diag, touch));
    ++g_groups;
}

static void TestOwnershipPriority() {
    // ★★ 顺序是"组件优先"（与有标题栏的时代相反，见 PanelGestures.h 的注释）：
    //    二级窗口现在没有标题栏，拖动把手是压在可滚动内容最上面的一条，
    //    若拖动优先，滑到那一条的进度条/开关就会被窗口拖动抢走触摸。
    assert(aurora::ResolvePanelGesture(true, true, true) == PanelGesture::Component);
    assert(aurora::ResolvePanelGesture(true, true, false) == PanelGesture::Component);
    // 组件之外才是拖动条（空白处按在顶部 ⇒ 拖窗口）
    assert(aurora::ResolvePanelGesture(true, false, true) == PanelGesture::WindowDrag);
    // 非拖动条、非组件 ⇒ 滚动
    assert(aurora::ResolvePanelGesture(false, false, true) == PanelGesture::Scroll);
    // 窗口外 = 不接管
    assert(aurora::ResolvePanelGesture(false, false, false) == PanelGesture::None);
    ++g_groups;
}

static void TestScrollClamp() {
    // 内容比窗口短 ⇒ 永远停在顶部（不能出现"负滚动"）
    assert(aurora::ClampPanelScroll(50.0f, 0.0f) == 0.0f);
    assert(aurora::ClampPanelScroll(-10.0f, -5.0f) == 0.0f);
    // 正常区间原样返回
    assert(aurora::ClampPanelScroll(120.0f, 400.0f) == 120.0f);
    // 到顶/到底夹住，**不越界**
    assert(aurora::ClampPanelScroll(-30.0f, 400.0f) == 0.0f);
    assert(aurora::ClampPanelScroll(430.0f, 400.0f) == 400.0f);
    ++g_groups;
}

static void TestInertiaDecay() {
    PanelScrollInertia inertia;
    assert(!inertia.Active());
    assert(inertia.Step(1.0f / 120.0f, 0.0f, 1000.0f) == 0.0f);   // 没速度就不动

    inertia.Start(1200.0f);            // 1200 px/s 向下滚
    assert(inertia.Active());
    const float first = inertia.Step(1.0f / 120.0f, 0.0f, 1000.0f);
    assert(first > 0.0f);
    assert(first < 1200.0f / 120.0f + 1e-3f);   // 一帧最多 velocity*dt

    // 越走越慢，最终停下
    float scroll = first;
    int frames = 0;
    while (inertia.Active() && frames < 1000) {
        scroll += inertia.Step(1.0f / 120.0f, scroll, 100000.0f);
        ++frames;
    }
    assert(!inertia.Active());
    assert(frames < 600);              // 不能永远滑下去
    ++g_groups;
}

static void TestInertiaStopsAtEdge() {
    PanelScrollInertia inertia;
    inertia.Start(2000.0f);
    // 已经贴着底边 ⇒ 立刻清零，且这一帧位移为 0（不允许越界继续跑）
    const float moved = inertia.Step(1.0f / 120.0f, 500.0f, 500.0f);
    assert(moved == 0.0f);
    assert(!inertia.Active());

    // 顶部同理
    inertia.Start(-2000.0f);
    assert(inertia.Step(1.0f / 120.0f, 0.0f, 500.0f) == 0.0f);
    assert(!inertia.Active());

    // 滑到底边时被夹住 ⇒ 停在边界，速度清零
    inertia.Start(6000.0f);
    const float near = inertia.Step(1.0f / 120.0f, 495.0f, 500.0f);
    assert(near > 0.0f && near <= 5.0f + 1e-3f);
    assert(!inertia.Active());
    ++g_groups;
}

static void TestInertiaFrameDropGuard() {
    PanelScrollInertia inertia;
    inertia.Start(1000.0f);
    // 一帧卡了 2 秒：必须按 0.1s 封顶，不能"一帧跳 2000px"
    const float moved = inertia.Step(2.0f, 0.0f, 100000.0f);
    assert(moved > 0.0f);
    assert(moved <= 1000.0f * 0.1f + 1e-3f);
    ++g_groups;
}

static void TestVelocitySmoothing() {
    // 首次采样直接给 dt 内的瞬时速度
    const float v1 = aurora::UpdatePanelVelocity(0.0f, 10.0f, 0.01f);   // 1000 px/s
    assert(v1 > 0.0f && v1 < 1000.0f);        // 低通：不会一步到位
    // 持续同向滑动 ⇒ 逼近瞬时速度
    float v = v1;
    for (int i = 0; i < 60; ++i) v = aurora::UpdatePanelVelocity(v, 10.0f, 0.01f);
    assert(std::fabs(v - 1000.0f) < 50.0f);
    // 单帧抖动不会把速度带飞（dt 非法时保持原值）
    assert(aurora::UpdatePanelVelocity(v, 9999.0f, 0.0f) == v);
    assert(aurora::UpdatePanelVelocity(v, 9999.0f, 5.0f) == v);
    ++g_groups;
}

int main() {
    TestThreshold();
    TestOwnershipPriority();
    TestScrollClamp();
    TestInertiaDecay();
    TestInertiaStopsAtEdge();
    TestInertiaFrameDropGuard();
    TestVelocitySmoothing();
    std::printf("PanelGesturesTest: %d regression groups passed\n", g_groups);
    return 0;
}
