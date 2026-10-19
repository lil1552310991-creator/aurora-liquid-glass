# ELF 标准化交付：单文件自解包 SH

> 项目附加交付规范：**ELF → 单文件 SH → ADB 推送 → 真机验收**。
> 这一层与渲染/输入核心开发**解耦**：不改 CMake 目标、不加第三方依赖、
> 开发调试阶段仍可直接部署 ELF，只有正式交付才生成 SH。

---

## 1. 三条命令（日常就这三步）

```powershell
# 1) 编译（与以前完全一样）
cmake --build build\elf-arm64 --target aurora_overlay

# 2) 封装成单个 .sh（内嵌 ELF，可放手机任意可读目录，sh 文件名.sh 启动）
pwsh tools\pack_elf_sh.ps1 -Elf build\elf-arm64\bin\aurora_overlay `
                           -Out dist\aurora_overlay.sh

# 3) ADB 推送 + 真机验收（11 项，自动出报告）
pwsh tools\deploy_elf_sh.ps1 -Sh dist\aurora_overlay.sh
```

可选：封装前先做一次**本机离线校验**（不需要设备，秒级）：

```powershell
python tools\verify_elf_sh.py    # 校验无 BOM/无 CR、载荷能解出逐字节相同的 ELF
```

产物：`dist/aurora_overlay.sh`（本轮 5448195 bytes；内嵌 ELF 10617384 bytes）。
当前采集默认 `mirror`，采集/渲染目标为 120 Hz；最新验证与哈希见
[`GLASS_LATENCY_POWER_OPTIMIZATION.md`](GLASS_LATENCY_POWER_OPTIMIZATION.md)。

---

## 2. SH 的运行时行为契约

| 要求 | 实现 |
| --- | --- |
| 内嵌完整 ELF、不依赖原始文件 | gzip + base64 载荷，追加在固定标记行之后 |
| 可放任意可读目录、`sh 文件名.sh` 启动 | `$0` 先解析成绝对路径（在任何 `cd` 之前），载荷从自身读 |
| 自动解包到"当前身份允许的可执行目录" | 候选顺序：`$AURORA_ELF_TMPDIR` → `/data/local/tmp` → `$TMPDIR` → `/data/local`；**实际 mkdir 一次**再决定（`-w` 只看权限位不够，见 §5） |
| 自动校验完整性 | 原始字节数（必做）+ sha256（有 `sha256sum` 时）；两者都写在 SH 头部 |
| 赋予执行权限并启动 | `chmod 700` 后执行 |
| 原样传递参数与退出码 | 直接 `"$BIN" "$@"`，`exit $status` |
| 保持标准输入输出 | 全程不重定向子进程的 fd 0/1/2 |
| 运行结束自动清理 | `trap cleanup EXIT` + `INT/TERM/HUP` 各自 `exit 128+n` 触发 EXIT trap |
| 异常中断的处理 | 被 SIGKILL（trap 跑不到）留下的 `.<名字>.<pid>` 目录，**下次启动按 PID 存活情况回收** |
| 清理失败的处理 | 打印警告 + 具体路径，**不改变程序本身的退出码** |
| 需要 Root 时显式走 su | `id -u` 非 0 时用 `su -c`，逐参数做 POSIX 单引号转义保证 argv 原样；无 `su` 时明确报错退 77 |
| 不关 SELinux / 不绕安全机制 | 全程不碰 `setenforce`、不写 `inhibited`、不改任何系统属性 |

### 退出码

| 码 | 含义 |
| --- | --- |
| 程序自身退出码 | 原样透传（这是唯一正常的成功/失败来源） |
| 65 | 载荷损坏：找不到标记 / 解码失败 / 字节数或 sha256 不符 |
| 66 | 找不到自身文件（例如用管道喂给 sh） |
| 73 | 没有可用解包目录（候选全部 mkdir 失败），或 chmod 失败 |
| 74 | 无法读取内嵌数据 |
| 77 | 需要 root 但设备上没有 `su` |
| 69 | 载荷是 gzip 但设备上没有 `gzip` |

### 环境变量（都不是必需的）

| 变量 | 作用 |
| --- | --- |
| `AURORA_ARGS` | 这一次要用的参数（优先级高于内置默认值） |
| `AURORA_NO_DEFAULT_ARGS=1` | 不注入内置默认参数（等价于必须显式传参） |
| `AURORA_ELF_TMPDIR` | 指定解包目录（仍会检查可写 + 实际可建） |
| `AURORA_ELF_NO_SU=1` | 即使不是 root 也直接执行（调用方已自行提权时用） |
| `AURORA_ELF_VERIFY=1` | 只解包 + 校验 + 清理，不运行 ELF（交付验收用） |
| `AURORA_ELF_KEEP=1` | 运行结束后保留解包目录（排障用） |
| `AURORA_CRASH_LOG` | 崩溃取证落盘路径（默认 `/data/local/tmp/aurora_overlay.crash.log`） |

---

## 2.4 崩溃取证（**给"设备不在手边"准备的**）

**背景（真实事故）**：用户把单文件 SH 发给朋友，朋友的另一台设备 上日志停在
`[overlay] 图层参数: …` 之后就是 `Segmentation fault`，**下一行都打不出来**。
设备不在手边、没有 gdb、没法复现 —— 只能靠猜，一轮往返就是一天。

所以 ELF 现在自己带崩溃取证：

- 捕获 `SIGSEGV / SIGBUS / SIGABRT / SIGILL / SIGFPE / SIGTRAP`
  （`SA_ONSTACK` + `sigaltstack`，栈溢出时处理器自己也有栈）；
- 落盘内容：信号 + `si_code` + 出错地址 + **最后一条日志** + `PC/LR/SP`
  + 每个地址**落在哪个 so**（含模块内偏移）+ 栈上像返回地址的字（最多 8 条）；
- 写两处：**stderr**（终端里直接看得到）与 `AURORA_CRASH_LOG`
  （默认 `/data/local/tmp/aurora_overlay.crash.log`）；
- 写完**恢复默认动作再重抛** ⇒ 退出码、core dump 行为与不加处理器时**完全一致**（仍是 139）。

SH 侧：ELF 退出码 ≥128 时，打印崩溃日志路径 + 关键 6 行（完整报告 ELF 已经打过一遍了）。

```
==== aurora_overlay 崩溃取证 ====
信号      = SIGSEGV(11) si_code=1 地址未映射
出错地址  = 0x0  不在任何映射里（野指针/栈溢出常见）
最后日志  = [overlay] [Startup] surface.create attempt=1 api=checked   ← 走到哪一步了
出错 PC   = 0x5fea668e88  在 aurora_overlay 内 +0x224e88 (可执行)
调用 LR   = 0x5fea668e7c  在 aurora_overlay 内 +0x224e7c (可执行)
栈指针 SP = 0x7fe1654430（栈区间已知）
栈上像返回地址的字（最多 8 条）:
  +0x7a8 -> 0x784755bb08  在 libc.so +0x27b08
  +0xa08 -> 0x73178bb000  在 [vdso] +0x0
