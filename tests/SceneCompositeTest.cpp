// ===========================================================================
//  SceneCompositeTest —— 图层次序 / 玻璃背景合成的宿主回归（不需要手机）
//
//  这个测试的存在理由：本次修复的根因是"背景采样不遵循 Z-order"，而 Z-order
//  是纯逻辑。真机上只能靠肉眼判断"玻璃里看到的是桌面还是主窗口"，看错也不知道；
//  把规则写成断言，才能在设备不在手边时也守住它。
//
//  覆盖：
//    A. 用户报的两个场景（跨窗口穿透 / 开关圆钮忽略绿色轨道）在模型里被复现，
//       并且能明确区分"修好前"与"修好后"的采样结果。
//    B. 规则本身：玻璃永远不采样自己，也不采样更上层的玻璃。
//    C. 合成数学：ui over world、alpha 钉 1、UI 缺失处等于世界原样。
//    D. 世界底层自身污染的反解（可选路径）。
//    E. 纹理坐标映射（本项目在上下镜像上真机踩过一次）。
// ===========================================================================

#include "SceneComposite.h"

#include <cassert>
#include <cmath>
#include <cstdio>

using aurora::LayerKind;
using aurora::LayerRect;
using aurora::PremulPixel;
using aurora::SceneLayer;

namespace {

int g_checks = 0;

void Check(bool condition, const char* what) {
    ++g_checks;
    if (!condition) {
        std::printf("  [失败] %s\n", what);
        // 断言会直接 abort，重定向到文件时 stdout 还是块缓冲 ⇒ 失败信息会丢。
        std::fflush(stdout);
        assert(condition);
    }
}

bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

bool SameColor(const PremulPixel& a, const PremulPixel& b, float eps = 1e-4f) {
    return Near(a.r, b.r, eps) && Near(a.g, b.g, eps) && Near(a.b, b.b, eps) &&
           Near(a.a, b.a, eps);
}

const PremulPixel kWorld = aurora::PremulFromStraight(0.0f, 0.0f, 1.0f, 1.0f);   // 蓝色桌面
const PremulPixel kMain = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 1.0f);    // 主窗口红
const PremulPixel kTrack = aurora::PremulFromStraight(0.0f, 1.0f, 0.0f, 1.0f);   // 绿色轨道
const PremulPixel kGlass = aurora::PremulFromStraight(0.5f, 0.5f, 0.5f, 0.6f);   // 半透明玻璃
const PremulPixel kGlassSolid = aurora::PremulFromStraight(0.5f, 0.5f, 0.5f, 1.0f);

LayerRect Rect(float l, float t, float r, float b) {
    LayerRect out;
    out.left = l;
    out.top = t;
    out.right = r;
    out.bottom = b;
    return out;
}

// ---------------------------------------------------------------------------
// 旧实现（= 根因）的采样结果：玻璃直接拿"整屏最终画面"当背景。
//
// 这里刻意分两种宿主配置，因为**两种都会产生用户报的现象**，而且这一点很关键：
//
//   skipOwn = true  —— 采集时排除了我们自己的图层（AURORA_SKIP_SCREENSHOT=1
//                      或 eSkipScreenshot）。采到的是"我们图层之下的桌面"，
//                      于是玻璃直接映射成桌面：用户原话
//                      「直接映射 Android 真实桌面的背景」。
//   skipOwn = false —— 采集包含我们自己的图层（当前默认）。玻璃采到的是
//                      "整屏画面"，而它自己在该像素上盖住了下层内容 ⇒ 它采到
//                      自己（不透明玻璃时 100%，半透明玻璃时按自己的 alpha 加权）。
//
// 两条路的**共同点**是：玻璃都看不到"就在它下面、但被它盖住"的图层。
// 这正是本次修复要解决的东西 —— 所以只切那个截图开关是不够的。
// ---------------------------------------------------------------------------
enum class OldMode { SkipOwnLayers, IncludeOwnLayers };

