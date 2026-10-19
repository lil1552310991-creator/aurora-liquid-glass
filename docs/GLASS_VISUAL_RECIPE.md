# 液态玻璃视觉配方（ytbl × Kyant0 对照）

> 目的：把我方基于 ytbl `kPanelFragmentSource` 的玻璃面板，从「塑料」调回参考实现的「液态玻璃」。
> 本文所有数值都标了来源文件:行号；凡是我推断而非源码明写的，都标注【推断】。

---

## 〇、口径与前提

### 0.1 路径约定

| 简称 | 实际路径 |
| --- | --- |
| `GlassConfig.h` 等 ytbl 文件 | `app/src/main/cpp/third_party/liquidglass/ytbl/` |
| `Kyant0` 根目录 | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/` |
| 我方实际宿主 | `build/recon/probe_m3_ytbl.cpp` |

### 0.2 一处需要纠正的任务描述

任务里提到的 `GlassPanel.cpp` 的 `applyPanelJob()` —— **未找到**该函数名。
与之对应的是：

- `appendJob()`（`GlassPanel.cpp:203`）：把 `PanelStyle` 逐字段折算成 `PanelJob` 的**设备像素**值；
- `submitJob()`（`GlassPanel.cpp:448`）/ `submitPanelJob()`（`GlassPanel.cpp:462`）；
- `glPaintPanel()`（`Context.cpp:804`）：把 `PanelJob` 写进 uniform —— 这一处任务描述正确。

### 0.3 单位口径（最要紧的一条）

- `px(dp)` = `dp * densityScale()`（`Runtime.cpp:311`；`densityScale()` 返回 `g_scale`，默认 `2.0f`，见 `Runtime.cpp:44`、`Runtime.cpp:60`）。
- 全部 `*_DP` 常量是 **dp**，经 `px()` 变成 **设备像素** 后才进 `PanelJob` 与 uniform。
- 因此 ytbl 的 uniform（`uLensHeight` / `uLensAmount` / `uEdgeWidth` / `uBlurRadius` …）**都是设备像素**，不是 dp。
- 480dpi 设备 → density = 480/160 = **3.0** → `px(1dp) = 3px`。
- ⚠️ ytbl 自带《使用说明.md:44》的示例写的是 `lgx::setScale(2.0f)`。**480dpi 上必须传 3.0**，否则所有 dp 派生的几何量系统性地小 1/3。我方是自建管线（直接写 uniform），不受此影响，但如果哪天回落到库路径，这是个必炸的坑。

### 0.4 模糊半径的口径

`Context.cpp:703`：`sigma = blurRadius * BLUR_SIGMA_FROM_RADIUS(=0.57735)`。
0.57735 = 1/√3，正是 Compose `BlurEffect(radiusX, radiusY)` 的 radius→sigma 约定。
**结论：ytbl 的 `blurPx` 与 Kyant0 的 `blur(dp.toPx())` 是同一口径，可以直接对比。**（`Blur.kt:22-28`）

---

## 一、常量对照表

### 1.1 `Render::` 命名空间 —— 模糊 / 捕获

| 常量名 | 值 | 控制的视觉 | 单位与换算 | 来源 |
| --- | --- | --- | --- | --- |
| `BLUR_SIGMA_FROM_RADIUS` | 0.57735 | 半径→高斯 σ 的换算系数 | 无量纲 | `GlassConfig.h:53` |
| `BLUR_MIN_SIGMA` | 0.5 | 低于此半径视为不模糊 | px | `GlassConfig.h:54` |
| `BLUR_FBO_SIGMA_CAP` | 3.0 | 单次 FBO 的最大 σ，超过就降采样 | px（FBO 空间） | `GlassConfig.h:55` |
| `BLUR_STEP_FROM_SIGMA` | 0.25 | 采样步长 = σ×0.25 | 无量纲 | `GlassConfig.h:56` |
| `BLUR_TAPS` | 12 | 单向 12 tap（共 25 采样）| 个 | `GlassConfig.h:58` |
| `BLUR_DECIMATE_LIMIT` | 16 | 最大降采样倍数 | 倍 | `GlassConfig.h:59` |
| `BLUR_TARGET_MIN_SPAN` | 8 | FBO 最小边长 | px | `GlassConfig.h:60` |
| `BLUR_STEP_BOX_AT` | 1.5 | 步长大于此值才加盒式预滤波 | px（FBO 空间） | `GlassConfig.h:61` |
| `BLUR_BOX_HALF_RATIO` | 0.5 | 盒式预滤波半宽 = 步长×0.5 | 无量纲 | `GlassConfig.h:62` |
| `BLUR_TARGET_ALIGN` | 32 | FBO 尺寸按 32 对齐 | px | `GlassConfig.h:63` |
| `BLUR_CAPTURE_SHRINK_RATIO` | 0.25 | 连续无变化后捕获缓冲收缩比例 | 比例 | `GlassConfig.h:65` |
| `BLUR_CAPTURE_SHRINK_FRAMES` | 120 | 触发收缩的静止帧数 | 帧 | `GlassConfig.h:68` |
| **`BLUR_RADIUS_MAX_DP`** | **10.0** | **模糊半径硬上限** | **dp → 480dpi 下 30px** | `GlassConfig.h:69` |
| **`LENS_REACH_MAX`** | **128.0** | **透镜高度/强度的 px 硬上限** | **px（不乘 density）** | `GlassConfig.h:70` |

### 1.2 `Render::` —— 捕获留白（决定边缘折射会不会被裁掉）

| 常量名 | 值 | 控制的视觉 | 单位 | 来源 |
| --- | --- | --- | --- | --- |
| `CAPTURE_PAD_BASE` | 3.0 | 基础留白 | px | `GlassConfig.h:72` |
| `CAPTURE_PAD_BLUR_GAIN` | 2.0 | 模糊留白 = 模糊半径×2 | 倍 | `GlassConfig.h:73` |
| `CAPTURE_PAD_BLUR_GAIN_SAFE` | 3.0 | 无 gather（走单张贴图）时用 3 倍 | 倍 | `GlassConfig.h:74` |
| `CAPTURE_PAD_MAX` | 640.0 | 留白上限 | px | `GlassConfig.h:75` |

公式（`GlassPanel.cpp:78-92`）：
`pad = clamp( 3 + blur×gain + lensAmount + lensAmount×spectral, 3, 640 )`
其中 `gain` = 2（有 gather）/ 3（无 gather），`lensAmount` 与 `blur` 都先夹到上限再乘 `uniformPixelScale`。

### 1.3 `Render::` —— 边缘光 / 高光

| 常量名 | 值 | 控制的视觉 | 单位 | 来源 |
| --- | --- | --- | --- | --- |
| `EDGE_WIDTH_MIN` | 0.5 | 默认边缘光宽度 | dp → 1.5px | `GlassConfig.h:77` |
| `EDGE_SOFTNESS_MIN` | 0.25 | 默认边缘光模糊 | dp → 0.75px | `GlassConfig.h:78` |
| `EDGE_ANGLE_DEFAULT` | 45.0 | 边缘光方位角 | 度 | `GlassConfig.h:79` |
| `EDGE_FALLOFF_DEFAULT` | 1.0 | 边缘光方向衰减指数 | 指数 | `GlassConfig.h:80` |
| `EDGE_PLAIN_WIDTH` | 0.5 | Plain 模式宽度 | dp | `GlassConfig.h:81` |
| `EDGE_PLAIN_SOFTNESS` | 0.25 | Plain 模式模糊 | dp | `GlassConfig.h:82` |
| `EDGE_PLAIN_ALPHA` | 1.0 | Plain 模式强度倍率 | 倍率 | `GlassConfig.h:83` |
| `EDGE_AMBIENT_WIDTH` | 0.33333334 | Ambient 模式宽度（= 0.5/1.5） | dp | `GlassConfig.h:84` |
| `EDGE_AMBIENT_SOFTNESS` | 0.16666667 | Ambient 模式模糊（= 0.25/1.5） | dp | `GlassConfig.h:85` |
| `EDGE_AMBIENT_ALPHA` | 0.38 | Ambient 模式强度 | 倍率 | `GlassConfig.h:86` |
| `DARK_GUARD_FLOOR` | 0.045 | **暗部保护系数**：`job.darkGuard = optics.darkGuard × 0.045` | 倍率 | `GlassConfig.h:87` |
| `EDGE_CONTOUR_ALPHA` | 0.50 | Contour 模式基础强度 | 倍率 | `GlassConfig.h:89` |
| `EDGE_CONTOUR_ANGLE` | −90.0 | Contour 默认方位角（朝上） | 度 | `GlassConfig.h:90` |
| `EDGE_CONTOUR_FALLOFF` | 1.35 | Contour 方向衰减 | 指数 | `GlassConfig.h:91` |
| `EDGE_CONTOUR_WIDTH` | 0.6 | Contour 宽度 | dp | `GlassConfig.h:92` |
| `EDGE_CONTOUR_SOFTNESS` | 0.30 | Contour 模糊 | dp | `GlassConfig.h:93` |
| `EDGE_CONTOUR_PRESS_GAIN` | 0.34 | 按下时强度增量 | 倍率 | `GlassConfig.h:94` |
| `SHADOW_EXTENT_GAIN` | 2.0 | 投影绘制范围 = 半径×2 | 倍 | `GlassConfig.h:96` |
| `GLOW_REACH_SCALE` | 1.5 | 光斑半径 = 面板短边×1.5 | 倍 | `GlassConfig.h:97` |
| **`SPECTRAL_MAX`** | **2.0** | **色散强度上限（= 参考的 2 倍）** | **倍率** | `GlassConfig.h:98` |

### 1.4 `Render::` —— 色散棱镜权重（勿改）

`GlassPanel.cpp` 不写这些；shader 直接消费（`Shaders.cpp:346-365`）。

| 常量名 | 值（3 元组） | 控制的视觉 | 来源 |
| --- | --- | --- | --- |
| `PRISM_PAIRS` | 3 | 三对采样 | `GlassConfig.h:100` |
| `PRISM_DIST` | {1/3, 2/3, 1} | 三对采样距离 | `GlassConfig.h:101` |
| `PRISM_EVEN_RED` | {3/21, 3/21, 9/42} | 红：对称分量权重 | `GlassConfig.h:102` |
| `PRISM_ODD_RED` | {3/21, 3/21, 3/42} | 红：反对称（前移）分量 | `GlassConfig.h:103` |
| `PRISM_EVEN_GREEN` | {6/21, 3/42, 0} | 绿：对称分量 | `GlassConfig.h:104` |
| `PRISM_ODD_GREEN` | {0, 3/42, 0} | 绿：反对称分量 | `GlassConfig.h:105` |
| `PRISM_EVEN_BLUE` | {7/42, 7/42, 7/42} | 蓝：对称分量 | `GlassConfig.h:106` |
| `PRISM_ODD_BLUE` | {−7/42, −7/42, −7/42} | 蓝：反对称（后移）分量 | `GlassConfig.h:107` |
| `PRISM_CENTER_GREEN` | 6/21 | 中心 tap 的绿权重 | `GlassConfig.h:108` |

### 1.5 `Render::` —— 阈值 / 混合 / 染色

| 常量名 | 值 | 控制的视觉 | 来源 |
| --- | --- | --- | --- |
| `PANEL_OPACITY_MAX` | 1.0 | 面板不透明度上限 | `GlassConfig.h:110` |
| `TRACK_NEUTRAL_ALPHA` | 0.20 | 滑轨中性色 alpha | `GlassConfig.h:111` |
| `SPECTRAL_MIN` | 0.001 | 低于此值视为无色散 | `GlassConfig.h:112` |
| `ALPHA_MIN` | 0.001 | 通用 alpha 阈值 | `GlassConfig.h:113` |
| `CONTENT_ALPHA_MIN` | 0.004 | 内容绘制阈值 | `GlassConfig.h:114` |
| `SURFACE_ALPHA_MIN` | 0.005 | 表面染色启用阈值 | `GlassConfig.h:115` |
| `GLOW_LEVEL_MIN` | 0.001 | 光斑启用阈值 | `GlassConfig.h:118` |
| **`EDGE_TINT`** | **0x80FFFFFF** | 默认边缘光：白 α=0.502 | `GlassConfig.h:126` |
| `EDGE_TINT_PLAIN` | 0x61FFFFFF | Plain：白 α=0.380 | `GlassConfig.h:127` |
| `EDGE_TINT_AMBIENT` | 0x61FFFFFF | Ambient：白 α=0.380 | `GlassConfig.h:128` |
| `EDGE_TINT_CONTOUR` | 0xCCFFFFFF | Contour：白 α=0.800 | `GlassConfig.h:129` |
| `EDGE_TINT_DIALOG` | 0xFFFFFF | 弹窗边缘光底 tint（alpha 由代码另给） | `GlassConfig.h:130` |
| `SHADOW_TINT_SOFT` | 0x0D000000 | 柔和投影色：黑 α=0.051 | `GlassConfig.h:122` |
| `SHADOW_OFFSET_DIVISOR` | 6.0 | 投影 Y 偏移 = 半径/6 | `GlassConfig.h:123` |
| `MENU_BAND` | 0x3DFFFFFF | 菜单高亮条：白 α=0.239 | `GlassConfig.h:131` |

### 1.6 `Color::` 命名空间 —— 色调派生（`ColorAdapter.cpp` 消费）

| 常量名 | 值 | 控制的视觉 | 来源 |
| --- | --- | --- | --- |
| `LUMA_BAND_LO` / `LUMA_BAND_HI` | 0.30 / 0.72 | 明暗判定的过渡带 | `GlassConfig.h:141-142` |
| `LUMA_FLIP` | 0.50 | 明/暗主题分界 | `GlassConfig.h:143` |
| `DARK_PRIMARY_*` | 0.07 / 0.08 / 0.10 | 深色主题主色 | `GlassConfig.h:145-147` |
| `DARK_SECONDARY_*` | 0.11 / 0.12 / 0.16 | 深色主题次色 | `GlassConfig.h:148-150` |
| `LIGHT_PRIMARY_*` | 0.98 / 0.98 / 0.99 | 浅色主题主色 | `GlassConfig.h:151-153` |
| `SHEET_MIX_DARK/LIGHT` | 0.06 / 0.18 | 玻璃表面往白里混多少 | `GlassConfig.h:156-157` |
| `SHEET_PULL_DARK/LIGHT` | 0.20 / 0.10 | 表面色往主色拉多少 | `GlassConfig.h:158-159` |
| **`SHEET_ALPHA_DARK/LIGHT`** | **0.34 / 0.62** | **表面染色的 alpha（决定"霜感"）** | `GlassConfig.h:160-161` |
| `SCRIM_GAIN` | 0.42 | 背景压暗层的色增益 | `GlassConfig.h:167` |
| `SCRIM_ALPHA_DARK/LIGHT` | 0.56 / 0.30 | 背景压暗层 alpha | `GlassConfig.h:168-169` |
| `EDGE_MIX_DARK/LIGHT` | 0.55 / 0.62 | 边缘光往"对极色"混多少 | `GlassConfig.h:175-176` |
| `SPECTRUM_GAIN` / `SPECTRUM_MIX` | 1.0 / 0.45 | 关闭动画的色环颜色 | `GlassConfig.h:183-184` |
| **`REFRACT_GAIN`** | **0.22** | **`refractTint.a = tintDeviation × 0.22`（上限，不是固定值）** | `GlassConfig.h:185` |
| `BRIGHTNESS_DARK/LIGHT` | **0.0** / 0.04 | `toneCtrl.x` 主题偏置 | `GlassConfig.h:187-188` |
| `SATURATION_DARK/LIGHT` | 1.0 / 1.0 | `toneCtrl.z` 主题偏置 | `GlassConfig.h:189-190` |
| `CONTRAST_DARK/LIGHT` | 1.0 / 1.0 | `toneCtrl.y` 主题偏置 | `GlassConfig.h:191-192` |
| `BUTTON_SHEET_ALPHA_DARK/LIGHT` | 0.22 / 0.36 | 按键表面染色 alpha | `GlassConfig.h:198-199` |

> 注意 `REFRACT_GAIN` 的语义（`ColorAdapter.cpp:234-237`）：
> `tintRefract() = Rgba(g_primary.rgb, tintDeviation() × 0.22)`。
> 默认深色主题下 `tintDeviation() = 0`（`ColorAdapter.cpp:147-155`），所以 **参考实现的 `refractTint.a` 恒为 0**，0.22 只是「主题色偏离中性色到最大时」的上限。

### 1.7 `Layout::` —— 按键 / 开关 / 滑块

| 常量名 | 值 | 控制的视觉 | 480dpi 换算 | 来源 |
| --- | --- | --- | --- | --- |
| `BUTTON_BLUR_DP` | 2.0 | 按键模糊 | 6px | `GlassConfig.h:406` |
| `GLASS_BLUR_DP` | 4.0 | 通用玻璃模糊（底部面板类） | 12px | `GlassConfig.h:407` |
| `GLASS_BLUR_FLOOR_DP` | 1.0 | 滑块/开关静止时的最小模糊 | 3px | `GlassConfig.h:408` |
| `BUTTON_LENS_HEIGHT_DP` | 12.0 | 按键透镜高度 | 36px | `GlassConfig.h:404` |
| `BUTTON_LENS_AMOUNT_DP` | 24.0 | 按键透镜位移 | 72px | `GlassConfig.h:405` |
| `BUTTON_GLOW_RISE_DP` | 4.0 | 按键按下的辉光抬升 | 12px | `GlassConfig.h:409` |
| `BUTTON_DRAG_TANH` | 0.05 | 拖拽初始导数（tanh 映射） | 无量纲 | `GlassConfig.h:410` |
| `TOGGLE_LENS_HEIGHT_DP` | 5.0 | 开关滑钮透镜高度 | 15px | `GlassConfig.h:423` |
| `TOGGLE_LENS_AMOUNT_DP` | 10.0 | 开关滑钮透镜位移 | 30px | `GlassConfig.h:424` |
| `TOGGLE_INNER_DP` | 4.0 | 开关内阴影半径 | 12px | `GlassConfig.h:432` |
| `TOGGLE_DARK_GUARD` | **1.0** | **唯一使用暗部保护的组件** → `job.darkGuard = 0.045` | 倍率 | `GlassConfig.h:436` |
| `SLIDER_LENS_HEIGHT_DP` | 12.0 | 滑块手柄透镜高度 | 36px | `GlassConfig.h:441` |
| `SLIDER_LENS_AMOUNT_DP` | 14.0 | 滑块手柄透镜位移 | 42px | `GlassConfig.h:442` |
| `SLIDER_INNER_DP` | 4.0 | 滑块内阴影半径 | 12px | `GlassConfig.h:454` |
| `SLIDER_SHADOW_DP` | 4.0 | 滑块投影半径 | 12px | `GlassConfig.h:453` |

### 1.8 `Layout::` —— 标签栏

| 常量名 | 值 | 控制的视觉 | 480dpi | 来源 |
| --- | --- | --- | --- | --- |
| `TAB_PANEL_H_DP` | 64.0 | 标签栏面板高 | 192px | `GlassConfig.h:456` |
| `TAB_INNER_H_DP` | 56.0 | 内层指示器高 | 168px | `GlassConfig.h:457` |
| `TAB_INSET_DP` | 4.0 | 内缩 | 12px | `GlassConfig.h:458` |
| `TAB_CELL_SCALE` | 1.2 | 按下时图标/文字放大 | 倍率 | `GlassConfig.h:461` |
| **`TAB_BLUR_DP`** | **8.0** | **标签栏模糊** | **24px** | `GlassConfig.h:465` |
| `TAB_LENS_HEIGHT_DP` | 24.0 | 标签栏面板透镜高度 | 72px | `GlassConfig.h:466` |
| `TAB_LENS_AMOUNT_DP` | 24.0 | 标签栏面板透镜位移（1:1） | 72px | `GlassConfig.h:467` |
| `TAB_PRESS_LENS_HEIGHT_DP` | 10.0 | 指示器透镜高度 | 30px | `GlassConfig.h:468` |
| `TAB_PRESS_LENS_AMOUNT_DP` | 14.0 | 指示器透镜位移 | 42px | `GlassConfig.h:469` |
| **`TAB_SPECTRAL`** | **1.0** | **指示器色散（等价参考的 chromaticAberration=true）** | 倍率 | `GlassConfig.h:474` |
| `TAB_IDLE_ALPHA` | 0.10 | 静止白罩 alpha | 倍率 | `GlassConfig.h:472` |
| `TAB_PRESS_ALPHA` | 0.03 | 按下白罩 alpha | 倍率 | `GlassConfig.h:473` |
| `TAB_CONTAINER` / `_LIGHT` | 0x66121212 / 0x66FAFAFA | 表面染色：深/浅各 α=0.4 | — | `GlassConfig.h:477-478` |
| `TAB_SHADOW_DP` / `_ALPHA` | 24.0 / 0.10 | 投影半径/强度 | 72px | `GlassConfig.h:479-480` |
| `TAB_PRESS_INNER_DP` | 8.0 | 按下内阴影半径 | 24px | `GlassConfig.h:481` |

### 1.9 `Layout::` —— 弹窗（Dialog）★主力面板 A

| 常量名 | 值 | 控制的视觉 | 480dpi | 来源 |
| --- | --- | --- | --- | --- |
| `DIALOG_MAX_W_DP` | 420.0 | 最大宽度 | 1260px | `GlassConfig.h:483` |
| `DIALOG_MIN_W_DP` | 260.0 | 最小宽度 | 780px | `GlassConfig.h:484` |
| `DIALOG_SIDE_DP` | 80.0 | 左右安全边距 | 240px | `GlassConfig.h:485` |
| `DIALOG_H_DP` | 252.0 | 面板高度 | 756px | `GlassConfig.h:486` |
| `DIALOG_PAD_X_DP` / `BODY_PAD_X_DP` | 28.0 / 24.0 | 文字左右内缩 | 84 / 72px | `GlassConfig.h:489-490` |
| **`DIALOG_CLOSE_RADIUS_DP`** | **48.0** | **静止圆角半径** | **144px** | `GlassConfig.h:501` |
| **`DIALOG_GLASS_BLUR_DP`** | **10.0** | **弹窗模糊（= `BLUR_RADIUS_MAX_DP`，即上限）** | **30px** | `GlassConfig.h:502` |
| **`DIALOG_LENS_HEIGHT_DP`** | **24.0** | **透镜高度** | **72px** | `GlassConfig.h:503` |
| **`DIALOG_LENS_AMOUNT_DP`** | **48.0** | **透镜位移** | **144px → 被 `LENS_REACH_MAX` 截到 128px** | `GlassConfig.h:504` |
| `DIALOG_FLARE_LENS_HEIGHT_DP` | 16.0 | 形变瞬间额外透镜高度 | 48px | `GlassConfig.h:505` |
| `DIALOG_FLARE_LENS_AMOUNT_DP` | 24.0 | 形变瞬间额外透镜位移 | 72px | `GlassConfig.h:506` |
| **`DIALOG_SPECTRAL_BASE`** | **0.12** | **静止色散强度** | 倍率 | `GlassConfig.h:507` |
| `DIALOG_SPECTRAL_FLARE` | 0.60 | 形变瞬间色散 | 倍率 | `GlassConfig.h:508` |
| `DIALOG_SPECTRAL_CLOSE` | 0.55 | 关闭时色散 | 倍率 | `GlassConfig.h:509` |
| `DIALOG_SPECTRAL_MIN` | 0.002 | 色散启用阈值 | 倍率 | `GlassConfig.h:510` |
| **`DIALOG_EDGE_ALPHA`** | **0.62** | **边缘光 alpha（与 tint alpha 0.62 相乘 → 有效 0.384）** | 倍率 | `GlassConfig.h:511` |
| `DIALOG_EDGE_FLARE_ALPHA` | 0.30 | 形变时边缘光增量 | 倍率 | `GlassConfig.h:512` |
| **`DIALOG_SHEET_ALPHA_SCALE`** | **0.72** | **表面染色 alpha 缩放：0.34×0.72 = 0.245（深色）** | 倍率 | `GlassConfig.h:513` |
| `DIALOG_SHADOW_DP` / `_ALPHA` | 19.0 / 0.11 | 投影半径 / 强度 | 57px | `GlassConfig.h:514-515` |
| `DIALOG_RING_*` / `HALO_*` | 3.0/8.0/0.30/0.055/1.2/26.0/0.05/6.0 | 关闭时的色散环与光晕 | — | `GlassConfig.h:516-524` |
| `DIALOG_BUTTON_BLUR_IDLE/PRESS_DP` | 6.0 / 2.0 | 弹窗按钮模糊 | 18 / 6px | `GlassConfig.h:526-527` |
| `DIALOG_BUTTON_LENS_IDLE/PRESS_DP` | 9.0 / 4.0 | 弹窗按钮透镜高度 | 27 / 12px | `GlassConfig.h:528-529` |
| `DIALOG_BUTTON_LENS_AMOUNT_IDLE/PRESS_DP` | 18.0 / 8.0 | 弹窗按钮透镜位移 | 54 / 24px | `GlassConfig.h:530-531` |
| `DIALOG_BUTTON_SPECTRAL_IDLE/PRESS` | 0.22 / 0.45 | 弹窗按钮色散 | 倍率 | `GlassConfig.h:532-533` |

### 1.10 `Layout::` —— 弹出菜单（Menu）★主力面板 B

| 常量名 | 值 | 控制的视觉 | 480dpi | 来源 |
| --- | --- | --- | --- | --- |
| **`MENU_DEFAULT_W_DP`** | **200.0** | **菜单默认宽度（是我方 620×900px 面板的尺寸孪生）** | **600px** | `GlassConfig.h:566` |
| `MENU_DEFAULT_CORNER_DP` | 32.0 | 菜单圆角 | 96px | `GlassConfig.h:567` |
| `MENU_DEFAULT_BAND_CORNER_DP` | 24.0 | 高亮条圆角 | 72px | `GlassConfig.h:568` |
| `MENU_SLOT_DP` | 44.0 | 行高 | 132px | `GlassConfig.h:551` |
| `MENU_PAD_Y_DP` / `SIDE_PAD_DP` | 12.0 / 12.0 | 上下/左右内边距 | 36px | `GlassConfig.h:547,549` |
| `MENU_MIN_W_DP` / `MIN_H_DP` | 80.0 / 80.0 | 最小尺寸 | 240px | `GlassConfig.h:564-565` |
| `MENU_CAP_BLUR_DP` | 2.0 | 收起态（胶囊）模糊 | 6px | `GlassConfig.h:579` |
| **`MENU_BLUR_HI_DP`** | **12.0** | **展开态模糊** | **36px** | `GlassConfig.h:586` |
| `MENU_CAP_LENS_HEIGHT_DP` | 12.0 | 收起态透镜高度 | 36px | `GlassConfig.h:580` |
| **`MENU_LENS_HI_DP`** | **11.0** | **展开态透镜高度** | **33px** | `GlassConfig.h:587` |
| `MENU_CAP_LENS_AMOUNT_DP` | 24.0 | 收起态透镜位移 | 72px | `GlassConfig.h:581` |
| **`MENU_LENS_AMOUNT_HI_DP`** | **22.0** | **展开态透镜位移** | **66px** | `GlassConfig.h:588` |
| `MENU_CAP_SPECTRAL` | 0.0 | 收起态色散 = 无 | 倍率 | `GlassConfig.h:582` |
| **`MENU_SPECTRAL_HI`** | **0.55** | **展开态色散** | 倍率 | `GlassConfig.h:589` |
| `MENU_FLARE_SPECTRAL` | 0.85 | 形变瞬间色散增量 | 倍率 | `GlassConfig.h:600` |
| `MENU_FLARE_LENS_HEIGHT/AMOUNT_DP` | 16.0 / 24.0 | 形变瞬间额外透镜 | 48 / 72px | `GlassConfig.h:598-599` |
| `MENU_FLOW_GATE_SPAN` | 0.28 | 「胶囊→面板」材质切换的进度窗 | 无量纲 | `GlassConfig.h:590` |
| `MENU_RIM_WIDTH_DP` / `SOFTNESS_DP` | 1.0 / 0.6 | 展开态轮廓光宽/柔 | 3 / 1.8px | `GlassConfig.h:591-592` |
| `MENU_RIM_ANGLE_DEGREES` | −90.0 | 轮廓光朝上 | 度 | `GlassConfig.h:593` |
| `MENU_RIM_FALLOFF` | 1.35 | 轮廓光衰减指数 | 指数 | `GlassConfig.h:594` |
| **`MENU_RIM_ALPHA`** | **0.52** | **展开态边缘光强度（× tint α0.8 → 有效 0.416 上限）** | 倍率 | `GlassConfig.h:595` |
| `MENU_RIM_GREET_BOOST` / `FLARE_BOOST` | 0.24 / 0.32 | 展开/形变时的增量 | 倍率 | `GlassConfig.h:596-597` |
| `MENU_CAP_EDGE_ALPHA` / `WIDTH_DP` / `SOFTNESS_DP` | 0.50 / 0.6 / 0.30 | 收起态边缘光 | — | `GlassConfig.h:583-585` |
| `MENU_IDLE_ALPHA` | 0.90 | 行文字静止 alpha | 倍率 | `GlassConfig.h:606` |
| `MENU_BAND_IDLE` | 0.62 | 高亮条静止 alpha | 倍率 | `GlassConfig.h:610` |
| `MENU_SIDE_MARGIN_DP` | 16.0 | 菜单距宿主边距 | 48px | `GlassConfig.h:563` |

### 1.11 其他

| 常量名 | 值 | 说明 | 来源 |
| --- | --- | --- | --- |
| `CORNER_ARC_TANGENT` | 0.5522847498 | 圆弧→贝塞尔的切线长（普通圆角，**非**连续曲率） | `GlassConfig.h:616` |
| `DEGREES_TO_RADIANS` | 0.017453292 | 角度换算 | `GlassConfig.h:44` |
| `DENSITY_MIN/MAX` | 0.5 / 8.0 | `setScale()` 的钳位范围 | `GlassConfig.h:41-42` |
| `CAPSULE_RADIUS` | 3.4028235e38 | 胶囊圆角的哨兵值 | `GlassConfig.h:43` |

---

## 二、主力玻璃面板的推荐配方

### 2.1 谁是「面积最大的面板」

| 候选 | 尺寸（dp） | 面积（dp²） | 说明 |
| --- | --- | --- | --- |
| **Dialog** | `min(宿主宽−80, 420) × 252` | 1080px/3 = 360dp 屏上 **280×252 = 70,560**；411dp 屏上 **331×252 = 83,412** | 有**独立完整**的一套 `DIALOG_*` 视觉常量（`:501-524`，共 24 个），且与 Kyant0 `DialogContent.kt` 逐项可对齐 |
| Menu（展开，6 行） | 200 × ≈298 | 59,600 | |
| Menu（展开，12 行满） | 200 × ≈574 | 114,800 | 行数可变，无固定面积 |
| TabStrip | 全宽 × 64 | 411×64 = 26,304 | |
| PillButton | ≈160 × 48 | 7,680 | |

**结论：Dialog 是「面积最大的固定尺寸玻璃面板」，取它作主力配方 A。**
理由不只是面积 —— 它是 ytbl 里唯一在 `GlassConfig.h` 有 24 个专属视觉常量的组件，也是唯一能被 Kyant0 上游源码逐项验证的组件。

**但还有一个关键的尺寸巧合**：我方面板 620×900px，480dpi 下 = **206.7 × 300 dp**，
而 `MENU_DEFAULT_W_DP = 200` —— 几乎完全一致。所以 **Menu 展开态（配方 B）才是尺寸上的孪生参照**，它的「液态」特征也最强（`depthAmount=1.0` + `spectral=0.55` + 轮廓光）。

**最终建议：以 A 的几何（圆角/透镜/模糊）为骨架，叠加 B 的色彩与轮廓光。**

### 2.2 配方 A：Dialog 静止态（源码明确，逐项可查）

`Dialog.cpp:303-348` 在 `settle = 1.0`（`curve()` 是 smoothstep，`Dialog.cpp:44-49`）时的取值：

| 项 | 计算式 | 值（dp） | 480dpi px | 依据 |
| --- | --- | --- | --- | --- |
| 圆角 | `px(DIALOG_CLOSE_RADIUS_DP)`，再钳到 `min(w,h)/2` | 48 | **144** | `Dialog.cpp:283-287` / `GlassConfig.h:501` |
| 模糊 | `px(glassBlurDp)`，`glassBlurDp` 默认 = `DIALOG_GLASS_BLUR_DP` | 10 | **30** | `Dialog.cpp:305` / `GlassUI.h:577` / `GlassConfig.h:502` |
| 透镜高度 | `px(DIALOG_LENS_HEIGHT_DP) × lensStage(=1)` | 24 | **72** | `Dialog.cpp:315-317` |
| 透镜位移 | `px(DIALOG_LENS_AMOUNT_DP) × lensStage` | 48 | **144 → 截 128** | `Dialog.cpp:320` / `GlassPanel.cpp:238` |
| 透镜深度 | `depthAmount = 1.0f`（写死） | 1.0 | 1.0 | `Dialog.cpp:323` |
| 色散 | `0.12 × smoothstep(settle, 0.55, 1.00)` = 0.12×1 | 0.12 | 0.12 | `Dialog.cpp:306-309,324` |
| 面板不透明度 | `style.alpha = material = 1.0` | 1.0 | 1.0 | `Dialog.cpp:303,348` / `GlassPanel.cpp:245-248` |
| 暗部保护 | 未设置 → `optics.darkGuard = 0` | **0** | 0 | `GlassUI.h:306` / `GlassPanel.cpp:241` |
| 色调 | `toneCtrl = (tintBrightness, 1×1, 1×1)`；深色 = (0,1,1)，浅色 = (0.04,1,1) | (0,1,1) 深 | 同 | `GlassPanel.cpp:249-251` / `ColorAdapter.cpp:239-252` |
| 饱和提升 | `colorBoost = false` → `toneBoost = 0` | 0 | 0 | `Dialog.cpp:314` / `GlassPanel.cpp:252` |
| 折射染色 | `tintRefract()`：默认深色主题下 alpha = **0** | α=0 | 0 | `ColorAdapter.cpp:234-237` |
| 表面染色 | `tintSheet()` 色 @ `0.34 × 1 × 0.72 = 0.2448`，**Overlay 模式（2）** | α=0.245 | 同 | `Dialog.cpp:344-347` / `ColorAdapter.cpp:167-181` |
| 边缘光 | Gradient 模式，角 45°，falloff 1，宽 0.5dp，柔 0.25dp，α = 0.62，tint α = 0.62 → 有效 **0.384** | 有效 α 0.384 | 宽 1.5→2px、柔 0.75px | `Dialog.cpp:326-336` / `GlassPanel.cpp:116-125` |
| 投影 | 半径 19dp，Y 偏移 19/6，黑 α=0.11 | 19 | 57px | `Dialog.cpp:340-343` |

### 2.3 配方 B：Menu 展开态（`material ≥ 0.62`，`grade → 1`）

`PopupMenu.cpp:163-217`，`grade = curveHold(material)`（`Runtime.cpp:254-256`）：

| 项 | 计算式 | 值 | 480dpi px | 依据 |
| --- | --- | --- | --- | --- |
| 圆角 | `px(cornerDp)`，默认 `MENU_DEFAULT_CORNER_DP` | 32 dp | **96** | `PopupMenu.cpp:281` / `GlassConfig.h:567` |
| 模糊 | `px(lerp(2, 12, grade))` | 12 dp | **36** | `PopupMenu.cpp:176-178` |
| 透镜高度 | `px(lerp(12, 11, grade))` | 11 dp | **33** | `PopupMenu.cpp:179-182` |
| 透镜位移 | `px(lerp(24, 22, grade))` | 22 dp | **66** | `PopupMenu.cpp:183-186` |
| 透镜深度 | `depthAmount = grade` | **1.0** | 1.0 | `PopupMenu.cpp:187` |
| 色散 | `lerp(0.0, 0.55, grade)` | **0.55** | 0.55 | `PopupMenu.cpp:188-190` |
| 饱和提升 | `colorBoost = true` → shader `ctrl.z × 1.5` | 1.5 | — | `PopupMenu.cpp:172` / `Shaders.cpp:370` |
| 表面染色 | `tintSheet()`，α = `tintSheetAlpha()` = 深 0.34 / 浅 0.62，Overlay | 0.34 | — | `PopupMenu.cpp:191-197` / `ColorAdapter.cpp:178-181` |
| 边缘光 | **Contour**，角 −90°，falloff 1.35，宽 1.0dp，柔 0.6dp，α = 0.52 | 有效 ≤0.416 | 3 / 1.8px | `PopupMenu.cpp:200-213` |
| 投影 / 内阴影 | `shadowOn = false`、`insetOn = false` | 无 | — | `PopupMenu.cpp:214-215` |

### 2.4 其它组件速查（同一套常量的不同组合）

| 组件 | 模糊 | 透镜高 | 透镜位移 | depth | 色散 | 圆角 | 来源 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| TabBar 面板 | 8dp | 24dp | 24dp | 0 | 0 | 胶囊 | `TabBar.cpp:282-285` |
| TabBar 指示器 | 0 | 10dp | 14dp | 0 | **1.0** | 胶囊 | `TabBar.cpp:357-361` |
| PillButton | 2dp | 12dp | 24dp | 0 | 0（默认） | 胶囊 | `PillButton.cpp:72-77` |
| Toggle 滑钮 | 1→4dp | 5dp | 10dp | 0 | **1.0** | 胶囊 | `Toggle.cpp:159-165` |
| Slider 手柄 | 1→4dp | 12dp | 14dp | 0 | 0 | 胶囊 | `Slider.cpp:193-198` |
| Dialog 按钮 | 6→2dp | 9→4dp | 18→8dp | 0 | 0.22→0.45 | 胶囊 | `Dialog.cpp:451-459` |

**注意：只有小尺寸组件（指示器 / 滑钮 / 手柄 / 弹窗按钮）用 `spectral = 1.0` 这一档；大面板一律 ≤ 0.55，弹窗只有 0.12。** 这是第 5 节的一条核心依据。

---

## 三、与 Kyant0 原实现的对比

### 3.1 几何/光学主表

| 视觉项 | ytbl（主力配方 A：Dialog） | Kyant0 原实现 | 一致？ |
| --- | --- | --- | --- |
| 模糊半径 | 10 dp（`GlassConfig.h:502`） | 深色 **8dp** / 浅色 **16dp**（`DialogContent.kt:66`） | ⚠️ 取了中间值；深色偏大 25% |
| 透镜高度 | 24 dp（`:503`） | `refractionHeight = 24.dp.toPx()`（`DialogContent.kt:67`） | ✅ **完全一致** |
| 透镜位移 | 48 dp（`:504`） | `refractionAmount = 48.dp.toPx()`（`DialogContent.kt:67`） | ✅ **完全一致** |
| 深度效应 | `depthAmount = 1.0`（`Dialog.cpp:323`） | `depthEffect = true`（`DialogContent.kt:67`） | ✅ 一致 |
| 色散 | 0.12（`:507`） | **关闭**（`DialogContent.kt:67` 未传 `chromaticAberration`） | ⚠️ ytbl 额外加了 0.12 |
| 圆角半径 | 48 dp（`:501`） | `RoundedRectangle(48f.dp)`（`DialogContent.kt:60`） | ✅ **完全一致** |
| 饱和/亮度 | `colorBoost=false` → 饱和 1.0；亮度 +0.04（浅） | `colorControls(brightness = 0(深)/0.2(浅), saturation = **1.5**)`（`DialogContent.kt:62-65`） | ❌ **ytbl 弹窗丢了 vibrancy** |
| 表面色调 | `tintSheet()` @ 0.245 深 / 0.446 浅（`:513` × `:160`） | `#121212 @0.4`（深）/ `#FAFAFA @0.6`（浅）（`DialogContent.kt:42-47,70`） | ⚠️ 结构一致、深浅不同 |
| 边缘光几何 | 宽 0.5dp / 柔 0.25dp（`Dialog.cpp:332-333`） | `Highlight.width = 0.5dp`、`blurRadius = width/2 = 0.25dp`（`Highlight.kt:11-12`） | ✅ **完全一致** |
| 边缘光颜色/强度 | Gradient 45° falloff 1，有效 α = 0.62×0.62 = **0.384** | `Highlight.Plain`：白 α=0.38，`BlendMode.Plus`（`HighlightStyle.kt:33-42`） | ✅ 数值一致（但 ytbl 换成方向光，见 3.3） |
| 投影 | 19dp / 偏移 r/6 / 黑 α0.11 | `Shadow(radius=24dp, offset=(0, r/6), black α0.1)`（`Shadow.kt:14-20`） | ✅ 公式一致，半径 19 vs 24 |
| 内阴影 | 仅按下时启用，偏移 **0**（`GlassUI.h:289-290`） | `InnerShadow(24dp, offset=(0, radius), black α0.15)`（`InnerShadow.kt:17-23`） | ⚠️ ytbl 把偏移吃了 |
| 暗部保护 | `DARK_GUARD_FLOOR=0.045`，但**弹窗不用**（`GlassPanel.cpp:241`） | **未找到**——Kyant0 全库无对应项 | ➖ ytbl 独有 |
| 折射染色 | `REFRACT_GAIN=0.22`，默认主题下 **α=0** | **未找到** | ➖ ytbl 独有 |

