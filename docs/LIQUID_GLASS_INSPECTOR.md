# Liquid Glass Inspector —— 液态玻璃全参数实时调试系统

> 主窗口「主题」页从"一组共用 static 变量的滑杆"升级成**由组件注册表驱动的调试工作台**。
> 本文是这一块的权威设计记录；参数清单见
> [`GLASS_INSPECTOR_PARAMETERS.md`](GLASS_INSPECTOR_PARAMETERS.md)（**从代码自动导出**，勿手改）。

---

## 1. 一句话架构

```
渲染实现里的真实字段  ←──指针绑定──  组件注册表  ──生成──  分页栏 + 参数控件
        ↑                                    │
        └────────── 组件自己改（手指操作）────┘  反向通道
```

三个不可动摇的性质：

| 性质 | 为什么它必须成立 | 靠什么保证 |
| --- | --- | --- |
| **显示值 == 渲染值** | 否则就是假控件 | 参数槽 `ref` 直接指向渲染字段，UI 读写的是同一块内存 |
| **组件之间不串味** | "调按钮 1 改了按钮 2"是用户会立刻发现的错 | 每个目标一份独立存储（每枚按钮/开关/滑块/芯片各有自己的 tune） |
| **UI 线程不阻塞** | 拖滑块不能等 GPU | 高频写入合并进队列，帧边界一次性提交 |

---

## 2. 分层与文件

| 层 | 文件 | 依赖 | 能否离线回归 |
| --- | --- | --- | --- |
| 参数模型 | `glass/GlassParam.h/.cpp` | 无 | ✅ |
| 注册表 / 参数总线 | `glass/GlassRegistry.h/.cpp` | 无 | ✅ |
| 极简 JSON | `glass/GlassJson.h/.cpp` | 无 | ✅ |
| 持久化 / 预设 / 迁移 | `glass/GlassPreset.h/.cpp` | 无 | ✅ |
| 撤销重做 | `glass/GlassHistory.h/.cpp` | 无 | ✅ |
| **场景参数（真实存储）** | `glass/GlassScene.h/.cpp` | ytbl 公开类型 | ✅ |
| **参数 → PanelStyle 配方** | `ytbl/GlassRecipes.h/.cpp` | ytbl | ✅ |
| **参数绑定表（清单唯一出处）** | `GlassTargets.h/.cpp` | 上面两层 | ✅ |
| 工作台界面 | `GlassWorkbench.h/.cpp` | ImGui | 真机 |
| 组件级可调配方 | `ytbl/GlassUI.h` 的 `*Tune` | 无 | ✅（随组件编译） |

★ 前六层**刻意不依赖 Android / ImGui / GL**，所以能在 PC 上跑断言。
这不是洁癖：调试工具本身出错时的表现（值不对、串味、存不下来）和渲染 bug 很难区分，
只有能离线断言才查得动。

---

## 3. 组件注册表

### 3.1 目标（Target）

```
secondary_window                     整窗口
secondary_window.card                液态玻璃卡片
secondary_window.pill_button_1..5    五枚按钮（各自一份参数）
secondary_window.toggle_1..3         三枚开关
  ├─ .track                          轨道（尺寸 + 颜色）
  └─ .thumb                          玻璃圆钮（折射/模糊/背景/光影/交互）
secondary_window.slider_1..2         两根进度条
  ├─ .track                          底轨 + 已填充段
  └─ .thumb                          玻璃滑杆
secondary_window.navigation          底部导航栏
  ├─ .background                     容器面板
  └─ .indicator                      选中指示器
secondary_window.chip_1..6           六枚胶囊芯片（形状行 3 + 边光行 3）
secondary_window.popmenu             弹出菜单
secondary_window.dialog              液态弹窗
secondary_window.playground          玻璃实验场
secondary_window.capability_gaps     能力缺口总表（只读）
```

当前统计（`tools/gen_glass_param_report.ps1` 导出）：
**35 个目标 / 1134 条参数 / 其中 1016 条可读写 / 118 条能力缺口 / 分页栏 22 项**。

