# M5：固化为正式模块与手动命令行启动

> 目标要求的交付方式：**手动命令行启动**，不安装 APK、不需要开机自启。

## 一、两条路线必须分清

`root-host/` 下现在有**两条不同的技术路线**，不要混用：

| 目录 | 路线 | 启动方式 | 依赖 |
| --- | --- | --- | --- |
| `root-host/*.cpp` | **服务 APK 宿主** | `aurora_root_bootstrap` 用 ActivityManager/AppOps 启停 APK | 需要安装 APK |
| `root-host/elf/` | **纯 ELF** | `start.sh` 直接跑 ELF | 不装 APK、不依赖 dex 旁挂 |

本目标要的是**纯 ELF** 那条。两条路线共用「桌面帧来源」这一个概念，
但实现方式不同：

- APK 路线：`RootDesktopCapture` 走 MediaProjection（有授权弹窗）
- 纯 ELF 路线：`aurora_demo` 直接 `posix_spawn` 调 `screencap`（无弹窗，
  但只有约 6 fps）

## 二、模块结构

```text
root-host/elf/
├── CMakeLists.txt      构建三个纯 ELF 目标
├── start.sh            手动命令行启动（带安全检查）
└── README.md           本文

构建产物（build/elf-arm64/bin/）：
├── aurora_demo         主程序：液态玻璃演示
├── inputshield_test    触摸盾验证工具（selftest / probe / live）
└── ime_probe           输入法桥接分步验证（release / connect / show）
```

## 三、构建

```powershell
$sdk  = "<sdk>"
$ndk  = "$sdk\ndk\27.0.12077973"
$cmake = "$sdk\cmake\3.22.1\bin\cmake.exe"
$ninja = "$sdk\cmake\3.22.1\bin\ninja.exe"

& $cmake -S root-host\elf -B build\elf-arm64 -G Ninja `
  -DCMAKE_MAKE_PROGRAM="$ninja" `
  -DCMAKE_TOOLCHAIN_FILE="$ndk\build\cmake\android.toolchain.cmake" `
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 `
  -DCMAKE_BUILD_TYPE=Release

& $cmake --build build\elf-arm64
```

实测产物（均为 AArch64 PIE）：

| 二进制 | 大小 |
| --- | --- |
| `aurora_demo` | 628,312 B |
| `ime_probe` | 647,864 B |
| `inputshield_test` | 606,544 B |

**动态依赖只有 NDK 公共库**：

```text
libEGL.so  libGLESv3.so  liblog.so  libdl.so  libm.so  libc.so
```

`libgui.so` / `libbinder.so` / `libutils.so` 全部在运行时用 `dlopen` 打开，
所以这些二进制**没有链接期对私有平台库的依赖** —— 在缺少某个私有符号的
ROM 上也能启动，只是会自己报告不可用，而不是加载失败。

构建用 `Release`（CMake 默认 `-O3`）。`-ffp-contract=off` 已在 CMakeLists
里显式添加，理由见 `docs/CORNER_PORT_AUDIT.md` 5.6 节。

## 四、部署与启动

```bash
# 推送到设备
adb push build/elf-arm64/bin/aurora_demo      /data/local/tmp/aurora/
adb push build/elf-arm64/bin/inputshield_test /data/local/tmp/aurora/
adb push build/elf-arm64/bin/ime_probe        /data/local/tmp/aurora/
adb push root-host/elf/start.sh               /data/local/tmp/aurora/
adb shell su -c "chmod -R 755 /data/local/tmp/aurora"

# 环境检查（不改动任何状态）
adb shell su -c "sh /data/local/tmp/aurora/start.sh --check"

# 启动演示（触摸盾关闭，安全）
adb shell su -c "sh /data/local/tmp/aurora/start.sh"

# 启动演示 + 触摸盾
adb shell su -c "sh /data/local/tmp/aurora/start.sh --shield"

# 只做清理
adb shell su -c "sh /data/local/tmp/aurora/start.sh --clean"
```

## 五、start.sh 的三条安全设计

这部分比功能更重要，因为**触摸盾用错会让用户的手机点不动**。

### 5.1 启动前先复位触摸屏

如果上一次异常退出留下了 `inhibited=1`，脚本在启动前就会把它复位。
复位不是「写 0 就完」，而是**写后回读确认**：

```sh
echo 0 > "$INHIBIT"
val="$(cat "$INHIBIT")"
[ "$val" = "0" ] || warn "复位失败！请手动执行: echo 0 > $INHIBIT"
```

### 5.2 退出时无条件复位

启用触摸盾时注册 `trap ... EXIT INT TERM`，无论正常结束还是被 Ctrl+C /
kill，都会复位。这是第二道防线（第一道在 `InputShield` 自身的析构 /
`atexit` / 信号处理里）。

### 5.3 触摸盾默认关闭

抑制触摸是有代价的操作，必须显式 `--shield`。让「只是看一眼玻璃效果」
这件事零风险。

### 5.4 一个容易踩的 shell 坑

复位必须用 `echo` 而**不能用 `printf`**：

```text
su -c "printf 0 > /sys/.../inhibited"   ❌ Permission denied
su -c "echo 0 > /sys/.../inhibited"     ✅
```

原因：`printf` 是外部程序，重定向由它执行，进程会掉回 `uid=2000 shell`；
`echo` 是 shell 内建，重定向在 root shell 里完成。
这条来自 `docs/ROOT_ELF_DEVICE_PROBE.md` 的实测。

## 六、验收清单（真机）

每次真机实验前后都要走一遍，尤其是最后一项。

**启动前**

```bash
adb shell su -c "cat /sys/class/input/event7/device/inhibited"   # 应为 0
adb shell su -c "pidof aurora_demo"                              # 应为空
adb shell su -c "dumpsys SurfaceFlinger --list | grep -i aurora" # 应为空
```

**运行中**

- 屏幕中央应出现一块圆角玻璃面板
- 玻璃里能看到桌面内容（**有模糊**，不是清晰倒影）
- 面板每 8 秒展开/收起一次，展开时有过冲回弹

**退出后**

```bash
adb shell su -c "cat /sys/class/input/event7/device/inhibited"   # ★ 必须为 0
adb shell su -c "pidof aurora_demo"                              # 应为空
adb shell su -c "dumpsys SurfaceFlinger --list | grep -i aurora" # 应为空
adb shell su -c "ls /data/adb | grep aurora-ime"                 # 应为空
adb shell pm path com.aurora.imebridge                           # 应无结果
```

**如果 `inhibited` 仍是 1，立刻手动复位**：

```bash
adb shell su -c "echo 0 > /sys/class/input/event7/device/inhibited"
```

## 七、状态

**已完成（离线验证）**：

- 三个纯 ELF 目标经 CMake 构建通过
- 动态依赖仅公共库（已用 `llvm-readelf -d` 核对）
- `start.sh` 经 `sh -n` 语法检查
- 各模块的本机自检：`Motion.h` 33 项、`InputShield` 19 项、
  圆角算法与源码逐点零偏差、玻璃配方 GPU 像素回归

**未验证（必须有设备）**：

1. 三个二进制在真机上的实际运行
2. 自建图层在 Android 16 上的行为（此前 M1 验证过，但参数已改）
3. 玻璃的最终视觉（配方修正后未复跑）
4. 触摸盾的 `inhibited` 实际写入效果与触摸分流
5. 输入法桥接的整条链路
6. `start.sh` 在 Android 的 `mksh` 下的实际行为
   （`local`、`trap EXIT` 在 mksh 上应当可用，但未实测）
