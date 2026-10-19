# 纯 native ELF 液态玻璃覆盖层 —— Android 16 上五项系统级难题的解法

> 目标平台：测试机（测试机）/ Android 16 (API 36) / KernelSU root（uid 0，`u:r:ksu:s0`）/ SELinux Enforcing
> 交付形态：**单个 native ELF 可执行文件**，无 Java、无常驻 APK、无 Gradle、无 Xposed 模块
> 所有结论均经**真机实测 + AOSP `android16-release` 源码双向坐实**，文末给复现步骤

---

## 摘要：五项难题与结论

| # | 难题 | 本质障碍 | 结论 |
|---|---|---|---|
| 1 | **纯 ELF 液态玻璃上屏** | 无 Java 即无 `SurfaceControl` Java API；厂商 ROM 还有隐藏缺陷 | ✅ 真机验证 |
| 2 | **真实反射桌面程序** | 无 Java 环境下拿不到桌面内容做折射 | ✅ 真机验证 |
| 3 | **窗口范围内拦截触摸** | Android 把输入窗口的创建权限收归系统 | ✅ 四条验收全部通过 |
| 4 | **窗口移动时拦截跟随** | 直觉上需要每帧重设 region | ✅ SF 自动跟随，无需重设 |
| 5 | **拉起系统输入法** | 两道硬门：PID 门禁 + 包名归属检查 | ✅ 真机验证（含像素级证据）|

这五项的共同点：**Android 把「图层」「输入」「输入法」三条链路的入口，都设计成只对系统组件开放**。
本文记录的就是逐条绕开（而非绕过）这些限制的全过程，包括 5 个被实测证伪的错误假设。

---

## 背景：为什么「纯 ELF」在 Android 16 上这么难

NDK 给应用层的图形/输入 API 是 `ANativeWindow` + `AInputQueue`，而这两者的**生命周期都被 Java 框架层托管**
（`SurfaceView` / `Activity` 持有）。一个从 `init` 或 root shell 启动的裸进程两者都拿不到。

于是只剩一条路：**直接对着 binder 服务（SurfaceFlinger / InputFlinger / WindowManager / IMMS）说 AIDL 协议**。
本项目的全部工作，本质上就是手写这些协议。

> **一个贯穿始终的坑**：本机上 `defaultServiceManager()->getService()` 对**所有**服务返回 `null`。
> 正确做法是用 `IPCThreadState::transact(handle=0, code=1, ...)` 直连 servicemanager，
> 自己解析返回的 binder 句柄。

---

## 难题 1：纯 ELF 液态玻璃上屏

### 问题
没有 Java，就没有 `SurfaceControl.Builder`。要把像素显示到屏幕上，必须自己完成
`SurfaceComposerClient` 的构造 → `createSurface` → `getSurface` → EGL 上屏 这一整条链，
且全部通过 `dlsym` 解析 libgui 的 C++ 符号（**没有头文件，靠 mangled name**）。

### 根因：厂商 ROM 会返回一个「假 SurfaceControl」

这是本项目最隐蔽的一个坑，**它会让失败伪装成成功**：

```
I SurfaceComposerClient: createSurfaceChecked: Create fake SurfaceControl to prevent exception
E SurfaceControl: invalid handle (0x0) or client (0x6112e0fa20)
```

厂商的 libgui 在 `createSurfaceChecked` 内部失败时**不抛异常**，而是返回一个
`mHandle == nullptr` 的空壳 `SurfaceControl`。拿它去提交事务，SurfaceFlinger 只会打印：

```
W SurfaceFlinger: applyTransactions Handle 0x0 is not valid
```

并把**整个事务当空操作**——而 `Transaction::apply()` **仍然返回 0（成功）**。

⇒ 症状是「代码全部返回成功，屏幕上什么都没有」，极易误判成 EGL 或 shader 问题。

**判别与修法**：

```cpp
// 必须显式校验句柄，不能只看返回值
if (sc->getHandle() == nullptr) { /* 这是空壳，重试 */ }
```

实测**第 2 次调用必定成功**（`layerId` 非 0、`validate() == 0`）。
这条与函数无关、只与「第几次调用」有关——同一轮里 `createSurfaceChecked` 返回 `-19`、
紧接着的 `createSurface` 就成功。

### 解法要点

- **不要降权**：保持 uid 0。降到 `AID_GRAPHICS(1026)` 会让图层掉进 **Offscreen Hierarchy**（详见难题 3 根因 B）。
- **参数不要自己发明**：圆角半径、透镜深度、色调控制、`surfaceMode` 等一律从既有配方取值，
  并做 dp→px 换算（本机 `density = 3.0`）。
- 图层类型用 `eFXSurfaceEffect = 0x00020000`。
- 玻璃渲染管线（12-tap 可分离高斯模糊链 + 折射）**逐字复制已验证的实现**，不要按描述重新实现。

### 验证

```
渲染帧数 = 1200, 总耗时 12.6 s => 95.3 fps
模糊 pass 平均 = 0.12 ms/帧
面板 pass 平均 = 14.21 ms/帧
```

像素分布 `暗/透明 75.3% / 中调 24.7% / 亮 0.02%`，与「深色玻璃叠在深色背景上」完全吻合；
截图目视确认面板为深色磨砂玻璃，能透过它看清后方内容。

---

## 难题 2：真实反射桌面程序

### 问题
液态玻璃的核心观感来自**实时折射真实背景**。但在无 Java 的裸进程里，
拿不到「当前屏幕上显示的是什么」——Android 把整屏捕获收归 `MediaProjection`（需用户授权 + Java）。

### 解法
用 uid 0 直接跑 `screencap` 子进程抓屏，把输出喂给 GL 纹理，作为玻璃的 backdrop：

```
screencap -d <物理屏 DisplayId>   →   raw RGBA  →  glTexSubImage2D  →  玻璃 shader 采样
```

