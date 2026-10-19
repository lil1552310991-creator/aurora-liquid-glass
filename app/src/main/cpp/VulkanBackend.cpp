#include "VulkanBackend.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <cstdio>
#include <algorithm>
#include <string>
#include <chrono>
#include <android/log.h>

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

namespace {
// 诊断预算：只打最前面几次，避免每秒刷屏。Vulkan 后端的失败大多是
// "某一帧里静默 return"，而 state 仍然是 Ready —— 不加这个就只能看到
// present 恒为 0 却不知道为什么。
int g_vkDiagBudget = 8;
void VkDiag(const char* what, VkResult result) {
    if (g_vkDiagBudget <= 0) return;
    --g_vkDiagBudget;
    __android_log_print(ANDROID_LOG_WARN, "AuroraVk", "%s result=%d", what,
                        static_cast<int>(result));
}
void VkDiagText(const char* what) {
    if (g_vkDiagBudget <= 0) return;
    --g_vkDiagBudget;
    __android_log_print(ANDROID_LOG_WARN, "AuroraVk", "%s", what);
}
}  // namespace
#endif

namespace aurora {

VulkanBackend::~VulkanBackend() {
    shutdown();
}

BackendDescriptor VulkanBackend::descriptor() const {
    return descriptor_;
}

bool VulkanBackend::initialize(ANativeWindow* window, int width, int height) {
    (void)window;
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
#if !defined(__ANDROID__)
    descriptor_ = DescribeVulkanBackend();
    return false;
#else
    shutdown();
    if (!loadLoader()) {
        shutdown();
        descriptor_.diagnostic = "loader unavailable";
        return false;
    }
    const auto fail = [this](const char* fallback) {
        const std::string detail = descriptor_.diagnostic;
        shutdown();
        descriptor_.diagnostic = detail.empty() ? fallback : detail;
        return false;
    };
    if (!createInstance()) {
        return fail("instance creation failed");
    }
    if (!createSurface(window)) {
        return fail("Android surface creation failed");
    }
    if (!selectPhysicalDevice()) {
        return fail("no physical device");
    }
    if (!selectGraphicsQueue()) {
        return fail("no graphics queue");
    }
    if (!querySurfaceSupport()) {
        return fail("surface present unsupported");
    }
    if (!createSwapchain()) {
        return fail("swapchain creation failed");
    }
    if (!createRenderResources()) {
        return fail("render resources creation failed");
    }
    if (!initializeImGui()) {
        return fail("ImGui Vulkan initialization failed");
    }
    const std::uint32_t surfaceFormatCount = descriptor_.surfaceFormatCount;
    const std::uint32_t presentModeCount = descriptor_.presentModeCount;
    const bool supportsPresentation = descriptor_.supportsPresentation;
    descriptor_ = ProbeVulkanBackend();
    descriptor_.state = imguiReady_ ? BackendState::Ready : BackendState::SwapchainReady;
    descriptor_.name = imguiReady_ ? "Vulkan" : "Vulkan swapchain";
    descriptor_.surfaceFormatCount = surfaceFormatCount;
    descriptor_.presentModeCount = presentModeCount;
    descriptor_.supportsPresentation = supportsPresentation;
    descriptor_.swapchainImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    descriptor_.diagnostic = imguiReady_ ? "Vulkan renderer ready" :
        "swapchain ready; ImGui renderer deferred because another backend owns the context";
    return true;
#endif
}

void VulkanBackend::resize(int width, int height) {
    const int nextWidth = width > 0 ? width : 1;
    const int nextHeight = height > 0 ? height : 1;
    if (nextWidth != width_ || nextHeight != height_) {
        swapchainRecreatePending_ = true;
    }
    width_ = nextWidth;
    height_ = nextHeight;
}

void VulkanBackend::beginFrame(float deltaSeconds) {
#if defined(__ANDROID__)
    (void)deltaSeconds;
    if (!imguiReady_ || frames_.empty() || frameActive_) {
        if (!imguiReady_) VkDiagText("beginFrame early: imguiReady=false");
        else if (frames_.empty()) VkDiagText("beginFrame early: frames empty");
        else VkDiagText("beginFrame early: previous frame still active");
        return;
    }
    if (swapchainRecreatePending_ && !recreateSwapchain()) {
        descriptor_.state = BackendState::SurfaceLost;
        return;
    }
    auto waitForFences = reinterpret_cast<PFN_vkWaitForFences>(getDeviceProc_(device_, "vkWaitForFences"));
    auto resetFences = reinterpret_cast<PFN_vkResetFences>(getDeviceProc_(device_, "vkResetFences"));
    auto acquireNextImage = reinterpret_cast<PFN_vkAcquireNextImageKHR>(getDeviceProc_(device_, "vkAcquireNextImageKHR"));
    auto resetCommandPool = reinterpret_cast<PFN_vkResetCommandPool>(getDeviceProc_(device_, "vkResetCommandPool"));
    auto beginCommandBuffer = reinterpret_cast<PFN_vkBeginCommandBuffer>(getDeviceProc_(device_, "vkBeginCommandBuffer"));
    auto cmdBeginRenderPass = reinterpret_cast<PFN_vkCmdBeginRenderPass>(getDeviceProc_(device_, "vkCmdBeginRenderPass"));
    if (waitForFences == nullptr || resetFences == nullptr || acquireNextImage == nullptr ||
        resetCommandPool == nullptr || beginCommandBuffer == nullptr || cmdBeginRenderPass == nullptr) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "frame entry points unavailable";
        return;
    }
    ImGui_ImplVulkan_NewFrame();
    FrameResources& frame = frames_[frameSlot_ % frames_.size()];
    if (waitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "frame fence wait failed";
        return;
    }
    const VkResult acquireResult = acquireNextImage(device_, swapchain_, UINT64_MAX, frame.imageAvailable,
                                                    VK_NULL_HANDLE, &imageIndex_);
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        VkDiag("vkAcquireNextImageKHR", acquireResult);
        descriptor_.state = BackendState::SurfaceLost;
        descriptor_.diagnostic = "acquire next image result=" + std::to_string(static_cast<int>(acquireResult));
        return;
    }
    if (imageIndex_ >= imageFences_.size()) {
        descriptor_.state = BackendState::SurfaceLost;
        descriptor_.diagnostic = "acquired image index is out of range";
        return;
    }
    if (imageFences_[imageIndex_] != VK_NULL_HANDLE && imageFences_[imageIndex_] != frame.fence) {
        if (waitForFences(device_, 1, &imageFences_[imageIndex_], VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
            descriptor_.state = BackendState::DeviceLost;
            descriptor_.diagnostic = "image fence wait failed";
            return;
        }
    }
    if (resetFences(device_, 1, &frame.fence) != VK_SUCCESS ||
        resetCommandPool(device_, frame.commandPool, 0) != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "frame synchronization reset failed";
        return;
    }
    imageFences_[imageIndex_] = frame.fence;
    VkCommandBufferBeginInfo commandBeginInfo{};
    commandBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (beginCommandBuffer(frame.commandBuffer, &commandBeginInfo) != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "begin command buffer failed";
        return;
    }
    VkClearValue clearValue{};
    clearValue.color = {{0.035f, 0.045f, 0.07f, 1.0f}};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = frames_[imageIndex_].framebuffer;
    renderPassInfo.renderArea.extent = swapchainExtent_;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;
    cmdBeginRenderPass(frame.commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    frameActive_ = true;
#else
    (void)deltaSeconds;
#endif
}

void VulkanBackend::submit(ImDrawData* drawData) {
#if defined(__ANDROID__)
    if (imguiReady_ && frameActive_ && drawData != nullptr) {
        ImGui_ImplVulkan_RenderDrawData(drawData, frames_[frameSlot_ % frames_.size()].commandBuffer);
    }
#else
    (void)drawData;
#endif
}

void VulkanBackend::endFrame() {
#if defined(__ANDROID__)
    if (!frameActive_ || frames_.empty()) {
        return;
    }
    FrameResources& frame = frames_[frameSlot_ % frames_.size()];
    auto cmdEndRenderPass = reinterpret_cast<PFN_vkCmdEndRenderPass>(getDeviceProc_(device_, "vkCmdEndRenderPass"));
    auto endCommandBuffer = reinterpret_cast<PFN_vkEndCommandBuffer>(getDeviceProc_(device_, "vkEndCommandBuffer"));
    auto queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(getDeviceProc_(device_, "vkQueueSubmit"));
    auto queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(getDeviceProc_(device_, "vkQueuePresentKHR"));
    if (cmdEndRenderPass == nullptr || endCommandBuffer == nullptr || queueSubmit == nullptr || queuePresent == nullptr) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "frame submission entry points unavailable";
        frameActive_ = false;
        return;
    }
    cmdEndRenderPass(frame.commandBuffer);
    if (endCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "end command buffer failed";
        frameActive_ = false;
        return;
    }
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinished;
    if (queueSubmit(graphicsQueue_, 1, &submitInfo, frame.fence) != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "queue submit failed";
        frameActive_ = false;
        return;
    }
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frame.renderFinished;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex_;
    const VkResult presentResult = queuePresent(graphicsQueue_, &presentInfo);
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) {
        // 真正上屏的帧才计数。外面靠这两个字段算 present fps：
        //   分母是 lastPresentNanoseconds 的增量，它一直为 0 的话，
        //   无论 present 多少次，上报的 present fps 都会是 0 —— 之前
        //   Vulkan 看起来"一帧都没上屏"就是这个字段没维护。
        // ★ 语义差异：GLES 那边能通过 EGL_ANDROID_get_frame_timestamps 拿到
        //   **硬件扫描输出时间戳**；Vulkan 没有等价 API，这里用提交完成的单调时钟
        //   近似，足以算帧率，但不能当作真实的显示延迟。
        ++framePerf_.presentedFrames;
        framePerf_.lastPresentNanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
    } else {
        VkDiag("vkQueuePresentKHR", presentResult);
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        descriptor_.state = BackendState::SurfaceLost;
        descriptor_.diagnostic = "present requires swapchain recreate";
    } else if (presentResult != VK_SUCCESS) {
        descriptor_.state = BackendState::DeviceLost;
        descriptor_.diagnostic = "queue present result=" + std::to_string(static_cast<int>(presentResult));
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        swapchainRecreatePending_ = true;
    }
    frameSlot_ = (frameSlot_ + 1) % frames_.size();
    frameActive_ = false;
