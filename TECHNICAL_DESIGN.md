# Aurora UI 技术设计说明

> 对应产品文档：`PRD.md`
>
> 文档状态：首个 GLES 纵向切片设计
>
> 本文只定义基座的技术边界、接口职责和实现顺序，不定义具体业务页面。

## 1. 设计结论

Aurora 首个可交付版本采用 **C++20 + Android NDK + GLES3 + Dear ImGui Adapter**。Dear ImGui 只位于适配层，不向业务页面暴露；页面、布局、输入、动画和材质属于 Aurora 自己的运行时。

首个宿主选择自有 Android `Activity + Surface`，并在 GLES 闭环稳定后复用同一 Runtime 实现公开 Android Overlay。任意应用内加载、Root 专用 native 宿主和 Vulkan 仍然独立适配，不改变 UI Core。

```text
Android Activity / Surface
        |
      IHost
        |
   AuroraRuntime
   /     |      \
Input  UI Core  Motion
          |       |
       Widgets  Material
          \       /
          ImGuiAdapter
                |
             GLES3
```

## 2. 模块职责

### 2.1 `Host`

`Host` 是 Android 和 Aurora Runtime 的边界，负责：

- Surface 创建、销毁、尺寸和密度变化；
- 帧时钟和帧请求；
- Activity 暂停、恢复和窗口焦点；
- Android 触摸事件、软键盘和安全区域；
- 向 Runtime 报告能力，例如是否可提供背景纹理。

`Host` 不负责组件样式、页面业务和 ImGui 全局状态。

建议接口：

```cpp
struct HostCapabilities {
    HostMode mode = HostMode::Activity;
    TouchPolicy touchPolicy = TouchPolicy::PanelOnly;
    bool hasSurface = false;
    bool supportsBackgroundTexture = false;
    bool supportsIme = false;
    bool supportsSystemOverlay = false;
    bool supportsRootNative = false;
    int apiLevel = 0;
    TouchRegion interactiveRegion{};
};

class IHost {
public:
    virtual ~IHost() = default;
    virtual HostCapabilities capabilities() const = 0;
    virtual void requestFrame() = 0;
    virtual void onSurfaceCreated(void* nativeWindow) = 0;
    virtual void onSurfaceDestroyed() = 0;
    virtual void onSurfaceSizeChanged(int width, int height, float density) = 0;
};
```

实际项目中不把 `void*` 继续向上层传递；它只在 Android/GLES Host 实现内部转换为 `ANativeWindow*`。

### 2.2 `Input`

输入层把 Android 的 pointer index 转换为稳定的 `pointerId`，并在 Runtime 内维护：

- `Down/Move/Up/Cancel` 生命周期；
- 触点捕获和手势归属；
- 坐标到逻辑 UI 坐标的转换；
- 窗口失焦、系统抢占和 Surface 销毁时的取消；
- IME 文本事件和输入焦点。

触控不能直接伪装成单个鼠标点击后丢给 ImGui。ImGui Adapter 只能接收经过 Aurora 路由后的指针、键盘和文本语义。

### 2.3 `UI Core`

UI Core 负责页面层级和交互状态：

- 稳定 ID；
- 页面、浮层、模态层和滚动容器；
- 布局约束、测量和摆放；
- 命中测试和可交互区域；
- 焦点、键盘导航和输入法焦点；
- 每帧构建视图与跨帧保留状态。

UI Core 可以采用即时调用式页面 API，但不能把跨帧状态只放在临时栈变量里。动画、滚动、焦点和捕获关系必须存放在 Runtime 的稳定状态表中。

### 2.4 `Widgets`

Widgets 是页面可以使用的组件层。首批只实现：

- `Panel`；
- `Button`；
- `Toggle`；
- `Slider`；
- `TextInput`；
- `List`；
- `SegmentedControl`；
- `Modal`；
- `Toast`。

每个组件必须返回语义事件和状态变化，不得让页面直接解析 ImGui 的 `IsItemHovered`、`IsItemActive` 等内部状态。

### 2.5 `Motion`

动画层以属性为单位管理动画，不绑定具体绘制 API。首版需要：

- 时间缓动；
- 二阶弹簧；
- 当前值、目标值和速度；
- 目标修改时不中断重置；
- 手势接管后从当前值和速度继续；
- reduced-motion 和全局效果强度。

