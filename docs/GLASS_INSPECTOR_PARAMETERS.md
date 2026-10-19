# 液态玻璃参数清单（自动生成）

> ★ 本文件由 `tools/glass_param_report.cpp` **从组件注册表直接导出**，请勿手工编辑。
> 重新生成：`pwsh -File tools/gen_glass_param_report.ps1`

## 总览

| 指标 | 数值 |
| --- | --- |
| 调试目标（组件 / 子层） | 36 |
| 参数条目 | 1159 |
| 其中可读写 | 1041 |
| 能力缺口（只读 + 原因） | 118 |
| 顶层对象（分页栏条目） | 22 |

## 目标一览

| # | 目标 ID | 名称 | 类型 | 父目标 | 参数数 | 可写 | 缺口 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `secondary_window` | 整窗口 | Window | `-` | 39 | 36 | 3 |
| 2 | `secondary_window.resize` | 窗口缩放方式 | Interaction | `-` | 25 | 25 | 0 |
| 3 | `secondary_window.card` | 液态玻璃卡片 | GlassCard | `secondary_window` | 52 | 41 | 11 |
| 4 | `secondary_window.pill_button_1` | 按钮 1 · 主按钮 | PillButton | `secondary_window` | 69 | 67 | 2 |
| 5 | `secondary_window.pill_button_2` | 按钮 2 · 着色 | PillButton | `secondary_window` | 69 | 67 | 2 |
| 6 | `secondary_window.pill_button_3` | 按钮 3 · 表面色 | PillButton | `secondary_window` | 69 | 67 | 2 |
| 7 | `secondary_window.pill_button_4` | 按钮 4 · 小按钮 | PillButton | `secondary_window` | 69 | 67 | 2 |
| 8 | `secondary_window.pill_button_5` | 按钮 5 · 弹窗触发 | PillButton | `secondary_window` | 69 | 67 | 2 |
| 9 | `secondary_window.toggle_1` | 开关 1 | Toggle | `secondary_window` | 9 | 9 | 0 |
| 10 | `secondary_window.toggle_1.track` | 开关 1 · 轨道 | ToggleTrack | `secondary_window.toggle_1` | 4 | 4 | 0 |
| 11 | `secondary_window.toggle_1.thumb` | 开关 1 · 圆钮 | ToggleThumb | `secondary_window.toggle_1` | 31 | 30 | 1 |
| 12 | `secondary_window.toggle_2` | 开关 2 | Toggle | `secondary_window` | 9 | 9 | 0 |
| 13 | `secondary_window.toggle_2.track` | 开关 2 · 轨道 | ToggleTrack | `secondary_window.toggle_2` | 4 | 4 | 0 |
| 14 | `secondary_window.toggle_2.thumb` | 开关 2 · 圆钮 | ToggleThumb | `secondary_window.toggle_2` | 31 | 30 | 1 |
| 15 | `secondary_window.toggle_3` | 开关 3 | Toggle | `secondary_window` | 9 | 9 | 0 |
| 16 | `secondary_window.toggle_3.track` | 开关 3 · 轨道 | ToggleTrack | `secondary_window.toggle_3` | 4 | 4 | 0 |
| 17 | `secondary_window.toggle_3.thumb` | 开关 3 · 圆钮 | ToggleThumb | `secondary_window.toggle_3` | 31 | 30 | 1 |
| 18 | `secondary_window.slider_1` | 进度条 1 | Slider | `secondary_window` | 12 | 12 | 0 |
| 19 | `secondary_window.slider_1.track` | 进度条 1 · 轨道 | SliderTrack | `secondary_window.slider_1` | 5 | 5 | 0 |
| 20 | `secondary_window.slider_1.thumb` | 进度条 1 · 滑杆 | SliderThumb | `secondary_window.slider_1` | 33 | 33 | 0 |
| 21 | `secondary_window.slider_2` | 进度条 2 | Slider | `secondary_window` | 12 | 12 | 0 |
| 22 | `secondary_window.slider_2.track` | 进度条 2 · 轨道 | SliderTrack | `secondary_window.slider_2` | 5 | 5 | 0 |
| 23 | `secondary_window.slider_2.thumb` | 进度条 2 · 滑杆 | SliderThumb | `secondary_window.slider_2` | 33 | 33 | 0 |
| 24 | `secondary_window.navigation` | 底部导航栏 | TabStrip | `secondary_window` | 21 | 21 | 0 |
| 25 | `secondary_window.navigation.background` | 导航栏 · 背景层 | NavigationBackground | `secondary_window.navigation` | 11 | 10 | 1 |
| 26 | `secondary_window.navigation.indicator` | 导航栏 · 指示器 | NavigationIndicator | `secondary_window.navigation` | 8 | 8 | 0 |
| 27 | `secondary_window.chip_1` | 胶囊 1 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 28 | `secondary_window.chip_2` | 胶囊 2 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 29 | `secondary_window.chip_3` | 胶囊 3 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 30 | `secondary_window.chip_4` | 胶囊 4 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 31 | `secondary_window.chip_5` | 胶囊 5 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 32 | `secondary_window.chip_6` | 胶囊 6 | GlassChip | `secondary_window` | 54 | 43 | 11 |
| 33 | `secondary_window.popmenu` | 弹出菜单 | PopMenu | `secondary_window` | 23 | 22 | 1 |
| 34 | `secondary_window.dialog` | 液态弹窗 | Modal | `secondary_window` | 5 | 4 | 1 |
| 35 | `secondary_window.playground` | 玻璃实验场 | GlassPlayground | `secondary_window` | 63 | 52 | 11 |
| 36 | `secondary_window.capability_gaps` | 能力缺口总表 | Gaps | `secondary_window` | 11 | 0 | 11 |

## 参数明细

「实现位置」是参数真正被渲染读取的字段 / 常量所在处；「成本」决定更新策略（即时 = 当帧；帧边界 = 合并后按帧提交）。

### 整窗口（`secondary_window`）

二级窗口的外壳玻璃：整块圆角面板 + 投影。★ 它的参数**不会**复制给子组件

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 外观 | `glassEnabled` | 整窗液态玻璃 | glassEnabled | bool | 开 / 关 | true | — | 帧边界·采样 | `s_WindowGlassEnabled` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 外观 | `opacityOn` | 调节窗口透明度 | adjustOpacity | bool | 开 / 关 | false | — | 即时 | `s_WindowOpacityEnabled` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 外观 | `opacity` | 窗口透明度 | opacity | float | 0.05 ~ 0.85 × | 0.2800 | × | 即时 | `s_WindowOpacity` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 外观 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 几何 | `cornerDp` | 窗口圆角 | corner | float | 0 ~ 96 dp | 28.0000 | dp | 帧边界·效果链 | `kWindowCornerDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 10.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 200 dp | 18.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 200 dp | 30.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | dispersion | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `chromaticOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `s_WindowChromatic` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `depthOn` | 景深折射 | depthEffect | bool | 开 / 关 | true | — | 即时 | `s_WindowDepth` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `windowStyle` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `surfaceLight` | 浅色主题表面色 | surfaceLight | color | RGBA 0 ~ 1 | 0.960,0.970,0.990,0.000 | — | 即时 | `WindowTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 色彩 / 表面 | `surfaceDark` | 深色主题表面色 | surfaceDark | color | RGBA 0 ~ 1 | 0.100,0.110,0.140,0.000 | — | 即时 | `WindowTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 色彩 / 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `WindowTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 色彩 / 表面 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 1 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用投影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `WindowTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 阴影 | `shadowRadiusDp` | 投影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOffsetXDp` | 投影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOffsetYDp` | 投影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `SHADOW_OFFSET_DIVISOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 阴影 | `shadowAlpha` | 投影强度 | alpha | float | 0 ~ 1 × | 0.1412 | × | 即时 | `WindowTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 0.0000 | × | 帧边界·效果链 | `ScopedPanelClip` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapCombinedBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 把多个来源按顺序画到一起。本机是「进程内 Z 序合成 + 单一采集源」，见 docs/GLASS_ZORDER_COMPOSITOR.md；要做到 Kyant 那样需要采集层支持多次采样 |
| 能力缺口（只读） | `gapExposure` | 曝光调整 exposureAdjustment | exposureAdjustment | 缺口 | — | — | — | — | 参考项目 **2.0.1 已移除**该 API（只在 1.0.6 存在：scale = 2^(ev/2.2) 的 ColorMatrix）。本实现的 tone 控制只有 brightness/contrast/saturation → 不提供，免得做成「看起来能调、其实只是亮度」的假控件 |
| 能力缺口（只读） | `gapGamma` | 伽马调整 gammaAdjustment | gammaAdjustment | 缺口 | — | — | — | — | 同上：参考项目 2.0.1 已移除，只有 1.0.6 的 pow(color.rgb, power)。接入需要在片元着色器里新增一个 uniform 与一次 pow |

### 窗口缩放方式（`secondary_window.resize`）