### 3.2 动态发现与注销

- 目标在**第一次绘制时**注册（`RegisterSecondaryTargets()` 幂等，每帧调用安全）。
  新加一个液态玻璃按钮 ⇒ 只要写一段绑定，分页栏与控件**自动出现**。
- 注册表每帧记账 `lastSeenFrame`；`Sweep()` 回收连续多帧没有出现过的目标。
  组件销毁后不会在分页栏里留下幽灵页。
- 检索接口：`FindTarget / FindByType / FindByWindow / ChildrenOf / Search`
  （`Search` 同时匹配 ID、中英文名、类型与参数名）。

### 3.3 参数描述符

每条参数登记：`id / 中文名 / 英文原名 / 类型 / 分组 / 值域 / 步长 / 单位 / 说明 /
约束 / 实时性 / 性能成本 / 源码位置`。

三条登记纪律（写在 `GlassTargets.h` 顶部）：

1. **只登记真实参与绘制的量** —— shader 不读、绘制不用的东西不许登记。
2. **值域取自算法本身**（`GlassConfig.h` 的常量、shader 的读取范围、调用点的 clamp），
   不为了"看起来能拖"随便给区间。例：模糊半径上限是 `BLUR_RADIUS_MAX_DP = 10dp`。
3. **实现里没有的能力用 `Unsupported()` 登记成只读 + 原因**，绝不做一个转了没反应的控件。

### 3.4 默认值

默认值在**注册那一刻从渲染字段现场捕获**（`slot.def = slot.Read()`），
不在任何地方重写一遍。这样"重置"必然回到组件自己初始化的值 ——
写两遍默认值就一定会漂。

---

## 4. 双向实时绑定

### 4.1 主窗口 → 二级窗口（正向）

```
拖动滑块
  → Registry::Set（校验 + 夹紧 + 写入渲染字段）
  → 标记 dirty（同一参数只保留最新一次）
  → 帧边界 Commit()：把"请求值"拷进"生效值"（只对高成本参数）
  → 二级窗口本帧读到的就是新值
```

★ **提交点放在二级窗口绘制之前**（`secondary_imgui.cpp` 的 `Draw()` 里）：
主题窗口在同一帧里先画，所以用户这一帧拖的滑块**当帧**就反映到二级窗口，零帧延迟；
同时 UI 线程从头到尾没有等待 GPU。

### 4.2 二级窗口 → 主窗口（反向）

手指点开关、拖滑杆、点标签栏之后，`DrawContents()` 末尾把**组件自己改的状态**
写回注册表（`ChangeOrigin::Component`）：

```cpp
registry.SetB("secondary_window.toggle_1", "on", scene.toggles[0].on, Component);
registry.SetF("secondary_window.slider_1", "value", scene.sliders[0].value, Component);
registry.SetI("secondary_window.navigation", "activeIndex", scene.tabs.activeIndex, Component);
registry.SetI("secondary_window.popmenu", "cursorRow", scene.menu.cursorRow, Component);
```

`Registry::Set` 自带"值没变就直接返回"的短路，所以这段每帧执行是**零成本**的。

### 4.3 统一接口

| 语义 | 接口 |
| --- | --- |
| 列出全部目标 | `Registry::TargetCount / TargetAt / DisplayOrder` |
| 列出目标的全部参数 | `ParamCount / ParamAt` + `Target::groups` |
| 读取 | `Get(target, param)` |
| 修改 | `Set / SetF / SetI / SetB / SetColor`（带 `ChangeOrigin`） |
| 恢复单个参数 | `ResetParam` |
| 恢复单个组件 | `ResetTarget` / `ResetGroup` |
| 恢复全部 | `ResetAll` |
| 快照 | `Capture / CaptureTarget / CaptureGroup / CaptureDiffFromDefault` |
| 应用快照 | `Apply(snapshot, origin, &skipped, &adjusted)` |
| 订阅变化 | `Subscribe / Unsubscribe`（函数指针 + user，回调里可再写参数） |