动画时间使用单调时钟和秒作为单位，不按固定帧数累加。

### 2.6 `Material`

材质层只描述效果，不负责决定画面源来自哪里。首批材质：

- 实色；
- 线性/径向渐变；
- 半透明面板；
- 阴影和描边；
- 玻璃面板；
- 无背景源时的玻璃降级材质。

玻璃材质的输入是可选的 `BackgroundTexture`。如果 Host 没有提供，Material 选择 fallback，不允许直接访问其他进程或私有系统接口。

当前 `GlassMaterialSpec` 已保留可选 `ImTextureID` 输入和动态光泽相位：

```cpp
struct GlassMaterialSpec {
    ImVec4 tint;
    ImVec4 highlight;
    ImVec4 border;
    float rounding;
    float sheenPhase;
    bool hasBackgroundTexture;
    ImTextureID backgroundTexture;
};
```

宿主没有纹理时使用半透明、描边、内层高光和动态 sheen fallback。当前自有 Activity/GLES Host 已提供上一帧 framebuffer 的纹理输入，并执行半分辨率降采样、横向高斯和纵向高斯 pass；Overlay Host 明确禁用后方应用画面输入。未来 Vulkan Host 只需要提供同一契约的合法 framebuffer/swapchain texture，不改变页面组件和材质调用。

### 2.7 `ImGuiAdapter`

Adapter 负责：

- 将 Aurora 输入状态映射到 `ImGuiIO`；
- 将 Aurora 组件绘制请求转成 DrawList 或自定义绘制命令；
- 管理字体 Atlas 的后端上传；
- 在 GLES 设备重建时重新创建 ImGui 后端资源。

Adapter 不拥有页面状态、宿主生命周期或业务数据。未来可以用另一个 Renderer 替换它，而不改变 UI Core 和 Widgets。

### 2.8 `RenderBackend`

`RenderBackend` 是 GLES3 和 Vulkan 的公共边界：

- `BackendKind::Gles3`：当前已实现并在测试机 上验证；
- `BackendKind::Vulkan`：接口已定义，作为独立后端运行；已具备设备、图形队列、Android Surface、present 能力查询、Swapchain/image view、同步资源、RenderPass/Framebuffer 和 ImGui DrawData 提交路径，仍需真机验收；
- 页面只能读取 `BackendDescriptor`，不能创建 EGL/Vulkan 对象；
- 后端替换不改变 UI Core、Widgets、Motion、Material 或页面 API。

官方 Dear ImGui `imgui_impl_vulkan.cpp/.h` 已固定到同一 `1.90.7` 快照，并作为独立 `imgui_vulkan` CMake 目标参与各 ABI 编译；Vulkan 后端在显式选择时使用完整的 Android Surface、Swapchain、Queue、CommandBuffer、同步和 RenderPass 生命周期，默认 GLES 路径不受影响。

Runtime 启动时会通过动态加载 `libvulkan.so` 做无 Surface 的能力探测：读取 Vulkan API 版本、创建临时 Instance、枚举物理设备并立即销毁。探测结果只用于诊断和后端选择，不会在 GLES 宿主中偷偷创建 Vulkan 设备；真正的 Vulkan 绘制仍需要独立 Surface/Swapchain backend。

`VulkanBackend` 已进一步包含 Android 设备/图形队列初始化、Surface 能力检查、Swapchain/image view、同步资源、RenderPass/Framebuffer 创建和销毁路径，并调用官方 backend 提交 DrawData。Vulkan Surface 不能与同一个已绑定 EGL 的 GLES `ANativeWindow` 并行创建；因此 Activity 默认保持 GLES，Vulkan 必须在 Surface 首次绑定前选择独立后端。不能把未经过真机 present、重建和设备丢失验收的代码称为已完成。

Activity 的后端选择通过启动参数完成：`backend=vulkan` 必须在 Surface 首次创建前设置；缺省值为 GLES3。一个 Renderer 实例不在已有 EGL Surface 上热切换到 Vulkan。Vulkan 后端现在拥有命令池、命令缓冲、Fence、Semaphore、RenderPass、Framebuffer、DescriptorPool 和 ImGui Vulkan DrawData 提交路径；如果 Vulkan 初始化失败，宿主会保留诊断并回退到 GLES，避免黑屏。

当前材质层已经可以接收可选 `ImTextureID`，因此后端未来只需提供合法的 framebuffer/texture 输入，不需要修改页面代码。