崩溃日志已写入 /data/local/tmp/aurora_overlay.crash.log
```

**★ 这条链路必须自己先被证明有效**：`--crash-test` 故意写空指针，真机跑一遍看报告
（已验证：退出码 139 + 完整报告，连 `[vdso]` 都能认出来）。

**★★ 两个只有真机才暴露的坑**（都踩过，别再踩）：

1. `/proc/self/maps` 在真机上是 **560~665 KB**（system_server 实测 665564 字节）。
   **绝不能"整读进固定缓冲再解析"**：64KB 缓冲被截断 ⇒ 找不到栈区间 ⇒
   栈扫描按 `sp+32KB` 这种常数越界读 ⇒ **信号处理器自己再崩一次** ⇒
   报告**后半段全丢**（退出码照样 139，极难发现少了东西）。
   现在是**流式单遍扫描**：只保留①已知地址的归属 ②`SP` 所在区间 ③可执行映射表；
   栈扫描上界**只**由 maps 给的区间决定，拿不到就跳过并写明"不用常数猜边界"。
2. 解析器必须**完全覆盖输出**：缺列时残留上一次的 `offset`（离线单测抓到 0x3000），
   报告里会把模块内偏移指错 —— **比没定位到更糟**。
   纯解析逻辑在 `root-host/elf/CrashReport.h`（零 Android 依赖），
   回归 `tests/CrashReportTest.cpp`（6 组，maps 样本取自真机）。

---

## 2.5 不带参数运行（MT 管理器 / 文件管理器里点一下）

`aurora_overlay` 自己的默认值是「虚拟显示 + 采集/渲染 120fps + **永不退出**」——
对"点一下看看窗口"这件事完全没用。所以 SH 顶部有一行**可编辑的默认参数**，
不带位置参数时注入：

```sh
DEFAULT_ARGS='--display 0 --yes-physical --max-seconds 300 --capture-fps 120 --render-fps 120 --no-ime'
```

| 参数 | 含义 |
| --- | --- |
| `--display 0` | 目标显示 = 物理屏。不给的话会落到虚拟显示（看不见） |
| `--yes-physical` | 允许在物理屏运行。**不给会被直接拒绝并返回 3**（项目纪律：物理屏要显式同意） |
| `--max-seconds 300` | **300 秒后自动退出**。`0` = 永不退出（ELF 默认值，别在手机上这么干） |
| `--capture-fps 120` | 背景采集目标与镜像显示请求；实际源更新服从系统调度。可显式选 30/60 |
| `--render-fps 120` | 界面目标帧率；视觉输入相同会跳过重复 GPU 提交，输入/动画检查继续 |
| `--no-ime` | 不拉起系统输入法（那一项会临时装一个辅助 APK，点一下看看时没必要） |

**改默认行为**：在 MT 管理器里用文本编辑器打开 `aurora_overlay.sh`，
改那一行即可，**不需要重新打包**（改完 sha 会变，但校验的是 ELF 载荷，不影响）。

**这一次临时换参数**（不改文件）：`AURORA_ARGS="--max-seconds 30" sh aurora_overlay.sh`

**看用法**：`sh aurora_overlay.sh --help`（本脚本自带，不会误启动覆盖层）。

**运行记录**：每次运行都会写 `/data/local/tmp/aurora_overlay.run.log`
（`time / uid / argv0 / argc / args / exit`）。MT 那种只给一个终端视图的执行器
出问题时，读这个文件就能对账 —— **"参数到底传进去没有"只有它说了算**。

### ★ 默认参数的判据是"有没有以 `-` 开头的参数"，不是 `$# = 0`

