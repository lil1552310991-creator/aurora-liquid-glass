cmake_minimum_required(VERSION 3.22.1)
project(aurora_root_elf LANGUAGES CXX)

# ★ 纯净版开关（全局）：默认 1 = 编译期移除所有日志，连参数都不求值。
#   覆盖范围包括 aurora_glass_ui（ImGui 外壳 / 玻璃 UI）与 aurora_overlay 本身。
#   需要日志时：cmake -DAURORA_SILENT=0 …
if(NOT DEFINED AURORA_SILENT)
    set(AURORA_SILENT 1)
endif()
add_compile_definitions(AURORA_SILENT=${AURORA_SILENT})
message(STATUS "AURORA_SILENT=${AURORA_SILENT} (1 = 纯净版，无任何日志)")

# =============================================================================
# Aurora 纯 ELF 液态玻璃覆盖层
#
# 这一组目标与上级目录的 aurora_root_bootstrap / aurora_root_capture 是
# **两条不同的技术路线**，不要混用：
#
#   ../AuroraRootBootstrap     服务 APK 宿主（ActivityManager / AppOps 启停 APK）
#   ../RootDesktopCapture      APK 路线的桌面帧来源
#   ./ (本文件)                 单 ELF 交付：采集 Dex/JNI 内嵌，运行时释放后清理
#
# 纯 ELF 路线自建 SurfaceFlinger 图层并在其上跑 EGL/GLES，因此需要
# libgui 的私有符号；那些符号在运行时用 dlopen 打开，所以本组目标**没有**
# 链接期对私有平台库的依赖（缺失时程序会自己报告而不是加载失败）。
# =============================================================================

# 浮点收缩必须关闭（Clang 默认 -ffp-contract=on，aarch64 有 FMA 会把
# a*b+c 融合成只舍入一次）。连续曲率圆角的解析解对此很敏感：实测开启后
# 840 个控制点里 89 个偏离、最大 12 ulp，端点类自检仍会通过但形状会变。
# 见 docs/CORNER_PORT_AUDIT.md 5.6 节。禁止 -Ofast / -ffast-math。
add_compile_options(-ffp-contract=off)

set(AURORA_CPP_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../app/src/main/cpp)
set(RECON_DIR       ${CMAKE_CURRENT_SOURCE_DIR}/../../build/recon)
set(IME_BRIDGE_DIR  ${CMAKE_CURRENT_SOURCE_DIR}/../../ime-bridge/build)

# -----------------------------------------------------------------------------
# 主程序：液态玻璃演示
#
# 自建图层 + EGL 上屏 + 真实桌面反射 + ytbl 玻璃 shader + 12-tap 模糊
# + Kyant0 弹簧动效 + 可选的内核层触摸防穿透。
# -----------------------------------------------------------------------------
add_executable(aurora_demo ${RECON_DIR}/demo_entry.cpp)
target_include_directories(aurora_demo PRIVATE
    ${RECON_DIR}          # ytbl_shaders_embedded.h
    ${AURORA_CPP_DIR})    # Motion.h / InputShield.h
target_compile_features(aurora_demo PRIVATE cxx_std_20)
target_compile_options(aurora_demo PRIVATE -Wall -Wextra -Werror=return-type)
target_link_libraries(aurora_demo PRIVATE EGL GLESv3 log dl)

# -----------------------------------------------------------------------------
# 输入法桥接自检（含内嵌 APK）
#
# 只做「释放 APK + 引导 app_process + 等 READY」，用来分步定位输入法问题。
# 它与 aurora_demo 分开，是因为输入法失败原因很多（装包、悬浮窗权限、
# 焦点），单独一个可跑的探针比在大程序里猜要快。
# -----------------------------------------------------------------------------
add_executable(ime_probe ${RECON_DIR}/ime_probe.cpp)
target_include_directories(ime_probe PRIVATE
    ${AURORA_CPP_DIR}
    ${IME_BRIDGE_DIR})    # AuroraImeApk.h
