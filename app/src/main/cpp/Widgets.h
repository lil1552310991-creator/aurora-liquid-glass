#pragma once

#include "Theme.h"

#include <cstddef>

namespace aurora::Widgets {

bool BeginPanel(const char* id, const Theme& theme, const ImVec2& position, const ImVec2& size,
                float alpha, ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse);
void EndPanel();
bool Button(const char* label, const Theme& theme);
bool Toggle(const char* label, bool* value);
bool Slider(const char* label, float* value, float minValue, float maxValue);
bool Tab(const char* label, bool selected, const Theme& theme);
bool TextInput(const char* label, char* buffer, std::size_t capacity);

}  // namespace aurora::Widgets