`DisplayId` 是个 **uint64**（本机物理屏 = `1111111111111111111`），不是逻辑编号 `0`；
用 `dumpsys SurfaceFlinger --display-id` 现查，不要硬编码别人的值。

### 最难的一环：防自反馈

抓屏抓到的画面里**包含我们自己刚画上去的玻璃面板**。如果直接拿它当 backdrop 去折射，
就形成正反馈回路，画面会自我放大直至饱和（早期版本表现为「整屏被白漆覆盖」）。

**解法**：采集线程在把帧交给 GPU 之前，先在 CPU 侧**抹除面板所占区域**，
再用面板外圈的像素做**环填充**。实测五帧连续比对：

```
第 1..5 次比对: 相邻帧平均像素差 = 0.00
平均差异 = 0.00 => 收敛，无自反馈
```

> ### ★★ 更正（更晚，2026-10-13）：这套解法**在 `aurora_overlay` 里一度是缺失的**
>
> 上面写的解法是**在旧的 `demo_entry.cpp` 探针里**验证的。移植到交付形态
> `root-host/elf/aurora_overlay.cpp` 时，这段「抹除 + 环填充」**漏掉了** ——
> 采集线程直接把整屏喂给 `renderer.projectionFrame()`，其中包含我们自己刚画上去的
> 玻璃面板。用户真机实测立刻复现：**「二级液态玻璃窗口无限镜像」**，
> 截图里是蓝/品红/橙/绿的失控递归色块（正反馈放大到饱和）。
>
> **已补上**，并把算法抽成可单测的纯函数：
> `app/src/main/cpp/SelfFeedbackErase.h` 的 `aurora::EraseRectWithRingFill()`，
> 由 `aurora_overlay.cpp` 的采集线程调用（`capture.setEraseRect(0, 0, kUiBoxW, kUiBoxH)`）。
>
> **★ 抽出来单测抓到了一个真机级的坑**（`tests/SelfFeedbackEraseTest.cpp` 8 组断言）：
> 参考像素原来写成 `lx = max(0, x0-1)` / `ty = max(0, y0-1)`，**区域贴边时会退化** ——
> 而本项目的抹除区域是 `(0,0)-(800,2160)`，**正好贴着左上角**，于是 `lx`/`ty`
> 落在区域内部，**左边缘和上边缘的原始玻璃像素被原样保留，自反馈断不干净**。
> 真机上这只表现为「边缘还残留一点镜像」，极难判断对错。
> 现在贴边时改取**对侧**像素；两个方向都没有外圈可用（区域横跨整幅）时，
> 诚实退化为「不填」（调用方不该把整幅图都当成自己的界面）。
>
> **★★ 还有一个顺序坑（我第一版就写错了）**：抹除必须放在**行翻转之前**。
> 采集管线是 `降采样 → 抹除 → 行翻转 → 交给 GPU`。我一开始把抹除插到了翻转
> **之后**，而注释还写着"此刻缓冲还是自上而下" —— 实际缓冲已经自下而上，
> 抹除却用自上而下的屏幕坐标 ⇒ **区域被垂直镜像**：
> 实际抹掉的是屏幕 `y∈[216,2376)`，而 `y∈[0,216)`（二级窗口标题栏那一段）
> 没抹，那里仍会残留自己的图层继续反馈。真机现象是「**上面**还残留一点镜像」。
>
> **★ 为了不再靠肉眼判断，状态日志现在会打印 `抹除帧=N`**：
> 只要这个数在涨，就说明抹除真的在跑；它不涨而镜像仍在，问题就在别处。
> 这个项目已经因为"靠肉眼判断"误判过两次（截图时机错位、区域垂直镜像）。
>
> **教训**：这类"画错了只是看着有点怪"的像素算法，必须抽成纯函数在 PC 上逐像素断言。
> 它要保证的性质其实非常明确 —— **区域内不得残留任何原始像素、区域外一个像素都不许动**。

### ★ 另一个真机复现的问题：窗口右侧的空白区也被拦截

用户报告：「**空白的没窗口遮挡的地方我滑动不了**」——
注意这正是下面「真实事故」里用户原话的同一句。

根因与内存无关，是**拦截区的形状**：`attachInputWindow` 只能设**一个矩形**，
之后无法替换（`Region` 替换符号已按 `[未核实-2]` 禁用），所以它必然是
两个窗口的**包络框**。原先主题窗口宽 860、二级窗口宽 760 ⇒ 包络框要 900 宽，
于是窗口右侧 100+ 像素的空白**也被拦截** —— 触摸进了我们的图层但什么都不做，
用户就是「滑不动」。

**已修**：把两个窗口宽度对齐到 760，并集成为规整矩形 `(16,16)-(776,2130)`，
拦截区收到 **800** 宽 ⇒ 右侧留出 280 像素的**穿透带**。
（`kUiBoxW` 900 → 800，见 `aurora_overlay.cpp`；窗口宽度见 `LiquidGlassRuntime.cpp`。）

### ⚠️ 这条路的代价（以及一次真实事故的完整归因）

`screencap` 的原始输出模式是**整屏 CPU 读回**：

```cpp
buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &base)   // CPU 读回
for (y...) write(fd, base + y*stride, w*Bpp);             // 每行一次 write()
```

本机 1080×2376 屏 ⇒ **单帧 ≈ 9.79 MiB**（`1080*2376*4 = 10,264,320 B`）。

