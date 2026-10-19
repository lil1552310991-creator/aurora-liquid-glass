#include "FrameStats.h"

#include <cassert>

int main() {
    aurora::FrameStats stats;
    stats.tick(1.0f / 120.0f);
    stats.tick(1.0f / 60.0f);
    assert(stats.frameCount == 2);
    assert(stats.smoothedFps > 60.0f && stats.smoothedFps <= 120.0f);
    assert(stats.worstFrameMilliseconds >= 16.6f);
    return 0;
}
