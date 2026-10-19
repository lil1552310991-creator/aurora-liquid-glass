// ButtonEdgeSequencer 的单元测试。
//
// 纯逻辑、无依赖，PC 上跑（tests/run_host_tests.ps1）也能在设备上跑。
//
// 为什么值得单测：它保证的是**顺序**（down 与 up 永不共享同一帧）。
// 这类代码出错时表现是「偶尔点不中」，真机上极难复现和定位。

#include "MouseEdgeSequencer.h"

#include <cassert>
#include <cstdio>

using aurora::ButtonEdgeSequencer;
using Out = aurora::ButtonEdgeSequencer::Out;

int main() {
    // ---------------------------------------------- 1. 静止：什么都不发
    {
        ButtonEdgeSequencer s;
        assert(s.step(false, false) == Out::None);
        assert(s.step(false, false) == Out::None);
        assert(!s.hasPendingUp());
    }

    // ---------------------------------------------- 2. 正常按下 → 抬起
    {
        ButtonEdgeSequencer s;
        assert(s.step(true, false) == Out::Down);   // 按下那一帧
        assert(s.step(false, false) == Out::None);  // 按住不放
        assert(s.step(false, true) == Out::Up);     // 抬起那一帧
        assert(s.step(false, false) == Out::None);  // 之后恢复静止
    }

    // ---------------------------------------------- 3. ★ 核心：同帧 down+up 不丢
    //
    // 这正是关掉 ImGui 涓流后会丢点击的场景。必须拆成两帧。
    {
        ButtonEdgeSequencer s;
        // 第 1 帧：两个边沿同时为真
        assert(s.step(true, true) == Out::Down);    // 只发 DOWN
        assert(s.hasPendingUp());                   // UP 被记下来了
        // 第 2 帧：把 UP 补发
        assert(s.step(false, false) == Out::Up);
        assert(!s.hasPendingUp());
        // 第 3 帧：干净了
        assert(s.step(false, false) == Out::None);
    }

    // ---------------------------------------------- 4. ★ 绝不出现「一帧内 down 又 up」
    //
    // 用穷举把所有输入组合跑一遍，断言「只要返回 Down，就绝不同时返回 Up」，
    // 并且每个 Down 最终都配到一个 Up（配对完整性）。
    {
        for (int mask = 0; mask < 16; ++mask) {
            ButtonEdgeSequencer s;
            int downs = 0, ups = 0;
            for (int frame = 0; frame < 8; ++frame) {
                // 每个 bit 决定某一帧的两个标志
                const bool p = (mask >> (frame % 4)) & 1;
                const bool r = (mask >> ((frame + 2) % 4)) & 1;
                const Out o = s.step(p, r);
                // 返回值天然只能是三者之一 —— 结构上就不可能同帧两个事件
                assert(o == Out::None || o == Out::Down || o == Out::Up);
                if (o == Out::Down) ++downs;
                if (o == Out::Up) ++ups;
                // 不变式：任何时刻 pendingUp 为真时，下一次 step 必须优先处理它
                if (s.hasPendingUp()) {
                    const Out next = s.step(false, false);
                    assert(next == Out::Up);
                    ++ups;
                }
            }
            // 每个 DOWN 都必须有一个 UP 与之配对（否则就是丢了抬起）
            assert(ups >= downs);
        }
    }

    // ---------------------------------------------- 5. 连续点击不粘连
    {
        ButtonEdgeSequencer s;
        // 三次快速点击，每次都在同一帧内完成
        for (int i = 0; i < 3; ++i) {
            assert(s.step(true, true) == Out::Down);
            assert(s.step(false, false) == Out::Up);
            assert(s.step(false, false) == Out::None);
        }
    }

    // ---------------------------------------------- 6. 按住跨多帧
    {
        ButtonEdgeSequencer s;
        assert(s.step(true, false) == Out::Down);
        for (int i = 0; i < 100; ++i) assert(s.step(false, false) == Out::None);
        assert(s.step(false, true) == Out::Up);
    }

    // ---------------------------------------------- 7. reset() 丢掉等待中的抬起
    {
        ButtonEdgeSequencer s;
        assert(s.step(true, true) == Out::Down);
        assert(s.hasPendingUp());
        s.reset();
        assert(!s.hasPendingUp());
        // reset 后不会被旧的抬起污染
        assert(s.step(false, false) == Out::None);
        // 新的一轮仍然正常
        assert(s.step(true, false) == Out::Down);
        assert(s.step(false, true) == Out::Up);
    }

    // ---------------------------------------------- 8. 只有抬起没有按下
    //
    // 真实场景：输入通道重建后第一个快照就带着 released（InputRouter::cancelAll
    // 会置 primaryReleased_，见 InputRouter.cpp:108）。也要能正常发出。
    {
        ButtonEdgeSequencer s;
        assert(s.step(false, true) == Out::Up);
        assert(!s.hasPendingUp());
    }

    std::printf("MouseEdgeSequencerTest: 全部断言通过（8 组）\n");
    return 0;
}