两种缩放方式（实时 / 虚线预览）+ 松手后的液态玻璃填充动画参数。★ 这里的值与「主题 → 窗口缩放」页是**同一块内存**，改哪边都一样

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 缩放方式 | `mode` | 窗口缩放方式 | resizeMode | enum | 0 ~ 1 | 0 | — | 即时 | `ResizeMode` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `dashWidthDp` | 虚线线宽 | lineWidth | float | 0.5 ~ 4 dp | 1.4000 | dp | 即时 | `CornerResizeDrawPreview` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 虚线预览框 | `dashLengthDp` | 虚线段长度 | dashLength | float | 2 ~ 24 dp | 8.0000 | dp | 即时 | `BuildDashedRoundedRect` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `dashGapDp` | 虚线间隔 | dashGap | float | 1 ~ 20 dp | 5.0000 | dp | 即时 | `BuildDashedRoundedRect` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `lineAlpha` | 虚线不透明度 | lineAlpha | float | 0 ~ 1 × | 0.9400 | × | 即时 | `CornerResizeDrawPreview` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 虚线预览框 | `underlayAlpha` | 下衬暗线强度 | underlayAlpha | float | 0 ~ 1 × | 0.4200 | × | 即时 | `CornerResizeDrawPreview` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 虚线预览框 | `cornerAddDp` | 圆角增量 | cornerAdd | float | -16 ~ 32 dp | 0.0000 | dp | 即时 | `PreviewCornerRadius` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `sizeLabel` | 显示预计宽高 | sizeLabel | bool | 开 / 关 | true | — | 即时 | `CornerResizeDrawPreview` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 虚线预览框 | `labelOffsetDp` | 数值外移量 | labelOffset | float | 0 ~ 48 dp | 10.0000 | dp | 即时 | `ResizePreviewParams` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `labelScale` | 数值字号比例 | labelScale | float | 0.4 ~ 1.2 × | 0.7200 | × | 即时 | `ResizePreviewParams` @ app/src/main/cpp/ResizePreview.h |
| 虚线预览框 | `dissolveFrom` | 虚线框解体时机 | dissolveFrom | float | 0 ~ 0.95 进度 | 0.5500 | 进度 | 即时 | `PreviewDissolveAlpha` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（几何） | `springStiffness` | 弹簧刚度 k | springStiffness | float | 20 ~ 900 | 420.0000 | — | 即时 | `ResizePreviewParams::springStiffness` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（几何） | `springDamping` | 弹簧阻尼 c | springDamping | float | 4 ~ 120 | 38.5000 | — | 即时 | `ResizePreviewParams::springDamping` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（几何） | `settleEpsilonPx` | 到位阈值 | settleEpsilonPx | float | 0.1 ~ 6 px | 1.0000 | px | 即时 | `ResizeCommitAnim::Step` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（几何） | `maxDurationMs` | 兜底时限 | maxDurationMs | float | 60 ~ 4e+03 ms | 1200.0000 | ms | 即时 | `ResizeCommitAnim::Step` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（玻璃） | `fillOn` | 启用玻璃填充 | fillOn | bool | 开 / 关 | true | — | 即时 | `ResizePreviewParams::fillOn` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（玻璃） | `fillRimWidthDp` | 前沿带宽 | rimWidth | float | 4 ~ 120 dp | 22.0000 | dp | 即时 | `ComputeResizeRims` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（玻璃） | `fillAlpha` | 前沿不透明度 | fillAlpha | float | 0 ~ 1 × | 0.4200 | × | 即时 | `ResizePreviewParams::fillAlpha` @ app/src/main/cpp/ResizePreview.h |
| 填充动画（玻璃） | `fillBlurDiffDp` | 前沿模糊差 | blurDiff | float | 0 ~ 8 dp | 2.5000 | dp | 即时 | `ScopedPanelClip` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 填充动画（玻璃） | `fillRefractGain` | 前沿折射增益 | refractGain | float | 1 ~ 2 × | 1.2500 | × | 即时 | `lgx::drawPanel` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 填充动画（玻璃） | `fillSpectralGain` | 前沿色散增益 | spectralGain | float | 1 ~ 2 × | 1.3500 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 填充动画（玻璃） | `fillGlowAlpha` | 前沿柔光强度 | glowAlpha | float | 0 ~ 1 × | 0.3000 | × | 即时 | `lgx::drawPanel` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 填充动画（玻璃） | `fillGlowWidthDp` | 柔光宽度 | glowWidth | float | 0 ~ 120 dp | 26.0000 | dp | 即时 | `lgx::drawPanel` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 填充动画（玻璃） | `edgeFlowGain` | 边缘流动强度 | edgeFlow | float | 0 ~ 3 × | 0.8500 | × | 即时 | `CornerResizeEdgeFlow` @ app/src/main/cpp/CornerResizeOverlay.cpp |
| 填充动画（玻璃） | `edgeFlowSoftnessDp` | 边缘柔化量 | flowSoftness | float | 0 ~ 3 dp | 0.4500 | dp | 即时 | `CornerResizeEdgeFlow` @ app/src/main/cpp/CornerResizeOverlay.cpp |

### 液态玻璃卡片（`secondary_window.card`）

折射测试卡片：下面垫了 6 色条纹，专门用来看折射/色散是否真的在采样背景

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7500 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 12.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 10.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 1.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | true | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 6.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 6.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 按钮 1 · 主按钮（`secondary_window.pill_button_1`）

主按钮（Capsule + 默认表面配方）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 宽度 | width | float | 24 ~ 420 dp | 160.0000 | dp | 即时 | `s_ButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `heightDp` | 高度 | height | float | 20 ~ 160 dp | 48.0000 | dp | 即时 | `s_ButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 40 dp | 12.0000 | dp | 即时 | `button.lensHeightDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `button.lensAmountDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `button.spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textSizeSp` | 文字大小 | textSizeSp | float | 8 ~ 40 sp | 16.0000 | sp | 即时 | `button.textSizeSp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `visualScale` | 视觉缩放 | visualScale | float | 0 ~ 1.5 × | 1.0000 | × | 即时 | `BUTTON_VISUAL_SCALE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `alphaScale` | 整体 alpha | alphaScale | float | 0 ~ 1 × | 1.0000 | × | 即时 | `button.alphaScale` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `glassOn` | 启用玻璃 | glassOn | bool | 开 / 关 | true | — | 即时 | `button.glassOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textOn` | 显示文字 | textOn | bool | 开 / 关 | true | — | 即时 | `button.textOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `PillTune::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩 / 表面 | `tinted` | 着色（Hue） | tinted | bool | 开 / 关 | false | — | 即时 | `button.tinted` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `tint` | 着色颜色 | tint | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,1.000 | — | 即时 | `button.tint` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaced` | 表面色（Overlay） | surfaced | bool | 开 / 关 | false | — | 即时 | `button.surfaced` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surface` | 表面颜色 | surface | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.300 | — | 即时 | `button.surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaceModeOverride` | 强制表面模式 | surfaceMode | enum | -1 ~ 2 | -1 | — | 即时 | `PillTune::surfaceModeOverride` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackR` | 兜底色 R | fallbackR | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `fallbackG` | 兜底色 G | fallbackG | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackB` | 兜底色 B | fallbackB | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackA` | 兜底色 A | fallbackA | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 光影 | `shadowOn` | 外阴影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `button.shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowTint` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PillTune::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetTint` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `contourEdge` | 轮廓光 | contourEdge | bool | 开 / 关 | false | — | 即时 | `button.contourEdge` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `contourAlpha` | 轮廓光基础强度 | contourAlpha | float | 0 ~ 1 × | 0.5000 | × | 即时 | `EDGE_CONTOUR_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourPressGain` | 轮廓光按压增益 | contourPressGain | float | 0 ~ 1 × | 0.3400 | × | 即时 | `EDGE_CONTOUR_PRESS_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourWidthDp` | 轮廓光宽度 | contourWidth | float | 0 ~ 6 dp | 0.6000 | dp | 即时 | `EDGE_CONTOUR_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourSoftnessDp` | 轮廓光柔度 | contourSoftness | float | 0 ~ 4 dp | 0.3000 | dp | 即时 | `EDGE_CONTOUR_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleY` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `PanelStyle::backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 交互 | `glowStiffness` | 按压缩放弹簧刚度 | pressStiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `PRESS_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `glowDamping` | 按压弹簧阻尼 | pressDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `PRESS_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerStiffness` | 光斑跟随刚度 | pointerStiffness | float | 10 ~ 2e+03 × | 300.0000 | × | 即时 | `POINTER_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerDamping` | 光斑跟随阻尼 | pointerDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `POINTER_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 能力缺口（只读） | `gapIconGap` | 图标与文字间距 | iconGap | 缺口 | — | — | — | — | G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段 |
| 能力缺口（只读） | `gapDragTanh` | 拖动跟手度 | dragTanh | 缺口 | — | — | — | — | G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05） |

### 按钮 2 · 着色（`secondary_window.pill_button_2`）

着色（Hue 混合）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 宽度 | width | float | 24 ~ 420 dp | 140.0000 | dp | 即时 | `s_ButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `heightDp` | 高度 | height | float | 20 ~ 160 dp | 48.0000 | dp | 即时 | `s_ButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 40 dp | 12.0000 | dp | 即时 | `button.lensHeightDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `button.lensAmountDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `button.spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textSizeSp` | 文字大小 | textSizeSp | float | 8 ~ 40 sp | 16.0000 | sp | 即时 | `button.textSizeSp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `visualScale` | 视觉缩放 | visualScale | float | 0 ~ 1.5 × | 1.0000 | × | 即时 | `BUTTON_VISUAL_SCALE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `alphaScale` | 整体 alpha | alphaScale | float | 0 ~ 1 × | 1.0000 | × | 即时 | `button.alphaScale` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `glassOn` | 启用玻璃 | glassOn | bool | 开 / 关 | true | — | 即时 | `button.glassOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textOn` | 显示文字 | textOn | bool | 开 / 关 | true | — | 即时 | `button.textOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `PillTune::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩 / 表面 | `tinted` | 着色（Hue） | tinted | bool | 开 / 关 | true | — | 即时 | `button.tinted` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `tint` | 着色颜色 | tint | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,1.000 | — | 即时 | `button.tint` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaced` | 表面色（Overlay） | surfaced | bool | 开 / 关 | false | — | 即时 | `button.surfaced` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surface` | 表面颜色 | surface | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.300 | — | 即时 | `button.surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaceModeOverride` | 强制表面模式 | surfaceMode | enum | -1 ~ 2 | -1 | — | 即时 | `PillTune::surfaceModeOverride` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackR` | 兜底色 R | fallbackR | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `fallbackG` | 兜底色 G | fallbackG | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackB` | 兜底色 B | fallbackB | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackA` | 兜底色 A | fallbackA | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 光影 | `shadowOn` | 外阴影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `button.shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowTint` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PillTune::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetTint` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `contourEdge` | 轮廓光 | contourEdge | bool | 开 / 关 | false | — | 即时 | `button.contourEdge` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `contourAlpha` | 轮廓光基础强度 | contourAlpha | float | 0 ~ 1 × | 0.5000 | × | 即时 | `EDGE_CONTOUR_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourPressGain` | 轮廓光按压增益 | contourPressGain | float | 0 ~ 1 × | 0.3400 | × | 即时 | `EDGE_CONTOUR_PRESS_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourWidthDp` | 轮廓光宽度 | contourWidth | float | 0 ~ 6 dp | 0.6000 | dp | 即时 | `EDGE_CONTOUR_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourSoftnessDp` | 轮廓光柔度 | contourSoftness | float | 0 ~ 4 dp | 0.3000 | dp | 即时 | `EDGE_CONTOUR_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleY` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `PanelStyle::backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 交互 | `glowStiffness` | 按压缩放弹簧刚度 | pressStiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `PRESS_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `glowDamping` | 按压弹簧阻尼 | pressDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `PRESS_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerStiffness` | 光斑跟随刚度 | pointerStiffness | float | 10 ~ 2e+03 × | 300.0000 | × | 即时 | `POINTER_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerDamping` | 光斑跟随阻尼 | pointerDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `POINTER_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 能力缺口（只读） | `gapIconGap` | 图标与文字间距 | iconGap | 缺口 | — | — | — | — | G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段 |
| 能力缺口（只读） | `gapDragTanh` | 拖动跟手度 | dragTanh | 缺口 | — | — | — | — | G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05） |

### 按钮 3 · 表面色（`secondary_window.pill_button_3`）

