# 调试器参数行响应式布局 + 重置按钮裁切 + 系统输入法修复

> 本文记录 2026-10-14 那一轮的三项修复：主窗口调试器的**参数行布局**、
> **重置按钮被裁切**、以及**系统输入法（IME）在纯 ELF 上失效**。
> 三项都做了根因定位（不是"调大几个常量"），并各自有可执行的验收证据。

---

## 一、参数滑块不随窗口变宽 —— 根因

### 现象

主窗口（主题）已经支持拉伸，但液态玻璃参数行的滑块**永远那么短**：
把窗口拉宽一倍，滑块宽度纹丝不动；`背景缩放 X / 背景缩放 Y / 背景混合量`
这些 Float 参数尤其明显，精细值根本调不出来。

### 根因（在 ImGui 源码里核实，不是猜）

旧实现是 ImGui 的**四列表格** + 每帧按可用宽度算列宽：

```cpp
ImGui::TableSetupColumn("名称", ImGuiTableColumnFlags_WidthFixed, columns.name);
ImGui::TableSetupColumn("控件", ImGuiTableColumnFlags_WidthFixed, columns.control);
ImGui::TableSetupColumn("数值", ImGuiTableColumnFlags_WidthFixed, columns.value);
ImGui::TableSetupColumn("重置", ImGuiTableColumnFlags_WidthFixed, columns.reset);
```

看起来是"响应式"，实际有两条硬约束把它废掉了：

| # | 事实（文件:行） | 后果 |
|---|---|---|
| ① | `TableSetupColumn(..., WidthFixed, w)` 里的 **w 只在表格第一次建立的那一帧生效**：`imgui_tables.cpp:1572-1585` 整段被 `if (table->IsInitializing)` 包着，而 `IsInitializing` 只在 `table->RawData == NULL`（首次创建）时置位（`:548`），并在 `BeginTable` 结束时清掉（`:1516`） | 之后**每帧算出来的新列宽被静默忽略**。表格是在窗口默认宽度（760px）时创建的 ⇒ 列宽从此锁死在那一刻，窗口拉宽滑块也不动 |
| ② | 表格里**没有** `WidthStretch` 列时，剩余宽度不会分给任何列：`imgui_tables.cpp:958-983` 只对 `WidthStretch` 列分配 `width_avail_for_stretched_columns` | 拉宽窗口只是右边多出一块空白，四个列宽都不变 |

⇒ **"滑块永远是那么短"不是参数写死了宽度，而是列宽只被 ImGui 采纳了一次。**

### 根因二（重置按钮被裁切，与上面同源）

旧代码在窄窗口下做了两次"保底"：

```cpp
float usable = available;
if (usable < 300.0f) usable = 300.0f;      // 可用宽度不足 300 也按 300 算
...
if (out.control < 110.0f) { out.control = 110.0f; ... }   // 控件列保底 110
```

于是**四列之和会大于真实可用宽度** ⇒ 表格比窗口宽 ⇒ 最后一列（重置）落在
表格裁剪矩形之外被切掉。而列宽又被 ①锁死 ⇒ **之后把窗口拉宽也不会恢复**
（用户报的"拉伸主窗口宽度，问题仍然存在"就是这么来的）。
同理，`else if ((window->Flags & NoTitleBar) == 0)` 那条记录标题栏形状的分支在
二级窗口删掉标题栏之后已经**恒假**（死分支），这类"语义已腐"的地方一并清掉。

### 修法：自己算每一行的宽度（纯逻辑内核 + 逐帧重排）

新增 `app/src/main/cpp/ParamRowLayout.h`（**纯逻辑，零 ImGui/零 Android 依赖**）：

```
宽屏：  参数名称 | 可伸缩滑块 | 数值 | 重置
窄屏：  第一行 参数名称 | 数值 | 重置
        第二行 滑块（占满整行）
```

* 让位顺序固定：**名称（可裁） → 滑块（先缩到下限） → 数值（硬宽度） → 重置（最后才动）**
  ⇒ "重置按钮被裁"从结构上不可能发生；
* 可用宽度、最长参数名、最长数值、按钮文字宽**每帧现量**，
  容器一变宽/变窄当帧重排；
