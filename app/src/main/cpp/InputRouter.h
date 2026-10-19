#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace aurora {

enum class TouchAction : int {
    Down = 0,
    Up = 1,
    Move = 2,
    Cancel = 3,
    PointerDown = 5,
    PointerUp = 6,
};

struct PointerSnapshot {
    bool active = false;
    int id = -1;
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 0.0f;
    float size = 0.0f;
    int toolType = 0;
    double timeSeconds = 0.0;
};

struct InputSnapshot {
    std::array<PointerSnapshot, 10> pointers{};
    std::size_t activeCount = 0;
    PointerSnapshot primary{};
    bool primaryDown = false;
    bool primaryPressed = false;
    bool primaryReleased = false;
    bool cancelled = false;
};

class InputRouter final {
public:
    void submit(TouchAction action, int pointerId, float x, float y, float pressure, float size,
                int toolType, double timeSeconds);
    void cancelAll(double timeSeconds);
    InputSnapshot snapshot();

private:
    PointerSnapshot* findPointer(int pointerId);
    PointerSnapshot* findOrCreatePointer(int pointerId);

    mutable std::mutex mutex_;
    std::array<PointerSnapshot, 10> pointers_{};
    PointerSnapshot primaryLast_{};
    int primaryId_ = -1;
    bool primaryPressed_ = false;
    bool primaryReleased_ = false;
    bool cancelled_ = false;
};

}  // namespace aurora
