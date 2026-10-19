#pragma once

#include <cstdint>
#include <limits>

namespace aurora {

enum class VisibleUiShapeKind : std::uint8_t {
    Rectangle,
    RoundedRectangle,
};

// A visible input surface in physical display pixels. Keep the shape's original
// bounds when clipping: moving a clipped corner must not create a new round arc.
// Shadows are deliberately excluded because they do not receive input.
struct VisibleUiShape {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float radius = 0.0f;
    VisibleUiShapeKind shape = VisibleUiShapeKind::Rectangle;
    // TL, TR, BR, BL. ImGui's expanded title strip rounds only its top corners.
    std::uint8_t roundedCorners = 0x0fu;
    float clipLeft = -std::numeric_limits<float>::infinity();
    float clipTop = -std::numeric_limits<float>::infinity();
    float clipRight = std::numeric_limits<float>::infinity();
    float clipBottom = std::numeric_limits<float>::infinity();
};

}  // namespace aurora