### 3.2 透镜折射：公式级对比 —— 数学等价

**Kyant0**（`Shaders.kt:63-83`）：

```
circleMap(x)  = 1 - sqrt(1 - x*x)
d             = circleMap(1 - (-sd)/refractionHeight) * refractionAmount
gradRadius    = min(radius * 1.5, min(halfSize.x, halfSize.y))
grad          = normalize(gradSdRoundedRect(centered, halfSize, gradRadius)
                          + depthEffect * normalize(centered))
refractedCoord = coord + d * grad
```
注意 `Lens.kt:49` 传的是 `-refractionAmount`，所以位移方向朝内。

**ytbl**（`Shaders.cpp:205-208, 324-342`）：

```
lensCurve(x) = clamp(x,0,1) * tan(asin(clamp(x,0,1)) * 0.5)
lensT        = 1 - (-insideDepth / lensHeight)
lensShift    = lensCurve(lensT) * (-lensAmount)
lensDir      = unitOrZero(boxNormal(...) + uLensDepth * unitOrZero(centered))
sampleCoord  = vLocal + lensShift * lensDir
```

**两条结论（这是我最想强调的一处「其实没写错」）：**

1. **曲线完全等价。** 令 `θ = asin t`，则 `tan(θ/2) = t/(1+√(1−t²))`，乘 `t` 得 `t²/(1+√(1−t²))`；而 `circleMap(t) = 1−√(1−t²)` 同乘 `(1+√(1−t²))` 也等于 `t²/(1+√(1−t²))`。
   **即 `lensCurve(x) ≡ circleMap(x)`，解析恒等。** 【推断，已代数验证】取值抽查：t=0 → 0/0；t=0.5 → 0.1340/0.1340；t=1 → 1/1。
