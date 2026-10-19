# M4：纯 ELF 拉起系统输入法（IME 桥接）

> ★★ **2026-10-14 更新（先读这一段）**：本轮定位并修掉了"输入法最近失效"的三个真根因，
> 权威记录见 [`docs/UI_LAYOUT_AND_IME_FIX.md`](UI_LAYOUT_AND_IME_FIX.md) §二。摘要：
>
> 1. **交付的单文件包默认带 `--no-ime`**（`tools/pack_elf_sh.ps1` 的 `$DefaultArgs`），
>    而 `wantIme=false` 时主循环两个分支都不成立 ⇒ **一条日志都不打**，
>    从外面看与"输入法坏了"一模一样。现已去掉，并在被吃掉时打 WARN。
> 2. **键盘收起后再也拉不起来**：`onHidden` 不复位 `imeWanted`，而 `s_WantsTextInput`
>    是"只在画到输入框那一帧赋值"⇒ 不失焦就恒 true ⇒ 上升沿永不再来。
>    现在：HIDDEN 事件 → 复位 + `Renderer::requestTextInputEnd()`（只做一次，不轮询不强拉）
>    + `secondary_imgui::BeginTextInputFrame()` 每帧清零。
> 3. **键盘被我们的玻璃层压住、按键点不动**（我们的图层是 SF `mRoot` 的兄弟，
>    z 序在整棵 WMS 子树之上）：Java 侧现在把 `WindowInsets.Type.ime()` 的**真实矩形**
>    用协议里**原本预留的 3 个 0 字段**回传 ——
>    `IME <session> <visible> <left> <top> <right> <bottom>`（旧宿主只读前两段，向后兼容）；
>    宿主据此把键盘那块**从触摸拦截区挖掉**，并用 `ImeAvoid.h` 做 adjustPan 式的窗口上移。
>
> 另外修掉一个回填语义错误：Java 回传的是**整段文本**，而 `Renderer::textInput()` 是
> **追加**语义 ⇒ 以前敲 `a` 再敲 `b` 会变成 `aab`。现在宿主侧换算成
> "公共前缀 + 按 UTF-8 字符数退格 + 增量"。
>
> 回归：`tests/ImeAvoidTest.cpp`（6 组）。真机取证：`logcat -s aurora_overlay` 里的
> `UI 请求文字输入 → 拉起系统输入法` / `键盘已收起 → 复位…` / `输入法弹层矩形 = (l,t)-(r,b)`。

> 实现：`ime-bridge/`（Java 辅助包）+ `app/src/main/cpp/InputBridge.h`（C++ 侧）
>
> 参考：ssyclr/AndroidSurfaceImgui-Enhanced-Input-method
> （MIT，署名见 `docs/THIRD_PARTY_NOTICES.md`）。
> 用户提供的那份参考实现细节见 `build/recon/ref_ime/INPUT_METHOD.md`。

## 一、为什么纯 ELF 不能直接拉起输入法

输入法不是「打开一个开关」，而是**系统把键盘显示给某个获得焦点的窗口**。
请求它的唯一途径是 `InputMethodManager.showSoftInput(View, ...)`，而 `View`
必须属于一个真实的 Android 窗口。纯 ELF 进程没有 Activity、没有 WindowManager
里的窗口、更没有 `InputConnection`。

所以必须**借一个 Android 身份**。本方案借的是一个内嵌的小辅助 APK。

## 二、整体链路

```text
C++ (InputBridge.h)
  ├─ 1. 释放内嵌 APK 到 /data/adb/aurora-ime-XXXXXX/  (目录 0700 / APK 0400)
  ├─ 2. 监听 127.0.0.1 随机端口，生成随机令牌
  ├─ 3. root app_process 加载 APK 里的 Main 类
  │      app_process -Djava.class.path=<apk> /system/bin \
  │                   com.aurora.imebridge.Main \
  │                   --port <p> --token <t> --apk <apk>
  │
  └─ Java 侧
       ├─ 4. Main 把 APK 安装成临时应用（PackageInstaller）
       ├─ 5. Main 启动 AuroraImeService
       └─ 6. Service 创建 **透明 1×1 悬浮窗**，按需请求焦点并拉起键盘
              ↕ 127.0.0.1 TCP（令牌校验）↔ C++
```

## 三、为什么是 1×1 而不是全屏

全屏透明窗会挡住下方应用的所有触摸 —— 那就变成了另一个「触摸盾」，
与 `InputShield` 的职责重复且更难控制。

1×1 几乎不占地方，触摸归属完全交给 `InputShield` 判断：
面板内的触摸被吞掉（因此点不到下方应用），键盘区域的触摸交给 Android。

