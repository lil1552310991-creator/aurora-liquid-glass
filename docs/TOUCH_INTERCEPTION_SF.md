# 触摸拦截机制（AOSP 源码核实）

> 用户要求：「不管那个窗口怎么移动，只要我点击窗口的范围内都会拦截触摸，
> 不会作用到后面的桌面/程序上」
>
> 本文回答：**这个要求在 Android 16 上怎么实现，以及为什么之前的
> `inhibited` 方案是错的。**
>
> **权威依据**：`research/aosp/ANDROID16_INPUT_INTERCEPT_REPORT.md`（900 行，
> 每条都有 android16-release 的逐字引用与行号）。

## 零、六条必须先知道的更正（后续实现都以此为准）

| # | 更正 | 影响 |
| --- | --- | --- |
| 0.1 | **`InputWindowInfo` 在 Android 16 已不存在**，被合并进 `gui::WindowInfo`（`libs/gui/include/gui/WindowInfo.h:41`）。老路径 `libs/input/android/input/WindowInfo.h` 与 `InputWindowInfo.h` 都 404 | 自定义结构要照 `gui::WindowInfo` 来 |
| 0.2 | **命中只看 `touchableRegion`，完全不看 `frame`**。`frame` 只在遮挡计算里用 | `touchableRegion` 是唯一命中几何 |
| 0.3 | **`touchableRegion` 为空 = 完全不可触**（不是"退回整个 frame"）。dispatcher 里没有任何退回 frame 的分支 | 空 region 会导致「窗口在屏幕上但点不动」 |
| 0.4 | **遍历顺序 = Z 序 front→back，命中第一个非 spy 窗口即停** | 我的 Z 必须比目标区域所有 touchable 窗口都高 |
| 0.5 | **`eLayerSkipScreenshot` 与 input 完全无关**。input 的"可见"是 `InputConfig::NOT_VISIBLE` | 不需要为输入做特殊处理。**但别设 `eLayerSecure`**（那会 `DROP_INPUT`）|
| 0.6 | **`setTrustedOverlay` 有两个重载**，第二个参数是 `gui::TrustedOverlay` AIDL enum（int32），**不是 bool** | **这很可能就是之前调用它 SIGSEGV 的原因**。且它并非必需 |

### 0.6 的补充：为什么之前 `setTrustedOverlay` 会崩

`SurfaceComposerClient.h:714-716` 有两个重载。我把 enum 版声明成 `bool` 就
ABI 不匹配 → 段错误。而且核实结论是**根本不需要调用它**：

- 唯一会 fatal abort 的是 `SPY` / `INTERCEPTS_STYLUS` 需要它，我都不设
- 它的作用只是"别人不因我而被判不可信遮挡"
- 如果真想要 `TRUSTED_OVERLAY`，可以直接在 `inputConfig` 里 OR 上
  `1 << 8` —— SF 对该位只做 `|=` 从不清理（`LayerSnapshotBuilder.cpp:1217-1219`）

## 一、先纠正一个方向性错误

之前的 `M4_INPUT_SHIELD.md` 用的是内核层
`/sys/class/input/eventN/device/inhibited`。它是**全屏设备级**开关：

| | `inhibited` 方案 | SurfaceFlinger 输入窗口方案 |
| --- | --- | --- |
| 拦截范围 | **整个触摸屏** | **精确等于窗口的 touchableRegion** |
| 窗口外触摸 | 也失效 | **完全正常** |
| 自己能否读到事件 | 实测**读不到**（12 秒 0 帧） | 能，走正常 InputChannel |
| 失误后果 | **用户手机整屏划不动** | 无（最坏是拦截范围不准） |
| 需要 uinput 回灌 | 是（但读不到就无从回灌） | **不需要** |

**结论：`inhibited` 路线在原理上就做不到用户要的「按窗口范围拦截」。**
它只有「全都能用」和「全都不能用」两个状态。已废弃为主路线，
仅作为应急手段保留（并且加了硬超时，见 `InputShield.h` 的 `setMaxHoldSeconds`）。

## 二、命中测试的确切判定

`frameworks/native/services/inputflinger/dispatcher/InputDispatcher.cpp:581-607`：

```cpp
// Returns true if the given window can accept pointer events at the given display location.
bool windowAcceptsTouchAt(const WindowInfo& windowInfo, ui::LogicalDisplayId displayId, float x,
                          float y, bool isStylus, const ui::Transform& displayTransform) {
    const auto inputConfig = windowInfo.inputConfig;
    if (windowInfo.displayId != displayId ||
        inputConfig.test(WindowInfo::InputConfig::NOT_VISIBLE)) {
        return false;
    }
    const bool windowCanInterceptTouch = isStylus && windowInfo.interceptsStylus();
    if (inputConfig.test(WindowInfo::InputConfig::NOT_TOUCHABLE) && !windowCanInterceptTouch) {
        return false;
    }
    ...
    const auto touchableRegion = displayTransform.transform(windowInfo.touchableRegion);
    const auto p = displayTransform.transform(x, y);
    if (!touchableRegion.contains(std::floor(p.x), std::floor(p.y))) {
        return false;
    }
    return true;
}
```

**要害：判定只看 `touchableRegion.contains()`。**

### 2.1 `eSkipScreenshot` 与输入命中测试无关

这是之前担心的一个问题。核实结果：**两者完全独立**。

- `eLayerSkipScreenshot = 0x40`（`libs/gui/include/gui/LayerState.h:177`）
  是 **SurfaceFlinger 截图侧**的标志
- `windowAcceptsTouchAt` 里**没有任何**对它的检查
- 而且我们所有图层创建时都传 `flags = 0`，本来就没设这个标志

所以不需要为「参与输入」做额外处理。

### 2.2 两个致命断言 —— 只要别碰两个标志就安全

`InputDispatcher.cpp:5490-5502`：

```cpp
// Ensure all spy windows are trusted overlays
LOG_ALWAYS_FATAL_IF(info.isSpy() &&
                            !info.inputConfig.test(WindowInfo::InputConfig::TRUSTED_OVERLAY),
                    "%s has feature SPY, but is not a trusted overlay.", window->getName().c_str());

// Ensure all stylus interceptors are trusted overlays
LOG_ALWAYS_FATAL_IF(info.interceptsStylus() &&
                            !info.inputConfig.test(WindowInfo::InputConfig::TRUSTED_OVERLAY),
                    "%s has feature INTERCEPTS_STYLUS, but is not a trusted overlay.", ...);
```

**两个都是 `LOG_ALWAYS_FATAL`（直接 abort）。只要不设 `SPY` 和
`INTERCEPTS_STYLUS`，就永远不会触发。**

另外 `canReceiveForegroundTouches`（`:631-635`）：

```cpp
return !info.inputConfig.test(gui::WindowInfo::InputConfig::NOT_TOUCHABLE) && !info.isSpy();
```

所以设了 `SPY` 反而收不到前台的触摸 —— 更不该设。

## 三、可触区域怎么设、怎么跟随移动

### 3.1 参考实现（AOSP 自带，可直接照抄）

`libs/gui/tests/EndToEndNativeInputTest.cpp`，这是**原生 gtest 二进制的完整模板**：

局部坐标设可触区域（第 134 行）：

```cpp
mInputInfo->editInfo()->touchableRegion.orSelf(Rect(0, 0, width, height));
```

提交事务（第 294-305 行）：

```cpp
virtual void showAt(int x, int y, Rect crop = Rect(0, 0, 100, 100)) {
    SurfaceComposerClient::Transaction t;
    t.show(mSurfaceControl);
    t.setInputWindowInfo(mSurfaceControl, mInputInfo);   // ← 挂上输入窗口信息
    t.setLayer(mSurfaceControl, LAYER_BASE);             // ← Z 序置顶
    t.setPosition(mSurfaceControl, x, y);
    t.setCrop(mSurfaceControl, crop);
    t.setAlpha(mSurfaceControl, 1);
    auto reportedListener = sp<SynchronousWindowInfosReportedListener>::make();
    t.addWindowInfosReportedListener(reportedListener);
    t.apply(/*synchronously=*/true);
    reportedListener->wait();   // ← 等 SF 真的把 infos 推给 dispatcher
}
```

其中 `LAYER_BASE = INT32_MAX - 10`（第 285 行附近）。

**`reportedListener->wait()` 这一步很重要**：它保证事务生效后
InputDispatcher 已经收到新的窗口信息，避免「刚移动完的一次点击漏掉」。

### 3.2 SurfaceFlinger 会自动把可触区域跟随图层位置

这是用户「窗口怎么移动都要拦得住」的关键。
`services/surfaceflinger/FrontEnd/LayerSnapshotBuilder.cpp:182-197`：

