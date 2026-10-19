# Aurora UI

Aurora 是一个面向 Android NDK 和授权宿主的 C++ UI Runtime。当前首个纵向切片使用 Android Activity + Surface、GLES3 和仓库内固定的 Dear ImGui 1.90.7 源码。

Dear ImGui 只作为底层适配器使用。Aurora 自己维护输入路由、UI 组件、主题、动画、材质 fallback 和 Android 生命周期，页面不需要直接管理 EGL、GLES 或 Surface。

## 当前范围

- Android Activity + SurfaceView 宿主；
- Android `TYPE_APPLICATION_OVERLAY` 宿主：全屏透明渲染 Surface 只负责显示，另一个全屏输入盾消费所有触摸；只有从 Aurora 面板开始的触摸序列转发到 native，面板外触摸不会穿透到后方应用或触发其输入法；
- EGL/GLES3 渲染线程和 Surface 重建；
- `RenderBackend` 后端描述层：GLES3 ready，Vulkan capability probe + Android Surface/Swapchain 初始化骨架；
- 启动时 Vulkan capability probe：报告系统 Vulkan API 版本和物理设备数量，不改变默认 GLES3 路径；
- 官方 ImGui Vulkan backend `1.90.7` 已固定并参与 Android ABI 编译，当前不替换默认 GLES 提交路径；Vulkan Surface 探测必须使用独立的 Vulkan Surface，不能复用已经绑定 EGL 的 GLES `ANativeWindow`；
- Activity 可通过启动参数 `backend=vulkan` 在 Surface 首次创建前选择 Vulkan；不传参数时默认使用 GLES3。Overlay 仍固定使用 GLES3，避免同一 Runtime 实例在窗口生命周期中切换图形 API。
- C++20、NDK `27.0.12077973`、CMake 3.22.1；
- 稳定 pointer ID、多点触控元数据、主指捕获和取消；
- `Spring` 动画基元；
- `Theme`、`Widgets`、`AuroraUi` 组件层；
- Activity 自有 framebuffer 输入、半分辨率双向高斯模糊；Overlay 在用户确认 MediaProjection 后使用投屏帧作为真实桌面反射源，无授权或源失效时降级到安全的半透明玻璃材质；
- Android 原生 IME 文本提交和退格桥接；
- Overview、Material、Input 三页 Showcase。
- `showcase=glass` 直接复用用户提供的 `secondary_imgui.cpp` 和 `ytbl` 展示界面；Aurora
  只替换其 Android Surface、输入、IME 和生命周期宿主，并使用示例同一份嵌入字体。

任意第三方进程加载、未授权跨应用屏幕采集和 Vulkan 不属于当前首个切片。公开 Android Overlay 宿主已经完成；用户明确确认 MediaProjection 后的投屏帧反射已接入；Root 专用 native 宿主仍会复用同一套 native Runtime，只有 Host 适配层不同。

参考 `SoyBeanMilkx/imgui-liquid-glass` 后，项目继续保持同一边界：液态玻璃的实时模糊、折射和高光需要宿主提供当前 framebuffer/swapchain image。普通 `TYPE_APPLICATION_OVERLAY` 不能直接读取后方任意应用的合成画面；本项目通过用户确认的 MediaProjection 将桌面帧上传到 GLES 纹理，另提供 `aurora_root_capture` 探测特定 ROM 的受支持 SurfaceFlinger AIDL 零拷贝源。未授权或未验证的 Root 私有接口不会被伪装成可用能力。

液态玻璃展示迁移边界：`app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp`
和 `ytbl` 源文件来自用户提供的 demo，保持原组件、圆角、折射、动画和文本布局；
`LiquidGlassRuntime` 负责调用顺序、Surface 尺寸、输入指针和背景纹理契约。示例的
`字体.h` 已作为 `font.h` 固定在同一目录，并在 ImGui 初始化时以 40px 加载，随后按
示例的 2 倍 UI 尺寸缩放。该路径不再用 Aurora 自己的 Glass Lab 控件替代原 demo。

