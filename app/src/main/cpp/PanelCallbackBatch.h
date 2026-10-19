#pragma once

#include <cstdint>
#include <imgui.h>

namespace aurora {

// ImDrawList::AddCallback always leaves an unused command after the callback.
// Replace only a consecutive Capture -> Reset -> unused tail. A track fill,
// clip change, texture change or vertex/index offset change must retain the
// original split callbacks so capture observes precisely the same backdrop.
inline bool CombineAdjacentPanelCallbacks(ImDrawList& draw, int panelIndex,
                                          ImDrawCallback captureCallback,
                                          ImDrawCallback combinedCallback) {
    if (draw.CmdBuffer.Size < 3) return false;
    ImDrawCmd& capture = draw.CmdBuffer[draw.CmdBuffer.Size - 3];
    const ImDrawCmd& reset = draw.CmdBuffer[draw.CmdBuffer.Size - 2];
    const ImDrawCmd& unused = draw.CmdBuffer[draw.CmdBuffer.Size - 1];
    if (capture.UserCallback != captureCallback || capture.ElemCount != 0 ||
        capture.UserCallbackData != reinterpret_cast<void*>(static_cast<intptr_t>(panelIndex)) ||
        reset.UserCallback != ImDrawCallback_ResetRenderState || reset.ElemCount != 0 ||
        unused.UserCallback != nullptr || unused.ElemCount != 0) {
        return false;
    }
    const auto sameHeader = [](const ImDrawCmd& lhs, const ImDrawCmd& rhs) {
        return lhs.ClipRect.x == rhs.ClipRect.x && lhs.ClipRect.y == rhs.ClipRect.y &&
               lhs.ClipRect.z == rhs.ClipRect.z && lhs.ClipRect.w == rhs.ClipRect.w &&
               lhs.TextureId == rhs.TextureId && lhs.VtxOffset == rhs.VtxOffset &&
               lhs.IdxOffset == rhs.IdxOffset;
    };
    if (!sameHeader(capture, reset) || !sameHeader(capture, unused)) return false;
    capture.UserCallback = combinedCallback;
    return true;
}

}  // namespace aurora
