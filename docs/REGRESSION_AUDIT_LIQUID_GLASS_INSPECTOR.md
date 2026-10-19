# 液态玻璃实时调试器 · 回归审查与修复报告

> 日期：2026-10-14　范围：上一轮「Liquid Glass Inspector（液态玻璃实时调试器）」的
> **回归审查 + 未授权改动恢复 + 参数链路打通**
>
> 本仓库**没有 git**（`git log` 报 not a git repository），所以"与上一轮之前的版本对比"
> 是用**四类硬证据**重建的（见 §一之证据来源）。所有结论都能指到具体文件与行号。

---

## 〇、一句话结论

上一轮**改动范围确实越界**：它在实现调试器的同时，
**改坏了按钮行的换行结构**（导致两枚按钮被裁到窗口外）、
**挪动了「点击 N」的位置**、**翻转了弹窗默认动画路径**、
**给按钮行塞了第 5 枚按钮**、**把实验场默认圆角放大了一倍**、
**让实验场 7 个参数变成拖了没反应的死控件**。
而**参数到渲染的主链路本身是通的** —— 我用极端参数实测证明二级窗口画面
**57.4% 的像素会变化**、恢复原值后**逐像素完全一致（差异 0.000）**。
用户感觉"改了没反应"，直接原因是上面那些断链叠在一起（尤其是实验场死控件、
滑块命中区与滚动条打架），不是"只做了 UI 没做绑定"。

---

## 一、上一轮未经授权修改的原有行为

### 证据来源（本仓库无 git，这四条是替代方案）