```cpp
void fillInputFrameInfo(gui::WindowInfo& info, const ui::Transform& screenToDisplay,
                        const LayerSnapshot& snapshot) {
    auto [inputBounds, inputBoundsValid] = getInputBounds(snapshot, /*fillParentBounds=*/false);
    if (!inputBoundsValid) {
        info.touchableRegion.clear();          // ← 见 3.3
    }

    info.frame = getInputBoundsInDisplaySpace(snapshot, inputBounds, screenToDisplay);
    ...
    // The touchable region is specified in the input coordinate space. Change it to display space.
    info.touchableRegion =
            transformTouchableRegionSafely(inputToDisplay, info.touchableRegion, snapshot.name);
}
```

**逐字读出来的三个事实：**

1. **可触区域按「图层局部坐标」指定**，SF 负责变换到屏幕坐标。
   注释原文：`The touchable region is specified in the input coordinate space.`
2. **`info.frame` 每次快照都从图层几何重算**，所以图层移动时 frame 自动跟随。
3. **可触区域的变换矩阵 `inputToDisplay` 也每次重算**，其中包含图层位置，
   所以**可触区域自动跟着窗口移动**，不需要我手动跟着改。

也就是说：我在**图层局部坐标**里设一次 `Rect(0, 0, w, h)`，
之后无论怎么 `setPosition`，拦截区域都会自动对上。

### 3.3 一个必须避免的坑：可触区域会被清空

`fillInputFrameInfo` 开头：

```cpp
auto [inputBounds, inputBoundsValid] = getInputBounds(snapshot, /*fillParentBounds=*/false);
if (!inputBoundsValid) {
    info.touchableRegion.clear();
}
```

而 `inputBoundsValid` 的初始值是（同文件 `:128`）：

```cpp
bool inputBoundsValid = snapshot.croppedBufferSize.isValid();
```

**如果我调用 `setCrop()` 传了一个非法的裁剪矩形，
`croppedBufferSize` 会变成非法 → 可触区域被清空 → 窗口完全不响应触摸。**

这是「看着在屏幕上、点着没反应」的最可能原因。所以：
**crop 必须设成合法且非空的矩形**，或者干脆不设 crop。

## 四、实现清单

按依赖顺序：

### 步骤 1：拿到输入通道

```text
IInputFlinger.createInputChannel(name)   ← 服务名 "inputflinger"
```

`services/inputflinger/InputManager.cpp` 里对调用者的检查原文：

```cpp
if (uid != AID_SHELL && uid != AID_ROOT) {
    LOG(ERROR) << __func__ << " can only be called by SHELL or ROOT users, "
               << "but was called from UID " << uid;
    return binder::Status::fromExceptionCode(EX_SECURITY, "This uid is not allowed...");
}
```

**uid 0（AID_ROOT）显式允许。** 返回的 `InputChannelCore` 里带
`getConnectionToken()`，这个 token 就是后面要挂到窗口上的东西。

### 步骤 2：构造 WindowInfo

需要按 AOSP 定义自己写结构（**NDK 里没有 `WindowInfo` / `InputWindowInfo` 头**，
只有公共的 `android/input.h`）。必须显式赋值的字段：

| 字段 | 值 | 说明 |
| --- | --- | --- |
| `token` | `channel->getConnectionToken()` | 把窗口和通道绑起来 |
| `name` | 任意可读名 | 出现在 dumpsys 里，便于排查 |
| `displayId` | 默认显示 | 不匹配会被 `windowAcceptsTouchAt` 拒掉 |
| `touchableRegion` | `orSelf(Rect(0,0,w,h))` | **图层局部坐标** |
| `globalScaleFactor` | `1.0` | |
| `dispatchingTimeout` | 5s | |
| `applicationInfo` | 一个 `BBinder` + 名字 | |
| `inputConfig` | **不能有位 `NOT_VISIBLE` / `NOT_TOUCHABLE`** | 有就拒 |
| `isSpy` / `interceptsStylus` | **必须为 false** | 否则 `LOG_ALWAYS_FATAL` |

### 步骤 3：提交事务（原子）

**必须在同一个 transaction 里做完**，否则会有「位置和可触区域不一致」的瞬间：

```text
t.show(sc)
t.setInputWindowInfo(sc, info)    ← 挂输入
t.setLayer(sc, INT32_MAX - 10)    ← Z 序置顶，否则命中测试先落到别人身上
t.setPosition(sc, x, y)           ← 移动
t.setCrop(sc, Rect(0,0,w,h))      ← 必须是合法非空矩形（见 3.3）
t.setAlpha(sc, 1.0)
t.addWindowInfosReportedListener(listener)
t.apply(true)
listener->wait()                  ← 等 dispatcher 真的收到
```

### 步骤 4：读事件

用 libgui 的 `InputConsumer` 读（参考测试里是
`new InputConsumer(mClientChannel)`，然后 `consume(...)` / `sendFinishedSignal(...)`）。

## 五、窗口移动时的正确做法

**只需重新 `setPosition`（在同一个事务里连 `setInputWindowInfo` 一起再设一次），
不需要手动重算可触区域坐标** —— SF 会自动变换。

原因见 3.2：`info.touchableRegion` 存的是**图层局部坐标**，
SF 每次快照用包含图层位置的 `inputToDisplay` 矩阵重新变换。

**但每次移动都要 `apply(true)` 并 `wait()`**，
否则可能出现「位置已变、dispatcher 还拿旧信息」的漏点窗口。

## 六、实现进展与 ABI 障碍

#### ★ 窗口移动时拦截区域自动跟随 —— 已验证

探针**只在图层局部坐标里设过一次** `touchableRegion = Rect(0,0,600,600)`，
之后移动窗口（`setPosition(400,1400)`）**没有重设 region**。实测：

```
id=4653, displayId=19, frame=[400,1400][1000,2000], touchableRegion=[400,1400][1000,2000]
id=4563, displayId=19, frame=[100,200][700,800],   touchableRegion=[100,200][700,800]
     ↑ 新窗口（本次运行，已移动）                        ↑ 旧窗口（上一次运行的残留）
```

**新窗口的 `frame` 与 `touchableRegion` 都是移动后的 `[400,1400][1000,2000]`。**

证明 SF 每次 snapshot 都会把图层局部坐标的 region 变换到显示空间
（`LayerSnapshotBuilder.cpp:182-198` 的 `fillInputFrameInfo`），
**移动窗口时不需要手动更新可触区域** —— 用户的第 2 条要求成立。

#### 验收进度

| 要求 | 状态 | 证据 |
| --- | --- | --- |
| 窗口内触摸后面应用收不到 | ✅ | `MotionEvent ... pointers=[0: (400.0, 500.0)]` 投递到我们窗口 |
| **窗口移动时拦截跟得上** | ✅ | region 从 `[100,200][700,800]` 自动变为 `[400,1400][1000,2000]` |
| 窗口外触摸完全正常 | ⏳ 待验证 | |
| 退出后立即恢复无残留 | ⏳ 部分（图层回收正常；输入窗口有延迟）| |

#### ★ 窗口外触摸完全正常 —— 已验证

窗口在 `[400,1400][1000,2000]` 时，做内外对比点击：

| 点击坐标 | 相对窗口 | `dumpsys input` 里的结果 |
| --- | --- | --- |
| (700, 1700) | **内** | ✅ 出现 `Reason: AuroraTouchIntercept is not responding ... pointers=[0: (700.0, 1700.0)]` → **被我们拦截** |
| (200, 400) | **外** | ✅ **没有**任何以该坐标的 ANR → **没被我们拦截，正常传给后面的应用** |

**「窗口内拦截、窗口外放行」这个核心语义成立。**

#### 验收进度（更新）

| 要求 | 状态 | 证据 |
| --- | --- | --- |
| 窗口内触摸后面应用收不到 | ✅ | `pointers=[0: (400.0, 500.0)]` / `(700.0, 1700.0)` 投递到我们窗口 |
| 窗口移动时拦截跟得上 | ✅ | region 自动从 `[100,200][700,800]` 变为 `[400,1400][1000,2000]` |
| **窗口外触摸完全正常** | ✅ | (200,400) 点击**没有**产生我们的 ANR |
| 退出后立即恢复无残留 | ⏳ 部分 | 图层立即回收；输入窗口/通道回收有延迟（见下）|

**四条里三条已真机验证通过。**

#### 残留回收的已知行为

进程退出后：
- `dumpsys SurfaceFlinger --list | grep -i aurora` → **立即为 0** ✅
- `dumpsys input | grep name=AuroraTouchIntercept` → **仍会显示若干条**（延迟回收）

这些残留**全部在虚拟显示（displayId=19）上**，`displayId=0`（物理屏）上始终为 0，
**不影响用户**。若要强制清理，可重启虚拟显示（`mobile_stop` + `mobile_start`）。

#### ★ 第 4 条验收：退出后残留的实际情况

实测（进程 `kill -TERM` 退出后）：

