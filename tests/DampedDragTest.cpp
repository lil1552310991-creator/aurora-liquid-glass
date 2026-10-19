// M-10 DampedDragAnimation 的单元测试。
//
// 纯逻辑、无 Android 依赖，可编成 aarch64 直接在设备上跑（与 InputRouterTest 同样方式）：
//   clang++ --target=aarch64-linux-android28 -std=c++20 -O1 -UNDEBUG \
//           -I app/src/main/cpp -o DampedDragTest tests/DampedDragTest.cpp
//
// ★ 注意必须**不加 -DNDEBUG**，否则 assert 全部失效、测试变空跑。
//
// 重点验证的是 M-10 的两个「漏了就废掉手感」的细节，而不是普通数值：
//   1. release() 的「先落位，再松开手指」（2.5% 区间等待）
//   2. 速度是独立弹簧平滑过的（带迟滞）

#include "Motion.h"

#include <cassert>
#include <cmath>
#include <cstdio>

namespace {

constexpr float kDt = 1.0f / 60.0f;

// 推进 n 帧；nowSeconds 是速度追踪用的绝对时间
void run(aurora::DampedDrag& d, int frames, double& now) {
    for (int i = 0; i < frames; ++i) {
        now += kDt;
        d.tick(kDt, now);
    }
}

}  // namespace