2. **深度偏置、方向归一化、朝内位移都逐字对应。**

**唯一实质差异：法线场的圆角半径。**

| | 半径取值 |
| --- | --- |
| Kyant0 | `gradRadius = min(radius × 1.5, min(halfSize))`（`Shaders.kt:79,116`） |
| ytbl | `bodyRound = min(radius, min(bodyHalf))`（`Shaders.cpp:329`）—— **没有 ×1.5** |

对胶囊（radius = 短边一半）两者相同；对普通圆角矩形，Kyant0 用 1.5 倍半径算 SDF 法线，
会让「轴对齐直边法线 → 圆角径向法线」的过渡区更靠内、圆角处的折射方向转得更缓。
ytbl 用真半径，圆角附近的折射方向切换更「硬」。**这是一个真实存在的观感差异，但 ytbl 的常量里没有对应开关**——未找到可调项。

### 3.3 色散：ytbl 是 Kyant0 的代数重写，数值恒等

**Kyant0**（`Shaders.kt:120-157`）7 抽样：

```
dispersionIntensity = chromaticAberration * ((centered.x * centered.y) / (halfSize.x * halfSize.y))
dispersedCoord      = d * grad * dispersionIntensity
红 = eval(+1)/3.5 + eval(+2/3)/3.5 + eval(+1/3)/3.5 + eval(-1)/7
绿 = eval(+2/3)/7 + eval(+1/3)/3.5 + eval(0)/3.5 + eval(-1/3)/3.5
蓝 = eval(-1/3)/3 + eval(-2/3)/3 + eval(-1)/3
a  = 7 个抽样各 /7
```

