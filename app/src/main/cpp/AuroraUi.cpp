#include "AuroraUi.h"

#include "imgui.h"
#include "Material.h"
#include "Widgets.h"

#include <algorithm>
#include <cstring>

#if defined(__ANDROID__)
#include <vulkan/vulkan.h>
#endif

namespace aurora {

AuroraUi::AuroraUi() : panelReveal_(1.0f) {}

bool AuroraUi::consumeVulkanProbeRequest() {
    const bool requested = vulkanProbeRequested_;
    vulkanProbeRequested_ = false;
    return requested;
}

void AuroraUi::appendText(const char* text) {
    if (text != nullptr && text_[0] != '\0') {
        const std::size_t used = std::strlen(text_.data());
        const std::size_t available = text_.size() - used - 1;
        std::strncat(text_.data(), text, available);
    } else if (text != nullptr) {
        std::strncpy(text_.data(), text, text_.size() - 1);
        text_.back() = '\0';
    }
}

void AuroraUi::deleteBackward() {
    const std::size_t used = std::strlen(text_.data());
    if (used != 0) {
        text_[used - 1] = '\0';
    }
}

void AuroraUi::resetForGraphicsContext() {
    panelReveal_.snap(panelOpen_ ? 1.0f : 0.0f);
}

void AuroraUi::draw(const InputSnapshot& input, const GestureFrame& gesture, const BackendDescriptor& backend,
                    const BackendDescriptor& vulkanProbe,
                    const HostCapabilities& host,
                    const FrameStats& frameStats,
                    float deltaSeconds, float density, float width, float height) {
    (void)density;
    panelRegion_ = {};
    textInputRequested_ = false;
    materialTime_ += deltaSeconds;
    theme_.apply();
    panelReveal_.setTarget(panelOpen_ ? 1.0f : 0.0f);
    const float reveal = panelReveal_.tick(deltaSeconds);
    if (reveal <= 0.001f) {
        return;
    }

    const bool overlay = host.mode == HostMode::ApplicationOverlay;
    const float margin = overlay ? 8.0f : 24.0f;
    const float safeLeft = host.safeRegion.right > host.safeRegion.left ? host.safeRegion.left : margin;
    const float safeTop = host.safeRegion.bottom > host.safeRegion.top ? host.safeRegion.top : margin;
    const float safeRight = host.safeRegion.right > host.safeRegion.left ? host.safeRegion.right : width - margin;
    const float safeBottom = host.safeRegion.bottom > host.safeRegion.top ? host.safeRegion.bottom : height - margin;
    const float availableWidth = std::max(1.0f, safeRight - safeLeft);
    const float availableHeight = std::max(1.0f, safeBottom - safeTop);
    const float panelWidth = overlay
        ? std::max(180.0f, std::min(availableWidth, width * 0.86f))
        : std::clamp(availableWidth, 180.0f, 560.0f);
    const float panelHeight = overlay
        ? std::max(300.0f, std::min(availableHeight, height * 0.62f))
        : std::clamp(availableHeight, 300.0f, 900.0f);
    panelRegion_ = TouchRegion{safeLeft, safeTop, safeLeft + panelWidth, safeTop + panelHeight};
    if (!Widgets::BeginPanel("Aurora UI", theme_, ImVec2(safeLeft, safeTop), ImVec2(panelWidth, panelHeight), reveal)) {
        panelRegion_ = TouchRegion{};
        Widgets::EndPanel();
        return;
    }
    const ImVec2 actualPanelPosition = ImGui::GetWindowPos();
    const ImVec2 actualPanelSize = ImGui::GetWindowSize();
    panelRegion_ = TouchRegion{
        actualPanelPosition.x,
        actualPanelPosition.y,
        actualPanelPosition.x + actualPanelSize.x,
        actualPanelPosition.y + actualPanelSize.y};

    ImGui::TextUnformatted("Aurora UI");
    ImGui::TextDisabled("%s vertical slice", backend.name);
    ImGui::SameLine(ImGui::GetWindowWidth() - 116.0f);
    ImGui::TextColored(theme_.accent, "LIVE");
    ImGui::Separator();

    const char* tabs[] = {"Overview", "Glass Lab", "Material", "Input"};
    for (int index = 0; index < 4; ++index) {
        if (index != 0) {
            ImGui::SameLine();
        }
        if (Widgets::Tab(tabs[index], selectedTab_ == index, theme_)) {
            selectedTab_ = index;
        }
    }
    ImGui::Separator();

    textInputRequested_ = false;
    if (selectedTab_ == 0) {
        ImGui::TextUnformatted("Runtime status");
        ImGui::BeginChild("status", ImVec2(0.0f, 210.0f), true);
        ImGui::Text("Renderer  %s", backend.name);
        const char* vulkanState = vulkanProbe.state == BackendState::Ready ? "ready" :
                                   vulkanProbe.state == BackendState::SwapchainReady ? "swapchain" :
                                   vulkanProbe.state == BackendState::SurfaceReady ? "surface" :
                                   vulkanProbe.state == BackendState::Probed ? "probed" : "unavailable";
        ImGui::Text("Vulkan    %s (%u devices)",
                    vulkanState,
                    vulkanProbe.physicalDeviceCount);
        if (!vulkanProbe.diagnostic.empty()) {
            ImGui::TextDisabled("VK status:");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", vulkanProbe.diagnostic.c_str());
            ImGui::PopTextWrapPos();
        }
        if (vulkanProbe.state == BackendState::Probed) {
            ImGui::TextDisabled("Vulkan device probe passed; use backend=vulkan for an independent Surface.");
        }
        if (vulkanProbe.state == BackendState::SurfaceReady) {
            ImGui::Text("VK Surface %u formats / %u present modes",
                        vulkanProbe.surfaceFormatCount, vulkanProbe.presentModeCount);
        }
        if (vulkanProbe.state == BackendState::SwapchainReady || vulkanProbe.state == BackendState::Ready) {
            ImGui::Text("VK Swapchain %u images", vulkanProbe.swapchainImageCount);
        }
        if (vulkanProbe.apiVersion != 0) {
            ImGui::Text("VK API    %u.%u.%u",
                        VK_VERSION_MAJOR(vulkanProbe.apiVersion),
                        VK_VERSION_MINOR(vulkanProbe.apiVersion),
                        VK_VERSION_PATCH(vulkanProbe.apiVersion));
        }
        const char* hostName = host.mode == HostMode::Activity ? "Activity" :
                               host.mode == HostMode::ApplicationOverlay ? "Overlay" : "RootNative";
        ImGui::Text("Host      %s", hostName);
        ImGui::Text("IME       %s", host.supportsIme ? "available" : "unavailable");
        ImGui::Text("Surface   %s", host.hasSurface ? "ready" : "lost");
        ImGui::Text("Safe area %.0f,%.0f - %.0f,%.0f", host.safeRegion.left, host.safeRegion.top,
                    host.safeRegion.right, host.safeRegion.bottom);
        ImGui::Text("Frame     %.2f ms (%.0f FPS)", frameStats.lastDeltaSeconds * 1000.0f,
                    frameStats.smoothedFps);
        ImGui::Text("Worst     %.2f ms", frameStats.worstFrameMilliseconds);
        ImGui::Text("Pointers  %zu active", input.activeCount);
        ImGui::Text("Touch     %.0f, %.0f", input.primary.x, input.primary.y);
        ImGui::EndChild();

        ImGui::Spacing();
        if (Widgets::Button("Probe Vulkan Surface", theme_)) {
            vulkanProbeRequested_ = true;
        }
        ImGui::Spacing();
        if (Widgets::Button(panelOpen_ ? "Hide panel" : "Show panel", theme_)) {
            panelOpen_ = !panelOpen_;
        }
    } else if (selectedTab_ == 1) {
        ImGui::TextUnformatted("Liquid Glass Lab");
        ImGui::TextDisabled("Adapted to Aurora host framebuffer and input regions.");
        ImGui::Separator();

        ImGui::TextUnformatted("Pill controls");
        const ImVec2 buttonSize(150.0f, theme_.controlHeight);
        if (ImGui::Button("Liquid Glass", buttonSize)) {
            ++showcaseClicks_;
        }
        ImGui::SameLine();
        if (ImGui::Button("Tinted", buttonSize)) {
            showcaseTinted_ = !showcaseTinted_;
        }
        ImGui::SameLine();
        ImGui::Text("Clicks %d", showcaseClicks_);

        Widgets::Toggle("Interactive switch", &showcaseToggle_);
        Widgets::Slider("Glass intensity", &showcaseSlider_, 0.0f, 1.0f);

        const ImVec2 cardMin = ImGui::GetCursorScreenPos();
        const ImVec2 cardSize(ImGui::GetContentRegionAvail().x, 190.0f);
        const ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);
        GlassMaterialSpec card;
        card.tint = showcaseTinted_
            ? ImVec4(0.04f, 0.34f, 0.78f, 0.52f * showcaseSlider_)
            : ImVec4(0.10f, 0.14f, 0.22f, 0.62f * showcaseSlider_);
        card.highlight = ImVec4(0.82f, 0.92f, 1.0f, 0.28f);
        card.border = showcaseToggle_
            ? ImVec4(0.42f, 0.72f, 1.0f, 0.46f)
            : ImVec4(0.58f, 0.64f, 0.74f, 0.26f);
        card.rounding = 28.0f;
        card.sheenPhase = materialTime_ * 0.11f;
        card.hasBackgroundTexture = backend.supportsBackgroundInput &&
                                     backend.backgroundTextureHandle != 0;
        if (card.hasBackgroundTexture) {
            card.backgroundTexture = reinterpret_cast<ImTextureID>(backend.backgroundTextureHandle);
        }
        MaterialRenderer::drawGlassFallback(*ImGui::GetWindowDrawList(), cardMin, cardMax, card);
        ImGui::Dummy(cardSize);
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 22.0f, cardMin.y + 24.0f));
        ImGui::TextColored(theme_.accent, "Liquid Glass");
        ImGui::Text("%s", card.hasBackgroundTexture ? "Live host framebuffer" : "Fallback material");
        ImGui::TextDisabled("Blur, tint, edge light and sheen share one material contract.");
        ImGui::SetCursorScreenPos(ImVec2(cardMin.x, cardMax.y + theme_.itemGap));
        ImGui::TextDisabled("The overlay host never samples another application's protected frame.");
    } else if (selectedTab_ == 2) {
        ImGui::TextUnformatted("Material fallback");
        ImGui::TextDisabled("No background source is required for this pass.");
        Widgets::Toggle("Glass material", &glassEnabled_);
        Widgets::Slider("Effect intensity", &effectIntensity_, 0.0f, 1.0f);
        const ImVec2 previewMin = ImGui::GetCursorScreenPos();
        const ImVec2 previewSize(ImGui::GetContentRegionAvail().x, 150.0f);
        const ImVec2 previewMax(previewMin.x + previewSize.x, previewMin.y + previewSize.y);
        GlassMaterialSpec glass;
        glass.tint.w *= glassEnabled_ ? effectIntensity_ : 0.35f;
        glass.sheenPhase = materialTime_ * 0.08f;
        glass.hasBackgroundTexture = backend.supportsBackgroundInput &&
                                     backend.backgroundTextureHandle != 0;
        if (glass.hasBackgroundTexture) {
            glass.backgroundTexture = reinterpret_cast<ImTextureID>(
                backend.backgroundTextureHandle);
        }
        MaterialRenderer::drawGlassFallback(*ImGui::GetWindowDrawList(), previewMin, previewMax, glass);
        ImGui::Dummy(previewSize);
        ImGui::SetCursorScreenPos(ImVec2(previewMin.x + 16.0f, previewMin.y + 16.0f));
        ImGui::TextColored(theme_.accent, "Fallback glass");
        ImGui::TextWrapped(glass.hasBackgroundTexture
            ? "Host framebuffer input is active for this material pass."
            : "No legal host framebuffer input is available; using fallback.");
        ImGui::SetCursorScreenPos(ImVec2(previewMin.x, previewMax.y + theme_.itemGap));
    } else {
        textInputRequested_ = false;
        ImGui::TextUnformatted("Input routing");
        ImGui::TextDisabled("Stable pointer IDs and cancellation are handled before ImGui.");
        ImGui::BulletText("Primary down: %s", input.primaryDown ? "yes" : "no");
        ImGui::BulletText("Pressed edge: %s", input.primaryPressed ? "yes" : "no");
        ImGui::BulletText("Released edge: %s", input.primaryReleased ? "yes" : "no");
        ImGui::BulletText("Cancelled: %s", input.cancelled ? "yes" : "no");
        ImGui::BulletText("Gesture pan: %.1f, %.1f", gesture.panX, gesture.panY);
        ImGui::BulletText("Gesture scale: %.3f", gesture.scale);
        ImGui::BulletText("Gesture rotation: %.3f rad", gesture.rotationRadians);
        ImGui::Separator();
        ImGui::TextUnformatted("IME preview");
        Widgets::TextInput("Text", text_.data(), text_.size());
        textInputRequested_ = ImGui::IsItemActive() || textInputRequested_;
        ImGui::TextDisabled("Native Android IME bridge is ready.");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Aurora Runtime 0.1.0");
    Widgets::EndPanel();
}

}  // namespace aurora