这套接口不依赖任何 UI，**未来接 AI Agent 或外部调试工具时直接用**：
`--glass-params-*` 这类命令行入口或者一个薄薄的适配层就能驱动。

---

## 5. 滑块与渲染线程解耦

| 环节 | 谁负责 | 做什么 |
| --- | --- | --- |
| 触摸反馈 | UI 线程 | ImGui 自己画滑块，立即响应 |
| 校验与合并 | 参数管理层 | `Set` 夹紧值域；`MarkDirty` 把同一参数的多帧写入合并成**一条** |
| 应用 | 渲染线程 | 帧边界 `Commit()`，只应用最新值 |
| 最终提交 | — | 松手值一定进入队列（值没变时 Set 直接返回，不会"漏掉最后一次"） |

实测语义（`tests/GlassRegistryTest.cpp` 组 6）：
连续 120 帧写同一个参数 ⇒ `PendingCount() == 1`，`MergedUpdates() == 119`，
字段里始终是**最新值**（这一点很关键：字段本身是即时更新的，队列只管"高成本参数的生效时机"）。

### 5.1 性能成本分级

| 成本 | 含义 | 例子 |
| --- | --- | --- |
| `Uniform` | 只写进 `PanelStyle`，随后被拷成 shader uniform | 折射高度/强度、边光、内阴影、颜色 |
| `EffectChain` | 会改变效果链结构（多一个投影面板、圆角 SDF、采样区间） | 阴影半径、内阴影半径 |
| `Pipeline` | 会改变采样来源或 gather 目标 | 模糊半径、背景采样开关、整窗玻璃开关 |

`Pipeline` 参数用**双槽**表达：UI 改 `blurDp`（请求值），渲染读 `blurAppliedDp`（生效值），
`Commit()` 在帧边界拷贝。`tests/GlassBindingTest.cpp` 组 5 钉死了这个语义：
连写 60 次之后、`Commit()` 之前，渲染读到的**仍是上一帧的值**。

---

## 6. 持久化

### 6.1 格式（版本化 JSON，当前 v2）

```json
{
  "format": "aurora.glass.params",
  "version": 2,
  "active": "当前工作区",
  "presets": {
    "默认":      { "targets": { "secondary_window": { "blurDp": { "kind": "float", "f": 10 } } } },
    "我的配方":  { "targets": { ... } },
    "当前工作区": { "targets": { ... } }
  }
}
```

- **「默认」是隐式的**：它的值就是注册表里每个参数注册时捕获的 `def`，
  因此"恢复默认"和"把参数写回注册值"是**同一条代码**，不可能出现两套默认值；
  也不受"预设仓库什么时候被创建"影响（这是个真实的坑，见 §9）。
- **「当前工作区」**在导出瞬间快照一份并设为 `active` ⇒ 导入方按 `active` 应用一次
  就能 1:1 还原屏幕上的样子。
- 每条值带 `kind`，读回来按**注册表 descriptor 的类型**还原（JSON 里的 kind 只作提示），
  所以"手工改过类型的文件"也不会把渲染字段写坏。

### 6.2 版本迁移与改名

| 场景 | 处理 |
| --- | --- |
| v1（旧面板的"按组件分组平铺"） | `LegacyGroupTarget()` 把组名映射到目标 ID；`MigrateParamId()` 处理字段改名；颜色 `#RRGGBBAA` 解析成 RGBA；全程写进导入报告 |
| 参数改名 | 只维护**一张表**（`kRenames`）。老配置里对不上的字段不会静默丢失 —— 会在导入/应用时以"无此参数"报告出来 |
| 不合法数值 | `Sanitize()` 夹进值域并**单独报告**（`adjusted` 列表），与"被跳过"分开 |
| 配置缺失 | 目标未注册的条目在导入时保留、应用时跳过并报告（这样"先导入后注册"不会丢东西） |
| JSON 本身坏了 | 整体拒绝 + 带**行列号**的错误，当前参数一个字节都不动 |

