// ===========================================================================
//  ThreadTuning.h 的离线回归（只测纯逻辑，零系统调用）
//
//  这一组回答的是"设备深度调度优化凭什么敢绑核"：
//      「把渲染线程绑到某几个核，凭什么认为那几个核是最快的？」
//
//  判据完全来自 /sys 的每核最大频率，不写死任何型号。所以这里用几台**真实机器
//  的拓扑**当输入，断言归纳结果 —— 型号换了、核数变了，只要频率表还在，结论就对。
//
//  ① 测试机 / 骁龙 8 Elite：6×3.53GHz + 2×4.32GHz ⇒ prime 簇 = cpu6-7
//  ② 全同频（单簇）：prime 簇 = 全部核，不该硬编成"最后两个核"
//  ③ 三簇（经典 1+3+4）：prime = 主频最高的那一簇
//  ④ 离线核（频率读成 0）：跳过，不能把 0 当成"最慢"而污染 min
//  ⑤ 非相邻同频：必须切成多簇（不能被"频率相同"合并掉中间不同频的核）
//  ⑥ 空输入 / 全 0：invalid，调用方据此跳过绑核而不是绑到空集合
// ===========================================================================

#include "ThreadTuning.h"

#include <cassert>
#include <cstdio>
#include <vector>

using aurora::tuning::CpuTopology;
using aurora::tuning::SummarizeTopology;