target_compile_features(ime_probe PRIVATE cxx_std_20)
target_compile_options(ime_probe PRIVATE -Wall -Wextra -Werror=return-type)
target_link_libraries(ime_probe PRIVATE log dl)

# -----------------------------------------------------------------------------
# 触摸盾验证工具
#
#   ime_probe  的兄弟工具，三种模式：
#     selftest  纯逻辑自检（无设备也能跑）
#     probe     只读探测，不修改任何状态   <-- 真机上先跑这个
#     live      真实联调，会短暂抑制触摸
# -----------------------------------------------------------------------------
add_executable(inputshield_test ${RECON_DIR}/inputshield_test.cpp)
target_include_directories(inputshield_test PRIVATE ${AURORA_CPP_DIR})
target_compile_features(inputshield_test PRIVATE cxx_std_20)
target_compile_options(inputshield_test PRIVATE
    -Wall -Wextra -Werror=return-type -Wno-unused-parameter)
target_link_libraries(inputshield_test PRIVATE log dl)

# -----------------------------------------------------------------------------
# 圆角算法本机自检（在 PC 上跑，不在设备上跑）
#
# 解析解写错一个系数只会让形状「看着有点怪」而不会报错，所以单独留一个
# 可运行的数值验证程序。审计报告见 docs/CORNER_PORT_AUDIT.md。
# -----------------------------------------------------------------------------
if(NOT ANDROID)
    add_executable(corner_check ${RECON_DIR}/kyant_corner_check.cpp)
    target_compile_features(corner_check PRIVATE cxx_std_20)

    add_executable(ccrr_fill ${RECON_DIR}/ccrr_fill.cpp)
    target_compile_features(ccrr_fill PRIVATE cxx_std_20)

    add_executable(motion_selftest ${RECON_DIR}/motion_selftest.cpp)
    target_include_directories(motion_selftest PRIVATE ${AURORA_CPP_DIR})
    target_compile_features(motion_selftest PRIVATE cxx_std_20)
endif()

# -----------------------------------------------------------------------------
# 液态玻璃 UI 栈静态库（libaurora_glass_ui.a）—— 供纯 ELF 宿主复用
#
# 编的是和 app/src/main/cpp/CMakeLists.txt 同一批源码，区别只在产物形态：
# APK 路线出 SHARED libaurora.so 并走 JNI，纯 ELF 路线出 STATIC 归档，
# 由宿主可执行文件直接链进去，没有 JNIEnv、没有 JavaVM、没有 dex。
#
# 唯一被排除的源文件是 AuroraJni.cpp：它是 Java 侧入口
# （app/src/main/cpp/AuroraJni.cpp:3-4 引 <android/native_window_jni.h> 和
# <jni.h>），纯 ELF 既拿不到也没必要拿这两个头。
#
# 浮点收缩：本文件第 23 行已有全目录的 -ffp-contract=off；下面每个新目标再
# 显式写一遍，这样即使目标被搬去别的 CMakeLists 也仍然守着「禁止 FMA 收缩 /
# 禁止 -Ofast / -ffast-math」这条硬约定（docs/CORNER_PORT_AUDIT.md 5.6 节）。
# -----------------------------------------------------------------------------
set(ELF_IMGUI_DIR       ${AURORA_CPP_DIR}/third_party/imgui)
set(ELF_LIQUIDGLASS_DIR ${AURORA_CPP_DIR}/third_party/liquidglass)

