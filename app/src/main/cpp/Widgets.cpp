#include "Widgets.h"

#include "imgui.h"

namespace aurora::Widgets {

bool BeginPanel(const char* id, const Theme& theme, const ImVec2& position, const ImVec2& size,
                float alpha, ImGuiWindowFlags flags) {
    (void)theme;
    ImGui::SetNextWindowPos(position, ImGuiCond_Once);
    ImGui::SetNextWindowSize(size, ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.90f * alpha);
    return ImGui::Begin(id, nullptr, flags | ImGuiWindowFlags_NoResize);
}

void EndPanel() {
    ImGui::End();
}

bool Button(const char* label, const Theme& theme) {
    return ImGui::Button(label, ImVec2(-1.0f, theme.controlHeight));
}

bool Toggle(const char* label, bool* value) {
    return ImGui::Checkbox(label, value);
}

bool Slider(const char* label, float* value, float minValue, float maxValue) {
    return ImGui::SliderFloat(label, value, minValue, maxValue, "%.2f");
}

bool Tab(const char* label, bool selected, const Theme& theme) {
    const float width = ImGui::CalcTextSize(label).x + 28.0f;
    return ImGui::Selectable(label, selected, 0, ImVec2(width, theme.controlHeight));
}

bool TextInput(const char* label, char* buffer, std::size_t capacity) {
    return ImGui::InputText(label, buffer, capacity);
}

}  // namespace aurora::Widgets
