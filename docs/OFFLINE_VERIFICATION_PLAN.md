# 无设备离线验证规划与执行报告（M3 / M3.5 阶段）

> 结论先行：**在没有测试机 的情况下，本机已经能对 ytbl 面板 shader 做真正的像素级回归**——本机 GPU（桌面 GPU，OpenGL 3.3 Core，经 moderngl 独立上下文）上直接编译并执行 `ytbl_panel.frag` / `ytbl_gather.frag` / `ytbl_quad.vert` 原文，本轮共跑了 1 张基线 + 21 个分支用例 + 67 个 uniform 敏感度用例（71 个 uniform 去掉 2 个采样器与 2 个几何 uniform）+ 1 个「全新程序全默认」用例。
>
> uniform 覆盖率审计结果：**未赋值清单为空**——panel program 71 个 uniform、gather program 10 个 uniform，`Context.cpp` 生产路径与 `probe_m3_ytbl.cpp` 探针路径都 100% 赋值。之前踩的两个坑（uPrism\* 静默失效、uSurfaceMode 语义导致整块变白）**不是漏赋值造成的**，本轮把它们的失败条件与量级实测出来了（见 2.3）。
>
> 附带产出：4 个可复跑的离线脚本（`build/recon/offline/` 下）、23 张回归 PNG、以及对 7 个既有宿主测试 exe 与 27 个 dlsym 符号的复核结果。

---

## 0. 本机环境探测（决定「能不能跑 GL」）

```
$ python -m pip list | Select-String "numpy|Pillow|moderngl"
numpy 2.5.0
pillow 12.3.0
（moderngl 未安装 → pip install moderngl 成功，网络可用，PyPI 可达）

$ python -c "import moderngl; ctx = moderngl.create_standalone_context(require=330); ..."
[GL] 3.3.0 Core Profile Context 26.8.1.260810 | 桌面 GPU | GPU 厂商
```

关键事实：

| 项 | 结果 |
|---|---|
| 本机 GPU | 桌面 GPU（另有核显 + GameViewer 虚拟显示适配器） |
| OpenGL | **3.3 Core，真实驱动**（不是 GDI generic、不是 llvmpipe） |
| 创建上下文方式 | `moderngl.create_standalone_context()`（Windows 走 WGL，无需窗口、无需桌面会话交互） |
| 编译器 | 无 MSVC/clang-cl；有 LLVM `clang++`（C:\Program Files\LLVM）、MinGW `g++`、cmake、ninja |
| NDK | `<sdk>\ndk\27.0.12077973`（含 `llvm-nm.exe`、`llvm-objdump.exe`） |
| 缺失 | kotlinc、gradle（HOST 侧构建）、Android 模拟器 —— 与本任务无关 |

**GLES → 桌面 GL 的翻译只有两处纯语法替换**（写在脚本里，可复核）：

```python
src.replace("#version 300 es", "#version 330 core")   # 版本行
# 并删除所有以 "precision " 开头的行（桌面 GLSL 无精度语义，删掉不影响数值）
```

未做任何语义改写：`texture()`、`fwidth()`、`inversesqrt()`、`smoothstep()`、`gl_VertexID` 生成几何等全部沿用原文；
uniform 赋值顺序、混合函数、FBO 尺寸、gather 的两遍可分离卷积（blit 裁窗+翻转 → 水平 → 垂直）全部逐行对应 `probe_m3_ytbl.cpp`。

---

## 第一部分 · 可验证事项清单（按 价值 × 可行性 排序）

