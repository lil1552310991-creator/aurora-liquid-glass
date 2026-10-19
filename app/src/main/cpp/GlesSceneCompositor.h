#pragma once

// ===========================================================================
//  GlesSceneCompositor —— 液态玻璃 Backdrop 的 Z-order 合成器（GLES3 实现）
//
//  它解决的就是这一条：
//      "背景纹理始终来自全局桌面捕获，而不是正确的 Backdrop。"
//
//  旧链路（= 根因）：
//      GlesBackend 把**整屏采集**当作全局背景纹理交给 ytbl
//        ⇒ ytbl 的 captureScene() 走 backdropReady() 分支，
//          给每块面板都设 sampleOrigin=(0,0) / sampleSize=整屏，
//        ⇒ 所有玻璃都在"整屏画面"上按屏幕坐标取 UV。
//      而"整屏画面"里，玻璃自己已经盖住了它本该看到的东西（同一 Surface 里
//      先画的主窗口、同窗口里的绿色轨道…都在它下面被它自己遮住），
//      于是玻璃只能看到"屏幕最终长什么样" —— 也就是它自己 + 桌面。
//
//  新链路：
//      1) 不再绑定全局背景纹理（PlanBackdrop 返回 SceneComposite，纹理为 0）；
//      2) ytbl 回到它自己的"从当前帧缓冲抠区域"路径 —— 那**天生**是 Z 序正确的
//         （此刻帧缓冲里只有先于本面板绘制的内容：同窗口下层组件、更早绘制的
//          其它 ELF 窗口）；
//      3) 本合成器在那个区域上，把**世界底层**（我们图层之下的屏幕内容）
//         合成到它下面：
//             scene = ui + world * (1 - ui.a)      scene.a = 1
//      4) 后续的模糊 / 折射 / 色调映射全部复用 ytbl 原样管线，一行没改。
//
//  为什么 alpha 必须 = 1：见 SceneComposite.h 的 ComposeBackdrop 注释。
//
//  成本：每块玻璃每帧 1 次 FBO 绑定 + 1 个覆盖采样区域的全屏四边形
//        （面积 = 面板 + capture padding），与面板自身的绘制同量级。
// ===========================================================================

#include <cstdint>

#include "third_party/liquidglass/ytbl/GlassUI.h"

namespace aurora {

// "世界底层"的来源：一张覆盖整个宿主屏幕的纹理（屏幕像素 [0,viewW]×[0,viewH]）。
// 关键约束：它必须是**排除了我们自己图层**的采集结果，否则我们上一帧的画面
// 会被当成世界的一部分再合成一次（重影/反馈）。
struct WorldBaseSource {
    unsigned int texture = 0;
    // 供诊断：采集端声明的存储尺寸（可能是降采样后的），合成只用归一化 UV。
    int storageWidth = 0;
    int storageHeight = 0;
    bool ready = false;
};

class GlesSceneCompositor final {
public:
    GlesSceneCompositor() = default;
    ~GlesSceneCompositor();

    GlesSceneCompositor(const GlesSceneCompositor&) = delete;
    GlesSceneCompositor& operator=(const GlesSceneCompositor&) = delete;

    void setWorldBase(const WorldBaseSource& base) {
        base_ = base;
        // 每帧由后端重新递交一次世界底层 ⇒ 这里同时把"本帧已全屏接管"复位。
        fullScreenActive_ = false;
    }
    void clearWorldBase() { base_ = WorldBaseSource{}; fullScreenActive_ = false; }
    const WorldBaseSource& worldBase() const { return base_; }

    // ytbl 场景合成钩子的实现入口。返回 true 表示 captureTexture 已被重写。
    bool compose(const lgx::PanelSampleRect& rect, unsigned int captureTexture,
                 int captureStrideW, int captureStrideH, int viewWidth, int viewHeight);