## 3. Runtime 帧流程

每一帧按以下顺序执行：

```text
Host beginFrame
  -> 更新尺寸、密度、时间和能力
Input beginFrame
  -> 接收并归一化 Android 事件
Motion tick
  -> 根据单调时间更新属性和弹簧
UI beginFrame
  -> 清理过期状态，建立页面/浮层上下文
Page draw
  -> 通过 Widgets 构建当前帧 UI
Layout + hit test
  -> 确定可视区域、命中区域和手势捕获
Material/Render build
  -> 生成绘制命令和必要的离屏 pass
ImGuiAdapter submit
  -> 生成 ImGui DrawData 或 Aurora DrawList
GLES present
  -> 提交 Surface
Host endFrame
```

输入事件不能在 Render 阶段临时读取 Android 全局状态；输入快照必须在帧开始阶段固定，避免一帧内状态不一致。

## 4. 生命周期和资源恢复

Runtime 状态分为两类：

### 4.1 与宿主无关的状态

- 页面状态；
- 组件状态；
- 滚动位置；
- 动画值和速度；
- 主题和业务数据；
- 输入捕获的语义状态。

Surface 销毁时这些状态保留，除非宿主明确关闭 Runtime。

### 4.2 与 GLES 设备有关的状态

- EGLDisplay、EGLSurface、EGLContext；
- GLES 纹理、Framebuffer、Renderbuffer；
- ImGui 字体纹理和后端缓存；
- 材质 Shader 和程序对象。

Surface 销毁或 EGL 上下文失效时全部释放。Surface 重建后按固定顺序重新创建：

```text
EGL context
  -> Renderer resources
  -> Font atlas
  -> Material resources
  -> ImGui backend resources
  -> first frame
```

不能把 GLES 对象的句柄存入 UI Core 或页面状态。

## 5. 首个纵向切片

首个切片只需要证明基座闭环，而不是展示全部产品能力：

1. Activity 创建 Surface，Renderer 建立 GLES3 上下文；
2. Runtime 接收单指触摸和窗口尺寸变化；
3. Page 使用 `Panel`、`Button`、`Toggle` 和 `Slider`；
4. 一个 Panel 支持出现/收起动画；
5. Renderer 可安全处理 Surface 销毁和重建；
6. 无背景源时使用玻璃 fallback，不依赖屏幕采样；
7. 所有资源在 Activity 销毁时释放。

首个 Activity/GLES 纵向切片本身不包括以下能力，但这些能力已在后续阶段按独立 Host/Backend 边界继续推进：

- Root 专用 native Surface/Input broker；
- 任意第三方进程加载；
- Vulkan 独立 Surface 上的 DrawData、同步和 Present 真机验收；
- 跨应用屏幕采集；
- 最终版苹果式视觉效果。

公开 Android Overlay、基础多指手势、IME、Material fallback 和 Root bootstrap 已在后续切片中实现并完成部分测试机 验证。

## 6. 实现顺序

### S0：工程恢复与构建基线

- 确认 Android Studio、SDK、NDK、CMake 和 Gradle 版本；
- 恢复或建立 Android Activity、CMake 和 native library；
- 锁定仓库内 Dear ImGui `1.90.7` 源码快照和获取方式；
- 在目标设备上跑通空 Surface。

### S1：GLES Renderer 与 Host

- 抽象 `IHost`；
- 完成 EGL/GLES 生命周期；
- 完成帧时钟和 Surface 重建；
- 提供最小 RenderContext。

### S2：Input 与 UI Core

- 完成 pointerId 归一化和取消；
- 完成页面/窗口/命中测试最小模型；
- 完成 ImGuiAdapter 的输入边界；
- 完成一个可点击组件。

### S3：Widgets 与 Motion

- 完成首批组件；
- 完成 Token/Theme 数据结构；
- 完成 Tween、Spring 和动画中途改目标；
- 完成 Showcase 页面。

### S4：Material 与输入法

- 完成玻璃 fallback；
- Activity/GLES 已加入宿主自有 framebuffer 输入、半分辨率降采样和横纵高斯模糊；
- 再接入授权应用内 framebuffer；
- 通过 Android IME 桥接支持文本输入。

当前 S4 已验证：测试机 上可以建立 `InputConnection`、调起系统键盘，并将输入事件送入 native Runtime；输入法具体组合文本行为由 Android IME 决定。

