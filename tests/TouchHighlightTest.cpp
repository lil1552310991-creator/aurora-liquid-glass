// M-14 触摸点高亮（手指光斑）的单元测试。
//
// 纯逻辑、无 Android 依赖，编成 aarch64 直接在设备上跑：
//   clang++ --target=aarch64-linux-android28 -std=c++20 -O1 -UNDEBUG \
//           -I app/src/main/cpp -o TouchHighlightTest tests/TouchHighlightTest.cpp
// ★ 不加 -DNDEBUG，否则 assert 失效。

#include "TouchHighlight.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ComputeHighlightAlphas;
using aurora::HighlightIntensity;
using aurora::TouchHighlight;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
constexpr float kDt = 1.0f / 60.0f;
void run(TouchHighlight& h, int frames) {
    for (int i = 0; i < frames; ++i) h.tick(kDt);
}
}  // namespace

int main() {
    // ------------------------------------------------- 1. ★ smoothstep 的反向语义
    //
    // 这是整份实现最容易写反的地方。r = 100 时：
    //   dist ≤ 50      → 1.0（实心核，= r/2 = minDimension*0.75）
    //   dist = 75      → 0.5
    //   dist = 100     → 0.0
    // 若写成常规的 smoothstep(0, r, dist)，下面这些断言会**全部反向失败**。
    {
        const float r = 100.0f;
        assert(near(HighlightIntensity(0.0f, r), 1.0f));
        assert(near(HighlightIntensity(25.0f, r), 1.0f));   // 核内恒为 1
        assert(near(HighlightIntensity(50.0f, r), 1.0f));   // 核边界
        assert(near(HighlightIntensity(75.0f, r), 0.5f));   // 半程
        assert(near(HighlightIntensity(100.0f, r), 0.0f));  // 外边界归零
        assert(near(HighlightIntensity(200.0f, r), 0.0f));  // 外面当然是 0

        // ★ 方向性断言：中心必须比边缘**亮**（写反了这条会挂）
        assert(HighlightIntensity(10.0f, r) > HighlightIntensity(90.0f, r));
        // 单调不增
        float prev = 2.0f;
        for (int i = 0; i <= 20; ++i) {
            const float v = HighlightIntensity(static_cast<float>(i) * 10.0f, r);
            assert(v <= prev + 1e-6f);
            prev = v;
        }
    }

    // ------------------------------------------------- 2. 半径 = 短边 × 1.5
    {
        // 800x400 的按钮：短边 400 → 半径 600，实心核 300
        const auto a = ComputeHighlightAlphas(1.0f, 800.0f, 400.0f);
        assert(near(a.radius, 600.0f));
        assert(near(a.ambient, 0.08f));   // White × 0.08 × 1
        assert(near(a.spot, 0.15f));      // White × 0.15 × 1

        // progress = 0 时两层都不画
        const auto z = ComputeHighlightAlphas(0.0f, 800.0f, 400.0f);
        assert(near(z.ambient, 0.0f));
        assert(near(z.spot, 0.0f));
        assert(near(z.radius, 600.0f));   // 半径与 progress 无关

        // 半按 → alpha 线性减半
        const auto h = ComputeHighlightAlphas(0.5f, 800.0f, 400.0f);
        assert(near(h.ambient, 0.04f));
        assert(near(h.spot, 0.075f));
    }

    // ------------------------------------------------- 3. 按下：press→1，位置瞬移到落点
    {
        TouchHighlight h;
        assert(!h.active());
        h.onDragStart(120.0f, 240.0f);
        // ★ 瞬移：**不用 tick** 位置就已经在落点（源码 :102 是 snapTo）
        assert(near(h.positionX(), 120.0f));
        assert(near(h.positionY(), 240.0f));
        assert(near(h.offsetX(), 0.0f));
        assert(near(h.offsetY(), 0.0f));

        run(h, 60);
        assert(h.pressProgress() > 0.99f);   // 弹簧(0.5,300) 1 秒内收敛
        assert(h.active());
    }

    // ------------------------------------------------- 4. ★ 拖动是瞬移，不是弹簧
    //
    // 源码 :118 用的是 `snapTo(change.position)`。若误用弹簧，
    // 光斑会滞后于手指，产生拖影感 —— 这条断言就是为了钉死它。
    {
        TouchHighlight h;
        h.onDragStart(0.0f, 0.0f);
        run(h, 60);

        h.onDrag(500.0f, 300.0f);
        // ★ 不给 tick，位置必须**立刻**就是新落点
        assert(near(h.positionX(), 500.0f));
        assert(near(h.positionY(), 300.0f));
        // 而且相对起点的偏移也应立刻反映出来
        assert(near(h.offsetX(), 500.0f));
        assert(near(h.offsetY(), 300.0f));

        // 连续拖动也一样
        for (int i = 0; i < 5; ++i) {
            const float x = 500.0f + static_cast<float>(i) * 20.0f;
            h.onDrag(x, 300.0f);
            assert(near(h.positionX(), x));   // 每一帧都即时跟上
        }
    }

    // ------------------------------------------------- 5. 松手：press→0，位置弹回起点
    {
        TouchHighlight h;
        h.onDragStart(100.0f, 100.0f);
        run(h, 60);
        h.onDrag(400.0f, 400.0f);       // 拖走
        run(h, 5);
        assert(near(h.positionX(), 400.0f));

        h.onDragEnd();
        run(h, 5);
        // 弹簧回位需要时间：5 帧后应该**还在路上**（证明是弹簧而不是瞬移）
        assert(!near(h.positionX(), 100.0f, 1.0f));

        run(h, 240);                    // 4 秒足够收敛
        assert(near(h.positionX(), 100.0f, 0.05f));
        assert(near(h.positionY(), 100.0f, 0.05f));
        assert(h.pressProgress() < 0.01f);
        assert(!h.active());
    }

    // ------------------------------------------------- 6. 取消（onDragCancel）与松手同义
    {
        TouchHighlight h;
        h.onDragStart(10.0f, 20.0f);
        run(h, 30);
        h.onDrag(300.0f, 300.0f);
        h.onDragEnd();
        run(h, 300);
        assert(near(h.positionX(), 10.0f, 0.05f));
        assert(near(h.positionY(), 20.0f, 0.05f));
        assert(h.pressProgress() < 0.01f);
    }

    // ------------------------------------------------- 7. 降级路径的常量
    {
        assert(near(aurora::kHighlightFallbackAlpha, 0.25f));
        // 与两层之和（0.08 + 0.15 = 0.23）接近但**不相等** —— 是两个不同的分支，别搞混
        assert(!near(aurora::kHighlightFallbackAlpha,
                     aurora::kHighlightAmbientAlpha + aurora::kHighlightSpotAlpha, 1e-3f));
    }

    std::printf("TouchHighlightTest: 全部断言通过（7 组）\n");
    return 0;
}
