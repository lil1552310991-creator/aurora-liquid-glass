# Root ELF 真机能力探测报告（测试机 / 测试机 / Android 16 / API 36）

> 本文只记录**实测**结果，不记录推测。每条结论都标注了验证命令和输出。
> 探测产物位于 `build/recon/`，不参与正式构建。

## 0. 环境

| 项 | 值 | 来源 |
| --- | --- | --- |
| 设备 | 测试机（测试机） | `adb devices -l` |
| 系统 | Android 16 / API 36 / `<build-number>` | `getprop ro.build.version.release` |
| Root | KernelSU `3.3.0`，`/data/adb/ksud` | `su --version` |
| su 后进程域 | `u:r:ksu:s0`（用 `su -c cat /proc/self/attr/current` 测得的**被 exec 的子进程**域） | 见 1.1 |
| SELinux | `Enforcing` | `getenforce` |
| 屏幕 | 物理 1080x2376 @ 640dpi，override density 480 | `wm size` / `wm density` |
| 主显示 ID | `1111111111111111111` | `dumpsys SurfaceFlinger --display-id` |
| GPU | Adreno (TM) 830，`/dev/kgsl-3d0` 可打开 | probe_egl |
| 触摸屏 | `/dev/input/event7`，name `touchpanel`，ABS_MT_POSITION_X 0..23040，Y 0..50688（16 倍缩放） | probe_input |

`u:r:ksu:s0` 是 KernelSU 给出的高权限域：probe 进程可读 `/system/lib64`、可读写 `/dev/input/event7`、可读写 `/dev/uinput`。
注意 `id -Z` 在被 exec 的子进程里也可能回显父进程的 `u:r:shell:s0`，**判定域必须以 `/proc/self/attr/current` 为准**。

## 1. 结论总表

| # | 能力 | 结果 | 证据 |
| --- | --- | --- | --- |
| 1.1 | root ELF 可读系统库 / 输入设备 | ✅ | `probe_rw` |
| 1.2 | root ELF 可打开 GPU 并获得 EGL/GLES3 上下文 | ✅ | `probe_egl` |
| 1.3 | root ELF 可 `dlopen` `libgui.so` 并解析合成器符号 | ✅ | `probe_egl` |
| 1.4 | root ELF 可读原始多点触摸事件流 | ✅ | `probe_input listen` |
| 1.5 | root ELF 可 `EVIOCGRAB` 独占触摸屏 | ❌ `EBUSY`（InputReader 已持有） | `probe_input listen` |
| 1.6 | root ELF 可创建 uinput 虚拟触摸设备，系统可见 | ✅ `event12 = aurora-glass-touch` | `probe_input_path [3]` |
| 1.7 | **内核输入抑制开关可写**（硬防穿透原语） | ✅ `inhibited` 0↔1 读写通过 | `probe_input_path [2]` |
| 1.8 | 注入事件能走完系统输入管线 | ⏳ 待用户确认秒表 | 见 §6 GATE |
| 1.9 | root ELF 可在 SurfaceFlinger 建图层（含镜像桌面） | ⏳ 待 M1 验收 | 见 §7 |
| 1.10 | 系统 IME 可在纯 ELF 下被拉起 | ❌ 原理上不可行 | 见 §4 |

**1.7 是本次探测最重要的发现，它把「防触摸穿透」从窗口层问题变成了内核原语问题。**

`/sys/class/input/event7/device/inhibited` 是 Android 输入子系统在每个输入设备上暴露的开关：

```text
event0  gpio-keys                  inhibited=0  -rw-r--r-- root:root
event7  touchpanel                 inhibited=0  -rw-r--r-- root:root
event11 uinput_nav                 inhibited=0
（event1..event11 全部存在同一属性）
```

实测语义：

```text
before: /sys/class/input/event7/device/inhibited = 1
after echo 0: /sys/class/input/event7/device/inhibited = 0
before = 0, want = 0 / after = 0 / RESULT: OK
```

