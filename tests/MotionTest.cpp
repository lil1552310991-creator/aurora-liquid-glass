#include "Motion.h"

#include <cassert>
#include <cmath>

int main() {
    aurora::Spring spring(0.0f);
    spring.setTarget(1.0f);
    for (int frame = 0; frame < 240; ++frame) {
        spring.tick(1.0f / 120.0f);
    }
    assert(std::abs(spring.value() - 1.0f) < 0.01f);

    spring.setTarget(0.0f);
    for (int frame = 0; frame < 240; ++frame) {
        spring.tick(1.0f / 120.0f);
    }
    assert(std::abs(spring.value()) < 0.01f);
    return 0;
}