* `GlassWorkbench.cpp` 的 `DrawParamRow()` 不再用表格，改用
  `SetCursorScreenPos` 精确摆放四格（滑块吃满余量），窄屏时把滑块换到第二行；
* 分组标题、以及"（N 项，已改 M 项）"这类右对齐文字走新增的 `FitTextToWidth()`
  **按可用宽度截断加省略号**（ImGui 不会自动换行，只会硬切出"已改 3"这种断口）。

### 验收

* 离线回归 `tests/ParamRowLayoutTest.cpp`（**7 组**）：320dp/480dp/720dp 三档
  （本工程密度 2.0 ⇒ 640/960/1440px）逐档断言"滑块严格变宽"、
  "名称+数值+重置+间距 ≤ 可用宽度"、"重置 ≥ 按钮自然宽度"、
  "空间不足必须切双行且滑块占满整行"、极端窄宽度不返回负数/NaN；
* 真机：见 §四。

---

## 二、系统输入法（IME）

### 链路（原样保留，未改架构）

```
二级窗口的 InputText 激活
  → secondary_imgui::WantsTextInput()             （每帧事实）
  → Renderer::wantsTextInput_                     （atomic）
  → aurora_overlay 主循环：wantIme && wants && !imeWanted（**上升沿**）
  → ImeHost::show() → InputBridge（TCP）→ app_process 起 Java 桥接
  → AuroraImeService：1×1 透明窗 + EditText + InputMethodManager.showSoftInput
  → 文本经 EDIT 回传 → Renderer::textInput/deleteBackward → ImGui 输入框
```

### 三个真根因

| # | 根因 | 证据 | 修法 |
|---|---|---|---|
| ① | **交付的单文件包默认带 `--no-ime`**：`tools/pack_elf_sh.ps1:28` 的 `$DefaultArgs` 里就写着它，而 `wantIme=false` 时主循环那两个分支都不成立 ⇒ **一条日志都不打**，从外面看和"输入法坏了"完全一样 | `dist/aurora_overlay.sh` 第 5 行 `DEFAULT_ARGS='... --no-ime'`；`aurora_overlay.cpp` 的 `wantIme/wants` 判据 | 打包默认参数去掉 `--no-ime`（与 ELF 默认值一致）；需要无键盘环境时再显式加；**并且**在"想要输入但被 `--no-ime` 吃掉"时打一条 WARN |
| ② | **键盘收起后再也拉不起来**：`ImeHost::onHidden()` 只打日志、不复位 `imeWanted`；而 `s_WantsTextInput` 是**全量赋值**且只在"真的画到输入框那一帧"执行 ⇒ InputText 不失焦时它恒为 true ⇒ `!imeWanted` 的上升沿永不再来 | `aurora_overlay.cpp` 的 `onHidden`；`secondary_imgui.cpp` 的 `s_WantsTextInput = ImGui::IsItemActive()` | ① `ImeHost` 记录 HIDDEN 事件，主循环消费它：复位 `imeWanted` + `Renderer::requestTextInputEnd()`（下一帧对激活的输入框 `ClearActiveID()` 一次，**只在事件上做一次，不轮询不强拉**）；② `secondary_imgui::BeginTextInputFrame()` 每帧开头把 `s_WantsTextInput` 清零，只有本帧真画到输入框才置真 |
| ③ | **键盘被我们这层玻璃压住、按键点不动**：我们的图层挂在 SurfaceFlinger 的 `mRoot` 下，是 WMS Display Root 的**兄弟**（z 序在整棵 WMS 子树之上，含输入法弹层）；而 Java 侧明明拿到了 `WindowInsets.Type.ime()`，却**只取 isVisible、把几何丢了** | `Renderer.cpp` 的 `GatherVisibleTouchShapes()`（主题窗口整矩形进拦截区）；`AuroraImeService.java` 的 insets 监听 | ① Java 把键盘矩形用协议里**原本预留的 3 个 0 字段**回传（`IME <id> <vis> <l> <t> <r> <b>`，旧 ELF 只读前两段 ⇒ 向后兼容）；② `Renderer::GatherVisibleTouchShapes()` 把**键盘那一块从触摸拦截区里挖掉**（按 bottom 夹紧，整块被盖住的形状直接丢弃）；③ `ImeAvoid.h` 做 **adjustPan 语义的窗口避让**：与键盘重叠的窗口整体上移，键盘收起后回原位（纯逻辑，有离线回归） |

