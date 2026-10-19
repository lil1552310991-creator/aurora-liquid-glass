#pragma once

#include "imgui.h"

namespace aurora {

struct GlassMaterialSpec final {
    ImVec4 tint = ImVec4(0.10f, 0.14f, 0.22f, 0.78f);
    ImVec4 highlight = ImVec4(0.72f, 0.84f, 1.0f, 0.20f);
    ImVec4 border = ImVec4(0.72f, 0.84f, 1.0f, 0.30f);
    float rounding = 18.0f;
    float borderWidth = 1.0f;
    float highlightWidth = 1.0f;
    bool hasBackgroundTexture = false;
    ImTextureID backgroundTexture = nullptr;
    float sheenPhase = 0.0f;
};

class MaterialRenderer final {
public:
    static void drawGlassFallback(ImDrawList& drawList, const ImVec2& min, const ImVec2& max,
                                  const GlassMaterialSpec& spec);
};

}  // namespace aurora