窗口的两种状态：

| 状态 | 标志 | 效果 |
| --- | --- | --- |
| 待命 | `FLAG_NOT_FOCUSABLE \| FLAG_NOT_TOUCHABLE \| FLAG_NOT_TOUCH_MODAL \| FLAG_LAYOUT_IN_SCREEN` | 焦点归前台应用，不抢输入 |
| 输入中 | 移除 `FLAG_NOT_FOCUSABLE` + `requestFocus()` | 窗口成为输入法焦点宿主，`showSoftInput` 生效 |

另外 `softInputMode` 设为 `SOFT_INPUT_ADJUST_NOTHING`，
避免软键盘把 1×1 窗顶来顶去。

## 四、协议

文本一律 Base64 编码（避免空格与换行破坏行协议）。

| 方向 | 消息 | 含义 |
| --- | --- | --- |
| C++ → Java | `HELLO <token>` | 首行令牌校验，不通过即断开 |
| Java → C++ | `READY` | 校验通过 |
| C++ → Java | `SYNC <sess> <start> <end> <flags> <cap> <b64text>` | 同步文本与选区 |
| C++ → Java | `SHOW <sess>` / `HIDE` | 显示 / 收起键盘 |
| Java → C++ | `EDIT <sess> <start> <end> <b64text>` | 完整编辑快照 |
| Java → C++ | `IME <sess> <visible> <top> <w> <h>` | 键盘可见性与几何 |
| Java → C++ | `HIDDEN <sess>` | 用户主动收起 |
| C++ → Java | `GETCLIP` / `SETCLIP <b64>` | 剪贴板 |
| Java → C++ | `CLIP <sess> <b64>` | 剪贴板内容 |
| C++ → Java | `QUIT` | 退出并清理 |

**回传的是完整编辑快照，不是模拟按键**。这样中间插入、选区替换、
删除都能正确反映 —— 模拟按键做不到这些。

## 五、构建

工具链：JDK 17（`javac`/`keytool`/`jar`）+ Android SDK build-tools
（`aapt2`/`d8`/`zipalign`/`apksigner`）+ `android.jar`。
**全部离线可用，不需要 gradle。**

```powershell
cd ime-bridge
pwsh -File build-ime-apk.ps1        # -> build/aurora-ime.apk
pwsh -File build-embedded-apk.ps1   # -> build/AuroraImeApk.h
```

实测产物：

```text
aurora-ime.apk     20,888 字节（已签名，CN=Android Debug）
AuroraImeApk.h    112,942 字节（20,888 字节的字节数组 + 大小常量）
classes.dex        22,660 字节（5 个类，lambda 已被 d8 脱糖）
```

C++ 侧编译验证：把 `ime-bridge/build` 加入 include 路径即可，
`InputBridge.h` 会包含那份头文件。

## 六、踩到的坑（都已修）

### 6.1 `-bootclasspath android.jar` 会让 lambda 编译失败

报错 `找不到符号 metafactory` —— `android.jar` 里没有
`java.lang.invoke.LambdaMetafactory`。改用 `-classpath android.jar`，
lambda 由 d8 脱糖（实测生成 `AuroraImeService$$ExternalSyntheticLambda0..11`）。

### 6.2 `ctx.getPackageName()` 在 app_process 里是 `"android"`

`app_process` 起的进程用的系统 Context，包名不是辅助包。
用它去 `ComponentName` 会启动不到 Service。**必须硬编码包名**
（`Main.AUX_PACKAGE`），manifest 里的 `package` 也要一致。

### 6.3 TextWatcher 的自我回环

`applySync` 里 `setText`/`append` 会触发 `TextWatcher`，
如果不屏蔽就会把自己的同步动作当成「用户编辑」回传给 C++，形成回环。
修法是 `ignoreNextChange` 标志 + 30ms 合并节流
（中文输入法一次上屏会触发多次回调）。

### 6.4 UTF-8 与 UTF-16 偏移不一致

C++ 用 UTF-8 字节偏移，Android 用 UTF-16 偏移。**不做转换，中文会在
中间编辑时错位**（一个汉字 UTF-8 是 3 字节、UTF-16 是 1 个 char；
emoji 是 4 字节 / 2 个 char 的代理对）。
`utf8ToUtf16` / `utf16ToUtf8` 两个函数负责换算。

### 6.5 内嵌头文件不能自带命名空间

`AuroraImeApk.h` 被 `InputBridge.h` 在 `namespace aurora` 内部包含。
如果它自己也开 `namespace aurora`，就会形成 `aurora::aurora` 嵌套。
生成脚本已去掉外层命名空间。

### 6.6 每次运行必须用新目录