> **真实事故**：在一台日常主力机上跑这个演示时，用户报告「整块屏幕卡死，窗口也滑动不了，
> **窗口没覆盖到的区域也滑动不了**」。
>
> #### 归因一：不是输入被拦截（源码级证伪）
>
> 直觉上「全屏裸图层吞掉了触摸」是最自然的解释，但它**不成立**。逐层核实：
>
> - 未调 `setInputWindowInfo` 的图层**确实会进** input snapshot
>   （`LayerSnapshotBuilder.cpp:1183-1185` 给它打上 `NO_INPUT_CHANNEL`；
>   本探针的图层因 `hasBufferOrSidebandStream()` 为真而 `needsInputInfo()` 成立，
>   且挂在上层级 ⇒ `reachablilty == Reachable`）。
> - **但它永远拿不到事件**：`InputDispatcher.cpp:5356-5360`
>   `if (NO_INPUT_CHANNEL) { ALOGW("Not sending touch gesture to %s ..."); return false; }`，
>   且 `:5482-5488` 会 `releaseChannel()`。
> - `InputChannel` 的唯一来源是 `Transaction::setInputWindowInfo`
>   （`SurfaceComposerClient.cpp:1924-1931`）——**本探针从未调用它**
>   （全文件无 `Transaction`、无 `createInputChannel`）。
>
> ⇒ 该窗口没有任何 InputChannel，`findTouchedWindowTargetsLocked` 永远不会选它做 target，
> **触摸照常落到它下面的真实窗口**。事后 `Input list` 里查不到它，
> 只是因为 dumpsys 是在探针退出、图层已销毁之后抓的。
>
> > **但这是本次没走那条路才安全**：一旦按「窗口范围内拦截触摸」给它塞进真 token，
> > 而图层仍是全屏 1080×2376，它就会变成**全屏 touchable 窗口**
> > （配合 `LayerSnapshotBuilder.cpp:362` 的 `touchOcclusionMode = BLOCK_UNTRUSTED`）
> > —— 那才会真的吃掉全屏触摸。**图层尺寸与 `touchableRegion` 必须同步收窄。**
>
> #### 归因二：`screencap` 的每帧进程 churn（真正的主因）
>
> 探针自身的常驻内存只有 **约 85 MiB**（4 块 9.79 MiB 帧缓冲 + 2 张 7.31 MiB gather 纹理
> + EGL BufferQueue 池 ≈29.4 MiB），**无内存泄漏（0 字节）**。
> 真正持续施压的是**每帧 spawn 一个 `screencap` 进程**：
>
> - 每帧一次 `posix_spawn`（一个完整 Android 进程 + `ProcessState::startThreadPool()`）
> - SurfaceFlinger 侧每帧 `createGraphicBuffer(1080, 2376, RGBA_8888, ...)`
>   = **一次全新的 10.26 MB dma-buf 分配**，且 usage 含 `GRALLOC_USAGE_SW_READ_OFTEN`
>   ⇒ 必然走 CPU 读回路径
> - 每次捕获都要掐 SF 主线程的 `mStateLock` 并做一次全屏 GPU 合成
>
> **关键判据**：`MemAvailable` 仍有 2.05 GB（**不是硬 OOM**）、`MemFree` 仅 270 MB、
> CPU 有 493%/800% idle、loadavg 12.3、swap 已用 7.9 GB。
> ⇒ 典型「**可回收内存耗尽**」。此时每个新的 10 MB dma-buf 分配都要先驱逐干净页，
> 而 `SW_READ_OFTEN` 的映射又走在 `get_user_pages` 路径上。
>
> **这解释了那个看似矛盾的现象**：探针自己工作集小、驻留稳，所以它仍能跑 73.7 fps；
> 而用户手里的 app 每次都要抢页/被抢占，表现为「屏幕卡死」。
>
> #### 数字更正（本节早先版本算错过）
>
> 3000 是**渲染**帧数；**采集只有约 5~6 fps**，40.7 秒内约 200 帧。
> 所以采集侧的实际搬运量是 **约 2 GB，不是 30 GB**。
> 「约 30 GB」那个量级其实来自 **GPU 带宽**（3000 帧 × 9.79 MiB 面板 pass ≈ 28.7 GB），
> **它是带宽，不是内存占用**，两者不能混为一谈。
>
> #### 结构性修复方向（已定位，未实现）
>
> | # | 改动 | 依据 |
> |---|---|---|
> | **F1** | 干掉每帧一次 `screencap` 进程 —— 换 `mirrorDisplay()`（**零拷贝、零读回、零子进程**；`SurfaceFlinger.cpp:5446` 显式放行 `AID_ROOT`）或改常驻采集 | 进程 churn 与 10 MB dma-buf 反复分配的根源 |
> | **F2** | 用 `CaptureArgs.frameScaleX/Y` **降采样**（`frameScale=0.25` ⇒ 270×594×4 ≈ 0.61 MiB/帧，1/16），模糊半径同步 ÷4 | ⚠️ **`-p`（PNG）是错的优化方向**：PNG 路径要额外压缩 + fork `am broadcast` 通知媒体扫描器，把内存压力换成 CPU 压力与更多进程 |
> | **F3** | 省掉每帧 `memcpy(uploadBuf, cap.front, kFrameBytes)`，双缓冲下可直接把 `cap.front` 交给 `glTexSubImage2D` | 唯一的每帧大拷贝 |
> | **F4** | 图层尺寸可缩，但**必须同步改 `uViewSpan` / `uSampleSpan` 语义**，否则着色器的 `sceneUv()` 映射整体错位。稳妥做法是宽度保持全屏、只收高度到 `panelH + 2*margin` | 缩了会打断 margin 语义 |
> | **F5** | ~~当前**不需要**做任何输入侧的保险~~ **★ 已过时，见下方更正块 —— 那个「将来」已经到了** | 见归因一的反面警告 |
> | **F6** | 用 `eLayerSkipScreenshot` 替掉 CPU 侧逐像素抹除面板那 900×620 的循环（`RequestedLayerState.cpp:85-87`），抓帧里天然不含自己的面板 | 既消自反馈又省一个 CPU 大循环 |

> **教训**：任何「每帧处理整屏 ~10 MB」的实现，都必须先算清**内存水位**再上真机；
> 而且**「整机卡死」的第一嫌疑不一定是输入被吞** —— 现场证据（`Input list`、`inhibited`、
> 图层残留）可能**全部正常**，真因藏在 `meminfo` 与 `swap` 里。

---

### ★★ 更正块（更晚，F5 的前提条件已经到来）

