# 触控数据行布局抖动 + 输入框首次点击迟钝 —— 根因与修复

> 日期：2026-10-14　两个独立缺陷，都有真机数据支撑。
> 一句话结论：
> **① 抖动** = 触控数据那行开了 `PushTextWrapPos`，数字变长时折行 ⇒ item 高度 2↔3 行切换 ⇒ 下方整体上下移动。
> **② 首次点击迟钝** = 输入法桥接的 Java 冷启动（实测 **1755ms**）被放在"用户第一次点输入框"的同步调用栈里。

---

## 任务一：触控数据实时更新导致 UI 抽搐

### 1. 动态数据为什么会触发 UI 重新布局

旧代码（`secondary_imgui.cpp`，修复前）：

```cpp
ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
ImGui::Text("Pan %.1f,%.1f  Pinch %.2f  Rot %.2f rad", …);
ImGui::PopTextWrapPos();
```

* `PushTextWrapPos()` **打开了自动换行**；`ImGui::Text()` 在即时模式 UI 里是**真实布局 item**，
  它的高度 = 文本实际行数 × 行高。
* 触控数值变化 ⇒ 字符串长度变化（`0.0` 3 字符 ↔ `-100000000.0` 12 字符）⇒
  一旦超过可用宽度就**折成更多行** ⇒ **这个 item 的高度变了** ⇒ ImGui 把后续所有 item 顺次下推
  ⇒ 下方 Demo 整体上下移动 ⇒ 手指还在动 ⇒ 下一帧数字变短又折回 ⇒ **反复，看起来就是窗口在抽搐**。

### 2. 实测数据（同二进制、A/B 开关 `AURORA_TOUCH_LEGACY`，虚拟屏）

仿真入口：`AURORA_TOUCH_SIM=1` 让 Pan/Pinch/Rot 在"最短文本"与"最长文本"之间每 30 帧翻转一次；
`AURORA_DIALOG_TRACE=1` 打印该行的 **item 高度** 与 **行后光标 y**（后者=下方内容的起点）。

| | 短值 `Pan=0.0,0.0` | 长值 `Pan=-100000000.0` | 结论 |
|---|---|---|---|
| **旧布局** 行高 | **80.0**（2 行） | **120.0**（3 行） | 高度随数据变 ✗ |
| **旧布局** 行后光标 y | **396.0** | **436.0** | **下方整体抖动 40px** ✗ |
| 新布局 v1（一行三段）行高 / 行后 y | 40.0 / 356.0 | 40.0 / 356.0 | 零抖动 ✓ 但**窄窗口三段会重叠** ✗ |
| **新布局 v2（垂直分列，最终版）** 块高 / 块后 y | **168.0 / 484.0** | **168.0 / 484.0** | **零抖动 + 不重叠** ✓ |

样本：旧布局 555（短）/446（长）；v1 261/330；**v2 300/282**。

### 3. 采用的稳定布局方案（垂直**分列**：每个字段独占一行）

> ★ 用户后续反馈："把这 3 个信息分列显示，而不是并排显示 —— 现在窗口缩小到一定程度时
> 它们全部重叠在一起了"。**确认属实**：同一行三段固定比例（0%/44%/70%）在窄窗口下，
> `Pan` 的文本（例如 `Pan -99999999.9,-99999999.9`）会越过自己那一段的右边界压到 `Pinch` 上
> ⇒ 三个字段**叠在一起**。已改为**每个字段一整行**：

```cpp
const ImVec2 blockMin = ImGui::GetCursorScreenPos();
const float  blockW   = ImGui::GetContentRegionAvail().x;          // 每帧重读 ⇒ 缩放时重算
const float  lineH    = ImGui::GetTextLineHeight();
const float  spacingY = ImGui::GetStyle().ItemSpacing.y;
const float  blockH   = lineH * 3.0f + spacingY * 2.0f;            // 恒定 = 3 行 + 2 间距
ImGui::PushClipRect(blockMin, ImVec2(blockMin.x + blockW, blockMin.y + blockH), true);
ImGui::TextUnformatted(panBuf);      // 单行、不换行 ⇒ 行高恒为 lineH
ImGui::TextUnformatted(pinchBuf);
ImGui::TextUnformatted(rotBuf);
ImGui::PopClipRect();
```

对应任务书的每一条要求：