真机踩出来的：**MT 管理器点脚本执行时会额外塞位置参数**，于是 `$#` 不为 0、
却一个选项都没有。旧的 `$# = 0` 判据就不注入默认值 ⇒ ELF 退回它自己的默认
（**虚拟显示 + 永不退出**）⇒ 现象就是「点了脚本，主屏上什么都没有，像是跑到副屏了」。
判据改成"argv 里有没有 `-` 开头的选项"就耐操了；多塞的位置参数被 ELF 忽略掉。

### 在 MT 里必须注意的两件事

1. **需要 root**：解包目录 `/data/local/tmp` 与执行 ELF 都需要 root。
   MT 里请用「以 root 身份运行」。若当前身份建不出解包目录，脚本会**自动整体提到
   root 重跑一遍**（并把自己的开关一起带过去，避免"只校验不运行"在提权后失效）；
   没有 `su` 才会以退出码 73 明确报错。
2. **触摸**：窗口并集区域（约 x 16–776、y 16–2130）会被覆盖层接管，
   外面的区域照常穿透。所以默认给了 `--max-seconds 300`（5 分钟）兜底 ——
   想更长就改 `DEFAULT_ARGS`。

---

## 3. ADB 通道

```powershell
# USB 插上即可；没插线时走网络调试（手机需 root，且本机 adb key 需已在手机授权列表里）
#   手机:  setprop service.adb.tcp.port 5555; setprop ctl.restart adbd
#   PC  :  adb connect <手机IP>:5555
#   恢复:  setprop service.adb.tcp.port -1; setprop ctl.restart adbd
```

`deploy_elf_sh.ps1` 会检测在线设备，没有就自动 `adb connect <PhoneIp>:5555`；
仍失败才中止并打印通道建立方法。重启 adbd 不会影响手机侧 `虚拟显示服务` / `虚拟显示服务`
（两者 PPID 均为 1，init 托管 —— 已核实）。

---

## 4. 验收项

`pwsh tools\deploy_elf_sh.ps1 -Sh dist\aurora_overlay.sh` 自动跑完并打印 PASS/FAIL：

