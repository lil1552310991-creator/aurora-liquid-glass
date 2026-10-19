#include "GestureRouter.h"

#include <cassert>
#include <cmath>

int main() {
    aurora::GestureRouter gestures;
    aurora::InputSnapshot one;
    one.activeCount = 1;
    one.pointers[0].active = true;
    one.pointers[0].id = 1;
    one.pointers[0].x = 10.0f;
    one.pointers[0].y = 10.0f;
    auto began = gestures.update(one);
    assert(began.began);

    one.pointers[0].x = 24.0f;
    one.pointers[0].y = 18.0f;
    auto pan = gestures.update(one);
    assert(std::abs(pan.panX - 14.0f) < 0.001f);
    assert(std::abs(pan.panY - 8.0f) < 0.001f);

    aurora::InputSnapshot two = one;
    two.activeCount = 2;
    two.pointers[1].active = true;
    two.pointers[1].id = 2;
    two.pointers[1].x = 44.0f;
    two.pointers[1].y = 18.0f;
    gestures.update(two);
    two.pointers[0].x = 20.0f;
    two.pointers[1].x = 52.0f;
    auto pinch = gestures.update(two);
    assert(pinch.scale > 1.0f);
    assert(pinch.pointerCount == 2);

    two.pointers[0].x = 21.0f;
    two.pointers[0].y = 21.0f;
    two.pointers[1].x = 51.0f;
    two.pointers[1].y = 31.0f;
    auto transform = gestures.update(two);
    assert(transform.panX != 0.0f || transform.panY != 0.0f);
    assert(transform.rotationRadians != 0.0f);

    aurora::InputSnapshot cancelled;
    cancelled.cancelled = true;
    auto cancel = gestures.update(cancelled);
    assert(cancel.cancelled);
    return 0;
}
