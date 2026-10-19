# 液态玻璃弹窗「把 Aurora 运行时信息带上来」—— 根因与修复

> 日期：2026-10-14　现象：点击任意「打开液态弹窗」按钮后，
> 页面**最底部**的「Aurora 运行时信息」被插到了**窗口中部**（弹窗正下方）。
>
> 用户截图（补充提供）与我的独立复现**完全一致**。
> 一句话根因：**弹窗绘制时把 ImGui 的排版光标留在了弹窗按钮上，而运行时信息是在弹窗之后绘制的。**

---

## 一、真实根因（有运行时数据，不是推测）

### 1.1 现象与坐标

`secondary_imgui.cpp` 的绘制顺序是：

```
2143  DrawContents();          ← 内部末尾（1316 行）绘制弹窗 scene.modal.draw(...)
2146  DrawHostDiagnostics();   ← 「Aurora 运行时信息」在这里画
2149  RunSecondaryWindowGestures(window);
```

⇒ **弹窗先画，运行时信息后画**。任何被弹窗改动的排版状态，都会直接落在这段内容上。

### 1.2 现场数据（`AURORA_DIALOG_PROBE` + `AURORA_DIALOG_TRACE`，真机虚拟屏）

| 时刻 | 光标屏幕坐标 | 说明 |
|---|---|---|
| **修复前** 弹窗绘制前 | `(60, 5584)` | 内容流末尾（页面总高 ≈ 6484，滚动范围 4888） |
| **修复前** 弹窗绘制后 | `(60, 594)` | ★ **被往上拖了 4990px**（窗口 y=16 起，即窗口顶部往下 578） |
| **修复前** 运行时信息开画 | `(60, 594)` | 于是它从窗口中部开始排版 ⇒ **用户看到的现象** |
| **修复后** 弹窗绘制前 | `(60, 5584)` | — |
| **修复后** 弹窗绘制后 | `(60, 5584)` | ★ **已恢复** |
| **修复后** 运行时信息开画 | `(60, 5584)` | 回到内容流末尾 ✓ 滚动范围也从 4888 恢复到 **5784** |
| **修复后 + 滚到 633** 运行时信息开画 | `(60, 4951)` | `4951 + 633 = 5584` ✓ 精确对应；**滚动位置 633 保持不变** |

### 1.3 机制链

1. 弹窗里的 **Cancel / OK** 是 ytbl 的**自绘**按钮 → 绘制时调用 `lgx::probeBox(box, owner, interactive)`。
2. `probeBox` 转发给宿主 `HostBackend::onProbe`（`secondary_imgui.cpp:638-656`）：

   ```cpp
   ImGui::SetCursorScreenPos(ImVec2(box.left, box.top));   // ← 改写光标
   if (!interactive) { ImGui::Dummy(size); return probe; } // ← 推进光标
   ImGui::PushID(owner);
   const bool released = ImGui::InvisibleButton("##lgx_probe", size, ...);  // ← 推进光标
   ImGui::PopID();
   ```

   **它改写了 ImGui 的排版光标，而且从不恢复。**
3. 弹窗 → 之后 `DrawHostDiagnostics()` 从**弹窗按钮的位置**继续排版。

### 1.4 为什么"平时看不出问题"

内容里的其它自绘组件（按钮 / 开关 / 滑块 / 芯片）**本来就位于内容流之中**，
`probeBox` 把它们的光标留在"内容流当前位置附近" ⇒ 副作用不可见。
而弹窗是**浮动层**，它的按钮坐标与内容流位置**无关**（垂直居中，y≈594）——
一 probe 就把光标**从 5584 拽回 594** ⇒ 副作用第一次变得肉眼可见。

### 1.5 五个排查方向的结论（对照用户任务书 §三）

| 用户怀疑 | 实测结论 |
|---|---|
| 1 触摸事件穿透 | **不是**。没有任何回调被重复触发；`modal` 只有一个实例，点击只走一条路径 |
| 2 组件状态错误共享 | **不是**。运行时信息的展开状态（`CollapsingHeader`）没有被改动，它只是被**画到了别的位置** |
| 3 页面滚动状态 | **不是**。滚动偏移全程不变（修复前后都是 0；滚到 633 后打开弹窗仍是 633） |
| 4 图层与 Overlay | **不是**。弹窗本来就在窗口级 Overlay（`s_RecordOverlayTouchShapes`），没有把运行时信息放进 Overlay |
| 5 动画状态与布局副作用 | ★ **就是它** —— 弹窗的**绘制副作用**（推进父窗口的排版光标），属"共享的布局状态被弹窗污染" |

---

## 二、修复（最小、精准，符合任务书 §七 边界）

`secondary_imgui.cpp` 的 `modal.draw(...)` 前后，把**整行排版状态**原样存取：