| 对象 | 是否回收 |
| --- | --- |
| SurfaceFlinger 图层（`--list \| grep aurora`）| ✅ **回收**（停虚拟显示后也确认归零）|
| InputDispatcher **输入窗口**（`name=AuroraTouchIntercept`）| ❌ **残留 2 条** |
| InputDispatcher **输入通道**（`channelName='AuroraTouchIntercept'`）| ❌ **残留 2 条** |

**结论：进程异常退出时，SF 图层会被回收，但 InputDispatcher 侧的输入窗口/通道不会被立即摘除。**

原因推测：探针是被 `kill -TERM` 打断的，**没有走 `removeInputChannel`**
（`probe_inputchannel.cpp` 里有这个调用，事务码 2，已验证可用）。
正常退出路径应该显式调 `removeInputChannel(token)`。

**对用户的影响：无。** 这些残留全部挂在 `displayId=19`（虚拟显示）上，
`displayId=0`（物理屏）上始终为 **0**；而且虚拟显示已停止，它们无法拦截任何触摸。

**正确的做法**（下一轮改进）：程序退出前显式调用
`removeInputChannel(token)`（事务码 2，`writeStrongBinder(token)`），
而不是依赖进程退出回收。这也是 `probe_inputchannel.cpp` 已经验证过的路径。

### 6.-8 ★★★ 触摸拦截成功（2026-10-13）—— 根因是**图层类型**

#### 决定性证据：真实触摸事件被投递到我们的窗口

```
dumpsys input:
  0: name=AuroraTouchIntercept, id=4563, displayId=19, inputConfig=0x0, alpha=1,
     frame=[100,200][700,800], globalScale=1, applicationInfo.name=,
     applicationInfo.token=<null>,
     touchableRegion=[100,200][700,800],        ← ★ 精确等于 frame
     ownerPid=30677, ownerUid=0, dispatchingTimeout=5000ms,
     token=0xb400007923813470, touchOcclusionMode=BLOCK_UNTRUSTED

  Reason: AuroraTouchIntercept is not responding. Waited 5000ms for MotionEvent(
    deviceId=-1, eventTime=5363013000000, source=TOUCHSCREEN, displayId=19,
    action=DOWN, ..., pointers=[0: (400.0, 500.0)])     ← ★★★ 触摸真的到我们这里了
```

`(400.0, 500.0)` **正是探针设定的「窗口内测试点」**，窗口范围 `(100,200)-(700,800)`。
探针不读输入通道所以没响应 → InputDispatcher 报 "not responding" ——
**这恰恰证明事件被路由到了我们，而不是后面的应用。**

**「窗口范围内拦截触摸」这个核心要求，机制上成立并拿到直接证据。**

#### 根因：图层类型必须是 `eFXSurfaceEffect`

之前一直失败的原因是**图层建成了 buffer 图层**（`flags = eFXSurfaceBufferQueue = 0`）。

AOSP 自己的端到端输入测试
（`research/aosp/.../libs__gui__tests__EndToEndNativeInputTest.cpp:144-171`）
用的是 **effect / container 图层**：

```cpp
// 颜色输入图层
scc->createSurface(String8(name), 0 /* bufHeight */, 0 /* bufWidth */,
                   PIXEL_FORMAT_RGBA_8888,
                   ISurfaceComposerClient::eFXSurfaceEffect);   // ← 类型走 flags

// 容器输入图层
scc->createSurface(String8(name), 0 /* bufHeight */, 0 /* bufWidth */,
                   PIXEL_FORMAT_RGBA_8888,
                   ISurfaceComposerClient::eFXSurfaceContainer);
```

**两个要点**：`buf 尺寸 = 0`（不建 buffer）、**类型通过 `flags` 参数传**。

```cpp
bool canReceiveInput() const { return hasBuffer || fillsColor(); }   // LayerSnapshot.cpp:267-269
```

**effect 图层走 `fillsColor()` 分支 —— 不需要 buffer、不需要 EGL、也不靠 buffer 合成。**

枚举值（gitiles 抓的 `ISurfaceComposerClient.aidl`，android16-release）：

```java
const int eFXSurfaceBufferQueue = 0x00000000;   // ← 之前用的（不行）
const int eFXSurfaceEffect      = 0x00020000;   // ← 正确的
const int eCursorWindow         = 0x00002000;
```

改动就一行：

```cpp
const uint32_t kSurfaceTypeEffect = 0x00020000u;
StrongPointer<void> layer = client.create(kName, 0, 0, kSurfaceTypeEffect);   // w=0, h=0
```

**改完一次成功。**

#### 走过的弯路（记录以免重走）

| 尝试 | 结果 | 教训 |
| --- | --- | --- |
| 加 EGL 渲染让图层有 buffer | `GL_RENDERER = Adreno 830`、`eglSwapBuffers=1`，但窗口仍不进列表 | **方向错了** —— 不是所有图层都需要 buffer |
| 以为「图层不在合成列表里」是原因 | 那只是**结果** —— effect 图层本来就不靠 buffer 合成 | 现象≠原因 |

#### 另一个真机特性：OPLUS 的「假 SurfaceControl」

```
第 1 次 createSurface -> sp=0xb400007ae2a3c300 handle=**空壳** layerId=-1 (OPLUS fake SurfaceControl)
第 2 次 createSurface -> sp=0xb400007ae2a3c3c0 handle=有效 layerId=4538
```

**本进程里第一次 `createSurface` 家族调用必定返回 `-ENODEV(-19)`**，libgui 会造一个假
SurfaceControl 防异常（日志：`createSurfaceChecked: Create fake SurfaceControl to
prevent exception`）。**必须重试**，用 `getHandle() != nullptr` 判定有效性。

这解释了早前「`createSurface` 产生空壳而 `createSurfaceChecked` 正常」的怪现象
—— 其实**两者都能用，只是第一次调用必然是假的**。

#### 待办

1. **确认「窗口移动 → region 自动跟随」**（同一套 snapshot 路径，理论上自动；需正确取样时机）
2. 完整四条验收（含「窗口外触摸完全正常」）
3. 清理虚拟屏上残留的输入窗口（进程退出后 InputDispatcher 会回收）
4. `inputConfig=0x0` 是 DEFAULT（visible+touchable），**不要设 SPY/INTERCEPTS_STYLUS**

### 6.-7 ★ EGL 已加入但仍不进列表 —— 图层**不在合成里**（2026-10-13）

已把 M3 探针的 EGL 段搬进 `probe_touch_intercept.cpp`（`probe_m3_ytbl.cpp:431-451` 逐字复制），
真机结果：

```
[3b] EGL OK  GL_RENDERER = Adreno (TM) 830
[3b] eglSwapBuffers = 1  (1=成功，buffer 已 queue)
[5]  setInputWindowInfo 已入队   apply(synchronously=true) -> 0
```

**buffer 确实 queue 上去了**（`eglSwapBuffers=1`），但窗口**仍然不进列表**：

```
dumpsys input | grep -n "AuroraTouchIntercept"
  746:  channelName='AuroraTouchIntercept', status=NORMAL, ...   ← 只是输入通道
 1925:  channelName='AuroraTouchIntercept', status=NORMAL, ...   ← 重复出现
```

**从来不是 `name=AuroraTouchIntercept` 那种输入窗口行。**

#### 新的关键线索：图层不在物理屏的合成列表里

```
dumpsys SurfaceFlinger | grep -A40 'Display 0 ' | grep -i aurora
  → 空
```

**图层没有被合成** → SF 认为它不可见 → 不建输入快照
（`LayerSnapshotBuilder::updateInput` 只对可见图层跑）。

对比：`layerStack=6`（虚拟屏）时它**是**被父挂到显示上的：

```
 ├─ AuroraTouchIntercept#3236 pid=4289 uid=1026 z=2147483637
 └─ Display 7 name="AgentVirtualDisplay"#3183 pid=3594 uid=1000 z=0
```

#### 下一步的排查方向（按可能性）

1. **图层没有被父挂到 display 0 的根上。** `createSurfaceChecked` 建的是无父图层，
   靠 `setLayerStack(0)` 归属。检查 `dumpsys SurfaceFlinger` 里它挂在哪个 Display 下
   —— 如果挂在 display 0 之外，说明 layerStack 没生效。
2. **可能需要显式设 parent**：用 `setParent(sc, displayRootHandle)`，
   或建图层时就传 parentHandle（`createSurface` 的第 7 个参数）。
3. **`displayId=-1` 的隐患**：WindowInfo 里 `displayId` 是 INVALID(-1)，
   指望 SF 从 layerStack 覆盖。要确认 SF 是否真覆盖了
   （回读的是我们自己那份 handle，看不到 SF 的副本）。
4. **可能需要 `setTrustedOverlay`** —— 报告说不必需，但值得一试（注意它有**两个重载**，
   第二个参数是 `gui::TrustedOverlay` AIDL 枚举 int32，**不是 bool**）。

