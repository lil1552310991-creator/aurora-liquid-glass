# 液态玻璃演示程序（`aurora_demo`）

> 一个可直接在真机运行的纯 native ELF 演示，把此前分散在各探针里验证过的
> 环节汇成一条完整链路。源码：`build/recon/demo_entry.cpp`。

## 一、构建

```powershell
$ndk = "<sdk>\ndk\27.0.12077973"
$cxx = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\clang++.exe"
& $cxx --target=aarch64-linux-android28 -std=c++20 -O2 -ffp-contract=off `
  -Wall -Wextra -Werror=return-type `
  -I"build\recon" -I"app\src\main\cpp" `
  -o "build\recon\aurora_demo" "build\recon\demo_entry.cpp" `
  -lEGL -lGLESv3 -llog -ldl
```

产物：AArch64 ELF（Type DYN / PIE），81 KB。

**`-ffp-contract=off` 是必须的**，理由见 `docs/CORNER_PORT_AUDIT.md` 5.6 节。

链接 `-lpthread` 非必需（NDK 的 bionic 已并入 libc）。

## 二、运行

```bash
adb push build/recon/aurora_demo /data/local/tmp/
adb shell su -c "chmod 755 /data/local/tmp/aurora_demo"

# 默认：安全模式（不碰触摸屏）
adb shell su -c "/data/local/tmp/aurora_demo 620 900 1800 30"

# 启用触摸防穿透（有风险，见下）
adb shell su -c "/data/local/tmp/aurora_demo 620 900 1800 30 --shield"
```

参数：`[面板宽] [面板高] [帧数] [模糊半径] [--shield] [--touch <设备>]`。

**`--shield` 默认关闭是刻意的**。`inhibited=1` 期间触摸屏对整个系统失效，
如果进程异常退出而没有复位，手机将无法响应触摸。默认关闭让「只是看一眼
玻璃效果」这件事没有任何风险。启用后有三条兜底复位路径
（析构 / `atexit` / 信号），详见 `docs/M4_INPUT_SHIELD.md`。

## 三、它演示了什么

| 环节 | 来源 | 状态 |
| --- | --- | --- |
| 自建 SurfaceFlinger 图层 + EGL 上屏 | M1 | 曾截图验证 |
| **真实桌面反射（screencap → GL 纹理）** | M2 / M2.5 | **已接入**，抓帧 6fps / 渲染 60fps |
| **12-tap 可分离高斯模糊（gather 链）** | M3 | **本机 GPU 回归已验证** |
| ytbl 液态玻璃 shader（逐字嵌入） | M3 | **本机 GPU 像素回归已验证** |
| 修正后的玻璃配方 | `GLASS_RECIPE_VERIFICATION.md` | **本机 GPU 扫描已验证** |
| Kyant0 弹簧动效 | `Motion.h` | **本机 33 项自检通过** |
| **内核层触摸防穿透** | `InputShield.h` | 本机 19 项自检通过，真机待验 |

### 渲染链路

每帧四个 pass：

```text
A. blit      scene 的 gather 窗口 -> gatherFbo[0]（裁窗 + 垂直翻转）
B. gather 水平 gatherTex[0] -> gatherFbo[1]
C. gather 垂直 gatherTex[1] -> gatherFbo[0]（结果最终留在 gatherTex[0]）
D. panel     同时采 uScene 与 uGathered，输出玻璃
```

**为什么要 blit 这一步**：gather shader 的 `readRegion()` 把区域坐标映射成
`v = (uRegionSpan.y - regionPos.y) / uSourceTexel.y`，它没有独立的「区域原点」
uniform。所以先把窗口裁出来并翻转成 y 向上，之后两趟卷积就都在区域自身
坐标系里进行，坐标语义直白。

### 模糊参数（照搬 ytbl）

```text
blurRadius = 30 px          (DIALOG_GLASS_BLUR_DP = 10dp × density 3.0)
sigma      = 17.3205        (blurRadius × BLUR_SIGMA_FROM_RADIUS 0.57735)
step       = 4.3301 texels  (BLUR_STEP_FROM_SIGMA 0.25 × sigma)
12 个 tap 的权重和 = 4.5046
```

`buildTapMass()` 逐字复刻自 `Context.cpp:553`。**12 个抽头的权重不是简单
高斯**：每一跳的衰减本身也在按 `decay²` 递减，所以权重下降得比高斯快得多
（`tap[0]=0.969` → `tap[11]=0.011`）。用错公式会让模糊要么不够、要么出现环带。

### gather 区域尺寸的一个紧约束

取样边距公式（`GlassPanel.cpp:78-92`）：

```text
margin = 3 + blurRadius*2 + lensAmount + lensAmount*spectral
       = 3 + 60 + 128 + 15.36
       = 207 px
```