// glassSurface = 玻璃所在的 Android 图层编号。SkipOwnLayers 会整层排除它 ——
// 这正是 eSkipScreenshot / setExcludeLayers 的真实粒度（按图层，不按窗口/组件）。
PremulPixel DisplayCompositeAt(const SceneLayer* layers, const PremulPixel* colors, int count,
                               float x, float y, OldMode mode, int glassSurface = 1) {
    PremulPixel out;  // 透明
    for (int i = 0; i < count; ++i) {
        if (mode == OldMode::SkipOwnLayers && layers[i].surface == glassSurface) continue;
        if (layers[i].rect.contains(x, y)) out = aurora::OverPremul(colors[i], out);
    }
    return out;
}

// ---------------------------------------------------------------------------
// 新实现（= 修复目标）的采样结果：玻璃只采"严格位于它下方的图层"，世界底层垫底。
// 这就是 GlesSceneCompositor 在 GPU 上做的事（见 SceneComposite.h 的规则 1-3）。
// ---------------------------------------------------------------------------
PremulPixel ZOrderBackdropAt(const SceneLayer* layers, const PremulPixel* colors, int count,
                             int glassIndex, float x, float y) {
    PremulPixel scene = kWorld;
    scene.a = 1.0f;
    int below[16] = {};
    const int belowCount =
        aurora::BackdropLayersOverlapping(layers, count, glassIndex, below, 16);
    for (int k = 0; k < belowCount; ++k) {
        const int index = below[k];
        if (layers[index].kind == LayerKind::WorldBase) continue;  // 已经在最底下
        if (!layers[index].rect.contains(x, y)) continue;
        scene = aurora::OverPremul(colors[index], scene);
    }
    return scene;
}

// ===========================================================================
// A-1. 场景一：二级液态玻璃窗口跨窗口采样
//
// 复现用户报的现象必须是"两个窗口重叠"的布局：默认布局下二级窗口在主窗口
// 上方、互不重叠，用户把二级窗口拖到主窗口上面才暴露出问题。
// ===========================================================================
void CrossWindowScenario() {
    std::printf("A-1 跨窗口采样（二级窗口 vs ELF 主窗口 vs Android 桌面）\n");
    const SceneLayer layers[] = {
        {"world(android launcher)", LayerKind::WorldBase, Rect(0, 0, 1080, 2376), true, 0},
        {"elf main window", LayerKind::Ui, Rect(16, 730, 776, 2130), true, 1},
        // 二级窗口被拖到主窗口上：y ∈ [700,900)，与主窗口的 [730,…) 重叠。
        // surface = 1：和主窗口在**同一个** Android 图层里（当前交付形态）。
        {"elf secondary glass", LayerKind::Glass, Rect(16, 700, 776, 900), false, 1},
    };
    const PremulPixel colors[] = {kWorld, kMain, kGlass};
    const int count = 3;
    Check(aurora::WorldBaseIsRoot(layers, count), "世界底层必须是唯一的下标 0");

    // 重叠区内的一个点。
    const float x = 300.0f;
    const float y = 800.0f;

    // 旧行为 A（采集整层排除了我们的 Android 图层）：两个窗口在同一个图层里，
    // 所以**两个都被排除掉**，采到的就是桌面 —— 用户原话
    // 「二级窗口直接穿透 ELF 主窗口，采样到了 Android 真实桌面，
    //   仿佛中间的 ELF 主窗口不存在」。
    const PremulPixel oldSkipped =
        DisplayCompositeAt(layers, colors, count, x, y, OldMode::SkipOwnLayers);
    Check(SameColor(oldSkipped, kWorld), "旧行为(排除自身图层)：采到的是桌面，主窗口不可见");

    // 旧行为 B（采集包含我们自己，= 当前默认）：采到的是"整屏画面"，而玻璃
    // 在该像素上盖住了主窗口 ⇒ 被自己污染。
    const PremulPixel oldIncluded =
        DisplayCompositeAt(layers, colors, count, x, y, OldMode::IncludeOwnLayers);
    Check(!SameColor(oldIncluded, kMain), "旧行为(包含自身)：采不到纯主窗口");
    Check(Near(oldIncluded.r, 0.7f) && Near(oldIncluded.g, 0.3f) && Near(oldIncluded.b, 0.3f),
          "旧行为(包含自身) = 玻璃自己 0.6 + 主窗口 0.4");

    // 旧行为 B 的极端形态：不透明玻璃（panelOpacity=1 / 不透明背景）时，
    // 主窗口在整屏画面里**彻底消失**，玻璃采到的 100% 是它自己。
    const PremulPixel colorsSolid[] = {kWorld, kMain, kGlassSolid};
    const PremulPixel oldSolid =
        DisplayCompositeAt(layers, colorsSolid, count, x, y, OldMode::IncludeOwnLayers);
    Check(SameColor(oldSolid, kGlassSolid),
          "旧行为(不透明玻璃)：采到的就是玻璃自己");

    // 新行为：玻璃采到的是主窗口的红色 —— 它就在玻璃正下方。
    const PremulPixel newSample = ZOrderBackdropAt(layers, colors, count, 2, x, y);
    Check(SameColor(newSample, kMain), "新行为：玻璃采到下方主窗口的红色");
    Check(newSample.a == 1.0f, "合成结果 alpha 必须是 1（世界不透明）");

    // 玻璃伸出主窗口之外的部分（y=710 < 730），必须采到桌面 —— 不能凭空
    // 出现主窗口的颜色。
    const PremulPixel outside = ZOrderBackdropAt(layers, colors, count, 2, 300.0f, 710.0f);
    Check(SameColor(outside, kWorld), "主窗口之外仍是桌面蓝");
}