### S5：后续宿主

- 公开 Android `TYPE_APPLICATION_OVERLAY` 宿主已完成：全屏透明渲染 Surface 只负责绘制，前台服务通知提供 Stop action；全屏输入盾消费触摸，服务销毁时同步移除输入盾和 Surface；
- Overlay 的最终窗口模型固定为两个窗口：全屏透明渲染 Surface 仅绘制并设置 `FLAG_NOT_TOUCHABLE`，全屏透明输入盾负责触摸分发并默认设置 `FLAG_NOT_FOCUSABLE`；输入盾只把从 Aurora 面板内开始的序列送入 Native，面板外触摸不会穿透；
- 在同一 `IHost` 边界下实现授权设备的 Root 专用宿主；Root 宿主负责系统级 Surface/Input broker 接入，页面和 Runtime 不感知差异；没有受支持的系统接口时，`HostCapabilities::supportsRootNative` 必须为 false；
- 当前 Root 交付包含 `root-host/aurora_root_bootstrap` 启动器和 `root-host/aurora_root_capture` 能力探测/零拷贝验证工具：bootstrap 不重复实现渲染器；capture 只有在 ROM 暴露兼容的 SurfaceFlinger AIDL 时才报告 realtime source ready，否则明确 blocked；用户授权的 MediaProjection 仍由 APK 负责。
- `root-host/aurora_root_bootstrap` 的 arm64 版本已在测试机 上实测启动和停止 Overlay；Root 进程本身不能保证获得 AppOpsService 的 Binder 管理权限，因此能力报告必须区分“Root 身份”和“Overlay AppOp 已授权”。
- 测试机 曾通过 ADB 完成 Activity、Overlay、IME、Root bootstrap 和窗口清理验证；ADB 断开时只执行本地构建和 host tests，不把设备未连接误判为 Vulkan 或 Root native renderer 已完成。
- 固化宿主能力报告和权限错误状态；
- 用自有测试宿主实现 Vulkan；
- 不修改 UI Core 和页面 API。

## 7. 当前需要补充的信息

以下信息不会影响接口设计，但在 S0 开始前必须明确：

1. Android Studio、Android SDK、NDK、CMake 和 Gradle 的版本；
2. 目标设备是否为测试机、Android 16，以及是否可通过 USB/ADB 连接；
3. 这个工作区是否有需要恢复的现有 Android/C++ 源码，还是从空工程建立；
4. Dear ImGui 是否允许在仓库内固定源码版本，还是必须使用外部依赖。

如果暂时没有这些信息，默认基线为 Android Studio 2026.x、NDK r27 (`27.0.12077973`)、CMake 3.22.1、GLES3、自有 Activity、无外部 Overlay。

安全区域由 Android Host 通过 WindowInsets 转换为逻辑坐标并写入 HostCapabilities::safeRegion；页面和材质不直接读取 Android WindowInsets。

## 8. 原始液态玻璃 Demo 宿主适配

用户提供的液态玻璃 demo 保存在 `app/src/main/cpp/third_party/liquidglass`。其中
`secondary_imgui.cpp` 负责完整展示页面，`ytbl` 负责面板捕获、模糊、折射、色散、
圆角、动画和交互探针；Aurora 不复制这些组件的绘制逻辑。

`LiquidGlassRuntime` 按原 demo 的帧顺序执行：`lgx::beginFrame`、Surface 与指针同步、
`secondary_imgui::Draw`，再由 ImGui OpenGL backend 提交。showcase 模式使用物理像素
的 `io.DisplaySize`、1 倍 framebuffer scale、ytbl 2 倍 UI 密度、40px 嵌入字体和
`StyleAllSizes(2.0f)`，与原 demo 的 `main.cpp` 保持一致。面板的 `onPanelCapture`/
`onPanelPaint` 回调从当前宿主 framebuffer 捕获区域并执行原 ytbl shader；不能把上一帧
已经绘制过的 Aurora UI 当作下一帧玻璃背景。

原 demo 的 `字体.h` 已固定为 `third_party/liquidglass/font.h`，并通过 ImGui 字体 Atlas
加载。诊断信息通过 `secondary_imgui::SetHostInfo` 注入原窗口底部的 runtime 折叠区域，
不会改变示例组件的视觉实现。
