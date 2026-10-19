# M3：ytbl 液态玻璃 shader 接入

> 目标：用仓库内已有的 ytbl 液态玻璃 shader 替换 M2.5 里手写的简易 shader。
> 本文记录接入路径、踩到的坑，以及 Kyant0 连续曲率圆角算法的移植验证。

## 一、shader 接入方式（逐字搬运，不改写）

ytbl 的 shader 以字符串常量形式定义在
`app/src/main/cpp/third_party/liquidglass/ytbl/Shaders.cpp`。为了**保证逐字一致**，
不手工转写进新文件，而是用脚本抽取：

```text
build/recon/extract_shaders.py        从 Shaders.cpp 抽出三个 shader 源码
  -> build/recon/ytbl_quad.vert        570 字节 / 15 行
  -> build/recon/ytbl_gather.frag     1386 字节 / 39 行
  -> build/recon/ytbl_panel.frag     14000 字节 / 349 行
build/recon/gen_embedded_shaders.py   转成 C++ 原始字符串字面量头文件
  -> build/recon/ytbl_shaders_embedded.h
```

三个 shader 的分工：

| shader | 作用 |
| --- | --- |
| `kQuadVertexSource` | 用 `gl_VertexID` 生成四边形，`uFrameRect` 定位、`uViewSpan` 归一化 |
| `kGatherFragmentSource` | 12-tap 可分离高斯模糊（`buildTapMass` 算权重，`uBoxHalf` 控制盒式预滤波） |
| `kPanelFragmentSource` | 玻璃本体：透镜折射、棱镜色散、边缘光、内嵌高光、光泽、色调映射 |

宿主侧 uniform 语义参照 `Context.cpp:804 glPaintPanel()`，模糊参数照搬
`GlassConfig.h::Render`，权重公式照搬 `Context.cpp:553 buildTapMass()`。

## 二、踩到的关键坑（都是「不报错但效果完全错」的类型）

### 2.1 `uPrism*` 数组必须显式赋值，否则色散被静默废掉

`kPanelFragmentSource` 在 `uSpectral > 0` 时走彩色分支，用 7 个数组 uniform
（`uPrismDist` / `uPrismEvenRed` / `uPrismOddRed` / … / `uPrismCenterGreen`）
加权求和。GL 里未赋值的 uniform 默认全 0，于是 `color` 变成几乎全黑/全白。
必须从 `GlassConfig.h` 取真实权重：

```text
PRISM_DIST      = {1/3, 2/3, 1}
PRISM_EVEN_RED  = {3/21, 3/21, 9/42}
PRISM_ODD_RED   = {3/21, 3/21, 3/42}
PRISM_EVEN_GREEN= {6/21, 3/42, 0}
PRISM_ODD_GREEN = {0, 3/42, 0}
PRISM_EVEN_BLUE = {7/42, 7/42, 7/42}
PRISM_ODD_BLUE  = {-7/42, -7/42, -7/42}
PRISM_CENTER_GREEN = 6/21
```

### 2.2 `uSurfaceMode` 的正确取值（此处曾判断过宽，已修正）

`GlassPanel.cpp:259` 的映射：

```text
SurfaceMode::Overlay -> 2.0
SurfaceMode::Hue     -> 1.0
其它（None）         -> 0.0
```

首次尝试设成 `1.0` 时面板被完全洗成白色。

**但「全白」的成因我最初归因错了。** 用本机 GLES 像素回归（见
`docs/OFFLINE_VERIFICATION_PLAN.md`）测出精确条件是：

```text
uSurfaceMode > 0.5  **且**  uSurfaceTint 是「白色 + 高 alpha」
   mode = 1 + 白色 tint -> 93.6% 像素变白
   mode = 2 + 白色 tint -> 99.7% 像素变白
   只改 mode、tint 保持全 0 -> 输出与基线逐像素完全一致（Δ = 0.00）
```

也就是说 **`uSurfaceMode` 单独并不会致白，必须配合 tint**。首次实验时
tint 恰好是白色不透明，才出现全白。诊断真机白屏时，
应同时 dump 这两个 uniform，而不是只看 mode。

**另外，此前「纯玻璃必须用 0.0」的结论下得过宽**。经与 `GlassConfig.h` 逐条核对：

- ytbl 自家的 **Dialog / Menu / Tab / Button 四个组件全部用 `2.0`（Overlay）**
- Kyant0 在 `DialogContent.kt` 里对应的是 `onDrawSurface = drawRect(containerColor)`

