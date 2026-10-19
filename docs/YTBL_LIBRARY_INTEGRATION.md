# ytbl 库集成评估 —— 一个重要纠正

> **结论先行**：`ytbl`（用户本地源码 `c++液态玻璃展示源码`）**不是一份 shader 参考，
> 而是一套完整的、与 ImGui 无关的纯 C++/OpenGL ES 液态玻璃组件库**。
> 它已经完整 vendored 在本项目里，且**已确认能为 aarch64 干净编译**。
>
> 我之前只用了它的 shader，这是**严重的低估**。

## 一、事实核对

### 1.1 本地源码 = 项目内 vendored 副本（逐字节相同）

```text
对比 D:\纯c图形基座\app\src\main\cpp\third_party\liquidglass\ytbl\
  vs C:\Users\<you>\Downloads\c++液态玻璃展示源码\液态玻璃\app\src\main\jni\ytbl\

结果: 相同 24, 不同 0, 仅我有 0
```

**所以「照搬用户那个项目」这件事其实早就做到了** —— M3 探针用的
`ytbl_panel.frag` / `ytbl_gather.frag` 就是从这份源码逐字提取的
（`build/recon/extract_shaders.py` 的源路径指向 vendored 副本）。

### 1.2 它是一个完整组件库

库自己的 `使用说明.md` 原文：

> 本库是纯 C++ / OpenGL ES 实现的液态玻璃组件库，共 23 个文件
> （15 个 .cpp、5 个 .h，外加 LICENSE、NOTICE、本说明）。

包含的成品组件（`app/src/main/cpp/third_party/liquidglass/ytbl/`）：

| 文件 | 内容 |
| --- | --- |
| `GlassPanel.cpp` | 玻璃面板（margin 公式、gather 链的宿主）|
| `Dialog.cpp` | 对话框组件 |
| `PopupMenu.cpp` | 弹出菜单 |
| `TabBar.cpp` | 标签栏 |
| `PillButton.cpp` | 胶囊按钮 |
| `Toggle.cpp` / `Slider.cpp` | 开关 / 滑块 |
| `Context.cpp` | GL 管线（200 处 GL 调用）|
| `Runtime.cpp` | 生命周期（`startup()` / `shutdown()`）|
| `Shaders.cpp` | 全部 shader 源码 |
| `SpineSolver.cpp` / `SpringSim.cpp` | 动效 |
| `ColorAdapter.cpp` | 亮/暗主题适配 |
| `BezierPath.cpp` / `Icons.cpp` | 路径与图标 |

### 1.3 对 ImGui 零依赖（这是关键）

逐文件统计 `imgui|ImGui|IMGUI` 出现次数：

```text
BezierPath.cpp   imgui: 0     ColorAdapter.cpp imgui: 0
Context.cpp      imgui: 0     Dialog.cpp       imgui: 0
GlassPanel.cpp   imgui: 0     Icons.cpp        imgui: 0
PillButton.cpp   imgui: 0     PopupMenu.cpp    imgui: 0
Runtime.cpp      imgui: 0     Shaders.cpp      imgui: 0
Slider.cpp       imgui: 0     SpineSolver.cpp  imgui: 0
SpringSim.cpp    imgui: 0     TabBar.cpp       imgui: 0
Toggle.cpp       imgui: 0
```

而且 `GlassUI.h` 的 include 只有一行：`#include "GlassConfig.h"`。

**我之前以为它跟 ImGui 耦合，所以只敢用 shader —— 这个印象是错的。**

### 1.4 已确认为 aarch64 干净编译

```text
编译结果: 成功 15, 失败 0（共 15 个 .cpp）
命令: clang++ --target=aarch64-linux-android28 -std=c++20 -O2 -ffp-contract=off -c
```

## 二、它提供了什么接口（决定我怎么接）

`GlassUI.h` 里的关键 API：

```cpp
// 玻璃采样背景 —— 直接喂我的 screencap 纹理，不用自己写 shader pass
void setBackdropEnabled(bool on);                                   // :154
void bindBackdropTexture(unsigned int texture, int width, int height);  // :155
void releaseBackdropTexture();                                      // :156

// 绘制转发 —— 我只要实现这些虚函数，把绘制接到我的 GL 层
void attachCanvasBackend(CanvasBackend* backend);                   // :210

// 宿主喂给库的上下文
void setScale(float value);                                         // :143  屏幕密度
void setSurface(const Surface& surface);                            // :161
void setHostBounds(const Box& box);                                 // :162
void setTextMeasurer(TextMeasurer measurer);                        // :164  字体度量由宿主提供
void setPointer(const PointerState& state);                         // :159  指针/触摸状态
void setGlassColors(const GlassColors& colors, bool withMotion);    // :150
```