面板 620×900 居中在 1080×2376 上时，左右各只有 **230 px** 可用，
所以 margin 会用掉 207 px —— 区域宽度 `1056`，**右边界 1079，只剩 1 px 余量**。

也就是说：**这块面板的尺寸与 margin 公式已经接近极限**。
如果要把 `lensAmount` 再调大，或者面板再宽一点，gather 区域就会越界
（代码里有断言会直接报错退出）。这是一个不明显的耦合。

反过来，模糊本身只需要 `taps × step = 52 px` 的采样范围，
远小于 207 —— margin 的大头是给透镜色散留的。

### 动效设计

演示程序**不使用触摸输入**，而是用内部时序驱动，这样无需真机输入即可看到运动：

- 每 8 秒一个循环，前 4 秒展开、后 4 秒收起
- **展开用欠阻尼**（ζ=0.6, k=300，有回弹）
- **收起用临界阻尼**（ζ=1.0, k=300，干脆）
- 展开期间第 1.5~1.9 秒做一次按压脉冲（ζ=1.0, k=1000）

这三条弹簧规格取自 `docs/AMDL_MOTION_CATALOG.md` 核实的 Kyant0 参数。

真机联调时把 `DemoState::tick()` 里的时序换成 uinput 回灌的触摸事件即可，
接口位置已经留在那里。

### 参数取值的依据

代码里的每个数值都标了来源，关键是这几条（修正前曾系统性偏小 1.6~6.3 倍）：

| uniform | 值 | 依据 |
| --- | --- | --- |
| `uCornerRadii` | 144 | `DIALOG_CLOSE_RADIUS_DP = 48dp` × density 3.0 |
| `uLensHeight` / `uLensAmount` | 72 / 128 | 24dp / 48dp |
| `uLensDepth` | 1.0 | Kyant0 `depthEffect` 是**布尔**，不是小量 |
| `uSurfaceMode` + `uSurfaceTint` | 2.0 + `(0.071,0.071,0.071,0.40)` | #121212 @ 0.4 霜面 |
| `uToneBoost` | 1.0 | vibrancy，饱和 ×1.5 |
| `uToneCtrl` | `(0, 1.1, 1)` | 本机扫描：饱和 0.851→0.875 |
| `uDropRect` 等三个 | **-1** | 默认 0 会走错分支导致**面板整体消失** |

### 桌面反射是怎么接的

**独立抓帧线程 + 双缓冲**（M2.5 验证过的结构），渲染不被抓帧阻塞：

```text
抓帧线程：screencap -> 垂直翻转 -> 抹除面板区域 -> 换缓冲
渲染线程：取最新帧 -> glTexSubImage2D -> 玻璃 shader 采 uScene
```

三个必须做对的地方：

1. **垂直翻转**。`screencap` 输出是 y 向下，而 ytbl 的 shader 全程用 y 向上。
2. **抹除面板自身区域**。否则玻璃会采样到自己 —— 这就是用户实际报告过的
   「无限镜像」（Droste 效应）。做法是用面板四周一圈的像素填补面板区域，
   切断自反馈。见 `docs/M2_DESKTOP_REFLECTION.md`。
3. **`uDropRect` 等三个形体 uniform 必须置 -1**。默认 0 会走错分支导致面板消失。

退出时会打印抓帧统计（成功帧数 / 失败次数 / 平均耗时），
便于判断 `screencap` 在该机型上的实际表现。

## 四、已知限制

1. **输入法未接入**。输入桥接已实现（`docs/M4_IME_BRIDGE.md`），但演示程序
   还没有输入框，也没有与桥接连动。
2. **动效由内部时序驱动**，不响应真实触摸。真机联调时把 `DemoState::tick()`
   里的时序换成输入盾给出的触摸事件即可。
3. `screencap` 抓帧约 6 fps，桌面反射的更新率远低于渲染帧率 ——
   快速滑动桌面时玻璃里的倒影会有明显滞后。这是抓帧路径的固有限制。
4. 面板区域的背景是**用四周像素填补**的近似，不是真实背景。
   玻璃会因此显示略微偏移的桌面内容。这是防自反馈的代价。
5. **触摸盾启用后不做手势识别**。面板内的触摸被吞掉但不产生任何交互
   （不拖动、不点击）。需要真机验证后再接手势。
6. 演示程序不生成 `Skia`/`ImGui` 界面，只画一块玻璃面板。

## 五、清理

程序正常退出会销毁 EGL 资源并析构 client。若异常中断，检查残留：

```bash
adb shell su -c "dumpsys SurfaceFlinger --list | grep -i aurora"
```

另外务必确认触摸屏未被锁：

```bash
adb shell su -c "cat /sys/class/input/event7/device/inhibited"   # 应为 0
```