#endif
}

void VulkanBackend::shutdown() {
#if defined(__ANDROID__)
    if (imguiReady_) {
        ImGui_ImplVulkan_Shutdown();
        imguiReady_ = false;
    }
    if (ownsImGuiContext_ && ImGui::GetCurrentContext() != nullptr) {
        ImGui::DestroyContext();
        ownsImGuiContext_ = false;
        imguiContextReady_ = false;
    }
    destroyRenderResources();
    destroySwapchain();
    destroyDevice();
    if (instance_ != VK_NULL_HANDLE) {
        if (surface_ != VK_NULL_HANDLE && destroySurface_ != nullptr) {
            destroySurface_(instance_, surface_, nullptr);
            surface_ = VK_NULL_HANDLE;
        }
        auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
            getInstanceProc_(instance_, "vkDestroyInstance"));
        if (destroyInstance != nullptr) {
            destroyInstance(instance_, nullptr);
        }
        instance_ = VK_NULL_HANDLE;
    }
    if (loader_ != nullptr) {
        dlclose(loader_);
        loader_ = nullptr;
    }
    getInstanceProc_ = nullptr;
    getDeviceProc_ = nullptr;
    destroySurface_ = nullptr;
#endif
    descriptor_ = DescribeVulkanBackend();
    swapchainRecreatePending_ = false;
}

