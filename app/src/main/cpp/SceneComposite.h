#pragma once

// ===========================================================================
//  图层合成模型 + 玻璃背景合成数学（纯逻辑，零 Android / 零 GL 依赖）
//
//  为什么单独抽一个头文件：本次修复的**根因**是"背景采样没有遵循 Z-order"，
//  而 Z-order 是纯逻辑，可以在 PC 上写成断言。渲染结果只能靠肉眼看，看错了也
//  不知道；把规则写成可执行断言，才能在设备不在手边时也守住它。
//
//  这个模型是**规格**，不是实现细节：
//    规则 1（下方集合）  一块玻璃的背景 = 所有 z 严格小于它的图层，按 z 升序合成。
//    规则 2（禁止自采样）玻璃自身、以及 z 大于等于它的图层，绝不进入它的背景。
//    规则 3（世界底层）  z 最小的那一层是世界底层（Android 桌面 / 本图层之下的
//                        屏幕内容）。它参与合成，但绝不是"全部背景"。
//    规则 4（不透明世界）世界底层是不透明的 ⇒ 合成结果的 alpha 必须钉成 1。
// ===========================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aurora {

// ---------------------------------------------------------------------------
// 屏幕空间的矩形（像素，y 向下，原点 = 宿主 Surface 左上角）
// ---------------------------------------------------------------------------
struct LayerRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    constexpr bool empty() const { return right <= left || bottom <= top; }
    constexpr float width() const { return right - left; }
    constexpr float height() const { return bottom - top; }

    constexpr bool contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }

    // 相交结果；不相交时返回空矩形（right<=left 或 bottom<=top）。
    constexpr LayerRect intersect(const LayerRect& other) const {
        LayerRect out;
        out.left = left > other.left ? left : other.left;
        out.top = top > other.top ? top : other.top;
        out.right = right < other.right ? right : other.right;
        out.bottom = bottom < other.bottom ? bottom : other.bottom;
        return out;
    }

    constexpr bool overlaps(const LayerRect& other) const {
        return !intersect(other).empty();
    }
};

// ---------------------------------------------------------------------------
// 图层种类
//
// WorldBase 是"我们图层之下的现实世界"。它只能通过系统采集拿到，而且必须是
// **排除了我们自己图层** 的采集结果 —— 否则采集到的画面里，玻璃自己已经盖住
// 了它本该看到的下层内容，这正是"二级窗口穿透主窗口"的直接成因。
// ---------------------------------------------------------------------------
enum class LayerKind : std::uint8_t {
    WorldBase = 0,
    Ui = 1,
    Glass = 2,
};

struct SceneLayer {
    const char* name = "";
    LayerKind kind = LayerKind::Ui;
    LayerRect rect{};
    // dst 是否已不透明（世界底层恒为 true）。仅用于诊断与断言。
    bool opaque = true;
    // 该图层所在的 **Android 图层**（SurfaceControl）编号。
    //   0 = 现实世界（不是我们的图层，只能被采集）
    //   1 = 我们的 ELF 图层（当前交付形态：两个窗口都画在同一个图层里）
    // 这个字段存在的理由：系统采集只能整层地包含/排除一个 Android 图层
    // （ScreenCapture.CaptureArgs.setExcludeLayers / eSkipScreenshot）。
    // 因此"把每个窗口拆成独立 Surface 就能修好"这个假设是可以被证伪的 ——
    // 见 tests/SceneCompositeTest.cpp 的场景三。
    int surface = 1;
};

