# 液态玻璃背景采样的 Z-order 合成修复

> 起因（用户原话）：
> - 场景一：「二级窗口直接穿透 ELF 主窗口，采样到了 Android 真实桌面，仿佛中间的
>   ELF 主窗口不存在。」
> - 场景二：「圆钮会忽略绿色轨道，直接映射 Android 真实桌面的背景。类似现象也出现
>   在进度条、导航栏、底层图标等组件上：当玻璃覆盖这些内容时，它们可能直接消失。」
>
> 用户明确要求：不要给玻璃加绿色染色，也不要为开关/进度条/导航栏分别写补丁 ——
> 要解决的是整个 UI 渲染系统的基础能力。

---

## 1. 结论先行

**根因只有一条**：玻璃的背景纹理是**整屏画面**（采集到的"屏幕最终长什么样"），
而不是**"这块玻璃下方已经合成好的内容"**。

因为玻璃自己在屏幕上位于最上层，**它在整屏画面里盖住了它本该看到的一切**
—— 同一 Surface 里先画的主窗口、同一窗口里先画的绿色轨道、导航栏、图标，
在整屏画面里统统被玻璃自己遮掉了。玻璃只能看到"自己 + 桌面"。

**修复**：进程内建立**图层栈**并逐面板合成 Backdrop：

```
scene(玻璃的背景) = 本进程已绘制的下层内容  over  世界底层(我们图层之下的屏幕内容)
                                                       ↑ 合成结果 alpha 恒为 1
```

- **下层内容**由 ytbl 自己从当前帧缓冲抠区域得到 —— 那**天生**是 Z 序正确的：
  抠图发生在该面板绘制之前，所以帧缓冲里只有先于它绘制的东西。
- **世界底层**是独立的一张纹理（系统采集），它只是图层栈最下面那一层，
  不再是"全部背景"。

---

## 2. 修复前的链路（逐层，带行号）

### 2.1 背景从哪里来

| 环节 | 位置 | 行为 |
| --- | --- | --- |
| 采集 | `RealtimeCapture` / `realtime_capture/java/.../CaptureHelper.java:75` | `IWindowManager.captureDisplay(displayId, CaptureArgs, listener)` — 拿整屏 |
| 上传 | `GlesBackend::updateProjectionHardwareBuffer`（`GlesBackend.cpp:611`） | AHardwareBuffer → EGLImage → 一次 GPU 翻转 → `projectionTexture_` |
| 决策 | `PlanBackdrop`（`RenderBackend.h:174`） | `projectionUsable` ⇒ `BackdropOrigin::Projection` |
| 绑定 | `LiquidGlassRuntime::bindBackdropPlan`（`LiquidGlassRuntime.cpp:79`） | `lgx::bindBackdropTexture(整屏纹理, 屏宽, 屏高)` |
| 采样 | `GlassPanel.cpp:326 captureScene` | **`backdropReady()` 为真 ⇒ 每块面板都设成 `sampleOrigin=(0,0)`、`sampleSize=整屏`** |
| 取 UV | `Shaders.cpp:213 sceneUv` | `uv = 屏幕像素 / 整屏尺寸` |

**⇒ 每一块玻璃都在同一张"整屏画面"上按自己的屏幕坐标取 UV。**

### 2.2 每层怎么合成

`ImGui` 与 ytbl 面板用的是同一组混合因子（`imgui_impl_opengl3.cpp:414`、
`ytbl/Context.cpp:892`）：

```c
glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
```

即 `rgb` 是**预定乘**的、`a` 是覆盖率。这一条很重要：它意味着帧缓冲里天然存的就是
"本进程已绘制内容的预乘合成"，可以**直接当作玻璃的下层**用。

### 2.3 玻璃怎么取得采样纹理

ytbl 有两条路，而旧实现**只走了错的那一条**：