| 要求 | 实现 |
|---|---|
| 放入高度固定的显示容器 | 三行块，高度恒为 `3×行高 + 2×行间距`（实测 **168.0**，与数值长度无关） |
| 数据更新只能影响容器内绘制、不得改高度 | 每行都是一个**单行** item（没有 `PushTextWrapPos`）⇒ 行数恒为 3，与文本长度无关 |
| 强制单行、禁止换行 | 每个 `TextUnformatted` 单行；不设任何 wrap |
| 超宽时裁剪/截断 | `PushClipRect` 把整块裁到内容宽度内；超宽被裁而不是溢出/重叠 |
| **字段之间不重叠、不互相推挤** | **每个字段独占一整行**（用户点名要求的分列）⇒ 任何窗口宽度都不可能重叠 |
| 缩放时重新计算可用宽度 | `GetContentRegionAvail().x` 每帧重读 |
| 固定小数精度 | `%.1f / %.1f / %.2f / %.2f`（沿用原格式） |
| 极端值不破坏布局 | 用 `-100000000.0` / `100000000.00` / `-36000000.00` 压过（实测块高与光标 y 不变） |
| 不缩小页面/不隐藏 Demo/不降刷新率 | 数据仍**每帧**读取；未动任何其它组件 |
| 不产生每帧内存分配 | 栈上 `char[]` + `snprintf`；无 `std::string`、无堆分配 |

**关于"等宽数字"**：ImGui 字体不支持 OpenType `tnum`，无法启用 Tabular Figures；
分列后每个字段独占一行、行内左对齐，数字长度变化**不会推动任何其它字段**，
已达到用户要求的"其它字段明显左右跳动"的消除效果。

**★ 踩到并修掉的一个坑**：第一版 `blockH` 只写了 `3×行高`，漏了行与行之间的 `ItemSpacing`
⇒ 第三行 `Rot` 正好落在裁剪矩形外**被裁掉**（截图证据：只显示 Pan / Pinch 两行）。
必须写成 `3×行高 + 2×行间距`。

### 4. 更新机制审计（用户第 3 条）

| 检查项 | 结论 |
|---|---|
| 触摸数据是否仍实时 | 是。`s_HostInfo.gesture*` 每帧读取，未降频、未丢弃事件 |
| 是否触发整窗布局重算 | 修复后**不会**：该行不再产生可变高度 item |
| 文字测量/布局缓存是否被数值更新失效 | **不存在**这个问题：`AddText` 不做测量、不进布局缓存 |
| 是否每帧分配/创建对象 | 无。三段文本用栈上缓冲；`ImGui::Dummy` 无分配 |
| 触摸事件处理精度 | 未触碰输入链路（本任务只改这一行的**绘制方式**） |

---

## 任务二：文本输入框首次点击迟钝

### 1. 完整链路（逐跳核实，行号为修复前）

```
触摸 → SF InputDispatcher（本层 touchableRegion，主循环逐帧发布）
     → aosp_glue pollTouches（独立线程 4ms 轮询）→ Renderer::touch → InputRouter
     → 渲染线程 renderFrame：ButtonEdgeSequencer（DOWN 优先）→ ImGui::NewFrame
     → InputText 命中（imgui_widgets.cpp:4191/4209/4219 ⇒ **当帧即激活**）
     → s_WantsTextInput = IsItemActive()
     → Renderer::wantsTextInput() → 主循环 `wants`
     → ime.show() → ensureUp() → InputBridge::open()
     → [extractApk → startServer → spawnBridge → acceptWithToken]
     → Java Main: pm install → am start-service → AuroraImeService 建 1×1 窗 → TCP 连回
     → 去掉 FLAG_NOT_FOCUSABLE → requestFocus → showSoftInput（最多 60×32ms 重试）
     → WindowInsets → 回传 IME 状态 → 渲染避让
```

### 2. 真实原因（不是"Java 反射慢"）

**先说排除项**（用户点名不要未经调查就归因反射）：

| 怀疑 | 结论 | 证据 |
|---|---|---|
| JNI/反射慢 | **不成立**。ELF 的点击路径上**没有任何 JNI/反射** | `grep FindClass\|GetMethodID\|CallXxxMethod\|AttachCurrentThread` 在 `app/src/main/cpp` **0 命中**；唯一反射在桥接进程启动时一次（`Main.java`），不在 ELF、不在每次点击上 |
| ImGui"首次点击只获取焦点" | 不成立 | `imgui_widgets.cpp:4209/4219/4288` 当帧 `SetActiveID` ⇒ `IsItemActive()` 同帧为真 |
| 新手势层抢了点击 | 不成立 | `PanelGestures.h:42-47` 优先级是**组件 > 拖动条 > 滚动**，且手势层在内容**之后**跑、用本帧 `HoveredId` ⇒ 点输入框判为组件 |
| 每帧刷 touchableRegion 丢触摸 | 不成立 | AOSP `InputDispatcher` 只在**窗口被移除**时取消在途触摸，不看 region 变化 |
| 交付包默认 `--no-ime` | 已修（上一轮） | `tools/pack_elf_sh.ps1:34` 默认参数已不含它 |