表面色（Overlay 混合）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 宽度 | width | float | 24 ~ 420 dp | 140.0000 | dp | 即时 | `s_ButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `heightDp` | 高度 | height | float | 20 ~ 160 dp | 48.0000 | dp | 即时 | `s_ButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 40 dp | 12.0000 | dp | 即时 | `button.lensHeightDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `button.lensAmountDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `button.spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textSizeSp` | 文字大小 | textSizeSp | float | 8 ~ 40 sp | 16.0000 | sp | 即时 | `button.textSizeSp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `visualScale` | 视觉缩放 | visualScale | float | 0 ~ 1.5 × | 1.0000 | × | 即时 | `BUTTON_VISUAL_SCALE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `alphaScale` | 整体 alpha | alphaScale | float | 0 ~ 1 × | 1.0000 | × | 即时 | `button.alphaScale` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `glassOn` | 启用玻璃 | glassOn | bool | 开 / 关 | true | — | 即时 | `button.glassOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textOn` | 显示文字 | textOn | bool | 开 / 关 | true | — | 即时 | `button.textOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `PillTune::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩 / 表面 | `tinted` | 着色（Hue） | tinted | bool | 开 / 关 | false | — | 即时 | `button.tinted` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `tint` | 着色颜色 | tint | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,1.000 | — | 即时 | `button.tint` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaced` | 表面色（Overlay） | surfaced | bool | 开 / 关 | true | — | 即时 | `button.surfaced` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surface` | 表面颜色 | surface | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.300 | — | 即时 | `button.surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaceModeOverride` | 强制表面模式 | surfaceMode | enum | -1 ~ 2 | -1 | — | 即时 | `PillTune::surfaceModeOverride` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackR` | 兜底色 R | fallbackR | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `fallbackG` | 兜底色 G | fallbackG | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackB` | 兜底色 B | fallbackB | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackA` | 兜底色 A | fallbackA | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 光影 | `shadowOn` | 外阴影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `button.shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowTint` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PillTune::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetTint` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `contourEdge` | 轮廓光 | contourEdge | bool | 开 / 关 | false | — | 即时 | `button.contourEdge` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `contourAlpha` | 轮廓光基础强度 | contourAlpha | float | 0 ~ 1 × | 0.5000 | × | 即时 | `EDGE_CONTOUR_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourPressGain` | 轮廓光按压增益 | contourPressGain | float | 0 ~ 1 × | 0.3400 | × | 即时 | `EDGE_CONTOUR_PRESS_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourWidthDp` | 轮廓光宽度 | contourWidth | float | 0 ~ 6 dp | 0.6000 | dp | 即时 | `EDGE_CONTOUR_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourSoftnessDp` | 轮廓光柔度 | contourSoftness | float | 0 ~ 4 dp | 0.3000 | dp | 即时 | `EDGE_CONTOUR_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleY` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `PanelStyle::backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 交互 | `glowStiffness` | 按压缩放弹簧刚度 | pressStiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `PRESS_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `glowDamping` | 按压弹簧阻尼 | pressDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `PRESS_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerStiffness` | 光斑跟随刚度 | pointerStiffness | float | 10 ~ 2e+03 × | 300.0000 | × | 即时 | `POINTER_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerDamping` | 光斑跟随阻尼 | pointerDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `POINTER_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 能力缺口（只读） | `gapIconGap` | 图标与文字间距 | iconGap | 缺口 | — | — | — | — | G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段 |
| 能力缺口（只读） | `gapDragTanh` | 拖动跟手度 | dragTanh | 缺口 | — | — | — | — | G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05） |

### 按钮 4 · 小按钮（`secondary_window.pill_button_4`）

小尺寸「+」按钮

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 宽度 | width | float | 24 ~ 420 dp | 48.0000 | dp | 即时 | `s_ButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `heightDp` | 高度 | height | float | 20 ~ 160 dp | 48.0000 | dp | 即时 | `s_ButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 40 dp | 12.0000 | dp | 即时 | `button.lensHeightDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `button.lensAmountDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `button.spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textSizeSp` | 文字大小 | textSizeSp | float | 8 ~ 40 sp | 16.0000 | sp | 即时 | `button.textSizeSp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `visualScale` | 视觉缩放 | visualScale | float | 0 ~ 1.5 × | 1.0000 | × | 即时 | `BUTTON_VISUAL_SCALE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `alphaScale` | 整体 alpha | alphaScale | float | 0 ~ 1 × | 1.0000 | × | 即时 | `button.alphaScale` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `glassOn` | 启用玻璃 | glassOn | bool | 开 / 关 | true | — | 即时 | `button.glassOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textOn` | 显示文字 | textOn | bool | 开 / 关 | true | — | 即时 | `button.textOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `PillTune::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩 / 表面 | `tinted` | 着色（Hue） | tinted | bool | 开 / 关 | false | — | 即时 | `button.tinted` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `tint` | 着色颜色 | tint | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,1.000 | — | 即时 | `button.tint` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaced` | 表面色（Overlay） | surfaced | bool | 开 / 关 | false | — | 即时 | `button.surfaced` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surface` | 表面颜色 | surface | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.300 | — | 即时 | `button.surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaceModeOverride` | 强制表面模式 | surfaceMode | enum | -1 ~ 2 | -1 | — | 即时 | `PillTune::surfaceModeOverride` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackR` | 兜底色 R | fallbackR | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `fallbackG` | 兜底色 G | fallbackG | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackB` | 兜底色 B | fallbackB | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackA` | 兜底色 A | fallbackA | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 光影 | `shadowOn` | 外阴影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `button.shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowTint` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PillTune::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetTint` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `contourEdge` | 轮廓光 | contourEdge | bool | 开 / 关 | false | — | 即时 | `button.contourEdge` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `contourAlpha` | 轮廓光基础强度 | contourAlpha | float | 0 ~ 1 × | 0.5000 | × | 即时 | `EDGE_CONTOUR_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourPressGain` | 轮廓光按压增益 | contourPressGain | float | 0 ~ 1 × | 0.3400 | × | 即时 | `EDGE_CONTOUR_PRESS_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourWidthDp` | 轮廓光宽度 | contourWidth | float | 0 ~ 6 dp | 0.6000 | dp | 即时 | `EDGE_CONTOUR_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourSoftnessDp` | 轮廓光柔度 | contourSoftness | float | 0 ~ 4 dp | 0.3000 | dp | 即时 | `EDGE_CONTOUR_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleY` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `PanelStyle::backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 交互 | `glowStiffness` | 按压缩放弹簧刚度 | pressStiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `PRESS_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `glowDamping` | 按压弹簧阻尼 | pressDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `PRESS_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerStiffness` | 光斑跟随刚度 | pointerStiffness | float | 10 ~ 2e+03 × | 300.0000 | × | 即时 | `POINTER_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerDamping` | 光斑跟随阻尼 | pointerDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `POINTER_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 能力缺口（只读） | `gapIconGap` | 图标与文字间距 | iconGap | 缺口 | — | — | — | — | G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段 |
| 能力缺口（只读） | `gapDragTanh` | 拖动跟手度 | dragTanh | 缺口 | — | — | — | — | G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05） |

### 按钮 5 · 弹窗触发（`secondary_window.pill_button_5`）

液态弹窗的触发按钮

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 宽度 | width | float | 24 ~ 420 dp | 180.0000 | dp | 即时 | `s_ButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `heightDp` | 高度 | height | float | 20 ~ 160 dp | 48.0000 | dp | 即时 | `s_ButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 40 dp | 12.0000 | dp | 即时 | `button.lensHeightDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `button.lensAmountDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `button.spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textSizeSp` | 文字大小 | textSizeSp | float | 8 ~ 40 sp | 16.0000 | sp | 即时 | `button.textSizeSp` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `visualScale` | 视觉缩放 | visualScale | float | 0 ~ 1.5 × | 1.0000 | × | 即时 | `BUTTON_VISUAL_SCALE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `alphaScale` | 整体 alpha | alphaScale | float | 0 ~ 1 × | 1.0000 | × | 即时 | `button.alphaScale` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `glassOn` | 启用玻璃 | glassOn | bool | 开 / 关 | true | — | 即时 | `button.glassOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 折射 / 模糊 | `textOn` | 显示文字 | textOn | bool | 开 / 关 | true | — | 即时 | `button.textOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `PillTune::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerTrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBrDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `PillTune::cornerBlDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩 / 表面 | `tinted` | 着色（Hue） | tinted | bool | 开 / 关 | false | — | 即时 | `button.tinted` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `tint` | 着色颜色 | tint | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,1.000 | — | 即时 | `button.tint` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaced` | 表面色（Overlay） | surfaced | bool | 开 / 关 | false | — | 即时 | `button.surfaced` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surface` | 表面颜色 | surface | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.300 | — | 即时 | `button.surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `surfaceModeOverride` | 强制表面模式 | surfaceMode | enum | -1 ~ 2 | -1 | — | 即时 | `PillTune::surfaceModeOverride` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩 / 表面 | `fallbackR` | 兜底色 R | fallbackR | float | 0 ~ 1 × | 0.0000 | × | 即时 | `buttonStyle` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 色彩 / 表面 | `fallbackG` | 兜底色 G | fallbackG | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackB` | 兜底色 B | fallbackB | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 色彩 / 表面 | `fallbackA` | 兜底色 A | fallbackA | float | 0 ~ 1 × | 0.0000 | × | 即时 | — |
| 光影 | `shadowOn` | 外阴影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `button.shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowOffsetYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowTint` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PillTune::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetOffsetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetTint` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 光影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 光影 | `contourEdge` | 轮廓光 | contourEdge | bool | 开 / 关 | false | — | 即时 | `button.contourEdge` @ app/src/main/cpp/third_party/liquidglass/ytbl/PillButton.cpp |
| 光影 | `contourAlpha` | 轮廓光基础强度 | contourAlpha | float | 0 ~ 1 × | 0.5000 | × | 即时 | `EDGE_CONTOUR_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourPressGain` | 轮廓光按压增益 | contourPressGain | float | 0 ~ 1 × | 0.3400 | × | 即时 | `EDGE_CONTOUR_PRESS_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourWidthDp` | 轮廓光宽度 | contourWidth | float | 0 ~ 6 dp | 0.6000 | dp | 即时 | `EDGE_CONTOUR_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 光影 | `contourSoftnessDp` | 轮廓光柔度 | contourSoftness | float | 0 ~ 4 dp | 0.3000 | dp | 即时 | `EDGE_CONTOUR_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleY` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `PanelStyle::backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 交互 | `glowStiffness` | 按压缩放弹簧刚度 | pressStiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `PRESS_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `glowDamping` | 按压弹簧阻尼 | pressDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `PRESS_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerStiffness` | 光斑跟随刚度 | pointerStiffness | float | 10 ~ 2e+03 × | 300.0000 | × | 即时 | `POINTER_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pointerDamping` | 光斑跟随阻尼 | pointerDamping | float | 0.05 ~ 2 × | 0.5000 | × | 即时 | `POINTER_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 能力缺口（只读） | `gapIconGap` | 图标与文字间距 | iconGap | 缺口 | — | — | — | — | G14：BUTTON_ICON_GAP 是编译期常量（0.45×字号），PillButton 没有对应字段 |
| 能力缺口（只读） | `gapDragTanh` | 拖动跟手度 | dragTanh | 缺口 | — | — | — | — | G14：BUTTON_DRAG_TANH = 0.05 是编译期常量（Kyant 的 initialDerivative 同为 0.05） |