| # | 事项 | 价值 | 可行性 | 需要的工具 | 预计耗时 | 本轮状态 |
|---|---|---|---|---|---|---|
| 1 | **shader uniform 覆盖率审计**（声明 vs 生产路径 vs 探针赋值） | ★★★★★ | 完全可行 | Python + 正则 | 0.5h | ✅ 已执行 |
| 2 | **GPU 像素级回归**：本机真跑 GLES shader 出图、逐像素比对 | ★★★★★ | 可行（本机 GPU） | moderngl、numpy、Pillow | 2h | ✅ 已执行 |
| 3 | **uniform「默认 0」敏感度矩阵**（逐个显式置 0，量化后果） | ★★★★★ | 可行 | 同上（复用 #2 的 AI 助手） | 1h | ✅ 已执行 |
| 4 | **SDF / 圆角 / 曲线数值不变量**（GPU 求值 shader 原文里的数学函数） | ★★★★☆ | 可行 | 同上 + 独立实现对照 | 1h | ✅ 已执行 |
| 5 | **dlsym 符号存在性审计**（探针 mangled 名 vs 设备原库） | ★★★★☆ | 部分可行（本机只有 libgui/libinputflinger） | NDK `llvm-nm` | 0.5h | ✅ 已执行 |
| 6 | **宿主测试套件回归**（InputRouter / GestureRouter / Motion / RenderBackend / FrameStats / RootBootstrapCli / BackdropWiring） | ★★★★☆ | 完全可行 | 已有 exe（cmake 可重建） | 10min | ✅ 已执行 |
| 7 | **M3.5 连续曲率圆角几何**（Kyant0 算法的控制点/切线连续性/延伸量） | ★★★★☆ | 可行（已有两个宿主程序） | 已有 `kyant_corner_check.exe`、`corner_real_geometry.exe` | 0.5h | ✅ 已执行 |
| 8 | **M3.5 squircle 进 shader 后的像素回归**（把新 SDF 塞进 #2 的 AI 助手，对比圆角区） | ★★★★★ | 可行 | 需先改 shader（待办） | 2–4h | ⏳ 方案已就绪 |
| 9 | 模糊权重 `buildTapMass` 正确性（σ=10.39 时 tap 权重、归一化） | ★★★☆☆ | 可行 | Python | 0.5h | ⏳ |
| 10 | 色调映射/颜色空间（`toneMap` 的中性值与灰阶陷阱） | ★★★★☆ | 可行 | 已在 #3 中实测到 | 0.5h | ✅（含在 #3） |
| 11 | screencap 输出格式（PNG 头 / rowStride / 像素序） | ★★☆☆☆ | 只能读 AOSP 源码静态确认 | `research/aosp/.../screencap.cpp` | 0.5h | ⏳ |
| 12 | SurfaceFlinger binder 事务格式（parcel 布局、transaction code） | ★★☆☆☆ | 只能静态比对 AOSP 头文件 + 符号表；**真机才能证实** | AOSP 16 头文件、`llvm-nm` | 1h | ✅ 部分（符号已查） |
| 13 | `/sys/class/input/eventN/device/inhibited` 语义、uinput 回灌 | ★☆☆☆☆ | **不可离线验证**（内核行为 + 权限） | —— | —— | ❌ 必须真机 |
| 14 | 字体/纹理资源加载校验（`font.h` 位图、纹理尺寸/格式） | ★★☆☆☆ | 可行 | Python/Pillow | 0.5h | ⏳ |
| 15 | IME 输入法桥接（InputConnection 协议、焦点切换） | ★★☆☆☆ | **不可离线验证**（需要系统 IME 服务） | —— | —— | ❌ 必须真机 |
| 16 | Adreno 驱动一致性（真机 GLES 编译/精度/性能） | ★★★★★ | **不可离线**（本机是 AMD 桌面驱动） | 设备 | —— | ❌ 必须真机 |

---

## 第二部分 · 实际执行的验证与原始输出

所有脚本位于 `build/recon/offline/`，均可直接复跑：

| 脚本 | 作用 |
|---|---|
| `audit_uniforms.py` | uniform 覆盖率审计 + shader 源码一致性 + 敏感度汇总（输出 `uniform_audit_report.md`） |
| `gl_shader_regress.py` | GPU 像素回归：基线 / 分支矩阵 / `--sweep` 敏感度矩阵 / virgin 全默认 |
| `sdf_math_check.py` | SDF、圆角、法线、lensCurve、fuse、棱镜权重的不变量检查 |
| `symbol_audit.py` | dlsym 符号 vs 设备原库符号表 |

### 2.1 shader 源码一致性（先确认「逐字嵌入」没走样）

```
$ python audit_uniforms.py
==============================================================================
步骤 1：shader 源码一致性（Shaders.cpp / build/recon/*.frag / 嵌入头）
==============================================================================
  quad.vert    Shaders.cpp 5ade39487656 | recon 5ade39487656 | embedded 5ade39487656 | 一致: True / True
  gather.frag  Shaders.cpp fef7768be559 | recon fef7768be559 | embedded fef7768be559 | 一致: True / True
  panel.frag   Shaders.cpp d0a9915b740a | recon d0a9915b740a | embedded d0a9915b740a | 一致: True / True
```

> 说明（诚实记录）：本脚本第一版用贪婪正则抓 `R"YTBL(...)YTBL"`，把 quad.vert 的嵌入文本误抓成「quad+gather+panel 三份拼起来」，于是报出「嵌入头与源码不一致」的**假警报**。改成非贪婪后三个 shader 全部逐字节一致（SHA-256 前 12 位相同）。**不是项目的问题，是我脚本的问题**，此处保留记录以免误导。

### 2.2 uniform 覆盖率审计（本次任务重点）

