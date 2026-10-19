#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace aurora {

// This is an in-process comparison key, not a disk or network format. Only append
// actual scalar values or explicitly packed data: whole structs can have padding.
template <typename T>
inline void AppendRenderDamageScalar(std::vector<std::uint8_t>& bytes, const T& value) {
    static_assert(std::is_arithmetic_v<T> || std::is_enum_v<T>,
                  "Damage keys must serialize individual scalar fields");
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(T));
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

inline void AppendRenderDamageBytes(std::vector<std::uint8_t>& bytes,
                                    const void* data, std::size_t size) {
    if (size == 0) return;
    const auto offset = bytes.size();
    bytes.resize(offset + size);
    std::memcpy(bytes.data() + offset, data, size);
}

struct RenderDamageState {
    std::vector<std::uint8_t> bytes;
    std::uint64_t backgroundGeneration = 0;
    int width = 0;
    int height = 0;

    bool operator==(const RenderDamageState& other) const {
        return backgroundGeneration == other.backgroundGeneration
            && width == other.width && height == other.height && bytes == other.bytes;
    }
};

// A successful presentation is the only commit point. Comparing against a UI
// build that never reached the screen would suppress a required repaint after a
// failed submit, a frame dump, or an EGL surface replacement.
class RenderDamage {
public:
    bool changed(const RenderDamageState& next) const {
        return !valid_ || !(presented_ == next);
    }

    void commit(const RenderDamageState& next) {
        presented_ = next;
        valid_ = true;
    }

    void invalidate() { valid_ = false; }

private:
    RenderDamageState presented_;
    bool valid_ = false;
};

}  // namespace aurora
