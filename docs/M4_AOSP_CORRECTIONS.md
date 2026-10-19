# M4 重大更正：输入法与镜像路线的 AOSP 源码核实结论

> 两份独立的 AOSP `android16-release` 分支源码核实报告，**推翻了本项目
> M4 输入法方案的核心假设**，并解释了 M2 期间一个「试遍所有参数都失败」
> 的镜像 API。所有行号都来自 AOSP 源码，非推测。

## 一、镜像：`mirrorSurface(target, source)` 根本不存在

**M2 期间我花了很多轮试 `mirrorSurface` 的各种双参数组合，全部失败。**
原因很简单：**那个签名不存在。**

AOSP 里的真实签名是**单参数**的：

```cpp
// frameworks/native/libs/gui/include/gui/SurfaceComposerClient.h
sp<SurfaceControl> SurfaceComposerClient::mirrorSurface(SurfaceControl* mirrorFromSurface);  // :392
sp<SurfaceControl> SurfaceComposerClient::mirrorDisplay(DisplayId displayId);                 // :394
```

- `mirrorSurface` 只接受**一个图层句柄**，SF 用 `LayerHandle::getLayer()` 解析，
  不是活图层就返回 `NAME_NOT_FOUND`。**没有 display 形式。**
- **`mirrorDisplay(DisplayId)` 才是整屏镜像**，且 `SurfaceFlinger.cpp:5442-5480`：

```cpp
const int uid = ipc->getCallingUid();
if (uid != AID_ROOT && uid != AID_GRAPHICS && uid != AID_SYSTEM && uid != AID_SHELL) {
    ALOGE("Permission denied when trying to mirror display");
    return PERMISSION_DENIED;
}
```

**uid 0 显式通过。**

- Java 层 `SurfaceControl.java:2772` 有 `mirrorSurface` 但**没有** `mirrorDisplay`。
- **没有任何地方叫 `createMirrorLayer`。** 不要发明这个 API。

### 自反馈？不可能 —— 源码里有两道显式防护

这正是 M2 期间担心的「无限镜像」。核实后**在构造上就不可能发生**：

```cpp
// services/surfaceflinger/FrontEnd/LayerLifecycleManager.cpp:33-38
bool canMirrorRootLayer(RequestedLayerState& mirroringLayer, RequestedLayerState& rootLayer) {
    return rootLayer.isRoot() && rootLayer.layerStack == mirroringLayer.layerStackToMirror &&
            rootLayer.id != mirroringLayer.id;   // <-- 显式自我排除
}
```

外加 `LayerSnapshotBuilder.cpp:780-785`：**display 镜像永远被标记为
`handleSkipScreenshotFlag`** —— 即镜像图层不会被截图捕获。

### 对后续的意义

`mirrorDisplay` 是**零拷贝、GPU 合成、无 CPU 读回**的整屏镜像，
比目前用的 `screencap` 管道（6 fps，每帧 CPU 读回）在结构上高一个档次。

**但有个关键限制**：它是**图层**，本身就在屏幕上。要把镜像内容弄进
**我自己的 GL 纹理**做玻璃折射，还需要对它再跑一次 `captureLayers`。
所以它是一个「更好的桌面内容源」，不是「更好的反射结果」。

**建议**：单独一轮验证 `mirrorDisplay(DisplayId)`，成功后用它替换
`screencap` 管道，抓帧率应从 6 fps 提升到显示刷新率级别。

## 二、输入法：现有方案在 Android 16 上不可能成立

### 2.1 我原本的假设错在哪

`docs/M4_IME_BRIDGE.md` 里描述的方案是：
「起一个 `app_process` + 辅助 APK，创建一个 1×1 透明 `EditText` 悬浮窗，
让它拿到焦点，然后 `showSoftInput`」。

**这个方案在 Android 16 上的漏洞**：那个 `EditText` 所在的服务进程
**不是被 AMS 启动的**，因此拿不到 WMS 的窗口令牌。

### 2.2 硬拦路虎一：`IWindowSession` 拿不到

`IWindowSession.addWindow` **在 Android 14/15/16 都不存在**。
客户端方法是 `addToDisplay` / `addToDisplayAsUser` / `addToDisplayWithoutInputChannel`
（`IWindowSession.aidl:53-65`）。`addWindow` 是服务端方法。

真正致命的是 `Session.java:142-148`（android16）：

```java
synchronized (service.mGlobalLock) {
    mLastReportedAnimatorScale = service.getCurrentAnimatorScale();
    mProcess = service.mAtmService.mProcessMap.getProcess(mPid);
}
if (mProcess == null) {
    throw new IllegalStateException("Unknown pid=" + mPid + " uid=" + mUid);
}
```

`mProcessMap` **只由 AMS 启动进程时填充**。KernelSU 起的 ELF 永远不在里面
→ `openSession()` **在任何权限检查之前就抛异常**。

