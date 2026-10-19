cmake_minimum_required(VERSION 3.22.1)
project(aurora LANGUAGES C CXX)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(LIQUIDGLASS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/liquidglass)

file(GLOB LIQUIDGLASS_SOURCES CONFIGURE_DEPENDS
    ${LIQUIDGLASS_DIR}/ytbl/*.cpp)

# ★ Liquid Glass Inspector 的纯逻辑层（注册表 / 参数模型 / 持久化 / 历史）。
#   它刻意不依赖 Android、ImGui 或 GL —— 因此可以直接进宿主机离线回归
#   （tests/GlassRegistryTest、GlassPresetTest、GlassHistoryTest）。
file(GLOB GLASS_INSPECTOR_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/glass/*.cpp)

add_library(imgui STATIC
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${IMGUI_DIR})
target_compile_features(imgui PUBLIC cxx_std_20)

add_library(imgui_vulkan STATIC
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui_vulkan PUBLIC ${IMGUI_DIR})
target_compile_features(imgui_vulkan PUBLIC cxx_std_20)
target_compile_definitions(imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

add_library(aurora SHARED
    AuroraJni.cpp
    AuroraUi.cpp
    Widgets.cpp
    Material.cpp
    InputRouter.cpp
    GestureRouter.cpp
    RenderBackend.cpp
    GlesBackend.cpp
    GlesSceneCompositor.cpp
    VulkanBackend.cpp
    Renderer.cpp
    LiquidGlassRuntime.cpp
    ScreenRecorder.cpp
    Haptics.cpp
    ${LIQUIDGLASS_DIR}/secondary_imgui.cpp
    ${LIQUIDGLASS_SOURCES}
    ${GLASS_INSPECTOR_SOURCES}
    GlassWorkbench.cpp
    GlassTargets.cpp
    CornerResizeOverlay.cpp
    Theme.h
    Motion.h
    AuroraUi.h
    Widgets.h
    HostProtocol.h
    FrameStats.h
    Material.h
    InputRouter.h
    GestureRouter.h
    RenderBackend.h
    GlesBackend.h
    GlesSceneCompositor.h
    SceneComposite.h
    VulkanBackend.h
    Renderer.h)
# ${CMAKE_CURRENT_SOURCE_DIR}：secondary_imgui.cpp 现在会 include "GlassTargets.h" /
# "glass/GlassScene.h"（Liquid Glass Inspector 的注册表与场景参数），
# 它们住在 app/src/main/cpp 下。纯 ELF 那条路线（root-host/elf/CMakeLists.txt）
# 早就把 AURORA_CPP_DIR 加进 include 了，这里保持两条路线一致。
target_include_directories(aurora PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${IMGUI_DIR})
target_include_directories(aurora PRIVATE ${LIQUIDGLASS_DIR})
target_compile_features(aurora PRIVATE cxx_std_20)
target_compile_options(aurora PRIVATE -Wall -Wextra -Werror=return-type)

find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles-lib GLESv3)
find_library(log-lib log)
find_library(dl-lib dl)

# 浮点收缩必须关闭。Clang 默认是 -ffp-contract=on，而 aarch64 有 FMA，
# 会把 a*b+c 融合成只舍入一次的指令；C++/Kotlin 的语义要求舍入两次。
# 实测（docs/CORNER_PORT_AUDIT.md 5.6 节）：开启收缩后 840 个控制点里
# 有 89 个偏离，最大 12 ulp；-Ofast 更会让 543 个偏离。
# 玻璃 shader 的参数与本项目的几何算法都要求跨平台可复现，故显式关闭。
# 同时禁用会引入 -ffast-math 的优化等级。
target_compile_options(aurora PRIVATE -ffp-contract=off)

target_link_libraries(aurora PRIVATE
    imgui
    imgui_vulkan
    ${android-lib}
    ${egl-lib}
    ${gles-lib}
    ${log-lib}
    ${dl-lib})

add_dependencies(aurora imgui_vulkan)
