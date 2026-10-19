#pragma once

#include "RenderBackend.h"

#include <cstddef>
#include <vector>

#if defined(__ANDROID__)
#ifndef VK_USE_PLATFORM_ANDROID_KHR
#define VK_USE_PLATFORM_ANDROID_KHR
#endif
#include <vulkan/vulkan.h>
#endif

namespace aurora {

class VulkanBackend final : public IRenderBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override;

    VulkanBackend(const VulkanBackend&) = delete;
    VulkanBackend& operator=(const VulkanBackend&) = delete;

    BackendDescriptor descriptor() const override;
    // ★ 必须覆写：基类默认返回空结构 ⇒ presentedFrames 恒为 0。
    //   外面（包括"Vulkan 黑屏看门狗"）靠这个值判断到底有没有帧真的上了屏，
    //   不覆写的话正常渲染也会被当成"一帧都没提交"。
    BackendFramePerf framePerfSnapshot() const override { return framePerf_; }
    bool initialize(ANativeWindow* window, int width, int height) override;
    void resize(int width, int height) override;
    void beginFrame(float deltaSeconds) override;
    void submit(ImDrawData* drawData) override;
    void endFrame() override;
    void shutdown() override;

private:
#if defined(__ANDROID__)
    bool loadLoader();
    bool createInstance();
    bool selectPhysicalDevice();
    bool createSurface(ANativeWindow* window);
    bool selectGraphicsQueue();
    bool querySurfaceSupport();
    bool createSwapchain();
    bool createRenderResources();
    bool initializeImGui();
    bool recreateSwapchain();
    void destroyRenderResources();
    static PFN_vkVoidFunction loadImGuiFunction(const char* name, void* userData);
    void destroySwapchain();
    void destroyDevice();

    void* loader_ = nullptr;
    PFN_vkGetInstanceProcAddr getInstanceProc_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProc_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat surfaceFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueFamily_ = 0;
    PFN_vkDestroySurfaceKHR destroySurface_ = nullptr;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };
    std::vector<FrameResources> frames_;
    std::vector<VkFence> imageFences_;
    std::size_t frameSlot_ = 0;
    std::uint32_t imageIndex_ = 0;
    bool frameActive_ = false;
    bool swapchainRecreatePending_ = false;
    bool imguiContextReady_ = false;
    bool imguiReady_ = false;
    bool ownsImGuiContext_ = false;
#endif
    BackendDescriptor descriptor_ = DescribeVulkanBackend();
    BackendFramePerf framePerf_{};
    int width_ = 1;
    int height_ = 1;
};

}  // namespace aurora