### 6.3 原子保存

`SaveTextAtomic()`：写 `<path>.tmp` → `fflush` → **`fsync`** → `rename()` 覆盖。
同文件系统内 `rename` 是原子的 ⇒ 意外退出只会看到"旧的完整文件"或"新的完整文件"，
不会看到半份。写入不完整（磁盘满）会删掉临时文件并报错。

默认路径 `/data/local/tmp/aurora_glass_params.json`，可用 `AURORA_GLASS_PARAMS` 改。

### 6.4 自动保存与自动恢复

- **恢复**：工作台第一次初始化时读盘，并按文档里的 `active` 预设应用一次。
- **保存**：改动后 2 秒无新动作才落盘（去抖）；退出前 `FlushGlassWorkbench()` 再兜一次。
- 两个动作都会在界面上留一行状态（成功=路径，失败=原因），不静默。

### 6.5 撤销 / 重做

- **一个"步骤"是一组改动**：拖一次滑块 = 1 步（同一个参数在时间窗口内合并），
  "恢复默认" = 1 步（批处理）。
- 合并判据是"同一参数 + 时间窗口"，**不依赖 UI 显式 begin/end** —— UI 漏调不会毁掉历史。
- 回放走 `Registry::Set`，因此校验/夹紧/通知/帧边界提交全部照旧，撤销不会被绕过任何一层。
- 回放期间关掉记录（`replaying_`），撤销本身不会变成新记录。

---

## 7. 背景采样与图层关系

调试系统**不改变**任何采样路径，但把它变得**可见**（这是之前踩坑最多的地方：
玻璃忽略中间窗口 / 忽略开关的绿色轨道 / 忽略底层图标）。

诊断面板直接显示：

| 项 | 来源 |
| --- | --- |
| 背景采样来源 | `PlanBackdrop()` 的 `BackdropOrigin`（None / HostFramebuffer / SceneComposite …） |
| 采样纹理与映射尺寸 | `BackdropPlan.texture / mappingWidth / mappingHeight` |
| 宿主表面是否透明 | `BackendDescriptor.hostSurfaceIsTransparent`（透明⇒会退化为兜底色） |
| 裁剪边界 | 当前触摸/可见区域矩形 |
| 内容圆角遮罩半径 | ImGui 侧圆角裁剪的当前值（-1 = 关闭） |

★★ 两条**不允许**做的事（防背景采样递归/反馈）：

1. 调试系统自己**不新增任何背景采样源**，也不参与合成；
2. 面板的 z 序合成仍然只走既有的 `GlesSceneCompositor`，
   调试 UI 画在主题窗口里（与二级窗口同级），不会进入玻璃的采样内容。

---

## 8. 界面

- **分页栏**：标题由注册表动态生成（顶层 22 项）；左右翻页按钮 + 滚轮横向滚动 +
  切换时自动把选中项滚进可视区。**按钮宽度按文字真实宽度算**
  （本工程是中文字形 2 倍缩放，"上一页"约 96px，写死 44px 会把它切成一个字）。
- **子层**：有子层的组件在页面内出现一排子层按钮（整体 / 轨道 / 圆钮 …），
  子层是注册表里的独立目标，参数完全隔离。
- **参数行**：4 列表格（名称 / 控件 / 精确数值 / 单项重置）。
  - 名称：悬停显示中英文名、ID、类型、值域、默认值、成本、约束、说明、**源码位置**；
    偏离默认值的参数用黄色标出。
  - 控件：Float→滑块、Int→整数滑块、Bool→开关、Enum→下拉、Color→取色器。
  - 数值：点一下变成**直接输入框**。
  - 分组头显示"本组已改 N 项"，组内首行有"重置本组"。
- **能力缺口**：默认折叠，打开后逐条列出"参考项目有、本实现没有"以及**原因**。
- **全局操作**：撤销 / 重做 / 保存 / 重新载入 / 恢复默认 / 导出 / 导入 /
  预设下拉 + 另存为 / A-B 对比（存 A、存 B、切到 A、切到 B + 差异条数）。