| # | 验收内容 | 判据 |
| --- | --- | --- |
| T0 | 文件成功推送 | 回读设备上的字节数与本地一致 |
| T1 | 解包出的 ELF 完整无损 | 字节数 + sha256 与 SH 头部声明一致 |
| T2 | 参数传递 + 退出码 | `--display 0` → 退 3 且打印拒绝横幅 |
| T3 | 参数**取值**穿透 | `--capture-backend nonsense` → 退 2 |
| T4 | 与 SH 所在目录/CWD 无关 | `/sdcard` 与 `/data/local/tmp` 各跑一次、`cd /` 与 `cd /data` 各一次，结果一致；直执行与 `su` 两条路一致 |
| T5 | 临时文件清理 | 运行前后 `/data/local/tmp/.<名字>.*` 计数均为 0 |
| T6a | 异常能报原因 | 载荷截断 → 退 65 且有可读原因（不是崩溃/静默） |
| T6b | 目录回退链 | `AURORA_ELF_TMPDIR=/proc` → 跳过并落到 `/data/local/tmp` 成功 |
| T9 | 不带参数 → 注入内置默认参数 | MT 里点一下即可运行；用 `AURORA_ELF_VERIFY=1` 安全验证 |
| T9b | `AURORA_ARGS` 覆盖默认参数 | 生效且优先于内置默认值 |
| T9c | **执行器多塞位置参数时默认参数仍生效** | 复现 MT 场景（`sh x.sh <多余参数>`）—— 这条是真机踩出来的回归 |
| T9d | 运行记录可读 | `/data/local/tmp/<名字>.run.log` 有 uid/argv/exit |
| T10 | `--help` 打印用法并退 0 | 且**不会**误启动覆盖层 |
| T7 | ELF 真机正常运行 | 虚拟显示实跑 15s，退 0，日志有 `采集帧=`（证明动态库/运行资源可访问） |
| T8 | 收尾 | 无残留进程/临时文件 |

**验收用例按程序实际支持的参数设计**，不假设存在 `--help`
（`aurora_overlay` 本身没有 `--help`）：`--display 0` 与 `--capture-backend`
是这个 ELF 里**确定性最好**的两条失败路径 —— 它们在做任何实事之前就返回，
所以既能验证参数/退出码透传，又不会接管屏幕。

**最近一次结果**：`通过 16 项，失败 0 项`（测试机 / Android 16；T7 因本机没有虚拟屏
按设计**跳过**，另做过物理屏 12 秒实跑：567 采集帧、退出码 0）。

---

## 5. 踩过的坑（都是真机验收才暴露的）

| # | 现象 | 根因 | 修法 |
| --- | --- | --- | --- |
| 1 | **本机离线校验全绿，真机解包失败退 65** | PowerShell `StringBuilder.AppendLine()` 用的是 `Environment.NewLine`，在 Windows 上是 **CRLF**；Android 的 toybox `base64 -d` 遇 `\r` **静默截断**，而 Python 的 `b64decode` 会忽略它 | 载荷显式用 `\n`；`verify_elf_sh.py` 增加「载荷区 CR 计数必须为 0」断言 |
| 2 | **`adb push` 报 "1 file pushed"，设备上却没有文件** | 这是一次实测踩到的坑：`adb push <Windows 绝对路径> <目录>/` 会把文件名**截断成 9 字符** —— `aurora_overlay.sh` 变成了 `aurora_ov`（正好 9 字符）。文件其实建出来了，只是名字不对 | push 目标**永远写显式文件名**；并且推送后**回读字节数校验**，不信 adb 的 "pushed" |
| 3 | 一度误判为「FUSE 对大文件写入失败」 | 4 MB 阈值实验全用了 `t4096.bin` 这种 ≤9 字符的名字，所以"看起来"是大小问题 | 已推翻：与大小无关，是文件名截断 |
| 4 | `AURORA_ELF_TMPDIR=/proc` 没被跳过，退 73 且原因文不对题 | `[ -w /proc ]` 在 root 下为真，但那里根本建不出目录 | `choose_base` 改成**实际 `mkdir` 一次**再决定 |
| 5 | `deploy_elf_sh.ps1` 输出 `System.Collections.Hashtable` | PowerShell 函数名解析**大小写不敏感**：脚本里 `function Adb {...}` 之后，`& adb devices` 调的是自己的函数，不是 `adb.exe` | 先 `Get-Command adb -CommandType Application` 拿到完整路径，函数改名 `Invoke-AdbShell` |
| 6 | T1 假失败 | 拿 **SH 的** sha256 去比对 **ELF** 的 sha256 | 期望值从 SH 头部 `ELF_SHA256='…'` 读出来 |
| 7 | 真机「一个可写目录都没有」构造不出来 | `/data/local/tmp` 恒可用 | T6b 改为验证**回退链**（跳过坏候选 → 落到好候选），比强行造失败更贴近真实风险 |
| 8 | 朋友的手机上一跑就 `Segmentation fault`，**连"崩在哪一步"都拿不到** | 崩溃发生在启动段的私有 ABI 调用里，日志还没走到下一行；设备又不在手边 | 见 §2.4 崩溃取证。★ 顺带纠正一个认知：**"修好了"不等于"他能跑起来"** —— 修复进了源码但没重新打包，朋友手里还是旧产物 |
| 9 | 崩溃报告**后半段凭空消失**（退出码照样 139） | 栈扫描按 `sp+32KB` 常数越界读到未映射页 ⇒ 处理器自己再崩一次 ⇒ 直接重抛默认动作 | 扫描上界只由 `/proc/self/maps` 给出的区间决定；maps 必须**流式**读（真机 560~665 KB，固定缓冲会被截断） |
| 10 | 崩溃报告里的模块内偏移指错 | 解析器对缺失的列**残留上一次的值**（单测抓到 `offset=0x3000`） | `ParseMapLine` 入口先 `*out = MapHit{}` 完全覆盖；单测用金丝雀字节守越界 |
| 11 | 修完上面两条后，**换台机器仍然 SIGSEGV**（停在 `[Startup] input.attach begin`，`si_addr=0x4`，PC/LR 都在 `libutils.so`） | **WindowInfo 字段偏移写死成开发机实测值**：① `DecodeWindowInfoRegionOffset` 从设备指令里解出 Region 偏移后 `if (decoded != 248) return false;` —— 解出来了却不用，换台机器（Region 仍在 AOSP 的 184）就整条拒绝 ⇒ 区域更新失败并走进失败清理路径；② token/packageName/inputConfig 等偏移也是写死的，而 `validateLayout()` 只校验了 vptr、`+56`、`+92` ⇒ 布局一变就是**往错的地方写**（`std::string::assign` 写进非字符串内存），把某个 `sp<>` 写坏 ⇒ 之后谁做引用计数就是 `mRefs == 0` 的原子加崩溃 | 见 §2.5：**偏移取设备实测值 + 其余偏移跟着同一位移 + 写回读校验 + 解不出布局就干净拒绝**。★ 与坑 8 同类：**开发机上永远测不出来** |

