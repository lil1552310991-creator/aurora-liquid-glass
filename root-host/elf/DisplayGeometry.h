#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace aurora {

// Coordinates in the logical layer stack. SurfaceFlinger applies physical rotation.
struct DisplayGeometry {
    int width = 0;
    int height = 0;
    int rotation = 0;
    std::uint32_t generation = 0;
    bool valid() const { return width > 0 && height > 0 && width <= 32768 && height <= 32768 && rotation >= 0 && rotation < 4; }
    bool operator==(const DisplayGeometry&) const = default;
};

// The base description has natural dimensions; the override has the active rotation.
// Parse only the selected logical display and prefer its non-null override.
inline DisplayGeometry ParseDisplayGeometry(const std::string& dump, int logicalDisplayId) {
    DisplayGeometry base;
    DisplayGeometry overrideGeometry;
    for (std::size_t start = 0; start < dump.size();) {
        const auto end = dump.find('\n', start);
        const auto line = dump.substr(start, (end == std::string::npos ? dump.size() : end) - start);
        start = end == std::string::npos ? dump.size() : end + 1;
        const bool isOverride = line.find("mOverrideDisplayInfo=DisplayInfo{") != std::string::npos;
        const bool isBase = line.find("mBaseDisplayInfo=DisplayInfo{") != std::string::npos;
        if (!isOverride && !isBase) continue;
        const auto idAt = line.find("displayId ");
        if (idAt == std::string::npos) continue;
        char* tail = nullptr;
        const long id = std::strtol(line.c_str() + idAt + 10, &tail, 10);
        if (id != logicalDisplayId || tail == line.c_str() + idAt + 10 || *tail != ',') continue;
        const auto realAt = line.find("real ");
        const auto rotationAt = line.find(", rotation ");
        if (realAt == std::string::npos || rotationAt == std::string::npos) continue;
        DisplayGeometry value;
        const char* widthAt = line.c_str() + realAt + 5;
        const long width = std::strtol(widthAt, &tail, 10);
        if (tail == widthAt || line.compare(static_cast<std::size_t>(tail - line.c_str()), 3, " x ") != 0) continue;
        const char* heightAt = tail + 3;
        const long height = std::strtol(heightAt, &tail, 10);
        if (tail == heightAt || *tail != ',') continue;
        const char* rotationValue = line.c_str() + rotationAt + 11;
        const long rotation = std::strtol(rotationValue, &tail, 10);
        if (tail == rotationValue || *tail != ',' || width <= 0 || height <= 0 ||
            width > 32768 || height > 32768 || rotation < 0 || rotation > 3) continue;
        value.width = static_cast<int>(width);
        value.height = static_cast<int>(height);
        value.rotation = static_cast<int>(rotation);
        if (isOverride) overrideGeometry = value;
        else base = value;
    }
    return overrideGeometry.valid() ? overrideGeometry : base;
}

// 从 `wm size` 的输出里取屏幕尺寸。
//
// 输出形如（Override 优先，它才是当前生效的）：
//   Physical size: 1440x3168
//   Override size: 1080x2376
//
// 这是 DisplayInfo 解析失败时的兜底：不提供旋转信息（按 0 处理），但保证
// "至少能拿到一个可用的宽高"，而不是因为 dumpsys 格式不同就拒绝启动。
inline bool ParseWmSize(const std::string& dump, int* outWidth, int* outHeight) {
    if (outWidth == nullptr || outHeight == nullptr) return false;
    const char* keys[2] = {"Override size:", "Physical size:"};
    for (const char* key : keys) {
        const std::size_t pos = dump.find(key);
        if (pos == std::string::npos) continue;
        const char* p = dump.c_str() + pos + std::strlen(key);
        while (*p == ' ') ++p;
        char* tail = nullptr;
        const long w = std::strtol(p, &tail, 10);
        if (tail == p) continue;
        // 分隔符允许 `x` / `X`，且**两侧都可以有空格**（不同 ROM 的 wm 输出并不统一，
        // 真机上确实见过 "1080 X 2376" 这种写法）。离线用例钉住了这一条。
        while (*tail == ' ') ++tail;
        if (*tail != 'x' && *tail != 'X') continue;
        ++tail;
        while (*tail == ' ') ++tail;
        const long h = std::strtol(tail, &tail, 10);
        if (*tail != '\0' && *tail != '\n' && *tail != '\r' && *tail != ' ' && *tail != ',') continue;
        if (w <= 0 || h <= 0 || w > 32768 || h > 32768) continue;
        *outWidth = static_cast<int>(w);
        *outHeight = static_cast<int>(h);
        return true;
    }
    return false;
}

} // namespace aurora
