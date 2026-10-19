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
    std::puts("DisplayGeometryTest: 12 checks passed");
}