#if defined(__ANDROID__)
bool VulkanBackend::loadLoader() {
    loader_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (loader_ == nullptr) {
        descriptor_.diagnostic = "dlopen libvulkan.so failed";
        return false;
    }
    getInstanceProc_ = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader_, "vkGetInstanceProcAddr"));
    if (getInstanceProc_ == nullptr) {
        descriptor_.diagnostic = "vkGetInstanceProcAddr unavailable";
    }
    return getInstanceProc_ != nullptr;
}

bool VulkanBackend::createInstance() {
    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getInstanceProc_(nullptr, "vkCreateInstance"));
    if (createInstance == nullptr) {
        return false;
    }
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Aurora UI";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "Aurora Runtime";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
    createInfo.enabledExtensionCount = 2;
    createInfo.ppEnabledExtensionNames = extensions;
    const VkResult result = createInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateInstance result=" + std::to_string(static_cast<int>(result));
    }
    return result == VK_SUCCESS && instance_ != VK_NULL_HANDLE;
}

bool VulkanBackend::createSurface(ANativeWindow* window) {
    if (window == nullptr) {
        descriptor_.diagnostic = "ANativeWindow is null";
        return false;
    }
    auto createSurface = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(
        getInstanceProc_(instance_, "vkCreateAndroidSurfaceKHR"));
    destroySurface_ = reinterpret_cast<PFN_vkDestroySurfaceKHR>(
        getInstanceProc_(instance_, "vkDestroySurfaceKHR"));
    if (createSurface == nullptr || destroySurface_ == nullptr) {
        descriptor_.diagnostic = "Android surface entry points unavailable";
        return false;
    }
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.window = window;
    const VkResult result = createSurface(instance_, &surfaceInfo, nullptr, &surface_);
    if (result != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateAndroidSurface result=" + std::to_string(static_cast<int>(result));
    }
    return result == VK_SUCCESS;
}