| 路 | 触发条件 | 采样源 | Z 序 |
| --- | --- | --- | --- |
| A | `bindBackdropTexture()` 绑了全局纹理 | **整屏画面** | ✗ 玻璃自己盖住了下层 |
| B | 没绑 ⇒ `glCaptureRegion`（`Context.cpp:664`）`glCopyTexSubImage2D` | **当前帧缓冲的区域** | ✓ 天然正确（只有先画的东西） |

**旧实现在透明宿主上永远走 A**（因为要拿到"我们图层之下的桌面"只能靠采集），
于是 B 那条本来正确的路被整条绕开了。

---

## 3. 两条根因证据链

### 3.1 用户报的两个场景都能复现（离线断言）

`tests/SceneCompositeTest.cpp` 把图层栈写成可断言模型，A-1/A-2 直接复现：

```
旧行为(排除自身图层)  → 采到的是桌面，主窗口不可见
旧行为(包含自身)      → 采到的是玻璃自己 0.6 + 主窗口 0.4（被自己污染）
旧行为(不透明玻璃)    → 采到的 100% 是玻璃自己
新行为                → 采到下方主窗口的红色 / 下方绿色轨道
```

### 3.2 真机 A/B（同一台设备、同一屏幕、同一次会话）

步骤：两侧都启动覆盖层 → 把「主题」窗口拖到与「二级液态玻璃」窗口重叠 →
点一下二级窗口把它提到最前 → 截图取同一批像素。

| 采样点（物理像素） | 旧二进制 | 新二进制 |
| --- | --- | --- |
| (300, 600) | **(222,130,179) 壁纸粉** | **(44,39,45) 主题窗口深色 UI** |
| (450, 550) | **(232,177,242) 壁纸浅粉** | **(77,119,177) 主题窗口蓝色控件** |
| (250, 560) | (33,2,18) 壁纸暗红 | (20,60,111) 主题窗口蓝 |
| (300, 900) 仅主题窗口 | (31,47,73) | (32,46,73) —— 未受影响 ✓ |
| (900, 500) 仅壁纸 | (1,17,50) | (1,17,50) —— 未受影响 ✓ |

按行统计：玻璃重叠区（y 500–700）**65% 的像素变化 >30**，平均差 150+；
玻璃之外的区域逐行差异为 0。

原始截图与差异图：`research/zorder_acceptance/`
（`before_overlap.jpg` / `after_overlap.jpg` / `diff.png` / `base_dump.png`）。

---

## 4. 世界底层到底是什么（决定性实验）

整个修复的正确性都建立在"世界底层里**没有**我们自己"之上。如果它其实包含我们
上一帧的输出，那么凡是本帧缓冲透明的像素，玻璃就会以**增益 1** 采到自己 —— 无限镜像。
这一点**无法从截图判断**（玻璃本来就长得像桌面），必须把采集纹理直接读出来。

新增一次性诊断 `AURORA_DUMP_BASE=<path>`（`GlesBackend::maybeDumpWorldBase`）：

```bash
AURORA_DUMP_BASE=/data/local/tmp/base.ppm AURORA_DUMP_BASE_FRAME=240 \
  ./aurora_overlay --capture-fps 60 --render-fps 60
```

结果（`research/zorder_acceptance/base_dump.png`）：**整张图里只有壁纸，
没有我们的两个窗口、没有文字、没有任何 UI。**

**为什么**（子代理查证，见 `research/LAYER_EXCLUSION_CAPTURE_FINDINGS.md`）：
`aosp_glue.h:446` 建图层时 `parentHandle = nullptr` ⇒ SF 的
`LayerHierarchy::attachToParent`（`LayerHierarchy.cpp:228`）把它挂到**全局 `mRoot`**，
而 WMS 的 Display Root 也是无父创建 ⇒ **两者是兄弟**。WMS `captureDisplay` 走的是
`captureLayers(displayRoot, childrenOnly=true)` → `getPartialHierarchy(displayRoot)`
= **只抓子树** ⇒ 当前采集路径**本来就看不到我们的图层**。

**⇒ 三个重要后果：**