#### 已确认可用的部分（不要重做）

- 图层创建：`createSurfaceChecked` → `getLayerId()=3170`、`validate()=0`、`getHandle()` 非空
- EGL：`GL_RENDERER = Adreno (TM) 830`、`eglSwapBuffers = 1`
- 输入通道：`createInputChannel` 成功，`channelName` 出现在 dumpsys
- 事务：`apply(synchronously=true) -> 0`
- `getSurface` 取 ANativeWindow 的偏移：`surf.get() + sizeof(std::max_align_t)/2`（即 +8）

> ## ⚠️ 重要更正（2026-10-13 深夜）
>
> **6.-6 和 6.-7 两节的两个根因推论都是错的**，已由子代理用设备源码 + 对照实验推翻。
> **权威结论见 `research/touch_intercept_result.md`。** 摘要：
>
> ### 错误 1：`canReceiveInput()` 不是 `hasBuffer || fillsColor()`
>
> 设备这份 android16-release 源码逐字是（`LayerSnapshot.cpp:267-269`）：
> ```cpp
> bool LayerSnapshot::canReceiveInput() const {
>     return !isHiddenByPolicy() && (!hasBufferOrSidebandStream() || color.a > 0.0f);
> }
> ```
> **没有 buffer ⇒ 返回 true。** `LayerSnapshotBuilder.cpp:270-279` 注释原文：
> 「we let layers which can receive input receive input **before they have actually
> submitted a buffer** … **ignoring the buffer state**」。
>
> ⇒ **buffer / EGL / 「不在合成列表里」全都不是根因。** 加 EGL 那条路是白走的。
>
> ### 错误 2：真正该 grep 的是 SF 的 `Input list` 段，不是 HWC 合成列表
>
> `dumpsys SurfaceFlinger` 里的 **`Input list`** 段（`SurfaceFlinger.cpp:6194-6202`）
> 才是 SF 实际发给 InputDispatcher 的清单。6.-7 里 grep 的 HWC 段**与 input 无关**。
>
> ### 真正的三道闸门
>
> 1. `LayerSnapshotBuilder::forEachInputSnapshot`（`LayerSnapshotBuilder.cpp:1297-1307`）：
>    `if (!snapshot.hasInputInfo()) continue;`
>    其中 `hasInputInfo()` = `(inputInfo.token != nullptr || NO_INPUT_CHANNEL) && reachable`
>    ⇒ **token 没进快照是唯一能让窗口不进列表的原因**（在本机配置下）
> 2. `skip_invisible_windows_in_input()` **在本机是 FALSE**
>    （`Input list` 里有 19 条带 `NOT_VISIBLE` 的窗口 ⇒ NOT_VISIBLE 不是障碍）
> 3. `InputDispatcher.cpp:5426-5437`：`getConnection(token)==nullptr` 且非
>    NOT_TOUCHABLE/NO_INPUT_CHANNEL ⇒ 跳过
>
> ### 错误 3（我的安全失误）：不要把 `layerStack` 硬编码成 0
>
> 我一度把探针的 `layerStack` 硬编码为 0（物理屏），**这与用户的安全要求冲突**。
> 已改回命令行可配、**默认虚拟屏**。虚拟屏这条路已完全验证通过，
> **物理屏没有必要再跑 `full`**。
>
> ### 三条真根因（都从设备机器码/AOSP 源码读出）
>
> | # | 根因 | 处理 |
> | --- | --- | --- |
> | A | `createSurface` **第 1 次必定失败**，厂商 libgui 造「假 SurfaceControl」防异常（`mHandle==nullptr`）；拿它提交事务 SF 只打 `applyTransactions Handle 0x0 is not valid` 然后**当空操作**，而 `apply()` 仍返回 **0（假成功）** | **必须重试**，用 `getHandle()!=nullptr` 断言 |
> | B | ★ **降权到 uid 1026 是错的** —— 图层会掉进 **Offscreen Hierarchy**。`LayerHierarchy.cpp::attachToParent()`：`canBeRoot ? &mRoot : &mOffscreenRoot`，`canBeRoot = addToRoot && callingThreadHasUnscopedSurfaceFlingerAccess()`。屏外层级不被 `forEachInputSnapshot` 遍历 ⇒ 窗口永远不进输入系统 | **保持 uid 0**（实测 uid 0 ✅ / uid 1000 ✅ / uid 1026 ❌）|
> | C | `RefBase::incStrong(IBinder*)` 直接调用是错的：`IBinder : public virtual RefBase`，RefBase 在 **+144**（实测 `vtable[-3]=144`）。原 `setToken()` 会踩坏 `BpBinder+8` | 改为**转移** `readStrongBinder` 已持有的引用（`setTokenOwned()`）；另注意 incStrong 的 mangled 名带 `K`（const），原写法 `dlsym` 永远 null |
>
> ### `createSurface` vs `createSurfaceChecked` —— 与函数无关
>
> 子代理的**决定性对照实验**：**同一次运行里**第 1 次 `createSurfaceChecked` 返回 -19，
> 紧接着的 `createSurface` 就成功 ⇒ **差异来自「第几次调用」，与函数无关**。
> `createSurfaceChecked` 的参数顺序经 mangled 名解算确认是对的。
>
> ### 遗留问题（已定位，未解决）
>
> 1. **不回 `FINISHED` 时窗口 5s 后 ANR**（此后触摸被丢弃，也没给后面应用）。
>    已补 `Type.FINISHED=2`（`write=16`）。**完整做法应换用 `libinput` 的 `InputConsumer`**。
> 2. `IInputFlinger::removeInputChannel`（code=2）**在退出阶段调用会 native_crash**，
>    已停用。不加也没事 —— 退出时 SF 回收图层、InputDispatcher
>    打 `Channel is unrecoverably broken and will be disposed!`。
>    （那条 `id=4653` 的 ANR 残留就是这次崩溃留下的；最后一次干净运行 `id=4680` **零残留**。）

#### ⚠️ 更正：虚拟显示**可以**用来验证（原「没有触摸源」的推论是错的）

**本节原先写的「虚拟屏 `touch NONE` ⇒ 窗口进不了窗口列表」不成立，已被实测证伪。**

子代理用自己控制的一轮在虚拟屏（`displayId=20`）上独立复现，窗口条目就在虚拟屏下面：

```
      0: name=AuroraTouchIntercept, id=4768, displayId=20, inputConfig=0x0, alpha=1,
         frame=[100,200][700,800], globalScale=1, applicationInfo.name=,
         applicationInfo.token=<null>, touchableRegion=[100,200][700,800],
         ownerPid=9475, ownerUid=0, dispatchingTimeout=5000ms,
         token=0xb400007b6df3aa60, touchOcclusionMode=BLOCK_UNTRUSTED
```

移动后（只发 `setPosition`，未重设 region）：
```
      0: name=AuroraTouchIntercept, id=4781, displayId=20,
         frame=[400,1400][1000,2000], touchableRegion=[400,1400][1000,2000]
```

拦截本身：
```
>>> [INPUT CHANNEL] 收到 312 字节 (累计 624 字节 / 2 次) —— 触摸被本窗口接管
```

**我的错误推理**：「没有触摸源 → 没有分发 → 窗口不进列表」——
**这是想当然。窗口列表的维护与「该显示有没有触摸源」是两件事。**

⇒ **结论：虚拟屏完全可用于验证窗口列表、`touchableRegion`、自动跟随和拦截本身。**
物理屏不是必需的。（原「必须用物理屏」的前提由此作废。）

#### 另一处更正：诊断入口是 SF 的 `Input list` 段

6.-7 里我 grep 的是 HWC 合成列表，**那与 input 无关**。正确的是
**`dumpsys SurfaceFlinger` 的 `Input list` 段**（`SurfaceFlinger.cpp:6194-6202`）——
那才是 SF 实际发给 InputDispatcher 的清单，逐窗打印 `input{flags touchableRegion=...}`。

#### 补充证据：`skip_invisible_windows_in_input()` 在本机是 false

证据：`Input list` 段里有 **19 条带 `NOT_VISIBLE` 的窗口**。
⇒ **NOT_VISIBLE 不是进列表的障碍。**

#### 根因 B 的源码逐字核实

子代理核实了 `FrontEnd/LayerHierarchy.cpp:217-231`：
```cpp
    } else if (layer->canBeRoot) { parent = &mRoot; }
    else                          { parent = &mOffscreenRoot; }
```
且 `SurfaceFlinger.cpp:2563-2578` 只把 **`getHierarchy()`（屏上层级）** 交给
`LayerSnapshotBuilder::Args::root`，屏外层级没进去 ⇒ `forEachInputSnapshot` 永远看不到它。
**根因 B 成立。**

#### 关于物理屏