APK 一旦被 ART 映射，覆盖它会出问题。所以
`/data/adb/aurora-ime-<6字节随机>/`，退出时删除该文件与目录。

## 七、状态与未完成

> ### ★★ 更正（后加）：本节原来列的 6 条「未验证」**已经在真机上全部验证过了**
>
> 本节曾写着「未验证（必须有设备）」共 6 条，以及「未做退出时卸载辅助包」。
> **那些表述已经过时** —— 设备到手后这些全部跑通了。权威证据：
> `research/ime_result.md` §十一（全链路）+ **`research/ime_physical_run/README.md`**
> （像素级证据包：百度输入法完整键盘截图、逐张 sha256 双侧比对、
> 同一时刻的 `mCurClient` 与截图逐帧时间对齐）。
>
> | 原来的「未验证」条目 | 实际结果 |
> |---|---|
> | 1. `app_process` 能否加载 APK 里的 `Main` | ✅ 通了 |
> | 2. `PackageInstaller` root 安装 | ✅ `pm install => Success` |
> | 3. 1×1 窗取得焦点并真的弹出键盘 | ✅ 通了，**有像素证据**（不是只看状态位）|
> | 4. 悬浮窗 AppOp 是否需额外授予 | ✅ 已处理 |
> | 5. 文本往返（尤其中文） | ⏳ **仍未验证**（见下）|
> | 6. 退出清理是否彻底 | ✅ 通了：**退出自动卸载辅助包，零残留** |
>
> 关于「未做退出时卸载辅助包」：**已经不成立** ——
> `ime-bridge/src/com/aurora/imebridge/Main.java:158-159` 在退出时执行
> `pm uninstall <AUX_PACKAGE>`，真机核对过零残留。
> 原来提到的 `Main` 只 `Thread.sleep(1500)` 等安装完成 —— 该写法在当前源码里已不存在。
>
> **⚠️ 读 §十一 之前请先读 `research/ime_result.md` §十二** ——
> §十一 的**结论**成立，但它的**论证**被一次独立证据审计推翻了
> （最关键的一条：`isVisible` 那个判据抓的是**上一个窗口**的字段，从未真正验证过）。
> 那条方法论教训值得记住：**状态位与像素之间隔着一次 SurfaceFlinger 合成**，
> 「屏幕上应该看得见」的断言必须有像素证据。

**已完成并验证（离线）**：

- Java 辅助包编译、d8 脱糖、aapt2 链接、zipalign、apksigner 签名 —— 全部跑通
- APK 内容核对（`classes.dex` 里有 `Main` / `AuroraImeService` / 内部类）
- 内嵌头文件生成，C++ 侧编译通过

**★ 已完成并验证（真机）**：见上方更正块与 `research/ime_result.md` §十一。

**仍未验证（如实记录）**：

1. **文本往返**：中文上屏、选区、UTF-8↔UTF-16 偏移换算
   （`M4_IME_BRIDGE.md` §6.4 记了这个坑，但真实正确性没测过）
2. **`InputShield` 与输入法联动** —— 键盘区域的触摸应交给 Android，当前未分流
3. 冷启动多次统计（只测过一次，约 1.335 s）
4. 「同一实验内 HIDE 之后」的前后对照截图（`ime_result.md` §12.4 第 2 条）
5. 像素级归属指纹：本次的动作键是「确定/↵」（与 `imeOptions=0x12000006` 相符），
   但对照组里 `uid=1000` 的一帧**也是「确定」** ⇒ 动作键**不足以单独作为指纹**

**已知设计缺口**：

- 未与 `InputShield` 联动（同上第 2 条）
- ~~未接入 `aurora_demo`~~ → **已接入**：`aurora_overlay`（纯 ELF 交付形态）
  内含 `ImeHost`，并已真机验证 `ime=1` + 冷启动 1335 ms

## 八、真机验证步骤（待设备可用）

```bash
adb push ime-bridge/build/aurora-ime.apk /data/local/tmp/
adb shell su -c "ls -l /data/adb/ | head"          # 确认真机上的 /data/adb 可写

# 跑一个只验证「释放 + 引导 + 装包」的最小程序
adb shell su -c "/data/local/tmp/ime_probe"        # 待实现
adb logcat -s AuroraIme                            # 看 Java 侧日志
adb shell pm path com.aurora.imebridge             # 应能查到已安装

# 清理
adb shell su -c "pm uninstall com.aurora.imebridge"
adb shell su -c "rm -rf /data/adb/aurora-ime-*"
```

**务必确认 `/data/adb` 下没有残留的 `aurora-ime-*` 目录**，
以及辅助包已卸载。