> 上面 F5 写的是「当前**不需要**做任何输入侧的保险」。**这条已经过时** ——
> 它假设「将来」才会加 input window。**现在加了**（难题 3 的交付物
> `aurora_overlay` 就是），于是 F5 预告的两件事**都真的发生了**，而且都踩过：
>
> | F5 的预告 | 实际情况 |
> |---|---|
> | 「必须把 `touchableRegion` 收到面板矩形」 | ✅ 踩过。`attachInputWindow` 一度用**图层尺寸 1080×2376** 当 region ⇒ **整屏拦截**，用户表现是「什么都点不动」。修成窗口包络框（当前 `900×2160`）后才符合「窗口范围内拦截」 |
> | 「若图层仍是全屏……」 | ✅ 图层仍是整屏 1080×2376（因为两个 ImGui 窗口都可被用户拖动）。所以 region 与图层尺寸**必须解耦**，这是常态不是特例 |
>
> **另外还有一条 F5 没预告、但同样致命的**（现在写进难题 3 的交付说明）：
> 全屏图层 + 默认的 `touchOcclusionMode = BLOCK_UNTRUSTED` 会让 InputDispatcher
> 把后面 app 的触摸**判为不可信遮挡后整条丢弃** —— 这才是「整屏卡死」的另一个真凶，
> 与 region 是两回事。必须显式设成 `ALLOW(2)`。
>
> **两条现在都有启动自检兜底**（`aosp_glue.h` 的 `selfCheck`）：
> - 第 5 项：`touchOcclusionMode` 写回读 `== ALLOW(2)`
> - 第 7 项：`touchableRegion` 的尺寸 `==` 调用方传入的窗口框尺寸（传 `kUiBoxW/H`）
>
> 也就是说：**以后谁改了窗口尺寸却忘了同步拦截区，启动自检会 FAIL 并拒绝上屏**，
> 不会再退化成「点不动」。改动见 `aurora_overlay.cpp` 的 `selfCheck(&check, kUiBoxW, kUiBoxH)`。

---

## 难题 3：窗口范围内拦截触摸

### 问题
要求是「**只在窗口矩形内**拦截触摸（后面的 App 收不到），窗口外完全正常」。
内核层的 `inhibited` 方案能做到「整屏抑制」，但**做不到按窗口范围**，
且曾因误伤用户触摸屏 12 秒而被废弃。必须走 SurfaceFlinger 输入窗口这条正道。

### 机制链（三步，全部 uid 0 可走）

```
1. IInputFlinger.createInputChannel(name)   →  InputChannel{fd, token}
      InputManager.cpp:255-266  显式白名单：uid == AID_SHELL(2000) || uid == AID_ROOT(0)
2. SurfaceComposerClient::createSurface(...)  →  SurfaceControl（必须重试，见难题 1）
3. Transaction::setInputWindowInfo(sc, WindowInfoHandle{ token = channel->getConnectionToken() })
      Transaction::setPosition / setCrop / setAlpha / setLayerStack
      Transaction::apply(synchronously = true)
```

**关键认知：不需要 buffer、不需要 EGL、不需要 `getSurface`。**
一个没有 buffer 的图层照样能进 InputDispatcher 的窗口列表——这一点与直觉相反，但已被实测和源码双重确认（见下）。

### ★ 三条真根因（全部从设备机器码 / AOSP 源码读出）

#### 根因 A：`createSurface` 第一次必定失败
同难题 1。**拿空壳提交事务，`apply()` 返回 0 但整个事务被丢弃。**

#### 根因 B：降权到 `AID_GRAPHICS(1026)` 会让图层掉进 Offscreen Hierarchy

`FrontEnd/LayerHierarchy.cpp::attachToParent()`：

```cpp
    if (layer->parentId != UNASSIGNED_LAYER_ID) {
        parent = getHierarchyFromId(layer->parentId);
    } else if (layer->canBeRoot) {
        parent = &mRoot;            // ← 屏上层级
    } else {
        parent = &mOffscreenRoot;   // ← 屏外层级
    }
```

而 `canBeRoot = LayerCreationArgs::addToRoot`，它在 `SurfaceFlinger.cpp:5515` 被改写为：

```cpp
    args.addToRoot = args.addToRoot && callingThreadHasUnscopedSurfaceFlingerAccess();
```

**屏外层级不会被 `forEachInputSnapshot` 遍历**（`LayerSnapshotBuilder.cpp:1297-1307`），
于是 `buildWindowInfos`（`SurfaceFlinger.cpp:4376`）永远不会把我们的窗口发给输入系统。

实测（`layerStack=6`，dry 模式，无拦截风险）：

| uid | 图层落在 |
|---|---|
| **0** | **屏上层级** ✅ |
| 1000 (`AID_SYSTEM`) | 屏上层级 ✅ |
| 1026 (`AID_GRAPHICS`) | **屏外层级** ❌ |

⇒ **保持 uid 0，不要降权。** 这条推翻了很多「root 程序应该降权到 graphics」的直觉。

#### 根因 C：`RefBase::incStrong(IBinder*)` 不能直接调用

`IBinder.h:52`：`class IBinder : public virtual RefBase` —— RefBase 是**虚基类**，子对象不在偏移 0。
设备 libbinder.so 的 `BpRefBase::BpRefBase(const sp<IBinder>&)` 机器码：

```
ldr  x8, [x0]            ; x8 = this 的 vptr
ldur x8, [x8, #-0x18]    ; vtable[-3] = 虚基类偏移
add  x0, x0, x8          ; x0 = RefBase 子对象
bl   RefBase::incStrong
```

本机实测该偏移 = **144**。所以 `RefBase::incStrong(bpBinderPtr, id)` 会把 `BpBinder+8` 当 `mRefs` 去加计数：
真正的强引用没变，还踩坏了 BpBinder 自己的字段；随后析构的 decStrong 会减一个从未加过的计数
→ **提前 delete → use-after-free**。