**ytbl**（`Shaders.cpp:346-365`）把它重写成「成对 + 反对称」形式：

```
strength = uSpectral * clamp((centered.x*centered.y)/denom, -1, 1)
stride   = lensShift * lensDir * strength
pair = ahead + behind ;  skew = ahead - behind
```

**逐通道展开后与 Kyant0 完全一致**【推断，已代数验证】：

- 红：`+1/3`、`+2/3`、`+1` 各得权重 2/7，`-1` 得 1/7 —— 对应 Kyant0 的三个 1/3.5 加一个 1/7 ✅
- 绿：中心 2/7，`±1/3` 各 2/7，`+2/3` 得 1/7 —— 对应 Kyant0 的 /3.5、/3.5、/3.5、/7 ✅
- 蓝：`-1/3`、`-2/3`、`-1` 各 1/3 —— ✅
- alpha：7 抽样各 /7 —— ✅

**关键推论：`uSpectral = 1.0` 才是「与参考等价」，不是「开到满」。**
Kyant0 的 `chromaticAberration` 是 **bool**，开启时按 1.0 传入（`Lens.kt:51-53`）。
而 ytbl 让 `uSpectral` 同时承担**开关**和**位移缩放**（`strength = uSpectral × ...`），
所以 `uSpectral` 超过 1.0 是 ytbl 独有的超范围（`SPECTRAL_MAX = 2.0`）。
**`spectral = 1.0` 用在 620px 宽的大面板上，等于把小部件（指示器/滑钮）的档位放大到整块面板。**