// ===========================================================================
// A-2. 场景二：开关圆钮（玻璃）忽略下方绿色轨道
// ===========================================================================
void ToggleKnobScenario() {
    std::printf("A-2 开关圆钮 vs 绿色轨道\n");
    const SceneLayer layers[] = {
        {"world(android launcher)", LayerKind::WorldBase, Rect(0, 0, 1080, 2376), true, 0},
        // 开启状态的绿色轨道
        {"toggle track (green, ON)", LayerKind::Ui, Rect(100, 400, 300, 500), true, 1},
        // 白色圆钮在轨道右端，长按变液态玻璃；它比轨道右边界多伸出 40px。
        // ★ 和轨道在同一个 Android 图层里 —— 这是关键：系统采集无法只排除
        //   "圆钮" 而保留 "轨道"，因为它们是同一图层里的两个绘制命令。
        {"knob glass", LayerKind::Glass, Rect(280, 400, 340, 500), false, 1},
    };
    const PremulPixel colors[] = {kWorld, kTrack, kGlass};
    const int count = 3;
    Check(aurora::WorldBaseIsRoot(layers, count), "世界底层必须是唯一的下标 0");

    const float x = 290.0f;  // 圆钮内，同时也在轨道内
    const float y = 450.0f;

    // 旧行为 A（采集整层排除自身）：圆钮采到桌面蓝，绿色轨道完全不见 ——
    // 这正是用户报的「圆钮会忽略绿色轨道，直接映射 Android 真实桌面的背景」。
    const PremulPixel oldSkipped =
        DisplayCompositeAt(layers, colors, count, x, y, OldMode::SkipOwnLayers);
    Check(SameColor(oldSkipped, kWorld), "旧行为(排除自身图层)：采到的是桌面，轨道不见");

    // 旧行为 B（当前默认）：采到的是"整屏画面"，圆钮在那里盖住了轨道 ⇒ 自污染。
    const PremulPixel oldIncluded =
        DisplayCompositeAt(layers, colors, count, x, y, OldMode::IncludeOwnLayers);
    Check(!SameColor(oldIncluded, kTrack), "旧行为(包含自身)：采不到纯轨道");
    Check(Near(oldIncluded.r, 0.3f) && Near(oldIncluded.g, 0.7f) && Near(oldIncluded.b, 0.3f),
          "旧行为(包含自身) = 圆钮自己 0.6 + 轨道 0.4");

    // 新行为：圆钮采到它下方的绿色轨道。
    const PremulPixel newSample = ZOrderBackdropAt(layers, colors, count, 2, x, y);
    Check(SameColor(newSample, kTrack), "新行为：圆钮采到它下方的绿色轨道");

    // 圆钮伸出轨道之外的边缘（x=315 > 300）：那里下方只有桌面。
    const PremulPixel edge = ZOrderBackdropAt(layers, colors, count, 2, 315.0f, 450.0f);
    Check(SameColor(edge, kWorld), "圆钮伸出轨道的部分应采到桌面");
}