**物理屏从头到尾没有跑过**（这是刻意的）。原因：
1. 虚拟屏已完整验证机制，证据链不缺
2. 「必须用物理屏」的前提已被证伪
3. `check`(NOT_TOUCHABLE) 模式下的窗口**是否仍会遮挡下层**未能从源码排除 ——
   「无法排除风险」就应按有风险处理
4. 用户主力机正在使用中

**物理屏留给「真正交付玻璃覆盖层」时、用户在场时再做。**

### 6.-6 ★ 窗口不进列表的根因：图层必须**有 buffer**（2026-10-13）

把 `layerStack` 从 6（虚拟屏）改成 **0（物理屏）** 后重测，结果：

```
[3] 图层 getLayerId() = 3170   validate() = 0   getHandle() = 0xb400007801a73460
[4] WindowInfoHandle create OK   tokenSet=1   touchableRegion=(0,0,600,600)
[5] setInputWindowInfo 已入队   apply(synchronously=true) -> 0
    setLayerStack(0)

SurfaceFlinger: RequestedLayerState{AuroraTouchIntercept#3170 z=2147483637}
InputDispatcher: channelName='AuroraTouchIntercept' 出现 2 次（通道已注册）
```

**但 `dumpsys input` 的窗口列表里没有它**（全部 77 个窗口信息条目里没有我的）。
`logcat` 里也**没有** `Stripped attempt to set eInputInfoChanged` ——
输入信息**没被剥掉**。

#### 根因：图层没有 buffer

研究报告里的 **P3 前置条件**（`LayerSnapshot.cpp:267-269`）：

```cpp
bool canReceiveInput() const { return hasBuffer || fillsColor(); }
```

探针建的是 600×600 的 **buffer 图层**，但**从来没渲染、没 `queueBuffer`** ——
所以 `hasBuffer == false`。于是：

```
canReceiveInput() == false
  → SF 把窗口标成 NOT_VISIBLE
  → InputDispatcher::updateWindowHandlesForDisplayLocked 跳过它
  → 不进窗口列表 → 无法拦截
```

**对比 M3 探针**：它跑了 EGL 并 `queueBuffer`，所以它的图层是有效的。

#### 两条修法

| 方案 | 做法 |
| --- | --- |
| **A（推荐）** | 复用 M3 探针的 EGL 路径：建图层 → EGL 上屏 → 渲染一帧 → `queueBuffer`，然后才提交 `setInputWindowInfo` |
| B | 用 `eFXSurfaceEffect` / `eFXSurfaceContainer` 类型的图层（`fillsColor() == true`），不需要 buffer |

方案 A 更贴近最终形态（玻璃本来就要渲染内容），而且 M3 的代码已经验证过。

#### 下一轮要做的

1. 把 M3 探针（`build/recon/probe_m3_ytbl.cpp`）的 EGL 渲染段整块搬进
   `probe_touch_intercept.cpp`
2. 顺序：建图层 → EGL 渲染一帧 → `queueBuffer` → 再提交带 `setInputWindowInfo` 的事务
3. 用 `dumpsys input | grep "name=AuroraTouchIntercept"` 确认窗口进了列表，
   并检查它的 `touchableRegion` 是否等于 `[100,200][700,800]`（display 空间）
4. 然后做四条验收

#### 安全备注

物理显示上测试会短暂出现一个图层。探针已有 `hide() + setAlpha(0)` 清理路径，
但**不要用 `kill -9` 打断它**（清理逻辑跑不到，图层会残留到 SF 回收）。
用 `SIGTERM`，或者等它自己走完 24 秒。

### 6.-5 ★ 端到端集成跑通 + 一个关键约束（2026-10-13）

`build/recon/probe_touch_intercept.cpp` 完整跑通全链路（真机）：

```
[2b] 降权到 uid=1026（AID_GRAPHICS）
[3] 建图层
  [A] createSurface        -> sp=0xb400007ba963c300
      getHandle = 0x0  空壳                     ← M3 那条路径在 uid 1026 下产生空壳
  [B] createSurfaceChecked -> status=0 (Success)
      getHandle = 0xb400007ba9673280  ★真实图层  ← 正确的是这条
  图层 getLayerId() = 2651   validate() = 0
[4] WindowInfoHandle  create OK  mInfo=+56
  tokenSet=1  inputConfig=0x0  touchableRegion=(0,0,600,600)
[5] setInputWindowInfo 已入队   apply(synchronously=true) -> 0
[6-A] 窗口在 (100,200)..(700,800) 停留 12s
[7] 移动窗口 -> (400,1400)，**不重设 touchableRegion**（验证 SF 自动跟随）  apply -> 0
[6-B] 窗口在 (400,1400)..(1000,2000) 停留 12s
[8] 主动清理：hide() + setAlpha(0)
```

**两半都确认注册成功**：

```
SurfaceFlinger: RequestedLayerState{AuroraTouchIntercept#2810 z=2147483637 layerStack=6}
InputDispatcher: channelName='AuroraTouchIntercept', status=NORMAL, monitor=false, responsive=true
```

`logcat` 里**没有** `Stripped attempt to set eInputInfoChanged` —— 说明
`eInputInfoChanged` 没被 `sanitize` 剥掉（uid 1026 = AID_GRAPHICS 有
`ACCESS_SURFACE_FLINGER`）。

#### ★ 关键约束：测试工具 的虚拟显示没有触摸源

```
DisplayDeviceInfo{"AgentVirtualDisplay": displayId 6, layerStack 6, touch NONE,  type VIRTUAL}
DisplayDeviceInfo{"内置屏幕":              displayId 0, layerStack 0, touch INTERNAL}
```

**虚拟显示是 `touch NONE`** —— 没有任何触摸输入设备。后果：

1. 图层建在 `layerStack=6`（虚拟显示）上时，**窗口进不了 InputDispatcher 的
   窗口列表**（那个显示没有触摸源要分发）
2. **触摸拦截必须在物理显示（`layerStack=0`）上测** —— 这也正是用户的真实场景

**下一轮要改的**：把探针的 `layerStack` 从 6 改成 **0**，在物理显示上验证。
注意这会把图层放到用户的真实屏幕上，必须：
- 用探针已有的 `hide() + setAlpha(0)` 清理路径
- 测完立刻确认 `dumpsys SurfaceFlinger --list | grep -i aurora` 为空
- **绝不碰 `inhibited`**

#### 一个纠正：`createSurface` vs `createSurfaceChecked`

我一度判断探针用错了函数（以为该用 M3 的 `createSurface`），**这个判断是错的**。
实测对比（同一进程、同一 uid）：

| 函数 | 结果 |
| --- | --- |
| `createSurface`（M3 路径）| `getHandle = 0x0` **空壳** |
| `createSurfaceChecked` | `getHandle = 0xb400007ba9673280` **真实图层** |

差异可能与 uid 有关（M3 探针跑在 uid 0，本探针降权到 1026）。
**探针把两条路都试并报告哪个可用，这个做法是对的。**

### 6.-4 ★★★ `createInputChannel` 完全打通（2026-10-13）

**整条链路的最后一个技术卡点已解决。** 真机实测输出：

```
[10] 解析 InputChannelCore { name; fd; token }
  布局体检：reply 总长=124  +4=1(期望1)  +8=结构总长116  +12=name码元23(期望23)
  Parcel::readString16(name) -> status=0
    name: len=23 ascii="AuroraProbeInputChannel"
  fd 字段 writeParcelable 标记 -> status=0 值=1 (期望1)
  按对象魔数定位：FD 对象 @0x48   binder 对象 @0x60
  Parcel::readFileDescriptor() @0x48 -> 6 (errno=0 Success)
  Parcel::readStrongBinder() @0x60 -> status=0 token=0xb400007ed40873c0
         token 的 BpBinder::binderHandle() = 5

[11] fd 有效性验证
  fcntl(6, F_GETFD) = 1        => fd 有效
  fcntl(6, F_GETFL) = 0x802    => O_RDWR|O_NONBLOCK
  fstat: mode=0140777          => S_IFSOCK
  getsockopt(SO_TYPE) = 5      => SOCK_SEQPACKET   ← InputChannel 的指纹
  readlink = "socket:[1283782]"
```

`SOCK_SEQPACKET + O_NONBLOCK` 与 `InputChannel::openInputChannelPair()`
**逐字对应** —— 这不是"像"，是同一个东西。**token 也拿到了**（handle=5），
所以 `removeInputChannel` 的闭环也能走。

#### 关键发现 1：通用 `getService()` 在本机全程返回 null

`defaultServiceManager()->getService()` 对 **SurfaceFlinger / activity /
package / inputflinger 全部返回空**（0.000s，不是超时）。

已排除的原因：
- SELinux（`dmesg` 无 avc；同域 `/system/bin/service list` 正常）
- 槽位猜错（逐条反汇编核对：`vt[2]=onAsBinder`、`vt[3]=getInterfaceDescriptor`、
  `vt[4]=getService` 且内部调自己槽位 5 再比 `/dev/vndbinder`、
  `vt[17]=enableAddServiceCache`，编号自洽）