### 开关 1（`secondary_window.toggle_1`）

第一枚（默认关闭）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 状态 | `on` | 当前打开 | selected | bool | 开 / 关 | false | — | 即时 | `Toggle::on` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `accentColor` | 开启色 | accentColor | color | RGBA 0 ~ 1 | 0.188,0.820,0.345,1.000 | — | 即时 | `accentColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 交互 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_ToggleScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 交互 | `dragScaleDp` | 拖动换算 | dragScale | float | 2 ~ 100 dp | 20.0000 | dp | 即时 | `TOGGLE_DRAG_SCALE_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `dragSlopDp` | 拖动判定阈值 | dragSlop | float | 0 ~ 40 dp | 3.0000 | dp | 即时 | `TOGGLE_DRAG_SLOP_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 0.2 × | 0.0200 | × | 即时 | `TOGGLE_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pressedScale` | 按压缩放 | pressedScale | float | 0.5 ~ 3 × | 1.5000 | × | 即时 | `DRAG_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueStiffness` | 进度弹簧刚度 | valueStiffness | float | 10 ~ 3e+03 × | 1000.0000 | × | 即时 | `DRAG_VALUE_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueDamping` | 进度弹簧阻尼 | valueDamping | float | 0.05 ~ 2 × | 1.0000 | × | 即时 | `DRAG_VALUE_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 开关 1 · 轨道（`secondary_window.toggle_1.track`）

绿色/红色底轨：直接填充（不是面板），只受尺寸与颜色影响

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 轨道宽度 | trackWidth | float | 8 ~ 200 dp | 64.0000 | dp | 即时 | `TOGGLE_TRACK_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 轨道高度 | trackHeight | float | 4 ~ 120 dp | 28.0000 | dp | 即时 | `TOGGLE_TRACK_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `radiusRatio` | 圆角比例 | trackRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `trackRadius` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `trackColor` | 轨道底色 | trackColor | color | RGBA 0 ~ 1 | 0.471,0.471,0.502,0.361 | — | 即时 | `trackColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |

### 开关 1 · 圆钮（`secondary_window.toggle_1.thumb`）

玻璃圆钮：按下时模糊/折射/背景缩放一起变化（Kyant 的 LiquidToggle 同构）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 圆钮宽度 | thumbWidth | float | 8 ~ 200 dp | 40.0000 | dp | 即时 | `TOGGLE_THUMB_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 圆钮高度 | thumbHeight | float | 8 ~ 160 dp | 24.0000 | dp | 即时 | `TOGGLE_THUMB_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `paddingDp` | 起始内边距 | padding | float | 0 ~ 40 dp | 2.0000 | dp | 即时 | `TOGGLE_PADDING_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `travelDp` | 行程 | travel | float | 0 ~ 160 dp | 20.0000 | dp | 即时 | `TOGGLE_TRAVEL_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightPressDp` | 按压折射高度 | lensHeight | float | 0 ~ 40 dp | 5.0000 | dp | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountPressDp` | 按压折射强度 | lensAmount | float | 0 ~ 60 dp | 10.0000 | dp | 即时 | `TOGGLE_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensGain` | 折射增益 | lensGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurRestDp` | 静止模糊 | glassBlur | float | 0 ~ 10 dp | 4.0000 | dp | 帧边界·采样 | `GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurPressDp` | 按压模糊 | glassBlurFloor | float | 0 ~ 10 dp | 1.0000 | dp | 帧边界·采样 | `GLASS_BLUR_FLOOR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 1.0000 | × | 即时 | `TOGGLE_DARK_GUARD` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMin` | 静止背景缩放 X | stretchXMin | float | 0 ~ 2 × | 0.6667 | × | 即时 | `TOGGLE_BACKDROP_X_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMax` | 按压背景缩放 X | stretchXMax | float | 0 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_X_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMin` | 静止背景缩放 Y | stretchYMin | float | 0.001 ~ 2 × | 0.5500 | × | 即时 | `TOGGLE_BACKDROP_Y_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMax` | 按压背景缩放 Y | stretchYMax | float | 0.001 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_Y_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 1.0000 | × | 即时 | `backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceAlphaGain` | 表面透明度增益 | surfaceAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `edgeAlphaGain` | 边光强度增益 | edgeAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 32 dp | 4.0000 | dp | 帧边界·效果链 | `TOGGLE_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestAlpha` | 内阴影静止强度 | insetRestAlpha | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TOGGLE_INNER_REST_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestRadius` | 内阴影静止半径系数 | insetRestRadius | float | 0 ~ 1 × | 0.2500 | × | 即时 | `TOGGLE_INNER_REST_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapPressedScaleCurve` | 按压曲线自定义 | pressCurve | 缺口 | — | — | — | — | G14：Kyant 用 spring(dampingRatio, stiffness) 描述，ytbl 只能调刚度/阻尼两个数 |

### 开关 2（`secondary_window.toggle_2`）

第二枚（红色强调色）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 状态 | `on` | 当前打开 | selected | bool | 开 / 关 | false | — | 即时 | `Toggle::on` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `accentColor` | 开启色 | accentColor | color | RGBA 0 ~ 1 | 1.000,0.263,0.263,1.000 | — | 即时 | `accentColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 交互 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_ToggleScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 交互 | `dragScaleDp` | 拖动换算 | dragScale | float | 2 ~ 100 dp | 20.0000 | dp | 即时 | `TOGGLE_DRAG_SCALE_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `dragSlopDp` | 拖动判定阈值 | dragSlop | float | 0 ~ 40 dp | 3.0000 | dp | 即时 | `TOGGLE_DRAG_SLOP_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 0.2 × | 0.0200 | × | 即时 | `TOGGLE_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pressedScale` | 按压缩放 | pressedScale | float | 0.5 ~ 3 × | 1.5000 | × | 即时 | `DRAG_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueStiffness` | 进度弹簧刚度 | valueStiffness | float | 10 ~ 3e+03 × | 1000.0000 | × | 即时 | `DRAG_VALUE_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueDamping` | 进度弹簧阻尼 | valueDamping | float | 0.05 ~ 2 × | 1.0000 | × | 即时 | `DRAG_VALUE_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 开关 2 · 轨道（`secondary_window.toggle_2.track`）

绿色/红色底轨：直接填充（不是面板），只受尺寸与颜色影响

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 轨道宽度 | trackWidth | float | 8 ~ 200 dp | 64.0000 | dp | 即时 | `TOGGLE_TRACK_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 轨道高度 | trackHeight | float | 4 ~ 120 dp | 28.0000 | dp | 即时 | `TOGGLE_TRACK_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `radiusRatio` | 圆角比例 | trackRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `trackRadius` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `trackColor` | 轨道底色 | trackColor | color | RGBA 0 ~ 1 | 0.471,0.471,0.502,0.361 | — | 即时 | `trackColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |

### 开关 2 · 圆钮（`secondary_window.toggle_2.thumb`）

玻璃圆钮：按下时模糊/折射/背景缩放一起变化（Kyant 的 LiquidToggle 同构）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 圆钮宽度 | thumbWidth | float | 8 ~ 200 dp | 40.0000 | dp | 即时 | `TOGGLE_THUMB_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 圆钮高度 | thumbHeight | float | 8 ~ 160 dp | 24.0000 | dp | 即时 | `TOGGLE_THUMB_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `paddingDp` | 起始内边距 | padding | float | 0 ~ 40 dp | 2.0000 | dp | 即时 | `TOGGLE_PADDING_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `travelDp` | 行程 | travel | float | 0 ~ 160 dp | 20.0000 | dp | 即时 | `TOGGLE_TRAVEL_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightPressDp` | 按压折射高度 | lensHeight | float | 0 ~ 40 dp | 5.0000 | dp | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountPressDp` | 按压折射强度 | lensAmount | float | 0 ~ 60 dp | 10.0000 | dp | 即时 | `TOGGLE_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensGain` | 折射增益 | lensGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurRestDp` | 静止模糊 | glassBlur | float | 0 ~ 10 dp | 4.0000 | dp | 帧边界·采样 | `GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurPressDp` | 按压模糊 | glassBlurFloor | float | 0 ~ 10 dp | 1.0000 | dp | 帧边界·采样 | `GLASS_BLUR_FLOOR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 1.0000 | × | 即时 | `TOGGLE_DARK_GUARD` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMin` | 静止背景缩放 X | stretchXMin | float | 0 ~ 2 × | 0.6667 | × | 即时 | `TOGGLE_BACKDROP_X_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMax` | 按压背景缩放 X | stretchXMax | float | 0 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_X_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMin` | 静止背景缩放 Y | stretchYMin | float | 0.001 ~ 2 × | 0.5500 | × | 即时 | `TOGGLE_BACKDROP_Y_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMax` | 按压背景缩放 Y | stretchYMax | float | 0.001 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_Y_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 1.0000 | × | 即时 | `backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceAlphaGain` | 表面透明度增益 | surfaceAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `edgeAlphaGain` | 边光强度增益 | edgeAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 32 dp | 4.0000 | dp | 帧边界·效果链 | `TOGGLE_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestAlpha` | 内阴影静止强度 | insetRestAlpha | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TOGGLE_INNER_REST_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestRadius` | 内阴影静止半径系数 | insetRestRadius | float | 0 ~ 1 × | 0.2500 | × | 即时 | `TOGGLE_INNER_REST_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapPressedScaleCurve` | 按压曲线自定义 | pressCurve | 缺口 | — | — | — | — | G14：Kyant 用 spring(dampingRatio, stiffness) 描述，ytbl 只能调刚度/阻尼两个数 |

### 开关 3（`secondary_window.toggle_3`）

第三枚（默认打开）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 状态 | `on` | 当前打开 | selected | bool | 开 / 关 | true | — | 即时 | `Toggle::on` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `accentColor` | 开启色 | accentColor | color | RGBA 0 ~ 1 | 0.188,0.820,0.345,1.000 | — | 即时 | `accentColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 交互 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_ToggleScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 交互 | `dragScaleDp` | 拖动换算 | dragScale | float | 2 ~ 100 dp | 20.0000 | dp | 即时 | `TOGGLE_DRAG_SCALE_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `dragSlopDp` | 拖动判定阈值 | dragSlop | float | 0 ~ 40 dp | 3.0000 | dp | 即时 | `TOGGLE_DRAG_SLOP_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 0.2 × | 0.0200 | × | 即时 | `TOGGLE_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pressedScale` | 按压缩放 | pressedScale | float | 0.5 ~ 3 × | 1.5000 | × | 即时 | `DRAG_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueStiffness` | 进度弹簧刚度 | valueStiffness | float | 10 ~ 3e+03 × | 1000.0000 | × | 即时 | `DRAG_VALUE_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueDamping` | 进度弹簧阻尼 | valueDamping | float | 0.05 ~ 2 × | 1.0000 | × | 即时 | `DRAG_VALUE_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 开关 3 · 轨道（`secondary_window.toggle_3.track`）