- **检索**：关键词入口 + "只看结果"开关。
- **不溢出**：所有并排按钮都过"放不下就换行"的判据（ImGui 不会自动换行，
  超宽会被窗口右边缘直接切掉 —— 用户实测反馈过"好多按钮没完全显示"）。

### 8.1 触摸滚动（**必须自己接**）

ImGui 桌面版只有"滚轮"和"拖滚动条"两种滚动方式，**没有"拖动内容滚动"**。
触摸屏上没有滚轮、滚动条只有十几像素宽 ⇒ 面板 7000px 高的内容
（`最大滚动 = 5660`）**一大半参数根本够不着**。

`RunThemeWindowScroll()`（`LiquidGlassRuntime.cpp`）复用了二级窗口**同一个内核**
（`PanelGestures.h` 的阈值与惯性），所以两个窗口手感一致：

| 环节 | 规则 |
| --- | --- |
| 起手 | 只认**内容空白处**（`g.HoveredIdPreviousFrame == 0` 且 `ActiveId == 0`）⇒ 按在按钮/滑块上不滚 |
| 升级 | 位移 ≥ `PanelGestureThreshold(true)` = 18px 才从"点击"升级成滚动 |
| 跟手 | `SetScrollY(起手时滚动量 − 手指位移)` |
| 松手 | 带走一段惯性，撞到顶/底立刻清零（内核自带夹紧） |

### 8.2 窗口尺寸

主窗口（主题）与二级窗口都支持**右下角长按 150ms ⇒ 拖拽改尺寸**：

- 判定区：窗口右下角向内 44dp；
- 长按阈值 `LONG_PRESS_TRIGGER_MS = 150ms`（**唯一来源**，三反馈共用同一个状态转换）；
- 判定区里压着组件时**长按优先**：满阈值 ⇒ 缩放接管并在接管那一刻 `ClearActiveID()`；
  提前松手或移动超容差 ⇒ 整笔触摸还给组件；
- 手柄是**与右下圆角同心的一条粗圆弧**（`CornerResizeHandle.h`），
  画在前景层，不会被内容裁剪误伤。

★ 两个窗口用的是**同一个模块**（`CornerResizeOverlay.h/.cpp`），
手感不可能走偏；`ImGuiWindowFlags_NoResize` 继续保留，因为它关掉的是
**ImGui 自带的 resize grip**（那才是"滚动时被误判成缩放"的元凶），
与"能不能缩放"不是同一件事。

★ **判定区必须让开滚动条**（`window->ScrollbarSizes`）：ImGui 的滚动条是在
**`Begin()` 内部**处理的，按在滚动条空白处会**立即按比例跳转**（不是相对拖动），
晚一帧的 `ClearActiveID()` 根本来不及 —— 真机现象就是"长按改窗口大小，
内容顺带被滚走几百像素"。二级窗口是 `NoScrollbar`，所以它的行为一个像素都不变。

★ **每个窗口的尺寸参数住在 `ResizeGesture.h`**（`ResizeParamsForPanel()` /
`ResizeParamsForSecondaryWindow()`）。这不只是摆放问题：最小尺寸的单位是 **dp**，
窗口默认尺寸是 **px**，中间隔着密度（2.0）—— 曾经给主窗口写 `minWidthDp = 420`
心想"比 760 小"，实际是 **840px > 760px**，于是一长按就把窗口撑大 80px。
放在这个纯逻辑头里，`ResizeGestureTest` 就能直接断言
**「最小尺寸 × 密度 < 默认窗口尺寸」**，不用上真机才发现。

---

## 9. 踩过的坑（都写成了回归断言）

