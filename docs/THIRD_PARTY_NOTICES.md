# 第三方组件与署名

本项目在实现「Root ELF 自建 Android 合成器图层」时，参考并移植了以下开源项目的
做法。相关代码在 `root-host/` 内均保留来源标注。

## AndroidSurfaceImgui-Enhanced-Input-method

- 仓库：https://github.com/ssyclr/AndroidSurfaceImgui-Enhanced-Input-method
- 作者：ssyclr
- 许可证：MIT License, Copyright (c) 2025 AndroidSurfaceImgui C++17 Enhanced
- 上游：基于 [Bzi-Han/AndroidSurfaceImgui](https://github.com/Bzi-Han/AndroidSurfaceImgui)
  与 [AFan4724/AndroidSurfaceImgui-Enhanced](https://github.com/AFan4724/AndroidSurfaceImgui-Enhanced)
  的增强版。按其附加条款，任何分发或衍生作品都必须保留对 Bzi-Han/AndroidSurfaceImgui 的署名。

### 本项目移植了什么

只移植「如何在没有 NDK 私有头文件的前提下调用 Android 合成器」这一层技术，
不移植其 UI、ImGui 扩展或输入模块：

1. **把系统类当作不透明缓冲区包装**，只通过 `dlsym` 解析到的构造函数与方法操作它们，
   绝不手写这些类的字段布局。这避开了编译器插入填充导致的 ABI 错位。
2. **按 Android 版本分派符号**。`createSurface` 的签名从 Android 5 到 16 变化了 6 次，
   必须按 `ro.build.version.release` 选择正确的 mangled name。
3. **三处决定性细节**（本项目独立复现并验证）：
   - 不使用 `SurfaceComposerClient::getDefault()`，而是调用默认构造函数
     `_ZN7android21SurfaceComposerClientC2Ev`；
   - Android 14+ 的 `createSurface`，父句柄参数传 `&parentHandle`
     （一个值为 null 的 `sp<IBinder>` 的地址），而不是 `nullptr`；
   - `SurfaceControl::getSurface()` 返回的 `sp<Surface>` 需要
     `+ sizeof(std::max_align_t) / 2` 才是真正的 `ANativeWindow*`。
4. `SurfaceControl::eSkipScreenshot`（flag `0x40`）与
   `LayerMetadata::setInt32(2, 441731)` 这两种「让图层对截图/录屏隐身」的历史写法。

### 本项目没有移植什么

- 其 `ANativeWindowCreator.h` 是一个 1505 行的单头文件合集，本项目没有整体引入，
  而是按上面 4 点自行实现最小子集（`root-host/AuroraSurfaceBroker.*`）。
- 其输入法方案（运行时自安装辅助 APK + 透明 1×1 悬浮窗 + `app_process` 桥接）
  目前作为设计参考记录在 `docs/ROOT_ELF_DEVICE_PROBE.md`，尚未移植。

MIT 许可证全文见 `build/recon/ref_ime/LICENSE`（参考代码的副本）。
正式引入移植代码时，上述署名必须随源码一起保留。

## Kyant0 系列（连续曲率圆角 / 液态玻璃 Kotlin 原实现）

- 仓库：
  - https://github.com/Kyant0/Shapes —— 连续曲率圆角（squircle）解析解
  - https://github.com/Kyant0/AndroidLiquidGlass —— 液态玻璃的 Kotlin/Compose 原实现
  - https://github.com/Kyant0/AMDL —— 弹窗与动效参考
- 作者：Kyant0
- 参考者：用户提供的 `c++液态玻璃展示源码` 即基于 AndroidLiquidGlass 转写为 C++；
  本仓库内的 `app/src/main/cpp/third_party/liquidglass/ytbl` 属于这条脉络。

### 本项目移植了什么

1. **连续曲率圆角算法**（`Shapes`）：
   `ContinuousCurvatureRoundedRectangleCornerBuilder.kt` 的解析解 ——
   解三次方程与降次四次方程得到 κ，反解 10 个 Bézier 控制点。
   C++ 移植与验证程序见 `build/recon/kyant_corner_check.cpp`，
   验证方法与实测数据见 `docs/M3_YTBL_GLASS_INTEGRATION.md`。

### 本项目没有移植什么

- 未引入 Kyant0 的任何 Kotlin/Compose 源码，只依据其公开算法与常数在 C++ 中重新实现。
- `AndroidLiquidGlass` 与 `AMDL` 目前仅作为设计参考记录，尚未移植。
- 参考副本存放于 `build/recon/ref_kyant/`，不参与正式构建。

---

# Third-party notices (English)

Parts of this project's compositor-layer implementation were derived from
**AndroidSurfaceImgui-Enhanced-Input-method** by ssyclr
(https://github.com/ssyclr/AndroidSurfaceImgui-Enhanced-Input-method),
MIT License, Copyright (c) 2025 AndroidSurfaceImgui C++17 Enhanced.

That project is itself based on **AndroidSurfaceImgui** by Bzi-Han
(https://github.com/Bzi-Han/AndroidSurfaceImgui), whose attribution is required
by the upstream additional terms and is reproduced above.

Only the technique for invoking the Android compositor without NDK-private
headers was ported; the UI, ImGui extensions and input modules were not.


## Android Open Source Project — realtime capture API reference (2026-10-13)

The persistent capture client uses the platform `android.window.ScreenCapture`,
`IWindowManager.captureDisplay`, and NDK `AHardwareBuffer` Unix-socket transport.
Its implementation is original glue code; it does not copy AOSP Binder classes
or their private vtables. API/lifecycle references are the Android 16 versions of
`core/java/android/window/ScreenCapture.java`,
`services/core/java/com/android/server/wm/WindowManagerService.java`,
`core/jni/android_window_ScreenCapture.cpp`, and
`libs/nativewindow/AHardwareBuffer.cpp` (Android Open Source Project,
Apache License 2.0). The existing ytbl/Kyant-derived glass shader is unchanged.


## ytbl (LiquidGlass-Cpp) — additive host hook (2026-10-13, Z-order backdrop)

`app/src/main/cpp/third_party/liquidglass/ytbl/` is otherwise byte-identical to
the upstream sources. One **additive, opt-in** hook was added so a host can
supply a Z-ordered backdrop per glass panel:

- `GlassUI.h`: `struct PanelSampleRect`, `using SceneComposer`,
  `void setSceneComposer(SceneComposer, void* user)`
- `GlassContext.h`: `void detail::setSceneComposerHook(...)`
- `Context.cpp`: storage for the hook + a call at the end of `glCaptureRegion`,
  after the framebuffer region has been copied out and before blur/paint
- `Runtime.cpp`: the public `lgx::setSceneComposer` forwarder

When no composer is registered (the default), `setSceneComposerHook` leaves the
function pointer null and the capture path executes exactly the upstream code.
No glass recipe, shader, constant, or parameter was modified.

Rationale: `bindBackdropTexture()` can only express "one global full-screen
image for every panel", and the framebuffer-capture path can only express
"what this process has already drawn". Liquid glass needs both at once
(process content **over** the world beneath the overlay), and the only place
where both the captured region and the capture texture are known is right after
the copy. See `docs/GLASS_ZORDER_COMPOSITOR.md`.
