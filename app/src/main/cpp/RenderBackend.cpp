#include "RenderBackend.h"

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <vulkan/vulkan.h>
#endif

namespace aurora {

BackendDescriptor DescribeGles3Backend() {
    BackendDescriptor result;
    result.kind = BackendKind::Gles3;
    result.state = BackendState::Ready;
    result.name = "GLES3";
    return result;
}

BackendDescriptor DescribeVulkanBackend() {
    BackendDescriptor result;
    result.kind = BackendKind::Vulkan;
    result.state = BackendState::Unavailable;
    result.name = "Vulkan";
    return result;
}

BackendDescriptor ProbeVulkanBackend() {
    BackendDescriptor result = DescribeVulkanBackend();
#if !defined(__ANDROID__)
    return result;
#else
    void* library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        return result;
    }

    auto getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(library, "vkGetInstanceProcAddr"));
    if (getProc == nullptr) {
        dlclose(library);
        return result;
    }

    auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(getProc(nullptr, "vkEnumerateInstanceVersion"));
    std::uint32_t apiVersion = VK_API_VERSION_1_0;
    if (enumerateVersion != nullptr) {
        if (enumerateVersion(&apiVersion) != VK_SUCCESS) {
            apiVersion = VK_API_VERSION_1_0;
        }
    }

    auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getProc(nullptr, "vkCreateInstance"));
    if (createInstance == nullptr) {
        dlclose(library);
        return result;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Aurora capability probe";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "Aurora";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = apiVersion;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    VkInstance instance = VK_NULL_HANDLE;
    const VkResult createResult = createInstance(&createInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        dlclose(library);
        return result;
    }

    auto enumerateDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getProc(instance, "vkEnumeratePhysicalDevices"));
    auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getProc(instance, "vkDestroyInstance"));
    std::uint32_t deviceCount = 0;
    if (enumerateDevices != nullptr && enumerateDevices(instance, &deviceCount, nullptr) == VK_SUCCESS && deviceCount > 0) {
        result.state = BackendState::Probed;
        result.supportsExternalTexture = false;
        result.apiVersion = apiVersion;
        result.physicalDeviceCount = deviceCount;
    }
    if (destroyInstance != nullptr) {
        destroyInstance(instance, nullptr);
    }
    dlclose(library);
    return result;
#endif
}

}  // namespace aurora
