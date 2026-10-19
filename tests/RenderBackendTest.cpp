#include "RenderBackend.h"

#include <cassert>

int main() {
    const aurora::BackendDescriptor gles = aurora::DescribeGles3Backend();
    assert(gles.kind == aurora::BackendKind::Gles3);
    assert(gles.state == aurora::BackendState::Ready);
    assert(gles.name != nullptr);

    const aurora::BackendDescriptor vulkan = aurora::DescribeVulkanBackend();
    assert(vulkan.kind == aurora::BackendKind::Vulkan);
    assert(vulkan.name != nullptr);

    const aurora::BackendDescriptor probe = aurora::ProbeVulkanBackend();
    assert(probe.kind == aurora::BackendKind::Vulkan);
    assert(probe.state == aurora::BackendState::Unavailable || probe.state == aurora::BackendState::Probed);
    if (probe.state == aurora::BackendState::Probed) {
        assert(probe.physicalDeviceCount > 0);
    }
    return 0;
}