生命周期：

```cpp
bool startup();    // Runtime.cpp:428  → detail::glStartup()，返回 false = shader 编译失败
void shutdown();   // Runtime.cpp:434
```

### 2.1 需要实现的 `CanvasBackend`（~20 个纯虚函数）

```text
clipRect / onFillRect / onStrokeRect
onFillPoly / onFillConcave / onStrokePoly
onLine / onFillCircle / onFillTriangle / onFillArc
onText / onImage
onPushClip / onPopClip
onPanelCapture / onPanelPaint        ← 玻璃面板的渲染钩子（库自己画，宿主转发）
onProbe / onBlockPointer / onClearPointerBlock
```

**`onPanelCapture` / `onPanelPaint` 是玻璃的核心** —— 库内部完成玻璃绘制，
宿主只负责把结果落到 GL。

## 三、这改变了实现路径

### 之前（低估了库）的做法

自己维护 blit → 水平 gather → 垂直 gather → panel 四个 pass，
自己管理 FBO、gather 区域、margin 计算、tap 权重。
已经踩过的坑：margin 与 lensAmount 的耦合、gather 区域越界、tap 权重公式。

### 现在应该做的

**把整个 `ytbl` 库编进纯 ELF**，宿主只做三件事：

1. 实现一个 `CanvasBackend`，把绘制转发到已有的 EGL/GLES 图层
2. 把 `aurora_demo` 的 screencap 纹理通过 `bindBackdropTexture()` 交给库
3. 调 `startup()` + 驱动 `Dialog` / `GlassPanel` 等组件

**收益**：

- 不用再自己维护 shader pass 与 FBO —— 库内 `Context.cpp` 有完整的 200 处 GL 调用
- 直接用成品组件（Dialog / PopupMenu / TabBar / PillButton / Toggle / Slider），
  而不是只画一块玻璃板
- 动效直接用库自带的 `SpineSolver` / `SpringSim`
- **参数天然与用户参考一致**（因为是同一份代码）

**代价**：

- 要实现那 ~20 个虚函数
- 需要理清 `startup()` 对 GL 上下文的假设（是否要求特定 GLES 版本、
  是否自己创建 FBO、能否在我的 EGL surface 上工作）
- 库可能有自己的 GL 状态管理，与我的图层渲染需要协调

## 四、下一步（建议的顺序）

1. **读 `GlassInternal.h` + `Context.cpp` 的 `glStartup()`**，
   确认 `startup()` 对 GL 上下文的全部要求
2. **写一个最小 `CanvasBackend`**（先全部空实现 + 打印，验证能 `startup()` 成功、
   能画出东西），接到 `aurora_demo` 的 EGL 图层上
3. **接 `bindBackdropTexture()`** 把抓帧纹理交进去，看玻璃是否反射真实桌面
4. 逐步把空实现换成真实绘制，最后加 `Dialog` 等组件

**风险最低的第一步是第 2 步**：即使绘制全是空的，只要 `startup()` 返回 true、
不崩，就证明这条路可行。

## 五、与现有工作的关系

| 已有成果 | 是否还需要 |
| --- | --- |
| `ytbl_shaders_embedded.h`（逐字 shader）| 不再需要 —— 库自带 `Shaders.cpp` |
| `probe_m3_ytbl.cpp` 的四个 pass | 作为**对照参考**保留，验证库的输出是否一致 |
| `squircle_path.h` / SDF 烘焙 | 仍然有用 —— 库用的是标准圆角矩形 SDF（已核实），想要连续曲率仍需自己接 |
| `Motion.h` 弹簧实现 | 可退役 —— 库自带 `SpringSim.cpp` |
| `InputShield.h` | 已废弃为主路线（见 `M4_INPUT_SHIELD.md`）|
| M1/M2 的图层与抓帧 | **仍然必需** —— 那是纯 ELF 的地基，库不管这些 |