namespace {

std::vector<long> Repeat(long value, int count) {
    return std::vector<long>(static_cast<std::size_t>(count), value);
}

void TestSnapdragonElite() {
    // 测试机 实测：cpu0-5 = 3532800kHz，cpu6-7 = 4320000kHz
    std::vector<long> cores = Repeat(3532800, 6);
    cores.push_back(4320000);
    cores.push_back(4320000);
    const CpuTopology t = SummarizeTopology(cores);

    assert(t.valid());
    assert(t.clusters.size() == 2);
    assert(t.coreCount() == 8);
    assert(t.maxKhz == 4320000);
    assert(t.minKhz == 3532800);
    // ★ 核心断言：prime 簇必须正好是 cpu6-7
    assert(t.primeFirstCore() == 6);
    assert(t.primeCoreCount() == 2);
    const std::vector<int> prime = t.primeCores();
    assert(prime.size() == 2 && prime[0] == 6 && prime[1] == 7);
    // 不是"最后两个核"的巧合：这里显式给出编号
    assert(t.clusters[0].firstCore == 0 && t.clusters[0].coreCount == 6);
    assert(t.clusters[1].firstCore == 6 && t.clusters[1].coreCount == 2);
}

void TestSingleCluster() {
    // 8 核全同频 ⇒ 只有一个簇，prime 就是它（不能假定"总有两个簇"）
    const CpuTopology t = SummarizeTopology(Repeat(2000000, 8));
    assert(t.valid());
    assert(t.clusters.size() == 1);
    assert(t.primeCluster == 0);
    assert(t.primeFirstCore() == 0);
    assert(t.primeCoreCount() == 8);
    assert(t.primeCores().size() == 8);
}

void TestTripleCluster() {
    // 经典 1+3+4：cpu0-3 小核、cpu4-6 中核、cpu7 大核
    std::vector<long> cores = Repeat(1800000, 4);
    for (int i = 0; i < 3; ++i) cores.push_back(2400000);
    cores.push_back(3200000);
    const CpuTopology t = SummarizeTopology(cores);

    assert(t.clusters.size() == 3);
    assert(t.primeFirstCore() == 7);
    assert(t.primeCoreCount() == 1);
    assert(t.primeCores().size() == 1 && t.primeCores()[0] == 7);
}

void TestOfflineCore() {
    // 频率读成 0 的核（离线 / 节点不存在）要跳过，且不能把 0 当成最小值
    std::vector<long> cores = Repeat(3532800, 4);
    cores.push_back(0);
    cores.push_back(0);
    cores.push_back(4320000);
    const CpuTopology t = SummarizeTopology(cores);

    assert(t.valid());
    // 0 不进簇、也不参与 min/max
    assert(t.minKhz == 3532800);
    assert(t.maxKhz == 4320000);
    assert(t.coreCount() == 5);
    assert(t.primeFirstCore() == 6);
}

void TestNonAdjacentSameFrequency() {
    // 0,1 同频 → 2 不同频 → 3 又回到第一个频率：
    // 必须切成三簇（不能被"频率值相同"合并，否则簇会跨越不同频的核）
    std::vector<long> cores = {2000000, 2000000, 3000000, 2000000};
    const CpuTopology t = SummarizeTopology(cores);

    assert(t.clusters.size() == 3);
    assert(t.clusters[0].firstCore == 0 && t.clusters[0].coreCount == 2);
    assert(t.clusters[1].firstCore == 2 && t.clusters[1].coreCount == 1);
    assert(t.clusters[2].firstCore == 3 && t.clusters[2].coreCount == 1);
    assert(t.primeFirstCore() == 2);
}

void TestDegenerate() {
    const std::vector<long> none;
    assert(!SummarizeTopology(none).valid());
    assert(!SummarizeTopology(Repeat(0, 8)).valid());
    const CpuTopology empty = SummarizeTopology(none);
    assert(empty.primeFirstCore() == -1);
    assert(empty.primeCoreCount() == 0);
    assert(empty.primeCores().empty());
}

// ---------------------------------------------------------------------------
//  场景 → 档位（策略本体）
//
//  用户的要求是"线程不要写得太死"：他会在游戏里开这个窗口、会刷视频、也会让它
//  在桌面挂着。所以真正要断言的不是"跑得多快"，而是**该让路时真的让路**。
// ---------------------------------------------------------------------------
aurora::tuning::SceneContext Scene(bool glass, bool interacting, float fps, int busy,
                                  bool otherHighRefresh = false) {
    aurora::tuning::SceneContext c;
    c.glassActive = glass;
    c.interacting = interacting;
    c.targetFps = fps;
    c.systemBusyPercent = busy;
    c.otherHighRefreshClient = otherHighRefresh;
    return c;
}

void TestYieldsWhenBusy() {
    // ★ 游戏场景的保护：系统忙 ⇒ 一点优先级都不争
    const auto p = aurora::tuning::ComputeProfile(Scene(true, true, 120.0f, 85));
    assert(p.nice == 0);
    assert(p.uclampPercent == 0);
}

void TestYieldsToOtherHighRefreshClient() {
    // 别人也在要高刷（多半是游戏/相机）⇒ 让路，且这一条**优先于**系统忙不忙
    const auto p = aurora::tuning::ComputeProfile(Scene(true, true, 120.0f, 10, true));
    assert(p.nice == 0);
    assert(p.uclampPercent == 0);
}

void TestYieldsWhenIdleWindow() {
    const auto p = aurora::tuning::ComputeProfile(Scene(false, false, 120.0f, 5));
    assert(p.nice == 0);
    assert(p.uclampPercent == 0);
}

void TestModestWhenVisibleButStatic() {
    const auto p = aurora::tuning::ComputeProfile(Scene(true, false, 120.0f, 10));
    assert(p.nice == -4);
    assert(p.uclampPercent == 20);
}

void TestModestWhileInteracting() {
    // 系统有点忙（40~70）但还没到让路的程度 ⇒ 只轻度提升
    const auto p = aurora::tuning::ComputeProfile(Scene(true, true, 120.0f, 50));
    assert(p.nice == -6);
    assert(p.uclampPercent == 30);
}

void TestMostAggressiveNeedsBothIdleSystemAndHighRefresh() {
    const auto p = aurora::tuning::ComputeProfile(Scene(true, true, 120.0f, 10));
    assert(p.nice == -10);
    assert(p.uclampPercent == 50);
    // 60Hz 场景不必按 120 的要求去争
    const auto low = aurora::tuning::ComputeProfile(Scene(true, true, 60.0f, 10));
    assert(low.nice == -6 && low.uclampPercent == 30);
    // ★ 再怎么激进也不越过"轻度优先"这条线：没有任何一档是 -16/-20
    assert(p.nice >= -10);
}

void TestNeverPinsToPrimeByDefault() {
    // ★★ 这一条是用户"不要写死"要求的硬保证：
    //    策略本体在任何场景下都不主动要求绑核 —— 绑核只能由调用方显式开启
    //    （AURORA_SCHED_PIN=1），因为 prime 核要留给游戏/相机/系统 UI。
    const bool glass[2] = {true, false};
    const bool act[2] = {true, false};
    const float fps[2] = {120.0f, 60.0f};
    const int busy[3] = {0, 50, 95};
    for (bool g : glass) {
        for (bool a : act) {
            for (float f : fps) {
                for (int b : busy) {
                    const auto p = aurora::tuning::ComputeProfile(Scene(g, a, f, b));
                    assert(!p.pinToPrime);
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
//  系统繁忙度（/proc/stat 两次采样的差值）
// ---------------------------------------------------------------------------
void TestBusyPercent() {
    using aurora::tuning::CpuTimes;
    using aurora::tuning::BusyPercentDelta;

    // 1000 个单位里 250 idle ⇒ 75% 忙
    const CpuTimes a{1000, 10000};
    const CpuTimes b{1250, 11000};
    assert(BusyPercentDelta(a, b) == 75);

    // 全闲 ⇒ 0%（idle 增量必须等于总增量）
    const CpuTimes c{2250, 12000};
    assert(BusyPercentDelta(b, c) == 0);

    // 全忙 ⇒ 100%（idle 一点都不涨）
    const CpuTimes d{2250, 13000};
    assert(BusyPercentDelta(c, d) == 100);

    // 计数器回绕 / 未前进 / 空样本 ⇒ -1（调用方保留上一次的值）
    assert(BusyPercentDelta(d, c) == -1);
    assert(BusyPercentDelta(a, a) == -1);
    assert(BusyPercentDelta(CpuTimes{}, b) == -1);
    assert(BusyPercentDelta(a, CpuTimes{}) == -1);
}

}  // namespace

int main() {
    TestSnapdragonElite();
    TestSingleCluster();
    TestTripleCluster();
    TestOfflineCore();
    TestNonAdjacentSameFrequency();
    TestDegenerate();
    TestYieldsWhenBusy();
    TestYieldsToOtherHighRefreshClient();
    TestYieldsWhenIdleWindow();
    TestModestWhenVisibleButStatic();
    TestModestWhileInteracting();
    TestMostAggressiveNeedsBothIdleSystemAndHighRefresh();
    TestNeverPinsToPrimeByDefault();
    TestBusyPercent();
    std::printf("ThreadTuningTest: 14 regression groups passed\n");
    return 0;
}
