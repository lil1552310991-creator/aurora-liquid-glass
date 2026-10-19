#pragma once

#include "InputRouter.h"

#include <cstddef>

namespace aurora {

struct GestureFrame final {
    float panX = 0.0f;
    float panY = 0.0f;
    float scale = 1.0f;
    float rotationRadians = 0.0f;
    std::size_t pointerCount = 0;
    bool began = false;
    bool changed = false;
    bool ended = false;
    bool cancelled = false;
};

class GestureRouter final {
public:
    GestureFrame update(const InputSnapshot& input);
    void cancel();

private:
    static float distance(const PointerSnapshot& a, const PointerSnapshot& b);
    static float angle(const PointerSnapshot& a, const PointerSnapshot& b);

    InputSnapshot previous_{};
    bool active_ = false;
    float previousDistance_ = 0.0f;
    float previousAngle_ = 0.0f;
};

}  // namespace aurora
