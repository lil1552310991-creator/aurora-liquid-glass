# 玻璃配方离线验证（本机 GPU，无需设备）

> 目的：在设备不可用时，先把 M3 的玻璃参数调对，避免真机时间浪费在试参数上。
>
> 方法：用 `build/recon/offline/gl_shader_regress.py` 的 AI 助手，
> 在本机 GPU（桌面 GPU / OpenGL 3.3 Core，moderngl 独立上下文）
> 上**原样执行** ytbl 的三个 shader（只把 `#version 300 es` 改成 `330 core`、
> 删掉 `precision` 行），几何与 uniform 逐条对齐 `probe_m3_ytbl.cpp`。

## 一、修正前的配方存在三类问题

依据 `docs/GLASS_VISUAL_RECIPE.md` 对 `GlassConfig.h` 的逐条核对：

| 类别 | 问题 | 修正 |
| --- | --- | --- |
| 几何量 | 系统性偏小 1.6~6.3 倍 | 圆角 46→144、透镜 46/40→72/128、`lensDepth` 0.16→1.0 |
| 缺失层 | 没有整层霜面染色，也没有 vibrancy | `uSurfaceMode` 0→2 + `uSurfaceTint`，`uToneBoost` 0→1 |
| 过曝/过脏 | 折射染色给了 0.22、色散给了 1.0、内阴影反色 | 见下表 |

配置数据（480dpi，density = 3.0）：

| uniform | 修正前 | 修正后 | 依据 |
| --- | --- | --- | --- |
| `uCornerRadii` | 46 | **144** | `DIALOG_CLOSE_RADIUS_DP = 48dp` |
| `uLensHeight` | 46 | **72** | 24dp |
| `uLensAmount` | 40 | **128** | 48dp，被 `LENS_REACH_MAX` 截断 |
| `uLensDepth` | 0.16 | **1.0** | Kyant0 `depthEffect` 是 Bool，四处调用全传 `true` |
| `uBlurRadius` | 18 | **30** | `DIALOG_GLASS_BLUR_DP = 10dp`，也是上限 |
| `uSurfaceMode` | 0.0 | **2.0** | ytbl 四个组件全用 Overlay |
| `uSurfaceTint` | 未设 | **(0.071,0.071,0.071,0.40)** | Kyant0 `#121212 @ 0.4` |
| `uToneBoost` | 0 | **1.0** | vibrancy，饱和 ×1.5 |
| `uToneCtrl.x` | 0.04 | **0.0** | 0.04 是浅色主题的 `BRIGHTNESS_LIGHT` |
| `uSpectral` | 1.0 | **0.12** | `DIALOG_SPECTRAL_BASE` |
| `uRefractTint.a` | 0.22 | **0.00** | 参考深色主题下 `tintDeviation = 0` |
| `uEdgeBlur` | 1.2 | **0.75** | |
| `uEdgeAlpha` | 0.55 | **0.38** | Kyant0 `Highlight.Plain` 白 α0.38 |
| `uEdgeTint` | (0.92,0.96,1.0) 蓝调 | **(1,1,1)** | 参考是纯白 |
| `uInsetTint` | 白 | **黑** | 参考内阴影是黑色 α0.15；白 = 叠了第二道高光 |
| `uGlowOn` | 1.0 常开 | **0.0** | 参考里它是指针按下的光斑，静止面板应关 |

另有耦合项：**`gather margin` 必须同步放大**。
ytbl 公式 `pad = 3 + blur×2 + lens + lens×spectral`（`GlassPanel.cpp:78-92`），
按新配方约 **206 px**，而探针此前只留 40 px。
只调大透镜而不调 margin，四角的外向色散抽样会被 clamp —— 表现为
「越调越脏」，很容易被误判成参数没调对。

## 二、实测结果（本机 GPU 渲染）

四组对照，分离每个改动的单独贡献
（脚本：`build/recon/offline/recipe_compare.py`）：

```text
key          说明                    中心亮度
A_before     修正前（M3 原始值）      0.658
B_geom       只改几何量               0.658    Δmean 1.48    变化 2.1%
C_surface    只加霜面与色调           0.321    Δmean 13.85   变化 21.7%
D_after      修正后（全部）           0.321    Δmean 14.59   变化 21.7%
```

两个结论：

1. **几何量改动几乎不影响面板中心**（Δmean 1.48，仅 2.1% 像素变化）——
   因为圆角与透镜只作用在边缘与角部。**不能用中心区域判断几何改对没有**，
   必须看图或专门检查边缘。
2. **霜面染色 + 色调是观感变化的主因**（Δmean 13.85，21.7% 像素）。

## 三、参数扫描：一个被数据推翻的猜测

看 `recipe_grid.png` 时我怀疑「开启 vibrancy 后饱和度反而下降」，
于是做了单变量扫描（脚本：`build/recon/offline/recipe_sweep.py`）：

| 变体 | 亮度 | 饱和度 | Δmean |
| --- | --- | --- | --- |
| base（修正后基准） | 0.6814 | **0.8507** | — |
| 关闭 `uToneBoost` | 0.5694 | **0.6942** | 2.96 |
| 霜面 alpha 0.40→0.25 | 0.8331 | 0.8615 | 2.57 |
| 霜面 alpha 0.40→0.15 | 0.8941 | 0.8626 | 3.78 |
| 霜面改浅色 | 0.8408 | 0.8245 | 3.76 |
| 对比 1.1 | 0.7165 | **0.8747** | 0.75 |
| 亮度 0.06 | 0.7172 | 0.8230 | 1.31 |
| 浅霜面 + 亮度 0.06 | 0.8705 | 0.7917 | 5.19 |

**结论：猜测被推翻。** 关闭 `uToneBoost` 会让饱和度从 0.851 掉到 0.694，
说明它确实在做色彩增强（`vibrancy() ≡ saturation = 1.5`），方向是对的。

视觉上的「变灰」来自 **40% 霜面本身**，而这是参考实现的预期行为
（Kyant0 的 Dialog 就是 `#121212 @ 0.4`）。把霜面调淡（0.25 / 0.15）
或改浅色，反而让画面明显失色发灰（见 `sweep_grid.png` 下半两张）。

**唯一值得采纳的微调**：`uToneCtrl` 对比度 1.0 → **1.1**，
饱和度 0.851 → 0.875 且亮度几乎不变（0.681 → 0.717），
是所有变体里性价比最高的一项。

## 四、复现方式

```powershell
cd D:\纯c图形基座\build\recon\offline
python recipe_compare.py     # 修正前/后 + 中间组合，出 recipe_*.ppm
python recipe_sweep.py       # 单变量扫描，出 sweep_*.ppm
# PPM 转 PNG 后用 read_image 查看
```

产出图：

| 文件 | 内容 |
| --- | --- |
| `recipe_grid.png` | 2x2：修正前 / 只改几何 / 只加霜面 / 修正后 |
| `sweep_grid.png` | 2x2：基准 / 对比1.1 / 霜面0.25 / 浅霜面+亮度 |

## 五、本机 AI 助手 与真机的差异（必须知道）

本机验证覆盖了 shader 的数学与分支行为，但**不能替代真机**的部分：

- Adreno 830 的真实编译、精度与性能
- EGL + SurfaceFlinger 上屏链路
- 真实桌面反射（本机用的是合成的测试场景纹理）
- `inhibited` / uinput / 系统输入法 / 触摸命中率 / 帧率功耗

另外记一条方法学坑：**GL 的 uniform 状态是「粘」的**。
用「省略赋值」来测默认 0 会得到假阴性（沿用上一轮的值），
必须显式写 0 才能测准。