⇒ **修法**：**转移** `Parcel::readStrongBinder` 已经持有的那个强引用，不做 `incStrong`。

> 附带修掉一个静默 bug：`RefBase::incStrong` 是 **const 成员函数**，
> mangled name 是 `_ZNK7android7RefBase9incStrongEPKv`（带 `K`）。
> 写成 `_ZN...` 会让 `dlsym` 永远返回 null，函数静默失效。

### 被实测证伪的两个假设（省得后人再踩）

| 假设 | 实测结论 |
|---|---|
| 「无 buffer 的图层不能接收输入」 | **不成立**。`RequestedLayerState::needsInputInfo()` 对无 buffer 图层走 `getWindowInfo().token != nullptr` 分支即可通过；实测无 buffer、无 EGL、无 `getSurface` 的图层照样进了窗口列表 |
| 「`setInputWindowInfo` 的参数顺序和 `createSurface` 不同」 | **不成立**。差异来自「第几次调用」，与函数无关 |

真正决定图层能否接收输入的是：

```
LayerSnapshot::canReceiveInput()
  = !isHiddenByPolicy() && (!hasBufferOrSidebandStream() || color.a > 0.0f)
```

**没有 buffer 反而返回 true** —— 这与最初的直觉正好相反，项目里曾因此错误地引入 EGL。

### 验证：四条验收（真机）

| # | 要求 | 实测证据 |
|---|---|---|
| 1 | 窗口内触摸后面 App 收不到 | 点窗口内 → 本进程 InputChannel **读到 312+312 字节**（DOWN+UP） |
| 2 | 窗口移动时拦截跟随、无漏点 | 只发 `setPosition`，**未重设 region**，`touchableRegion` 自动从 `[100,200][700,800]` 变为 `[400,1400][1000,2000]`，再点新位置 → 通道 **读到 624 字节** |
| 3 | 窗口外触摸完全正常 | 点窗口外 → 本进程通道 **0 字节**，InputDispatcher 正常派发给后面 App |
| 4 | 退出后立即恢复、无残留 | 图层列表清空、`dumpsys input` 无本窗口、无进程、`inhibited=0` |

诊断入口是 **`dumpsys SurfaceFlinger` 的 `Input list` 段**（不是 HWC 合成列表——合成与否与输入无关）：

```cpp
out << "\nInput list\n";
mLayerSnapshotBuilder.forEachInputSnapshot([&](const frontend::LayerSnapshot& snapshot) { ... });
```

这与 `buildWindowInfos` 发给 InputDispatcher 的是**同一份清单**。

### 一个必须处理的细节：不回 `FINISHED` 会被判 ANR

`InputMessage` 协议要求应用读完事件后回一条 `FINISHED`（`InputTransport.h`：`KEY=0, MOTION=1, FINISHED=2, FOCUS=3`）。
不回的话 5 秒后：

```
W/InputDispatcher: Not sending touch to <name> because it is not responsive
```

此后该窗口的触摸被**直接丢弃**。生产实现应当使用 `libinput` 的 `InputConsumer`（自带 `sendFinished`）。

---

## 难题 4：窗口移动时拦截跟随

### 问题
玻璃面板是可拖动的。直觉上「窗口移动 ⇒ 触摸拦截区域要跟着改」，
所以每帧都要重算并重设 `touchableRegion`。但**实测不需要**。

### 结论与机制

**只发 `setPosition`，`touchableRegion` 会自动跟随：**

```
[100,200][700,800]  →  setPosition(400,1400)  →  [400,1400][1000,2000]
```

原因：`setInputWindowInfo` 里传入的 `touchableRegion` 是**相对于图层自身**的坐标，
SurfaceFlinger 在生成 `WindowInfo` 时会把它与图层的实时变换（position/scale/rotation）合成，
得到 display 空间的最终 region。

⇒ **应用侧只需维护「面板矩形相对于图层原点」这一个量**，
窗口移动/缩放/旋转都免费跟随，不会出现「移过去了但拦截区域还留在原地」的漏点。

### 验证
见难题 3 验收表第 2 条：移动后**没有重设 region**，新位置的触摸仍被正确拦截（渠道读到 624 字节）。

---

## 难题 5：拉起系统输入法

这是五项里最难、也是唯一需要改变交付形态的一项。

### 两道硬门

#### 硬门一：`Session` 的 PID 门禁（**uid 0 无效**）

`frameworks/base/services/core/java/com/android/server/wm/Session.java:142-148`（android16）：

```java
    synchronized (service.mGlobalLock) {
        mLastReportedAnimatorScale = service.getCurrentAnimatorScale();
        mProcess = service.mAtmService.mProcessMap.getProcess(mPid);
    }
    if (mProcess == null) {
        throw new IllegalStateException("Unknown pid=" + mPid + " uid=" + mUid);
    }
```

- `mPid` 来自 `Binder.getCallingPid()`（`Session.java:132`）——**内核填入，调用方无法伪造**。
- `mProcessMap` 是 `WindowProcessControllerMap`，内部是 `SparseArray<WindowProcessController>`，**key 是 pid**。
- 全 AOSP **只有一个写入点**：`ActivityTaskManagerService.java:6467`，
  其唯一上游是 `ActivityManagerService.java:984 addPidLocked(ProcessRecord)`，
  而 `ProcessRecord` **必须由 AMS 自己创建**（`ProcessList.java:3408`），
  且必须先在 `mPendingStarts` 挂号（唯一写入 `ProcessList.java:2168`，在 `startProcessLocked` 内部，**无 binder 入口**）。

**为什么 uid 0 完全没用**：这道门是「**数据结构成员资格**」，不是「权限」。
`getProcess(int pid)` 的签名里**根本没有 uid 参数**；而且 `Session.java:146` 的异常在
`Session.java:149` 的权限检查**之前**抛出——所有 uid 权限检查都被挡在身后，永不执行。