### 2.5 WindowInfo 布局：偏移必须现场推导（坑 11 的修法）

`gui::WindowInfo` 里 Region **之前**的字段是 AOSP 标准前缀（运行时用
`dispatchingTimeout@56` / `globalScaleFactor@92` 两个锚点校验，对不上直接拒绝），
**Region 及其之后**的字段会因 OEM 插入而整体移动 —— 开发机（测试机）就比 AOSP
多 64 字节的 "OEM Transform"（AOSP Region 在 184，开发机在 248）。

1. `DecodeWindowInfoRegionOffset()` 只校验**形状与合理性**
   （`BTI`+`ADD x0,x0,#imm`+`B`、8 字节对齐、落在 `[104,896]`），**偏移取实测值**。
2. `WindowInfoLayoutShift()` / `ShiftWindowInfoOffset()`：位移 = 实测 Region 偏移 −
   **本文件常量的参照 248**（★ 不是 AOSP 的 184 —— 常量已含开发机那 64 字节，
   按 184 算会**重复位移一次**：inputConfig 392→456）。
   所有字段读写统一过 `eff(off)`，前缀字段不动。
3. `verifyShiftedFields()` 在 create() 里对位移后的字段做**写回读**；
   布局解不出来时 **create() 直接拒绝**（干净退出 + 原因），绝不按开发机偏移硬写。
4. 失败路径**先记录原因再清理**：`failOpenTouchInput()` 第一行就 `startupTrace`，
   区域拒绝时打 `regionReplaceInfo()`，create() 打实测偏移与位移。
   朋友的机器上正是"崩在清理里、原因全丢"。

**验收**：宿主 `OverlayTouchRegionTest` 9 组（含「Region 在 AOSP 184 的设备必须被接受」
这条换机回归）；真机 `layout: Region 偏移=248（AOSP 184）位移=+0` 且自检全过、
拦截链路 746 触点、无崩溃；SH 真机验收 16/16。

### 2.7 内置录屏开关 + 界面字符（字体图集）

**录屏**（用户要求：随时开关 + 看得见时间；**不要常驻浮标**）：