1. **`eSkipScreenshot` 可以继续关闭** —— 系统截屏/录屏照样能拍到悬浮窗
   （用户上一轮明确要求的行为得以保留）。
2. **不需要做 `setExcludeLayers` 改造**（省下 ~220–250 行 binder 桥 + SELinux 风险）。
   该能力已核实可用（AOSP 16 `ScreenCapture.CaptureArgs.Builder.setExcludeLayers`
   在 WMS 路径上被原样透传到 SF 并从快照层级整棵剔除），**留给将来换采集路径时用**。
3. 早期一个悬案（"关掉 eSkipScreenshot 也没自反馈失控"）有了统一解释：
   不是防得好，是**采集路径压根看不到我们**。

---

## 5. 新架构

```
帧开始
 ├─ 清屏（透明）
 ├─ 世界底层就绪：projectionTexture_（= 我们图层之下的屏幕内容）交给合成器
 └─ 执行 ImGui 绘制命令（顺序 = Z 序）
      ├─ 普通内容（文字/图标/轨道/背景/第一个 ELF 窗口）→ 直接画进帧缓冲
      └─ 玻璃面板 paint 之前：
           ① ytbl glCaptureRegion：帧缓冲区域 → captureTexture   （只有下层内容）
           ② ★ 新增钩子：合成器把世界底层垫到它下面
                rgb   = world.rgb * (1 - dst.a) + dst.rgb
                alpha = 1
           ③ ytbl 照旧做模糊 / 折射 / 色散 / 色调映射 / 绘制
```

### 关键设计点

| # | 设计 | 理由 |
| --- | --- | --- |
| 1 | **不绑全局背景纹理**（`PlanBackdrop` 新增 `BackdropOrigin::SceneComposite`，`texture = 0`） | 绑了就会退回 `captureScene` 的 `backdropReady()` 分支，把整屏图当成每块玻璃的背景 —— 那就是根因 |
| 2 | **采样发生在该面板绘制之前** | 结构上杜绝"采到自己"；更上层的玻璃还没画，也不可能被采到 |
| 3 | **下层内容来自帧缓冲** | 同窗口下层组件、更早绘制的 ELF 窗口全都在里面 ⇒ 跨窗口与窗口内组件用同一套机制解决 |
| 4 | **合成结果 alpha 钉 1** | ytbl 的 `sceneSample` 用 `c.a` 反预乘并判定"场景缺失"。世界是不透明的，留 `a<1` 会让玻璃把自己下方的世界当成半透明场景，混入 `uAbsentTint` 兜底色 |
| 5 | **没有世界底层时返回 false** | ytbl 保持原生行为（只采样帧缓冲），比采样一张过期/错误的全屏图诚实 |
| 6 | **GL 状态全保存/恢复** | 钩子跑在 ImGui 绘制回调内部，不能污染宿主后端的实时状态 |

### 合成数学

```
scene.rgb = ui_premul + world.rgb * (1 - ui.a)
scene.a   = 1
```

对应 GL 混合因子（一次四边形即可）：

```c
glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
```

- rgb：`world * (1 - dst.a) + dst` ✓
- alpha：`1 * 1 + dst.a * (1 - 1) = 1` ✓

---

## 6. 改动清单与成本

| 文件 | 改动 | 规模 |
| --- | --- | --- |
| `ytbl/GlassUI.h` / `GlassContext.h` / `Context.cpp` | ★ 新增宿主场景合成钩子（**默认未注册 ⇒ 上游行为逐位不变**） | +55 行 |
| `ytbl/Runtime.cpp` | `lgx::setSceneComposer` 公开入口 | +6 行 |
| `app/src/main/cpp/SceneComposite.h` | 图层栈模型 + 合成数学 + 纹理坐标（**纯逻辑，可离线单测**） | 新增 ~230 行 |
| `app/src/main/cpp/GlesSceneCompositor.{h,cpp}` | GLES3 合成器（1 个 program / 1 个 FBO / 1 个 VAO，惰性创建） | 新增 ~380 行 |
| `app/src/main/cpp/RenderBackend.h` | `BackdropOrigin::SceneComposite` + 编译期自检 | +45 行 |
| `app/src/main/cpp/GlesBackend.{h,cpp}` | 每帧喂世界底层；descriptor 上报；一次性 dump 诊断 | +90 行 |
| `tests/SceneCompositeTest.cpp` | 50 项断言（含用户报的两个场景、多玻璃重叠、拆 Surface 的证伪） | 新增 ~430 行 |
| 构建 | 两个 CMakeLists 各加一行 | +2 行 |

