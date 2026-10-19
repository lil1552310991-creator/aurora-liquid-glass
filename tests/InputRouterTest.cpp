#include "InputRouter.h"

#include <cassert>

int main() {
    aurora::InputRouter router;
    router.submit(aurora::TouchAction::Down, 7, 10.0f, 20.0f, 0.5f, 0.25f, 1, 1.0);
    auto first = router.snapshot();
    assert(first.activeCount == 1);
    assert(first.primaryDown);
    assert(first.primaryPressed);
    assert(first.primary.id == 7);
    assert(first.primary.pressure == 0.5f);
    assert(first.primary.size == 0.25f);
    assert(first.primary.toolType == 1);

    router.submit(aurora::TouchAction::PointerDown, 9, 30.0f, 40.0f, 0.8f, 0.5f, 2, 1.1);
    router.submit(aurora::TouchAction::Move, 9, 35.0f, 45.0f, 0.7f, 0.4f, 2, 1.2);
    auto second = router.snapshot();
    assert(second.activeCount == 2);
    assert(second.primary.id == 7);
    assert(second.pointers[0].id == 7);
    assert(second.pointers[1].id == 9);
    assert(second.pointers[1].x == 35.0f);

    router.submit(aurora::TouchAction::PointerUp, 7, 12.0f, 22.0f, 0.4f, 0.2f, 1, 1.3);
    auto third = router.snapshot();
    assert(third.activeCount == 1);
    assert(third.primaryDown);
    assert(third.primaryReleased);
    assert(third.primaryPressed);
    assert(third.primary.id == 9);
    assert(third.primary.x == 35.0f);

    router.submit(aurora::TouchAction::Move, 9, 40.0f, 48.0f, 0.7f, 0.4f, 2, 1.4);
    auto fourth = router.snapshot();
    assert(fourth.activeCount == 1);
    assert(fourth.primaryDown);
    assert(fourth.primary.id == 9);
    assert(fourth.primary.x == 40.0f);

    router.submit(aurora::TouchAction::Up, 9, 40.0f, 48.0f, 0.7f, 0.4f, 2, 1.5);
    auto fifth = router.snapshot();
    assert(fifth.activeCount == 0);
    assert(!fifth.primaryDown);
    assert(fifth.primaryReleased);

    router.cancelAll(2.0);
    auto cancelled = router.snapshot();
    assert(cancelled.activeCount == 0);
    assert(cancelled.cancelled);
    // A display rotation cancels a DOWN which may not have reached a frame yet.
    router.submit(aurora::TouchAction::Down, 11, 50.0f, 60.0f, 1.0f, 1.0f, 1, 3.0);
    router.cancelAll(3.1);
    cancelled = router.snapshot();
    assert(cancelled.cancelled && cancelled.activeCount == 0);
    assert(!cancelled.primaryPressed && !cancelled.primaryDown && cancelled.primaryReleased);
    router.submit(aurora::TouchAction::Down, 12, 50.0f, 60.0f, 1.0f, 1.0f, 1, 4.0);
    router.submit(aurora::TouchAction::Cancel, 12, 50.0f, 60.0f, 1.0f, 1.0f, 1, 4.1);
    cancelled = router.snapshot();
    assert(cancelled.cancelled && !cancelled.primaryPressed && !cancelled.primaryDown);
    return 0;
}