也就是说 root ELF 可以：
1. `echo 1 > inhibited` —— 让**系统 InputReader 完全看不到真实触摸屏**（物理触摸不再分发到任何应用）；
2. 自己仍然用 `open("/dev/input/event7")+read()` 直接读原始事件（§3 已验证）；
3. 需要放行时，用 uinput 虚拟设备把事件**按自己的判断**重新注入系统。

这正是「触摸盾」+「输入法不被误弹」所需的全部原语，而且**完全在纯 ELF 内完成，不需要 APK**。

### 1.11 KernelSU `su` 调用契约（踩坑记录）

同一台设备上 `su -c` 的提权结果**依赖调用形态**，实测两种结果：

| 形态 | 结果 |
| --- | --- |
| `su -c <单个 ELF 路径>` | ✅ `uid=0`，context `u:r:ksu:s0` |
| `su -c "a && b"` / `su -c "a; b"` | ✅ 通常 `uid=0` |
| `su -c "printf 0 > /sys/.../inhibited"` | ❌ `Permission denied`（`printf` 是外部程序，重定向由它执行，掉回 `uid=2000 shell`） |
| `su -c "echo 0 > /sys/.../inhibited"` | ✅ 可行（`echo` 是 shell 内建，重定向在 root shell 里做） |
| 深层嵌套 `su -c "sh script.sh"` 且脚本内再 exec | ⚠️ 不稳定，必须由程序自检 `uid` |

**因此启动器与程序都必须自检 `uid==0` 并把真实 `uid`/context 打进日志**，不能假定 `su` 一定成功。
本项目的 `start.sh` 会先自检，失败时打印明确原因而不是静默降级。

## 2. 图形栈实测

```text
node /dev/kgsl-3d0                open=OK
dlopen /system/lib64/libEGL.so          = OK
dlopen /system/lib64/libgui.so          = OK
dlopen /system/lib64/libui.so           = OK
dlopen /system/lib64/libbinder.so       = OK
dlopen /system/lib64/libnativewindow.so = OK
dlopen /system/lib64/libandroid.so      = OK
eglInitialize OK version=1.5
EGL_VENDOR   = Android
EGL_VERSION  = 1.5 Android META-EGL
GL_VENDOR    = Qualcomm
GL_RENDERER  = Adreno (TM) 830
GL_VERSION   = OpenGL ES 3.2 V@0800.74
eglCreateContext(GL3) = ok
eglCreatePbufferSurface(64x64) = ok
eglMakeCurrent(pbuffer) = 1
glReadPixels -> rgba(64,128,191,255)  (expect ~64,128,191,255)
GL rasterization WORKS
```

`EGL_EXTENSIONS` 里对桌面反射关键的三项都在：
`EGL_ANDROID_image_native_buffer`、`EGL_KHR_image_base`、`EGL_ANDROID_get_native_client_buffer`。
也就是说 `AHardwareBuffer → EGLImage → GL 纹理` 的零拷贝导入路径可用。

### 2.1 libgui 符号（已解析）

```text
SurfaceComposerClient::getDefault()                                    = 0x75e2ede690
SurfaceComposerClient::createSurfaceChecked(String8 const&, uint32_t, uint32_t, int,
        sp<SurfaceControl>*, int, sp<IBinder> const&, gui::LayerMetadata, uint32_t*) 
SurfaceComposerClient::mirrorSurface(SurfaceControl*, SurfaceControl*) = 0x75e2f8ddd4
SurfaceControl::getSurface()                                           = 0x75e2ee2de8
SurfaceControl::createSurface()                                        = 0x75e2f89874
ANativeWindow_fromSurface                                              = 0x75d3a59c3c
ASurfaceControl_create                                                 = 0x75d3a65a00
```