| 来源 | 内容 | 用途 |
|---|---|---|
| **上游原版源码** `C:\Users\<you>\Downloads\c++液态玻璃展示源码\液态玻璃\app\src\main\jni\` | `secondary_imgui.cpp`(40667B) + `main.cpp` + `ytbl/` | 布局/文案/动画的**逐行原文**基线 |
| **调试器开发前的二进制** `build/recon/aurora_overlay`（sha256 `62377f44…`，2026-10-13 22:28） | 带符号表（`-g`，未 strip） | 读**默认值**与**组件数量**（`.data`/`.bss` 判定） |
| **组件清单文档** `docs/LIQUID_GLASS_DEMO_UI_INVENTORY.md`（10-13，60KB） | 逐组件文案/尺寸/行号 | 布局与文案的第三方记录 |
| **参数快照** `research/regression_audit/backup_before_restore/params_{before_preview,now}.json` | 10-14 16:54 / 18:34 的运行期 dump | 默认值与用户值的对照 |

> ★ 我把调试器开发前的**全部**可得二进制都查了一遍
> （`before_realtime` 10-13 14:54 / `before_high_rate` 15:39 / `before_power` 20:55 /
> `before_rotation` 21:48 / `build/recon/aurora_overlay` 22:28），结论一致。

### 1.1 「点击 N」被从按钮右侧挪到下一行左侧 —— 【已恢复】

* **上游原文**（`jni/secondary_imgui.cpp:338-340`）：
  ```cpp
  if (ImGui::GetContentRegionAvail().x >= lgx::px(180.0f) + gap) ImGui::SameLine();
  ImGui::Text("%s %d", Text("点击", "Clicks"), s_Button.clicks);
  ImGui::Spacing();      // ← 这一行结束第一行
  ```
* **被改坏的样子**：`ImGui::Spacing();` 被挪到 `Text(...)` **之前** ⇒ 强制换行。
* **第三方记录**：`docs/LIQUID_GLASS_DEMO_UI_INVENTORY.md:263`
  `| 3 | 文本 | 点击 %d（累计点击数） | **SameLine** | 338-339 |`；
  `:519-520` 的散文重建同样写"玻璃胶囊按钮**与**「点击 N」；**接着一行**「着色」"。
* **恢复**：`secondary_imgui.cpp:872-899`（见 §二）。

### 1.2 弹窗默认动画路径被翻转 —— 【已恢复】

* **原实现**：`static bool s_DialogAnimationFromButton;`
  ⇒ 用户上一轮之前看到的默认是 **`false` = `Modal::open()`（从中心玻璃球长出）**。
* **硬证据**：调试器开发前的**每一个**二进制里，符号
  `secondary_imgui::(anonymous namespace)::s_DialogAnimationFromButton` 都落在
  **`.bss`**（`readelf -sW`：`OBJECT LOCAL DEFAULT 24`，节 24 = NOBITS ⇒ 零初始化 ⇒ false）；
  而同一二进制里有初值的 `s_DialogGlassBlurDp` / `s_DialogButtonWidthDp` / `s_DialogButtonHeightDp`
  都在 **`.data`**（节 23）。
* **被改成**：`glass/GlassScene.h` 的 `bool dialogAnimationFromButton = true;`
  ⇒ 默认走 `openFrom()`（从按钮位置展开，位移与缩放幅度都大得多）
  ⇒ 用户原话"**弹跳幅度被明显放大，出现过度夸张的回弹效果**"。
* **★ 一处必须说清的冲突**：**上游 Demo 源码** `jni/secondary_imgui.cpp:38` 写的是
  `static bool s_DialogAnimationFromButton = true;` —— **与移植版历史相反**。
  也就是说：移植时这一行就被搬成了无初值版本（false），**不是调试器那一轮干的**；
  调试器那一轮把它显式写成 `true`（= 上游字面值）。
  用户的要求是"**恢复上一轮调试器开发之前的动画效果**" ⇒ 取**移植版历史值 `false`**
  （已在 `GlassScene.h` 注释里把两层证据都写明，避免以后有人拿上游源码复核时又改回去）。
* **弹窗的数值参数一个都没被改过**：`ytbl/GlassConfig.h`、`Dialog.cpp`、`PopupMenu.cpp`
  与上游源码 **SHA-256 逐字节相同**（我复核过）。回弹幅度本身来自
  `DIALOG_POP_DAMPING_OPEN=0.66`（`GlassConfig.h:265`），这个值是**原有**的。
* **恢复**：`glass/GlassScene.h` 的 `dialogAnimationFromButton = false`。

### 1.3 按钮行被塞进第 5 枚按钮 —— 【已恢复】

* **上游**：按钮行只有 4 枚（`jni/secondary_imgui.cpp:331-364`：
  `s_Button` / `s_ButtonTint` / `s_ButtonSurface` / `s_ButtonSmall`），
  "打开液态弹窗"是**卡片区**的独立按钮（上游 `:532` `s_DialogButton`）。
* **硬证据**：调试器开发前的二进制里，二级窗口的按钮对象符号只有
  `s_Button` / `s_ButtonTint` / `s_ButtonSurface` / `s_ButtonSmall` + 卡片区的 `s_DialogButton`
  —— **没有第五个**；`docs/LIQUID_GLASS_DEMO_UI_INVENTORY.md:288` 第 28 项也把
  「打开液态弹窗」列在卡片区（180×48dp）。
* **被改成**：按钮行的 `labels[4] = "打开液态弹窗"`（第 5 枚），
  并与卡片区按钮**功能完全重复**；它的存在还逼着代码写 `if (i != 4 && …) SameLine();`
  这种特例，也是"点击 N 掉到下一行"的连带原因之一。
* **恢复**：`kButtonCount` 5 → 4（`GlassScene.h`）、`buttonWidthDp[4]`/`buttonHeightDp[4]`、
  删除 `s_MotionDialog`、删除按钮行里那段重复的弹窗触发代码
  （`secondary_imgui.cpp:810-820`、`861-871`）、`GlassScene.cpp` 的 `ButtonId/ButtonName` 去掉 case 4。

### 1.4 ★ 按钮行的换行结构被破坏（两枚按钮被裁到窗口外）—— 【已恢复】

* **上游结构**：
  * 第 1 行 = `[液态玻璃按钮] [点击 N]`
  * 第 2 行 = `[着色] [表面色] [+] [计数]`（着色前**没有** SameLine —— 上一行的 `Spacing()` 已换行）
* **被改成**：循环里 `if (i > 0) { if (ImGui::GetContentRegionAvail().x >= needed + gap) ImGui::SameLine(); }`
  —— 而按钮是**自绘**的（不产生 ImGui item、不推进光标），
  `GetContentRegionAvail()` 在这里恒等于**整行宽度** ⇒ **判据恒真** ⇒
  四枚按钮 + 两段计数全挤到一行 ⇒ 超出的部分被窗口裁剪掉。
* **真机现象**（我本轮实测截图）：只剩「液态玻璃按钮 | 点击 0 | 着色」，
  **「表面色」和「+」整个看不见**。
* **★ 这个坑本项目自己记过一次**：`docs/LIQUID_GLASS_INSPECTOR.md:346`
  「`SameLineOrWrap` 改了等于没改｜Button 之后游标已被挪到**下一行行首**，
  `GetContentRegionAvail()` 给的是整行宽度而不是本行剩余」。
* **恢复**：判据改为"**上一枚按钮的真实右边界** + 间距 + 下一枚宽度 ≤ 内容区右边界"，
  并按上游结构让 i==1 另起一行、i==0 的计数后补 `Spacing()`
  （`secondary_imgui.cpp:822-830`、`872-899`）。

### 1.5 ★ 实验场默认圆角被放大一倍 —— 【已恢复】

* **上游**（`jni/secondary_imgui.cpp:652`）：
  `playgroundStyle.corners = lgx::Corners::rounded(objectSize * 0.5f * s_PlaygroundRadius);`
  ⇒ 默认 = 256 × **0.5** × 0.5 = **64dp**。
* **被改成**：`pg.cornerTlDp = … = stage.playgroundSizeDp * stage.playgroundRadius;`
  ⇒ 256 × 0.5 = **128dp**（半径已达短边一半 ⇒ 方块看着像圆）。
* **双重证据**：同一份运行期 dump `params_now.json` 的
  `secondary_window.playground` 里 `cornerXxDp = 128`，
  而**同一目标**的 `lensHeightDp = 25.6`（= 0.2×256×**0.5**）、`lensAmountDp = 51.2`（= 0.2×256）
  —— 另两处都带着 0.5，只有圆角漏了。
* **恢复**：补回 `* 0.5f`（`secondary_imgui.cpp:1207` 附近）。
  **实测确认**：修复后运行期保存值为 **`cornerXxDp = 64`** ✓

### 1.6 ★ 实验场 7 个参数是死控件（拖了没反应）—— 【已修复】

* 上游这些量**直接来自面板滑杆**，没有中间层。
* 当前实现里，`secondary_imgui.cpp` **每帧无条件**把
  `scene.playground` 的 `cornerTl/Tr/Br/BlDp`、`capsule`、`lensHeightDp`、`lensAmountDp`
  按 `stage.playground*` 重算，而工作台（`GlassTargets.cpp` 的 `BindPanelTune`）
  把**同一批字段**登记成了可编辑参数 ⇒ **当帧就被覆盖** ⇒ 假控件。
* **修复**：改成"**只在面板输入真的变化时才重算**"（变化检测），
  工作台写入的值在面板滑杆没动时得以保留。
* **★ 验证限制**：这条的"拖滑块看到变化"需要**真人手指拖动**才能验（见 §五）。

### 1.7 其余未经授权的细节改动 —— 【已恢复】

| # | 现象 | 上游依据 | 恢复位置 |
|---|---|---|---|
| A2 | 开关区三行之间各少一个 `ItemSpacing`（本机 24px） | `上:376`、`:383` 在开关 1/2 的 ON/OFF 后有 `Spacing()`，第三处没有 | `secondary_imgui.cpp:920-925`（`if (i < 2) ImGui::Spacing();`） |
| A3 | 第二枚形状芯片文案被改成读圆角值（中文界面恒显示 "r16"） | `上:487` `TextUnformatted(Text("圆角16","r16"))` | `secondary_imgui.cpp:1041-1048` |
| A7 | 主窗口提示"（它标题栏上的 × 也能关）"**失实**（二级窗口已 `NoTitleBar`，没有 ×） | `上主:185` 原文是"勾选后打开二级液态玻璃窗口" | `LiquidGlassRuntime.cpp:670-675` |
| A11 | 「色散(色差)」复选框的联动丢失（取消勾选后实验场仍有色散） | `上:557` 的 `s_Chromatic` 同时被卡片（`:577`）与实验场（`:658`）读取 | `secondary_imgui.cpp:1118-1125` |

### 1.8 本轮新发现并修掉的**调试器自身**的宿主接线 bug —— 【已修复】

这一条不是"上一轮改坏的"，而是**调试器自己的参数行实现**里的错（属于本次任务范围）：

* **现象**（真机截图）：参数名被裁成「鲜艳度增」「启用面板」。
* **根因**：`GlassWorkbench.cpp` 用 `ImGui::GetContentRegionAvail().x` 取"可用宽度"，
  而它 = 内容区宽度 − **当前光标 x**；调用点恰好停在上一行
  「重置本组 …（N 项，已改 M 项）」的右边 ⇒ 可用宽度被算成两三百像素的**零头**
  ⇒ 名称列被压到几十像素、被裁字。
* **修复**：改用 `ImGui::GetContentRegionMax().x`（内容区总宽度，与光标无关）；
  并新增**始终开启的宿主侧硬自检**：整行最右控件越出内容区就打印一条 WARN
  （这类"传进去的宽度不对"的错，纯逻辑回归结构上测不出来）。

### 1.9 明确**保留**的改动（属于用户要求或调试器必需，未回退）

| 项 | 为什么保留 |
|---|---|
| 二级窗口 `NoTitleBar/NoScrollbar/NoMove/NoResize` | 用户明确要求（"头上的标签直接删掉，让它变成整块液态玻璃"、"只靠滚动条翻页"） |
| 「输入法与多点触控」段、运行时信息段 | 宿主功能新增（IME 桥接、诊断），与上游 Demo 无关 |
| 22 页工作台取代 `DrawHostControls()` | 这就是调试器本身（第 1 类改动） |
| 组件实例化 + 参数注册（`glass::Scene()`） | 调试器必需（第 2 类改动）；**默认值逐条等于原常量**（见 §三.5 的对照） |
| 手势层 / 长按缩放 / 玻璃填充动画 | 上游没有对应实现，且都是用户点名要的功能 |
| 全局 ImGui 样式（WindowPadding 44 等） | 宿主自有主题；但它是"一行放不下 4 枚按钮"的第二原因，已由 §1.4 的正确判据解决 |

---

## 二、「点击 0」与按钮行是如何恢复的

```cpp
// secondary_imgui.cpp:822-830 —— 换行结构
const float contentRight = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
float lastButtonRight = -1.0f;
for (int i = 0; i < kButtonCount; ++i) {
  if (i >= 2) {                        // i==1（着色）天然另起一行
    const float needed = lgx::px(stage.buttonWidthDp[i]);
    if (lastButtonRight + gap + needed <= contentRight) ImGui::SameLine();
  }
  …画按钮…
  lastButtonRight = drawn.right;       // 自绘按钮：右边界自己记
  if (i == 0) {
    …snprintf(clicksText, "点击 %d", button.clicks)…
    if (lastButtonRight + gap + ImGui::CalcTextSize(clicksText).x <= contentRight) ImGui::SameLine();
    ImGui::TextUnformatted(clicksText);
    ImGui::Spacing();                  // ← 上游 :340：第一行到此结束
  } else if (i == 3) { …小按钮计数… }
}
```

* **不用绝对坐标**：全部走 ImGui 的 `SameLine()` 与真实 item 右边界 ✓
* **保留点击计数**：`button.draw()` 内部的 `++clicks` 未动 ✓
* **真机验证**：修复后截图第一行 = 「液态玻璃按钮 | 点击 0」，第二行出现「着色」「表面色」✓

---

## 三、参数链路审计

### 3.1 链路本身（结论：**主链路是通的**）

```
GlassTargets.cpp 注册（指针绑定）
   → glass::Scene() 字段                        [唯一实例，单例]
   → 每帧 BuildWindowStyle / BuildPanelStyle / GlassRecipes
   → PanelStyle
   → appendJob() 按值快照进 PanelJob            [不是引用，不存在"延迟读全局"]
   → Context.cpp 上传 uniform
   → Shaders.cpp 生效