```
步骤 2：program 级 uniform 声明（quad.vert ∪ 各 frag）
  quad.vert   : 2 个
  panel.frag  : 69 个
  gather.frag : 8 个
  => panel program  71 个 / gather program 10 个

步骤 3：赋值覆盖率
[panel program] 共 71 个 uniform
  未在 Context.cpp 赋值的: 无（全覆盖）
  未在 probe 赋值的: 无（全覆盖）
[gather program] 共 10 个 uniform
  未在 Context.cpp 赋值的: 无（全覆盖）
  未在 probe 赋值的: 无（全覆盖）

步骤 4：Context.cpp 中 appear 但 shader 未声明的槽位（拼写/漂移）
  panel : 无
  gather: 无
  已声明槽位数 panel=71 gather=10；已 set 槽位数 panel=71 gather=10

步骤 5：未赋值清单 + 默认 0 分支判定
[panel program] 未赋值 0 个
[gather program] 未赋值 0 个
```

**未赋值清单：空。** 逐项核对的口径：

* panel program = `quad.vert`(2) ∪ `panel.frag`(69)，其中 `uScene`/`uGathered` 两个采样器在 `glStartup()` 里一次性绑到 texture unit 0/1，其余 69 个在每个 `glPaintPanel()` 帧里赋值（`Context.cpp:821-889`）。
* 8 个 `uPrism*` 数组在 `glStartup()` 里用 `setArray()` 从 `GlassConfig.h::Render::PRISM_*` 常量上传（`Context.cpp:619-633`），探针侧在 `probe_m3_ytbl.cpp:689-707` 用 `glGetUniformLocation + glUniform1fv` 复刻同一组常量。
* gather program 的 `uTapMass` 走的是 `uploadTapMass()`（`Context.cpp:399-402`）——先 `location(G_TAP_MASS)` 再 `glUniform1fv`，不是 `setArray`，普通正则容易漏。本脚本把「任何 `g_*Uniforms.location(SLOT)` 出现处」都算作上传点后才判定为全覆盖；第一版脚本把「已声明」当成「已赋值」，属于**会掩盖真问题的口径错误**，已修正。

**旁证（GPU 侧）**：探针路径的 71 个 uniform 在真实 GL 上下文里逐个赋值后回读，全部落地：

```
[uniform] 赋值成功 71 个，程序中不存在 0 个: []
```

### 2.3 GPU 像素回归与「默认 0」敏感度矩阵

命令：

```powershell
cd D:\纯c图形基座\build\recon\offline
$env:PYTHONIOENCODING='utf-8'; python gl_shader_regress.py --sweep
```

渲染几何完全照搬探针：面板 620×900 居中（1080×2376 屏），gather 区域 704×992、margin=40、σ=18×0.57735=10.39、step=2.60、12 taps；场景是程序生成的合成桌面（渐变 + 彩色棋盘 + RGB 竖条 + 锐利网格线），保证模糊/折射/色散都有可见响应。

**基线**（`regress_baseline.ppm/.png`，620×900）：

```
[baseline] {"rgb_mean": 0.651, "lum_mean": 0.651, "lum_std": 0.0667,
            "alpha_mean": 0.997, "white_frac": 0.012, "black_frac": 0.0028}
```

![基线](../build/recon/offline/regress_baseline.png)

肉眼可见：圆角矩形（r=46）内是模糊+折射后的桌面内容，边缘有一圈加法边缘光，四角圆角正常。**AI 助手 的保真度由此得到视觉确认**——如果着色器或管道搭错，不可能得到这张图。

#### (a) 分支矩阵（21 个用例，与基线逐像素比对）

| 用例 | lum | 白像素占比 | Δmean | Δ>16 占比 | 判定 |
|---|---|---|---|---|---|
| `uSurfaceMode=0, tint=(0,0,0,0)`（探针现值） | 0.651 | 1.2% | 0.00 | 0.00% | 基线 |
| `uSurfaceMode=1, tint=(0,0,0,0)` | 0.651 | 1.2% | 0.00 | 0.00% | **与基线完全相同** |
| **`uSurfaceMode=1, tint=(1,1,1,1)`** | 0.967 | **93.6%** | 60.34 | 99.34% | **整块变白（复现）** |
| **`uSurfaceMode=2, tint=(1,1,1,1)`** | 0.997 | **99.7%** | 66.11 | 99.34% | **整块变白（更彻底）** |
| `uSurfaceMode=2, tint=(0,0,0,0)` | 0.651 | 1.2% | 0.00 | 0.00% | 与基线相同 |
| **`prism 权重全 0`（= 未赋值时的默认）** | **0.224** | 0.0% | **81.81** | 99.72% | **整块塌成常数色** |
| **`uToneCtrl=0`（= 默认）** | 0.678 | 1.6% | 38.17 | 99.31% | **整块变均匀灰（丢失全部内容）** |
| `uPanelOpacity=0`（= 默认） | 0.014 | 0.0% | 185.13 | 99.72% | 面板完全透明 |
| `uSceneOn=0`（= 默认） | 0.005 | 0.0% | 186.82 | 99.72% | 面板变黑 |
| `uGatheredOn=0`（退化为直采 scene，不模糊） | 0.642 | 3.3% | 47.37 | 97.55% | 内容变化（非错误分支） |
| `edgeMode=0/1/2/4` | 0.647–0.650 | 0–0.6% | 0.24–0.90 | 1.0–1.6% | 仅边缘带变化 |
| `uSpectral=0` | 0.651 | 1.2% | 0.15 | 0.64% | 色散消失（量级很小） |
| `uCornerRadii=0` | 0.653 | 1.2% | 0.73 | 0.50% | 仅四角变化（可作 SDF 视觉回归） |
| `uLensAmount=0` / `uLensHeight=0` | 0.652 | 1.2% | 0.79 | 2.24% | 透镜位移消失 |
| `uDarkGuard=0` | 0.651 | 1.2% | 0.00 | 0.00% | 本场景无影响（场景太亮） |
| `uInsetAlpha=0` / `uGlowOn=0` | 0.651/0.644 | 1.2% | 0.07/1.44 | 0.00% | 细节项 |