绿色/红色底轨：直接填充（不是面板），只受尺寸与颜色影响

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 轨道宽度 | trackWidth | float | 8 ~ 200 dp | 64.0000 | dp | 即时 | `TOGGLE_TRACK_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 轨道高度 | trackHeight | float | 4 ~ 120 dp | 28.0000 | dp | 即时 | `TOGGLE_TRACK_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `radiusRatio` | 圆角比例 | trackRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `trackRadius` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 颜色 | `trackColor` | 轨道底色 | trackColor | color | RGBA 0 ~ 1 | 0.471,0.471,0.502,0.361 | — | 即时 | `trackColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |

### 开关 3 · 圆钮（`secondary_window.toggle_3.thumb`）

玻璃圆钮：按下时模糊/折射/背景缩放一起变化（Kyant 的 LiquidToggle 同构）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 圆钮宽度 | thumbWidth | float | 8 ~ 200 dp | 40.0000 | dp | 即时 | `TOGGLE_THUMB_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 圆钮高度 | thumbHeight | float | 8 ~ 160 dp | 24.0000 | dp | 即时 | `TOGGLE_THUMB_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `paddingDp` | 起始内边距 | padding | float | 0 ~ 40 dp | 2.0000 | dp | 即时 | `TOGGLE_PADDING_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `travelDp` | 行程 | travel | float | 0 ~ 160 dp | 20.0000 | dp | 即时 | `TOGGLE_TRAVEL_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightPressDp` | 按压折射高度 | lensHeight | float | 0 ~ 40 dp | 5.0000 | dp | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountPressDp` | 按压折射强度 | lensAmount | float | 0 ~ 60 dp | 10.0000 | dp | 即时 | `TOGGLE_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensGain` | 折射增益 | lensGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `TOGGLE_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurRestDp` | 静止模糊 | glassBlur | float | 0 ~ 10 dp | 4.0000 | dp | 帧边界·采样 | `GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurPressDp` | 按压模糊 | glassBlurFloor | float | 0 ~ 10 dp | 1.0000 | dp | 帧边界·采样 | `GLASS_BLUR_FLOOR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 1.0000 | × | 即时 | `TOGGLE_DARK_GUARD` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMin` | 静止背景缩放 X | stretchXMin | float | 0 ~ 2 × | 0.6667 | × | 即时 | `TOGGLE_BACKDROP_X_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMax` | 按压背景缩放 X | stretchXMax | float | 0 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_X_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMin` | 静止背景缩放 Y | stretchYMin | float | 0.001 ~ 2 × | 0.5500 | × | 即时 | `TOGGLE_BACKDROP_Y_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMax` | 按压背景缩放 Y | stretchYMax | float | 0.001 ~ 2 × | 0.7500 | × | 即时 | `TOGGLE_BACKDROP_Y_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 1.0000 | × | 即时 | `backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceAlphaGain` | 表面透明度增益 | surfaceAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `edgeAlphaGain` | 边光强度增益 | edgeAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Toggle::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Toggle.cpp |
| 表面 / 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 32 dp | 4.0000 | dp | 帧边界·效果链 | `TOGGLE_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestAlpha` | 内阴影静止强度 | insetRestAlpha | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TOGGLE_INNER_REST_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetRestRadius` | 内阴影静止半径系数 | insetRestRadius | float | 0 ~ 1 × | 0.2500 | × | 即时 | `TOGGLE_INNER_REST_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapPressedScaleCurve` | 按压曲线自定义 | pressCurve | 缺口 | — | — | — | — | G14：Kyant 用 spring(dampingRatio, stiffness) 描述，ytbl 只能调刚度/阻尼两个数 |

### 进度条 1（`secondary_window.slider_1`）

0~100 量程

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 值与量程 | `value` | 当前值 | value | float | 0 ~ 1 | 0.5000 | — | 即时 | `Slider::value` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 值与量程 | `minValue` | 量程下限 | minValue | float | 0 ~ 100 | 0.0000 | — | 即时 | `Slider::minValue` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 值与量程 | `maxValue` | 量程上限 | maxValue | float | 1 ~ 1e+03 | 1.0000 | — | 即时 | `Slider::maxValue` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 布局 | `widthFraction` | 宽度占比 | widthFraction | float | 0.2 ~ 1 × | 1.0000 | × | 即时 | `s_SliderWidthFraction` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 布局 | `heightDp` | 外框高度 | heightDp | float | 16 ~ 80 dp | 24.0000 | dp | 即时 | `s_SliderHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 布局 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_SliderScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 1 × | 0.1000 | × | 即时 | `SLIDER_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pressedScale` | 按压缩放 | pressedScale | float | 0.5 ~ 3 × | 1.5000 | × | 即时 | `DRAG_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueStiffness` | 值弹簧刚度 | valueStiffness | float | 10 ~ 3e+03 × | 1000.0000 | × | 即时 | `DRAG_VALUE_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueDamping` | 值弹簧阻尼 | valueDamping | float | 0.05 ~ 2 × | 1.0000 | × | 即时 | `DRAG_VALUE_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `thumbEdgeMin` | 滑杆左越界比 | thumbEdgeMin | float | 0 ~ 1 × | 0.2500 | × | 即时 | `SLIDER_THUMB_EDGE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `thumbEdgeMax` | 滑杆右越界比 | thumbEdgeMax | float | 0 ~ 1 × | 0.7500 | × | 即时 | `SLIDER_THUMB_EDGE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 进度条 1 · 轨道（`secondary_window.slider_1.track`）

底轨 + 已填充段：直接填充绘制（不是玻璃面板）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 轨道高度 | trackHeight | float | 2 ~ 40 dp | 6.0000 | dp | 即时 | `SLIDER_TRACK_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `radiusRatio` | 轨道圆角比例 | trackRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `rail` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 尺寸 | `fillRadiusRatio` | 填充圆角比例 | fillRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `fillRadius` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 颜色 | `accentColor` | 填充色 | accentColor | color | RGBA 0 ~ 1 | 0.000,0.569,1.000,1.000 | — | 即时 | `accentColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 颜色 | `trackColor` | 轨道底色 | trackColor | color | RGBA 0 ~ 1 | 0.471,0.471,0.502,0.361 | — | 即时 | `trackColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |

### 进度条 1 · 滑杆（`secondary_window.slider_1.thumb`）

玻璃滑杆：按下时模糊/折射/背景缩放一起变化

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 滑杆宽度 | thumbWidth | float | 8 ~ 160 dp | 40.0000 | dp | 即时 | `SLIDER_THUMB_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 滑杆高度 | thumbHeight | float | 8 ~ 120 dp | 24.0000 | dp | 即时 | `SLIDER_THUMB_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightPressDp` | 按压折射高度 | lensHeight | float | 0 ~ 60 dp | 12.0000 | dp | 即时 | `SLIDER_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountPressDp` | 按压折射强度 | lensAmount | float | 0 ~ 80 dp | 14.0000 | dp | 即时 | `SLIDER_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensGain` | 折射增益 | lensGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `SLIDER_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurRestDp` | 静止模糊 | glassBlur | float | 0 ~ 10 dp | 4.0000 | dp | 帧边界·采样 | `GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurPressDp` | 按压模糊 | glassBlurFloor | float | 0 ~ 10 dp | 1.0000 | dp | 帧边界·采样 | `GLASS_BLUR_FLOOR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleXMin` | 静止背景缩放 X | stretchXMin | float | 0 ~ 2 × | 0.6667 | × | 即时 | `SLIDER_BACKDROP_X_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMax` | 按压背景缩放 X | stretchXMax | float | 0 ~ 2 × | 1.0000 | × | 即时 | `SLIDER_BACKDROP_X_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMin` | 静止背景缩放 Y | stretchYMin | float | 0.001 ~ 2 × | 0.5500 | × | 即时 | `SLIDER_BACKDROP_Y_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMax` | 按压背景缩放 Y | stretchYMax | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `SLIDER_BACKDROP_Y_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 1.0000 | × | 即时 | `backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `surfaceAlphaGain` | 表面透明度增益 | surfaceAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `edgeAlphaGain` | 边光强度 | edgeAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `shadowRadiusDp` | 外阴影半径 | radius | float | 0 ~ 48 dp | 4.0000 | dp | 帧边界·效果链 | `SLIDER_SHADOW_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowOffsetYRatio` | 阴影偏移比 | offsetYRatio | float | 0 ~ 1 × | 0.1667 | × | 即时 | `SHADOW_OFFSET_DIVISOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowAlpha` | 阴影强度 | shadowAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 表面 / 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 40 dp | 4.0000 | dp | 帧边界·效果链 | `SLIDER_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetOnPress` | 按压时启用内阴影 | insetOnPress | bool | 开 / 关 | true | — | 即时 | `insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |

### 进度条 2（`secondary_window.slider_2`）

0~1 连续值

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 值与量程 | `value` | 当前值 | value | float | 0 ~ 100 | 50.0000 | — | 即时 | `Slider::value` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 值与量程 | `minValue` | 量程下限 | minValue | float | 0 ~ 100 | 0.0000 | — | 即时 | `Slider::minValue` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 值与量程 | `maxValue` | 量程上限 | maxValue | float | 1 ~ 1e+03 | 100.0000 | — | 即时 | `Slider::maxValue` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 布局 | `widthFraction` | 宽度占比 | widthFraction | float | 0.2 ~ 1 × | 1.0000 | × | 即时 | `s_SliderWidthFraction` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 布局 | `heightDp` | 外框高度 | heightDp | float | 16 ~ 80 dp | 24.0000 | dp | 即时 | `s_SliderHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 布局 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_SliderScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 1 × | 0.1000 | × | 即时 | `SLIDER_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `pressedScale` | 按压缩放 | pressedScale | float | 0.5 ~ 3 × | 1.5000 | × | 即时 | `DRAG_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueStiffness` | 值弹簧刚度 | valueStiffness | float | 10 ~ 3e+03 × | 1000.0000 | × | 即时 | `DRAG_VALUE_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `valueDamping` | 值弹簧阻尼 | valueDamping | float | 0.05 ~ 2 × | 1.0000 | × | 即时 | `DRAG_VALUE_DAMPING` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `thumbEdgeMin` | 滑杆左越界比 | thumbEdgeMin | float | 0 ~ 1 × | 0.2500 | × | 即时 | `SLIDER_THUMB_EDGE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `thumbEdgeMax` | 滑杆右越界比 | thumbEdgeMax | float | 0 ~ 1 × | 0.7500 | × | 即时 | `SLIDER_THUMB_EDGE_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 进度条 2 · 轨道（`secondary_window.slider_2.track`）

