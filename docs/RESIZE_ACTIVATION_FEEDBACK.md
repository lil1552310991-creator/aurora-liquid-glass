# 缩放手柄 + 激活反馈（330ms / 边框加厚 / 震动）实现与验收

> 面向接手者：这一页是"做了什么、怎么验的、哪里会再踩坑"的权威记录。
> 代码入口：`app/src/main/cpp/CornerResizeHandle.h`、`ResizeGesture.h`、`Haptics.h`、
> `root-host/elf/HapticBinder.h`、`app/src/main/cpp/third_party/liquidglass/secondary_imgui.cpp`。

---

## 0. 一句话

长按窗口右下角 **330ms** → **同一帧**发生三件事：白色圆弧手柄展开、**整窗四边边框加厚提亮**、
**一次轻震**；继续拖动实时改宽高；松手后三者一起平滑恢复（实测**逐像素回到原状**）。

---

## 1. 需求 → 实现对照

| 用户要求 | 实现 | 判据/证据 |
| --- | --- | --- |
| 长按 450ms → **330ms**，统一常量 | `ResizeGesture.h` 的 `LONG_PRESS_TRIGGER_MS = 330.0f`（唯一来源，`ResizeHoldParams` 默认取它） | 真机日志 `真实按住 333ms / 334ms` |
| 必须真实时间，不能按帧 | 宿主用 `std::chrono::steady_clock` 传"自按下起算的绝对毫秒"；状态机只做 `heldMs - armBaseMs >= longPressMs` | `TestLongPressIndependentOfFrameRate`（4/8.3/16.7/50/100ms 五种帧长，激活时刻都落在 330ms±1 帧） |
| 保持移动容差、不误触 | 容差 14dp；判定区 44dp；**长按优先、其余让给组件** | 真机：快速点 / 快速上滑 / 判定区外长按 ⇒ 缩放日志 0 条、震动记录 0 条 |
| 四周边框同步加厚、保持圆角 | 原配方（gradient 边光 + 折射）增强 **＋ 叠加一层"均匀激活描边"**（同一套圆角 SDF） | 诊断模式四边剖面：上 0→3px、左 0→3px、下 3→8px、右 2→6px；峰值 +26→+63 / +98→+130 灰阶 |
| 非线性弹簧、不瞬间切换 | 一个阻尼弹簧（k=420, c=34, ζ≈0.83）同时驱动手柄与边框 | 录屏 60fps：手柄 230 帧起、边框 235 帧抬升、259 帧一起回落 |
| 不引起宽高跳变 / 内容重排 | 只改 `lgx::PanelStyle` 的视觉字段；窗口矩形、圆角、布局零改动 | 窗口内部（内缩 80px）平均绝对差 **0.249**（激活）/ **0.000**（恢复） |
| 松手平滑恢复 | 同一个弹簧反向 | 恢复帧与未激活帧 **逐像素最大差 = 0.0** |
| 参数可配置、不分散硬编码 | `CornerHandleParams`（手柄几何）、`ResizeActivationParams`（边框/光学/描边）、`HapticParams`（震动） | `CornerHandleGeometryTest` 断言"progress=0 时必须完全中性" |
| 只在激活瞬间震一次 | `HapticGate`：只在"未激活→激活"转换放行一次 | `HapticsTest` 5 组；真机 `resize_activate` 记录恰好 +1 |
| 等待/拖动/取消不震 | 同上（同一道门，无独立计时器） | 真机三种误触：震动记录 0 增加 |
| 不可用时安全降级 | 无 sink / 服务不可用 ⇒ `HapticTap()` 返回 false；Binder 事务 oneway 不阻塞 | `HapticsTest::TestNoSinkMeansUnavailableNotCrash`；启动日志 `触觉反馈：可用/不可用（原因）` |
| 三反馈同一状态驱动 | 唯一状态 `ResizeHold::Active()`；唯一进度 `ResizeActivation`（手柄与边框共用）；震动由 `HapticGate` 挂在同一转换 | 日志同一行：`长按 330ms 达成（真实按住 333ms）→ 圆弧展开 + 边框加厚 + 震动` |

---

## 2. 手柄造型（承接上一轮）

* 参考物是 iOS 控制中心磁贴右下角那段**白色粗圆弧**。像素实测（`research/handle_redesign/measure_arc.py`）：
  弧心线半径 ≈ 62px、线宽 ≈ 28–31px、跨度 ≈ 108°、圆端帽、与圆角同心。