**「整块面板变白」的根因已被钉死**：不是 `uSurfaceMode` 默认值的问题（mode=0 是安全默认），而是 **`uSurfaceMode>0.5` 与「白色且 alpha 高的 `uSurfaceTint`」同时成立**：

* `mode=1`（Hue）：`color.rgb = mix(color, surfaceTint*(0.35+1.3*luma), 0.75*tint.a)`，`color.a = max(color.a, 0.75*tint.a)` → 白色 tint 直接把面板洗成 93.6% 白像素。
* `mode=2`（Overlay）：按 alpha 合成 `surfaceTint` 与底色 → 白 tint 得到 99.7% 白像素。
* 只改 mode 而不动 tint（tint 全 0）时**输出与基线逐像素完全一致**（Δmean=0.00），说明这两个 uniform 必须一起看。

对照生产映射（`GlassPanel.cpp:259-260`）：`SurfaceMode::Overlay→2.0`、`Hue→1.0`、`None→0.0`；而 `PillButton/PopupMenu/Slider/Toggle/TabBar/Dialog` 大量使用 `Overlay`。**真机白屏时，第一件事应该是 dump 该帧的 `uSurfaceMode` 与 `uSurfaceTint`**——本轮已经把两者的组合后果量化好了。

#### (b) 「默认 0」敏感度矩阵（67 个 uniform 逐个显式置 0）

> 方法学提醒（重要）：**GL uniform 状态是「粘」的**。我第一版用「省略赋值」来模拟默认 0，得到的结果是**全部 Δ=0**——因为值还留在 GL 里。必须**显式写 0** 才是真正的默认值语义。这个坑本身就是一条结论：任何「靠不赋值来关掉某效果」的写法在 GL 里都不成立。

按后果排序（只列有影响的；其余 28 个置 0 后 Δmean=0.000，其中多数本就是「关闭」语义的默认值，如 `uTrack*`/`uShade*`/`uFeed*`/`uStretch*`）：

| uniform 置 0 | Δmean | Δ>16 占比 | 后果 |
|---|---|---|---|
| `uPanelSpan` | 188.13 | 99.72% | **面板整体消失（SDF 退化）** |
| `uDropRect` | 188.13 | 99.72% | **面板整体消失**——`if (uDropRect.z >= 0.0 && uDropRect.w >= 0.0)` 用 **-1 表示「无 drop 区」**，默认 0 **满足**条件，于是 `bodyHalf=(0,0)`、`round=uDropRound=0` |
| `uPanelOpacity` | 185.13 | 99.72% | 面板完全透明 |
| `uSceneOn` | 186.82 | 99.72% | 面板变黑（走 `uAbsentTint`=0 分支） |
| `uGatherLimit` | 76.90 | 99.61% | 采样贴图错位 |
| `uGatheredOn` | 47.37 | 97.56% | 退化为不模糊直采 |
| `uGatherPitch` | 38.01 | 99.09% | 采样坐标错 |
| `uToneCtrl` | 38.17 | 99.31% | **整块变均匀灰**（contrast=0 时 `toneMap` 把所有通道压成同一常数 + lift） |
| `uPrismEvenBlue` | 34.83 | 99.72% | 丢失桌面内容、整块偏色 |
| `uGatherTexel` | 33.56 | 63.31% | 采样错位 |
| `uRefractTint` | 23.37 | 99.56% | 折射染色丢失 |
| `uPrismEvenGreen` | 20.60 | 99.37% | 同上 |
| `uPrismEvenRed` | 16.60 | 99.71% | 同上 |
| `uGatherRows` | 11.60 | 25.72% | 采样错位 |
| `uSampleShift` | 40.10 | 99.08% | 采样原点错（面板取到别处的内容） |
| `uPrismCenterGreen` | 5.91 | 96.10% | 绿色通道丢失（面板塌成常数色） |
| `uGlowReach` / `uGlowOn` / `uGlowLevel` | 1.75 / 1.44 / 1.44 | Δ>16=0 | 辉光细节消失 |
| `uEdgeMode` / `uEdgeAlpha` / `uEdgeTintAlpha` / `uEdgeTint` | 0.90 | 1.64% | 边缘光消失 |
| `uLensHeight` / `uLensAmount` | 0.79 | 2.24% | 透镜折射消失 |
| `uCornerRadii` | 0.73 | 0.50% | 角变直角 |
| `uInsetTint` / `uInsetReach` / `uInsetAlpha` | 0.24 / 0.07 / 0.07 | 0–1.4% | 内描边减弱 |
| `uSpectral` / `uPrismDist` | 0.146 | 0.64% | 色散位移消失 |
| `uPrismOdd*` | 0.011–0.067 | 0–2.1% | 非对称项，量级最小 |
| `uDarkGuard` | 0.000 | 0.00% | 本场景未触发（合成桌面太亮） |
| `uSampleSpan` / `uSampleTexel` | 0.000 | 0.00% | **在当前用例下被分支遮蔽**：`uGatheredOn=1` 时 `sceneUv()` 根本不被调用；一旦 gather 未就绪回落（`uGatheredOn=0`），这两个的默认 0 会立刻变成花屏 |

