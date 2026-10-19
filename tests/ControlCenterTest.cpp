// M-09 控制中心进出场的单元测试。
//
// 纯逻辑、无 Android 依赖，编成 aarch64 直接在设备上跑：
//   clang++ --target=aarch64-linux-android28 -std=c++20 -O1 -UNDEBUG \
//           -I app/src/main/cpp -o ControlCenterTest tests/ControlCenterTest.cpp
// ★ 不加 -DNDEBUG。

#include "ControlCenter.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::ComputeControlCenterVisual;
using aurora::ControlCenterReleaseTarget;
using aurora::ControlCenterState;
using aurora::MakeControlCenterSpring;
using aurora::SaturateProgress;

namespace {
bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) < eps; }
constexpr float kDt = 1.0f / 60.0f;
void run(ControlCenterState& s, int frames) {
    for (int i = 0; i < frames; ++i) s.tick(kDt);
}
}  // namespace

int main() {
    constexpr float kPx = 1.0f;   // 1dp = 1px，只看系数

    // ------------------------------------------------- 1. ★ 过拖饱和（源码 :85-94）
    //
    // ProgressConverter(x) = sign(x)*(1 - e^{-|x|})，所以饱和值是 ±1。
    {
        // 正常区间原样通过
        assert(near(SaturateProgress(0.0f), 0.0f));
        assert(near(SaturateProgress(0.5f), 0.5f));
        assert(near(SaturateProgress(1.0f), 1.0f));

        // 负向：ProgressConverter(-1) = -(1 - e^-1)
        assert(near(SaturateProgress(-1.0f), -(1.0f - std::exp(-1.0f)), 1e-5f));
        // 负向饱和到 -1
        assert(near(SaturateProgress(-100.0f), -1.0f, 1e-4f));

        // 正向过拖：1 + ProgressConverter(1) = 1 + (1 - e^-1)
        assert(near(SaturateProgress(2.0f), 1.0f + (1.0f - std::exp(-1.0f)), 1e-5f));
        // ★ 过拖**饱和到 2**，不会无限涨
        assert(near(SaturateProgress(1e6f), 2.0f, 1e-4f));
        assert(SaturateProgress(1e6f) <= 2.0f);

        // 单调不减
        float prev = -2.0f;
        for (int i = -20; i <= 30; ++i) {
            const float v = SaturateProgress(static_cast<float>(i) / 10.0f);
            assert(v >= prev - 1e-6f);
            prev = v;
        }
    }

    // ------------------------------------------------- 2. 静止在两端时的观感量
    {
        // progress = 1（完全展开）：位置归零、全不透明、无模糊、无遮罩
        const auto open = ComputeControlCenterVisual(1.0f, 1.0f, kPx);
        assert(near(open.translationY, 0.0f));
        assert(near(open.alpha, 1.0f));         // EaseIn(1) = 1
        assert(near(open.scaleDivisor, 1.0f));
        assert(near(open.scrimAlpha, 0.4f));    // 源码 :65 的 0.4
        assert(near(open.blurPx, 4.0f));        // 4dp
        assert(near(open.rowSpacingPx, 0.0f));  // 未过拖就没有额外行距
        assert(near(open.edge1Px, 24.0f));
        assert(near(open.edge2Px, 48.0f));

        // progress = 0（完全收起）：从上方 -48dp 滑入、全透明、无模糊
        const auto shut = ComputeControlCenterVisual(0.0f, 0.0f, kPx);
        assert(near(shut.translationY, -48.0f));   // ★ 负值：在屏幕上方
        assert(near(shut.alpha, 0.0f));            // EaseIn(0) = 0
        assert(near(shut.scrimAlpha, 0.0f));
        assert(near(shut.blurPx, 0.0f));
        assert(near(shut.edge1Px, 0.0f));
        assert(near(shut.edge2Px, 0.0f));
    }

    // ------------------------------------------------- 3. ★ 几何用 raw、观感用 safe
    //
    // 这是 M-09 最容易被做错的地方：源码里 translationY/scale/行距读 raw progress，
    // 而 alpha/遮罩/模糊读 safeProgress。传不同的值必须得到可区分的输出。
    {
        // raw 过拖到 1.6，safe 还停在 1.0（safe 是临界阻尼、不会过冲）
        const auto v = ComputeControlCenterVisual(1.6f, 1.0f, kPx);
        // 几何跟随 raw：过拖时 scale 被压小
        assert(v.scaleDivisor > 1.0f);
        assert(near(v.scaleDivisor, 1.0f + 0.1f * 0.6f, 1e-4f));
        // ★ 行距只在过拖时才有（max(progress-1, 0)）
        assert(v.rowSpacingPx > 0.0f);
        assert(near(v.rowSpacingPx, 16.0f * 0.6f, 1e-4f));
        // 观感跟随 safe：safe=1 ⇒ 遮罩/模糊都已到位，没有跟着 raw 一起"冲过头"
        assert(near(v.scrimAlpha, 0.4f));
        assert(near(v.blurPx, 4.0f));
        assert(near(v.alpha, 1.0f));

        // 反过来：raw 未过拖（1.0）而 safe 落后（0.5）⇒ 几何到位、观感还在路上
        const auto v2 = ComputeControlCenterVisual(1.0f, 0.5f, kPx);
        assert(near(v2.translationY, 0.0f));       // 几何已到位
        assert(near(v2.scrimAlpha, 0.2f));         // 遮罩只有一半
        assert(near(v2.blurPx, 2.0f));
        assert(v2.alpha > 0.0f && v2.alpha < 1.0f);  // EaseIn(0.5)
    }

    // ------------------------------------------------- 4. alpha 走 EaseIn
    {
        const auto a25 = ComputeControlCenterVisual(1.0f, 0.25f, kPx);
        const auto a50 = ComputeControlCenterVisual(1.0f, 0.50f, kPx);
        const auto a75 = ComputeControlCenterVisual(1.0f, 0.75f, kPx);
        // EaseIn 是 CubicBezier(0.42, 0, 1, 1)：前段慢
        assert(a25.alpha < 0.25f);
        assert(a50.alpha < 0.5f);
        assert(a75.alpha < 0.75f);
        // 单调
        assert(a25.alpha < a50.alpha && a50.alpha < a75.alpha);
    }

    // ------------------------------------------------- 5. ★ 松手目标：速度优先，速度为零才看位置
    {
        // 有速度时**只看方向**，不看当前进度
        assert(near(ControlCenterReleaseTarget(5.0f, 0.1f), 1.0f));    // 正速度 → 展开
        assert(near(ControlCenterReleaseTarget(5.0f, 0.9f), 1.0f));
        assert(near(ControlCenterReleaseTarget(-5.0f, 0.9f), 0.0f));   // 负速度 → 收起
        assert(near(ControlCenterReleaseTarget(-5.0f, 0.1f), 0.0f));
        // 速度为零 → 过半展开、否则收起
        assert(near(ControlCenterReleaseTarget(0.0f, 0.49f), 0.0f));
        assert(near(ControlCenterReleaseTarget(0.0f, 0.51f), 1.0f));
        // 边界 0.5：源码是 `< 0.5f` 才收起，所以恰好 0.5 归为展开
        assert(near(ControlCenterReleaseTarget(0.0f, 0.5f), 1.0f));
    }

    // ------------------------------------------------- 6. ★ 不对称弹簧：展开会过冲、收起不会
    {
        // 展开：ζ=0.5 ⇒ 必须过冲（这是"弹出来"的手感）
        auto open = MakeControlCenterSpring(1.0f, 1000.0f);
        open.snap(0.0f);
        open.setTarget(1.0f);
        float maxV = 0.0f;
        for (int i = 0; i < 300; ++i) {
            open.tick(kDt);
            maxV = std::max(maxV, open.value());
        }
        assert(maxV > 1.0f);                       // ★ 过冲
        assert(std::abs(open.value() - 1.0f) < 0.01f);

        // 收起：ζ=1.0 ⇒ 绝不能过冲（否则退场会"弹一下"）
        auto shut = MakeControlCenterSpring(0.0f, 1000.0f);
        shut.snap(1.0f);
        shut.setTarget(0.0f);
        float minV = 1.0f;
        for (int i = 0; i < 300; ++i) {
            shut.tick(kDt);
            minV = std::min(minV, shut.value());
        }
        assert(minV >= -1e-4f);                    // ★ 不过冲
        assert(std::abs(shut.value()) < 0.01f);

        // 阈值也不同：展开的 thr = 0.5/1000 = 5e-4（远小于收起的 0.01）
        auto wide = MakeControlCenterSpring(1.0f, 1000.0f);
        wide.snap(0.0f);
        wide.setTarget(2.0f);
        for (int i = 0; i < 600; ++i) wide.tick(kDt);
        assert(std::abs(wide.value() - 2.0f) < 5e-4f);   // 收到阈值以内
    }

    // ------------------------------------------------- 7. 状态机全流程：从展开 → 拖到一半 → 甩出
    {
        ControlCenterState s;
        assert(near(s.enterProgress(), 1.0f));
        assert(near(s.safeProgress(), 1.0f));

        // 拖到 0.3（手势直接把动画值写进去）
        s.setDragProgress(0.3f);
        assert(near(s.progress(), 0.3f));
        // safe 还没跟上
        run(s, 3);
        assert(s.safeProgress() > 0.3f);   // 从 1.0 往 0.3 走的路上

        // 松手并给一个**向上的速度**（负）⇒ 应回落到 0（收起）
        s.release(-3.0f);
        run(s, 400);
        assert(near(s.enterProgress(), 0.0f, 0.01f));
        assert(near(s.safeProgress(), 0.0f, 0.01f));
        assert(s.atRest());
        // 收起态的观感量
        const auto v = ComputeControlCenterVisual(s.progress(), s.safeProgress(), kPx);
        assert(near(v.alpha, 0.0f, 0.01f));
        assert(near(v.scrimAlpha, 0.0f, 0.01f));
        assert(near(v.blurPx, 0.0f, 0.05f));
    }

    // ------------------------------------------------- 8. 甩出展开：正速度 + 展开弹簧
    {
        ControlCenterState s;
        s.setDragProgress(0.2f);
        run(s, 3);
        s.release(4.0f);          // 正速度 → 展开
        run(s, 400);
        assert(near(s.enterProgress(), 1.0f, 0.01f));
        assert(near(s.safeProgress(), 1.0f, 0.01f));
        assert(s.atRest());
        const auto v = ComputeControlCenterVisual(s.progress(), s.safeProgress(), kPx);
        assert(near(v.scrimAlpha, 0.4f, 0.01f));
        assert(near(v.blurPx, 4.0f, 0.05f));
        assert(near(v.translationY, 0.0f, 0.5f));
    }

    // ------------------------------------------------- 9. progress() 已经含饱和，不要再套一次
    {
        ControlCenterState s;
        s.setDragProgress(1.5f);   // 原始值 1.5（过拖）
        assert(near(s.enterProgress(), 1.5f));
        // progress() 会做饱和：1 + ProgressConverter(0.5)
        assert(near(s.progress(), 1.0f + (1.0f - std::exp(-0.5f)), 1e-5f));
        assert(s.progress() < 1.5f);   // 饱和后比原始值小
        // 负向同理
        s.setDragProgress(-0.5f);
        assert(near(s.progress(), -(1.0f - std::exp(-0.5f)), 1e-5f));
        assert(s.progress() > -0.5f);
    }

    // ------------------------------------------- 10. ★ 面板几何：收起态必须在屏幕**上方**
    //
    // 这是最值得单测的一条：translationY 在收起态是负的（-48dp），
    // 一旦符号写反，收起动画会变成「往下飞出去」。真机上只表现为
    // 「看着有点怪」，很难一眼看出是符号问题 —— 所以在这里钉死。
    {
        const float px = 1.0f;
        const float stageL = 100.0f, stageT = 200.0f, stageW = 500.0f, stageH = 600.0f;
        const float panelW = 400.0f, panelH = 480.0f, pad = 10.0f;

        // 完全展开：面板完整落在舞台内、不透明、无模糊、无额外行距
        const auto open = aurora::ComputeControlCenterLayout(1.0f, 1.0f, px, stageL, stageT, stageW,
                                                             stageH, panelW, panelH, pad);
        assert(near(open.alpha, 1.0f));
        assert(near(open.scrimAlpha, 0.4f));
        assert(near(open.blurPx, 4.0f));
        assert(near(open.rowSpacingPx, 0.0f));
        // 水平居中于舞台
        assert(near((open.left + open.right) * 0.5f, stageL + stageW * 0.5f, 1e-3f));
        // 上沿 = 舞台上沿 + 留白（translationY 为 0）
        assert(near(open.top, stageT + pad, 1e-3f));
        assert(near(open.bottom - open.top, panelH, 1e-3f));
        assert(near(open.right - open.left, panelW, 1e-3f));

        // 完全收起：面板上移 48dp 并淡出到不可见
        //
        // ⚠️ 注意**不是**「整块飞到屏幕外」—— 源码 :110 是
        //    `translationY = -48dp * (1 - progress)`，只上移 48dp。
        //    我第一版断言写成 `bottom < stageT`（整体在舞台上方）被这条测试抓出来了：
        //    48dp 相对 480dp 高的面板只占 10%，面板大部分仍在舞台内，靠 alpha 消失。
        const auto shut = aurora::ComputeControlCenterLayout(0.0f, 0.0f, px, stageL, stageT, stageW,
                                                             stageH, panelW, panelH, pad);
        assert(near(shut.top, stageT + pad - 48.0f, 1e-3f));   // 恰好上移 48dp
        assert(near((shut.bottom - shut.top), panelH, 1e-3f)); // 尺寸不变（未过拖）
        // ★ 相对展开态，位移就是 48dp —— 这是它唯一的位置差异
        assert(near(open.top - shut.top, 48.0f, 1e-3f));
        assert(near(shut.alpha, 0.0f));       // 靠淡出消失，不是靠移出屏幕
        assert(near(shut.scrimAlpha, 0.0f));
        assert(near(shut.blurPx, 0.0f));

        // 过拖：scaleDivisor > 1 ⇒ 面板变小（不是变大）
        const auto over = aurora::ComputeControlCenterLayout(1.6f, 1.0f, px, stageL, stageT, stageW,
                                                             stageH, panelW, panelH, pad);
        assert((over.right - over.left) < panelW);
        assert((over.bottom - over.top) < panelH);
        // 而且行距被撑开（源码 :141 的 16dp * max(progress-1, 0)）
        assert(over.rowSpacingPx > 0.0f);
        assert(near(over.rowSpacingPx, 16.0f * 0.6f, 1e-3f));

        // 单调性：进度越高，面板越大（scaleDivisor 单调降）
        float prevW = -1.0f;
        for (int i = 0; i <= 20; ++i) {
            const float p = static_cast<float>(i) / 20.0f;
            const auto l = aurora::ComputeControlCenterLayout(p, p, px, stageL, stageT, stageW,
                                                              stageH, panelW, panelH, pad);
            const float w = l.right - l.left;
            assert(w >= prevW - 1e-4f);
            prevW = w;
        }
    }

    // ------------------------------------------- 11. snapTo 两条一起到位
    {
        ControlCenterState s;
        s.setDragProgress(0.25f);
        assert(near(s.enterProgress(), 0.25f));
        assert(near(s.safeProgress(), 1.0f));   // safe 还没跟
        s.snapTo(0.0f);
        assert(near(s.enterProgress(), 0.0f));
        assert(near(s.safeProgress(), 0.0f));   // ★ 两条都立刻到位
        assert(near(s.progress(), 0.0f));
        s.snapTo(1.0f);
        assert(near(s.progress(), 1.0f));
    }

    std::printf("ControlCenterTest: 全部断言通过（11 组）\n");
    return 0;
}