`IWindowManager.openSession`（`WindowManagerService.java:6003-6006`）与
`IActivityManager.attachApplication`（`ActivityManagerService.java:4852-4863`）**本来就没有权限门**，
任何 uid 都能调——root 在这里没有可解锁的东西。

> **旁路也已穷尽核查**：`IActivityManager` 全文 1041 行中**没有** `startProcess` 之类的注册接口；
> `Process.start()` 走 zygote socket，**零 AMS 参与**（fork 出来的进程 AMS 完全不知情）；
> `registerProcessObserver` 只存 observer。
> 用 `attachApplication` 硬闯更糟：陌生 pid 会走到
> `AMS:4567` 打印 `"No pending application record for pid ...; dropping process"`，
> 然后 `AMS:4571 killProcessQuiet(pid)` —— **SIGKILL 自己**。

#### 硬门二：`EditorInfo.packageName` 必须属于调用者 uid

`InputMethodManagerService.java:3863-3868`：

```java
    if (editorInfo != null && !InputMethodUtils.checkIfPackageBelongsToUid(
            mPackageManagerInternal, cs.mUid, editorInfo.packageName)) {
        Slog.e(TAG, "Rejecting this client as it reported an invalid package name.");
        return InputBindResult.INVALID_PACKAGE_NAME;
    }
```

链路：`checkIfPackageBelongsToUid` → `PackageManagerService.isSameApp` → `UserHandle.isSameApp(pkgUid, callingUid)`。
**没有任何已安装包的 appId 是 0**，所以 uid 0 永远过不了这一关。
传 `editorInfo == null` 也不是解——那会直接返回 `NULL_EDITOR_INFO`，拿不到 `InputConnection`，收不到任何文字。

### ⇒ 唯一可行形态

```
纯 ELF 单文件交付
  + 运行时临时安装一个内嵌的小辅助包（做窗口宿主）
  + 退出时自动卸载
```

完整链路：

```
native ELF (uid 0)
  ├─ 1. 释放内嵌的签名 APK 到 /data/adb/<tmp>/xxx.apk (0400)
  ├─ 2. fork + execl("/system/bin/app_process", "-Djava.class.path=<apk>", ...)
  │        native ↔ Java 之间走 stdin/stdout 管道
  └─ Java 桥接（root，非 AMS 启动）
       ├─ 3. pm install -r --user 0 -S <size>       ← 从 stdin 流式喂字节
       ├─ 4. appops set <pkg> SYSTEM_ALERT_WINDOW allow
       ├─ 5. cmd deviceidle tempwhitelist -u 0 -d 10000 <pkg>
       ├─ 6. am startservice -n <pkg>/.Service       ← ★ 关键：由 AMS 启动，是真正的应用进程
       ├─ 7. 在 127.0.0.1 上开 ServerSocket
       └─ Android Service（TCP 客户端）
            ├─ 8. WindowManager.addView(EditText, 1×1 透明 TYPE_APPLICATION_OVERLAY)
            └─ 9. ime.showSoftInput(editor, SHOW_IMPLICIT)
```

**为什么必须绕这一圈**：窗口必须由一个**被 AMS 启动的进程**来建（过硬门一），
而 `EditorInfo.packageName` 必须对应一个**真的装上去了的包**且 appId 匹配调用者 uid（过硬门二）。
`am startservice` 同时满足两者。

**代价**：冷启动约 **1.5~2.5 秒**（释放 APK + 引导 + 安装 + 回环握手），退出清理约 **160~310 ms**。

### 真机上修掉的 7 个拦路虎

| # | 症状 | 根因 | 修法 |
|---|---|---|---|
| 1 | 窗口建好了，C++ 永远等不到连接 | 辅助包 manifest **缺 `INTERNET`**。Android 的 paranoid networking 按 `inet` gid 放行 `socket()`，**连 127.0.0.1 回环也要** | manifest 加 `INTERNET` |
| 2 | Service 起来 1 秒后被杀 | `NetworkOnMainThreadException`：在 `WindowInsets` 回调里直接写 socket，StrictMode 直接终结进程 | 所有写出一律入队，由单线程写出 |
| 3 | `pm install` 后偶发「桥接未返回 READY」 | 写线程在 HELLO/READY 握手**之前**启动，排队的消息抢在 READY 前发出 | Java 先完成握手再启动写线程；C++ 侧循环泵直到 READY 或超时 |
| 4 | 辅助包永远卸载不掉，`close()` 卡满 15s | 子进程继承了管道**写端**副本 ⇒ 永不 EOF | `pipe2(lifePipe, O_CLOEXEC)` |
| 5 | SYNC 永远被拒（离线即可发现） | `line.split(" ", 6)` 最多 6 段，而判据是 `p.length < 7` ——**恒真** | 改 `split(" ", 7)` |
| 6 | TCP 方向反了（离线即可发现） | 原实现让 Service 去 bind 一个 C++ 已监听的端口 ⇒ `EADDRINUSE` | 改成 C++ 是服务端、Service 是客户端 |
| 7 | 待命标志一直生效，键盘不弹 | `showKeyboard()` 没把 `SOFT_INPUT_STATE_ALWAYS_HIDDEN` 换成 `UNSPECIFIED` | 显示/收起时同步切换 `softInputMode` |

> 第 5、6 条尤其值得记：**它们离线就能看出来，但在真机上会表现成「完全连不上」这种含糊症状**，
> 极易被误判成 SELinux 或权限问题。

### ★ 硬约束：键盘只能出现在物理屏（display 0）

`WindowManagerService.java:8222-8245`：

```java
    public @ImeClientFocusResult int hasInputMethodClientFocus(IBinder windowToken,
            int uid, int pid, int displayId) {
        if (displayId == Display.INVALID_DISPLAY) return ImeClientFocusResult.INVALID_DISPLAY_ID;
        synchronized (mGlobalLock) {
            final DisplayContent displayContent = mRoot.getTopFocusedDisplayContent();
            ...
            if (displayContent == null
                    || displayContent.getDisplayId() != displayId
                    || !displayContent.hasAccess(uid)) {
                return ImeClientFocusResult.INVALID_DISPLAY_ID;      // ← 虚拟屏死在这里
            }
```