**真正的根因（两条叠加）**：

**(a) 冷启动被塞进"第一次点击"的同步调用栈** —— 这是"等好几秒"的来源。

`InputBridge::open()` 的四步里，**只有第 4 步慢**：

| 步骤 | 耗时 |
|---|---|
| `extractApk`（写 APK 到 /data/adb） | 通常 < 100ms |
| `startServer`（建监听 socket） | < 1ms |
| `spawnBridge`（posix_spawn app_process） | 几 ms |
| **`acceptWithToken`（等 Java 侧 pm install + am start-service + TCP 连回 + READY）** | **1.5~2.5 s**，最坏 30s |

仓库内已有的实测点：`research/ime_result.md` **1475ms**、`research/ime_physical_run/README.md` **2449ms**、
本次虚拟屏实测 **1755ms**。旧代码把整条 `open()` 放在 `show()` 里同步执行 ⇒ 用户第一次点输入框就等这么久。

**(b) 失败会把"上升沿"永久吃掉** —— 这是"必须重复点击"的来源。

旧代码：

```cpp
if (wantIme && wants && !imeWanted) {
    imeWanted = true;          // ← 先闩锁
    ime.show(…);               // ← 失败是静默的（show 返回 void）
}
```

一次失败（桥接超时 / 系统 IME 尚未 served by IMM / 窗口焦点未就绪）之后，
`imeWanted` 已是 true，而输入框保持 active 时 `wants` 恒为 true ⇒ **`!imeWanted` 再也不成立**
⇒ 之后点多少次都不会再拉起，直到输入框失焦一次。用户"补点"还会触发 `hide()`
把正在进行的拉起取消掉，进一步放大观感。

### 3. 修复

| # | 修复 | 位置 |
|---|---|---|
| 1 | **启动期非阻塞预热**：把冷启动前三步 `beginOpen()` 放在主循环开始处，后半段 `openPoll()` 每帧推进（`poll(0)`/`pump(0)`，永不等）。全程**只建窗、不发 SHOW** ⇒ 启动时不会主动弹键盘 | `InputBridge.h`（新增 `beginOpen`/`openPoll`，删除同步 `acceptWithToken`）+ `aurora_overlay.cpp`（`ImeHost::warmUpBegin/warmUpPoll`，主循环调用） |
| 2 | **失败不闩锁 + 1 秒重试**：`ImeHost::show()` 改为返回 `bool`；只有**真的请求成功**才置 `imeWanted`；失败时记录 `imeRetryAtMs = now+1000`（既不每帧重试白等冷启动，也不无限重试） | `aurora_overlay.cpp` 主循环 wants 分支 |
| 3 | **点击时若预热仍在路上，就把剩下的同步走完**（而不是从头再 open 一次，避免重复写 APK/fork 第二个桥接进程） | `ImeHost::ensureUp()` 的 `if (warming_)` 分支 |
| 4 | **状态转换可观测**：`AURORA_IME_TRACE=1` 每帧打印 `wants/imeWanted/wantIme/up/warming` | `aurora_overlay.cpp` 主循环 |

### 4. 实测耗时（用户要求 5）

| 项 | 修复前 | 修复后 |
|---|---|---|
| Java 桥接冷启动（`pm install` + `start-service` + TCP 连回 + READY） | **1755ms**（本次实测；历史数据 1475 / 2449ms）**发生在用户第一次点击时** | **同一段 1755ms，但发生在启动阶段**（用户点击之前） |
| 首次点击 → 发出拉起键盘请求 | 同步等待整段冷启动 ⇒ 用户感知"点一下等 1.8 秒" | **0ms 额外等待**（`ensureUp()` 命中 `up_` 快路径） |
| 后续点击 | 正常（`up_==true`） | 正常（不变） |

**证据（`--verbose` stdout 日志，虚拟屏，`AURORA_IME_AUTOFOCUS=1` 程序化模拟"第一次点击"）**：