// ---------------------------------------------------------------------------
// 规则 1 + 2：返回"对第 glassIndex 块玻璃可见的下层图层"（按 z 升序 = 数组顺序）。
//
// 图层数组本身必须按 z 升序排列（绘制顺序）。返回值是写入 out 的个数。
// 关键性质：glassIndex 及其之后的任何图层都不会被返回 —— 这就是"禁止自采样"
// 的可执行形式。宿主渲染顺序（ImGui 绘制顺序）与数组顺序一致时，这条规则由
// "先抠图、后绘制" 的时序天然保证。
// ---------------------------------------------------------------------------
inline int BackdropLayers(const SceneLayer* layers, int count, int glassIndex, int* out,
                          int capacity) {
    if (layers == nullptr || out == nullptr || capacity <= 0) return 0;
    if (glassIndex < 0 || glassIndex >= count) return 0;
    int written = 0;
    for (int i = 0; i < glassIndex && written < capacity; ++i) {
        out[written++] = i;
    }
    return written;
}

// 同样的下层集合，但只保留与该玻璃矩形相交的层 —— 合成是逐区域的，不相交的层
// 对这块玻璃的采样没有任何贡献。（用于断言"没被遮挡的内容不该凭空出现"。）
inline int BackdropLayersOverlapping(const SceneLayer* layers, int count, int glassIndex,
                                     int* out, int capacity) {
    if (layers == nullptr || out == nullptr || capacity <= 0) return 0;
    if (glassIndex < 0 || glassIndex >= count) return 0;
    const LayerRect glass = layers[glassIndex].rect;
    int written = 0;
    for (int i = 0; i < glassIndex && written < capacity; ++i) {
        if (layers[i].rect.overlaps(glass)) out[written++] = i;
    }
    return written;
}