底轨 + 已填充段：直接填充绘制（不是玻璃面板）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 轨道高度 | trackHeight | float | 2 ~ 40 dp | 6.0000 | dp | 即时 | `SLIDER_TRACK_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `radiusRatio` | 轨道圆角比例 | trackRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `rail` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 尺寸 | `fillRadiusRatio` | 填充圆角比例 | fillRadiusRatio | float | 0 ~ 1 × | 0.5000 | × | 即时 | `fillRadius` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 颜色 | `accentColor` | 填充色 | accentColor | color | RGBA 0 ~ 1 | 0.000,0.569,1.000,1.000 | — | 即时 | `accentColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 颜色 | `trackColor` | 轨道底色 | trackColor | color | RGBA 0 ~ 1 | 0.471,0.471,0.502,0.361 | — | 即时 | `trackColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |

### 进度条 2 · 滑杆（`secondary_window.slider_2.thumb`）

玻璃滑杆：按下时模糊/折射/背景缩放一起变化

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `widthDp` | 滑杆宽度 | thumbWidth | float | 8 ~ 160 dp | 40.0000 | dp | 即时 | `SLIDER_THUMB_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 尺寸 | `heightDp` | 滑杆高度 | thumbHeight | float | 8 ~ 120 dp | 24.0000 | dp | 即时 | `SLIDER_THUMB_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightPressDp` | 按压折射高度 | lensHeight | float | 0 ~ 60 dp | 12.0000 | dp | 即时 | `SLIDER_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountPressDp` | 按压折射强度 | lensAmount | float | 0 ~ 80 dp | 14.0000 | dp | 即时 | `SLIDER_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensGain` | 折射增益 | lensGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `SLIDER_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurRestDp` | 静止模糊 | glassBlur | float | 0 ~ 10 dp | 4.0000 | dp | 帧边界·采样 | `GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurPressDp` | 按压模糊 | glassBlurFloor | float | 0 ~ 10 dp | 1.0000 | dp | 帧边界·采样 | `GLASS_BLUR_FLOOR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.0000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleXMin` | 静止背景缩放 X | stretchXMin | float | 0 ~ 2 × | 0.6667 | × | 即时 | `SLIDER_BACKDROP_X_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleXMax` | 按压背景缩放 X | stretchXMax | float | 0 ~ 2 × | 1.0000 | × | 即时 | `SLIDER_BACKDROP_X_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMin` | 静止背景缩放 Y | stretchYMin | float | 0.001 ~ 2 × | 0.5500 | × | 即时 | `SLIDER_BACKDROP_Y_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropScaleYMax` | 按压背景缩放 Y | stretchYMax | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `SLIDER_BACKDROP_Y_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 1.0000 | × | 即时 | `backdropBlend` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `surfaceAlphaGain` | 表面透明度增益 | surfaceAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 2 | — | 即时 | `surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `edgeAlphaGain` | 边光强度 | edgeAlphaGain | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Slider::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 表面 / 光影 | `shadowRadiusDp` | 外阴影半径 | radius | float | 0 ~ 48 dp | 4.0000 | dp | 帧边界·效果链 | `SLIDER_SHADOW_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowOffsetYRatio` | 阴影偏移比 | offsetYRatio | float | 0 ~ 1 × | 0.1667 | × | 即时 | `SHADOW_OFFSET_DIVISOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowAlpha` | 阴影强度 | shadowAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 表面 / 光影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 40 dp | 4.0000 | dp | 帧边界·效果链 | `SLIDER_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `insetOnPress` | 按压时启用内阴影 | insetOnPress | bool | 开 / 关 | true | — | 即时 | `insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/Slider.cpp |
| 边光 | `edgeMode` | 边光模式 | edgeMode | enum | 0 ~ 5 | 0 | — | 即时 | `EdgeTune::mode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeWidthDp` | 边光宽度 | width | float | 0 ~ 6 dp | 0.5000 | dp | 即时 | `EDGE_*_WIDTH` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeSoftnessDp` | 边光柔度 | blurRadius | float | 0 ~ 4 dp | 0.2500 | dp | 即时 | `EDGE_*_SOFTNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光 | `edgeAlpha` | 边光强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `EdgeLight::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeAngleDegrees` | 光照角度 | angle | float | -180 ~ 180 deg | 45.0000 | deg | 即时 | `EdgeLight::angleDegrees` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeFalloff` | 光照衰减 | falloff | float | 0 ~ 8 × | 1.0000 | × | 即时 | `EdgeLight::falloff` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgePresetTint` | 用主题预设色 | presetTint | bool | 开 / 关 | true | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 边光 | `edgeTint` | 边光颜色 | color | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.500 | — | 即时 | `EdgeLight::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |

### 底部导航栏（`secondary_window.navigation`）

液态玻璃底部标签栏（Kyant 的 LiquidBottomTabs）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 布局 | `visible` | 显示导航栏 | visible | bool | 开 / 关 | true | — | 即时 | `TabStrip::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 布局 | `tabCount` | 标签数量 | tabCount | int | 1 ~ 8 | 4 | 个 | 即时 | `TabStrip::tabCount` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 布局 | `activeIndex` | 选中项（0 基） | activeIndex | int | 0 ~ 7 | 0 | — | 即时 | `TabStrip::activeIndex` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 布局 | `heightDp` | 外框高度 | heightDp | float | 24 ~ 200 dp | 64.0000 | dp | 即时 | `TAB_PANEL_H_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 布局 | `widthFraction` | 宽度占比 | widthFraction | float | 0.2 ~ 1 × | 1.0000 | × | 即时 | `s_TabsWidthFraction` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 布局 | `scale` | 整体倍率 | scale | float | 0.3 ~ 2.5 × | 1.0000 | × | 即时 | `s_TabsScale` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 文字与图标 | `iconSizeDp` | 图标大小 | iconSize | float | 8 ~ 64 dp | 28.0000 | dp | 即时 | `TAB_ICON_SIZE_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 文字与图标 | `labelSizeDp` | 文字大小 | labelSize | float | 6 ~ 32 dp | 12.0000 | dp | 即时 | `TAB_LABEL_SIZE_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 文字与图标 | `iconColor` | 图标强制色 | iconColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `iconColor` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 交互 | `slopDp` | 拖动判定阈值 | slop | float | 0 ~ 40 dp | 8.0000 | dp | 即时 | `TAB_SLOP_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `panelShiftDp` | 拖动位移上限 | panelShift | float | 0 ~ 40 dp | 4.0000 | dp | 即时 | `TAB_PANEL_SHIFT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `cellScale` | 按压缩放 | cellScale | float | 1 ~ 2 × | 1.2000 | × | 即时 | `TAB_CELL_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `panelGrowDp` | 面板膨胀 | panelGrow | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `TAB_PANEL_GROW_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `velocityGain` | 速度增益 | velocityGain | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TAB_VELOCITY_GAIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `indicatorHeldScale` | 指示器按压缩放 | heldScale | float | 1 ~ 2 × | 1.3929 | × | 即时 | `TAB_INDICATOR_HELD_SCALE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `panelStiffness` | 回位弹簧刚度 | stiffness | float | 10 ~ 1e+03 × | 300.0000 | × | 即时 | `TAB_PANEL_STIFFNESS` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 交互 | `panelVisible` | 弹簧静止阈值 | visibility | float | 0.01 ~ 2 × | 0.5000 | × | 即时 | `TAB_PANEL_VISIBLE` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 颜色 | `accentDark` | 深色主题强调色 | accentDark | color | RGBA 0 ~ 1 | 0.000,0.569,1.000,1.000 | — | 即时 | `s_TabsAccentDark` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 颜色 | `accentLight` | 浅色主题强调色 | accentLight | color | RGBA 0 ~ 1 | 0.000,0.533,1.000,1.000 | — | 即时 | `s_TabsAccentLight` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 颜色 | `containerDark` | 深色主题容器色 | containerDark | color | RGBA 0 ~ 1 | 0.071,0.071,0.071,0.400 | — | 即时 | `s_TabsContainerDark` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 颜色 | `containerLight` | 浅色主题容器色 | containerLight | color | RGBA 0 ~ 1 | 0.980,0.980,0.980,0.400 | — | 即时 | `s_TabsContainerLight` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |

### 导航栏 · 背景层（`secondary_window.navigation.background`）

容器面板：模糊 + 折射 + 表面色 + 边光 + 投影 + 按压高光斑

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 8.0000 | dp | 帧边界·采样 | `TAB_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `TAB_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 64 dp | 24.0000 | dp | 即时 | `TAB_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `TabStrip::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `TabStrip::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 表面 / 光影 | `shadowOn` | 启用投影 | shadowOn | bool | 开 / 关 | true | — | 即时 | `TabStrip::draw` @ app/src/main/cpp/third_party/liquidglass/ytbl/TabBar.cpp |
| 表面 / 光影 | `shadowRadiusDp` | 投影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 / 光影 | `shadowOffsetYDp` | 投影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 / 光影 | `shadowTint` | 投影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.102 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 / 光影 | `shadowAlpha` | 投影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | — |
| 能力缺口（只读） | `gapIconLayer` | 图标层的独立玻璃效果 | iconLayer | 缺口 | — | — | — | — | G14：Kyant 的 LiquidBottomTabs 有独立的图标层（放大的背景 + accent 着色），ytbl 的 TabStrip 只有背景+指示器两层，没有图标层 |

### 导航栏 · 指示器（`secondary_window.navigation.indicator`）

选中项滑块：按压时出现透镜折射 + 内阴影 + 投影

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 折射 | `pressLensHeightDp` | 按压折射高度 | lensHeight | float | 0 ~ 40 dp | 10.0000 | dp | 即时 | `TAB_PRESS_LENS_HEIGHT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 | `pressLensAmountDp` | 按压折射强度 | lensAmount | float | 0 ~ 60 dp | 14.0000 | dp | 即时 | `TAB_PRESS_LENS_AMOUNT_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 | `spectral` | 色散强度 | spectral | float | 0 ~ 2 × | 1.0000 | × | 即时 | `TAB_SPECTRAL` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `idleAlpha` | 静止表面 alpha | idleAlpha | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TAB_IDLE_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `pressAlpha` | 按压表面 alpha | pressAlpha | float | 0 ~ 1 × | 0.0300 | × | 即时 | `TAB_PRESS_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `pressInnerDp` | 按压内阴影半径 | pressInner | float | 0 ~ 40 dp | 8.0000 | dp | 帧边界·效果链 | `TAB_PRESS_INNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowDp` | 投影半径 | shadowRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `TAB_SHADOW_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 表面 / 光影 | `shadowAlpha` | 投影强度 | shadowAlpha | float | 0 ~ 1 × | 0.1000 | × | 即时 | `TAB_SHADOW_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |

### 胶囊 1（`secondary_window.chip_1`）

形状行：圆角各自独立（胶囊 / 统一圆角 / 四角独立）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | true | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 胶囊 2（`secondary_window.chip_2`）