// ===========================================================================
// A-3. 证伪「把每个窗口拆成独立 Android Surface 就能修好」
//
// 这是用户列的调查方向之一（"多个 ELF 窗口使用了独立的 Android Surface"）。
// 拆 Surface 确实能让跨窗口那一例"碰巧"变对（排除玻璃层后，主窗口那一层还
// 留在采集里），但：
//   1) 它完全修不了"同一窗口内部的组件采样"（圆钮 vs 轨道）—— 除非把每一个
//      玻璃组件都拆成一个 Android 图层，那是几十上百个图层；
//   2) 它仍然让玻璃采样的是"整屏画面"，而不是"它下方的内容"，所以对
//      「玻璃前的图标被玻璃盖掉」这类问题依然无能为力。
// 所以正确路线是进程内的 Z-order 合成，而不是拆 Surface。
// ===========================================================================
void SeparateSurfacesHypothesisIsRefuted() {
    std::printf("A-3 证伪：拆 Android Surface 不能修好\n");

    // 每个窗口各占一个 Android 图层：主窗口 surface=1，二级玻璃 surface=2。
    const SceneLayer windowed[] = {
        {"world", LayerKind::WorldBase, Rect(0, 0, 1080, 2376), true, 0},
        {"elf main window", LayerKind::Ui, Rect(16, 730, 776, 2130), true, 1},
        {"elf secondary glass", LayerKind::Glass, Rect(16, 700, 776, 900), false, 2},
    };
    const PremulPixel windowedColors[] = {kWorld, kMain, kGlass};

    // 跨窗口那一例：只排除玻璃所在图层（surface=2），主窗口留在采集里 ⇒
    // 碰巧采到主窗口。注意这是"碰巧"，不是设计。
    const PremulPixel windowedSample = DisplayCompositeAt(
        windowed, windowedColors, 3, 300.0f, 800.0f, OldMode::SkipOwnLayers, 2);
    Check(SameColor(windowedSample, kMain),
          "拆 Surface 后跨窗口一例碰巧变对（但组件内部仍然错）");

    // 组件内部那一例：圆钮和轨道始终在同一图层（surface=1）。
    // 即使圆钮"自己"是一个独立图层，轨道也还在原图层里，而原图层是宿主自己 ——
    // 排除它就连轨道一起排除了。
    const SceneLayer knobOnOwnSurface[] = {
        {"world", LayerKind::WorldBase, Rect(0, 0, 1080, 2376), true, 0},
        {"toggle track", LayerKind::Ui, Rect(100, 400, 300, 500), true, 1},
        {"knob glass", LayerKind::Glass, Rect(280, 400, 340, 500), false, 2},
    };
    const PremulPixel knobColors[] = {kWorld, kTrack, kGlass};
    const PremulPixel knobSample = DisplayCompositeAt(
        knobOnOwnSurface, knobColors, 3, 290.0f, 450.0f, OldMode::SkipOwnLayers, 2);
    Check(SameColor(knobSample, kTrack),
          "拆 Surface 后圆钮确实能采到轨道（轨道在别的图层）");
    // 但轨道所在的图层**就是宿主自己**：一旦为了别的玻璃把它排除掉，轨道也没了。
    const PremulPixel knobWithoutOwnLayer = DisplayCompositeAt(
        knobOnOwnSurface, knobColors, 3, 290.0f, 450.0f, OldMode::SkipOwnLayers, 1);
    Check(!SameColor(knobWithoutOwnLayer, kTrack),
          "排除宿主自身图层 ⇒ 轨道一起消失（拆 Surface 无法两全）");
    Check(Near(knobWithoutOwnLayer.r, 0.3f) && Near(knobWithoutOwnLayer.g, 0.3f) &&
              Near(knobWithoutOwnLayer.b, 0.7f),
          "此时圆钮采到的是「自己 0.6 + 桌面 0.4」，轨道无影无踪");
}

