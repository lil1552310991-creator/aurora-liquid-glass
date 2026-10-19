#include "Material.h"

#include <algorithm>
#include <cmath>

namespace aurora {

void MaterialRenderer::drawGlassFallback(ImDrawList& drawList, const ImVec2& min, const ImVec2& max,
                                         const GlassMaterialSpec& spec) {
    drawList.AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(spec.tint), spec.rounding);
    if (spec.hasBackgroundTexture && spec.backgroundTexture != nullptr) {
        drawList.AddImageRounded(spec.backgroundTexture, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                 IM_COL32(255, 255, 255, 80), spec.rounding);
    }
    const ImVec4 inner = ImVec4(spec.highlight.x, spec.highlight.y, spec.highlight.z,
                                spec.highlight.w * 0.45f);
    drawList.AddRect(min, max, ImGui::ColorConvertFloat4ToU32(spec.border), spec.rounding,
                     ImDrawFlags_RoundCornersAll, spec.borderWidth);
    drawList.AddRect(ImVec2(min.x + spec.highlightWidth, min.y + spec.highlightWidth),
                     ImVec2(max.x - spec.highlightWidth, max.y - spec.highlightWidth),
                     ImGui::ColorConvertFloat4ToU32(inner), std::max(0.0f, spec.rounding - spec.highlightWidth),
                     ImDrawFlags_RoundCornersAll, spec.highlightWidth);
    const float phase = spec.sheenPhase - std::floor(spec.sheenPhase);
    const float sheenX = min.x + (max.x - min.x) * phase;
    drawList.AddRectFilledMultiColor(
        ImVec2(sheenX - 42.0f, min.y + 1.0f), ImVec2(sheenX + 42.0f, max.y - 1.0f),
        IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 24),
        IM_COL32(255, 255, 255, 24), IM_COL32(255, 255, 255, 0));
}

}  // namespace aurora