```
[overlay] 输入法桥接预热完成，用时 1755ms（首次点击不再承担冷启动）
[overlay] UI 请求文字输入 -> 拉起系统输入法（会话 1）     ← 首次聚焦：中间**没有**"桥接已就绪，用时…"
[overlay] 键盘可见性 = 1
[overlay] 键盘可见性 = 0
[overlay] 键盘已收起 → 复位请求状态并让输入框失焦（下次点击可以重新拉起）   ← 复位逻辑正确
```

**关键判据**：预热日志与"拉起"日志之间**没有**出现 `输入法桥接已就绪，用时 Xms`
——那一条只在 `ensureUp()` 真的跑了一整轮 `open()` 时才会打 ⇒ **首次点击确实走了快路径、零等待**。

### 5. 其他阶段要求的落实

| 阶段 | 落实 |
|---|---|
| 一（调查链路） | 逐跳核实并列表；确认 ELF 侧点击路径零 JNI；给出插桩建议清单（见下） |
| 二（窗口与焦点） | 核实我们的 SF 输入窗是 `visible+touchable+focusable`，**从未设** `FLAG_NOT_FOCUSABLE`/`ALT_FOCUSABLE_IM`；Java 侧辅助窗待命时 `FLAG_NOT_FOCUSABLE`、SHOW 时移除——这是设计正确，只是带来"首次前置条件未满足"的窗口（已由修复 2 兜住） |
| 三（优化初始化） | 预热放在"窗口已在屏幕上之后"的阶段；**非阻塞**（每帧一小步）；没有固定几秒延迟、没有无限重试；首次出现时点击处理器已注册；启动时**不弹键盘** |
| 四（即时反馈） | 输入框的聚焦视觉由 ImGui `InputText` 自身提供（当帧激活、当帧绘制光标/高亮），**不等待键盘**；`show()` 不阻塞渲染线程（渲染是独立线程），因此窗口动画不受影响 |
| 五（冷启动验收） | 见"仍需真机验证" |

---

## 修改的文件

| 文件 | 改了什么 |
|---|---|
| `app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp` | 触控数据行改为「定高 Dummy + `AddText` + `PushClipRect` + 分段固定列」；新增 `AURORA_TOUCH_SIM`（数值仿真）、`AURORA_TOUCH_LEGACY`（A/B 对照）、`AURORA_IME_AUTOFOCUS`（程序化模拟首次点击）三个诊断入口（默认全关） |
| `app/src/main/cpp/InputBridge.h` | `open()` 拆成 `beginOpen()`（快，前三步）+ `openPoll()`（非阻塞，返回 0/1/-1，30s 超时自管）；新增 `opening_/openStartMs_/monoMs()/finishOpenFail()`；删除同步版 `acceptWithToken()` |
| `root-host/elf/aurora_overlay.cpp` | `ImeHost` 新增 `warmUpBegin/warmUpPoll/warming()/trace()`；`show()` 返回 `bool`；`ensureUp()` 处理"预热在途"；主循环启动时 `warmUpBegin()`、每帧 `warmUpPoll()`；wants 分支改为"成功才闩锁 + 失败 1 秒后重试"；新增 `[IME-TRACE]` 状态日志 |

---

## 测试状态

**已通过（有实测数据）**
* 任务一：A/B 对照证明旧布局行高 80↔120（下方抖动 40px）、新布局恒定 40px 且行后光标 y 恒定 356.0。
* 任务二：预热 1755ms 发生在启动阶段；首次聚焦时 `ensureUp()` 走快路径（无"桥接已就绪"日志）。
* 宿主离线回归 `tests/run_host_tests.ps1` → **36 项全通过**（本轮未改动被测逻辑，回归用于防意外）。
* 虚拟屏运行 40 秒无崩溃、收尾正常。

**仍需真机验证（我会如实标注，不声称已完成）**
1. **冷启动首次点击的端到端耗时**（ELF 自身已证明为 0 额外等待；剩余时间是系统输入法进程冷启动，
   需要在物理屏上量"SHOW 发出 → 键盘可见"这一段，并用 `am force-stop <输入法包>` 制造真冷态）。
2. **"必须重复点击"是否彻底消失**：需要真手指在"重启 → 第一次点输入框 → 立刻关键盘 → 再点"
   这条路径上验证（修复 2 的结构性缺陷已改，但没有真机失败样本可复现）。
3. **中文/英文输入、删除操作**（沿用既有链路，本轮未改）。
4. **物理屏上的预热**是否也成功（虚拟屏上已成功，耗时 1755ms）。
5. 用户任务书列出的其它冷启动场景（二级窗口关闭重开、切应用返回、缩放后点击、快速连点）。