// ===========================================================================
// B. 规则：不采样自己，也不采样更上层的玻璃
// ===========================================================================
void NoSelfOrUpperSampling() {
    std::printf("B   禁止自采样 / 上层采样\n");
    const SceneLayer layers[] = {
        {"world", LayerKind::WorldBase, Rect(0, 0, 1080, 2376), true, 0},
        {"lower glass", LayerKind::Glass, Rect(100, 100, 400, 400), false, 1},
        {"middle ui", LayerKind::Ui, Rect(150, 150, 350, 350), true, 1},
        {"upper glass", LayerKind::Glass, Rect(200, 200, 300, 300), false, 1},
    };
    const int count = 4;

    for (int glass = 0; glass < count; ++glass) {
        if (layers[glass].kind != LayerKind::Glass) continue;
        int below[8] = {};
        const int n = aurora::BackdropLayers(layers, count, glass, below, 8);
        for (int k = 0; k < n; ++k) {
            Check(below[k] < glass, "下层集合里不得出现自己或更上层");
        }
        Check(n == glass, "下层集合必须恰好是下标 0..glass-1");
        Check(n <= count, "下层集合不能越界");
    }

    // 上层玻璃采下层玻璃：允许，而且是正确的（下层玻璃确实在它下面且已绘制）。
    const PremulPixel colors[] = {kWorld, kGlass, kMain, kTrack};
    const PremulPixel upper = ZOrderBackdropAt(layers, colors, count, 3, 250.0f, 250.0f);
    Check(SameColor(upper, kMain), "上层玻璃应采到它下面的 UI 层（红）");
    const PremulPixel lower = ZOrderBackdropAt(layers, colors, count, 1, 250.0f, 250.0f);
    Check(SameColor(lower, kWorld), "下层玻璃不得采到上层的内容");
}

// ===========================================================================
// C. 合成数学
// ===========================================================================
void CompositeMath() {
    std::printf("C   合成数学\n");
    const PremulPixel ui = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 0.5f);  // 半透明红
    const PremulPixel scene = aurora::ComposeBackdrop(ui, kWorld);
    Check(Near(scene.r, 0.5f), "红 0.5 + 蓝底露出 0.5");
    Check(Near(scene.b, 0.5f), "蓝底 0.5");
    Check(scene.a == 1.0f, "alpha 钉 1");
    Check(aurora::BackdropEqualsWorldWhereUiAbsent(kWorld), "UI 缺失处等于世界原样");

    // over 是有序的：换顺序必须给出不同结果（防止实现里把 src/dst 写反）。
    const PremulPixel a = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 0.5f);
    const PremulPixel b = aurora::PremulFromStraight(0.0f, 0.0f, 1.0f, 0.5f);
    Check(!SameColor(aurora::OverPremul(a, b), aurora::OverPremul(b, a)),
          "over 不是可交换的：写反必须能被测出来");

    // 完全不透明的 UI 必须彻底挡住世界（玻璃下方是不透明内容时不得透出桌面）。
    const PremulPixel opaqueUi = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 1.0f);
    const PremulPixel covered = aurora::ComposeBackdrop(opaqueUi, kWorld);
    Check(SameColor(covered, PremulPixel{1.0f, 0.0f, 0.0f, 1.0f}), "不透明 UI 完全遮住世界");
}