形状行：圆角各自独立（胶囊 / 统一圆角 / 四角独立）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 胶囊 3（`secondary_window.chip_3`）

形状行：圆角各自独立（胶囊 / 统一圆角 / 四角独立）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 8.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 8.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 胶囊 4（`secondary_window.chip_4`）

边光行：边光模式各自独立（Gradient / Ambient / Plain）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 胶囊 5（`secondary_window.chip_5`）

边光行：边光模式各自独立（Gradient / Ambient / Plain）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 胶囊 6（`secondary_window.chip_6`）

边光行：边光模式各自独立（Gradient / Ambient / Plain）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 尺寸 | `heightDp` | 芯片高度 | chipHeight | float | 16 ~ 160 dp | 44.0000 | dp | 即时 | `s_ChipHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 尺寸 | `gapDp` | 列间距 | chipGap | float | 0 ~ 64 dp | 16.0000 | dp | 即时 | `s_ChipGapDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | false | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.7000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 8.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 16.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 2.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 0.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 24.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 24.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 弹出菜单（`secondary_window.popmenu`）

胶囊触发的液态弹出菜单（Kyant 的 PopMenu 移植）

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 触发胶囊 | `capWidthDp` | 胶囊宽度 | capWidth | float | 40 ~ 420 dp | 180.0000 | dp | 即时 | `s_MenuCapWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 触发胶囊 | `capHeightDp` | 胶囊高度 | capHeight | float | 24 ~ 160 dp | 44.0000 | dp | 即时 | `s_MenuCapHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 触发胶囊 | `capTextSp` | 胶囊文字大小 | capTextSp | float | 8 ~ 32 sp | 17.0000 | sp | 即时 | `MENU_CAP_TEXT_SP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `widthDp` | 面板宽度 | width | float | 80 ~ 520 dp | 200.0000 | dp | 即时 | `MENU_DEFAULT_W_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `cornerDp` | 面板圆角 | corner | float | 0 ~ 96 dp | 32.0000 | dp | 即时 | `MENU_DEFAULT_CORNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `bandCornerDp` | 高亮条圆角 | bandCorner | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `MENU_DEFAULT_BAND_CORNER_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `threadDp` | 连接线粗细 | thread | float | 0 ~ 40 dp | 5.5000 | dp | 即时 | `MENU_DEFAULT_THREAD_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `gapDp` | 胶囊与面板的间隙 | gap | float | 0 ~ 160 dp | 44.0000 | dp | 即时 | `MENU_DEFAULT_GAP_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `greetDp` | 问候位移 | greet | float | 0 ~ 96 dp | 12.0000 | dp | 即时 | `MENU_DEFAULT_GREET_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `drainStart` | 排水起点 | drainStart | float | 0 ~ 1 × | 0.1000 | × | 即时 | `MENU_DRAIN_START` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `drainSpan` | 排水跨度 | drainSpan | float | 0.01 ~ 1 × | 0.8000 | × | 即时 | `MENU_DRAIN_SPAN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `stretchGain` | 拉伸增益 | stretchGain | float | 0 ~ 3 × | 1.0000 | × | 即时 | `MENU_JELLY_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `jelly` | 果冻强度 | jelly | float | 0 ~ 3 × | 1.0000 | × | 即时 | `MENU_JELLY_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 菜单面板配方 | `materialFlare` | 材质闪光 | materialFlare | bool | 开 / 关 | true | — | 即时 | `MENU_FLARE_SPECTRAL` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光与高亮条 | `rimDp` | 边光宽度倍率 | rimDp | float | 0 ~ 4 × | 1.0000 | × | 即时 | `MENU_RIM_WIDTH_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光与高亮条 | `rimAlpha` | 边光强度 | rimAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `MENU_RIM_ALPHA` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 边光与高亮条 | `bandColor` | 高亮条颜色 | bandColor | color | RGBA 0 ~ 1 | 1.000,1.000,1.000,0.239 | — | 即时 | `MENU_BAND` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 行为 | `reach` | 伸出方向 | reach | enum | 0 ~ 4 | 0 | — | 即时 | `s_MenuReach` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 行为 | `clampToHost` | 限制在宿主内 | clampToHost | bool | 开 / 关 | true | — | 即时 | `s_MenuClampToHost` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 行为 | `closeOnOutside` | 点击外部关闭 | closeOnOutside | bool | 开 / 关 | true | — | 即时 | `s_MenuCloseOnOutside` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 行为 | `expanded` | 展开菜单 | expanded | bool | 开 / 关 | false | — | 即时 | `s_MenuOpenRequest` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 行为 | `cursorRow` | 光标行 | cursorRow | int | 0 ~ 11 | 1 | — | 即时 | `s_MenuCursorRow` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 能力缺口（只读） | `gapMenuGlass` | 菜单面板内部的玻璃配方 | menuPanelGlass | 缺口 | — | — | — | — | G14：菜单面板的模糊/透镜/边光是 PopupMenu.cpp 里的 MENU_CAP_* / MENU_LENS_* 编译期常量，PopMenu 没有对应字段（本次未改动该文件） |

### 液态弹窗（`secondary_window.dialog`）

Modal 只公开 glassBlurDp 一个玻璃字段，其余配方写在 Dialog.cpp 里

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 玻璃 | `glassBlurDp` | 弹窗玻璃模糊 | glassBlur | float | 0 ~ 10 dp | 10.0000 | dp | 帧边界·采样 | `DIALOG_GLASS_BLUR_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 触发按钮 | `buttonWidthDp` | 按钮宽度 | width | float | 40 ~ 420 dp | 180.0000 | dp | 即时 | `s_DialogButtonWidthDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 触发按钮 | `buttonHeightDp` | 按钮高度 | height | float | 24 ~ 160 dp | 48.0000 | dp | 即时 | `s_DialogButtonHeightDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 动效 | `animationFromButton` | 从按钮展开 | fromButton | bool | 开 / 关 | true | — | 即时 | `s_DialogAnimationFromButton` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 能力缺口（只读） | `gapDialogRecipe` | 弹窗内部玻璃配方 | dialogGlassRecipe | 缺口 | — | — | — | — | G14：透镜/闪光/边光/阴影/圆环/尺寸都是 Dialog.cpp 的编译期常量（DIALOG_LENS_* / DIALOG_EDGE_* / DIALOG_SHADOW_* / DIALOG_RING_*），Modal 没有对应字段 |

### 玻璃实验场（`secondary_window.playground`）

