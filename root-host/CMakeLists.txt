cmake_minimum_required(VERSION 3.22.1)
project(aurora_root_bootstrap LANGUAGES CXX)

# 浮点收缩必须关闭（Clang 默认 -ffp-contract=on，aarch64 有 FMA）。
# 连续曲率圆角的解析解对这一点很敏感：实测开启收缩后 840 个控制点里
# 89 个偏离、最大 12 ulp，端点类自检仍然通过但形状会变。
# 见 docs/CORNER_PORT_AUDIT.md 5.6 节。禁止 -Ofast / -ffast-math。
add_compile_options(-ffp-contract=off)

# RootBootstrapCli.h holds the pure CLI contract (parsing, validation, argv
# construction) and is covered by tests/RootBootstrapCliTest.cpp on the host.
add_executable(aurora_root_bootstrap AuroraRootBootstrap.cpp RootBootstrapCli.h)
target_compile_features(aurora_root_bootstrap PRIVATE cxx_std_20)
target_compile_options(aurora_root_bootstrap PRIVATE -Wall -Wextra -Werror)

# Root desktop frame source. It carries the SurfaceFlinger AIDL capability probe,
# the frame-source provider contract and the zero-copy AHardwareBuffer -> EGLImage
# consumer. It is a separate binary on purpose: the capture path can be probed,
# self-tested and verified on a device without touching the APK bootstrap, and a
# blocked capture path is reported as blocked instead of degrading the bootstrap.
add_executable(aurora_root_capture RootDesktopCapture.cpp RootDesktopCapture.h)
target_compile_features(aurora_root_capture PRIVATE cxx_std_20)
target_compile_definitions(aurora_root_capture PRIVATE AURORA_ROOT_CAPTURE_CLI=1)
target_compile_options(aurora_root_capture PRIVATE -Wall -Wextra -Werror)

if(ANDROID)
  # Only public NDK libraries are linked. libgui.so, libbinder_ndk.so and
  # libandroid.so are opened with dlopen at runtime, so this binary keeps no
  # link-time dependency on private platform libraries and still runs on ROMs
  # where a particular private symbol is missing (it reports that instead).
  target_link_libraries(aurora_root_capture PRIVATE EGL GLESv3 log)
endif()