`getTopFocusedDisplayContent()` 取的是 `mTopFocusedDisplayId`。而虚拟显示（如 测试工具 的
`AgentVirtualDisplay`）带 `FLAG_STEAL_TOP_FOCUS_DISABLED`：

```java
    boolean canStealTopFocus() {
        return (mDisplayInfo.flags & Display.FLAG_STEAL_TOP_FOCUS_DISABLED) == 0;
    }
```

⇒ 它**永远不可能**成为 top-focused 显示 ⇒ `mTopFocusedDisplayId` 恒为 0
⇒ `hasInputMethodClientFocus(..., 虚拟id)` 返回 `INVALID_DISPLAY_ID` ⇒ IMMS 不建立 session
⇒ `showSoftInput` **返回 true（调用被受理）但什么都不会显示**。

`IMMS.computeImeDisplayIdForTargetInner` 还补一刀：即使绑定成功，
显示的 IME 策略不是 `DISPLAY_IME_POLICY_LOCAL` 时，IME 会被显示到 **fallback display (=0)**。

**对本项目是好消息**：要交付的覆盖层本来就在物理屏上，那正是输入法唯一能工作的地方，
**不需要为虚拟屏做任何适配**。

### 验证：一个假阳性事故，与最终判据

**判定「输入法是被我们拉起来的」的唯一可靠判据是 `mCurClient` 的 uid == 辅助包 uid**，
不能只看 `mInputShown`。

> **真实事故**：某次实验因为文件传输失败，探针**根本没跑起来**（日志里只有
> `setsid: exec ./ime_probe: No such file or directory`），
> 但同一时刻采样器抓到了 5 帧 `mInputShown=true` —— 那是**用户自己**在用输入法（uid 10166 启动器）。
> **只看「键盘弹出来了」这一个信号，就会得出一个彻底的假阳性。**

### 最终验证：像素级证据（物理屏）

**为什么状态位不足以证明「显示出来了」**：`mInputShown` 是 IMMS 的记账位；
`mImeWindowVis` 是**输入法自己上报**的（`IMMS.java:6856-6862 → 2814-2828`）；
应用侧 `insets.isVisible(Type.ime())` 按 AOSP javadoc 只是「系统认为 IME 可见」，
**与是否真的覆盖本窗口无关**（`WindowInsets.java:397-415`）。⇒ 必须直接看屏幕像素。

**方法**：抓帧器**只在** `dumpsys input_method` 报 `mInputShown=true` 时截图，
并把同一时刻的 `mCurClient` 记入日志 —— 保证「屏幕上有键盘」与「键盘属于我们的客户端」
**逐帧时间对齐**。

两次独立实验（run1 / run2），逐帧记录（run1 含天然对照组）：

```
=== BASELINE（实验前）===
     mInputShown=false
   mCurClient=ClientState{5c93cd7 mUid=10420 mPid=22390 ...}      ← 别人

HIT#1 t=+15s  mCurClient=ClientState{6dcc082 mUid=10469 mPid=28259}  mInputShown=true
HIT#2 t=+17s  mCurClient=ClientState{6dcc082 mUid=10469 mPid=28259}  mInputShown=true
HIT#3 t=+19s  mCurClient=ClientState{6dcc082 mUid=10469 mPid=28259}  mInputShown=true
              ↑ 探针同轮打印 [阶段1] 辅助包 uid=10469  ← 精确对应
HIT#4 t=+21s  mCurClient=ClientState{11bbd2 mUid=1000 ...}      ← 对照：用户自己在「设置」里打字
HIT#5 t=+24s  （uid=1000）                                          （settings 以 uid 1000 运行）
HIT#6 t=+26s  （uid=1000）

=== AFTER（实验后）===
     mInputShown=false
   mCurClient=ClientState{230563b mUid=10166 ...}                 ← 交还
```

**这是一条完整的因果对照**：实验前后 `mCurClient` 都是别人的；
只有我们的 1×1 宿主窗请求输入法的那几秒里，它翻到辅助包 uid，
**同时**屏幕下半部分的像素就是百度输入法（`com.example.input`）的完整键盘
（候选词行 + 完整 QWERTY + `符 / 123 / 中英`）。
run1 的对照组还证明抓帧器**不会**把用户自己的键盘误记成我们的。

**「截的是物理屏」如何证明**（这条曾被独立审计质疑为缺口，现已补上）：

```
$ screencap -h
   -d: specify the display ID to capture (If the id is not given, it defaults to 1111111111111111111)

$ dumpsys SurfaceFlinger --display-id
Display 1111111111111111111 (HWC display 0): port=147 pnpId=QCM     ← 物理屏
Display 11529215047573656498 (Virtual display): "AgentVirtualDisplay"  ← 测试虚拟屏
```

抓帧命令是不带 `-d` 的 `screencap -p`，**默认值正是 `HWC display 0`**；
且探针同轮打印 `WINDOW requestDisplay=0 actualDisplay=0 ok`（宿主窗在 display 0），
与「IME 只绑定 top-focused 显示」的策略一致。

**原始证据与元数据**（含逐张 sha256、PC/手机双侧比对）：
`research/ime_physical_run/README.md`；截图 `shot_run1_hit_*.png`、`shot_run2_hit_*.png`；
手机侧打包原件 `frames_run1.tgz` / `frames_run2.tgz`。

**退出清理**（同轮实测）：`inhibited=0`、aurora 图层 = 0、辅助包已卸载、
临时目录不存在、残留进程 = 0。

> **方法论备注**：本节的存在本身是一次纠错的结果 —— 早先的结论文档
> （`research/ime_result.md` §十一）曾以状态位为依据宣布「键盘真的显示出来了」，
> 且引用的看门狗日志在仓库里**根本不存在**。一次独立审计推翻了该论证，
> 才有了上面这套像素级验证。
> **文档的结论也会错；能被重新验证的结论才作数。**

