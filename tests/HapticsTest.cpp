// ===========================================================================
//  Haptics.h 的离线回归（纯逻辑，零 Android / 零 Binder 依赖）
//
//  用户对震动的要求，能离线证明的都在这里：
//    · 只在"未激活 → 激活"这一个转换上放行**一次**；
//    · 等待阶段（还没到 330ms）不震；
//    · 拖动缩放期间（一直激活）**不重复震**；
//    · 松手 / 取消之后闩锁复位，下一次长按还能再震一次；
//    · 参数合理（默认用"越过激活阈值"那个常量、尊重用户触觉设置）；
//    · 没有注册平台实现时 = 不可用，调用直接返回 false（安全降级，不抛不崩）。
// ===========================================================================

#include "Haptics.h"

#include <cassert>
#include <cstdio>

using aurora::HapticGate;
using aurora::HapticParams;

static int g_groups = 0;

static void TestDefaultsAreSane() {
    const HapticParams p;
    // 23 = GESTURE_THRESHOLD_ACTIVATE（"刚越过激活阈值"）—— 与"长按进入缩放"同义。
    // 真机实测该常量被厂商映射为 Prebaked=TICK(STRONG)，短促清脆且力度够。
    assert(p.constant == 23);
    // 0 = 尊重用户设置：用户关掉触感反馈就不该震（刻意不用 IGNORE_GLOBAL_SETTING）。
    assert(p.flags == 0);
    assert(p.privFlags == 0);
    assert(p.reason != nullptr && p.reason[0] != '\0');   // dumpsys 里要靠它认出来
    ++g_groups;
}

static void TestFiresExactlyOncePerActivation() {
    HapticGate gate;
    int fired = 0;
    // ① 等待阶段：一直没激活 ⇒ 一次都不许震
    for (int i = 0; i < 40; ++i) {
        if (gate.Step(false)) ++fired;
    }
    assert(fired == 0);
    // ② 激活那一帧：放行一次
    assert(gate.Step(true));
    ++fired;
    // ③ 持续拖动（一直激活）：绝不再震
    for (int i = 0; i < 600; ++i) {
        assert(!gate.Step(true));
    }
    assert(fired == 1);
    // ④ 松手 ⇒ 复位；再次长按 ⇒ 再放行一次（且仅一次）
    assert(!gate.Step(false));
    assert(gate.Step(true));
    assert(!gate.Step(true));
    ++g_groups;
}

static void TestCancelDoesNotFire() {
    HapticGate gate;
    // 按下 → 没到阈值就松开（取消）：整个过程一次都不许震
    for (int i = 0; i < 20; ++i) assert(!gate.Step(false));
    assert(!gate.Step(false));
    // 取消之后重新长按到激活：这时才允许震
    assert(gate.Step(true));
    ++g_groups;
}

static void TestRepeatedFastActivations() {
    HapticGate gate;
    int fired = 0;
    // 快速反复长按（每次激活 → 松手 → 再激活）：每次激活都该有一下，且只有一下
    for (int round = 0; round < 5; ++round) {
        for (int i = 0; i < 3; ++i) if (gate.Step(false)) ++fired;
        if (gate.Step(true)) ++fired;
        for (int i = 0; i < 3; ++i) if (gate.Step(true)) ++fired;
    }
    assert(fired == 5);
    ++g_groups;
}

static void TestNoSinkMeansUnavailableNotCrash() {
    // 没注册平台实现（例如非 root、或 binder 服务不可用）⇒ 不可用 + 调用安全返回 false
    aurora::SetHapticSink(nullptr);
    assert(!aurora::HapticAvailable());
    assert(!aurora::HapticTap(HapticParams()));
    // 注册之后变为可用，并且真的会转到实现上
    static int calls = 0;
    aurora::SetHapticSink([](const HapticParams&) -> bool { ++calls; return true; });
    assert(aurora::HapticAvailable());
    assert(aurora::HapticTap(HapticParams()));
    assert(calls == 1);
    aurora::SetHapticSink(nullptr);   // 收尾，别把状态留给别的测试
    ++g_groups;
}

int main() {
    TestDefaultsAreSane();
    TestFiresExactlyOncePerActivation();
    TestCancelDoesNotFire();
    TestRepeatedFastActivations();
    TestNoSinkMeansUnavailableNotCrash();
    std::printf("HapticsTest: %d regression groups passed\n", g_groups);
    return 0;
}