#### (c) 最坏情况：全新 program、除采样器与几何外全部保持 GL 默认 0

```
[virgin] 全新 program，仅设采样器+几何，其余全部默认 0：
         lum=0.000 lum_std=0.000 a=0.000 黑=100.0% 白=0.0%
```

**面板彻底不可见、且没有任何报错。** 这正是这类 bug 危险的地方：uniform 上传路径整体失效（程序重链后缓存未失效、`glGetUniformLocation` 返回 -1、状态被外部代码覆盖……）时，症状是「玻璃没了」，而不是崩溃或异常颜色。

### 2.4 SDF / 圆角 / 曲线数值不变量（GPU 求值 shader 原文）

命令：`python sdf_math_check.py`。**不重写公式**——脚本从 `ytbl_panel.frag` 里原样剪出
`pickCorner / boxMetric / boxNormal / fuseFields / fuseWeight / lensCurve / unitOrZero` 拼进测试 shader，
再用「几何定义」和「独立实现」去判定，而不是用同一公式自证。

```
[helpers] 从 ytbl_panel.frag 原样剪出：boxMetric, boxNormal, fuseFields, fuseWeight, lensCurve, pickCorner, unitOrZero
半轴 = (150, 100)，圆角半径 r = 40；全部在 GPU fp32 上求值

T0 控制点自检（手算真值 vs shader 实现 vs 本脚本独立实现）
     (0,0)          期望 -100.0000 | boxMetric -100.0000 | 独立实现 -100.0000 | 偏差 0.00e+00 / 0.00e+00
     (0,100)        期望    0.0000 | boxMetric    0.0000 | 独立实现    0.0000 | 偏差 0.00e+00 / 0.00e+00
     (150,0)        期望    0.0000 | boxMetric    0.0000 | 独立实现    0.0000 | 偏差 0.00e+00 / 0.00e+00
     (0,140)        期望   40.0000 | boxMetric   40.0000 | 独立实现   40.0000 | 偏差 0.00e+00 / 0.00e+00
     (138.28,88.28) 期望    0.0000 | boxMetric    0.0000 | 独立实现    0.0000 | 偏差 4.20e-05 / 4.20e-05
     (110,60)       期望  -40.0000 | boxMetric  -40.0000 | 独立实现  -40.0000 | 偏差 0.00e+00 / 0.00e+00
     (120,70)       期望  -25.8579 | boxMetric  -25.8579 | 独立实现  -25.8579 | 偏差 3.62e-05 / 3.62e-05
     (300,0)        期望  150.0000 | boxMetric  150.0000 | 独立实现  150.0000 | 偏差 0.00e+00 / 0.00e+00
     => 最大偏差 4.196e-05 px（说明两个实现都可信）

T1 轮廓精确性：65536 个参数化轮廓点代入 boxMetric
     max|f| = 0.000e+00 px    （期望 0，判定阈值 1e-2）

T2 符号/数值一致性：256x256 网格（格距 2px），排除 |g|<=1 的边界带
     参与判定 64736 点，符号不一致 0 点
     与独立参数化 SDF 的最大偏差 = 0.000e+00 px

T3 法线：单位长度 max|‖n‖-1| = 1.192e-07
     方向严格错误（外点朝内/内点朝外）= 0 点
     中轴「并列点」（两条边等距，法线选哪条都合法）= 120 / 64584 点（0.19%）

T4 lensCurve：f(0)=0.000e+00  f(1)=1.000000  单调不减: True
T5 fuse：max|w(a,b)+w(b,a)-1| = 1.192e-07；max|fuseFields(a,a,k)-(a-k/4)| = 0.000e+00；
         max|fuseFields(a,b,k)-min(fuseFields,a)| = 0.000e+00
T6 棱镜 7 抽头权重和（应恒为 1.0，否则面板整体偏色/变暗）
     R = 2*ΣEvenRed            = 1.000000
     G = CenterGreen + 2*ΣEvenGreen = 1.000000
     B = 2*ΣEvenBlue           = 1.000000
     奇数项绝对值（对称采样下应互相抵消，仅参与方向性偏移）= ['0.357143', '0.071429', '0.500000']
     alpha: 7 个抽头各 1/7 -> 1.000000
```