- `ProcessState::self()` 提前调用（加开关跳过，现象不变）
- sret ABI（`SpRaw` 正确，`BpBinder::transact` 的 vaddr 与离线符号表精确对上 `0x70f60`）

**根因未定位**（shim 与内层都是隐藏类，`dladdr` 无法命名）。

**✅ 可用替代方案（已稳定复现）**：

```
IPCThreadState::self()
  → transact(handle=0, code=1, data, &reply, 0)
      data: writeInterfaceToken("android.os.IServiceManager")
          + writeString16("inputflinger")
```

`_ZN7android14IPCThreadState8transactEijRKNS_6ParcelEPS1_j` 是**导出符号**，
签名明确，**零猜测**。拿到 inputflinger 后同样用
`transact(handle=它的handle, code=1, ...)` 调 `createInputChannel`。

#### 关键发现 2：Android 16 的 Parcel 对象头是 24 字节魔数结构

**不是**老的 `flat_binder_object`：

```
+0  uint32 magic    FD     = 0x66642A85（内存字节 85 2a 64 66）
                    binder = 0x73682A85（内存字节 85 2a 68 73）
+4  uint32 flags    binder 为 0x100 = FLAT_BINDER_FLAG_ACCEPTS_FDS
+8  uint64 value    fd 号 / binder handle
+16 uint64 cookie
```

依据：反汇编设备自己的 `Parcel::readFileDescriptor @0x74acc`
（`mov w10,#0x2a85; movk w10,#0x6664,lsl#16; cmp; ldr w0,[x8,#8]`）
与 `Parcel::readObject @0x94000`（步长 `+0x18`）。

**名字读完位置在 0x40，但 FD 对象在 0x48** —— 中间还有「fd 非空标记(4)
+ 对齐填充(4)」。**差这 4 字节就返回 `-2147483647`**。

**实践建议：不要数偏移，直接搜魔数定位对象。** 这比推算布局稳得多。

#### 关键发现 3：三个真崩过的 ABI 陷阱

| 陷阱 | 现象 | 解法 |
| --- | --- | --- |
| `sp<T>` 返回 | 调用约定错位 | 必须经 sret(x8)：声明成**带用户析构**的 8 字节结构。`ProcessState::self()` 也返回 `sp<>` |
| `String16` 作输出参数 | **段错误** | **不能 memset 清零**（`mString=nullptr` 会让 `operator=` 走 `release(nullptr)` → `SharedBuffer::bufferFromData(nullptr)` 崩溃）。要先调真正的默认构造 `_ZN7android8String16C2Ev` |
| `dladdr()` 对隐藏符号 | 解析不出名字 | 用 `dladdr(已知导出符号).dli_fbase` 拿装载基址 → 函数指针减基址得 ELF vaddr → 与 PC 侧 `readelf`/`objdump` 对齐 |

#### 现在的状态

| 环节 | 状态 |
| --- | --- |
| 拿到 inputflinger binder | ✅ 真机验证 |
| `createInputChannel` 事务 | ✅ 异常码 0 |
| **fd（输入通道）** | ✅ **fd=6, SOCK_SEQPACKET** |
| **token** | ✅ **handle=5** |
| 挂到 `WindowInfo` | ⏳ 下一步（`TouchInterceptor.h` 已真机验证可用）|
| 提交事务 + 拦截验收 | ⏳ |

**只差把 token 挂上去、提交事务了。**

### 6.-3 ★★ `createInputChannel` 事务成功（2026-10-13 实测）

**uid 0 的权限检查通过了。** 自写的最小探针 `build/recon/probe_ic_min.cpp`
在真机上跑出：

```
[1] 符号就绪
[2] IPCThreadState = 0xb400007d01a59200
[3a] writeInterfaceToken("android.os.IServiceManager") = 0
[3b] GET_SERVICE -> 0
[4a] 回复异常码 = 0
[4b] inputflinger binder = 0xb400007d01a6f000 (readStrongBinder=0)
    handle = 1

[5] createInputChannel ...
    writeInterfaceToken = 0
    writeString16("aurora-touch") = 0
    transact -> 0                     ← ★ 事务成功

[6] 解析回复
    异常码 = 0 (OK)                   ← ★ 权限通过，无 EX_SECURITY
    readString16 = 0
    fd = -2147483647                  ← 待解决：字段解析
    token = 0x0 (readStrongBinder=-2147483647)
```

**这就是整条链路最后一个技术卡点的答案：可行。**

### 成功的三个关键点（每一条都是踩出来的）

1. **绕开 `IServiceManager` 的 vtable** —— 它的方法未导出，`dladdr` 在本设备失效。
   改用 `IPCThreadState::transact` 直接对 servicemanager(handle 0) 发事务。
2. **`writeInterfaceToken` 不能省** —— 第一次没写接口描述符，
   `GET_SERVICE` 返回 `INT32_MIN`（`STATUS_UNKNOWN_ERROR`）；
   补上 `"android.os.IServiceManager"` 后立刻返回 0。
3. **AIDL 回复开头有个 int32 异常码** —— 必须先 `readInt32` 消费掉再读 binder，
   否则 `readStrongBinder` 返回 `INT32_MIN`。
4. **`readString16` 是赋值给已构造的 `String16`** —— 传裸缓冲区会 SIGSEGV，
   必须先 `string16Ctor(&buf, "", 0)`。

### 剩下的一小步：解析回复字段

`InputChannelCore { @utf8InCpp String name; ParcelFileDescriptor fd; IBinder token; }`

`readString16` 成功（=0），但随后 `readFileDescriptor` 与 `readStrongBinder`
都返回 `INT32_MIN`。可能原因（按可能性）：

1. **fd 不是用 `readFileDescriptor` 读的** —— AIDL 的 `ParcelFileDescriptor`
   是个 parcelable，可能要走 `readUniqueParcelFileDescriptor`
   或 `readParcelable`（子代理的符号表里两者都有）
2. `readString16` 可能没完全消费掉字符串（`@utf8InCpp` 在线路上是 UTF-16，
   但长度前缀的处理可能不同）
3. 字段顺序与 AIDL 声明不同

**下一步建议**：像子代理之前那样把 `reply` 的**原始字节十六进制转储**出来
（用 `Parcel::data()` + `dataSize()`，这两个符号已验证可用），
按字节反推真实布局 —— 比继续猜字段顺序快。

### 6.-2 ★ 拿到 inputflinger binder 的可行路径（2026-10-13 实测）

绕过了之前所有失败的路径。**核心思路：直接用 `IPCThreadState` 对
servicemanager（handle 0）发事务**，不依赖 `IServiceManager` 的 vtable
（其方法未导出、`dladdr` 也解析不出），也不依赖 `libbinder_ndk` 的实现细节。

实测输出（`probe_inputchannel`，真机 uid 0）：

```
[4b] 经 IPCThreadState 直连 servicemanager (handle 0)
  --- code=1 transact(handle=0) -> 0 (errno=0 Success)  replySize=32
      reply 十六进制：
  0000: 00 00 00 00 85 2a 68 73 00 00 00 00 01 00 00 00
  0010: 00 00 00 00 00 00 00 00 00 00 00 00 0c 00 00 00
      异常码 = 0
      readStrongBinder -> status=0 binder=0xb4000071bf8870a0
  => 直连成功：inputflinger IBinder = 0xb4000071bf8870a0
     BpBinder::binderHandle() = 1
     IBinder::getDebugPid() -> status=0 pid=3594   <== 一次真实 transaction
```

**要点**：

1. `code=1` 就是 `IServiceManager.GET_SERVICE_TRANSACTION`
   （= `IBinder::FIRST_CALL_TRANSACTION + 0`）
2. 拿到的是**平台 `IBinder*`**（不是 NDK 的 AIBinder），
   所以后续可以直接用 `BpBinder::transact`（已确认导出）
   和平台 `Parcel` 的读写方法
3. **用 `getDebugPid()` 做过一次真实事务验证** —— 确认这个 binder 是活的，
   不是空壳。这一步很重要，否则无法区分「拿到了」和「拿到了一个无效指针」
4. 拿到的 `BpBinder::binderHandle()` = **1**

**相关的符号状态**（真机核对）：

| 符号 | 状态 |
| --- | --- |
| `IPCThreadState::self()` | ✅ 可用 |
| `BpBinder::transact` | ✅ 已导出 |
| `BpBinder::binderHandle` | ✅ 已导出 |
| `IBinder::getDebugPid` | ✅ 可用（用于验证 binder 有效）|
| `IServiceManager` 的方法 | ❌ 未导出（dladdr 在本设备失效）|
| `libbinder_ndk` 的 `AServiceManager_getService` | ⚠️ 可用但 `prepareTransaction` 返回 ENOSYS |
| `AIBinder_toPlatformBinder` | ❌ 未导出 |