外部 Overlay 由 `AuroraOverlayService` 创建两个职责分离的窗口：全屏透明渲染 Surface 设置 `FLAG_NOT_TOUCHABLE`，全屏透明输入盾消费触摸并设置 `SOFT_INPUT_ADJUST_NOTHING`；输入盾只转发从 Aurora 面板内开始的触摸序列。它默认 `FLAG_NOT_FOCUSABLE`，只有 native 输入框实际激活时才切换为可聚焦并建立 `InputConnection`，因此 Overlay 启动和面板外触摸不会拉起输入法。使用者不需要自己编写 SurfaceFlinger 或 InputFlinger 代码。Overlay 使用通知栏 Stop action 显式关闭，并在服务销毁时同步移除两个窗口，避免留下黑色残留图层。

Root 设备可以使用 `root-host/aurora_root_bootstrap` 作为 native 启动入口。它是启动 APK/服务的 bootstrap：默认通过受支持的系统命令授予当前应用的 Overlay AppOp，再启动 APK 内的 Aurora Overlay 服务；`showcase=glass` 由明确的子命令传给 APK。它自身不是 native Surface/Input/IME 宿主，也不做桌面捕获，不调用未经验证的私有 InputFlinger 或截屏绕权接口；绘制、输入路由、穿透区域和生命周期仍由 APK 内的 Aurora Host/Runtime 负责。

Root bootstrap CLI：

| 用途 | 命令 |
| --- | --- |
| 启动 APK 中 `showcase=glass` 的 Overlay service | `aurora_root_bootstrap overlay --showcase glass` |
| 启动 `showcase=glass` 且请求系统截屏授权的 Overlay service | `aurora_root_bootstrap overlay --showcase glass --capture` |
| 启动默认 Overlay service | `aurora_root_bootstrap overlay`（等价于无参数） |
| 启动 Activity 宿主 | `aurora_root_bootstrap activity --backend gles\|vulkan [--showcase glass]` |
| 停止服务与进程（幂等） | `aurora_root_bootstrap stop` |
| 查看服务与 AppOp 状态（幂等） | `aurora_root_bootstrap status` |
| 只打印将执行的系统命令 | `aurora_root_bootstrap <子命令> --dry-run` |

`--capture` 只对 `overlay --showcase glass` 有效，生成的启动命令为：

```text
/system/bin/am start -n com.aurora.runtime/.MainActivity --ez overlay true --es showcase glass --ez capture true
```

它把 `capture=true` 作为 APK 启动 extra 传给 Activity。APK 收到该 extra 后会调用 Android 系统的 MediaProjection 授权流程，由用户在系统弹窗中显式确认；只有授权成功后才会把授权 token 交给 `AuroraProjectionService` 建立采集通道。因此它触发的是 Android 系统 MediaProjection 授权，**不代表 Root ELF 绕过授权**：ELF 本身既不采集屏幕，也不持有或伪造授权 token；用户拒绝授权时 Overlay 仍按普通模式启动，只是没有屏幕采集源。

`--capture` 缺少 `--showcase glass`、或与非 `overlay` 子命令（`activity`/`stop`/`status`）组合时，会在执行任何系统命令前被拒绝并打印用法（退出码 2），不会静默忽略该参数。

旧参数保持兼容：`--glass`（等价 `--showcase glass`）、`--activity`、`--stop`、`--status`、`--backend gles|vulkan`。

- 幂等：`stop` 在服务未运行时仍返回 0，重复执行 `stop`/`status` 结果一致；
- 校验：未知参数、非 `gles|vulkan` 的 backend、相互冲突的子命令都在执行前拒绝并打印用法（退出码 2）；`--capture` 只在 `overlay --showcase glass` 下被接受，其他组合同样以退出码 2 拒绝；`--backend` 出现在 Overlay 命令中只告警，不改变 Overlay 固定 GLES3 的行为；
- 无 shell 拼接：所有系统命令以 argv 直接 exec（`/system/bin/am`、`/system/bin/cmd`、`/system/bin/dumpsys`），参数只来自固定白名单，调用方文本不会进入 shell；
- 可诊断状态：`start` 打印 `overlay_appop=granted|not_granted`、`start_command=accepted`、`overlay_service=running|not_detected`；`status` 打印 `overlay_service=running|stopped` 与 `overlay_appop=granted|not_granted`，并附原始命令输出；
- 退出码：`0` 成功；`2` 用法/参数错误；`3` `am start` 失败；`4` 无法执行系统 helper。`start` 返回 0 表示 `am start` 已被接受，`status` 在服务停止时也返回 0（查询成功）；请以 `overlay_service=`、`overlay_appop=` 两行判断实际状态。