### 3.4 边缘光：ytbl 的模式划分与 Kyant0 的对应关系

Kyant0 的高光是**描边**：`paint.style = Stroke`，`strokeWidth = ceil(width.toPx()) × 2`，
但画的时候 `clipOutline` 到形状内 → 实际只剩内侧一半，即宽度 = `Highlight.width`（`HighlightModifier.kt:151-154, 119-127`）。默认 `Highlight.Default`（`DrawBackdropModifier.kt:41,84`）。

| ytbl `uEdgeMode` | shader 分支 | 对应 Kyant0 | 差异 |
| --- | --- | --- | --- |
| 1.0 Gradient | `gate = step(0, align)`，加法 | `HighlightStyle.Default`（`HighlightStyle.kt:45-71`）：`intensity = pow(abs(d), falloff)` | Kyant0 取 **abs**（两侧对称亮），ytbl 取 **step**（只亮一侧） |
| 2.0 Ambient | `sharp = |align|^falloff`，`smoothstep(-0.15,0.35,align)` 门控，`mix(rgb, white, add)` | `HighlightStyle.Ambient`（`:74-100`）：`t = step(0,d)`，`return (t,t,t,1)*intensity` | ✅ 同为单侧，**忠实移植** |
| 3.0 Plain | `add = edgeAlpha × tintAlpha × band × insideMask`，均匀加法 | `HighlightStyle.Plain`（`:33-42`）：白 α0.38，`Plus` | ✅ **一致**，也是 Kyant0 弹窗/大面板实际用的（`DialogContent.kt:69`、`GlassPlaygroundContent.kt:79,135`、`AdaptiveLuminanceGlassContent.kt:110`） |
| 4.0 Contour | `rimDir = pow(clamp(lambert*0.5+0.5,0,1), falloff)`，`gain = mix(0.34, 1.0, rimDir)` | **未找到** | ytbl 自创：两侧都亮但有 0.34 地板值，用 `lensDir`（含 depth） |

**另外 ytbl 的三套预设是 Kyant0 的逐值搬运**：

| ytbl | 值 | Kyant0 对应 |
| --- | --- | --- |
| `EDGE_WIDTH_MIN` 0.5dp / `EDGE_SOFTNESS_MIN` 0.25dp | — | `Highlight()`：width 0.5dp、blurRadius width/2（`Highlight.kt:11-12`） |
| `EDGE_AMBIENT_WIDTH` 0.3333dp / `SOFTNESS` 0.1667dp | — | `Highlight.Ambient.width / 1.5f`（`LiquidToggle.kt:170-171`、`LiquidSlider.kt:179-180`） |
| `EDGE_TINT_PLAIN` 0x61FFFFFF（α0.380） | — | `HighlightStyle.Plain.color = White.copy(alpha = 0.38f)`（`HighlightStyle.kt:34`） |
| `EDGE_TINT` 0x80FFFFFF（α0.502） | — | `HighlightStyle.Default.color = White.copy(alpha = 0.5f)`（`:46`） |
| `EDGE_ANGLE_DEFAULT` 45 / `EDGE_FALLOFF_DEFAULT` 1 | — | `angle = 45f, falloff = 1f`（`:48-49`） |
| `DARK_GUARD_FLOOR` 0.045 × `TOGGLE_DARK_GUARD` 1.0 | — | **未找到** |

### 3.5 色彩：`toneCtrl` 与 Kyant0 `colorControls` 逐项等价

Kyant0（`ColorFilter.kt:47-75`）：

```
r = 0.213·(1-s) ; g = 0.715·(1-s) ; b = 0.072·(1-s)
out.R = (c·r + c·s)·R + c·g·G + c·b·B + t ,  t = (0.5 − 0.5c + brightness)·255
```

ytbl（`Shaders.cpp:272-281`）：

```
base = (0.213,0.715,0.072)·(1-saturation)
lift = 0.5 − contrast·0.5 + brightness
out  = dot(c, contrast·(base + (saturation,0,0))) + lift
```

展开后**完全一致**（仅 0–1 与 0–255 刻度差）【推断，已代数验证】。所以：

| ytbl 字段 | Kyant0 字段 |
| --- | --- |
| `toneCtrl.x` | `colorControls(brightness=)` |
| `toneCtrl.y` | `colorControls(contrast=)` |
| `toneCtrl.z` | `colorControls(saturation=)` |
| `toneBoost > 0.5` → `ctrl.z ×= 1.5`（`Shaders.cpp:370`） | `vibrancy()` = `colorControls(saturation = 1.5f)`（`ColorFilter.kt:41-45`） |
| `SurfaceMode::Overlay`（2） | `onDrawSurface = { drawRect(containerColor) }` |

**`vibrancy()` 在 Kyant0 里是几乎每块玻璃的标配**：`DialogContent.kt:62-65`、`ControlCenterContent.kt:118`、`LiquidBottomTabs.kt:170,208`、`LiquidButton.kt:59`、`GlassPlaygroundContent.kt:70,131`、`AdaptiveLuminanceGlassContent.kt:102`。ytbl 对应地在 Menu/Tab/Button 上开 `colorBoost = true`（`PopupMenu.cpp:172`、`TabBar.cpp:281`、`PillButton.cpp:72`），**只有弹窗 `Dialog.cpp:314` 关了**。

### 3.6 其他 Kyant0 侧的实测配方（供第 4 节引用）