# ytbl 的 .cpp 用 GLOB 收集（和 app/src/main/cpp/CMakeLists.txt:7-8 完全一致）：
# 组件增删不必同步维护清单。CONFIGURE_DEPENDS 保证新增文件能触发重新配置。
file(GLOB ELF_LIQUIDGLASS_SOURCES CONFIGURE_DEPENDS
    ${ELF_LIQUIDGLASS_DIR}/ytbl/*.cpp)

# ★ Liquid Glass Inspector 的纯逻辑层（注册表 / 参数模型 / JSON / 预设 / 撤销）。
#   与 ytbl 一样用 GLOB 收集；它不依赖 Android/ImGui/GL，因此同一批源码
#   也直接进 tests/run_host_tests.ps1 的离线回归。
file(GLOB ELF_GLASS_INSPECTOR_SOURCES CONFIGURE_DEPENDS
    ${AURORA_CPP_DIR}/glass/*.cpp)

# ImGui 的 Vulkan 后端单独成一个静态库，理由是编译定义的作用域：
# 它必须带 IMGUI_IMPL_VULKAN_NO_PROTOTYPES 编译（后端入口由 VulkanBackend
# 自己用 vkGetInstanceProcAddr/vkGetDeviceProcAddr 动态装载），而 ImGui 核心
# 和 OpenGL3 后端都不该背这个定义。
#
# 定义是 PUBLIC：aurora_glass_ui 必须继承它，因为 VulkanBackend.cpp 要调用
# ImGui_ImplVulkan_LoadFunctions，而 imgui_impl_vulkan.h 里那条声明被
# VK_NO_PROTOTYPES（由 IMGUI_IMPL_VULKAN_NO_PROTOTYPES 带出）保护着。
add_library(imgui_vulkan STATIC
    ${ELF_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp)
target_include_directories(imgui_vulkan PUBLIC ${ELF_IMGUI_DIR})
target_compile_features(imgui_vulkan PUBLIC cxx_std_20)
target_compile_options(imgui_vulkan PRIVATE
    -ffp-contract=off -Wall -Wextra -Werror=return-type)
target_compile_definitions(imgui_vulkan PUBLIC IMGUI_IMPL_VULKAN_NO_PROTOTYPES)

add_library(aurora_glass_ui STATIC
    # --- ImGui 核心 ---
    ${ELF_IMGUI_DIR}/imgui.cpp
    ${ELF_IMGUI_DIR}/imgui_draw.cpp
    ${ELF_IMGUI_DIR}/imgui_tables.cpp
    ${ELF_IMGUI_DIR}/imgui_widgets.cpp
    ${ELF_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    # --- ytbl 液态玻璃组件库（15 个 .cpp，GLOB 收集） ---
    ${ELF_LIQUIDGLASS_SOURCES}
    # --- 用户界面 + aurora UI 层 ---
    ${ELF_LIQUIDGLASS_DIR}/secondary_imgui.cpp
    ${AURORA_CPP_DIR}/AuroraUi.cpp
    ${AURORA_CPP_DIR}/Widgets.cpp
    ${AURORA_CPP_DIR}/Material.cpp
    ${AURORA_CPP_DIR}/InputRouter.cpp
    ${AURORA_CPP_DIR}/GestureRouter.cpp
    ${AURORA_CPP_DIR}/RenderBackend.cpp
    ${AURORA_CPP_DIR}/GlesBackend.cpp
    ${AURORA_CPP_DIR}/GlesSceneCompositor.cpp
    ${AURORA_CPP_DIR}/VulkanBackend.cpp
    ${AURORA_CPP_DIR}/Renderer.cpp
    ${AURORA_CPP_DIR}/LiquidGlassRuntime.cpp
    ${AURORA_CPP_DIR}/ScreenRecorder.cpp
    # --- Liquid Glass Inspector（组件注册表 + 调试工作台 + 各组件参数绑定）---
    ${ELF_GLASS_INSPECTOR_SOURCES}
    ${AURORA_CPP_DIR}/GlassWorkbench.cpp
    ${AURORA_CPP_DIR}/GlassTargets.cpp
    # 右下角长按缩放的可复用接线（主窗口与二级窗口共用同一套内核/手柄/震动）
    ${AURORA_CPP_DIR}/CornerResizeOverlay.cpp
    # 触觉反馈的纯逻辑层（参数 + "只放行一次"的门 + 平台注册）。
    # 平台实现在宿主侧（root-host/elf/HapticBinder.h），界面层不依赖 binder。
    ${AURORA_CPP_DIR}/Haptics.cpp)

# include 目录一律 PUBLIC：宿主可执行文件要拿同一批头文件，静态库的
# usage requirements 是唯一能把这些路径带给它的地方。
#   ${AURORA_CPP_DIR}              <- GlesBackend.cpp:14-15 用的是
#                                     "third_party/liquidglass/font.h"
#   third_party/imgui              <- imgui.h / imgui_internal.h /
#                                     backends/imgui_impl_opengl3.h
#   third_party/liquidglass        <- secondary_imgui.cpp:3 的 "ytbl/GlassUI.h"
target_include_directories(aurora_glass_ui PUBLIC
    ${AURORA_CPP_DIR}
    ${ELF_IMGUI_DIR}
    ${ELF_LIQUIDGLASS_DIR})
target_compile_features(aurora_glass_ui PUBLIC cxx_std_20)
target_compile_options(aurora_glass_ui PRIVATE
    -ffp-contract=off -Wall -Wextra -Werror=return-type)

# VulkanBackend.cpp 确实依赖 ImGui 的 Vulkan 后端 —— 已读源码确认，不是猜的：
# 它 include "backends/imgui_impl_vulkan.h"（第 10 行），并调用
# ImGui_ImplVulkan_LoadFunctions（714）、_Init（729）、_NewFrame（115）、
# _RenderDrawData（174）、_Shutdown（246、748），所以 imgui_vulkan 必须链上。
#
# 额外说明：除任务指定的 EGL/GLESv3/log/dl 之外，这里还链了 android。
# Renderer.cpp:78,82,108,164,201 调 ANativeWindow_acquire / ANativeWindow_release，
# 这两个符号由 libandroid.so 导出（app 的 aurora 目标同样链了它，见
# app/src/main/cpp/CMakeLists.txt:57,74）。静态库自身编译不需要它，但宿主 ELF
# 链接时会 undefined reference，所以作为 PUBLIC 依赖一并带出去。
target_link_libraries(aurora_glass_ui PUBLIC
    imgui_vulkan
    EGL
    GLESv3
    log
    dl
    android)
add_dependencies(aurora_glass_ui imgui_vulkan)

# -----------------------------------------------------------------------------
# aurora_overlay —— 交付形态：单个纯 ELF 液态玻璃覆盖层
#
# 它把五件事整合在一个进程里：
#   ① 多点触控（InputChannel 多指 MOTION → Renderer::touch，逐触点）
#   ② 全局实时背景（ScreenCapture → AHardwareBuffer → EGLImage/GPU）
#   ③ 可见形状内拦截触摸（SF输入窗口 + 实际圆角/裁剪/多窗口并集）
#   ④ 窗口移动时拦截跟随（region 与几何同一笔事务重设）
#   ⑤ 拉起系统输入法（InputBridge：内嵌 APK + app_process + 1×1 宿主窗）
#
# 界面走 aurora_glass_ui 里的 ytbl + secondary_imgui（与用户开源项目逐字节相同）。
#
# aosp_glue.h 是自建 SF 图层 / InputChannel / WindowInfo 的胶水层
# （整块复制自已真机验证的 probe_touch_intercept.cpp 与 probe_m3_ytbl.cpp）。
# -----------------------------------------------------------------------------
add_executable(aurora_overlay aurora_overlay.cpp RealtimeCapture.cpp)
target_include_directories(aurora_overlay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}   # aosp_glue.h
    ${IME_BRIDGE_DIR})            # AuroraImeApk.h（被 InputBridge.h 在 namespace 内 include）
target_compile_features(aurora_overlay PRIVATE cxx_std_20)
target_compile_options(aurora_overlay PRIVATE
    -ffp-contract=off -Wall -Wextra -Werror=return-type)
# aurora_glass_ui 是 PUBLIC 依赖，EGL/GLESv3/log/dl/android 与全部 include 路径
# 都会自动带过来，无需在这里重复声明。
target_link_libraries(aurora_overlay PRIVATE aurora_glass_ui)

# -----------------------------------------------------------------------------
# 安装规则：产物集中到一个目录，便于 adb push
# -----------------------------------------------------------------------------
set_target_properties(aurora_demo inputshield_test aurora_overlay PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
if(TARGET ime_probe)
    set_target_properties(ime_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()