已在测试机 上实测 arm64 bootstrap：`su` 执行启动器后 Overlay 窗口出现在 Settings 上方，后方窗口仍存在；执行 `--stop` 后 Aurora 窗口和服务均消失。部分 ROM 会拒绝通过 Binder 修改 AppOps，即使调用者是 Root；启动器会报告该警告，并使用设备已有的 Overlay 授权状态。

## 构建

工程使用仓库内 Gradle Wrapper。当前环境基线：

```text
JDK 17
Gradle 8.14.3
Android Gradle Plugin 8.13.2
Android SDK Platform 36
NDK 27.0.12077973
CMake 3.22.1
```

本切片有三个产物：APK（应用内宿主 + 公开 Overlay 宿主）、Root ELF bootstrap（启动 APK 宿主的可执行文件）和 Root desktop capture ELF（设备能力探测/验证工具）。

### 产物一：APK

```text
gradlew.bat :app:assembleDebug
```

输出路径：

```text
app/build/outputs/apk/debug/app-debug.apk
```

### 产物二：Root ELF bootstrap（本机编译检查）

用于 `--help`、`--dry-run` 和 CLI 契约测试，不操作设备：

```text
cmake -S root-host -B build/root-host -G Ninja
cmake --build build/root-host --config Release
```

输出 Windows 主机下的 `build/root-host/aurora_root_bootstrap.exe`（Linux 主机为 `build/root-host/aurora_root_bootstrap`）。

### 产物二：Root ELF bootstrap（Android arm64-v8a，推送设备用）

```text
cmake -S root-host -B build/root-host-android -G Ninja -DCMAKE_TOOLCHAIN_FILE=D:/Android/Sdk/ndk/27.0.12077973/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24
cmake --build build/root-host-android --config Release
```

输出路径：

```text
build/root-host-android/aurora_root_bootstrap
```

### 产物三：Root ELF desktop capture（Android arm64-v8a，能力探测/设备验证用）

同一份 `root-host` 还会生成 `aurora_root_capture`。它不是 MediaProjection 绕权器：`probe`/`sources` 会报告当前 ROM 是否暴露可用的 SurfaceFlinger AIDL；`zerocopy-selftest` 只验证 AHardwareBuffer 到 EGLImage 的导入；只有 probe 判定 private AIDL ready 时，`frames` 才会尝试实时源。普通 Android 设备上应使用 `bootstrap overlay --capture` 走用户确认的 MediaProjection。

```text
build/root-host-android/aurora_root_capture
```


- [Overlay 触摸穿透实现方案](docs/TOUCH_PASSTHROUGH.md)
- [Android 输入法实现方案](docs/ANDROID_IME_BRIDGE.md)

## 宿主测试

连接 Android 设备并开启 USB 调试后，先安装 APK 产物，再按宿主类型启动。

### APK 产物：安装 / 启动 / 停止 / 状态

```text
adb devices -l
adb install -r app/build/outputs/apk/debug/app-debug.apk

# 启动 Activity 宿主（默认 GLES3，可追加 --es backend vulkan）
adb shell am start -n com.aurora.runtime/.MainActivity

# 启动公开 Overlay 宿主（需要系统允许的悬浮窗权限）
adb shell am start -n com.aurora.runtime/.MainActivity --ez overlay true

# 启动 showcase=glass 的 Overlay 宿主
adb shell am start -n com.aurora.runtime/.MainActivity --ez overlay true --es showcase glass

# 启动 showcase=glass 的 Overlay 宿主并请求系统 MediaProjection 授权（需在系统弹窗确认）
adb shell am start -n com.aurora.runtime/.MainActivity --ez overlay true --es showcase glass --ez capture true

# 停止：通知栏 Stop action，或直接停服务与进程
adb shell am stopservice -n com.aurora.runtime/.AuroraOverlayService
adb shell am force-stop com.aurora.runtime

# 状态
adb shell dumpsys activity services com.aurora.runtime
adb shell cmd appops get com.aurora.runtime android:system_alert_window

adb logcat -s Aurora
```