也就是说，**Overlay 分支那层 38~40% 的霜面染色是参考实现的一部分**，
去掉它正是面板看起来「像塑料」的头号原因。正确取值是
`uSurfaceMode = 2.0` + `uSurfaceTint = (0.071, 0.071, 0.071, 0.40)`
（即 Kyant0 的 `#121212 @ 0.4`）。

### 2.3 `uToneCtrl` 不是「0 就是中性」

shader 的 `toneMap()` 形如
`dot(c, contrast * (base + vec3(saturation,0,0))) + lift`。
`contrast = 0` 会把颜色项整体清零，只剩 `lift`，输出变成单色。
中性值是 **亮度 0 / 对比 1 / 饱和 1**，对应
`Color::BRIGHTNESS_DARK=0.0`、`CONTRAST_*=1.0`、`SATURATION_*=1.0`。

### 2.4 `uDarkGuard` 是乘积语义

`GlassPanel.cpp:241`：`job.darkGuard = optics.darkGuard * DARK_GUARD_FLOOR`，
其中 `DARK_GUARD_FLOOR = 0.045`。直接传 1.0 会得到过强的暗部抬升。

### 2.5 `uDropRect` / `uFeedRect` / `uStrandRect` 必须显式置 -1

shader 用 `uDropRect.z >= 0.0 && uDropRect.w >= 0.0` 判断是否启用「下落形态」。
未赋值时为 0，条件成立，于是面板会按「形体融合」分支走 —— 出现意外的形状与凸起。

本机像素回归实测：`uDropRect` 默认 0 会让**面板整体消失**
（`bodyHalf = 0`，Δmean 188.1）。四个「默认 0 会出事」的 uniform 里，
它是**唯一**真正会走错分支的。详见第四章 4.2 节。

### 2.6 gather 的坐标语义

`gatherRead()` 把像素坐标映射成纹理 uv：

```text
uv = vec2(pixel.x * uGatherPitch.x, uGatherRows - pixel.y * uGatherPitch.y) / uGatherTexel
```

`glPaintPanel()` 的对应赋值（`Context.cpp:792-798`）：

```text
pitchX = fboW / regionW        rows  = fboH
pitchY = fboH / regionH        texelX = outputW, texelY = outputH
limitX = (fboW - 0.5) / outputW,  limitY = (fboH - 0.5) / outputH
```

本项目让 FBO 尺寸等于区域尺寸（`pitch = 1`、`rows = fboH`、`texel = fbo 尺寸`），
坐标最直白。

### 2.7 gather margin 必须随透镜参数一起放大（否则「越调越脏」）

ytbl 的取样边距公式（`GlassPanel.cpp:78-92`）：

```text
pad = 3 + blurRadius * 2 + lensAmount + lensAmount * spectral
```

按参考配方（blur 30 / lens 128 / spectral 0.12）算得 **pad ≈ 206 px**，
而当前 `probe_m3_ytbl.cpp` 里只留了 40 px。

**只调大透镜而不调 margin，四角的外向色散抽样会被 clamp 到边缘像素**，
表现为拖尾与糊边 —— 也就是「把透镜调大反而更脏」。
这个耦合关系很容易被误判成参数没调对。

## 三、参考配方（来自 `docs/GLASS_VISUAL_RECIPE.md` 的逐条核对）

主力面板参照 **Dialog**（ytbl 里唯一有 24 个专属 `DIALOG_*` 常量、
且能与 Kyant0 `DialogContent.kt` 逐项对齐的组件）。
480dpi 下 density = 3.0：

