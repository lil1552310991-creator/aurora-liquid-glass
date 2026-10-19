# M4：纯 ELF 的触摸防穿透（内核层输入盾）

> ## ⚠️ 已废弃为主路线 —— 请读 `TOUCH_INTERCEPTION_SF.md`
>
> **本文描述的 `inhibited` 方案做不到用户要的「按窗口范围拦截」。**
>
> `inhibited` 是**全屏设备级**开关，只有「全都能用」和「全都不能用」
> 两个状态。而且实测抑制期间**连本进程自己也读不到事件**
> （2026-10-13 一次 12 秒实验收到 0 帧），所以「面板内吞掉、面板外
> 经 uinput 回灌」的设计在原理上不成立。
>
> **后果是真实的**：那次 12 秒抑制期间用户正好想滑动屏幕，
> 结果整个触摸屏无响应。已因此废弃该路线。
>
> **正确路线**：SurfaceFlinger 输入窗口
> （`IInputFlinger.createInputChannel` + `Transaction::setInputWindowInfo`），
> 拦截范围精确等于窗口的 `touchableRegion`，窗口外触摸完全正常，
> 且**不需要抑制、不需要 uinput 回灌**。详见
> `docs/TOUCH_INTERCEPTION_SF.md`。
>
> 本文保留作为：**应急手段的记录**。`InputShield.h` 仍然可用，
> 但默认关闭，并且已加硬超时（`setMaxHoldSeconds`，默认 20 秒），
> 超时自动复位 —— 不会再出现无限期卡住用户手机的情况。

> 实现：`app/src/main/cpp/InputShield.h`
> 验证工具：`build/recon/inputshield_test.cpp`
>
> 与既有的 APK 方案（`docs/TOUCH_PASSTHROUGH.md`）是**完全不同的路径** ——
> 那份靠 Java 层的全屏透明输入盾窗口，纯 ELF 没有窗口系统可用。

## 一、为什么必须走内核层

设备探测（`docs/ROOT_ELF_DEVICE_PROBE.md`）给出三个决定性事实：

| 事实 | 结论 |
| --- | --- |
| `EVIOCGRAB(1)` 失败 `EBUSY` | InputReader 已持有触摸屏，**无法用 grab 抢独占** |
| `/sys/class/input/eventN/device/inhibited` 可写（0↔1） | **Android 输入子系统暴露的抑制开关** |
| root 可创建 uinput 虚拟触摸设备，系统可见 | 有回灌通道 |

由此得到三步：

```text
echo 1 > /sys/class/input/eventN/device/inhibited   # 系统 InputReader 完全看不到触摸屏
open("/dev/input/eventN") + read()                  # 但自己仍能直接读原始事件
按判断用 uinput 回灌需要放行的事件                   # 只放行面板外的
```

## 二、为什么不会自我回读（这类方案最容易踩的坑）

**我们只读真实触摸屏、只写虚拟设备，两者是不同设备节点。**

回灌的事件进的是 uinput 设备，不在我们的读取集合里，因此结构上就不可能
出现「读到自己的注入事件 → 再次注入」的无限循环。
这是结构性保证，不依赖任何标记位或过滤规则。

## 三、生命周期安全（这是本模块最关键的部分）

`inhibited = 1` 期间触摸屏对整个系统失效。**如果进程崩溃而没有复位，
用户的手机将无法响应触摸。** 因此设计了三重兜底：

| 路径 | 触发时机 | 实现 |
| --- | --- | --- |
| 析构 | 正常作用域退出 | `~InputShield()` |
| `atexit` | 正常 `return` / `exit()` | `InputShieldGuard::Arm()` |
| 信号处理 | `SIGINT` / `SIGTERM` / `SIGSEGV` / `SIGABRT` 等 | 同上，复位后重抛信号保留退出码语义 |

另外三条设计上的安全考虑：

1. **复位写的是原值不是硬写 0**。构造时先读出 `originalInhibit_`，
   复位时恢复它 —— 如果进入时本来就被别人置成了 1，我们不该擅自改成 0。
2. **写后回读确认**。`writeInhibit()` 写完会回读比对；不一致即视为失败。
3. **失败路径全部回滚**。`open()` 里任一步失败，前面已做的抑制都会撤销，
   保证「返回 false 时不会留下任何副作用」—— 这条已被自检覆盖。

## 四、坐标换算：一个容易写错的地方

真实触摸屏的 `ABS_MT_POSITION_*` 量程与屏幕像素之比**不是整数**：

