#pragma once

#include <cstdint>
#include <cstdlib>
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

} // namespace aurora