**没有修改任何玻璃配方 / shader / 参数**：模糊、折射、色散、棱镜、色调映射、
边缘光、液滴形变全部复用原管线。

### 性能

每块玻璃每帧：**1 次 FBO 绑定 + 1 个覆盖采样区域的四边形**（面积 = 面板 + capture
padding），与面板自身绘制同量级；没有额外纹理分配（复用 ytbl 自己的 captureTexture）。

真机实测（虚拟显示 1080×2376，采集 60fps）：加合成器后 **FPS 60.1–70.1**，
未见掉帧；采集侧耗时不变（6–9 ms/帧，仍是采集主导，与修复前一致）。

---

## 7. 验收

| 场景 | 方法 | 结果 |
| --- | --- | --- |
| ① 跨窗口 | 主题窗口拖到二级窗口上，二级窗口提到最前 | 玻璃由"壁纸"变为**主题窗口的模糊内容**；重叠区 65% 像素变化 >30 |
| ② 多图层组件 | 二级窗口玻璃之下是主窗口的滑块；二级窗口自己的文字画在玻璃之上 | 玻璃采到下方滑块（模糊蓝色块），文字仍在玻璃之上 ✓ |
| ③ 多个玻璃重叠 | 二级窗口整体玻璃 + 其内部 PillButton 玻璃 | 按钮玻璃内部呈现下方面板玻璃的内容，两者可区分 ✓ |
| ④ 无自采样/反馈 | 世界底层 dump + 模型断言 | dump 里**只有壁纸**，无我们的图层；模型断言玻璃的下层集合恒为 `0..glass-1` |
| ⑤ 未破坏既有行为 | 触摸拦截 / 拖动 / 动画 / 透明 / 模糊 | 拖动、滑动、按钮按压全部正常；`tests/run_host_tests.ps1` **14 项全绿** |

离线回归：`pwsh -File tests\run_host_tests.ps1` → **14 项通过，0 失败**
（新增 `SceneCompositeTest` 50 项断言、纳入此前未接线的 `RenderBackendTest`）。

---

## 8. 已知限制 / 未做

1. **世界底层是"我们图层之下"的一帧画面，不是实时透传。** 玻璃里的桌面有约一帧
   采集延迟（60fps ⇒ ~16ms）。玻璃之外的部分仍是真正的实时透传（我们图层透明）。
2. **受保护内容**（DRM / `FLAG_SECURE`）在采集里是黑的，玻璃里也会是黑的 ——
   这是采集路线的固有限制，与本次改动无关。
3. **`setExcludeLayers` 未接入**。当前不需要（见 §4），但已核实可用；
   一旦采集路径改成按 layerStack 遍历全部快照（例如改用 `screencap` 那套
   `captureDisplayById`），就**必须**接入，否则世界底层会混进我们自己。
   注意：`captureDisplayById` 在 SF 里**不读** `excludeHandles`
   （`SurfaceFlinger.cpp:7500-7502` 传 `nullptr`），token 版才读。
4. **世界底层只有"一层"**。它无法表达"我们图层之下还有其他会动的层"的时序差异；
   60fps 采集下未观察到问题，但 30fps 采集时快速滚动的桌面可能出现迟滞。
5. 本轮验收在 **测试虚拟显示**完成（纪律：物理屏实验需事先征得同意）。
   物理屏复测待用户授权。