### 退出清理（真机实测）

```
aurora-ime 父进程已退出（stdin EOF），卸载辅助包
aurora-ime pm uninstall => Success
[收尾] close() 返回，用时 163ms ~ 310ms

inhibited(event7)      : 0
aurora SF 图层         : 0
辅助包                 : (空=已卸载)
临时目录               : No such file or directory
残留进程               : 0
```

---

## 附录 A：关键常量与 ABI 事实（本机实测，换机型需重查）

| 项 | 值 |
|---|---|
| 物理屏 DisplayId（uint64） | `1111111111111111111`（`dumpsys SurfaceFlinger --display-id` 现查） |
| 图层类型 `eFXSurfaceEffect` | `0x00020000` |
| Android 16 Parcel 对象头 | **24 字节魔数结构**：FD=`0x66642A85`，binder=`0x73682A85` —— **搜魔数定位，不要数偏移** |
| `IBinder` 的 RefBase 虚基类偏移 | **144** |
| `RefBase::incStrong` mangled name | `_ZNK7android7RefBase9incStrongEPKv`（**注意 `K`**） |
| `InputMessage` 类型码 | `KEY=0, MOTION=1, FINISHED=2, FOCUS=3` |
| 本机 density | `3.0` |
| `screencap -d` 参数 | 收 **DisplayId（uint64）**，不是逻辑编号 |

---

## 附录 B：复现步骤

### B.1 构建（PC）

```powershell
$clang = "<NDK>/toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"

# 玻璃渲染探针
& $clang --target=aarch64-linux-android28 -std=c++20 -O2 -ffp-contract=off -Wall -Wextra `
  -I <repo>/build/recon -o probe_m3 <repo>/build/recon/probe_m3_ytbl.cpp `
  -lEGL -lGLESv3 -llog -ldl

# 输入法辅助包 + 内嵌头
cd <repo>/ime-bridge
pwsh -File build-ime-apk.ps1
pwsh -File build-embedded-apk.ps1

# 输入法探针
& $clang --target=aarch64-linux-android28 -std=c++20 -O2 -ffp-contract=off -Wall -Wextra `
  -I <repo>/app/src/main/cpp -I <repo>/ime-bridge/build `
  -o ime_probe <repo>/ime-bridge/probe/ime_probe.cpp -llog -ldl
```

### B.2 部署（HTTP，注意避开被占用的端口）

```powershell
# Windows 上 SO_REUSEADDR 语义允许新进程「抢绑」已被 stale 进程占用的端口，
# 表现是新服务器日志正常但连接全被投递给那个半死的旧进程。先确认端口空闲：
Get-NetTCPConnection -State Listen | Where-Object { $_.LocalPort -eq 9101 }
cd <repo>/build/recon ; python -m http.server 9101 --bind 0.0.0.0
```

```bash
# 手机侧（Android shell，root）
cd /data/local/tmp
curl -sS -o ime_probe -w "http=%{http_code} size=%{size_download}\n" http://<PC_IP>:9101/ime_probe
chmod 755 ime_probe
sha256sum ime_probe      # 必须与 PC 侧 Get-FileHash 一致，否则别往下走
```

### B.3 拉起输入法

```bash
# 安全核对模式（虚拟屏；键盘永远不会显示，但窗口/焦点/清理都能验）
setsid ./ime_probe --display <虚拟屏id> --dump --hold-window 2000 > log 2>&1 &

# ★ 真正拉起系统输入法（物理屏；键盘约 N 秒后自动收起）
setsid ./ime_probe --display 0 --show --show-ms 4000 --dump --yes-physical > log 2>&1 &
```

**判据**：`mCurClient` 的 `mUid` == 探针打印的「辅助包 uid」，
且 `mInputShown=true` / `mImeWindowVis=3` / `focusedWindowName=aurora-ime-bridge`。

---

## 附录 C：安全边界与工程纪律

在**别人的日常主力机**上做系统级实验，以下几条是用真实事故换来的：

1. **绝不无必要地写 `/sys/class/input/event*/device/inhibited`。**
   曾用它在内核层做触摸抑制测试，用户恰好在那 12 秒里想滑屏，**整个触摸屏无响应**。
   该方案已废弃为主路线（它做不到「按窗口范围拦截」，而这正是需求）。
2. **上真机前先算内存足迹。** 任何「每帧处理整屏 10 MB」的实现都可能把系统推入 swap thrashing，
   表现为「整机卡死」，而现场证据（`Input list`、`inhibited`、图层残留）会**全部正常**，极难归因。
3. **物理屏实验必须先取得同意**，并在程序里做**安全闸门**（不带显式确认参数直接拒绝运行）
   + **看门狗**（超时强制收起）。
4. **不要 `kill -9`** 打断程序——清理逻辑跑不到会留下残留；用 `SIGTERM`。
5. **每次实验前后都核查**：`inhibited` 全 0、无本程序图层残留、无残留进程、无残留已安装包、临时目录已删。
6. **区分「真机已验证」与「离线推断」**，逐条标注。本项目有 5 个曾被文档写死、
   后来被源码核实或真机实测推翻的假设——**文档的结论也会错，要能重新验证**。

---

## 附录 D：已知未完成项（如实列出）

| 项 | 状态 |
|---|---|
| 输入法**文本往返**（中文上屏、选区同步、UTF-8↔UTF-16 偏移换算） | 协议与换算只有离线实现，**未跑过真机中文上屏** |
| `InputShield` 与输入法联动（键盘区域触摸应交给 Android） | 未接线 |
| 采集端内存足迹优化（面板区域采集 / 缓冲复用 / `mirrorDisplay`） | 已定位，未实现 |
| `removeInputChannel` 在退出时 `native_crash` | 已停用（进程退出时 SF 会自行回收），未查清 |
| 冷启动耗时统计 | 实测 1.5~2.5 s，未做多次统计 |