```

* **不是"只在初始化读一次"**：全仓**没有** `static PanelStyle` / `static PanelTune` 缓存；
  每帧重建（`secondary_imgui.cpp` 的 `BuildWindowStyle(Scene().window)`、`BuildPanelStyle(pg)`、
  `PillButton.cpp:144`、`Toggle.cpp:166`、`Slider.cpp:193`、`TabBar.cpp:294/341`、`Dialog.cpp:325`）。
* **延迟 draw callback 的快照是对的**：`PanelJob` 是 POD 拷贝，画的是**提交那一帧**的样式。
* **组件实例是独立的**：`buttons[4] / toggles[3] / sliders[2] / chips[6]` 各一份 tune；
  `tests/GlassBindingTest.cpp` 有断言"按钮 1 改完按钮 3 不变"。
* **共享材质**（要明确区分）：窗口外壳**不继承**给子组件（`GlassTargets.cpp:1018` 的说明）；
  两根滑块的强调色/轨道色实际由 `stage.sliderAccent/Track` 共享（每帧写入）；
  三枚开关的轨道底色共享；芯片的高度/间距共享。

### 3.2 判为 **REAL**（已接入真实渲染，可实时生效）—— 逐条给"谁每帧读它"

| 参数 | 目标 | 每帧读取点 | GPU 入口 |
|---|---|---|---|
| 模糊半径 blurDp | 窗口 / 卡片 / 芯片 / 实验场 / 按钮 / 开关滑块拇指 / 导航栏 | `GlassScene.cpp:68`（`blurAppliedDp`）、`GlassRecipes.cpp:238/118/167/322` | `GlassPanel.cpp:398` → `Context.cpp:730` gather 12 抽头 → `Shaders.cpp:91-96` |
| 折射高度/强度 | 同上（含导航栏指示器） | `GlassScene.cpp:69-70`、`GlassRecipes.cpp:239-240/111,119-120/160,168-169/323-324,356-357` | `Context.cpp:875-876 uLensHeight/uLensAmount` → `Shaders.cpp:340-344/358` |
| 色散/色差 | 窗口（`chromaticOn` 门）/ 卡片 / 滑块拇指 / 导航栏指示器 | `GlassScene.cpp:73,115`、`GlassRecipes.cpp:172,359` | `Context.cpp:879 uSpectral` → `Shaders.cpp:359-381`（★ 需 `lensOn≥0.5`） |
| 玻璃着色 tint / 表面色 | 按钮 2/3、窗口、卡片、芯片、实验场 | `secondary_imgui.cpp:831-838`、`GlassScene.cpp:80-85,121-124` | `Context.cpp:884-885 uSurfaceTint/uSurfaceMode` → `Shaders.cpp:390-405` |
| 不透明度 opacity / 整体 alpha | 全部玻璃目标 | `GlassScene.cpp:120,138`、`GlassRecipes.cpp:286` | `Context.cpp:880 uPanelOpacity` / `:884 uSurfaceTint` → `Shaders.cpp:388,390-404` |
| 亮度 / 对比度 / 饱和度 / 鲜艳度 | 全部玻璃目标 | `GlassScene.cpp:74-76,110,116-118`、`GlassPanel.cpp:404-407` | `Context.cpp:881-882 uToneCtrl/uToneBoost` → `Shaders.cpp:279-287,386-387` |
| 边光（宽度/强度/柔度/角度/颜色） | 全部玻璃目标（含组件级 `edgeAlphaGain`） | `GlassScene.cpp:57-61`、`GlassRecipes.cpp:136,181` | `Context.cpp:887-893` → `Shaders.cpp:410-437` |
| 阴影（半径/偏移/颜色/强度） | 窗口、卡片、芯片、实验场、按钮 | `GlassScene.cpp:89-92,127-131` | `GlassPanel.cpp:435-469 appendShadeJob` → `Context.cpp:912-916` → `Shaders.cpp:310-320` |
| 圆角（含四角独立） | 全部玻璃目标 | `GlassScene.cpp:107-109`、`GlassRecipes.cpp:233-236` | `Context.cpp:862 uCornerRadii` → `Shaders.cpp:299-309` |
| 弹窗玻璃模糊 | `secondary_window.dialog` | `Dialog.cpp:305,325` | 同上 gather 路径 |
| 缩放预览参数（虚线/填充/弹簧…） | `secondary_window.resize` | `CornerResizeOverlay.cpp:349-431`、`ResizePreview.h` | 走宿主自绘 + 玻璃面板 |

### 3.3 判为 **UNSUPPORTED**（当前渲染器没有这个能力，UI 如实登记为只读，**不做假控件**）

`曝光 exposure`、`伽马 gamma`（参考项目 2.0.1 已移除/需新增 uniform + `pow`）、
模糊 TileMode、模糊抽头数、squircle 超椭圆、RuntimeShader、图层块、
多背景叠加、高光混合模式、菜单玻璃配方、**弹窗内部玻璃配方**
（透镜/闪光/边光/阴影/圆环/尺寸都是 `Dialog.cpp` 的编译期常量，`Modal` 只公开 `glassBlurDp`）。
—— 这些在 `GlassTargets.cpp` 里都调用了 `b.Unsupported(...)`，`GlassRegistry` 会把 `ref` 置空、
UI 画成只读行（`GlassRegistry.cpp:196-214`）。

### 3.4 判为 **DISCONNECTED / STATIC**（登记了但渲染不读，或只启动时读一次）

| 参数 | 状态 | 依据 |
|---|---|---|
| `panel.insetXDp` / `insetYDp` | **DISCONNECTED** | `BuildPanelStyle`（`GlassScene.cpp:64-105`）从不读这两个字段（全仓除声明外零引用） |
| `panel.shadowXDp` / `shadowYDp` | **DISCONNECTED** | 同上（阴影用的是 `DropShadow` 的偏移，不读这两个） |
| 实验场的 7 个派生参数 | **曾为死控件，已修**（§1.6） | 每帧覆写 → 改为变化时重算 |
| `navigation.tabCount` / `iconSizeDp` / `labelSizeDp` | **REAL 但延迟一帧** | `secondary_imgui.cpp:988-992` 在 `tabs.draw()` 之前读，写入当帧、下一帧可见 |
| 「能力缺口」整组 | **只读** | `ref = nullptr`，UI 不提供控件 |

### 3.5 默认值审计（**未发现任何光学参数默认值被改**）

把调试器开发前二进制里的 `secondary_imgui::(anonymous)::s_*` 变量初值逐条读出来，
与当前工作台的 `默认` 段对照（同一份 `params_now.json`）：

| 历史符号 | 历史初值 | 当前默认 | 判定 |
|---|---|---|---|
| `s_ButtonWidthDp` / `s_ButtonHeightDp` | 160 / 48 | 160 / 48 | 一致 |
| `s_ButtonSecondaryWidthDp` / `HeightDp` | 140 / 48 | 140 / 48 | 一致 |
| `s_ButtonSmallSizeDp` | 48 | 48 | 一致 |
| `s_ButtonBlurDp` | 2 | 2 | 一致 |
| `s_ButtonLensAmountDp` / `s_ButtonLensHeightDp` | 24 / 12 | 24 / 12 | 一致 |
| `s_DialogButtonWidthDp` / `HeightDp` | 180 / 48 | 180 / 48 | 一致 |
| `s_DialogGlassBlurDp` | 10 | 10 | 一致 |
| `s_ToggleWidthDp` / `HeightDp` | 64 / 28 | 64 / 28 | 一致 |
| `s_SliderHeightDp` | 24 | 24 | 一致 |
| **`s_DialogAnimationFromButton`** | **`.bss` ⇒ false** | 曾被改成 `true`，**已恢复 false** | ★ 唯一的默认值改动 |

⇒ "默认值取自原有实现"这条在很大程度上是成立的，**唯一的例外就是弹窗动画路径**。

---

## 四、实际验证证据（真机 / 虚拟屏）

设备：测试机（1080×2376，density 2.0）。覆盖层跑在 **测试虚拟屏**
（DisplayId `<display-id>`）上，**不占用物理屏**。

### 4.1 参数确实作用于渲染（极端值 A/B 对比）

在参数文件里把窗口外壳的四个参数改到极端值后重启：

| 参数 | 基线 → 极端 |
|---|---|
| `cornerDp` | 28 → **80** |
| `blurDp` | 10 → **0** |
| `lensAmountDp` | 30 → **0** |
| `saturation` | 1 → **2** |

| 区域 | 平均绝对差 | 明显变化像素 |
|---|---|---|
| **二级窗口区域** | **10.78** | **57.4%** |
| 主题窗口区域（对照组） | 0.00 | 0.0% |

### 4.2 恢复原值 ⇒ 画面完全恢复

| 对比 | 平均绝对差 | 最大通道差 | 明显变化像素 |
|---|---|---|---|
| 基线 vs **恢复原值后** | **0.000** | **0** | **0.00%** |

⇒ 逐像素完全一致（用户第 6 条"重置后完全一致"的硬证据）。

### 4.3 布局与公式修复的实测

| 项 | 判据 | 结果 |
|---|---|---|
| 「点击 N」位置 | 真机截图 | 第一行「液态玻璃按钮 \| 点击 0」✓ |
| 按钮行两行结构 | 真机截图 | 第二行出现「着色」「表面色」✓（修复前两枚被裁到窗口外） |
| 实验场圆角公式 | 运行期保存值 | `cornerXxDp = **64**`（上游公式 256×0.5×0.5），修复前为 128 ✓ |
| 实验场折射派生量 | 运行期保存值 | `lensHeightDp = 25.6`、`lensAmountDp = 51.2`，与上游公式一致 ✓ |

### 4.5 组件独立性（用户第 5 步要求）—— 只改目标组件，其余零变化

同一个 ELF、同一份会话参数，**唯一变量 = `secondary_window.pill_button_1.blurDp`（2 → 10）**：

| 区域 | 平均绝对差 | 明显变化像素 | 期望 |
|---|---|---|---|
| **按钮 1 本体** | **2.31** | **7.4%** | 变化 ✓ |
| 按钮 2（着色） | 0.00 | **0.0%** | 不变 ✓ |
| 窗口外壳·右上 | 0.00 | **0.0%** | 不变 ✓ |
| 窗口外壳·左 | 0.00 | **0.0%** | 不变 ✓ |
| 窗口中部空白 | 0.00 | **0.0%** | 不变 ✓ |

⇒ 组件级参数**确实是实例独立的**：改按钮 1 的玻璃参数，按钮 2、窗口外壳与其它区域
**逐像素完全不变（0.00）**。这同时证明了"窗口外壳不继承给子组件"这条设计。

### 4.6 参数名裁切的修复（§1.8）—— 真机截图确认

修复前参数名显示成「鲜艳度增」「启用面板」（被裁）；
修复后截图：「**启用面板透明**」「对比度」「饱和度」「透明度」「整体 alpha」**全部完整显示** ✓

### 4.7 触摸通道的边界（为什么"拖滑块"没能由我这边验完）

在虚拟屏上用 测试工具 注入做了四类尝试，结论明确：

| 注入方式 | 结果 | 说明 |
|---|---|---|
| 拖滚动条（swipe） | **生效**（内容滚动 ✓） | 滚动条由 ImGui 在 `Begin()` 内部处理，吃的是相对位移 |
| 单击滑块轨道 / 复选框 | 画面无变化 | |
| 1500ms 慢速拖动滑块 | 值不变，**但触发了悬停提示**（"点击直接输入数值（当前精确值…"） | ★ hover 生效 ⇒ **触摸坐标与控件命中区完全正确**、控件本身可交互 |
| 参数文件 → 渲染（重启加载） | **完全生效**（§4.1 / §4.2 / §4.5） | 这才是"参数是否真的作用于渲染"的判据 |

⇒ **参数行控件是可交互的**（hover 已证明命中正确），但
**"按下并持续拖动"这个状态无法由 测试工具 的一次性手势注入构造出来**。
AGENTS 早就记过同款坑：「`input swipe` 不能用来验拖动 —— 它把整串事件一次性灌进去
（UP 提前到），拖动只会跟前面一小段，会得到'拖动坏了'的假结论」，正解是
`input motionevent` 逐事件注入；而这台 Android 16 上 `input -d <DisplayId>` 因
DisplayId > 2^63 不可用。**所以"手指拖滑块 → 立即生效"这一条需要真人手指确认** ——
这是本轮唯一没能自动验完的项。

### 4.8 离线回归

`pwsh -File tests\run_host_tests.ps1` → **36 项全通过**：

| 测试 | 覆盖什么 |
|---|---|
| `GlassBindingTest`（67 断言） | 注册表 → 真实渲染字段（含"按钮 1 改完按钮 3 不变"） |
| `GlassRecipeTest` | 参数 → `PanelStyle`（逐字段，含 dp/px） |
| `ParamRowLayoutTest`（7 组） | 参数行响应式布局内核（滑块随宽度变宽、窄屏双行、重置永不被裁） |
| **`GlassDemoDeriveTest`（3 组，本轮新增）** | **实验场派生量公式**（圆角必须带 `0.5`，默认 64dp / 25.6 / 51.2 —— 少了 0.5 就会红）+ **按钮同行判据**（必须用真实右边界；并反证"用整行宽当输入"会恒假/恒真的差异） |
| `ImeAvoidTest` / `ImeTextDiffTest` | 输入法避让与回填换算 |

---

## 五、尚未完成的验证（如实列出）

1. **滑块/复选框的"手指拖动 → 立即生效"**：结论见 §4.7 —— 控件**可交互**（hover 已证明），
   但 测试工具 的一次性手势注入构造不出"持续按下"状态。**需要用户用真手指拖一次滑块确认**。
   参数→渲染那条链路已由 §4.1/4.2/4.5 用像素证明；
   `SliderFloat → registry.SetF → Commit → 字段` 这段由 `GlassBindingTest` + 代码核对覆盖。
2. ~~组件独立性~~ **已补验**（§4.5，设备恢复后完成）：只改按钮 1，其余区域 0.00%。
4. **色散需要 `lensOn ≥ 0.5`**（`Shaders.cpp:360`）：把折射调成 0 后色散整片失效，
   这是算法本身的行为，UI 的约束文案里没有写明（`GlassTargets.cpp:1055-1057` 只写了
   "仅在启用色散时生效"）。**未修**（属于文案完善，不在本次"恢复原状"范围内）。
5. **边光 Ambient 模式下角度/衰减无效**：同上，已登记为平台说明，未做灰显。
6. **A5 的产品判断**：`dialogAnimationFromButton` 取"移植版历史 false"还是
   "上游源码 true"，我按用户"恢复上一轮之前的效果"取了 **false**；
   如果用户其实想要"从按钮展开"，改一个布尔值即可。


## 六、修改文件清单（本轮）

| 文件 | 改了什么 |
|---|---|
| `app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp` | ① 按钮行恢复**原版两行结构**（第一行 `按钮｜点击N`、第二行 `着色｜表面色｜+`）+ 同行判据改用**真实右边界/窗口内侧**（`GetContentRegionAvail` 对自绘按钮恒为整行宽，是"两枚按钮被裁"的根因）；② **恢复第 5 枚「打开液态弹窗」按钮**（另起一行、180×48dp、点击走 openFrom/open —— 见 §七）；③ 实验场圆角补 `*0.5f`，派生量改成"变化时重算"（消除死控件）；④ 开关区间距、形状芯片文案、色散复选框联动 |
| `app/src/main/cpp/glass/GlassScene.h` | `kButtonCount` **回到 5**（第 5 枚 = 「打开液态弹窗」，用户要求恢复）、`buttonWidthDp/HeightDp[5]`、`dialogAnimationFromButton` `true`→`false`（含两层证据注释） |
| `app/src/main/cpp/glass/GlassScene.cpp` | `ButtonId` / `ButtonName`：index 4 **刻意不返回目标 ID**（弹窗触发按钮不进调试器） |
| `app/src/main/cpp/GlassTargets.cpp` | **移除弹窗专属调试入口**：不再调用 `BindDialogTarget`（函数保留 + 注释说明）；按钮注册循环改为 `i < kButtonCount - 1`（0..3）⇒ `secondary_window.dialog` 与 `pill_button_5` 都从注册表消失，而弹窗本身照画照点 |
| `app/src/main/cpp/GlassWorkbench.cpp` | 可用宽度改 `GetContentRegionMax()`（修名称被裁）+ 新增"越界即 WARN"的宿主侧硬自检 |
| `app/src/main/cpp/LiquidGlassRuntime.cpp` | 主窗口提示文案改为与实际一致 |
| `app/src/main/cpp/GlassDemoDerive.h`（新增） | **本轮修复的回归防护**：实验场派生量公式（系数来自上游原文）+ 同行判据（必须用真实右边界） |
| `tests/GlassDemoDeriveTest.cpp`（新增） | 上面这条的 3 组断言（含"少了 0.5 就会红"的显式反证） |
| `tests/`（新增，本轮之前已加） | `ParamRowLayoutTest`、`ImeAvoidTest`、`ImeTextDiffTest`（随本次一并跑通） |
| `research/regression_audit/backup_before_restore/`（新增） | 恢复前的 5 个源文件快照 + 两份参数 JSON + 历史 ELF 的 sha256 记录（证据留存） |

> **没有做**：`git reset`、整体回滚、批量替换、无关重构。
> 所有恢复都是**按文件/按代码块**做的，调试器本身的功能（参数注册、工作台、持久化、
> 撤销重做、组件实例化）**全部保留**。


---

## 七、用户纠正：恢复「打开液态弹窗」按钮 + 移除弹窗专属调试入口

> 这一节记录**我上一轮的一个误判**以及按用户要求做的修正。用户原话：
> 「在以前版本中有个叫"打开液态玻璃弹窗"的按钮，你怎么直接给我删除了」
> 「之前项目是参考这个源代码，实现二级窗口的内部按钮布局和视觉效果，现在你回调回去吧，
> 但是还要保留那个主题菜单调试功能」

### 7.1 我错在哪

上一轮我用「开发前二进制里没有第 5 个按钮符号」为据，判定按钮行里的
**「打开液态弹窗」是上一轮擅自新增**并**删除**了它。**这个判断是错的**：

* 证据没错（上游 `secondary_imgui.cpp:331-364` 确实只有 4 枚按钮，弹窗触发按钮在卡片区
  `:532` 的 `s_DialogButton`），但**用户的诉求不是"与上游逐字一致"**，而是
  **"恢复他此前看到的二级窗口"** —— 那个按钮在他的项目里**是刻意加的**（用来演示弹窗），
  并且它**功能真实**（点了会开弹窗）。
* 更关键的一条：用户明确说了「一个 Demo 不出现在调试器里，不代表它应该从二级窗口被删除」
  —— 我把"不该有专属调试页"错当成了"不该存在"。

⇒ **教训（已写进 AGENTS 顶部）**：**"不在调试器里注册"与"组件该不该存在"是两件事**，
不能用前者推断后者；恢复原状时若用户点名某组件存在过，以用户为准。

### 7.2 恢复内容（位置 / 尺寸 / 文字 / 行为）

| 项 | 恢复成什么 | 依据 |
|---|---|---|
| 位置 | **按钮行第 5 枚，另起一行**（前三行为：`[液态玻璃按钮] 点击 N` / `[着色][表面色][+]` / `[打开液态弹窗]`） | 用户要求恢复它原来的位置 |
| 尺寸 | **180×48dp**（`StageTune::buttonWidthDp[4] = 180.0f`，`kButtonCount` 回到 5） | 上游 `:535` `px(180.0f)` × `px(48.0f)` |
| 文字 | `打开液态弹窗` / `Open liquid dialog` | 上游 `:532`（与卡片区按钮同一文案） |
| 外观 | 与其它 PillButton 同一套玻璃配方（`PillTune` 默认值 = ytbl 原常量），动效状态 `s_MotionDialog` 一并恢复 | 不新造样式 |
| 点击行为 | `scene.modal.openFrom(title, msg, box)` 或 `scene.modal.open(...)`，由 `dialogAnimationFromButton` 决定 —— **与卡片区那个按钮逐行相同** | 上游 `:543-549` |
| 弹窗动画 | **未改任何参数**：`GlassConfig.h` / `Dialog.cpp` 与上游 **SHA-256 逐字节相同**；弹簧/时长/缩放全部是原有常量 | 用户担心"AI 助手 改过弹簧参数"—— 实测**没有** |

**验证证据**（真机虚拟屏截图）：按钮行显示为
`[液态玻璃按钮] 点击 0` → `[着色][表面色][+]` → `[打开液态弹窗]` ✓

### 7.3 顺带解决的一处"死控件"

第 5 枚按钮**不注册进调试器**之后，`secondary_window.pill_button_5` 这个目标也一并消失
—— 这正好消掉了审计里的一处隐患：从前 `pill_button_5` 绑的是 `scene.buttons[4]`，
而屏幕上画的是另一个对象（`dialogTrigger`），那一页 60+ 条参数**改了不会有任何可见效果**。

### 7.4 按钮行的同行判据：用窗口内侧而不是内容区

恢复第 5 枚按钮后实测发现：**`+` 掉到了单独一行**（破坏了原版「着色｜表面色｜+」的同行关系）。
根因不是按钮尺寸，而是**本项目的全局 ImGui 样式**：

| | WindowPadding | ItemSpacing | 四枚按钮需要 | 实际可用 |
|---|---|---|---|---|
| 上游（ImGui 默认 × ScaleAllSizes(2)） | 16px | 16px | 688px | 728px ✓ 放得下 |
| 本项目（宿主主题 × ScaleAllSizes(2)） | **44px** | **24px** | 704px | 676px ✗ 差 28px |

用户任务书允许"对原版固定坐标与当前动态窗口尺寸之间的差异做**最小必要适配**，
但不得改变原有的视觉层次和**相对位置关系**" ⇒ 我选择**保住相对位置关系**：
按钮行的"能否同行"判据从**内容区右边界**放宽到**窗口内侧（`InnerRect.Max.x`）− 4dp**，
**不去动全局样式**（那会影响整个主题），**也不改按钮尺寸**（那是原版参数）。

**副作用（如实记录）**：`+` 因此贴得更靠右，**它的点击计数在默认窗口宽度下会换到下一行**
（原版在 760px 窗口里是同行的）。这是尺寸冲突，不是实现缺陷：
判据是**响应式的** —— 把二级窗口拉宽约 60px，计数就会自动回到 `+` 右侧。

### 7.5 移除弹窗专属调试入口（用户 §四）

| 要求 | 实现 |
|---|---|
| 不为弹窗生成独立调试分页 | `BindDialogTarget(registry, scene)` 调用**移除**（保留函数本体，注释说明为什么不再调用） |
| 不为弹窗内部按钮建立调试分组 | 同上（弹窗内部两个 PillButton 本来就未注册） |
| 不再展示无意义的弹窗专属参数 | `secondary_window.dialog`（原本只有 `glassBlurDp` + 触发按钮尺寸 + 动画开关）整页消失 |
| 弹窗触发按钮不入调试器 | 按钮注册循环改为 `i < kButtonCount - 1`（0..3），`ButtonId(4)` 不返回目标 ID |
| **不得因此删除弹窗的真实渲染与事件** | 弹窗的绘制（`secondary_imgui.cpp` 的 `if (scene.modal.visible) scene.modal.draw(...)`）、两个触发按钮、动画路径**全部原样保留** |
| 调试注册与组件存在解耦 | 就是本节的做法：组件照画照点，只是不产生调试目标 |

**验证**（运行期参数文件 = 注册表真实内容的导出）：

```
注册表里的 secondary_window 目标共 33 个（移除前 35 个）
   secondary_window.dialog          存在? False   ✓ 弹窗调试页已移除
   secondary_window.pill_button_5   存在? False   ✓ 触发按钮不进调试器
   secondary_window.pill_button_1..4 都在? True   ✓ 其它按钮调试保留
```

其余 33 个目标（窗口外壳 / 卡片 / 6 枚芯片 / 导航栏 / 4 枚按钮 / 实验场 / 弹出菜单 /
缩放 / 2 根滑块及其子层 / 3 个开关及其子层）依然可调 ✓


---