| # | 现象 | 根因 | 现在由谁守住 |
| --- | --- | --- | --- |
| 1 | 「恢复默认」按了没反应 | 默认预设是**构造时**快照的，而 PresetStore 在组件注册**之前**就构造了 ⇒ 快照是空的 | 「默认」改成隐式（值=注册表的 def）；`GlassPresetTest` 组 1 |
| 2 | 第 2 根进度条的默认值 50 变成 1 | 注册时把它的上限写成了 `1.0`（把"第 1 根是 0~1"的条件写成了 `index == 1`） | `GlassBindingTest` 组 7（重置后与默认值零差异） |
| 3 | 开关圆钮"静止模糊"与"按压模糊"写反 | 原式是 `lerp(FLOOR, GLASS, 1-p)`，按名字直译会颠倒两端 | `GlassRecipeTest` 组 1（golden 值）；配方改成 `lerp(rest, press, p)` |
| 4 | 内阴影半径差 2 倍 | `InsetShadow` 收 **dp**（内部乘密度），但 TabStrip 那处原实现**确实**套了 `px()` —— 两处不一样 | `GlassRecipeTest` 组 5/6 逐处钉死 |
| 5 | 撤销一次只回退了一半 | 一个"步骤"只能装一条改动，批处理里的其余条目被丢了 | `History::Step{changes[]}`；`GlassHistoryTest` 组 3 |
| 6 | 关掉合并（窗口=0）却仍在合并 | 同一微秒内两次写入的差值是 0，`0 <= 0` 成立 | `coalesceMs_ > 0` 才合并 |
| 7 | 参数面板里有些参数**根本不显示** | 第一个参数挂在一个还不存在的分组上 | `TargetBuilder::Push` 兜底建「常规」组 |
| 8 | 导入报告把"被夹紧"算成"被跳过" | 两件事混在一个列表里，用户会以为配置没生效 | `Apply(..., &skipped, &adjusted)` 分开 |
| 9 | 并排按钮被窗口右边缘切掉 | ImGui **不会**自动换行 | `SameLineOrWrap()` |
| 10 | 翻页按钮只剩一个字 | 按钮宽度写死 44px，而中文 2 倍缩放下"上一页"约 96px | 宽度按 `CalcTextSize` 算 |
| 11 | 空白处上滑，**整个窗口被拖走 800px**，内容一点没滚 | ImGui 默认 `ConfigWindowsMoveFromTitleBarOnly = false` ⇒ 点窗口任何空白处都能拖窗口 | `Renderer.cpp` 里置 `true` |
| 12 | 滚动的"起手判据"恒成立 | `NewFrame()` 把 `g.HoveredId` 清 0，它要等本帧控件处理过才被设上；这段代码跑在 `Begin()` 之后、画控件之前 ⇒ **必然为 0** | 用 `g.HoveredIdPreviousFrame` |
| 13 | 按住时滚得好好的，**一松手弹回顶部** | `PanelScrollInertia::Step()` 返回的是**这一帧的位移**（`clamped - scroll`），不是新位置；写成 `SetScrollY(Step(...))` 就等于每帧把滚动量设成那几像素 | `SetScrollY(scroll + moved)` |
| 14 | `SameLineOrWrap` 改了等于没改 | `Button` 之后游标已被挪到**下一行行首**，`GetContentRegionAvail()` 给的是整行宽度而不是本行剩余 | 判据改成"内容区右边界 − 上一个控件右边界 − 一个间距" |
| 15 | 长按右下角，**什么都不拖窗口也变大** | 最小宽度按 dp 写（420）× 密度 2.0 = 840px > 默认窗口 760px | 尺寸参数搬进 `ResizeGesture.h`，加断言「最小 × 密度 < 默认」 |
| 16 | 数值列被切一半、重置只剩一条边 | ① 重置列写死 36px（实际要 ~80px）② `WidthFixed` 的宽度**含单元格 padding**，我又给每列加了一遍 ⇒ 表格比可用宽度宽 4×2×CellPadding ③ `Button(label, w)` 的 w 只是**最小**宽度，文字长了会撑破列 | 列宽按真实文字量算 + 名称/数值列按**本目标实际最长文本**算 + 名称列封顶 34% + 数值用 `Selectable`（硬宽度） |