// 世界底层必须恰好是下标 0，且唯一 —— 两条都错了就说明图层栈建模反了。
inline bool WorldBaseIsRoot(const SceneLayer* layers, int count) {
    if (layers == nullptr || count <= 0) return false;
    if (layers[0].kind != LayerKind::WorldBase) return false;
    for (int i = 1; i < count; ++i) {
        if (layers[i].kind == LayerKind::WorldBase) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 预定乘 alpha 的像素
//
// 全套宿主渲染（ImGui 的 glBlendFuncSeparate(SRC_ALPHA, ONE_MINUS_SRC_ALPHA,
// ONE, ONE_MINUS_SRC_ALPHA) 与 ytbl 面板的同一组混合因子）都满足：
//     dst.rgb' = src.rgb * src.a + dst.rgb * (1 - src.a)
//     dst.a'   = src.a        + dst.a   * (1 - src.a)
// 即 rgb 是**预定乘**的、a 是覆盖率。帧缓冲因此天然存的就是 PremulPixel。
// ---------------------------------------------------------------------------
struct PremulPixel {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// 直通 alpha 颜色的构造函数（预乘）。
inline PremulPixel PremulFromStraight(float r, float g, float b, float a) {
    return PremulPixel{r * a, g * a, b * a, a};
}

// src 在 dst 之上（两者都是预定乘）。
inline PremulPixel OverPremul(const PremulPixel& src, const PremulPixel& dst) {
    const float keep = 1.0f - src.a;
    PremulPixel out;
    out.r = src.r + dst.r * keep;
    out.g = src.g + dst.g * keep;
    out.b = src.b + dst.b * keep;
    out.a = src.a + dst.a * keep;
    return out;
}

// ---------------------------------------------------------------------------
// 规则 3 + 4：玻璃的背景 = 本进程已绘制的下层 UI 叠在世界底层之上，alpha 钉成 1。
//
// ui    = 当前帧缓冲里该像素的值（= 本进程先于这块玻璃绘制的一切）
// world = 世界底层在该像素的颜色（不透明）
//
// 为什么 alpha 必须钉 1：ytbl 的 sceneSample 用 c.a 做两件事 —— 反预乘
// （c.rgb / c.a）以及判定"场景缺失"（1-base 混入 uAbsentTint 兜底色）。
// 世界是不透明的，若这里留下 a<1，玻璃会把自己下方的世界当成"半透明场景"，
// 表现为玻璃变成一块纯色兜底（这正是旧实现里"背景是假的"的观感来源之一）。
// ---------------------------------------------------------------------------
inline PremulPixel ComposeBackdrop(const PremulPixel& ui, const PremulPixel& world) {
    PremulPixel base = world;
    base.a = 1.0f;
    PremulPixel scene = OverPremul(ui, base);
    scene.a = 1.0f;
    return scene;
}

// UI 完全没盖住的地方，玻璃采样到的就是世界原样 —— 这条断言能抓住"合成顺序反了"。
inline bool BackdropEqualsWorldWhereUiAbsent(const PremulPixel& world) {
    const PremulPixel scene = ComposeBackdrop(PremulPixel{}, world);
    return scene.a == 1.0f && std::fabs(scene.r - world.r) < 1e-6f &&
           std::fabs(scene.g - world.g) < 1e-6f && std::fabs(scene.b - world.b) < 1e-6f;
}

// ---------------------------------------------------------------------------
// 世界底层的"自身污染"反解（可选路径，默认不启用）
//
// 如果系统采集**无法排除我们自己的图层**，采集结果里就混进了我们上一帧的画面：
//     raw = prev + world * (1 - prev.a)
// 反解：
//     world = (raw - prev) / (1 - prev.a)
//
// prev.a → 1 时不可解（数学上被我们完全盖住的像素里没有世界的信息）。
// 用 minKeep 限幅除数：误差只出现在"上一层 UI 几乎不透明"的地方，而那些地方
// 当前帧大概率也不透明，世界项的权重 (1 - cur.a) 很小，误差本来就被压掉了。
// ---------------------------------------------------------------------------
struct BaseRecovery {
    PremulPixel world;
    // 1 - prev.a：越接近 0 越不可信。上层可用它做门限或调试着色。
    float confidence = 0.0f;
};

inline BaseRecovery RecoverWorldBase(const PremulPixel& raw, const PremulPixel& prev,
                                     float minKeep = 0.15f) {
    const float keepRaw = 1.0f - prev.a;
    const float keep = keepRaw > minKeep ? keepRaw : minKeep;
    BaseRecovery out;
    out.world.r = std::clamp((raw.r - prev.r) / keep, 0.0f, 1.0f);
    out.world.g = std::clamp((raw.g - prev.g) / keep, 0.0f, 1.0f);
    out.world.b = std::clamp((raw.b - prev.b) / keep, 0.0f, 1.0f);
    out.world.a = 1.0f;
    out.confidence = std::clamp(keepRaw, 0.0f, 1.0f);
    return out;
}

// ---------------------------------------------------------------------------
// 纹理坐标换算
//
// ★ 本项目在"上下镜像"上真机踩过一次（内部记录），所以把两个方向的
//   映射都写成可断言的纯函数，而不是散在 GL 调用里。
//
// 约定 A（世界底层 / 全局背景）：纹理 V=1 是屏幕最上一行扫描线。
//     屏幕 y 向下 ⇒ v = (mappingH - screenY) / mappingH
//     与 ytbl Shaders.cpp 的 sceneUv 完全一致。
//
// 约定 B（帧缓冲抠图 tile）：glCopyTexSubImage2D 把区域的**最下一行**放在
//     纹理第 0 行，所以 tile 的第 r 行对应屏幕 y = regionTop + (h - 1 - r)。
// ---------------------------------------------------------------------------
inline float WorldBaseV(float screenY, int mappingHeight) {
    const float h = mappingHeight > 0 ? static_cast<float>(mappingHeight) : 1.0f;
    return (h - screenY) / h;
}

inline float WorldBaseU(float screenX, int mappingWidth) {
    const float w = mappingWidth > 0 ? static_cast<float>(mappingWidth) : 1.0f;
    return screenX / w;
}

// tile 第 r 行（r 从 0 = 区域最下一行算起）对应的屏幕 y。
inline float TileRowToScreenY(int row, float regionTop, int regionHeight) {
    return regionTop + static_cast<float>(regionHeight - 1 - row);
}

}  // namespace aurora