* 本实现把它适配到窗口右下圆角：**圆心 = 圆角圆心**，半径 = 圆角半径 + 间距 + 半个线宽，
  跨度 96°、中心 45°；线宽 = `0.40 × 圆角半径`（夹在 8–16dp）。
* 整条弧（外弧 + 两个圆端帽 + 内弧）生成成**一个闭合多边形**，`AddConcavePolyFilled` 一次填充
  ⇒ 没有折线接头、没有端帽叠色、AA 一次算好。
* 展开/收起用**几何**表达（跨度 16%→100%、半径 92%→100%、线宽 55%→100%），
  alpha 只在最后 28% 收尾淡出 —— 是"沿轮廓长出来 / 缩回去"，不是淡入淡出。

**真机几何一致性**：把屏幕上的手柄与代码算出的轮廓逐像素对齐，**IoU = 0.985**，
包围盒完全相同（`research/handle_redesign/verify_geometry_iou.py`）。

---

## 3. 边框加厚：为什么需要"两层"

`lgx::EdgeLight::gradient()` 是 **45° 方向光**。诊断模式（`AURORA_NO_BACKDROP=1 AURORA_NO_SHADOW=1`）
下的四边剖面证明：**右边与下边有边光，上边与左边完全没有**（剖面恒等于底色 15.0）。
所以只调 gradient 的话，用户看到的是"只有右下两条边变厚"——与"四周边框同步加厚"不符。

做法（**不动原配方**）：

1. 原面板：`edge.widthDp += 1.5`、`edge.softnessDp += 0.30`、边光 tint alpha ×1.5、
   `lensHeight/lensAmount/spectral` 分别 ×1.30/×1.25/×1.35，并叠一圈极淡的内阴影；
2. **激活描边层**：同一个 `panelBox`、同一套圆角，只开 `edgeOn`（`EdgeLight::plain()`），
   宽度 = `rimWidthDp × progress`，整体 `alpha = rimAlpha × progress`；
   非激活时（alpha≤0.002）**整层不提交** ⇒ 常态零成本、零视觉影响。

两层都在同一个 `ScopedPanelClip` 里提交，圆角由面板自己的 SDF 保证 ⇒ 四个圆角连续，
不会出现直角、断裂、突兀白描边或矩形遮罩。

---

## 4. 震动：Binder 直连 vibrator_manager

**选型过程（都是真机实测，不是推断）**：

| 方案 | 结论 |
| --- | --- |
| `/sys/class/timed_output/vibrator/enable` | **本机不存在** |
| `cmd vibrator vibrate` | **本机没有这个命令**（`cmd: Can't find service: vibrator`） |
| `/sys/class/qcom-haptics/*` | 只有标定/诊断节点，没有"震一下"的触发口 |
| fork `app_process` 走 Java Vibrator API | 启动 200–400ms，震动会明显滞后 |
| **Binder → `vibrator_manager`（选中）** | oneway 事务，微秒级、不等回复，不阻塞渲染线程 |

* 接口：`android.os.IVibratorManagerService`，事务号 = AIDL 声明顺序（AOSP `android16-release`）：
  `1 getVibratorIds / 2 getCapabilities / … / 8 vibrate / 9 cancelVibrate / **10 performHapticFeedback**`。
* 选 **10 = `performHapticFeedback(uid, deviceId, opPkg, constant, reason, flags, privFlags)`**：
  常量由厂商调校、自动尊重用户触感设置，Parcel 也最简单（4 int + 2 string），
  不需要手搓版本敏感的 `VibrationEffect`/`VibrationAttributes`。
* **写回读校验**（`HapticBinder.h::Install`）：启动时先用**双向**事务 1（`getVibratorIds`）验证布局，
  期望 `异常码=0` 且数组长度 1..8、id 合法；不符就整体停用并写明原因，绝不硬发。
  真机实测事务 1 回 `00000000 00000001 00000000`（异常 0 / 长度 1 / id 0），事务 99 回
  `Not a data message` ⇒ 错号**可检测**。
