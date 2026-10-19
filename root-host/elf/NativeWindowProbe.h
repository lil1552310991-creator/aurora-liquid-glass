#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace aurora {

// Surface's ANativeWindow base offset is a platform ABI detail. Recognize the
// public native header instead of deriving it from the NDK's max_align_t.
inline int FindNativeWindowOffset(std::span<const std::byte> bytes) {
    constexpr std::uint32_t magic = 0x5f776e64u; // ANDROID_NATIVE_WINDOW_MAGIC
    int found = -1;
    for (std::size_t offset = 0; offset + 8 <= bytes.size(); offset += 8) {
        std::uint32_t header[2]{};
        std::memcpy(header, bytes.data() + offset, sizeof header);
        if (header[0] != magic || header[1] < 64 || header[1] > 4096 || header[1] % 8 != 0) continue;
        if (found >= 0) return -1; // Ambiguous bytes must never become a callable window.
        found = static_cast<int>(offset);
    }
    return found;
}

} // namespace aurora