顺带修掉的一个语义错误：Java 回传的是**整段文本**，而 `Renderer::textInput()` 的语义是
**增量追加** ⇒ 敲 `a` 变 `a`、再敲 `b` 会追加整段 `ab` 变成 `aab`。现在宿主侧做
"公共前缀 + 退格数 + 增量"的换算，退格按 **UTF-8 字符数**（按字节退会把汉字退成半个）。

### 验收

* 离线回归 `tests/ImeAvoidTest.cpp`（**6 组**）：连续 600 帧位置必须收敛（"每帧再叠一次位移"会让窗口一秒飞出屏幕）、屏幕顶夹紧、键盘收起回原位、NaN/负高度不产生垃圾位移；
* 真机：`dumpsys input_method` 看 `mCurClient`（判据是**它是不是我们的辅助包 uid**，不是 `mInputShown`）+ logcat 的 `UI 请求文字输入 / 键盘已收起 / 输入法弹层矩形`。

---

## 三、改动文件清单

| 文件 | 改了什么 |
|---|---|
| `app/src/main/cpp/ParamRowLayout.h` | **新增**：参数行响应式布局内核（纯逻辑） |
| `app/src/main/cpp/ImeAvoid.h` | **新增**：输入法避让计算（纯逻辑） |
| `app/src/main/cpp/GlassWorkbench.cpp` | 去掉四列表格，改逐帧自算布局；新增 `FitTextToWidth()`（标题/右对齐文字按宽度截断）；`AURORA_UI_TRACE=1` 时打印"可用宽度 / 各列宽 / 控件实测宽度" |
| `app/src/main/cpp/InputBridge.h` | `ImeCallbacks::onImeInsets()` + 解析 `IME` 行新增的 4 个几何字段 |
| `app/src/main/cpp/Renderer.h/.cpp` | `setImeInsetRect()` / `requestTextInputEnd()` / `imeInsetRect()`；帧首释放输入焦点；末端绘制后把键盘矩形写进 `HostCapabilities` |
| `app/src/main/cpp/HostProtocol.h` | `HostCapabilities` 增加键盘矩形四字段 |
| `app/src/main/cpp/LiquidGlassRuntime.cpp` | 主题窗口输入法避让 + 键盘矩形转发给二级窗口 |
| `app/src/main/cpp/third_party/liquidglass/secondary_imgui.h/.cpp` | `HostInfo` 增加键盘矩形；`BeginTextInputFrame()`（每帧清零"想要输入"）；二级窗口避让 |
| `root-host/elf/aurora_overlay.cpp` | `onHidden` → HIDDEN 事件 + 复位 + 失焦；`onEdit` 增量换算；`onImeInsets` 转发；`--no-ime` 被吃掉的 WARN；虚拟屏 IME 提示；`--verbose` 下行缓冲 stdout |
| `ime-bridge/src/com/aurora/imebridge/AuroraImeService.java` | 回传键盘真实几何（含可见期间几何变化） |
| `tools/pack_elf_sh.ps1` | 默认参数去掉 `--no-ime` |
| `tests/ParamRowLayoutTest.cpp`、`tests/ImeAvoidTest.cpp` | **新增**两组离线回归 |
| `tests/run_host_tests.ps1` | 登记上面两组 |

---

## 四、真机验收记录

见 `research/resize_preview/`（截图）与本文日期对应的运行日志：
`/data/local/tmp/ov_fix.log`（真机，`AURORA_UI_TRACE=1`）。

验收命令（都可复现）：

```sh
# 参数行布局：把窗口拉宽/拉窄，读每帧算出来的列宽
grep '\[布局\]' /data/local/tmp/ov_fix.log | tail -5
#   → 形如：可用=688 模式=单行 名称=218 滑块=212 数值=122 重置=88
#           可用=1024 模式=单行 名称=246 滑块=520 数值=122 重置=88   ← 拉宽后滑块真的变长

# 输入法：点击输入框前后
logcat -s aurora_overlay | grep -E 'UI 请求文字输入|键盘已收起|输入法弹层矩形|no-ime'
dumpsys input_method | grep -E 'mCurClient|mInputShown|mImeWindowVis'
```
