#pragma once

#include "InputRouter.h"
#include "GestureRouter.h"
#include "HostProtocol.h"
#include "Motion.h"
#include "Theme.h"
#include "RenderBackend.h"
#include "FrameStats.h"

#include <array>

namespace aurora {

class AuroraUi final {
public:
    AuroraUi();
    void resetForGraphicsContext();
    void draw(const InputSnapshot& input, const GestureFrame& gesture, const BackendDescriptor& backend,
              const BackendDescriptor& vulkanProbe,
              const HostCapabilities& host,
              const FrameStats& frameStats,
              float deltaSeconds, float density, float width, float height);
    void appendText(const char* text);
    void deleteBackward();
    bool textInputRequested() const { return textInputRequested_; }
    bool consumeVulkanProbeRequest();
    void setInitialTab(int tab) { initialTab_ = tab; selectedTab_ = tab; }
    TouchRegion interactiveRegion() const { return panelRegion_; }

private:
    Theme theme_;
    Spring panelReveal_;
    bool panelOpen_ = true;
    bool glassEnabled_ = true;
    float effectIntensity_ = 0.72f;
    bool showcaseTinted_ = false;
    bool showcaseToggle_ = true;
    float showcaseSlider_ = 0.58f;
    int showcaseClicks_ = 0;
    int selectedTab_ = 0;
    int initialTab_ = 0;
    std::array<char, 256> text_{};
    bool textInputRequested_ = false;
    float materialTime_ = 0.0f;
    bool vulkanProbeRequested_ = false;
    TouchRegion panelRegion_{};
};

}  // namespace aurora