**这是 PID 门，不是权限门。uid 0 完全无效** —— 前面没有权限检查可绕，
只有一个无条件 null 检查。

**版本差异**：Android 14 只是 `Slog.e` 记一条日志（`Session.java:701-707`），
所以老办法在 A14 能跑；**A15 引入抛异常，A16 沿用**。

> 这解释了为什么 ssyclr 那个参考项目「能拉起输入法」——
> 它是通过安装 APK 并由系统启动服务做到的，不是纯 ELF 自建窗口。

### 2.3 硬拦路虎二：uid 0 的 `EditorInfo.packageName` 检查

即使拿到窗口，还有 `InputMethodManagerService.java:3863-3868`：

```java
if (editorInfo != null && !InputMethodUtils.checkIfPackageBelongsToUid(
        mPackageManagerInternal, cs.mUid, editorInfo.packageName)) {
    Slog.e(TAG, "Rejecting this client as it reported an invalid package name.");
    return InputBindResult.INVALID_PACKAGE_NAME;
}
```

链路：`checkIfPackageBelongsToUid` → `PackageManagerService.isSameApp`
→ `UserHandle.isSameApp(pkgUid, callingUid)`。

**没有任何已安装包的 appId 是 0。所以 uid 0 永远过不了这一关。**

传 `editorInfo == null` 也不是解 —— 那样直接返回 `NULL_EDITOR_INFO`，
没有 `InputConnection`，收不到任何文字。

**解**：`setresuid(1000)`（system）+ `EditorInfo.packageName = "android"`。
uid 1000 仍享受 `PermissionManager.checkPermissionUncached:1718-1738` 的
第一条豁免：`if (appId == ROOT_UID || appId == SYSTEM_UID) return GRANTED;`

### 2.4 `showSoftInput` 没有 displayId 参数

`IInputMethodManager.aidl:77-79` 的实际签名里没有 display 参数。
display 由 `addClient(..., untrustedDisplayId)` 和**窗口令牌所在的 display**
共同决定，不一致直接拒（`WMS:8235-8240` / `:8241-8245`）。

**所以「虚拟屏上弹 IME」这条捷径不存在。**

## 三、好消息：有一整套 uid 0 畅通的路径

### 3.1 真触摸 —— 不需要 WMS

```text
IInputFlinger.createInputChannel(name)   ← 服务名 "inputflinger"
```

`InputManager.cpp:255-266` 的检查原文：

```cpp
if (uid != AID_SHELL && uid != AID_ROOT) {
    LOG(ERROR) << __func__ << " can only be called by SHELL or ROOT users, but was called from UID " << uid;
    return binder::Status::fromExceptionCode(EX_SECURITY, "...");
}
```

**uid 0（AID_ROOT）显式允许。**

然后 `Transaction::setInputWindowInfo(sc, WindowInfoHandle{token = channel->getConnectionToken()})`
把它挂上去。`setFocusedWindow` **没有任何权限检查**。

AOSP 自带可直接照抄的原生模板：
`frameworks/native/libs/gui/tests/EndToEndNativeInputTest.cpp`
（已下载到 `research/aosp/`）。

> 注意：`SPY` 标志必须配 `PRIVATE_FLAG_TRUSTED_OVERLAY`，
> 否则 InputDispatcher 里是 `LOG_ALWAYS_FATAL_IF` —— **直接 abort**。

### 3.2 桌面捕获 —— uid 0 全部畅通

| API | uid 0 | 源码依据 |
| --- | --- | --- |
| `captureDisplayById` | ✅ 显式列出 `AID_ROOT` | `SurfaceFlinger.cpp:9194-9202` |
| `captureDisplay` / `captureLayers` | ✅ `PermissionCache` 的 uid==0 短路 | `PermissionCache.cpp:91-96` |
| `mirrorDisplay` | ✅ 显式列出 `AID_ROOT` | `SurfaceFlinger.cpp:5444-5449` |
| `createVirtualDisplay(isSecure=true)` | ✅ | `SurfaceFlinger.cpp:583` |

`PermissionCache.cpp:91-96` 原文：

```cpp
if ((uid == 0) || (pid == getpid())) { // root and ourselves is always okay
    return true;
}
```

### 3.3 AHardwareBuffer → EGLImage

官方支持，但**必须走对间接层**：

```text
AHardwareBuffer
  -> eglGetNativeClientBufferANDROID()   ← 不能跳过这步
  -> EGLClientBuffer
  -> eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID)
  -> glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES)
```

坑：usage 必须含 `GPU_SAMPLED_IMAGE`；**必须等 fence**
（`ScreenCaptureResults.fenceResult`），否则读到半渲染帧；
HDR 内容走 8 位 GL 路径颜色会错。

## 四、`screencap` 为什么注定慢