// ===========================================================================
// D. 世界底层的自身污染反解
// ===========================================================================
void WorldBaseRecovery() {
    std::printf("D   自身污染反解（可选路径）\n");
    const PremulPixel prev = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 0.4f);
    const PremulPixel raw = aurora::OverPremul(prev, kWorld);  // 采集里混进了上一帧的我们
    const aurora::BaseRecovery recovered = aurora::RecoverWorldBase(raw, prev);
    Check(Near(recovered.world.b, 1.0f), "反解出的世界仍是蓝");
    Check(Near(recovered.world.r, 0.0f), "反解出的世界不含我们的红");
    Check(Near(recovered.confidence, 0.6f), "置信度 = 1 - 上一帧覆盖率");

    // 上一帧完全不透明时不可解 —— 必须被限幅而不是产生 NaN/巨大值。
    const PremulPixel solid = aurora::PremulFromStraight(1.0f, 0.0f, 0.0f, 1.0f);
    const aurora::BaseRecovery blind = aurora::RecoverWorldBase(solid, solid, 0.15f);
    Check(blind.confidence <= 0.0f + 1e-6f, "完全遮挡时置信度为 0");
    Check(blind.world.r >= 0.0f && blind.world.r <= 1.0f, "限幅后仍在合法区间");
    Check(!std::isnan(blind.world.r) && !std::isinf(blind.world.r), "不得产生 NaN/Inf");
}

// ===========================================================================
// E. 纹理坐标映射（上下镜像踩过坑）
// ===========================================================================
void TextureMapping() {
    std::printf("E   纹理坐标映射\n");
    const int mappingH = 2376;
    // V=1 是屏幕最上一行（与 ytbl Shaders.cpp 的 sceneUv 同一约定）。
    Check(Near(aurora::WorldBaseV(0.0f, mappingH), 1.0f), "屏幕 y=0 → v=1");
    Check(Near(aurora::WorldBaseV(2376.0f, mappingH), 0.0f), "屏幕 y=H → v=0");
    Check(aurora::WorldBaseV(100.0f, mappingH) > aurora::WorldBaseV(200.0f, mappingH),
          "v 随屏幕 y 向下单调递减");
    Check(Near(aurora::WorldBaseU(0.0f, 1080), 0.0f), "屏幕 x=0 → u=0");
    Check(Near(aurora::WorldBaseU(1080.0f, 1080), 1.0f), "屏幕 x=W → u=1");

    // 抠图 tile：第 0 行 = 区域最下一行。
    Check(Near(aurora::TileRowToScreenY(0, 100.0f, 50), 149.0f), "tile 第 0 行 = 区域底部");
    Check(Near(aurora::TileRowToScreenY(49, 100.0f, 50), 100.0f), "tile 最后一行 = 区域顶部");

    // 合成器的 v 区间必须覆盖该区域且在 [0,1] 内、方向正确。
    const int y0 = 700, h = 600;
    const float vBottom = aurora::WorldBaseV(static_cast<float>(y0 + h), mappingH);
    const float vTop = aurora::WorldBaseV(static_cast<float>(y0), mappingH);
    Check(vBottom < vTop, "四边形底部对应更小的 v");
    Check(vBottom >= 0.0f && vTop <= 1.0f, "v 落在 [0,1]");
    Check(Near(static_cast<float>(mappingH) * vBottom, static_cast<float>(mappingH - y0 - h)),
          "底部 v 与半像素推导一致");
}

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("SceneCompositeTest\n");
    CrossWindowScenario();
    ToggleKnobScenario();
    SeparateSurfacesHypothesisIsRefuted();
    NoSelfOrUpperSampling();
    CompositeMath();
    WorldBaseRecovery();
    TextureMapping();
    std::printf("全部通过（%d 项断言）\n", g_checks);
    return 0;
}
