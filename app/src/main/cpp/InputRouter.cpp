#include "InputRouter.h"

#include <algorithm>

namespace aurora {

PointerSnapshot* InputRouter::findPointer(int pointerId) {
    for (PointerSnapshot& pointer : pointers_) {
        if (pointer.active && pointer.id == pointerId) {
            return &pointer;
        }
    }
    return nullptr;
}

PointerSnapshot* InputRouter::findOrCreatePointer(int pointerId) {
    if (PointerSnapshot* existing = findPointer(pointerId); existing != nullptr) {
        return existing;
    }
    for (PointerSnapshot& pointer : pointers_) {
        if (!pointer.active) {
            pointer = {};
            pointer.active = true;
            pointer.id = pointerId;
            return &pointer;
        }
    }
    return nullptr;
}

void InputRouter::submit(TouchAction action, int pointerId, float x, float y, float pressure, float size,
                         int toolType, double timeSeconds) {
    std::lock_guard lock(mutex_);
    if (action == TouchAction::Cancel) {
        for (PointerSnapshot& pointer : pointers_) {
            pointer.active = false;
        }
        cancelled_ = true;
        primaryPressed_ = false;
        primaryReleased_ = true;
        primaryId_ = -1;
        return;
    }

    if (action == TouchAction::Down || action == TouchAction::PointerDown) {
        PointerSnapshot* pointer = findOrCreatePointer(pointerId);
        if (pointer == nullptr) {
            return;
        }
        pointer->x = x;
        pointer->y = y;
        pointer->pressure = pressure;
        pointer->size = size;
        pointer->toolType = toolType;
        pointer->timeSeconds = timeSeconds;
        if (action == TouchAction::Down) {
            primaryId_ = pointerId;
            primaryLast_ = *pointer;
            primaryPressed_ = true;
        }
        return;
    }

    PointerSnapshot* pointer = findPointer(pointerId);
    if (pointer == nullptr) {
        if (action == TouchAction::Move) {
            pointer = findOrCreatePointer(pointerId);
        }
        if (pointer == nullptr) {
            return;
        }
    }
    pointer->x = x;
    pointer->y = y;
    pointer->pressure = pressure;
    pointer->size = size;
    pointer->toolType = toolType;
    pointer->timeSeconds = timeSeconds;
    if (pointerId == primaryId_) {
        primaryLast_ = *pointer;
    }

    if (action == TouchAction::Up || action == TouchAction::PointerUp) {
        if (pointerId == primaryId_) {
            primaryReleased_ = true;
            primaryId_ = -1;
        }
        pointer->active = false;
        if (pointerId == primaryLast_.id) {
            for (const PointerSnapshot& candidate : pointers_) {
                if (candidate.active) {
                    primaryId_ = candidate.id;
                    primaryLast_ = candidate;
                    primaryPressed_ = true;
                    break;
                }
            }
        }
    }
}

void InputRouter::cancelAll(double timeSeconds) {
    std::lock_guard lock(mutex_);
    for (PointerSnapshot& pointer : pointers_) {
        pointer.active = false;
        pointer.timeSeconds = timeSeconds;
    }
    cancelled_ = true;
    primaryPressed_ = false;
    primaryReleased_ = true;
    primaryId_ = -1;
}

InputSnapshot InputRouter::snapshot() {
    std::lock_guard lock(mutex_);
    InputSnapshot result;
    result.primaryPressed = primaryPressed_;
    result.primaryReleased = primaryReleased_;
    result.cancelled = cancelled_;
    primaryPressed_ = false;
    primaryReleased_ = false;
    cancelled_ = false;

    std::size_t outputIndex = 0;
    for (const PointerSnapshot& pointer : pointers_) {
        if (!pointer.active || outputIndex >= result.pointers.size()) {
            continue;
        }
        result.pointers[outputIndex++] = pointer;
    }
    result.activeCount = outputIndex;
    result.primary = primaryLast_;
    if (primaryId_ >= 0) {
        for (std::size_t i = 0; i < outputIndex; ++i) {
            if (result.pointers[i].id == primaryId_) {
                result.primaryDown = true;
                result.primary = result.pointers[i];
                break;
            }
        }
    }
    return result;
}

}  // namespace aurora