* 权限：uid 0（root）通过 VIBRATE 检查；非 root 不会崩，只是不可用。
* 常量：默认 **23 = `GESTURE_THRESHOLD_ACTIVATE`** —— 语义是"动作刚越过激活阈值"，
  本机映射为 `Prebaked=TICK(STRONG)`（64ms，比 CONTEXT_CLICK 的轻 tick 重）。
  环境变量可覆盖：`AURORA_HAPTIC_CONSTANT=25`（DRAG_START → HEAVY_CLICK 更重）/ `-1`（关闭）。
* 取证方式：`dumpsys vibrator_manager | grep resize_activate` —— 历史里能看到
  `reason: performHapticFeedback(constant=23): resize_activate | played: Prebaked=TICK(STRONG)`。

---

## 5. 验收证据索引（`research/handle_redesign/`）

| 文件 | 内容 |
| --- | --- |
| `measure_arc.py` / `mask_camera.png` | 参考图圆弧的像素实测（骨架化 + 拟合圆） |
| `verify_geometry_iou.py` / `iou_armed.png` | 屏幕手柄 vs 设计几何 **IoU 0.985** |
| `anim_timing.py` / `retract_montage.png` | 120fps 抽帧的展开/收起曲线与中间态帧数 |
| `render_filmstrip.py` / `anim_filmstrip.png` | progress 0→1 的形状胶片（离线） |
| `verify_border_ab.ps1` / `measure_border_ab.py` | 诊断模式四边剖面（加厚 + 逐像素恢复） |
| `verify_round3.ps1` | 330ms/震动/误触/夹紧（自适应坐标） |
| `verify_round4.ps1` / `sync_check.py` | 长按计时日志 + 录屏逐帧的同步曲线 |
| `r4_active_window.png` / `r4_corner_ab.png` | 激活态整窗与右下角 A/B 视觉 |

宿主回归：`pwsh -File tests\run_host_tests.ps1` → **26/26 通过**
（新增 `CornerHandleGeometryTest` 17 组、`HapticsTest` 5 组，`ResizeGestureTest` 15 组）。

---

## 6. 踩过的坑（接手必读）

1. **测试脚本必须先 `pkill -x aurora_overlay`**：旧实例还在跑时新进程建图层会失败，
   而屏幕上留着旧窗口 ⇒ 截图拍到"两个会话的混合体"，数据全废（踩过一次）。
2. **写死坐标一定打偏**：窗口会被前一次手势/用户移动。测试脚本改成先探一次，
   从覆盖层自己的 `[手势] 按下 … 窗口=(l,t)-(r,b)` 日志里读实时矩形。
3. **`logcat` 缓冲会冲掉关键日志**：开 `--dump`/`AURORA_RESIZE_TRACE` 时，
   "长按达成"那条会被冲走（取证失败一次）。要取证就单独跑一轮、只留状态转换日志。
4. **`CornerHandleAnimation` 首次 `Update` 不能 Reset 目标**：否则"先 SetTarget 再 Update"
   的第一帧会把目标冲回 0（离线回归抓到：那一帧 `Settled()` 竟为真）。
5. **`adb pull` 到中文路径下的 `.mp4` 会失败**（`cannot create file/directory`）：
   先 pull 到 `%TEMP%` 再复制（png 没这个问题，原因未深究）。
6. **玻璃边光是加法高光**：压在亮背景上会被饱和吃掉，量边框必须用暗背景
   （或 `AURORA_NO_BACKDROP=1` 的诊断模式）。
7. **`EdgeLight::gradient()` 是方向光**，上/左边本来就没有边光 —— 别去"修"它
   （AGENTS 早先也记过这条），要四周均匀就叠一层 `plain()` 描边。

---

## 7. 可调旋钮（都在一处，不分散）

| 想要的效果 | 改哪里 |
| --- | --- |
| 长按快慢 | `ResizeGesture.h` 的 `LONG_PRESS_TRIGGER_MS` |
| 手柄大小/粗细/跨度 | `CornerHandleParams`（比例 + dp 上下限） |
| 手柄展开/收起手感 | `CornerHandleAnimation::stiffness/damping` |
| 边框加厚幅度/亮度/折射 | `ResizeActivationParams`（`edgeWidthAddDp` / `edgeTintAlphaGain` / `lens*Gain` / `rimWidthDp` / `rimAlpha`） |
| 震动轻重 | `HapticParams::constant`（真机 A/B：`AURORA_HAPTIC_CONSTANT=23/25/0/12/1/6`） |
| 判定区大小/位移容差 | `ResizeHoldParams::zoneDp / moveToleranceDp` |