要点：

* **T1/T2 证明 `boxMetric` 的零等值面就是「半轴 (150,100)、r=40 的圆角矩形」**，且与独立参数化实现零偏差（65536 个轮廓点全部 max|f| = 0）。
* **T3 的 120 个「并列点」不是缺陷**：它们正好落在面板内部 `q.x≈q.y` 的中轴对角线上，那里两条边**等距**，`boxNormal` 用 `step(q.y,q.x)` 选 x 轴，我的独立实现并列时选 y 轴，两者都指向最近边。严格方向错误为 0。若将来要做像素级黄金标准，这一点需要约定一致的 tie-break。
* **T6 是本次最有复用价值的结论**：棱镜 7 个抽头对 R/G/B 三个通道的权重和**恒等于 1**（R 完全来自 6 个 pair 抽头、G 来自 mid+pair、B 来自 6 个 pair）。因此当 8 个 `uPrism*` 全为 0 时，`sum.rgb` 直接变 0（`mid.rgb` 只经 `uPrismCenterGreen` 进 G 通道），面板会塌成常数色——**与 2.3 的实测（lum 0.651→0.224，Δmean 81.8）完全吻合**。这条不变量可以直接做成 CI 断言：任何手改 `PRISM_*` 常量的行为都会被它抓住。

> 诚实记录：本脚本第一版有 4 个**测试侧** bug（轮廓采样索引越界 1024/65536、独立 SDF 内部距离漏加 +r、角区最近点错用角心方向、Python 网格与 `gl_FragCoord` 像素中心差 0.5），导致 T1/T2/T3 全部报「失败」。加上 T0 手算控制点自检并逐个修正后，才是上面这份结果。**验证器本身也需要被验证**——这是本轮最值得写进流程的一条经验。

### 2.5 dlsym 符号存在性审计（用设备原库，不用设备）

命令：`python symbol_audit.py`（内部调用 NDK `llvm-nm --dynamic`）

```
[库] build\recon\libgui.so            动态符号 2916 个
[库] build\recon\libinputflinger.so   动态符号 2337 个
[库] build\device-probe\libbinder_ndk.so 动态符号 199 个
探针里出现的 mangled 符号共 27 个
== 在本机保存的设备库里找到 ==  （18 个，含）
  OK   _ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRK...
  OK   _ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControl...
  OK   _ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17Physi...
  OK   _ZN7android14SurfaceControl10getSurfaceEv
  OK   _ZN7android3gui13LayerMetadataC2Ev
  OK   _ZN7android21SurfaceComposerClient11Transaction5applyEbb
  ...（共 18 个）
== 本机没有对应库 / 找不到（需要真机或补库才能确认）== （9 个）
  ??   _ZN7android7String8C2EPKc            (libutils.so —— 本机未保存)
  ??   _ZN7android7RefBase9incStrongEPKv     (libutils.so)
  ??   _ZN7android14ServiceManager10getServiceERKNS_8String16E (libbinder.so)
  ??   _ZN7android7IBinder8transactEjRKNS_6ParcelEPS1_j        (libbinder.so)
  ...
小计：找到 18，未确认 9
```

结论：探针依赖的 `libgui.so` / `libinputflinger.so` 符号**全部对得上**；剩下 9 个都在 `libutils.so` / `libbinder.so`，**本机没保存这两份库**，所以无法离线确认。→ 建议下次设备在线时第一件事就是把 `/system/lib64/libutils.so`、`libbinder.so` 拉进 `build/recon/`，这条离线检查的覆盖率立刻从 67% 升到 100%。

### 2.6 既有宿主测试套件回归

```
$ cd D:\纯c图形基座\build\host-tests; 逐个执行 *.exe
===== BackdropWiringCheck.exe =====   backdrop wiring checks passed            [exit=0]
===== FrameStatsTest.exe =====                                                  [exit=0]
===== GestureRouterTest.exe =====                                               [exit=0]
===== InputRouterTest.exe =====                                                 [exit=0]
===== MotionTest.exe =====                                                      [exit=0]
===== RenderBackendTest.exe =====                                               [exit=0]
===== RootBootstrapCliTest.exe =====  RootBootstrapCliTest passed               [exit=0]
===== RootDesktopCaptureHost.exe =====  未带参数运行 → 打印用法后退出            [exit=2]
```