---

## 10. 测试

| 回归 | 覆盖 |
| --- | --- |
| [`tests/GlassRegistryTest.cpp`](../tests/GlassRegistryTest.cpp) | 动态注册/注销、目标隔离、值域夹紧、NaN、快照、订阅、合并队列、分页顺序（64 项断言 / 8 组） |
| [`tests/GlassPresetTest.cpp`](../tests/GlassPresetTest.cpp) | v2 round-trip、v1 迁移与改名、非法 JSON/数值、原子保存与读回、多预设（74 项 / 7 组） |
| [`tests/GlassHistoryTest.cpp`](../tests/GlassHistoryTest.cpp) | 拖动合并成一步、批处理、回放不产生新记录、历史上限（29 项 / 5 组） |
| [`tests/GlassRecipeTest.cpp`](../tests/GlassRecipeTest.cpp) | **参数 → PanelStyle**：默认值复现原常量、改一项只改对应字段、两实例互不影响、dp/px 逐处核对 |
| [`tests/GlassBindingTest.cpp`](../tests/GlassBindingTest.cpp) | **注册表 → 真实渲染字段**：写按钮 1 不动按钮 2、整窗口不覆盖子组件、帧边界生效、父子层隔离（67 项 / 8 组） |

另加 `tests/ResizeGestureTest.cpp` 的第 16 组 `TestLongPressWithoutDragKeepsSize`
（长按不动绝不改尺寸 / 最小尺寸必须小于默认窗口尺寸 / 拖动方向与夹紧）。

跑法：`pwsh -File tests\run_host_tests.ps1`（当前 **31 项全通过**）。

★ `GlassRecipeTest` / `GlassBindingTest` 会链接 ytbl 的组件实现，因此需要
`tests/host_glass_stubs.cpp` 提供 GL 侧的**空壳**（只满足链接，永不执行）——
详见该文件顶部。生产逻辑（`px()`/`saturate()`/主题色）一律来自真实源文件，
替身里一行都没有复制。

---

## 11. 能力缺口（参考项目 ≥ 本实现）

完整列表见参数清单文档的"能力缺口"行；以下是分类摘要：

| 缺口 | 参考项目 | 本实现 | 接入代价 |
| --- | --- | --- | --- |
| 多背景源合成 | `rememberCombinedBackdrop` | 单一采集源 + 进程内 Z 序合成 | 需要采集层支持多次采样并合并 |
| 任意图层变换 | `layerBlock`（scale/rotationZ/transformOrigin） | 轴对齐矩形 + `uStretchScale` | 需要改面板采样坐标模型 |
| 自定义 RuntimeShader | `runtimeShaderEffect`（API 33） | 预编译 GLES 程序 | 需要 shader 源运行时可注入 |
| 曝光 / 伽马 | 只有 1.0.6 有（2.0.1 已移除） | 没有 | shader 加一个 uniform + 一次 pow（约 3 行），**尚未接入** |
| 边光混合模式 | `HighlightStyle.blendMode` | 固定加法高光 | 需要 shader 增加混合分支 |
| 任意表面绘制回调 | `onDrawSurface` | `PanelStyle::surface` + 3 种模式 | — |
| 连续曲率圆角 | `com.kyant.shapes` | 标准圆角 SDF | 需要换 SDF 实现 |
| 模糊边缘模式 / 抽头数 | `blur(radius, edgeTreatment)` | 固定 Clamp / 12 抽头 | 抽头数是 GLES 循环上界（编译期） |
| 菜单 / 弹窗的内部配方 | 组件内可调 | `PopupMenu.cpp` / `Dialog.cpp` 的常量 | 需要给这两个组件加 Tune（本次未改动） |

**平台/实现限制（不是缺口，因此不提供控件）**：阴影 `spread` 两边都没有；
`blur > 10dp` 会被 `BLUR_RADIUS_MAX_DP` 截断；Ambient 边光的角度/衰减在 shader 里
根本不读（参考项目也硬编码 45°/1.0）。