| 场景 | 效果链 | 圆角 | 来源 |
| --- | --- | --- | --- |
| Control Center | `vibrancy()` + `lens(24dp, 48dp, depthEffect)`，**无 blur**；`Highlight(Default(angle=重力角, falloff=2))`；`onDrawSurface = drawRect(containerColor)` | itemShape | `ControlCenterContent.kt:99-124` |
| 底部标签栏面板 | `vibrancy()` + `blur(8dp)` + `lens(24dp, 24dp)`；高 64dp；`containerColor` | `Capsule()` | `LiquidBottomTabs.kt:166-185` |
| 标签指示器 | `lens(10dp×p, 14dp×p, chromaticAberration=true)`；`Highlight.Default.copy(alpha=p)` + `InnerShadow(8dp×p)` + `Shadow(alpha=p)` | `Capsule()` | `LiquidBottomTabs.kt:241-266` |
| 玻璃面板（playground） | `vibrancy()` + `blur(4dp)` + `lens(16dp, 32dp)` + `Highlight.Plain`；表面 `White α0.5` | `RoundedRectangle(32dp)` | `GlassPlaygroundContent.kt:127-138` |
| 自适应亮度面板 | `colorControls(brightness 0.1→0.5 / 0.1→−0.2, contrast 1→0, saturation 1.5)` + `blur(8→16dp / 8→2dp)` + `lens(24dp, minDimension/2, depth)` | `RoundedRectangle(24dp)` | `AdaptiveLuminanceGlassContent.kt:92-110` |
| 锁屏大面板 | `colorControls(−0.1, 0.75, 1.5)` + `blur(2dp)`；表面 `White α0.25`；宽 ≤400dp | `Rectangle` | `LockScreenContent.kt:59-73` |
| 按键 | `vibrancy()` + `blur(2dp)` + `lens(12dp, 24dp)` | `Capsule()` | `LiquidButton.kt:58-62` |
| 开关滑钮 | `blur(8dp×(1−p))` + `lens(5dp×p, 10dp×p, CA)` + `Highlight.Ambient` | `Capsule()` | `LiquidToggle.kt:158-174` |
| 滑块手柄 | `blur(8dp×(1−p))` + `lens(10dp×p, 14dp×p, CA)` + `Highlight.Ambient` | `Capsule()` | `LiquidSlider.kt:167-182` |
| 指示光斑 | `drawRect(White α0.08×p, Plus)` + 径向 `White α0.15×p`、`intensity = smoothstep(radius, radius/2, dist)`、`radius = minDimension × 1.5` | — | `InteractiveHighlight.kt:44-89` |
| 投影 | `Shadow(radius 24dp, offset (0, r/6), black α0.1)` | — | `Shadow.kt:14-20` |
| 内阴影 | `InnerShadow(24dp, offset (0, radius), black α0.15)` | — | `InnerShadow.kt:17-23` |

---

## 四、600×900px 面板 @480dpi 的调校建议

### 4.1 先把尺寸说清楚

| 量 | 值 |
| --- | --- |
| 面板 | 620×900 px（`probe_m3_ytbl.cpp:346-347`；任务描述写 600×900，取实际 620×900） |
| 屏幕 | 1080×2376 px（`:58-59`） |
| 假设密度 | 480dpi → **density = 3.0**，`px(1dp) = 3px` |
| 面板折算 | **206.7 × 300 dp** |
| 与参考的尺寸关系 | ≈ `MENU_DEFAULT_W_DP = 200dp` × 1.5（`GlassConfig.h:566`）；宽度约为弹窗上限 420dp 的一半 |

**换算表（density = 3.0）**

| dp | px |
| --- | --- |
| 0.25 | 0.75 |
| 0.5 | 1.5（`applyEdge` 会 `ceilf` → **2**） |
| 8 | 24 |
| 10 | 30 |
| 12 | 36 |
| 24 | 72 |
| 32 | 96 |
| 48 | 144 |

### 4.2 推荐配方（620×900px，density 3.0）

| 项 | 推荐值（px） | dp 换算 | 依据 |
| --- | --- | --- | --- |
| `uBlurRadius` | **30** | 10 dp | `DIALOG_GLASS_BLUR_DP = 10.0`（`GlassConfig.h:502`），同时正好 = `BLUR_RADIUS_MAX_DP` 上限（`:69`）。上游 Kyant0 深色弹窗用 8dp、浅色 16dp（`DialogContent.kt:66`） |
| （备选）`uBlurRadius` | 24 / 36 | 8 / 12 dp | 24 = Kyant0 深色弹窗原值；36 = `MENU_BLUR_HI_DP`（`:586`）。**不建议超过 36** |
| `uCornerRadii`（四角同值） | **144** | 48 dp | `DIALOG_CLOSE_RADIUS_DP = 48.0`（`:501`）＝ Kyant0 `RoundedRectangle(48f.dp)`（`DialogContent.kt:60`）。要更「面板感」可用 96（32dp，`:567`＋`GlassPlaygroundContent.kt:129`）。上限 = `min(620,900)/2 = 310` |
| `uLensHeight` | **72** | 24 dp | `DIALOG_LENS_HEIGHT_DP = 24.0`（`:503`）＝ Kyant0 `refractionHeight = 24.dp.toPx()`（`DialogContent.kt:67`） |
| `uLensAmount` | **128** | 48 dp（理论 144，被截） | `DIALOG_LENS_AMOUNT_DP = 48.0`（`:504`）＝ Kyant0 `refractionAmount = 48.dp.toPx()`。⚠️ `GlassPanel.cpp:238` 先 `clampRange(optics.lensAmountPx, 0, LENS_REACH_MAX=128)` 再乘 `uniformPixelScale`，所以 144 会**静默截到 128** |
| （若 128 显得过强）`uLensAmount` | 90 | ≈29 dp | 【推断】按参考的相对强度折算：Kyant0 弹窗位移/面板宽 = 48dp / 331dp = **14.5%**；620px × 14.5% = **90px**。**请保持 `lensAmount ≈ 2 × lensHeight`**，这是参考的主导比例（24:48、11:22、12:24、5:10、16:32、10:14、24:min/2） |
| `uLensDepth` | **1.0** | — | `Dialog.cpp:323` 写死 `depthAmount = 1.0f`；Kyant0 `depthEffect = true`（`DialogContent.kt:67`、`ControlCenterContent.kt:122`） |
| `uSpectral` | **0.12** | — | `DIALOG_SPECTRAL_BASE = 0.12`（`:507`），且 Kyant0 弹窗**根本不色散**。想要更明显的彩边可到 **0.20–0.25**（仍在「大面板」档内：菜单 0.55 是上限参考）；**不要用 1.0** |
| `uPanelOpacity` | **1.0** | — | 参考静止态就是 1.0（`Dialog.cpp:348` → `GlassPanel.cpp:245-248`；`PANEL_OPACITY_MAX = 1.0`）。**透明度必须靠表面染色做，不能压 `uPanelOpacity`** |
| `uSurfaceMode` | **2.0** | — | `SurfaceMode::Overlay`（`GlassPanel.cpp:259-260`）。ytbl 的 Dialog/Menu/Tab/Button **全部**用 Overlay（`Dialog.cpp:347`、`PopupMenu.cpp:197`、`TabBar.cpp:287`、`PillButton.cpp:109`）。⚠️ 见 §6.1，这是当前最大的一处缺失 |
| `uSurfaceTint` | **(0.071, 0.071, 0.071, 0.40)** 深色<br>**(0.98, 0.98, 0.98, 0.60)** 浅色 | — | Kyant0 弹窗 `#121212 @0.4` / `#FAFAFA @0.6`（`DialogContent.kt:42-47,70`）。ytbl 折算值是 `tintSheet()`≈(0.145,0.154,0.188) @ `0.34×0.72 = 0.245`（`ColorAdapter.cpp:167-181`＋`:513`）——**更轻的一档，可作保守替代** |
| `uToneCtrl` | **(0.0, 1.0, 1.0)** | — | `BRIGHTNESS_DARK = 0.0`、`CONTRAST_* = 1.0`、`SATURATION_* = 1.0`（`:187-192`）＋ `GlassPanel.cpp:249-251` |
| `uToneBoost` | **1.0** | — | 打开 `ctrl.z × 1.5`（`Shaders.cpp:370`）= Kyant0 `vibrancy()`（`ColorFilter.kt:41-45`）。依据：`DialogContent.kt:62-65`、`ControlCenterContent.kt:118`、`LiquidBottomTabs.kt:170`、`GlassPlaygroundContent.kt:70`；ytbl 对应 `colorBoost=true`（`PopupMenu.cpp:172`、`TabBar.cpp:281`、`PillButton.cpp:72`） |
| `uRefractTint` | **(0.07, 0.08, 0.10, 0.00)** | — | `tintRefract()` = 主题主色 @ `tintDeviation × 0.22`（`ColorAdapter.cpp:234-237`）；默认深色主题 `tintDeviation = 0` → **α = 0**。硬上限 0.22 只在主题色极度偏离中性色时达到（`GlassConfig.h:185`） |
| `uDarkGuard` | **0.02**（或保留 0.045） | — | Kyant0 **未找到**对应项；ytbl 只在 Toggle 上用满 0.045（`TOGGLE_DARK_GUARD=1.0`，`:436`＋`:87`）。它不是「液态玻璃」配方的一部分，只是防死黑。若已按上面加了表面染色，可降到 0～0.02 |
| `uEdgeMode` | **3.0**（Plain） | — | 与 Kyant0 弹窗的 `Highlight.Plain` 对应（`DialogContent.kt:69`）。想要方向感更强的「液态反光」用 **4.0**（Contour，ytbl 菜单的做法，`PopupMenu.cpp:206`），或用 **1.0**（等价 `Highlight.Default`，但注意 ytbl 是单侧门控，见 §3.4） |
| `uEdgeWidth` | **2.0** | 0.5 dp（ytbl 会 `ceilf` → 2） | `EDGE_WIDTH_MIN = 0.5`（`:77`）＝ `Highlight.width = 0.5dp`（`Highlight.kt:11`）；`GlassPanel.cpp:119-124`：`ceilf(widthDp × density × scale)` |
| `uEdgeBlur` | **0.75** | 0.25 dp | `EDGE_SOFTNESS_MIN = 0.25`（`:78`）＝ `Highlight.blurRadius = width/2`（`Highlight.kt:12`） |
| `uEdgeAlpha` × `uEdgeTintAlpha` | 乘积 ≈ **0.38** | — | 弹窗有效值 = `0.62 × 0.62 = 0.3844`（`GlassConfig.h:511`＋`Dialog.cpp:327-335`）；Kyant0 `HighlightStyle.Plain.color = White α0.38`（`HighlightStyle.kt:34`）。建议 `uEdgeTint = (1,1,1,1)`、`uEdgeAlpha = 0.38` |
| `uEdgeTint` | **(1, 1, 1, 1)** | — | 参考高光就是纯白（`HighlightStyle.kt:34`）；ytbl 弹窗也把 tint 掩成白色（`Dialog.cpp:329` 的 `& 0x00FFFFFFu`） |
| `uEdgeAngle` | 45（Neutral）| — | `EDGE_ANGLE_DEFAULT = 45.0`（`:79`）＝ `HighlightStyle.Default.angle = 45f`（`:48`）。**仅 mode 1/2/4 有效，mode 3 忽略** |
| `uEdgeFalloff` | 1.0 | — | `EDGE_FALLOFF_DEFAULT = 1.0`（`:80`）＝ `falloff = 1f`（`:49`）。✅ 当前值正确 |
| `uInsetTint` | **(0, 0, 0, 1)** 黑 | — | Kyant0 `InnerShadow.color = Color.Black.copy(alpha = 0.15f)`（`InnerShadow.kt:20`）。⚠️ 见 §6.5 |
| `uInsetShift` | **(0, −72)** | 0 / 24 dp | Kyant0 `offset = DpOffset(0.dp, radius)`（`InnerShadow.kt:19`）＝向下偏 24dp；在 ytbl 的 **y 向上**坐标里是负 y（与当前 `(0,-1)` 同号，量级差 72 倍） |
| `uInsetReach` | **72** | 24 dp | `InnerShadow.radius = 24f.dp`（`InnerShadow.kt:18`）；ytbl 已确认的横截面另有两档：`TAB_PRESS_INNER_DP = 8dp`（`:481`）、`TOGGLE/SLIDER_INNER_DP = 4dp`（`:432,454`） |
| `uInsetAlpha` | **0.15** | — | `InnerShadow.color` 的 α0.15（`InnerShadow.kt:20`）× `alpha = 1f`（`:21`） |
| `uGlowOn` / `uGlowLevel` | **0 / 0**（静止面板） | — | 光斑在参考里是**交互态**：Kyant0 `InteractiveHighlight` 由 `pressProgress` 驱动，静止为 0（`InteractiveHighlight.kt:62-89`）；ytbl 的 `GlowSpring` 同理（`GlassPanel.cpp:188-201`，`progress ≤ GLOW_LEVEL_MIN` 直接关掉）。**静止面板上挂一个固定光斑 = 假高光** |
| （若一定要保留光斑）`uGlowReach` | **930** | — | `glowReach = 短边 × GLOW_REACH_SCALE(1.5)`（`GlassPanel.cpp:198`＋`:97`）＝ Kyant0 `radius = size.minDimension × 1.5f`（`InteractiveHighlight.kt:73`）。620×900 的短边… 实际 `shorter = min(frame.w, frame.h) = 620` → **930px** |
| （若一定要保留光斑）`uGlowAt` | 跟随指针 | — | `InteractiveHighlight.kt:70-78`（钳到面板内）；ytbl 用 `style.glow.x/y`（`GlassPanel.cpp:194-195`） |