7 个测试全绿（`RootDesktopCaptureHost.exe` 是 CLI，不带参数时按约定返回 2=usage error，不是失败）。

### 2.7 M3.5 连续曲率圆角的既有离线证据

```
$ corner_real_geometry.exe      （面板 620x900, 半径 46.0）
半径夹取: 请求 46.0 -> 实际 46.0 (上限 min(W,H)/2 = 310.0)
原始 tW = 5.7391, tH = 8.7826 -> 夹取后 tW = tH = 1.0000（半径相对面板很小，走完整形态）
右上角曲线包围盒: x ∈ [543.33, 620.00]  y ∈ [0.00, 76.67]
角沿顶边向左延伸 = 76.67 px = 1.6667*r（=5/3 r 的实测确认）
角纵向需要 76.67 px，面板半高 450.00 px -> 放得下

$ kyant_corner_check.exe        （Kyant0 算法 C++ 移植, r=100）
OK  x 控制点单调递增    OK 起点切线水平    OK 末点 x=1.0（与直边相切，曲率连续）
OK 末端三点 x 均为 1.0  OK y 分量单调递增至 1.6667（=5/3）
```

也就是说：**M3.5 的几何侧已经离线验证通过**；真正没验证的是「把 squircle 塞进 `boxMetric` 之后的着色器行为」——见 3.3 的方案。

---

## 第三部分 · 结论与剩余风险

### 3.1 可以确定的事（离线结论）

1. **uniform 覆盖没有缺口。** panel 71 个 / gather 10 个，生产路径与探针路径都是 100% 赋值；没有拼写漂移、没有「声明了但没人设」的槽位。之前踩的 uPrism\* 与 uSurfaceMode 两个坑，**成因都不是漏赋值**。
2. **uPrism\* 的失败形态被纠正了认知**：全 0 不是「色散静默失效」，而是**整块面板塌成常数色**（RGB 权重和 0、alpha 仍为 1），实测 lum 0.651 → 0.224、99.72% 像素变化超过 16/255。根因是权重设计：`mid.rgb` 只有 G 通道经 `uPrismCenterGreen` 参与，R/B 完全依赖那 6 个 pair 抽头。
3. **「整块变白」的精确触发条件是** `uSurfaceMode>0.5`（1=Hue / 2=Overlay）**且** `uSurfaceTint` 为白色且 alpha 高：mode=1 得到 93.6% 白像素，mode=2 得到 99.7%；两者任一单独存在（mode 改了但 tint 全 0）输出与基线**逐像素完全一致**。
4. **三条会「静默抹平内容」的默认值**（都值得做成启动自检断言）：
   * `uToneCtrl=(0,0,0)` → `toneMap` 把颜色压成常数灰（实测 Δmean 38.2、99.31% 像素受影响）；
   * `uPanelOpacity=0` → 面板全透明；`uSceneOn=0` → 面板全黑；
   * 8 个 `uPrism*` 全 0 → 面板塌成常数色。
   三者共同特征：**画面还在、没有报错、只是「玻璃没了」**。
5. **「默认 0 会走错分支」的典型是 `uDropRect` 这类哨兵值 uniform**：`uDropRect` 用 **-1 表示「无 drop 区」**（`if (uDropRect.z >= 0.0 && uDropRect.w >= 0.0)`），默认 0 恰好满足判定 → 走进 drop 分支并把 `bodyHalf` 设成 0 → **面板整体消失**（实测 Δmean 188.1、99.72% 像素变化）。同类还有 `uFeedRect`/`uStrandRect`（另有 `uFeedMerge/uStrandMerge` 双重门控，置 0 无害）。
   **但要说清楚：项目里这个坑被 C++ 结构体默认值挡住了** —— `GlassInternal.h` 里 `dropRect/feedRect/strandRect = {0,0,-1,-1}`、`panelOpacity = 1.0f`、`toneCtrl = {0,1,1}`、`surfaceMode = 0`、`trackOn/trackRect` 在 `GlassPanel.cpp:174-183` 成对赋值，方向都是安全的；`sceneOn` 默认 0（=面板黑，实测 Δmean 186.8）也由 `GlassPanel.cpp:388`「无 scene 纹理时显式置 0」的守卫兜住。
   真正需要警惕的是**上传路径整体失效**（程序重链后脏值缓存未失效 / `location` 返回 -1 / 状态被外部覆盖）：此时症状是「玻璃静默消失」（virgin 用例 lum=0.000、alpha=0.000、无任何报错），而不是崩溃。