```text
ABS_MT_POSITION_X 量程 0..23040，屏幕宽 1080  -> 23040/1080 = 21.3333
ABS_MT_POSITION_Y 量程 0..50688，屏幕高 2376  -> 50688/2376 = 21.3333
```

所以**不能用整除做坐标换算**（`x / 21` 会累积误差，`x / scale` 在
非整数比下更是错的）。本模块的做法是：触点保留原始值，命中判断一律走
**归一化坐标**（`xNorm()` / `yNorm()`），矩形参数也用 0..1，
这样与机型无关。`scaleHint()` 只用于打印诊断。

## 五、捕获策略

```text
ACTION_DOWN 落在面板内  -> 开始捕获
捕获期间               -> 无论手指是否移出面板，全部吞掉
全部手指抬起           -> 结束捕获
未捕获的帧             -> 回灌给系统（放行）
```

**为什么捕获后要一直吞到抬手**：如果手指从面板滑出就立刻转为放行，
后方应用会收到一个「半截」序列（只有 MOVE/UP 没有 DOWN），
可能被解释成点击或误触发输入法。这也是既有 APK 方案
（`TOUCH_PASSTHROUGH.md` 第 2.2 节）采用同样策略的原因。

## 六、构建与验证

### 本机（不需要设备）

```powershell
$ndk = "<sdk>\ndk\27.0.12077973"
$cxx = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
& $cxx -std=c++20 -O2 -ffp-contract=off -Wall -Wextra `
  -DAURORA_HOST_SELFTEST=1 -I"build\recon\hostshim" `
  -include "build\recon\hostshim\aurora_host_compat.h" `
  -I"app\src\main\cpp" `
  -o "build\recon\inputshield_test.exe" "build\recon\inputshield_test.cpp"
build\recon\inputshield_test.exe selftest
```

`build/recon/hostshim/` 是本机自检用的最小头文件替身。**为什么需要**：
NDK 的 sysroot 头是为 Android bionic 写的，直接拿给 MinGW 编译会与 UCRT 的
`stdint.h` 冲突（`intptr_t` 重定义）；而 MinGW 本身既没有 `poll.h`、
`sys/ioctl.h`，也缺少 `O_CLOEXEC` / `O_NONBLOCK` / `SIGHUP` / `SIGQUIT`。
把这些补齐后，纯逻辑（帧解析、归一化命中、路径推导、生命周期幂等）
就能在本机无设备验证。**设备上的真实构建不使用这些替身。**

自检覆盖（19 项，全部通过）：

```text
[1] inhibited 路径推导        event7 / 两位数编号 / 无分隔符拒绝 / 非 event 前缀拒绝
[2] 帧解析（MT-B）            屏幕中心归一化为 (0.5,0.5) / 量程比非整数的确认
[3] 归一化命中判断            allInside / anyInside / 抬起手指不参与 / 空帧
[4] 生命周期幂等              未打开时状态正确 / close() 可重复
[5] 失败无副作用              打开不存在的设备返回 false 且未进入抑制态
```

第 5 项是**最关键的安全属性**：任何失败路径都不能让触摸屏留在失效状态。

### 真机

```bash
adb push build/recon/inputshield_test_arm64 /data/local/tmp/
adb shell su -c "chmod 755 /data/local/tmp/inputshield_test_arm64"

# 只读探测：不改任何状态
adb shell su -c "/data/local/tmp/inputshield_test_arm64 probe /dev/input/event7"

# 真实联调：抑制触摸 30 秒，面板内吞掉、面板外放行
adb shell su -c "/data/local/tmp/inputshield_test_arm64 live /dev/input/event7 30"

# 结束后务必确认已复位
adb shell su -c "cat /sys/class/input/event7/device/inhibited"   # 应为 0
```

## 七、尚未完成 / 已知边界

1. **未接输入法（IME）**。这是 M4 的另一半：面板内点输入框时需要拉起系统
   输入法。参考 `build/recon/ref_ime/` 的 ssyclr 方案（`app_process`
   + 透明 1×1 悬浮窗 + loopback 协议），尚未移植。
2. **未在真机验证**。本机自检只覆盖纯逻辑；`inhibited` 实际写入效果、
   uinput 设备被系统识别、以及真实的触摸分流都必须在设备上确认。
3. **未接进演示程序**。`aurora_demo` 目前不接收触摸，输入盾尚未与它联动。
4. **多指跨面板边界的策略**：当前实现是「任一点在面板内即开始捕获」，
   多指场景下的取舍（如一指在面板内、一指在桌面）尚未定义。