| 入口 | 用法 |
| --- | --- |
| 主题窗口 | 第一行下面是 `[开始录屏]` / `[停止录屏]`，旁边实时 `mm:ss` + 文件大小 + 路径 |
| 命令行 | `--record [路径]`（不给路径就带时间戳存 `/sdcard/Download/`）、`--record-seconds N` |

- 实现是 **fork/exec 系统的 `screenrecord`**（ELF 本来就是 root），不自己写编码器：
  手机自带录屏走 MediaProjection/镜像**录不到我们**（我们的图层是 display root 的兄弟），
  而 `screenrecord` 走 SF 合成路径，拍得到（真机抽帧验证）。
- ★★ **停止必须走 SIGINT**（`screenrecord` 收到才写完 moov）；收尾路径里第一件事就是停它，
  否则留下打不开的 mp4。★ 不录声音。★ 文件名带 UTC 时间戳，永不覆盖。
- ★ **别用 `pidof screenrecord` 判断有没有在录** —— 那是我们自己 fork 的子进程，
  按名字查不到。判据用日志（`录屏已开始/已停止`）或抽帧。

**界面问号**（用户问："不是生僻字为什么显示问号"）：

- 根因**不是**字体缺字，而是**图集没申请**：`GlesBackend` 原来只传
  `GetGlyphRangesChineseSimplifiedCommon()`（常用 2500 汉字 + 拉丁），
  `● ★ → × …` 这些字形**字体里有**但没申请 ⇒ 画成占位符。
- 修法：`tools/gen_ui_glyphs.py` 扫**字符串字面量**（先剥注释）生成 `UiGlyphSet.h`，
  图集范围 = 默认 + 常用汉字 + 常用符号区 + 该表；`--check` 进了宿主回归（`UiGlyphCheck`）。
  字体里确实没有的字符（`✓ ✗ ⇒`）换掉（`√ × →`）。
- 代价：图集 **2048x4096 = 32 MB 上传**。★ 图集懒构建，量尺寸要显式 `io.Fonts->Build()`。

---

### 2.6 默认静默（成功时输出 0 字节）

用户要求「点了就直接跑，不要日志和输出参数」，所以**正常运行一个字都不打**：

| 场景 | 输出 |
| --- | --- |
| 正常运行（含 MT 里点一下） | **stdout+stderr = 0 字节**（真机实测 `RC=0 BYTES=0`） |
| 要日志 | `--verbose` / `-v`、`AURORA_VERBOSE=1`；任何诊断模式自动打开 |
| 出错退出 | 一行 `启动失败（退出码 N），原因见 …/aurora_overlay.error.log` + 文件头 4 行 |
| 崩溃（≥128） | 崩溃取证文件 + 关键 6 行（**不能省**，远端机器唯一线索） |

- `logf` 静默 stdout/stderr 但 **logcat 照写**；`startupTrace` 同步静默，**hook 照旧记录**
  ⇒ 崩溃取证一个字都不少。
- `reportFailure()` 只在失败时写 `aurora_overlay.error.log`（成功不写）；
  **SH 每次运行前先删掉它**，免得把上一次的原因当成这一次的。
- **SH 交付件里没有任何注释**：注释只留在 `pack_elf_sh.ps1` 模板里，打包时按行剥掉整行注释
  （`#!` 除外）并折叠空行。★ 只剥整行、不做正则去尾（字符串里的 `#` 是数据）；
  ★ 载荷是之后按字节追加的，不经过这一步。
- 验收新增 **T11**：不带参数真跑一次，断言 `RC=0` 且 `BYTES=0`（**必须真跑**，
  看代码看不出有没有人往 stdout 写）。T9/T9b/T9c 与 T7 改为显式 `--verbose` 才能断言日志。
  真机验收 **17/17**。

---

## 6. 文件清单

| 文件 | 作用 |
| --- | --- |
| `tools/pack_elf_sh.ps1` | ELF → 单文件 SH（封装逻辑的唯一来源，SH 模板就在里面） |
| `tools/verify_elf_sh.py` | **不需要设备**的离线校验：无 BOM/无 CR、载荷解出逐字节相同的 ELF |
| `tools/deploy_elf_sh.ps1` | ADB 推送（含回读校验与两段式回退）+ 16 项真机验收 |
| `dist/aurora_overlay.sh` | 交付产物（构建生成，非源码） |
| `root-host/elf/CrashReport.h` | 崩溃取证的纯逻辑（maps 解析 / 手写格式化），进离线回归 |
| `tests/CrashReportTest.cpp` | 上面那份的离线回归（6 组，maps 样本取自真机） |