6. **SDF/圆角/曲线数学全部正确**：零等值面精确（65536 点 max|f|=0）、与独立实现零偏差、法线单位长度（1.19e-7）且严格外指、`lensCurve` 端点精确单调、`fuse*` 不变量成立、棱镜权重每通道和为 1。
7. **`gather` 与 `panel` 的双路径依赖关系**：`uSampleSpan`/`uSampleTexel` 在 `uGatheredOn=1` 时被分支遮蔽（置 0 无影响），一旦 gather 未就绪回落到直采路径（`uGatheredOn=0`，Δmean 47.4），这两个 uniform 的默认 0 立刻会让画面错乱。**当前两边都赋值了所以安全，但这是一个「零冗余」的耦合点。**
8. 探针依赖的 libgui/libinputflinger 符号 18/18 全部存在；7 个宿主测试全绿；M3.5 的圆角几何离线证据齐备。

### 3.2 必须等设备回来才能验证的（离线做不到）

| 项 | 为什么离线做不到 |
|---|---|
| **Adreno 驱动侧的真实编译/精度/性能** | 本机是 AMD 桌面 GL 3.3 与 fp32；GLES 3.0 在 Adreno 上的精度、`fwidth`、`inversesqrt` 实现与优化都可能不同。本轮结果是**逻辑/分支/量级**级别的回归，不是设备像素黄金标准。 |
| **EGL/SurfaceFlinger 上屏路径** | `createSurface` → `getSurface` → `eglCreateWindowSurface` → 图层合成，必须真机；本机只能到「符号存在」这一步。 |
| **真实桌面反射链路** | 与 screencap 帧、display id、屏幕旋转、面板实际位置耦合，离线只能用合成场景代替。 |
| **M4：`inhibited` + uinput 回灌** | 内核输入子系统行为与权限，纯设备侧。 |
| **系统输入法（InputConnection/焦点/上屏）** | 需要真实 IMMS 与 IME 进程。 |
| **触摸/手势在真机上的命中率** | 本机只能跑 `GestureRouter`/`InputRouter` 的纯逻辑单测（已绿），物理触控与输入分发必须真机。 |
| **整机帧率/功耗/温升** | 本机 GPU 与手机完全不同量级。 |

### 3.3 建议的下一步（离线可做，按顺序）

1. **把本轮 AI 助手 接进 M3.5**：新建 `ytbl_panel_squircle.frag`（把 `boxMetric` 的角部从圆弧换成 Kyant0 曲线对应的 SDF），用 `gl_shader_regress.py` 做**基线/新 shader 的逐像素对比**，重点看：
   * 四角区域 Δ 图（`uCornerRadii=0` 用例已证明该区域只占 0.5% 像素 → 信噪比足够）；
   * 新增一条**曲率剖面检查**：沿角部按弧长采样，用 `boxNormal` 的差分算曲率，验证「圆弧=常数曲率」vs「squircle=从 0 平滑升到 1/r」，这是 M3.5 的验收指标，且完全能离线测；
   * 保证 `boxMetric` 在角部与直边的 **C1 接缝**没有台阶（T1 的 65536 点轮廓检查直接复用）。
2. **加一组 host 侧「uniform 自检断言」**（把 3.1 第 4/5 条变成代码）：`uPanelOpacity/uSceneOn/uToneCtrl.xy/uPrism* 权重和/uDropRect 哨兵值` 在上传前校验，失败直接打日志——避免真机上再出现「玻璃静默消失」。
3. **补齐离线库**：设备在线时拉 `/system/lib64/{libutils,libbinder}.so`，符号审计覆盖率 67%→100%。
4. **补两处低成本的静态核对**：`screencap` 输出格式（对照 `research/aosp/.../screencap.cpp`）与 `buildTapMass` 的 12 个 tap 权重归一化。
5. 真机复跑顺序建议：**(a)** 先只跑 `probe_m3_ytbl`，把 `uSurfaceMode/uSurfaceTint/uToneCtrl/uPrism*` 四个值 dump 出来与本轮基线对齐 → **(b)** 再上 M3.5 新 shader → **(c)** 最后动 M4 输入。

### 3.4 复现命令

```powershell
cd D:\纯c图形基座\build\recon\offline
$env:PYTHONIOENCODING='utf-8'
python audit_uniforms.py                 # uniform 覆盖审计 → uniform_audit_report.md
python gl_shader_regress.py --sweep      # 基线 + 21 个分支用例 + 67 个敏感度用例 + virgin
python sdf_math_check.py                 # SDF/圆角/曲线不变量
python symbol_audit.py                   # dlsym 符号 vs 设备原库
cd ..\..\host-tests; Get-ChildItem *.exe | ForEach-Object { & $_.FullName }
```

产物：`gl_regress_result.json`、`sdf_math_result.json`、`regress_baseline.png`、`case_*.png`（23 张）、`run_*.log`。

> 复现说明：`python -m pip install moderngl` 需要网络（本轮可用）；若离线环境没有该包，可退化为「把 GLSL 数学函数剪出来编译成 C++ 跑」——本轮的 `sdf_math_check.py` 已经证明「剪函数 + 独立实现对照」这条路是可行的，只是会失去真实光栅化那一层（`fwidth`、纹理采样、混合）。