| uniform | 我方曾用 | 参考值 | 依据 |
| --- | --- | --- | --- |
| `uCornerRadii` | 46 | **144** | `DIALOG_CLOSE_RADIUS_DP = 48dp` |
| `uLensHeight` | 46 | **72** | 24dp |
| `uLensAmount` | 40 | **128** | 48dp，被 `LENS_REACH_MAX` 截断 |
| `uLensDepth` | 0.16 | **1.0** | Kyant0 `depthEffect` 是 Bool，四处调用全传 `true` |
| `uBlurRadius` | 18 | **30** | `DIALOG_GLASS_BLUR_DP = 10dp`，也是上限 |
| `uSurfaceMode` | 0.0 | **2.0** | 四个组件全用 Overlay |
| `uSurfaceTint` | 未设 | **(0.071,0.071,0.071,0.40)** | Kyant0 `#121212 @ 0.4` |
| `uToneBoost` | 0 | **1.0** | vibrancy，饱和 ×1.5 |
| `uToneCtrl.x` | 0.04 | **0.0** | 0.04 是浅色主题的 `BRIGHTNESS_LIGHT` |
| `uSpectral` | 1.0 | **0.12** | `DIALOG_SPECTRAL_BASE` |
| `uRefractTint.a` | 0.22 | **0.00** | 参考深色主题下 `tintDeviation = 0` |
| `uEdgeBlur` | 1.2 | **0.75** | |
| `uEdgeAlpha` | 0.55 | **0.38** | Kyant0 `Highlight.Plain` 白 α0.38 |
| `uInsetTint` | 白 | **黑** | 参考内阴影是黑色 α0.15；白 = 叠了第二道高光 |

**结论：几何量此前系统性偏小 1.6~6.3 倍**，根因是我把 `lensDepth` 的注释
「参考里是小量」误当成数值语义，实际它是布尔开关。

### 两条无法靠调参消除的差异

1. Kyant0 的法线半径用 `radius × 1.5`（`Shaders.kt:79,116`），
   ytbl 用真半径（`Shaders.cpp:329`）—— 需改 shader 才能对齐。
2. `glowOn = 1.0` 常开是**假高光**：参考里它是指针按下的光斑
   （`InteractiveHighlight.kt:62-89`），静止面板应关掉；
   若保留，`glowReach` 应为 **930**（短边 ×1.5）而不是 220。

### 一处已澄清的「疑似错误」

- `lensCurve(x)` 与 Kyant0 的 `circleMap(x)` **是解析恒等**（已代数验证，
  t=0.5 时两者都 = 0.1340），所以折射曲线本身没写错。
- ytbl 的棱镜色散是 Kyant0 7-tap 色散的**代数重写，逐通道权重完全等价**。
  因此 `uSpectral = 1.0` 并非超范围，但**参考组件的基准值只有 0.12**。

### 另一处需要修的上游示例

`使用说明.md:44` 的示例写 `setScale(2.0f)`，但 480dpi 上必须 **3.0**
（`densityScale` 默认 2.0，见 `Runtime.cpp:44`）。自建管线不受影响，
但走回落库路径会出问题。

## 四、本机像素回归（不需要设备）

`docs/OFFLINE_VERIFICATION_PLAN.md` 记录了一项关键能力突破：
本机可以用 **moderngl + OpenGL 3.3 Core + 真实 GPU 驱动**逐字执行 ytbl 的
panel / gather / quad shader（只把 `#version 300 es` 改成 `330 core`、
删掉 `precision` 行），几何与 uniform 逐条对齐 `probe_m3_ytbl.cpp`。

已跑 **1 基线 + 21 分支用例 + 67 uniform 敏感度用例 + 1 全默认用例**，出 23 张 PNG。

### 4.1 uniform 覆盖率：无遗漏

panel program 71 个 uniform、gather 10 个，生产路径（`Context.cpp`）与探针路径
**都 100% 赋值**，GPU 回读确认 71/71 落地。所以此前踩的两个坑
（`uPrism*`、`uSurfaceMode`）**都不是漏赋值**。

### 4.2 三个「默认 0 会出事」的 uniform

| uniform | 默认 0 的后果 | 本探针是否已规避 |
| --- | --- | --- |
| `uDropRect` | `z>=0 && w>=0` 判定成立 → bodyHalf=0 → **面板整体消失**（Δmean 188.1） | 已设为 -1 |
| `uToneCtrl` | 整块均匀灰（Δmean 38.2） | 已设为 (0, 1, 1) |
| `uPanelOpacity` | 全透明（Δmean 185.1） | 已设为 1.0 |
| `uSceneOn` | 全黑（Δmean 186.8） | 已设为 1.0 |

`uDropRect` 是**唯一**真正会走错分支的（用 -1 作哨兵值）。
另外「全默认」的 virgin 用例输出 lum = 0 / alpha = 0 且**不报任何错**——
也就是说**上传路径一旦整体失效，症状是「玻璃静默消失」**。
这四个都值得做成启动断言。

### 4.3 `uPrism*` 全 0 的真实后果

