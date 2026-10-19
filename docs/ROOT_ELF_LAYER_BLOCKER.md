# 纯 ELF 自建合成器图层：从阻塞到打通

> 本文记录「root ELF 不依赖 APK 自建 SurfaceFlinger 图层」这条路的完整过程。
> 前半是**曾经卡住的根因**，后半是**解法与实测证据**。两半都保留，因为踩过的坑
> 是这份代码最容易回归的地方。

## 结论（已实测通过）

在测试机（Android 16 / API 36 / 厂商 ROM / KernelSU root）上，**纯 native ELF
可以自建 SurfaceFlinger 图层、用 EGL 绑定、并把帧真实呈现在屏幕上**。

| 验证项 | 实测证据 |
| --- | --- |
| 建图层 | `SurfaceControl = 0xb4000075afc3c300` |
| 对象真实 | 对象 `[+0x00]` 指向 libgui 区间内的 vtable |
| EGL 绑定 | `eglCreateWindowSurface` 成功，`GL_RENDERER = Adreno (TM) 830` |
| 提交帧 | 连续 300 帧 `eglSwapBuffers` 成功 |
| 合成器认可 | `dumpsys SurfaceFlinger`：`AuroraGlass#120131 pid=27173 uid=0` |
| 缓冲分配 | `[BBQ] AuroraGlass#120131#0(BLAST Consumer)0 w/h:1080x2376 format:RGBA_8888_UBWC` |
| **真实显示** | 渲染期间截图可见半透明蓝色面板 + 金色竖条覆盖在 B 站视频之上 |
| `eSkipScreenshot` | 开启后同一图层在截图中完全消失（flag 生效） |

## 一、曾经卡住的根因

### 1.1 环境本身没问题

`uid=0`、`u:r:ksu:s0`、`dlopen libgui/libui/libbinder/libEGL` 全部成功，
Adreno 830 可用，SurfaceFlinger 也接受了连接：

```text
I/SurfaceFlinger: [SF client] NEW(0xb4000079607541d0) for (24457:)
```

### 1.2 崩在 `DefaultComposerClient::getComposerClient()`

```text
F/DEBUG: signal 11 (SIGSEGV), fault addr 0x7f01ed1690
F/DEBUG: #00 pc 00000000000bfed8 /system/lib64/libgui.so
           (android::DefaultComposerClient::getComposerClient()+56)
```

反汇编还原机理：`getComposerClient()` 把 `Singleton<DefaultComposerClient>`
返回的 48 字节空壳的 `[holder+0x28]` 当成 `sp<SurfaceComposerClient>` 来用，
而该槽位为 0；随后对空对象调用 `RefBase::incStrong`，按 vtable 负偏移取控制块时
跳到非法地址。

**实测**：`Singleton<DefaultComposerClient>::getInstance()` 返回的 48 字节对象
`[0]=0`（连 vptr 都没有，只是一个 mutex）、`[0xd8]=0`、`[0x28]=0`，确为空壳。

### 1.3 因此作废的入口

| 入口 | 结果 |
| --- | --- |
| `SurfaceComposerClient::getDefault()` | ❌ SIGSEGV |
| `Singleton<DefaultComposerClient>::getInstance()` | ⚠️ 空壳，不可用 |
| `ComposerServiceAIDL::getComposerService()` | ❌ 内部走同一 holder，SIGSEGV |
| `ComposerService::getInstance()` / `ExtensionLoaderProxy::getInstance()` | ❌ 符号不存在 |
| `ServiceManager::getService` | ❌ libbinder.so 未导出 |
| `SurfaceComposerClient::SurfaceComposerClient(sp<ISurfaceComposerClient> const&)` | ❌ SIGSEGV（入参不是真对象） |

> `screencap` 是系统自带 ELF 且依赖 libgui，看似反例，但它是在 AOSP 源码树里编译的，
> 用的是 NDK 不提供的 `gui::CaptureArgs`／`ScreenCaptureResults` 等私有头文件，
> 不能照抄。

## 二、解法：三个决定性细节

参考 `AndroidSurfaceImgui-Enhanced-Input-method`（MIT，见
`docs/THIRD_PARTY_NOTICES.md`）后定位到差异点。

### 2.1 用默认构造函数，而不是 `getDefault()`

```cpp
// ❌ 会崩
void* client = SurfaceComposerClient::getDefault();

// ✅ 可行：解析并调用默认构造函数
auto ctor = dlsym(libgui, "_ZN7android21SurfaceComposerClientC2Ev");
ctor(buffer);                       // buffer 是不透明缓冲区，不手写布局
RefBase::incStrong(buffer, this);
```

**并且对象必须是 `static`**：`RefBase::incStrong` 内部的
`__android_log_assert` 会拒绝栈上的对象地址，直接 abort：

```text
RefBase used with stack pointer argument
Aborted
```

### 2.2 Android 14+ 的父句柄要传 `&parentHandle`

```cpp
void* parentHandle = nullptr;
createSurface(data, name, w, h, /*format=*/1, flags,
              &parentHandle,        // ✅ 一个值为 null 的 sp<IBinder> 的地址
              metadata, &transformHint);
//            nullptr            // ❌ 参数语义不同
```

### 2.3 `getSurface()` 的返回需要 +8

```cpp
auto sp = SurfaceControl__GetSurface(sc);
void* window = reinterpret_cast<void*>(
    reinterpret_cast<size_t>(sp.pointer) + sizeof(std::max_align_t) / 2);
```

## 三、仍未解决的一处（不影响 M1）

`Transaction::setTrustedOverlay` / `setLayer` 目前仍会崩：

```text
F/DEBUG: #00 pc 00000000000c674c /system/lib64/libgui.so
           (android::TransactionState::getLayerState(sp<SurfaceControl> const&)+80)
F/DEBUG: #01 pc 000000000017ff2c
           (android::SurfaceComposerClient::Transaction::setTrustedOverlay(sp<SurfaceControl> const&, bool)+28)
```

崩在 `RefBase::incStrong` 按 vtable 负偏移取控制块处（`fault addr 0xd65f03c0d5032447`，
该值解码后是 ARM64 指令字节，说明偏移量算成了垃圾）。

**影响评估**：`setTrustedOverlay` + `setLayer(INT_MAX)` 只是「把图层拉到最高 z 序」
的置顶手段。跳过它之后图层依然可见、依然能正常渲染（已由截图证明）。
因此 M1 验收不受影响；该分支保留在代码中但默认关闭，等 M2 需要精确控制 z 序时再修。

## 四、复现方式

```powershell
# 构建
clang++ --target=aarch64-linux-android24 -std=c++20 -O1 `
  -o build/recon/probe_m1_fixed build/recon/probe_m1_fixed.cpp `
  -lEGL -lGLESv3 -llog -ldl

# 部署并运行（Transaction 分支默认关闭）
adb push build/recon/probe_m1_fixed /data/local/tmp/
adb shell su -c "chmod 755 /data/local/tmp/probe_m1_fixed; /data/local/tmp/probe_m1_fixed 1080 2376 1"
```

`skipScreenshot` 参数：`1` = 图层对截图/录屏隐身（flag `0x40`），`0` = 可见。