```cpp
ImGuiWindow* const win = ImGui::GetCurrentWindow();
const ImVec2 savedCursorScreen      = ImGui::GetCursorScreenPos();
const ImVec2 savedCursorPosPrevLine = win->DC.CursorPosPrevLine;
const ImVec2 savedCurrLineSize      = win->DC.CurrLineSize;
const ImVec2 savedPrevLineSize      = win->DC.PrevLineSize;
const float  savedTextBaseOffset    = win->DC.CurrLineTextBaseOffset;

scene.modal.draw(lgx::Box(...));                       // ← 弹窗照旧绘制，动画/视觉一个字没动

ImGui::SetCursorScreenPos(savedCursorScreen);
win->DC.CursorPosPrevLine     = savedCursorPosPrevLine;
win->DC.CurrLineSize          = savedCurrLineSize;
win->DC.PrevLineSize          = savedPrevLineSize;
win->DC.CurrLineTextBaseOffset = savedTextBaseOffset;
```

* **没有**改弹窗的任何视觉/动画参数（`Dialog.cpp` / `GlassConfig.h` 一行未动）。
* **没有**改运行时信息模块（没隐藏、没删除、没锁坐标 —— 任务书明令禁止）。
* **没有**改窗口外壳、滚动容器、调试器。
* **没有**动 `onProbe` 本身 —— 那才是"根治"，但它会改变**所有**自绘组件的排版语义
  （平时无弹窗时的内容流位置也会变），超出"只修弹窗副作用"的边界。这一条**明确记在这里**，
  供后续决策。

---

## 三、诊断入口（本轮新增，默认零开销）

| 环境变量 | 作用 |
|---|---|
| `AURORA_DIALOG_PROBE=1` | 启动后（延迟 300 帧，留出"先滚动"的时间）自动用 `open()` 打开弹窗 —— 动画二：从中心玻璃球 |
| `AURORA_DIALOG_PROBE=2` | 同上，但用 `openFrom()` —— 动画一：从触发按钮展开 |
| `AURORA_DIALOG_TRACE=1` | 每帧打印三处光标位置（弹窗前 / 弹窗后 / 运行时信息开画）+ 滚动偏移 |

两者都只在显式设置时生效，**发布行为不受影响**。

**为什么需要它**：测试工具 的一次性手势注入**构造不出"按下并抬起"**（AGENTS 早记过：
`input swipe` 会提前送 UP），所以无法靠注入点击弹窗按钮来复现；
这个入口让问题可以**被脚本化地复现和验证**，而不是靠手点。

---

## 四、验证结果

| 项 | 证据 |
|---|---|
| 复现（修复前） | 光标 `5584 → 594`；截图：弹窗下方紧跟「Aurora 运行时信息」标题栏（**与用户截图一致**） |
| 修复（后） | 光标 `5584 → 5584`；滚动范围 4888 → **5784**（内容高度恢复正常）；截图：运行时信息从视口消失（回到页面底部） |
| 滚动位置不变 | 滚到 633 后打开弹窗，滚动仍是 633/5784 ✓ |
| 任意滚动位置 | 运行时信息开画光标 `4951 = 5584 − 633` ✓ 与内容流末尾严格对应 |
| 两条动画路径 | `open`（动画二）与 `openFrom`（动画一）都走同一个 `modal.draw` 收口点 ⇒ 修复同时覆盖；`PROBE=2` 截图与 `PROBE=1` 逐字节相同（两者最终状态都是"弹窗居中"） |
| 弹窗视觉/动画未变 | `ytbl/Dialog.cpp`、`ytbl/GlassConfig.h` 与参考源码 **SHA-256 逐字节相同**（本轮未触碰） |
| 离线回归 | `tests/run_host_tests.ps1` → **36 项全通过** |

### 关于"三个同名按钮"

我在当前源码里只找到**两个**「打开液态弹窗」按钮：

1. 按钮行第 5 枚（`secondary_imgui.cpp:824`，180×48dp，另起一行）
2. 卡片区的 `scene.dialogTrigger`（`secondary_imgui.cpp:1118`）

两者**调用同一个 `scene.modal` 实例、走同一个 `modal.draw()` 收口点**，
所以"不同按钮各自的动画"实际由「弹窗动画一 / 动画二」两个复选框
（`dialogAnimationFromButton`）决定，与按哪个按钮无关。
**这两种动画路径本轮都验过**（见上表）。
若你看到的第三个按钮在别处（例如某个滚动位置上的另一个入口），
把它的位置或文案告诉我，我再核对 —— 但无论入口有几个，
它们都会经过同一个 `modal.draw()`，**本修复对它们一体生效**。

---

## 五、尚未验证 / 需要你确认的

1. **手指点击弹窗按钮 → 弹窗弹出**这条链路：仍然只能由真手指验
   （注入通道构造不出持续按下）。本修复不涉及这条链路的正确性。
2. **弹窗关闭（Cancel / OK）后的状态**：关闭动画与关闭后的页面状态我没有真机点过；
   从代码看关闭只影响 `modal` 自身，不触碰排版光标 ⇒ 不应有跳变（但未实测）。
3. **连续快速开合**（任务书 §六.8）：未实测，需真手指。
4. 我**没有**实测 `PROBE=2` 的**动画过程**截图（只验了最终状态与光标轨迹）；
   动画本身本轮未改动，风险为零。