不是「色散静默失效」，而是**整块面板塌成常数色**：
R/B 通道完全依赖 6 个 pair 抽头，全 0 时 `sum.rgb = 0` 而 alpha 仍为 1，
实测 lum 0.651 → 0.224、99.72% 的像素变化超过 16/255。
数学上每个通道的权重和恒等于 1（可做成 CI 断言）。

### 4.4 方法学上的一个坑（值得记住）

**GL 的 uniform 状态是「粘」的**：如果用「省略赋值」来测默认 0，
会得到假阴性（沿用上一轮的值）。必须显式写 0 才能测准。

## 五、坐标系：y 向下 vs y 向上

ytbl 全程使用 **y 向上** 的像素坐标（`RoundedRectangleOutline` 里 `y = h - p[1]*r`，
`kQuadVertexSource` 里 `clip.y = 1 - device.y*2/viewSpan.y`）。
`screencap` 给出的帧是 **y 向下**。因此抓帧后先做一次垂直翻转再上传，
之后面板、gather、纹理坐标就都在同一个 y-up 空间里。

自反馈抹除区域也随之换算：翻转后面板下边为 `screenH - panelY - panelH`。

## 六、Kyant0 连续曲率圆角（squircle）移植验证

液态玻璃的「高级感」很大一部分来自 Apple 风格的四角。仓库内 ytbl 用的是普通
圆角 SDF（`boxMetric`），因此额外移植了 Kyant0 的解析解。

来源与算法：
- 仓库 `https://github.com/Kyant0/Shapes`（分支 `master`）
- `shapes/src/commonMain/kotlin/com/kyant/shapes/ContinuousCurvatureRoundedRectangleCornerBuilder.kt`
- 解三次方程 `solveCubicSingle` 与降次四次方程 `solveDepressedQuarticSingle` 得到 κ，
  再反解出 10 个 Bézier 控制点（20 个 double），按半径 r 缩放使用。

移植产物：`build/recon/kyant_corner_check.cpp`（本机可编译运行，**不需要设备**）。

### 实测结果

```text
参数: extendedFraction = 2/3, arcFraction = 0.5

tW=tH=1 时的 10 个控制点:
  P0 = (-0.666667,  0.000000)     P5 = ( 0.811490,  0.364845)
  P1 = (-0.096298,  0.000000)     P6 = ( 0.903326,  0.586555)
  P2 = ( 0.180052,  0.000000)     P7 = ( 1.000000,  0.819948)
  P3 = ( 0.413445,  0.096674)     P8 = ( 1.000000,  1.096298)
  P4 = ( 0.635155,  0.188510)     P9 = ( 1.000000,  1.666667)

合理性检查全部通过:
  OK  x 控制点单调递增
  OK  起点切线水平 (P0..P2 的 y 均为 0)
  OK  末端三点 x 均为 1.0（顶点处切线与直边重合）
  OK  y 分量单调递增至 5/3（由 extendedFraction=2/3 决定）

几何验证:
  起点切角 = 0.000°   终点切角 = 90.000°   => 两端与直边相切，曲率连续
  收笔 x 精确等于 w（与右边缘对齐）
```

### 为什么这个验证是必要的

这是纯数值算法，移植时写错一个系数**不会报错**，只会让形状「看着有点怪」。
验证过程中我先后犯过两个判断错误，都靠数值核对纠正：

1. 误以为 `P6 的 x` 应为 1.0 —— 实际它就该是 0.903326，只有末端三点是 1.0。
2. 误以为弧心在 `(w-r, r)`，用「圆心角应覆盖 0~90 度」检验，
   得到 -33.7°~123.7° 就误判为失败。**连续曲率圆角的弧心并不在那里**，
   它比标准圆弧更「方」，曲线会略微越出扇形区域。改为检验「端点精确性 + 端点切线」
   后全部通过。

### 落地方式（待设备可用后实施）

该算法给出的是 Bézier 控制点，不是解析 SDF，两种落地方式：

1. **在 shader 里用多项式逼近 SDF**：对 `t ∈ [0,1]` 预计算超椭圆指数映射，
   用 `pow` 形式近似。成本低，但与精确解有偏差。
2. **主机侧把这些 Bézier 离散成多边形/三角形带**，与玻璃的其余渲染分离。
   形状精确，但需要额外的几何通道。

倾向方案 1（先用一个标定过的超椭圆指数），因为它可以完全留在 fragment shader 内，
不必改动现有的 `kQuadVertexSource` 单四边形管线。