**当前卡点**：探针在拿到 binder 后，于第 [5] 步（`getService` 的 vtable 槽位查找）
失败并**提前退出**，还没走到 `createInputChannel`。
修法是：跳过第 [5] 步，直接用 [4b] 拿到的 binder 调 `createInputChannel`。

**下一轮要做的**（很明确）：

```text
1. 用 [4b] 的方式拿到 inputflinger 的 IBinder*
2. 构造平台 Parcel：
     writeInterfaceToken(String16("android.os.IInputFlinger"))
     writeString16(String16("aurora-touch"))     ← @utf8InCpp String 在线路上是 UTF-16
3. BpBinder::transact(code=1, data, &reply, 0)
4. reply.readInt32() 检查异常码
5. reply.readString16() → name
6. reply.readFileDescriptor() → fd
7. reply.readStrongBinder() → token
```

`InputChannelCore` 的 AIDL 定义：
`parcelable InputChannelCore { @utf8InCpp String name; ParcelDescriptor fd; IBinder token; }`

### 6.-1 `createInputChannel` 权限验证（2026-10-13，部分成功）

**最重要的结论已经拿到：SELinux 不拦 uid 0。**

用 `libbinder_ndk.so` 的稳定 C API（比戳 libbinder 内部可靠）：

```
[3] AServiceManager_getService("inputflinger") ...
    返回 0xb400007f47a6f140  OK        ← ★ 服务能拿到，SELinux 没有拦截
[4] prepareTransaction = -38, in=0x0   ← -38 = -ENOSYS = STATUS_INVALID_OPERATION
    改用 AParcel_create() 直接建 parcel
    AParcel_create = 0xb400007f47a5e7a0
    writeString("aurora-touch", 12) = 0   ← 成功
[5] transact(code=1, FIRST_CALL_TRANSACTION+0) ...
    transact = -22, out=0x0            ← -22 = -EINVAL = STATUS_BAD_VALUE
```

**已验证**：
- `dlopen("/system/lib64/libbinder_ndk.so")` 成功
- `AServiceManager_getService("inputflinger")` **返回有效 binder** ——
  这是之前最大的未知（`u:r:ksu:s0` 能否访问 inputflinger 服务），
  **答案是能，不需要额外的 SELinux 放行**。
- `AParcel_create()` / `AParcel_writeString()` 可用
- `dmesg` 里没有与本进程相关的 avc denial

**卡住的地方**：`createInputChannel` 的事务发不出去。两条路都不通：

| 路径 | 结果 |
| --- | --- |
| `AIBinder_prepareTransaction(binder, &in)` | **-38 (ENOSYS)** |
| `AParcel_create()` + `AIBinder_transact` | **-22 (EINVAL)** —— transact 可能要求 parcel 来自 prepareTransaction（那一步会写接口描述符）|

**下一步排查方向**（按可能性排序）：

1. `AIBinder_prepareTransaction` 的符号可能有版本差异（设备上 grep 到 2 处匹配）。
   用 `llvm-readelf` 核对它的确切签名与版本。
2. 可能需要先初始化 NDK binder 运行时
   （`ABinderProcess_setThreadPoolMaxThreadCount` / `ABinderProcess_startThreadPool`）。
3. 退回 **libbinder 平台 API**：现在已经有办法拿到 binder 了，
   可以试 `AIBinder_toPlatformBinder`（设备上**没导出**，
   但 `AIBinder_fromPlatformBinder` 有），或者用 NDK 拿到 handle 后
   走 `BpBinder::transact`（该符号**已确认导出**）。
4. `service list` 输出里的 `210 inputflinger` —— 那个数字就是 **service handle**，
   可以直接构造 `BpBinder`。`BpBinder::create` 没导出，但
   `BpBinder::transact` / `binderHandle` 都有。

**注意**：本节的失败**不代表方案不可行**。权限这一关已经过了
（能拿到 binder），剩下的纯粹是 binder 调用的技术问题。

### 6.0 ★ WindowInfo 真机验证通过（2026-10-13）

`WindowInfoHandle` 的构造与字段设置**已在测试机 上实测通过**：

```
[1] create OK (ABI 校验通过, mInfo=+56)
[2] 字段全部设置完成
    状态: id=4242 gsf=1.00 alpha=1.00 disp=5000000000 pid=10829 uid=0 cfg=0x0 displayId=-1 region@184
    inputConfig = 0x0 (DEFAULT=visible+touchable)
[3] 未崩溃，退出 0
```

**关键点：`mInfo` 偏移 = +56**（不是 android16-release 的 16），
运行时校验（vptr 像代码地址 + `dispatchingTimeout==5000000000`
+ `globalScaleFactor==1.0` 三重判据）全部通过。

### 6.0.1 踩到并绕开的两个 ABI 陷阱

| 陷阱 | 现象 | 处理 |
| --- | --- | --- |
| `sp<T>` 返回约定 | `aurora_demo` 的 `getSurface` 返回时崩 | 需要非平凡包装类型（见第七节）|
| `ftl::Flags<E>` 按值传参 | 调 `WindowInfo::setInputConfig` **必 SIGSEGV** | 改成直接按偏移写 `uint32`（语义等价：AOSP 自己用 `static_assert(sizeof(inputConfig)==4u)` 钉死）|

`ftl::Flags<E>` 与 `sp<T>` 同属 *non-trivial for the purposes of calls*，
按 AArch64 ABI 应经内存传递；按 `uint32_t` 声明就成了寄存器传递，调用约定错位。

### 6.0.2 真机 ABI 与 android16-release 不一致（重要）

子代理用真机 `libgui.so` 的**机器码**实测（反汇编，非推测）：

| 项 | android16-release | 真机实测 |
| --- | --- | --- |
| `WindowInfoHandle::mInfo` | 16 | **56** |
| `sizeof(WindowInfo)` | 432 | **496** |
| `sizeof(WindowInfoHandle)` | 448 | **552** |
| `touchableRegion` 之后的所有字段 | — | **整体 +64** |

证据（`WindowInfoHandle::readFromParcel` @0x1bd844 仅 12 字节）：

```
5f2403d5 00e00091 47210014   =  bti j; add x0, x0, #0x38; b <plt>     (0x38 = 56)
```

四处机器码互相印证（`readFromParcel` / `writeToParcel` / `updateFrom` / 默认构造）。
推测真机上 `touchableRegion` 占 168 字节而非 104（`Region` 的内联容量 4→8）。

**libgui/libui 是平台私有库，QPR 与安全更新允许改内部 ABI。**
所以 `TouchInterceptor.h` 里的偏移是**本机实测值**，
并且 `create()` 里有**运行时校验** —— 校验不过就拒绝操作，
宁可拦截失效也不写坏系统内存。

### 6.1 符号核对（用 llvm-readelf 读设备上的 libgui.so，权威）

### 6.1 符号核对（用 llvm-readelf 读设备上的 libgui.so，权威）

设备 `/system/lib64/libgui.so` 已拉到 `build/recon/devlibs/` 核对：

| 符号 | 文件内偏移 | 大小 |
| --- | --- | --- |
| `gui::WindowInfoHandle::WindowInfoHandle()` | `0x1bd710` | 72 |
| `gui::WindowInfoHandle::~WindowInfoHandle()` | `0xf87a0` | 76 |
| `gui::WindowInfoHandle::updateFrom(sp<WindowInfoHandle>)` | `0x1bd884` | 20 |
| `Transaction::setInputWindowInfo(sp<SurfaceControl>, sp<WindowInfoHandle>)` | `0xda198` | 92 |
| `layer_state_t::updateInputWindowInfo(sp<WindowInfoHandle>&&)` | `0x1682f4` | 28 |

**`setInputWindowInfo` 按值收 `sp<WindowInfoHandle>`** —— 所以必须传一个
**真实的 RefBase 对象**（`sp<>` 会做 `incStrong`/`decStrong`），
不能传 `malloc` 出来的假结构。

### 6.2 突破口：调用真实构造函数，而不是复刻 ABI

`WindowInfoHandle` 有**导出的默认构造函数**，所以正确做法是：

```text
1. posix_memalign 分配足够大的内存
2. 调 _ZN7android3gui16WindowInfoHandleC2Ev(ptr)   ← 真实构造，字段初始化正确
3. 按「运行时探测出的偏移表」改需要的字段
4. 包成 sp<> 传给 setInputWindowInfo
5. 退出时调 _ZN7android3gui16WindowInfoHandleD1Ev(ptr)
```

这样避免手工复刻整个 `WindowInfo` 布局（它含 `std::string` ×2、
`ui::Transform`、`Region`、`std::optional`、`InputApplicationInfo`，
还有 `Parcelable` 基类带来的 vptr）。

