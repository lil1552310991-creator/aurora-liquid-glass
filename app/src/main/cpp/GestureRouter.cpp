#include "GestureRouter.h"

#include <algorithm>
#include <cmath>

namespace aurora {

float GestureRouter::distance(const PointerSnapshot& a, const PointerSnapshot& b) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    return std::sqrt(dx * dx + dy * dy);
}

float GestureRouter::angle(const PointerSnapshot& a, const PointerSnapshot& b) {
    return std::atan2(b.y - a.y, b.x - a.x);
}

GestureFrame GestureRouter::update(const InputSnapshot& input) {
    GestureFrame frame;
    frame.pointerCount = input.activeCount;
    frame.cancelled = input.cancelled;
    if (input.cancelled) {
        cancel();
        return frame;
    }
    if (input.activeCount == 0) {
        if (active_) {
            frame.ended = true;
        }
        active_ = false;
        previous_ = input;
        return frame;
    }

    if (!active_) {
        active_ = true;
        frame.began = true;
    }
    frame.changed = true;
    if (input.activeCount == 1 && previous_.activeCount >= 1) {
        frame.panX = input.pointers[0].x - previous_.pointers[0].x;
        frame.panY = input.pointers[0].y - previous_.pointers[0].y;
    } else if (input.activeCount >= 2) {
        const PointerSnapshot& first = input.pointers[0];
        const PointerSnapshot& second = input.pointers[1];
        const float currentDistance = distance(first, second);
        const float currentAngle = angle(first, second);
        if (previous_.activeCount >= 2) {
            const PointerSnapshot& oldFirst = previous_.pointers[0];
            const PointerSnapshot& oldSecond = previous_.pointers[1];
            frame.panX = ((first.x + second.x) - (oldFirst.x + oldSecond.x)) * 0.5f;
            frame.panY = ((first.y + second.y) - (oldFirst.y + oldSecond.y)) * 0.5f;
            if (previousDistance_ > 0.001f) {
                frame.scale = currentDistance / previousDistance_;
            }
            frame.rotationRadians = currentAngle - previousAngle_;
            while (frame.rotationRadians > 3.14159265359f) {
                frame.rotationRadians -= 6.28318530718f;
            }
            while (frame.rotationRadians < -3.14159265359f) {
                frame.rotationRadians += 6.28318530718f;
            }
        }
        previousDistance_ = currentDistance;
        previousAngle_ = currentAngle;
    }
    previous_ = input;
    return frame;
}

void GestureRouter::cancel() {
    active_ = false;
    previous_ = {};
    previousDistance_ = 0.0f;
    previousAngle_ = 0.0f;
}

}  // namespace aurora
