#include "OverlayTouchRegion.h"
#include <cassert>
#include <cstdio>
#include <limits>

using namespace aurora;
int main() {
    // The old envelope included both the window gap and the empty right strip.
    auto region = BuildTouchShapeRegion({{16,16,776,716,28}, {16,730,776,2130,12}},1080,2376);
    assert(TouchRegionContains(region,400,400));
    assert(!TouchRegionContains(region,400,722));
    assert(!TouchRegionContains(region,850,400));
    assert(!TouchRegionContains(region,16,16));
    assert(TouchRegionContains(region,44,16));
    // Free dragging removes the old input coverage; it never accumulates.
    const auto moved = BuildTouchShapeRegion({{216,116,976,816,28}},1080,2376);
    assert(!TouchRegionContains(moved,100,400));
    assert(TouchRegionContains(moved,900,400));
    // Closing/collapsing removes content immediately, while a popup remains.
    assert(BuildTouchShapeRegion({},1080,2376).empty());
    const auto collapsed = BuildTouchShapeRegion({{16,730,776,770,12},{800,400,1000,600,8}},1080,2376);
    assert(!TouchRegionContains(collapsed,300,800));
    assert(TouchRegionContains(collapsed,900,500));
    // Overlapping windows and nested popups are an exact union, with no holes added.
    auto united = UnionTouchRects({{0,0,20,20},{10,10,30,30},{40,0,50,30}});
    for (int y = -1; y < 32; ++y) for (int x = -1; x < 52; ++x)
        assert(TouchRegionContains(united,x,y) ==
               ((x>=0 && x<20 && y>=0 && y<20) ||
                (x>=10 && x<30 && y>=10 && y<30) ||
                (x>=40 && x<50 && y>=0 && y<30)));
    assert(UnionTouchRects(united) == united);
    assert(UnionTouchRects({{0,0,10,10},{0,10,10,20}}) ==
           std::vector<TouchRect>({{0,0,10,20}}));
    // Screen clipping and exact half-open pixel boundaries.
    auto clipped = BuildTouchShapeRegion({{-100,-100,20,20,0},{90,90,200,200,0}},100,100);
    assert(TouchRegionContains(clipped,0,0));
    assert(!TouchRegionContains(clipped,20,0));
    assert(TouchRegionContains(clipped,99,99));
    assert(!TouchRegionContains(clipped,100,99));
    // Individual corner flags match a straight titlebar/body seam.
    auto upperOnly = BuildTouchShapeRegion({{0,0,100,100,20,3}},100,100);
    assert(!TouchRegionContains(upperOnly,0,0));
    assert(TouchRegionContains(upperOnly,0,99));
    auto contentClip = BuildTouchShapeRegion({{0,0,100,100,20,15,10,30,90,100}},100,100);
    assert(!TouchRegionContains(contentClip,50,29));
    assert(TouchRegionContains(contentClip,10,30));
    assert(!TouchRegionContains(contentClip,10,99));
    // The renderer's snapshot is the actual production input to this rasteriser.
    VisibleUiShape visible{0,0,100,100,20,VisibleUiShapeKind::RoundedRectangle,3,0,10,100,100};
    auto visibleRegion = BuildOverlayTouchRegion(std::vector<VisibleUiShape>{visible},100,100);
    assert(!TouchRegionContains(visibleRegion,50,9));
    assert(TouchRegionContains(visibleRegion,0,99));
    visible.shape = VisibleUiShapeKind::Rectangle;
    visible.clipTop = 0;
    assert(TouchRegionContains(BuildOverlayTouchRegion(std::vector<VisibleUiShape>{visible},100,100),0,0));
    // Invalid input and extreme coordinates remain bounded and inert.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assert(BuildTouchShapeRegion({{nan,0,20,20,1},{1,1,0,0,2}},100,100).empty());
    assert(BuildTouchShapeRegion({{-1e30f,-1e30f,1e30f,1e30f,0}},100,100) ==
           std::vector<TouchRect>({{0,0,100,100}}));
    // ── WindowInfo 布局：Region 偏移必须取设备实测值，不能写死 ──────────────
    size_t offset = 0;
    const uint32_t devThunk[]   = {0xd503245fU,0x9103e000U,0x14001e0dU};  // 开发机：ADD x0,x0,#248
    const uint32_t aospThunk[]  = {0xd503245fU,0x9102e000U,0x14001e0dU};  // 纯 AOSP：ADD x0,x0,#184
    const uint32_t plus8Thunk[] = {0xd503245fU,0x91030000U,0x14001e0dU};  // 另一种插入：ADD #192
    const uint32_t wrongRegister[] = {0xd503245fU,0x9103e020U,0x14001e0dU};
    const uint32_t unalignedThunk[] = {0xd503245fU,0x9103e800U,0x14001e0dU}; // #250
    const uint32_t tinyThunk[]  = {0xd503245fU,0x91000000U,0x14001e0dU};  // #0
    const uint32_t hugeThunk[]  = {0xd503245fU,0x913e0000U,0x14001e0dU};  // #3968
    assert(DecodeWindowInfoRegionOffset(devThunk,3,&offset) && offset==248);
    // ★★ 换机回归：**Region 仍在 AOSP 的 184 的设备必须被接受**。
    //    旧代码写死 `decoded != 248 → 拒绝`，于是朋友的另一台设备 上
    //    replaceTouchableRegionRects() 第一行就 return false ⇒ 拦截失效，
    //    并走进失败清理路径崩掉（SIGSEGV，出错地址 0x4 = 空 mRefs 的原子加）。
    //    这条断言就是那次事故本身。
    assert(DecodeWindowInfoRegionOffset(aospThunk,3,&offset) && offset==184);
    assert(DecodeWindowInfoRegionOffset(plus8Thunk,3,&offset) && offset==192);
    // 形状/合理性不对的仍然拒绝
    assert(!DecodeWindowInfoRegionOffset(wrongRegister,3,&offset));
    assert(!DecodeWindowInfoRegionOffset(unalignedThunk,3,&offset));  // 必须 8 字节对齐
    assert(!DecodeWindowInfoRegionOffset(tinyThunk,3,&offset));       // 落进标量前缀
    assert(!DecodeWindowInfoRegionOffset(hugeThunk,3,&offset));       // 装不进 WindowInfo
    assert(!DecodeWindowInfoRegionOffset(devThunk,1,&offset));        // 指令条数不够
    // 位移数学：参照是**本文件常量自己的参照**（开发机 248），不是 AOSP 的 184。
    // 若按 184 当参照，开发机上会重复位移一次（inputConfig 392→456）反而写错。
    assert(WindowInfoLayoutShift(248) == 0);     // 开发机：常量原样可用
    assert(WindowInfoLayoutShift(184) == -64);   // 纯 AOSP：≥248 的常量整体前移
    assert(WindowInfoLayoutShift(192) == -56);
    assert(ShiftWindowInfoOffset(8, -64) == 8);      // token 在前缀里，不动
    assert(ShiftWindowInfoOffset(96, -64) == 96);    // alpha 在前缀里，不动
    assert(ShiftWindowInfoOffset(392, 0) == 392);    // 开发机 inputConfig 实测值
    assert(ShiftWindowInfoOffset(392, -64) == 328);  // 纯 AOSP 布局的 inputConfig
    assert(ShiftWindowInfoOffset(368, -64) == 304);  // packageName 同理
    assert(ShiftWindowInfoOffset(248, -64) == 184);  // Region 回到 AOSP 位置
    std::puts("OverlayTouchRegionTest: 9 regression groups passed");
}
