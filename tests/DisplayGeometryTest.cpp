#include "DisplayGeometry.h"
#include <cassert>
#include <cstdio>

int main() {
    using aurora::ParseDisplayGeometry;
    const char* dump =
        "mBaseDisplayInfo=DisplayInfo{\"built-in\", displayId 0, real 1080 x 2376, density 640, rotation 0, layerStack 0}\n"
        "mOverrideDisplayInfo=DisplayInfo{\"built-in\", displayId 0, real 2376 x 1080, density 480, rotation 1, layerStack 0}\n"
        "mBaseDisplayInfo=DisplayInfo{\"mirror\", displayId 10, real 540 x 1188, rotation 0, layerStack 10}\n";
    auto g = ParseDisplayGeometry(dump, 0);
    assert(g.valid() && g.width == 2376 && g.height == 1080 && g.rotation == 1);
    g = ParseDisplayGeometry(dump, 10);
    assert(g.valid() && g.width == 540 && g.height == 1188 && g.rotation == 0);
    assert(!ParseDisplayGeometry(dump, 1).valid());
    assert(!ParseDisplayGeometry(dump, 100).valid());
    g = ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, real 1081 x 2377, rotation 2, layerStack 0}\nmOverrideDisplayInfo=null\n", 0);
    assert(g.valid() && g.width == 1081 && g.height == 2377 && g.rotation == 2);
    assert(!ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, real 0 x 2376, rotation 0, layerStack 0}\n", 0).valid());
    assert(!ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, real 1080 x 2376, rotation 4, layerStack 0}\n", 0).valid());
    assert(!ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, real 99999999999 x 2376, rotation 0, layerStack 0}\n", 0).valid());
    assert(!ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, real 1080x2376, rotation 0, layerStack 0}\n", 0).valid());
    assert(!ParseDisplayGeometry("mBaseDisplayInfo=DisplayInfo{displayId 0, rotation 0, real 1080", 0).valid());
    auto reversed = g; reversed.rotation = 0;
    assert(reversed != g);
    reversed = g; ++reversed.generation;
    assert(reversed != g);
    // ── ParseWmSize 兜底：dumpsys 格式对不上时用它取尺寸 ───────────────────
    // 这是"某些厂商 ROM 的 DisplayInfo 排版不同 ⇒ 读不到尺寸 ⇒ 拒绝启动"的补丁，
    // 所以每条边界都要钉住：Override 优先、单行/多行都认、坏数据必须拒绝。
    {
        using aurora::ParseWmSize;
        int w = 0, h = 0;
        // 有 Override 时用它（Override 才是当前生效的尺寸）
        assert(ParseWmSize("Physical size: 1440x3168\nOverride size: 1080x2376\n", &w, &h));
        assert(w == 1080 && h == 2376);
        // 只有 Physical
        assert(ParseWmSize("Physical size: 1440x3168\n", &w, &h) && w == 1440 && h == 3168);
        // 字段顺序颠倒也要认
        assert(ParseWmSize("Override size: 720x1600\nPhysical size: 1440x3168\n", &w, &h));
        assert(w == 720 && h == 1600);
        // 大写 X 与多余空格
        assert(ParseWmSize("Physical size:  1080 X 2376", &w, &h) && w == 1080 && h == 2376);
        // 垃圾/缺失必须拒绝
        assert(!ParseWmSize("", &w, &h));
        assert(!ParseWmSize("Physical size: unknown", &w, &h));
        assert(!ParseWmSize("Physical size: 0x2376", &w, &h));
        assert(!ParseWmSize("Physical size: 1080x", &w, &h));
        assert(!ParseWmSize("Physical size: 99999x2376", &w, &h));
        // 空指针不能崩
        assert(!ParseWmSize("Physical size: 1080x2376", nullptr, &h));
    }
    std::puts("DisplayGeometryTest: 12 + 12 checks passed");
}