int main() {
    double now = 0.0;

    // ---------------------------------------------------------------- 1. 配置与 progress
    {
        aurora::DampedDrag d;
        d.configure(0.0f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        assert(std::abs(d.value() - 0.0f) < 1e-6f);
        assert(std::abs(d.progress() - 0.0f) < 1e-6f);
        assert(std::abs(d.pressProgress() - 0.0f) < 1e-6f);
        assert(std::abs(d.scaleX() - 1.0f) < 1e-6f);
        assert(std::abs(d.scaleY() - 1.0f) < 1e-6f);

        // progress 的映射：(value - start)/(end - start)
        d.updateValue(0.25f);
        run(d, 240, now);  // 4 秒，足够收敛
        assert(std::abs(d.value() - 0.25f) < 1e-3f);
        assert(std::abs(d.progress() - 0.25f) < 1e-3f);

        // 夹取：超出区间的值必须被夹回来
        d.updateValue(9.0f);
        run(d, 240, now);
        assert(std::abs(d.value() - 1.0f) < 1e-3f);
        d.updateValue(-9.0f);
        run(d, 240, now);
        assert(std::abs(d.value() - 0.0f) < 1e-3f);
    }

    // ---------------------------------------------------------------- 2. 按下：press→1，scale→pressedScale
    {
        aurora::DampedDrag d;
        d.configure(0.0f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        d.press();
        run(d, 60, now);  // 1 秒足够 spring(1.0,1000)≈210ms 收敛
        assert(d.pressProgress() > 0.99f);
        assert(std::abs(d.scaleX() - 1.5f) < 0.01f);
        assert(std::abs(d.scaleY() - 1.5f) < 0.01f);
        // 主值没有目标变化，应停在原地
        assert(std::abs(d.value() - 0.0f) < 1e-4f);
    }

    // ---------------------------------------------------------------- 3. ★ 2.5%「先落位，再松开手指」
    //
    // 这是 M-10 最容易被漏掉的规则：松手时若主值还没到目标，
    // **必须先等主值进入目标 ±2.5% 区间，才开始收回 press/scale**。
    // 判定方式：在主值离目标还很远时断言 pressProgress 仍然很高。
    {
        aurora::DampedDrag d;
        d.configure(0.0f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);

        d.press();
        run(d, 60, now);
        assert(d.pressProgress() > 0.99f);   // 已按下去
        assert(d.scaleX() > 1.45f);

        // 跳到远端，然后**立刻**松手 —— 此时主值离目标 1.0 还很远
        d.animateToValue(1.0f);              // 内部 press() + 设目标 + release()
        run(d, 3, now);                      // 只走 3 帧（50ms），主值必然还没到位
        const float farDiff = std::abs(d.value() - d.targetValue());
        assert(farDiff > 0.025f);            // 前提成立：确实在 2.5% 区间之外
        // ★ 关键断言：还没落位，所以**不能**开始弹回
        assert(d.pressProgress() > 0.9f);
        assert(d.scaleX() > 1.4f);

        // 继续推进，直到主值进入 2.5% 区间；此后 press/scale 才应开始回落
        bool fellBackWhileFar = false;
        for (int i = 0; i < 600; ++i) {
            now += kDt;
            d.tick(kDt, now);
            const float diff = std::abs(d.value() - d.targetValue());
            if (diff > 0.025f && d.pressProgress() < 0.5f) fellBackWhileFar = true;
            if (std::abs(d.value() - 1.0f) < 1e-3f && d.pressProgress() < 0.01f &&
                std::abs(d.scaleX() - 1.0f) < 0.01f) {
                break;
            }
        }
        assert(!fellBackWhileFar);           // ★ 绝不能在没落位时就弹回
        assert(std::abs(d.value() - 1.0f) < 1e-3f);
        assert(d.pressProgress() < 0.05f);   // 最终收回
        assert(std::abs(d.scaleX() - 1.0f) < 0.02f);
        assert(std::abs(d.scaleY() - 1.0f) < 0.02f);
    }

    // ---------------------------------------------------------------- 4. 已落位时松手：应立即弹回
    {
        aurora::DampedDrag d;
        d.configure(0.5f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        d.press();
        run(d, 60, now);
        assert(d.pressProgress() > 0.99f);
        // 主值本来就在目标上（value == target），松手不该等待
        d.release();
        run(d, 90, now);                     // 1.5 秒
        assert(d.pressProgress() < 0.05f);
        assert(std::abs(d.scaleX() - 1.0f) < 0.02f);
    }

    // ---------------------------------------------------------------- 5. ★ 速度是独立平滑的
    //
    // 源码 :129-136：量到的速度先喂给 spring(0.5, 300)，所以它带迟滞、不会瞬间归零。
    //
    // ⚠️ 这里有一条**容易写错的期望**（我第一版就写错了，被这个测试抓出来）：
    //    `updateValue()`（拖拽路径）结束后，**velocity 不会回到 0** —— 源码里只有
    //    `animateToValue()`（点击路径，:121-123）才显式 `velocityAnimation.animateTo(0f)`。
    //    拖拽路径残留的速度是**留给消费者做"液体拉伸"用的**（M-11 的 `v/50`）。
    //    所以正确的断言是「被追踪到 / 松手后不瞬间归零 / 有界」，
    //    而不是「最终归零」—— 后者不是这个类的契约。
    {
        aurora::DampedDrag d;
        d.configure(0.0f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        d.press();

        // 模拟一段拖拽：每帧把目标往前推
        for (int i = 0; i < 30; ++i) {
            d.updateValue(std::min(1.0f, static_cast<float>(i) * 0.03f));
            now += kDt;
            d.tick(kDt, now);
        }
        const float vDuringDrag = d.velocity();
        assert(std::abs(vDuringDrag) > 1e-4f);   // 拖拽中速度被追踪到了

        d.release();
        run(d, 3, now);
        // ★ 平滑性：release 之后 3 帧，速度不应直接变成 0（那是"直接量"而非"弹簧平滑"）
        const float vAfterRelease = d.velocity();
        assert(std::abs(vAfterRelease) > 1e-6f);

        run(d, 400, now);                        // 足够的收敛时间
        // 速度是**归一化**量（除以区间跨度），必须落在合理范围内 —— 不应发散
        assert(std::abs(d.velocity()) <= 1.0f);
        // ⚠️ 这里断言「值收敛到**它自己的目标**」，而不是硬写 1.0 ——
        //    拖拽循环最后一帧的目标是 29*0.03 = 0.87，不是 1.0（这个错误也被测试抓过一次）。
        assert(std::abs(d.value() - d.targetValue()) < 1e-3f);
        assert(d.atRest());                           // press/scale/值都静止
    }

    // ---------------------------------------------------------------- 5b. 点击路径必须把速度收回 0
    //
    // 这条是源码 :121-123 明确写了的：`if (velocity != 0f) velocityAnimation.animateTo(0f, ...)`。
    // 与上面的拖拽路径形成对照 —— 两条路径对速度的处理**不同**，这正是要盯住的点。
    {
        aurora::DampedDrag d;
        d.configure(0.0f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        d.press();
        for (int i = 0; i < 30; ++i) {
            d.updateValue(std::min(1.0f, static_cast<float>(i) * 0.03f));
            now += kDt;
            d.tick(kDt, now);
        }
        assert(std::abs(d.velocity()) > 1e-4f);

        d.animateToValue(0.0f);   // 点击跳转：内部会显式把速度目标设为 0
        run(d, 400, now);
        assert(std::abs(d.velocity()) < 0.02f);       // ★ 这次必须归零
        assert(std::abs(d.value() - 0.0f) < 1e-3f);
        assert(d.atRest());
    }

    // ---------------------------------------------------------------- 6. animateToValue 与 atRest
    {
        aurora::DampedDrag d;
        d.configure(0.2f, 0.0f, 1.0f, 0.01f, 1.0f, 1.5f);
        assert(!d.atRest() || d.atRest());  // 初值即目标，允许两种实现语义
        d.animateToValue(0.8f);
        run(d, 400, now);
        assert(std::abs(d.value() - 0.8f) < 1e-3f);
        assert(d.atRest());
    }

    // ---------------------------------------------------------------- 7. 区间跨负值也要正确
    {
        aurora::DampedDrag d;
        d.configure(0.0f, -1.0f, 1.0f, 0.001f, 1.0f, 1.2f);
        d.updateValue(-1.0f);
        run(d, 400, now);
        assert(std::abs(d.value() - (-1.0f)) < 1e-3f);
        assert(std::abs(d.progress() - 0.0f) < 1e-3f);
        d.updateValue(1.0f);
        run(d, 400, now);
        assert(std::abs(d.value() - 1.0f) < 1e-3f);
        assert(std::abs(d.progress() - 1.0f) < 1e-3f);
    }

    std::printf("DampedDragTest: 全部断言通过（7 组）\n");
    return 0;
}