### 4.3 与「不过曝 / 不脏」相关的两条硬约束

1. **gather 窗口留白必须跟着放大**（`GlassPanel.cpp:78-92`）：
   `pad = 3 + blur×2 + lensAmount + lensAmount×spectral`
   按推荐值 = `3 + 60 + 128 + 15.4 = 206px`。
   当前 `probe_m3_ytbl.cpp:433` 是 `margin = ceil(blurRadius × 2) + 4 = 40px`（blur=18 时）。
   **只要把 `uLensAmount` 提到 128 而不放大 margin，面板四角的外向色散抽样就会落到 gather 窗口之外被 clamp —— 表现为四角出现拖尾/糊边。** 这是「调大后反而更脏」的最可能原因。
   最低要求：`margin ≥ 2×blur + lensAmount×spectral`（推荐值下 ≈ 75px）；照抄 ytbl 公式则取 206px。

2. **`uSpectral` 同时缩放位移**（`Shaders.cpp:348-350`）：`strength = uSpectral × clamp(x·y/(hx·hy))`。
   所以色散不是「加一层彩边」，而是**把折射抽样点往色散方向再拉开 `uSpectral` 倍**。
   `uSpectral = 1.0` ＋ `lensAmount = 128` 时，四角红/蓝通道之间的最大偏移量 = 128px —— 相当于把桌面图像在四角横拉 128px，必然「脏」。
   `uSpectral = 0.12` 时该偏移 = 15px，只在桌面高对比边缘（窗口边框、文字）产生可见彩边 —— 这正是参考的观感。

---

## 五、我方当前值的逐项体检

基准：§2.2 配方 A（Dialog 静止态）＋ §2.3 配方 B（Menu 展开态）。
我方当前值来自 `build/recon/probe_m3_ytbl.cpp:527-550`。

| 项 | 当前值 | 基准 | 判定 | 建议 |
| --- | --- | --- | --- | --- |
| `cornerRadius` | 46 px | 144 px（48dp） | ❌ **偏小 3.1 倍** | **改 144**。46px = 15.3dp，是「大按钮」的圆角量级，不是 620×900 面板的 |
| `lensHeight` | 46 px | 72 px（24dp） | ❌ 偏小 1.57 倍 | **改 72** |
| `lensAmount` | 40 px | 128 px（48dp，截断后） | ❌ **偏小 3.2 倍** | **改 128**（或按 14.5% 宽度折算取 90） |
| `lensDepth` | 0.16 | 1.0 | ❌ **偏小 6.25 倍**（注释写「参考代码里是小量」，实际参考是**布尔满值**） | **改 1.0**；`depthEffect` 在 Kyant0 里是 bool（`Lens.kt:19,50`），弹窗与 Control Center 都传 `true` |
| `spectral` | 1.0 | 0.12（弹窗）/ 0.55（菜单上限） | ❌ **偏大 8.3 倍** | **改 0.12**，最多 0.25 |
| `panelOpacity` | 1.0 | 1.0 | ✅ 正确 | 保持 |
| `darkGuard` | 0.045 | 参考无此项（ytbl 只有 Toggle 用满） | ⚠️ 可接受但非配方成分 | 已加表面染色后可降到 0.02；不必再抬高 |
| `toneCtrl` | (0.04, 1.0, 1.0) | (0.0, 1.0, 1.0) | ⚠️ `0.04` 是 **`BRIGHTNESS_LIGHT`（浅色主题值）**（`GlassConfig.h:188`） | 深色场景改 **0.0** |
| `toneBoost` | 0 | 1 | ❌ **缺 vibrancy** | **改 1.0**（或等价地把 `toneCtrl.z` 设为 1.5） |
| `refractTint` | (0.62,0.72,0.90, **α0.22**) | 主题主色 @ α=0（深色中性主题） | ❌ **α 取了硬上限，且 rgb 是浅蓝** | **α 改 0.00–0.03**；若想要色偏，rgb 用主题主色 (0.07,0.08,0.10)。⚠️ 这条是「洗白偏塑料」的主因之一：shader 里 `color.rgb += tint.rgb × tint.a × color.a`（`Shaders.cpp:373`），α0.22 的浅蓝等于给整块面板加 (+0.136, +0.158, +0.198) 的恒定泛白 |
| `uSurfaceMode` | **0.0** | **2.0** | ❌ **缺整层表面染色** | **改 2.0** ＋ `uSurfaceTint` 见 §6.1 |
| `uSurfaceTint` | (0,0,0,0) | (0.071,0.071,0.071, 0.40) 深 | ❌ 未启用 | 见 §6.1 |
| `edgeMode` | 3.0 | 3.0（Plain） | ✅ 与 Kyant0 弹窗一致 | 保持；想更「液态」可试 4.0 |
| `edgeWidth` | 1.6 px | 2.0 px | ✅ 基本正确（0.53dp vs 0.5dp） | 可留 1.6，或改 2.0 对齐 `ceilf(px(0.5dp))` |
| `edgeBlur` | 1.2 px | 0.75 px（0.25dp） | ⚠️ **偏软 1.6 倍** | **改 0.75**。边缘光糊掉会直接变成「塑料亮边」 |
| `edgeAlpha` | 0.55 | 0.384 | ⚠️ 偏强 1.43 倍 | **改 0.38**（配 `uEdgeTintAlpha = 1.0`） |
| `edgeTintAlpha` | 1.0 | 0.62（与 edgeAlpha 相乘得 0.384） | ✅ 组合语义正确 | 保持 1.0，只调 `edgeAlpha` |
| `edgeTint` | (0.92,0.96,1.0) | 白 (1,1,1) | ⚠️ 带蓝 | 在 Plain 模式下这是**加法**（`Shaders.cpp:411-413`），冷色叠加会偏青。**改 (1,1,1,1)** |
| `edgeAngle` | 0.0 | 45.0 | ➖ mode 3 忽略该值（`Shaders.cpp:398-401` 只在 mode 2/1 用 `align`，mode 4 用） | 若改用 mode 4，设 −90（`EDGE_CONTOUR_ANGLE`）或 45 |
| `edgeFalloff` | 1.0 | 1.0 | ✅ 正确（但 **mode 3 下无效**） | 保持；换 mode 1/2/4 才有意义 |
| `insetTint` | (1,1,1,1) **白** | 黑 α0.15 | ❌ **颜色反了** | **改 (0,0,0,1)**。参考的内阴影是**暗**的（压出玻璃厚度），我方是白的，等于在内缘又加一道高光 → 双重高光 = 塑料 |
| `insetShift` | (0,-1) | (0,−72)（24dp） | ⚠️ 方向对、量级偏小 72 倍 | **改 (0,−72)**（配合 `insetReach = 72`） |
| `insetReach` | 7 px | 72 px（24dp）/ 24px（8dp）/ 12px（4dp） | ⚠️ 偏小 3–10 倍 | **改 72**（Kyant0 默认）或 24（`TAB_PRESS_INNER_DP`） |
| `insetAlpha` | 0.18 | 0.15 | ✅ 基本正确 | 改 0.15 |
| `glowOn` | 1.0（常开） | 仅交互态 | ❌ 静止面板挂固定光斑 | **改 0.0** |
| `glowAt` | (w/2, 0.72h) | 指针位置 | ⚠️ 写死 | 若保留则跟随指针 |
| `glowReach` | 220 px | 930 px（620 × 1.5） | ⚠️ 偏小 4.2 倍 | **改 930** |
| `glowLevel` | 0.10（恒定） | pressProgress（静止 0） | ❌ | **改 0.0** |
| `blurRadius` | 18 px（默认实参） | 30 px（10dp） | ⚠️ 偏小 | **改 30**（或 24 = Kyant0 深色弹窗原值） |
| gather `margin` | 40 px | ≥ 2×30 + 128×0.12 = 75 px（ytbl 公式给 206） | ❌ 会不足 | **改 206**（照抄 `capturePadding`） |
| 色散棱镜 uniform | 已按 `PRISM_*` 正确赋值（`:518-525, 688-707`） | 同 | ✅ **正确** | 保持 |

