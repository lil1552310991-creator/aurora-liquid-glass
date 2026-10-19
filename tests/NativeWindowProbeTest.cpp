#include "NativeWindowProbe.h"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    std::array<std::byte, 256> bytes{};
    auto put = [&](std::size_t offset, std::uint32_t magic, std::uint32_t version) {
        const std::uint32_t header[]{magic, version};
        std::memcpy(bytes.data() + offset, header, sizeof header);
    };
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    put(8, 0x5f776e64u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == 8);
    bytes.fill(std::byte{});
    put(16, 0x5f776e64u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == 16);
    assert(aurora::FindNativeWindowOffset(std::span<const std::byte>(bytes.data(), 23)) == -1);
    assert(aurora::FindNativeWindowOffset(std::span<const std::byte>(bytes.data(), 24)) == 16);
    put(32, 0x5f776e64u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    bytes.fill(std::byte{});
    put(16, 0x5f776e64u, 0);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    put(16, 0x5f776e64u, 4097);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    put(16, 0x5f776e64u, 231);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    put(16, 0x5f776e63u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    bytes.fill(std::byte{});
    put(17, 0x5f776e64u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == -1);
    bytes.fill(std::byte{});
    put(248, 0x5f776e64u, 232);
    assert(aurora::FindNativeWindowOffset(bytes) == 248);
    std::puts("NativeWindowProbeTest: 12 ABI header checks passed");
}