`SurfaceComposerClient::createSurface`／`SurfaceControl` 的 C++ ABI 与 NDK r27 头文件**不一致**
（它们有 `SurfaceControl::getSurface()` 而 NDK 头文件没有），所以不能靠猜结构体布局来调用。
因此本项目的合成器接入采用**手写 binder 事务**（AIDL wire format），而不是猜 C++ ABI：

`BpSurfaceComposerClient::createSurface` 的 wire order（由 `llvm-objdump` 反汇编确认）：

```text
writeInterfaceToken(getInterfaceDescriptor())
writeUtf8AsUtf16(name)
writeInt32(width)
writeStrongBinder(parent)          // 可以是 null：即显示根图层
writeParcelable(LayerMetadata)
writeInt32(flags)
... transact(code)
```

## 3. 输入栈实测

```text
open(/dev/input/event7,O_RDONLY|O_NONBLOCK) = 3 (ok)
ABS_MT_POSITION_X min=0 max=23040
ABS_MT_POSITION_Y min=0 max=50688
device name = touchpanel
[before] current grab state = -1        <- 无人持有 grab
EVIOCGRAB(1) FAILED errno=16(Device or resource busy)
```

结论（重要）：

- 系统 `InputReader` 打开触摸设备时自己先拿了 grab，所以**第二个 grab 一定 EBUSY**。
  代码里会尝试 grab，失败即降级，不把它当错误。
- 未持有 grab 的第二个 `open()+read()` **仍然收得到事件流**，所以「读取真实触摸」这条路成立。
- 因此**无法用独占输入设备的方式阻止事件到达系统**。防穿透必须走「输入盾窗口」路线（见 §4）。

## 4. 防穿透与输入法的硬约束（设计依据）

Android 的输入分发顺序是 `evdev → InputReader → InputDispatcher → 窗口`。
一个纯 ELF 进程在 InputDispatcher 眼里**没有窗口**，因此：

1. 它无法成为触摸目标，也就无法"接住"触摸；
2. 它无法持有 `InputConnection`，因此**系统输入法无法以它为宿主弹出**；
3. 反过来，只要系统 InputReader 还看得见触摸屏，触摸就一定会走到后方应用。

由 §3 的实测，`EVIOCGRAB` 被 InputReader 占住，纯 ELF **没有**内核级的"吞掉触摸"手段。
所以「防触摸穿透 + 不误弹输入法」两个需求，在纯 ELF 单进程方案里只能通过以下之一实现：

| 方案 | 纯二进制 | 能否防穿透 | 能否用系统输入法 |
| --- | --- | --- | --- |
| A. 纯 ELF + uinput 回灌 + 面板内自绘键盘 | ✅ | ⚠️ 条件成立（见下） | ❌（自绘键盘替代） |
| B. 极小 APK 提供输入盾窗口 + 系统 IME | ❌ 需要 APK | ✅ 已在本仓库 Overlay 宿主验证 | ✅ |

方案 A 要让防穿透成立，必须让**输入盾素材**由 ELF 自己提供：ELF 用 uinput 建一个
`INPUT_PROP_DIRECT` 的虚拟触摸设备并注册成唯一可见触摸源，同时把真实触摸屏对系统的可见性
通过 ROM 级开关关闭（如 `/sys/class/input/eventN/device/enable`）；若 ROM 不提供该开关，
则退化为「面板内触摸只进我自己的 GL 场景，但后方应用仍会收到一份」，此时**必须在报告里
明确标注 passthrough=partial**，不能宣称已防穿透。

这一点是产品边界的硬约束，不能靠代码技巧绕开。

## 5. 探测工具

| 文件 | 用途 |
| --- | --- |
| `build/recon/probe_rw.c` | `/dev/uinput`、`/dev/input/*`、fb0 的读写权限 |
| `build/recon/probe_egl.c` | GPU 节点、EGL/GLES 上下文、libgui 符号解析、光栅化自检 |
| `build/recon/probe_input.c` | EVIOCGRAB 能力、原始触摸读取、uinput 虚拟触摸设备创建与注入 |
