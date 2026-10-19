#pragma once

#include "imgui.h"

namespace aurora {

struct Theme final {
    ImVec4 background = ImVec4(0.035f, 0.045f, 0.070f, 1.0f);
    ImVec4 panel = ImVec4(0.055f, 0.065f, 0.095f, 0.96f);
    ImVec4 panelElevated = ImVec4(0.09f, 0.105f, 0.145f, 0.84f);
    ImVec4 surface = ImVec4(0.14f, 0.17f, 0.23f, 0.92f);
    ImVec4 accent = ImVec4(0.35f, 0.58f, 0.98f, 1.0f);
    ImVec4 accentPressed = ImVec4(0.20f, 0.38f, 0.76f, 1.0f);
    ImVec4 text = ImVec4(0.93f, 0.95f, 1.0f, 1.0f);
    ImVec4 textMuted = ImVec4(0.58f, 0.64f, 0.74f, 1.0f);
    ImVec4 divider = ImVec4(0.22f, 0.27f, 0.38f, 0.6f);
    float windowRounding = 18.0f;
    float controlRounding = 10.0f;
    float windowPadding = 22.0f;
    float controlHeight = 44.0f;
    float itemGap = 12.0f;

    void apply() const {
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = windowRounding;
        style.ChildRounding = windowRounding;
        style.FrameRounding = controlRounding;
        style.GrabRounding = controlRounding;
        style.PopupRounding = controlRounding;
        style.WindowPadding = ImVec2(windowPadding, windowPadding);
        style.FramePadding = ImVec2(14.0f, 10.0f);
        style.ItemSpacing = ImVec2(itemGap, itemGap);
        style.Colors[ImGuiCol_WindowBg] = panel;
        style.Colors[ImGuiCol_ChildBg] = panelElevated;
        style.Colors[ImGuiCol_FrameBg] = surface;
        style.Colors[ImGuiCol_FrameBgHovered] = accent;
        style.Colors[ImGuiCol_FrameBgActive] = accentPressed;
        style.Colors[ImGuiCol_Button] = accent;
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x + 0.10f, accent.y + 0.10f, accent.z, 1.0f);
        style.Colors[ImGuiCol_ButtonActive] = accentPressed;
        style.Colors[ImGuiCol_Text] = text;
        style.Colors[ImGuiCol_TextDisabled] = textMuted;
        style.Colors[ImGuiCol_Separator] = divider;
        style.Colors[ImGuiCol_CheckMark] = text;
        style.Colors[ImGuiCol_SliderGrab] = accent;
        style.Colors[ImGuiCol_SliderGrabActive] = text;
    }
};

}  // namespace aurora