bool VulkanBackend::selectPhysicalDevice() {
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        getInstanceProc_(instance_, "vkEnumeratePhysicalDevices"));
    if (enumerate == nullptr) {
        return false;
    }
    std::uint32_t count = 0;
    if (enumerate(instance_, &count, nullptr) != VK_SUCCESS || count == 0) {
        return false;
    }
    VkPhysicalDevice devices[8]{};
    count = count > 8 ? 8 : count;
    if (enumerate(instance_, &count, devices) != VK_SUCCESS || count == 0) {
        return false;
    }
    physicalDevice_ = devices[0];
    return true;
}

bool VulkanBackend::selectGraphicsQueue() {
    auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceQueueFamilyProperties"));
    auto getSurfaceSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    auto createDevice = reinterpret_cast<PFN_vkCreateDevice>(getInstanceProc_(instance_, "vkCreateDevice"));
    if (getProperties == nullptr || getSurfaceSupport == nullptr || createDevice == nullptr ||
        physicalDevice_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE) {
        return false;
    }
    std::uint32_t count = 0;
    getProperties(physicalDevice_, &count, nullptr);
    if (count == 0) {
        return false;
    }
    VkQueueFamilyProperties properties[16]{};
    count = count > 16 ? 16 : count;
    getProperties(physicalDevice_, &count, properties);
    bool foundGraphicsQueue = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        if ((properties[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            VkBool32 presentSupported = VK_FALSE;
            if (getSurfaceSupport(physicalDevice_, index, surface_, &presentSupported) == VK_SUCCESS &&
                presentSupported == VK_TRUE) {
                graphicsQueueFamily_ = index;
                foundGraphicsQueue = true;
                break;
            }
        }
    }
    if (!foundGraphicsQueue) {
        return false;
    }
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    const char* deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    deviceInfo.enabledExtensionCount = 1;
    deviceInfo.ppEnabledExtensionNames = deviceExtensions;
    const VkResult deviceResult = createDevice(physicalDevice_, &deviceInfo, nullptr, &device_);
    if (deviceResult != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateDevice result=" + std::to_string(static_cast<int>(deviceResult));
        return false;
    }
    getDeviceProc_ = reinterpret_cast<PFN_vkGetDeviceProcAddr>(getInstanceProc_(instance_, "vkGetDeviceProcAddr"));
    if (getDeviceProc_ == nullptr) {
        return false;
    }
    auto getQueue = reinterpret_cast<PFN_vkGetDeviceQueue>(getDeviceProc_(device_, "vkGetDeviceQueue"));
    if (getQueue != nullptr) {
        getQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
    }
    return graphicsQueue_ != VK_NULL_HANDLE;
}

bool VulkanBackend::createSwapchain() {
    auto getCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    auto getFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    auto getModes = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    auto createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        getDeviceProc_(device_, "vkCreateSwapchainKHR"));
    auto getSwapchainImages = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        getDeviceProc_(device_, "vkGetSwapchainImagesKHR"));
    auto createImageView = reinterpret_cast<PFN_vkCreateImageView>(getDeviceProc_(device_, "vkCreateImageView"));
    if (getCapabilities == nullptr || getFormats == nullptr || getModes == nullptr ||
        createSwapchain == nullptr || getSwapchainImages == nullptr || createImageView == nullptr) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    const VkResult capabilityResult = getCapabilities(physicalDevice_, surface_, &capabilities);
    if (capabilityResult != VK_SUCCESS) {
        descriptor_.diagnostic = "surface capabilities result=" + std::to_string(static_cast<int>(capabilityResult));
        return false;
    }
    std::uint32_t formatCount = 0;
    std::uint32_t modeCount = 0;
    if (getFormats(physicalDevice_, surface_, &formatCount, nullptr) != VK_SUCCESS ||
        getModes(physicalDevice_, surface_, &modeCount, nullptr) != VK_SUCCESS || formatCount == 0 || modeCount == 0) {
        descriptor_.diagnostic = "surface formats or present modes unavailable";
        return false;
    }
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    std::vector<VkPresentModeKHR> modes(modeCount);
    if (getFormats(physicalDevice_, surface_, &formatCount, formats.data()) != VK_SUCCESS ||
        getModes(physicalDevice_, surface_, &modeCount, modes.data()) != VK_SUCCESS) {
        descriptor_.diagnostic = "surface formats or present modes query failed";
        return false;
    }
    const auto preferredFormat = std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
        return format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    });
    const VkSurfaceFormatKHR selectedFormat = preferredFormat == formats.end() ? formats.front() : *preferredFormat;
    surfaceFormat_ = selectedFormat.format;
    colorSpace_ = selectedFormat.colorSpace;
    presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_FIFO_KHR) == modes.end()) {
        presentMode_ = modes.front();
    }
    if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
        presentMode_ = VK_PRESENT_MODE_MAILBOX_KHR;
    }
    if ((capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        descriptor_.diagnostic = "surface lacks color attachment usage";
        return false;
    }
    swapchainExtent_.width = capabilities.currentExtent.width != UINT32_MAX
                                 ? capabilities.currentExtent.width
                                 : std::clamp(static_cast<std::uint32_t>(width_), capabilities.minImageExtent.width,
                                              capabilities.maxImageExtent.width);
    swapchainExtent_.height = capabilities.currentExtent.height != UINT32_MAX
                                  ? capabilities.currentExtent.height
                                  : std::clamp(static_cast<std::uint32_t>(height_), capabilities.minImageExtent.height,
                                               capabilities.maxImageExtent.height);
    std::uint32_t imageCount = std::max(capabilities.minImageCount, 2u);
    if (capabilities.maxImageCount != 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat_;
    info.imageColorSpace = colorSpace_;
    info.imageExtent = swapchainExtent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    constexpr VkCompositeAlphaFlagBitsKHR compositeAlphaPreferences[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    bool compositeAlphaSelected = false;
    for (const VkCompositeAlphaFlagBitsKHR candidate : compositeAlphaPreferences) {
        if ((capabilities.supportedCompositeAlpha & candidate) != 0) {
            info.compositeAlpha = candidate;
            compositeAlphaSelected = true;
            break;
        }
    }
    if (!compositeAlphaSelected) {
        descriptor_.diagnostic = "surface has no supported composite alpha mode";
        return false;
    }
    info.presentMode = presentMode_;
    info.clipped = VK_TRUE;
    const VkResult swapchainResult = createSwapchain(device_, &info, nullptr, &swapchain_);
    if (swapchainResult != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateSwapchain result=" + std::to_string(static_cast<int>(swapchainResult));
        return false;
    }
    std::uint32_t actualCount = 0;
    const VkResult countResult = getSwapchainImages(device_, swapchain_, &actualCount, nullptr);
    if (countResult != VK_SUCCESS || actualCount == 0) {
        descriptor_.diagnostic = "vkGetSwapchainImages count result=" + std::to_string(static_cast<int>(countResult));
        return false;
    }
    swapchainImages_.resize(actualCount);
    const VkResult imagesResult = getSwapchainImages(device_, swapchain_, &actualCount, swapchainImages_.data());
    if (imagesResult != VK_SUCCESS) {
        descriptor_.diagnostic = "vkGetSwapchainImages result=" + std::to_string(static_cast<int>(imagesResult));
        return false;
    }
    swapchainImageViews_.resize(actualCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < actualCount; ++index) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages_[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat_;
        viewInfo.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                               VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        const VkResult imageViewResult = createImageView(device_, &viewInfo, nullptr, &swapchainImageViews_[index]);
        if (imageViewResult != VK_SUCCESS) {
            descriptor_.diagnostic = "vkCreateImageView result=" + std::to_string(static_cast<int>(imageViewResult));
            return false;
        }
    }
    return true;
}

bool VulkanBackend::createRenderResources() {
    auto createRenderPass = reinterpret_cast<PFN_vkCreateRenderPass>(getDeviceProc_(device_, "vkCreateRenderPass"));
    auto createCommandPool = reinterpret_cast<PFN_vkCreateCommandPool>(getDeviceProc_(device_, "vkCreateCommandPool"));
    auto allocateCommandBuffers = reinterpret_cast<PFN_vkAllocateCommandBuffers>(getDeviceProc_(device_, "vkAllocateCommandBuffers"));
    auto createFence = reinterpret_cast<PFN_vkCreateFence>(getDeviceProc_(device_, "vkCreateFence"));
    auto createSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(getDeviceProc_(device_, "vkCreateSemaphore"));
    auto createFramebuffer = reinterpret_cast<PFN_vkCreateFramebuffer>(getDeviceProc_(device_, "vkCreateFramebuffer"));
    auto createDescriptorPool = reinterpret_cast<PFN_vkCreateDescriptorPool>(getDeviceProc_(device_, "vkCreateDescriptorPool"));
    if (createRenderPass == nullptr || createCommandPool == nullptr || allocateCommandBuffers == nullptr ||
        createFence == nullptr || createSemaphore == nullptr || createFramebuffer == nullptr ||
        createDescriptorPool == nullptr || swapchainImageViews_.empty()) {
        descriptor_.diagnostic = "render resource entry points unavailable";
        return false;
    }

    VkAttachmentDescription attachment{};
    attachment.format = surfaceFormat_;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference colorAttachment{};
    colorAttachment.attachment = 0;
    colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &attachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    VkResult result = createRenderPass(device_, &renderPassInfo, nullptr, &renderPass_);
    if (result != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateRenderPass result=" + std::to_string(static_cast<int>(result));
        return false;
    }

    const std::uint32_t frameCount = static_cast<std::uint32_t>(swapchainImageViews_.size());
    frames_.resize(frameCount);
    imageFences_.assign(frameCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < frameCount; ++index) {
        FrameResources& frame = frames_[index];
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = graphicsQueueFamily_;
        result = createCommandPool(device_, &poolInfo, nullptr, &frame.commandPool);
        if (result != VK_SUCCESS) {
            descriptor_.diagnostic = "vkCreateCommandPool result=" + std::to_string(static_cast<int>(result));
            return false;
        }
        VkCommandBufferAllocateInfo allocationInfo{};
        allocationInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocationInfo.commandPool = frame.commandPool;
        allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocationInfo.commandBufferCount = 1;
        result = allocateCommandBuffers(device_, &allocationInfo, &frame.commandBuffer);
        if (result != VK_SUCCESS) {
            descriptor_.diagnostic = "vkAllocateCommandBuffers result=" + std::to_string(static_cast<int>(result));
            return false;
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        result = createFence(device_, &fenceInfo, nullptr, &frame.fence);
        if (result != VK_SUCCESS) {
            descriptor_.diagnostic = "vkCreateFence result=" + std::to_string(static_cast<int>(result));
            return false;
        }
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        result = createSemaphore(device_, &semaphoreInfo, nullptr, &frame.imageAvailable);
        if (result == VK_SUCCESS) {
            result = createSemaphore(device_, &semaphoreInfo, nullptr, &frame.renderFinished);
        }
        if (result != VK_SUCCESS) {
            descriptor_.diagnostic = "vkCreateSemaphore result=" + std::to_string(static_cast<int>(result));
            return false;
        }
        VkImageView attachments[] = {swapchainImageViews_[index]};
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = swapchainExtent_.width;
        framebufferInfo.height = swapchainExtent_.height;
        framebufferInfo.layers = 1;
        result = createFramebuffer(device_, &framebufferInfo, nullptr, &frame.framebuffer);
        if (result != VK_SUCCESS) {
            descriptor_.diagnostic = "vkCreateFramebuffer result=" + std::to_string(static_cast<int>(result));
            return false;
        }
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1000;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    result = createDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_);
    if (result != VK_SUCCESS) {
        descriptor_.diagnostic = "vkCreateDescriptorPool result=" + std::to_string(static_cast<int>(result));
        return false;
    }
    return true;
}

PFN_vkVoidFunction VulkanBackend::loadImGuiFunction(const char* name, void* userData) {
    auto* backend = static_cast<VulkanBackend*>(userData);
    if (backend == nullptr || name == nullptr) {
        return nullptr;
    }
    PFN_vkVoidFunction function = nullptr;
    if (backend->getDeviceProc_ != nullptr && backend->device_ != VK_NULL_HANDLE) {
        function = backend->getDeviceProc_(backend->device_, name);
    }
    if (function == nullptr && backend->getInstanceProc_ != nullptr) {
        function = backend->getInstanceProc_(backend->instance_, name);
    }
    return function;
}

bool VulkanBackend::initializeImGui() {
    if (ImGui::GetCurrentContext() != nullptr && !ownsImGuiContext_) {
        descriptor_.diagnostic = "swapchain ready; existing ImGui context belongs to another backend";
        return false;
    }
    if (ImGui::GetCurrentContext() == nullptr) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContextReady_ = true;
        ownsImGuiContext_ = true;
    }
    if (!ImGui_ImplVulkan_LoadFunctions(&VulkanBackend::loadImGuiFunction, this)) {
        descriptor_.diagnostic = "ImGui Vulkan function loading failed";
        return false;
    }
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = instance_;
    initInfo.PhysicalDevice = physicalDevice_;
    initInfo.Device = device_;
    initInfo.QueueFamily = graphicsQueueFamily_;
    initInfo.Queue = graphicsQueue_;
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.RenderPass = renderPass_;
    initInfo.MinImageCount = static_cast<std::uint32_t>(frames_.size());
    initInfo.ImageCount = static_cast<std::uint32_t>(frames_.size());
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        descriptor_.diagnostic = "ImGui_ImplVulkan_Init returned false";
        return false;
    }
    imguiReady_ = true;
    return true;
}

bool VulkanBackend::recreateSwapchain() {
    if (device_ == VK_NULL_HANDLE || surface_ == VK_NULL_HANDLE) {
        return false;
    }
    auto deviceWaitIdle = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDeviceWaitIdle>(getDeviceProc_(device_, "vkDeviceWaitIdle"));
    if (deviceWaitIdle == nullptr || deviceWaitIdle(device_) != VK_SUCCESS) {
        descriptor_.diagnostic = "device idle failed during swapchain recreate";
        return false;
    }
    if (imguiReady_) {
        ImGui_ImplVulkan_Shutdown();
        imguiReady_ = false;
    }
    destroyRenderResources();
    destroySwapchain();
    if (!createSwapchain() || !createRenderResources() || !initializeImGui()) {
        descriptor_.diagnostic = descriptor_.diagnostic.empty()
            ? "swapchain recreate failed" : descriptor_.diagnostic;
        return false;
    }
    descriptor_.state = BackendState::Ready;
    descriptor_.swapchainImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    descriptor_.diagnostic = "Vulkan swapchain recreated";
    swapchainRecreatePending_ = false;
    frameSlot_ = 0;
    imageIndex_ = 0;
    return true;
}

void VulkanBackend::destroyRenderResources() {
    if (device_ == VK_NULL_HANDLE) {
        frames_.clear();
        imageFences_.clear();
        renderPass_ = VK_NULL_HANDLE;
        descriptorPool_ = VK_NULL_HANDLE;
        return;
    }
    auto deviceWaitIdle = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDeviceWaitIdle>(getDeviceProc_(device_, "vkDeviceWaitIdle"));
    if (deviceWaitIdle != nullptr) {
        deviceWaitIdle(device_);
    }
    auto destroyFramebuffer = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyFramebuffer>(getDeviceProc_(device_, "vkDestroyFramebuffer"));
    auto destroySemaphore = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroySemaphore>(getDeviceProc_(device_, "vkDestroySemaphore"));
    auto destroyFence = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyFence>(getDeviceProc_(device_, "vkDestroyFence"));
    auto destroyCommandPool = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyCommandPool>(getDeviceProc_(device_, "vkDestroyCommandPool"));
    for (FrameResources& frame : frames_) {
        if (destroyFramebuffer != nullptr && frame.framebuffer != VK_NULL_HANDLE) {
            destroyFramebuffer(device_, frame.framebuffer, nullptr);
        }
        if (destroySemaphore != nullptr && frame.imageAvailable != VK_NULL_HANDLE) {
            destroySemaphore(device_, frame.imageAvailable, nullptr);
        }
        if (destroySemaphore != nullptr && frame.renderFinished != VK_NULL_HANDLE) {
            destroySemaphore(device_, frame.renderFinished, nullptr);
        }
        if (destroyFence != nullptr && frame.fence != VK_NULL_HANDLE) {
            destroyFence(device_, frame.fence, nullptr);
        }
        if (destroyCommandPool != nullptr && frame.commandPool != VK_NULL_HANDLE) {
            destroyCommandPool(device_, frame.commandPool, nullptr);
        }
    }
    frames_.clear();
    imageFences_.clear();
    auto destroyRenderPass = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyRenderPass>(getDeviceProc_(device_, "vkDestroyRenderPass"));
    if (destroyRenderPass != nullptr && renderPass_ != VK_NULL_HANDLE) {
        destroyRenderPass(device_, renderPass_, nullptr);
    }
    auto destroyDescriptorPool = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyDescriptorPool>(getDeviceProc_(device_, "vkDestroyDescriptorPool"));
    if (destroyDescriptorPool != nullptr && descriptorPool_ != VK_NULL_HANDLE) {
        destroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    renderPass_ = VK_NULL_HANDLE;
    descriptorPool_ = VK_NULL_HANDLE;
    frameSlot_ = 0;
    imageIndex_ = 0;
    frameActive_ = false;
}

void VulkanBackend::destroySwapchain() {
    if (device_ == VK_NULL_HANDLE) {
        swapchainImageViews_.clear();
        swapchainImages_.clear();
        swapchain_ = VK_NULL_HANDLE;
        return;
    }
    auto destroyImageView = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroyImageView>(getDeviceProc_(device_, "vkDestroyImageView"));
    auto destroySwapchain = getDeviceProc_ == nullptr ? nullptr :
        reinterpret_cast<PFN_vkDestroySwapchainKHR>(getDeviceProc_(device_, "vkDestroySwapchainKHR"));
    if (destroyImageView != nullptr) {
        for (VkImageView view : swapchainImageViews_) {
            if (view != VK_NULL_HANDLE) {
                destroyImageView(device_, view, nullptr);
            }
        }
    }
    if (destroySwapchain != nullptr && swapchain_ != VK_NULL_HANDLE) {
        destroySwapchain(device_, swapchain_, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    swapchain_ = VK_NULL_HANDLE;
}

bool VulkanBackend::querySurfaceSupport() {
    auto getSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfaceSupportKHR"));
    auto getFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    auto getModes = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        getInstanceProc_(instance_, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    if (getSupport == nullptr || getFormats == nullptr || getModes == nullptr || surface_ == VK_NULL_HANDLE) {
        return false;
    }
    VkBool32 supported = VK_FALSE;
    if (getSupport(physicalDevice_, graphicsQueueFamily_, surface_, &supported) != VK_SUCCESS || !supported) {
        return false;
    }
    std::uint32_t formatCount = 0;
    std::uint32_t modeCount = 0;
    if (getFormats(physicalDevice_, surface_, &formatCount, nullptr) != VK_SUCCESS ||
        getModes(physicalDevice_, surface_, &modeCount, nullptr) != VK_SUCCESS ||
        formatCount == 0 || modeCount == 0) {
        return false;
    }
    descriptor_.surfaceFormatCount = formatCount;
    descriptor_.presentModeCount = modeCount;
    descriptor_.supportsPresentation = true;
    return true;
}

void VulkanBackend::destroyDevice() {
    if (device_ != VK_NULL_HANDLE) {
        auto destroyDevice = getDeviceProc_ != nullptr
            ? reinterpret_cast<PFN_vkDestroyDevice>(getDeviceProc_(device_, "vkDestroyDevice"))
            : reinterpret_cast<PFN_vkDestroyDevice>(getInstanceProc_(instance_, "vkDestroyDevice"));
        if (destroyDevice != nullptr) {
            destroyDevice(device_, nullptr);
        }
        device_ = VK_NULL_HANDLE;
        graphicsQueue_ = VK_NULL_HANDLE;
    }
}
#endif

}  // namespace aurora