可拖动/缩放的圆形玻璃对象：圆角比例、尺寸、折射都按对象尺寸归一化

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 几何 | `radiusRatio` | 圆角比例 | cornerRadius | float | 0.1 ~ 1 × | 0.5000 | × | 即时 | `s_PlaygroundRadius` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 几何 | `sizeDp` | 对象大小 | size | float | 96 ~ 256 dp | 256.0000 | dp | 即时 | `s_PlaygroundSizeDp` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 几何 | `offsetX` | 位置 X | offsetX | float | -200 ~ 200 px | 0.0000 | px | 即时 | `s_PlaygroundOffset` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 几何 | `offsetY` | 位置 Y | offsetY | float | -200 ~ 200 px | 0.0000 | px | 即时 | `s_PlaygroundOffset` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 几何 | `sheet` | 显示内置参数面板 | showControls | bool | 开 / 关 | true | — | 即时 | `s_PlaygroundSheet` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `chromaticOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `PanelTune` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `dispersion` | 色散强度 | dispersion | float | 0 ~ 2 × | 0.9000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 0.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `heightRatio` | 折射高度比例 | refractionHeight | float | 0 ~ 1 × | 0.2000 | × | 即时 | `s_PlaygroundHeight` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `amountRatio` | 折射强度比例 | refractionAmount | float | 0 ~ 1 × | 0.2000 | × | 即时 | `s_PlaygroundAmount` @ app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 1.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `capsule` | 胶囊形状 | capsule | bool | 开 / 关 | false | — | 即时 | `Corners::capsule` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 形状 | `cornerTlDp` | 左上圆角 | cornerTl | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerTrDp` | 右上圆角 | cornerTr | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBrDp` | 右下圆角 | cornerBr | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 形状 | `cornerBlDp` | 左下圆角 | cornerBl | float | 0 ~ 96 dp | 24.0000 | dp | 即时 | `corners()` @ app/src/main/cpp/glass/GlassScene.cpp |
| 折射 / 模糊 | `spectralOn` | 启用色散 | chromatic | bool | 开 / 关 | true | — | 即时 | `Optics::spectral` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `dispersion` | 色散强度 | spectral | float | 0 ~ 2 × | 0.9000 | × | 即时 | `SPECTRAL_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensHeightDp` | 折射高度 | lensHeight | float | 0 ~ 64 dp | 0.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `lensAmountDp` | 折射强度 | lensAmount | float | 0 ~ 96 dp | 0.0000 | dp | 即时 | `LENS_REACH_MAX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `blurDp` | 模糊半径 | blur | float | 0 ~ 10 dp | 0.0000 | dp | 帧边界·采样 | `BLUR_RADIUS_MAX_DP` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 折射 / 模糊 | `depthAmount` | 景深折射 | depthEffect | float | 0 ~ 1 × | 1.0000 | × | 即时 | `Optics::depthAmount` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 折射 / 模糊 | `darkGuard` | 暗部保底 | darkGuard | float | 0 ~ 1 × | 0.0000 | × | 即时 | `DARK_GUARD_FLOOR` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 色彩滤镜 | `colorBoost` | 鲜艳度增强 | vibrancy | bool | 开 / 关 | true | — | 即时 | `Optics::colorBoost` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `brightness` | 亮度 | brightness | float | -1 ~ 1 × | 0.0000 | × | 即时 | `Optics::brightness` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `contrast` | 对比度 | contrast | float | 0 ~ 2 × | 1.0000 | × | 即时 | `Optics::contrast` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `saturation` | 饱和度 | saturation | float | 0 ~ 3 × | 1.0000 | × | 即时 | `Optics::saturation` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacityOn` | 启用面板透明度 | opacityOn | bool | 开 / 关 | false | — | 即时 | `Optics::opacityOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `opacity` | 透明度 | opacity | float | 0.05 ~ 1 × | 1.0000 | × | 即时 | `Optics::opacity` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 色彩滤镜 | `alpha` | 整体 alpha | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `PanelStyle::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceOn` | 启用表面色 | surfaceOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::surfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surface` | 表面色 | surface | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.000 | — | 即时 | `PanelStyle::surface` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 表面 | `surfaceMode` | 表面模式 | surfaceMode | enum | 0 ~ 2 | 0 | — | 即时 | `SurfaceMode` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowOn` | 启用外阴影 | shadowOn | bool | 开 / 关 | false | — | 即时 | `PanelStyle::shadowOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowRadiusDp` | 阴影半径 | radius | float | 0 ~ 96 dp | 24.0000 | dp | 帧边界·效果链 | `DropShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowXDp` | 阴影偏移 X | offsetX | float | -64 ~ 64 dp | 0.0000 | dp | 帧边界·效果链 | `DropShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowYDp` | 阴影偏移 Y | offsetY | float | -64 ~ 64 dp | 4.0000 | dp | 帧边界·效果链 | `DropShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowColor` | 阴影颜色 | color | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.100 | — | 即时 | `DropShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `shadowAlpha` | 阴影强度 | alpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `DropShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetOn` | 启用内阴影 | insetOn | bool | 开 / 关 | true | — | 即时 | `PanelStyle::insetOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetRadiusDp` | 内阴影半径 | insetRadius | float | 0 ~ 64 dp | 6.0000 | dp | 帧边界·效果链 | `InsetShadow::radiusDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetXDp` | 内阴影偏移 X | insetOffsetX | float | -64 ~ 64 dp | 0.0000 | dp | 即时 | `InsetShadow::offsetXDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetYDp` | 内阴影偏移 Y | insetOffsetY | float | -64 ~ 64 dp | 6.0000 | dp | 即时 | `InsetShadow::offsetYDp` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetColor` | 内阴影颜色 | insetColor | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,0.149 | — | 即时 | `InsetShadow::color` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 阴影 | `insetAlpha` | 内阴影强度 | insetAlpha | float | 0 ~ 1 × | 1.0000 | × | 即时 | `InsetShadow::alpha` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropOn` | 启用背景采样 | backdropOn | bool | 开 / 关 | true | — | 帧边界·采样 | `PanelStyle::backdropOn` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `opaqueBackdrop` | 不透明背景 | opaqueBackdrop | bool | 开 / 关 | false | — | 即时 | `PanelStyle::opaqueBackdrop` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleX` | 背景缩放 X | stretchX | float | 0 ~ 2 × | 1.0000 | × | 即时 | `PanelStyle::backdropScaleX` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 背景采样 | `backdropScaleY` | 背景缩放 Y | stretchY | float | 0.001 ~ 2 × | 1.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 背景采样 | `backdropBlend` | 背景混合量 | stretchMix | float | 0 ~ 1 × | 0.0000 | × | 即时 | `BACKDROP_SCALE_MIN` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassConfig.h |
| 兜底色 | `fallbackFollowTheme` | 兜底色跟随主题 | followTheme | bool | 开 / 关 | true | — | 即时 | `fallbackFollowTheme` @ app/src/main/cpp/glass/GlassScene.cpp |
| 兜底色 | `fallback` | 兜底色 | fallback | color | RGBA 0 ~ 1 | 0.000,0.000,0.000,1.000 | — | 即时 | `PanelStyle::fallback` @ app/src/main/cpp/third_party/liquidglass/ytbl/GlassUI.h |
| 能力缺口（只读） | `gapSquircle` | 连续曲率圆角 | continuousCorner | 缺口 | — | — | — | — | G1：ytbl 的圆角 SDF 是标准圆角，连续曲率（squircle）需要改 shader 的 SDF 实现 |
| 能力缺口（只读） | `gapBlurTile` | 模糊边缘模式 | TileMode | 缺口 | — | — | — | — | G2：Kyant 的 blur(radius, edgeTreatment) 有 Clamp/Decal/Repeat/Mirror；ytbl 的 gather pass 没有对应 uniform |
| 能力缺口（只读） | `gapBlurTaps` | 模糊抽头数 | blurTaps | 缺口 | — | — | — | — | G3：BLUR_TAPS / BLUR_DECIMATE_LIMIT 是 GLES 循环上界（编译期），不是 uniform |
| 能力缺口（只读） | `gapLensProfile` | 折射剖面曲线 | lensProfile | 缺口 | — | — | — | — | G4：Kyant 用 circleMap，ytbl 用 lensCurve；切换需要新增 uniform |
| 能力缺口（只读） | `gapNormalGain` | 法线场圆角增益 | gradRadiusGain | 缺口 | — | — | — | — | G5：Kyant 的 gradRadius = min(radius*1.5, ...)，ytbl 没有这个增益项 |
| 能力缺口（只读） | `gapEdgeBlend` | 边光混合模式 | edgeBlendMode | 缺口 | — | — | — | — | G6：Kyant 的 HighlightStyle 带 BlendMode（Plus / SrcOver），ytbl 只有加法高光 |
| 能力缺口（只读） | `gapMultiBackdrop` | 多背景源合成 | CombinedBackdrop | 缺口 | — | — | — | — | G7：Kyant 的 rememberCombinedBackdrop 可以把多个背景源按顺序画到一起；ytbl/本机是单一采集源 + 进程内 Z 序合成（见 docs/GLASS_ZORDER_COMPOSITOR.md） |
| 能力缺口（只读） | `gapBackdropTransform` | 背景任意仿射变换 | onDrawBackdrop | 缺口 | — | — | — | — | G8：Kyant 的 onDrawBackdrop 可以任意变换背景；ytbl 只有中心等比缩放 |
| 能力缺口（只读） | `gapRotation` | 图层旋转 | rotationZ | 缺口 | — | — | — | — | G9：Kyant 的 layerBlock 支持 rotationZ；ytbl 的 drawPanel 只接受轴对齐矩形 |
| 能力缺口（只读） | `gapRuntimeShader` | 自定义 RuntimeShader | runtimeShaderEffect | 缺口 | — | — | — | — | G10：需要 API 33 的 RuntimeShader；本机是预编译 GLES 程序 |
| 能力缺口（只读） | `gapSpread` | 阴影扩散 spread | spread | 缺口 | — | — | — | — | Kyant 与本实现都**没有** spread 参数 → 不提供（不是缺口） |

### 能力缺口总表（`secondary_window.capability_gaps`）

参考项目有、本实现没有（或不适合实时调）的能力 —— 只读 + 原因，不做假控件

| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapCombinedBackdrop` | CombinedBackdrop 多背景源 | CombinedBackdrop | 缺口 | — | — | — | — | 参考项目用 rememberCombinedBackdrop 把多个来源按顺序画进同一画布。本机是「进程内 Z 序合成 + 单一采集源」（docs/GLASS_ZORDER_COMPOSITOR.md），要做到等价需要在采集层支持多次采样并合并 |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapLayerBlock` | layerBlock 任意变换 | layerBlock | 缺口 | — | — | — | — | 参考项目的 layerBlock 支持 scale/rotationZ/transformOrigin 并会走InverseLayerScope 逆变换；ytbl 的面板只接受轴对齐矩形 + uStretchScale |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapRuntimeShader` | runtimeShaderEffect 自定义着色器 | runtimeShaderEffect | 缺口 | — | — | — | — | 参考项目要 API 33 的 RuntimeShader；本机是预编译 GLES 程序，接入需要把 shader 源做成运行时可注入（当前 shaders 是编译期字符串常量） |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapExposure` | exposureAdjustment | exposureAdjustment | 缺口 | — | — | — | — | 只在参考项目 1.0.6 存在（2.0.1 已移除）：ColorMatrix scale = 2^(ev/2.2)。本实现的 tone 只有 brightness/contrast/saturation |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapGamma` | gammaAdjustment | gammaAdjustment | 缺口 | — | — | — | — | 同上，只有 1.0.6：shader 里 pow(color.rgb, power)。接入需要在片元着色器新增 uniform + 一次 pow（约 3 行） |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapHighlightBlend` | Highlight BlendMode | blendMode | 缺口 | — | — | — | — | 参考项目的 HighlightStyle 带 BlendMode.Plus / SrcOver；ytbl 的边光是固定加法（uEdgeMode 只切换形状配方，不切换混合） |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapOnDrawSurface` | onDrawSurface 任意绘制回调 | onDrawSurface | 缺口 | — | — | — | — | 参考项目允许在玻璃表面与内容之间插入任意 DrawScope 绘制；ytbl 用 PanelStyle::surface + SurfaceMode 表达固定几种情况 |
| 参考项目能力对照（Kyant AndroidLiquidGlass 2.0.1） | `gapSquircle` | 连续曲率圆角（squircle） | continuousCorner | 缺口 | — | — | — | — | 参考项目用 com.kyant.shapes 的 ContinuousCapsule/RoundedRectangularShape；ytbl 的圆角 SDF 是标准圆角，需要改 shader |
| 平台限制（不是缺口） | `noteBlurCap` | 模糊上限 10dp | blurCap | 缺口 | — | — | — | — | BLUR_RADIUS_MAX_DP = 10dp 是 ytbl 的硬上限（不是没做，是会被截断） |
| 平台限制（不是缺口） | `noteAmbientAngle` | Ambient 的角度/衰减无效 | ambientAngle | 缺口 | — | — | — | — | shader 的 Ambient 分支只读 alpha/tint/band，不读 uEdgeAngle/uEdgeFalloff（参考项目的 Ambient 也把 angle/falloff 硬编码成 45/1.0） |
| 平台限制（不是缺口） | `noteSpread` | 阴影没有 spread | spread | 缺口 | — | — | — | — | 参考项目与本实现都没有 spread 参数 |

## 约束关系

| 目标 | 参数 | 约束 |
| --- | --- | --- |
| `secondary_window` | `opacity` | 仅在开启「调节窗口透明度」时生效 |
| `secondary_window` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.card` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.card` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.card` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.card` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.card` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.card` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.pill_button_1` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_1` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_1` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_1` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_1` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.pill_button_2` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_2` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_2` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_2` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_2` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.pill_button_3` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_3` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_3` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_3` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_3` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.pill_button_4` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_4` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_4` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_4` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_4` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.pill_button_5` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_5` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_5` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_5` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.pill_button_5` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.slider_1` | `value` | 上限跟随组件的 maxValue；改量程请用下面的「量程上限」参数 |
| `secondary_window.slider_1.thumb` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.slider_2` | `value` | 上限跟随组件的 maxValue；改量程请用下面的「量程上限」参数 |
| `secondary_window.slider_2.thumb` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_1` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_1` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_1` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_1` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_1` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_1` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.chip_2` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_2` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_2` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_2` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_2` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_2` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.chip_3` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_3` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_3` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_3` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_3` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_3` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.chip_4` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_4` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_4` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_4` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_4` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_4` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.chip_5` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_5` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_5` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_5` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_5` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_5` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.chip_6` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_6` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_6` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_6` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.chip_6` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.chip_6` | `opacity` | 仅在启用面板透明度时生效 |
| `secondary_window.playground` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.playground` | `cornerTlDp` | 关闭胶囊后才生效 |
| `secondary_window.playground` | `cornerTrDp` | 关闭胶囊后才生效 |
| `secondary_window.playground` | `cornerBrDp` | 关闭胶囊后才生效 |
| `secondary_window.playground` | `cornerBlDp` | 关闭胶囊后才生效 |
| `secondary_window.playground` | `dispersion` | 仅在启用色散时生效 |
| `secondary_window.playground` | `opacity` | 仅在启用面板透明度时生效 |