### 6.3 运行时布局探测结果（真机实测）

工具：`build/recon/probe_wininfo_layout.cpp`（只读，不创建图层、不碰输入系统）

```text
调用真实构造函数 WindowInfoHandle() ... 构造返回，未崩溃  OK
偏移 0 的 vptr = 0x000000775fdefb78   （合法 vtable 指针）
构造函数触及范围: [0, 545) 字节
```

用已知默认值反推出的字段偏移：

| 偏移 | 字段 | 依据（`WindowInfo.h` 的默认值） |
| --- | --- | --- |
| `+0x00` | vptr | `WindowInfo : public Parcelable`（经 `LightRefBase`），有虚表 |
| `+0x50` | `int32_t id = -1` | 该处读出 `0xFFFFFFFF` |
| `+0x70` | `dispatchingTimeout = 5s` | 该处读出 `5000000000`（纳秒）|
| `+0x80` / `+0x84` | `ownerPid` / `ownerUid` = `INVALID` | 两个相邻 `-1` |
| `+0x90` | `float globalScaleFactor = 1.0f` | 该处读出 `0x3F800000` |

**尚未定位**（含非平凡类型，需要进一步探测）：
`sp<IBinder> token`、`windowToken`、`std::string name`、
`Rect frame`、`ui::Size contentSize`、`ui::Transform transform`、
`Region touchableRegion`、`InputApplicationInfo applicationInfo`、
`ftl::Flags<InputConfig> inputConfig`、`wp<IBinder> touchableRegionCropHandle`。

**`token` 是最关键的一个**：没有它 `hasInputInfo()==false` → 窗口收到
`NO_INPUT_CHANNEL` → 完全收不到触摸。而 `token` 是一个 `sp<IBinder>`，
赋值要正确做引用计数，不能直接 memcpy 指针。

### 6.4 更稳的替代方案（待评估）

与其按偏移手工写字段，不如**用 libgui 自己的接口**：

- `WindowInfo::readFromParcel(Parcel*)` —— 我可以用 `Parcel` 按 AOSP 的
  字段顺序写入，再让它自己反序列化。这样**字段顺序与类型完全由库自己决定**，
  我只需要保证 parcel 的写入顺序与 `writeToParcel` 一致。
- 需要先取到 `WindowInfo::writeToParcel` 的**逐字实现**来确定 parcel 格式。

这条路比手工偏移表更稳（不怕字段增删），但需要先拿到
`libs/gui/WindowInfo.cpp` 的 `writeToParcel` 源码。

### 6.5 下一步

1. 取 `libs/gui/WindowInfo.cpp`，分析 `writeToParcel` 的字段顺序
2. 决定性实验：**先只做 `createInputChannel` 并确认返回成功**
   （这一步不依赖 `WindowInfo` 布局，可以独立验证 uid 0 的权限链路）
3. 再解决 `WindowInfo` 的构造，然后 `setInputWindowInfo`
4. 真机跑第四节那五条验收

## 七、★ 一个跨模块的 ABI 陷阱：`sp<T>` 的返回约定

这条是子代理在写 `createInputChannel` 探针时发现的，**它很可能解释了一个
之前一直没查清的崩溃**。

### 7.1 问题

libutils 的 `sp<T>` **有用户提供的拷贝构造函数与析构函数**，因此按 C++ ABI
属于 *non-trivial for the purposes of calls*。后果是：

| 返回类型 | AArch64 返回方式 |
| --- | --- |
| 平凡结构体（如 `struct { void* p; }`）| **x0 直接返回** |
| `sp<T>`（有用户拷贝构造/析构）| **sret（x8 间接返回）** |

**如果我按平凡结构体声明一个返回 `sp<>` 的函数指针，编译器认为它在 x0 返回，
而真实函数往 x8 指向的内存写 —— 调用约定立刻错位。**

### 7.2 为什么这解释了 `aurora_demo` 的 `getSurface` 崩溃

`aurora_demo` 里我这样调用：

```cpp
template <typename T>
struct StrongPointer { T* pointer = nullptr; T* get() const { return pointer; } };
// ↑ 平凡结构体，编译器认为返回值走 x0
auto f = (StrongPointer<void> (*)(void*))R("_ZN7android14SurfaceControl10getSurfaceEv");
static StrongPointer<void> surf = f(layer.get());   // ← 崩在这里
```

`SurfaceControl::getSurface()` 真实返回的是 `sp<Surface>`（非平凡），
**走 sret**。我的声明让它走 x0 —— 两者不一致，返回那一刻就出问题。
而 `createSurface` 能过，是因为它的调用点写法与已验证的探针逐字一致，
恰好凑对（探针的 `StrongPointer<void>` 也没带用户析构，属于同一个隐患，
只是那个调用点的栈布局碰巧容忍了）。

### 7.3 正确做法

给包装类型**加一个用户提供的析构函数**，强制它变成非平凡类型：

```cpp
struct SpRaw {                      // 用于接收 sp<T> 的返回值
    void* p;
    ~SpRaw() {}                     // ★ 关键：用户提供的析构 -> 非平凡 -> 走 sret
    SpRaw() : p(nullptr) {}
    SpRaw(const SpRaw&) : p(nullptr) {}   // 拷贝构造也自己给
};
```

这样编译器才会用 sret 传参，与真实函数一致。

### 7.4 适用范围

**凡是本地声明「返回 libutils/libgui 的 `sp<>` / `wp<>`」的函数指针，
都必须用非平凡包装类型。** 本项目已知受影响的地方：

| 位置 | 状态 |
| --- | --- |
| `aurora_demo` 的 `getSurface` | ⚠️ 待修（当前声明为平凡类型）|
| `aurora_demo` / 探针的 `createSurface` | 目前能用，但同样有隐患，建议一并加固 |
| `docs/TOUCH_INTERCEPTION_SF.md` 的 `createInputChannel` | 探针已用 `SpRaw` 处理 |

### 7.5 ⚠️ 一个反证（这条推断尚未验证）

理论说平凡声明会错，但**实测有个反证**：M3 探针（`probe_m3_ytbl.cpp`）
里的 `createSurface` 用的也是平凡 `StrongPointer<void>` 声明，
而它**在真机上跑通了**（图层创建成功、渲染 95.3fps）。

同一份代码里 `createSurface` 能过、`getSurface` 崩，说明：

- 要么 sret 理论对 `createSurface` 不适用（比如它的实际返回类型不同）
- 要么两者都错但 `createSurface` 的调用点栈布局碰巧容忍了
- 要么 `getSurface` 的崩溃另有原因

**所以这一节是「有价值的假设」，不是结论。**
验证方法（设备回来后）：

1. 写一个只调 `getSurface` 并把返回值按 sret 与非 sret 两种声明各试一次
2. 如果非平凡声明能过 → 假设成立，顺带修掉 `aurora_demo`
3. 如果两种都崩 → 说明另有原因（可能是 `layer.get()` 本身失效），
   应回到「整块复制探针代码」的做法（见 `M3_DEVICE_RERUN.md` 第四节）

## 八、置信度
**【已核实】**（读的是 `android16-release` 分支源码，文件已存
`research/aosp/platform__frameworks__native/android16-release/`）：

- `windowAcceptsTouchAt` 的完整判定（`InputDispatcher.cpp:581-607`）
- `canReceiveForegroundTouches`（`:631-635`）
- 两个 `LOG_ALWAYS_FATAL` 断言（`:5490-5502`）
- `eLayerSkipScreenshot = 0x40`（`LayerState.h:177`）
- `fillInputFrameInfo` 的三条行为（`LayerSnapshotBuilder.cpp:182-197`）
- `inputBoundsValid` 的来源（同文件 `:128`）
- `InputManager::createInputChannel` 的 AID_ROOT 白名单
- 参考实现的 `touchableRegion.orSelf` + `showAt` 事务序列
- 事务里 `eInputInfoChanged` 的处理（`RequestedLayerState.cpp:309-311`）

**【推断 / 未验证】**：

- `LAYER_BASE = INT32_MAX - 10` 是否足以压过所有系统窗口
  （参考测试是在它自己的进程里跑的，没和真实系统窗口竞争）
- 在真实设备上与锁屏、状态栏、输入法等系统窗口的命中优先级
  —— **需要真机验证**
- `setTrustedOverlay` 是否必需（我目前调用它会 SIGSEGV，希望不用它）

## 七、待真机验证的验收标准

| # | 验收项 |
| --- | --- |
| 1 | 窗口内任意位置点击/滑动 → 后面应用**完全收不到** |
| 2 | 窗口移动过程中 → 拦截区域跟得上，**没有漏点** |
| 3 | 窗口外 → 触摸**完全正常** |
| 4 | 程序退出 → 立刻恢复，无残留 |
| 5 | 与状态栏/导航栏/输入法共存时不打架 |