    // ---------------------------------------------------------------------
    //  全屏接管：把世界底层**不透明地铺满整个视口**
    //
    //  为什么需要它（用户的实测反馈，不是推测）：
    //    我们的图层是覆盖层 ⇒ 窗口外是屏幕**直出**（0 延迟），窗口内是镜像抓来的
    //    帧（1 帧前）。一个运动物体跨过玻璃边界时，两边是两条时间线 —— 用户看到
    //    的"明显延迟 + 撕裂感"就是这个接缝。用户已经把光学参数全部调成 0 验证过：
    //    变成纯透明玻璃板之后现象**依旧**，所以它与光学、与渲染性能都无关。
    //
    //  做法：把世界底层铺满整屏，然后在它上面画玻璃。整块屏幕于是来自同一条链路，
    //        延迟变成**全屏一致**的（实测 0.7 帧 @120Hz，约 5.8ms），接缝消失。
    //        人对"一致的 6ms"几乎无感，对"不一致的 6ms"极其敏感。
    //
    //  前提：镜像要 1:1（--downscale 1），否则整屏都会糊。
    //  安全：世界底层不可用时返回 false（调用方保持透明覆盖层，退回原行为）。
    // ---------------------------------------------------------------------
    // ---------------------------------------------------------------------
    //  ★ 部分接管：region 非空时只在那个矩形内铺满世界底层（像素坐标，左上原点）。
    //    动机：整屏铺满要求玻璃从整屏纹理里做模糊采样，GPU 会从 222MHz 顶到 520MHz，
    //    前台应用跟着抢不到 GPU。只铺"玻璃面板的包围盒并集"，采样量和 GPU 同时下降，
    //    面板外的区域完全不经过我们 —— 那部分既没有延迟也没有开销。
    //    代价：矩形边界上会出现一条时间线接缝（面板外是直出、面板内晚一帧）。
    //    region 为 nullptr 时行为与整屏接管完全一致。
    bool composeFullScreen(int viewWidth, int viewHeight, const float region[4] = nullptr);

    // GL 对象必须在拥有它的上下文仍然 current 时释放。
    void destroy();

    std::uint64_t composedCount() const { return composedCount_; }
    std::uint64_t fullScreenCount() const { return fullScreenCount_; }
    std::uint64_t partialComposeCount() const { return partialComposeCount_; }
    std::uint64_t skippedNoBaseCount() const { return skippedNoBaseCount_; }
    int lastComposedWidth() const { return lastWidth_; }
    int lastComposedHeight() const { return lastHeight_; }

private:
    bool ensureResources();
    bool ensureFramebuffer(unsigned int captureTexture, int storageWidth,
                           int storageHeight);

    WorldBaseSource base_{};
    unsigned int program_ = 0;
    unsigned int framebuffer_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int sourceUniform_ = -1;
    int uvRectUniform_ = -1;
    unsigned int attachedTexture_ = 0;
    int attachedWidth_ = 0;
    int attachedHeight_ = 0;
    bool framebufferComplete_ = false;
    // 本帧是否已经由 composeFullScreen() 铺满世界底层。铺满之后帧缓冲里每个像素
    // 的 alpha 都已经是 1，逐面板再垫一次世界是**纯浪费**（还要两次 FBO 往返），
    // 所以 compose() 会直接跳过。
    bool fullScreenActive_ = false;
    std::uint64_t composedCount_ = 0;
    std::uint64_t fullScreenCount_ = 0;
    std::uint64_t partialComposeCount_ = 0;
    std::uint64_t skippedNoBaseCount_ = 0;
    std::uint64_t fboFailureCount_ = 0;
    int lastWidth_ = 0;
    int lastHeight_ = 0;
};

// 把合成器注册到 ytbl（幂等）。注册后，每次面板抠图都会经过 compose()。
GlesSceneCompositor* InstallGlesSceneCompositor();

// 当前已注册的实例；未注册时返回 nullptr。
GlesSceneCompositor* ActiveGlesSceneCompositor();

}  // namespace aurora
