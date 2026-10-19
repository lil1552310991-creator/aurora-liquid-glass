// ===========================================================================
//  ImeAvoid.h 的离线回归（纯逻辑，零 ImGui / 零 Android 依赖）
//
//  输入法弹层避让看着像"挪一下窗口"这么简单，实际有三个只有算错才会暴露的坑，
//  这一组测试把它们钉死：
//    · **必须**先用"当前实际位置 + 已施加位移"还原逻辑位置，否则每帧都会再叠一次
//      位移 —— 窗口会一秒飞出屏幕（120Hz 下 8ms 一帧、"挪一点点"根本不成立）；
//    · 窗口比"键盘顶边以上的空间"还高时，只能顶到屏幕顶、不能把标题栏推出屏幕；
//    · 键盘收起（imeTop<=0）时必须**回到 0 位移**（回到原位），不能停在上面。
// ===========================================================================

#include "ImeAvoid.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ComputeImeAvoidShift;
using aurora::ImeAvoidWindowY;
using aurora::ImeVisibleAt;

static int g_groups = 0;

// 模拟"多帧连续调用"：与宿主里的用法完全一致（每帧用上一帧的实际位置 + 已施加位移）
static float SimulateFrames(int frames, float baseTop, float height, float imeTop) {
    float posWithShift = baseTop;      // 第 0 帧：还没施加任何位移
    float applied = 0.0f;
    for (int i = 0; i < frames; ++i) {
        const float want = ComputeImeAvoidShift(posWithShift, height, applied, imeTop, 0.0f);
        posWithShift = ImeAvoidWindowY(posWithShift, applied, want);
        applied = want;
    }
    return posWithShift;
}

static void TestNoKeyboardNoShift() {
    // 键盘不可见（imeTop <= 0）⇒ 一律不位移
    assert(!ImeVisibleAt(0.0f, 2376.0f));
    assert(!ImeVisibleAt(-10.0f, 2376.0f));
    assert(ImeVisibleAt(1476.0f, 2376.0f));
    const float noShift = ComputeImeAvoidShift(730.0f, 1400.0f, 0.0f, 0.0f, 0.0f);
    assert(noShift == 0.0f);
    // 窗口本来就在键盘上方 ⇒ 也不位移
    assert(ComputeImeAvoidShift(16.0f, 700.0f, 0.0f, 1476.0f, 0.0f) == 0.0f);
    ++g_groups;
}

// ★ 核心：与键盘重叠时移多少
static void TestShiftAmount() {
    // 主题窗口 (16,730) 高 1400 ⇒ 底边 2130；键盘顶边 1476 ⇒ 需要上移 654
    const float shift = ComputeImeAvoidShift(730.0f, 1400.0f, 0.0f, 1476.0f, 0.0f);
    assert(std::fabs(shift - 654.0f) < 0.01f);
    // 施加之后底边正好落在键盘顶边
    const float y = ImeAvoidWindowY(730.0f, 0.0f, shift);
    assert(std::fabs((y + 1400.0f) - 1476.0f) < 0.01f);
    ++g_groups;
}

// ★★ 多帧稳定性：连续 600 帧下来位置必须收敛，绝不能一路往上飘
static void TestStableAcrossFrames() {
    const float baseTop = 730.0f;
    const float height = 1400.0f;
    const float imeTop = 1476.0f;
    const float settled = SimulateFrames(600, baseTop, height, imeTop);
    const float expected = imeTop - height;              // = 76
    assert(std::fabs(settled - expected) < 0.01f);
    // 再跑很多帧也必须停在那里（幂等）
    float posWithShift = settled;
    float applied = baseTop - settled;
    for (int i = 0; i < 300; ++i) {
        const float want = ComputeImeAvoidShift(posWithShift, height, applied, imeTop, 0.0f);
        assert(std::fabs(want - applied) < 0.01f);        // 每帧算出来的都一样
        posWithShift = ImeAvoidWindowY(posWithShift, applied, want);
        applied = want;
    }
    assert(std::fabs(posWithShift - expected) < 0.01f);
    ++g_groups;
}

// ★ 窗口比"键盘顶边以上的空间"还高 ⇒ 顶到屏幕顶为止（不许把标题栏推出屏幕）
static void TestClampAtScreenTop() {
    // 高 2000 的窗口、底边 2500、键盘顶边 1476 ⇒ 想移 1024，但顶边只能到 0 ⇒ 移 730... 
    // 这里用 y=730 的例子：maxShift = 730 - 0 = 730
    const float shift = ComputeImeAvoidShift(730.0f, 2000.0f, 0.0f, 1476.0f, 0.0f);
    assert(std::fabs(shift - 730.0f) < 0.01f);           // 被屏幕顶夹住，而不是 1024
    const float y = ImeAvoidWindowY(730.0f, 0.0f, shift);
    assert(y >= -0.01f);                                 // 顶边不跑出屏幕
    // 完全在屏幕外的窗口（负 y）也不能算出负位移
    assert(ComputeImeAvoidShift(-400.0f, 1000.0f, 0.0f, 1476.0f, 0.0f) == 0.0f);
    // 屏幕顶不是 0 时（安全区）也要尊重
    const float shifted = ComputeImeAvoidShift(730.0f, 2000.0f, 0.0f, 1476.0f, 100.0f);
    assert(std::fabs(shifted - 630.0f) < 0.01f);
    ++g_groups;
}

// ★ 键盘收起后必须回到原位（否则窗口会永远停在上面）
static void TestRestoresAfterKeyboardHides() {
    const float baseTop = 730.0f;
    const float height = 1400.0f;
    float posWithShift = SimulateFrames(60, baseTop, height, 1476.0f);
    const float applied = baseTop - posWithShift;
    assert(applied > 600.0f);
    // 键盘收起 ⇒ want = 0，本帧位置回到 base
    const float want = ComputeImeAvoidShift(posWithShift, height, applied, 0.0f, 0.0f);
    assert(want == 0.0f);
    const float y = ImeAvoidWindowY(posWithShift, applied, want);
    assert(std::fabs(y - baseTop) < 0.01f);
    ++g_groups;
}

// 退化输入：高度 0 / NaN / 键盘顶边在屏幕外，都不许算出 NaN 或负位移
static void TestDegenerateInputs() {
    assert(ComputeImeAvoidShift(100.0f, 0.0f, 0.0f, 1476.0f, 0.0f) == 0.0f);
    assert(ComputeImeAvoidShift(100.0f, -50.0f, 0.0f, 1476.0f, 0.0f) == 0.0f);
    assert(ComputeImeAvoidShift(std::nanf(""), 1400.0f, 0.0f, 1476.0f, 0.0f) == 0.0f);
    assert(ComputeImeAvoidShift(100.0f, std::nanf(""), 0.0f, 1476.0f, 0.0f) == 0.0f);
    // 键盘顶边大于屏幕高（数据坏了）⇒ 不位移
    assert(ComputeImeAvoidShift(100.0f, 1400.0f, 0.0f, 99999.0f, 0.0f) == 0.0f);
    ++g_groups;
}

int main() {
    TestNoKeyboardNoShift();
    TestShiftAmount();
    TestStableAcrossFrames();
    TestClampAtScreenTop();
    TestRestoresAfterKeyboardHides();
    TestDegenerateInputs();
    std::printf("ImeAvoidTest: %d regression groups passed\n", g_groups);
    return 0;
}