`screencap.cpp:244-260` 的原始输出模式：

```cpp
buffer->lock(GraphicBuffer::USAGE_SW_READ_OFTEN, &base)   // CPU 读回
for (y...) write(fd, base + y*stride, w*Bpp);             // 每行一次 write()
```

**每帧 = 进程创建 + 整屏 CPU 读回 + h 次 write() 系统调用。**
这从结构上就决定了它到不了高帧率 —— 实测 6 fps 与之吻合。

另外核实：`screencap` **没有流式/守护模式**，也**没有 `--mirror-display` 参数**。
真实参数只有 `[-ahp] [-d display-id] [FILENAME]` 和 `--hint-for-seamless`。

## 五、修正后的路线建议

按性价比排序（供你选择）：

### 路线 A：`setresuid(1000)` + 自建 WMS 窗口 + IMMS（真正的输入法）

1. `setresuid(1000, 1000, 1000)` —— **必须**，uid 0 过不了 package 检查
2. `IWindowSession.openSession()` → `addToDisplay`，**不传 `attrs.token`**，
   用 `TYPE_APPLICATION_OVERLAY (2038)`
   （WMS 会用 `client.asBinder()` 替你造 WindowToken，`WMS:1667-1674`）
3. 把 EGL/GLES 画进这个窗口的 `SurfaceControl`（不再用裸 SF 图层）
4. 从返回的 `InputChannel` + `InputConsumer` 读真触摸
5. `IInputMethodManager.addClient()`
6. `startInputOrWindowGainedFocus(editorInfo{packageName="android"})`
7. `showSoftInput()`，文字从 `IRemoteInputConnection.commitText` 回来

**代价**：手写两个 `BnInterface`（`IInputMethodClient` 约 10 个方法 +
`IRemoteInputConnection` 约 20 个），手工 parcel
`WindowManager.LayoutParams` 与 `EditorInfo`。

### 路线 B：`app_process` + 小 jar（省力版）

同样的 uid + 窗口 + IMMS 流程，但用几十行 Java 代替手写 binder。
`CLASSPATH=<jar> app_process /system/bin com.x.Main` 已核实可用
（`parsed_options.cc:708-709` 读 `CLASSPATH`；
`app_main.cpp:337-338` 启动 `RuntimeInit`）。

**这仍然不是「纯 ELF」，但不安装 APK** —— 只是一个磁盘上的 jar。

### 路线 C：委派给下层 App（唯一不改 uid 的路）

用 uinput 往真实 `EditText` 注入一次点击 → 那个 App 自己的窗口获得焦点
→ 它用自己的 package/uid 调 `startInput` → IMMS 接受 → 真系统输入法弹出。

**但文字落在那个 App 里，不在我的进程里。**

### 路线 D：自绘键盘

用自己的 GL 代码画一个进程内软键盘。完全可控，但不是「系统输入法」。

## 六、Shell 层的核实结论（省得再试）

- `adb shell ime` 就是 `cmd input_method ime`（`cmds/ime/ime.sh` 全文一行）。
  子命令只有 `list/enable/disable/set/reset/tracing`。
  **没有任何 show/hide 子命令** —— 没有 shell 命令能在无焦点 App 时弹键盘。
- `adb shell input text` 走 `KeyCharacterMap.getEvents()`
  + `injectInputEvent`（`InputShellCommand:391-392`），**全程不碰 IMMS**。
  所以它弹不出键盘、打不出中文/联想。
- `input keyevent BACK` **能关掉**已显示的键盘。

## 七、置信度说明

**已核实**（AOSP `android16-release` 源码逐行读取）：所有行号、AIDL 签名、
判定分支、两个 `mirrorSurface`/`mirrorDisplay` 签名、
`Session.mProcessMap` 抛异常、`checkIfPackageBelongsToUid` 链路、
`InputManager::createInputChannel` 的 AID_ROOT 白名单、
`LayerLifecycleManager` 的自镜像排除。

**未核实 / 推断**：

- `u:r:ksu:s0` 域能否 `find` 到 `window` 与 `inputflinger` 服务
  —— **取决于 KernelSU 策略，AOSP 源码里没有**。
  这是路线 A/B 唯一剩下的未知拦路虎。
  **建议下一步先做 SELinux 探测**：直接尝试连接这两个服务，
  然后 `dmesg | grep avc`。
- IME 窗口的生命周期前提（`mInputMethodWindow != null`，
  即开机后键盘至少显示过一次）
- 厂商 ROM 是否在 AOSP 之上对 overlay 加了额外 app-op 策略

**报告环境限制**：两份报告的 `web_search`/`web_fetch` 都不可用
（401 / fetch failed），改用 `pwsh` 直连 `android.googlesource.com` 与
`raw.githubusercontent.com` 获取。因此「没有找到某个开源项目」这类结论
是**假设而非证明**。