### Root ELF 产物：安装 / 启动 / 停止 / 状态

```text
adb push build/root-host-android/aurora_root_bootstrap /data/local/tmp/aurora_root_bootstrap
adb shell su -c "chmod 755 /data/local/tmp/aurora_root_bootstrap; /data/local/tmp/aurora_root_bootstrap --help"

# 启动 APK 中 showcase=glass 的 Overlay service（Root）
adb shell su -c "/data/local/tmp/aurora_root_bootstrap overlay --showcase glass"
# 旧写法的等价命令
adb shell su -c "/data/local/tmp/aurora_root_bootstrap --glass"

# 启动 showcase=glass 的 Overlay service，并让 APK 走 Android 系统 MediaProjection 授权
# （系统会在设备上弹出授权确认框；这不是 Root 截屏绕权，用户拒绝时只启动普通 Overlay）
adb shell su -c "/data/local/tmp/aurora_root_bootstrap overlay --showcase glass --capture"

# 停止（幂等，可重复执行；--stop 为兼容写法）
adb shell su -c "/data/local/tmp/aurora_root_bootstrap stop"
adb shell su -c "/data/local/tmp/aurora_root_bootstrap --stop"

# 状态（幂等；--status 为兼容写法）
adb shell su -c "/data/local/tmp/aurora_root_bootstrap status"
adb shell su -c "/data/local/tmp/aurora_root_bootstrap --status"

# Activity 后端测试
adb shell su -c "/data/local/tmp/aurora_root_bootstrap activity --backend gles"
adb shell su -c "/data/local/tmp/aurora_root_bootstrap activity --backend vulkan"

# 执行前确认将运行的系统命令（不执行任何命令）
adb shell su -c "/data/local/tmp/aurora_root_bootstrap overlay --showcase glass --dry-run"
adb shell su -c "/data/local/tmp/aurora_root_bootstrap overlay --showcase glass --capture --dry-run"
```

Overlay 宿主需要系统允许的悬浮窗权限。授权后可通过带 `overlay=true` 的启动入口运行；全屏输入盾会消费触摸，只有从 Aurora 面板内开始的触摸序列进入 native，面板外触摸不会落到后方应用；退出时使用通知栏的 Stop action。

Root ELF bootstrap 只负责启动/停止 APK 宿主和报告状态，本身不创建 Surface、不接收输入、不提供 IME，也不采集屏幕内容。`--capture` 同样只是把 `capture=true` 传给 APK，屏幕采集与 MediaProjection 授权确认全部发生在 APK 内，Root ELF 不绕过该授权。

当前真机验证目标是测试机。未连接设备时可以完成本机构建、CLI 契约测试（`tests/RootBootstrapCliTest.cpp`）、`--dry-run` 输出检查和 arm64 交叉编译，但无法证明真实 GLES 画面、触摸、IME 和 Surface 重建行为。

## 本地逻辑测试

不依赖 Android SDK 的输入、动画测试和 Root bootstrap CLI 契约测试位于 `tests/`。在 Windows MinGW 环境下：

```text
g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/InputRouterTest.cpp app/src/main/cpp/InputRouter.cpp -o build/host-tests/InputRouterTest.exe
build/host-tests/InputRouterTest.exe

g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/MotionTest.cpp -o build/host-tests/MotionTest.exe
build/host-tests/MotionTest.exe

g++ -std=c++20 -Wall -Wextra -I app/src/main/cpp tests/GestureRouterTest.cpp app/src/main/cpp/InputRouter.cpp app/src/main/cpp/GestureRouter.cpp -o build/host-tests/GestureRouterTest.exe
build/host-tests/GestureRouterTest.exe

g++ -std=c++20 -Wall -Wextra -I root-host tests/RootBootstrapCliTest.cpp -o build/host-tests/RootBootstrapCliTest.exe
build/host-tests/RootBootstrapCliTest.exe
```

产品边界和阶段要求见 [PRD.md](PRD.md)，模块接口和生命周期设计见 [TECHNICAL_DESIGN.md](TECHNICAL_DESIGN.md)。