---

## 六、诊断结论

### 6.1 最大的一处：`uSurfaceMode = 0` —— 整层「霜面」缺失

`M3_YTBL_GLASS_INTEGRATION.md:52-64` 下的结论是「纯玻璃必须用 0.0」，理由是 `uSurfaceMode = 1.0` 时面板被洗成白色。

**这个结论过宽了。** shader 里三个分支是（`Shaders.cpp:374-389`）：

| 值 | 分支 | 行为 |
| --- | --- | --- |
| 0 | 无 | 不染色 |
| 1 | Hue | `color.rgb = mix(color.rgb, tint.rgb*(0.35+1.3*luma), 0.75*tint.a)`；`color.a = max(color.a, 0.75*tint.a)` |
| **2** | **Overlay** | **标准 source-over：`out = tint×a + color×(1−a)`** |

当初洗白是用了 **1（Hue）**，不是 2（Overlay）。
**ytbl 自己的 Dialog / Menu / Tab / Button 全部用 2（Overlay）**（`Dialog.cpp:347`、`PopupMenu.cpp:197`、`TabBar.cpp:287`、`PillButton.cpp:109`），Kyant0 对应 `onDrawSurface = { drawRect(containerColor) }`。

**没有这一层，玻璃就只剩「模糊 + 折射的桌面」——这正是「塑料」的直接来源**：真实液态玻璃的辨识度来自那层 ~38–40% 的半透明霜面染色，它压掉背景的对比度、给面板一个统一的体色。

**结论：`uSurfaceMode` 改 2.0，`uSurfaceTint` 用 (0.071,0.071,0.071, 0.40)（深色）。**

### 6.2 第二处：几何量系统性偏小，且 `lensDepth` 被当成微调量

`cornerRadius 46 / lensHeight 46 / lensAmount 40 / lensDepth 0.16` 这四项**同时偏小 1.6–6.3 倍**，
而它们恰恰是「液态玻璃」区别于「磨砂亚克力」的全部来源 —— 边缘那圈把桌面图像向内挤压的折射带。
`lensDepth = 0.16` 尤其关键：注释说「参考代码里是小量」，但 Kyant0 的 `depthEffect` 是 **Bool**（`Lens.kt:19,50`），
弹窗（`DialogContent.kt:67`）、Control Center（`ControlCenterContent.kt:122`）、
playground（`GlassPlaygroundContent.kt:75`）、自适应亮度面板（`AdaptiveLuminanceGlassContent.kt:108`）
**全部传 `true`**，即 1.0。
它给法线叠加一个指向面板中心的偏置，让折射方向在直边中段也带一点向心分量 —— 这正是「液体」而不是「玻璃片」的关键。

### 6.3 第三处：`refractTint` α=0.22 的浅蓝 + `spectral`=1.0 的色散

这两条一起造成「脏 + 泛白」：

- `Shaders.cpp:373`：`color.rgb += tint.rgb × tint.a × color.a` —— α0.22 的 (0.62,0.72,0.90) 等于整块加 **(+0.136,+0.158,+0.198)**。
  参考里这一项默认 **α=0**（`ColorAdapter.cpp:234-237` + 深色主题 `tintDeviation=0`），0.22 是理论上限。
- `uSpectral` 同时缩放色散位移（`Shaders.cpp:348-350`），1.0 意味着四角红蓝通道分离达 `lensAmount` 的量级。

### 6.4 第四处：缺 vibrancy

Kyant0 几乎每块玻璃都过 `vibrancy()`（饱和 1.5），ytbl 对应 `colorBoost = true`。
我方 `toneBoost = 0` 且 `toneCtrl.z = 1.0` → 背景饱和度完全没被提升，模糊后的桌面发灰。**改 `toneBoost = 1.0`。**

### 6.5 第五处：内阴影反色 + 常开光斑

- 内阴影用了**白色**（参考是黑色 α0.15），再叠加白色边缘光 → 面板四周两道白 → 塑料高光感。
- `glowOn = 1.0` 且 `glowLevel = 0.10` 常开，`glowAt` 写死在 72% 高度 → 一个不会动的假高光。
  参考里这是**按下/拖拽**才出现的指针光斑（`InteractiveHighlight.kt:62-89`；ytbl `GlowSpring` `GlassPanel.cpp:188-201`）。静止面板应当关掉。

---

## 七、最小改动清单（按性价比排序）

```text
# 几何（一次到位）
uCornerRadii  = 144,144,144,144     # 46   -> 参考 48dp
uLensHeight   = 72                  # 46   -> 参考 24dp
uLensAmount   = 128                 # 40   -> 参考 48dp（144 会被 LENS_REACH_MAX 截到 128）
uLensDepth    = 1.0                 # 0.16 -> 参考 depthEffect = true
uBlurRadius   = 30                  # 18   -> 参考 10dp（= BLUR_RADIUS_MAX_DP）
gather margin = 206                 # 40   -> capturePadding 公式

# 材质（补上缺的那一层）
uSurfaceMode  = 2.0                 # 0    -> Overlay
uSurfaceTint  = 0.071,0.071,0.071,0.40   # 深色；浅色用 0.98,0.98,0.98,0.60
uToneBoost    = 1.0                 # 0    -> vibrancy (饱和 x1.5)
uToneCtrl     = 0.0,1.0,1.0         # 0.04 -> 0.0（深色 BRIGHTNESS_DARK）

# 收敛过曝/过脏
uSpectral     = 0.12                # 1.0  -> DIALOG_SPECTRAL_BASE
uRefractTint  = 0.07,0.08,0.10,0.00 # α0.22 浅蓝 -> α0 主题主色
uDarkGuard    = 0.02                # 0.045（可留 0.045，非配方成分）

# 边缘光
uEdgeWidth    = 2.0                 # 1.6
uEdgeBlur     = 0.75                # 1.2
uEdgeAlpha    = 0.38                # 0.55（配 uEdgeTintAlpha = 1.0）
uEdgeTint     = 1.0,1.0,1.0,1.0     # 0.92,0.96,1.0 蓝调 -> 纯白

# 内阴影 / 光斑
uInsetTint    = 0.0,0.0,0.0,1.0     # 白 -> 黑
uInsetShift   = 0.0,-72.0           # (0,-1) -> 24dp
uInsetReach   = 72                  # 7
uInsetAlpha   = 0.15                # 0.18
uGlowOn       = 0.0                 # 1.0（静止面板）
uGlowLevel    = 0.0                 # 0.10
```

---

## 八、注意事项 / 未找到项

1. **`LENS_REACH_MAX = 128` 是 px 上限**（`GlassPanel.cpp:236-239`）：480dpi 下 `px(48dp) = 144` 会被静默截到 128。
   如果走高 `uniformPixelScale`（>1）的路径，截断发生在乘 scale **之前**，最终值可达 256。
2. **`edgeWidth` 会被 `ceilf`**（`GlassPanel.cpp:124`）：0.5dp 在 density 3 下是 1.5px → **2px**。直接写 uniform 则不受此限。
3. **shader 里 `bandWidth = max(uEdgeWidth, 0.5)`、`bandBlur = max(uEdgeBlur, 0.5)`**（`Shaders.cpp:394-395`）：小于 0.5 的值无效。
   边缘光的实际覆盖深度 = `uEdgeWidth + 2 × uEdgeBlur`（`Shaders.cpp:397`）。
4. **`radiusAt()` 在 corner 区用角点判定**（`Shaders.kt:23-31` / `Shaders.cpp:176-179`）：四角半径不一致时，法线场的过渡会有轻微不连续 —— 建议四角同值。
5. **Kyant0 的 `gradRadius = radius × 1.5`**（`Shaders.kt:79,116`）在 ytbl 里**未找到**对应实现（ytbl 用真半径，`Shaders.cpp:329`）。这是目前**无法通过调参消除**的一处差异；若要完全对齐，需要改 shader 第 329 行。
6. **Kyant0 的大面板高光默认是 `Highlight.Default`**（`DrawBackdropModifier.kt:41,84`，白 α0.5、`|dot|^1` 两侧对称），
   但**集成 demo 里的大面板反而显式改成 `Highlight.Plain`**（`DialogContent.kt:69`、`GlassPlaygroundContent.kt:79,135`）。
   ytbl 的 mode 3（Plain）与之对应；但 ytbl 的 mode 1（Gradient）是**单侧**门控（`Shaders.cpp:420`），
   与 Kyant0 `Default` 的 `abs(d)` **双**侧不同 —— 这是一处**未找到**等价实现的行为差异。
7. **ytbl 的 `InsetShadow(radius, alpha)` 构造把偏移置 0**（`GlassUI.h:289-290`），
   丢掉了 Kyant0 `InnerShadow` 默认的 `offset = (0, radius)`（`InnerShadow.kt:19`）。
   用库路径时内阴影会变成对称的（四周都有一条），而不是「只在顶部」。
8. **暗部保护 `darkGuard` 在 Kyant0 全库未找到**：它不是「液态玻璃」配方的一部分，只是 ytbl 为 Toggle 加的防死黑。不要把它当成主要调参项。
9. **`refractTint` 在 Kyant0 全库未找到**：同为 ytbl 独有。
10. **Kyant0 的 `ContinuousCurvatureRoundedRectangle`（连续曲率圆角）在两侧源码里都未用于 backdrop 形状**：
    `DialogContent.kt:60` 用的是 `RoundedRectangle(48f.dp)`，来自独立依赖 `io.github.kyant0:shapes:1.2.1`（`gradle/libs.versions.toml:8,18`），**该库不在本仓库内**。
    ytbl 只有普通圆角 SDF（`CORNER_ARC_TANGENT = 0.5522847498`，`GlassConfig.h:616`）。
    我方 `M3_YTBL_GLASS_INTEGRATION.md:112-169` 已单独移植了 Kyant0 的连续曲率算法 —— **这部分不属于本次「视觉配方」范畴，但它确实影响四角的「高级感」，建议与本配方一同验证。**
