# 用户 Demo（c++液态玻璃展示源码）界面结构清单 —— 纯只读分析

> 分析对象：`C:\Users\<you>\Downloads\c++液态玻璃展示源码\液态玻璃\app\src\main\`
> 分析方式：read / grep / glob，**未修改任何被分析文件**。
> 所有结论均带 `文件:行号` 与源码原文；不确定处标注「未确认」。
> 下文行号若不带路径，默认相对 `jni/` 目录（即 `...\app\src\main\jni\`）。

---

## 0. 分析范围与文件事实

`jni/` 下文件（含字节数）：

| 路径 | 字节 | 作用 |
| --- | --- | --- |
| `main.cpp` | 13513 | JNI 入口 + EGL + 渲染主循环 + 主窗口 UI |
| `secondary_imgui.cpp` | 40667 | **二级液态玻璃窗口全部内容** + CanvasBackend 实现 |
| `secondary_imgui.h` | 292 | 9 个导出函数 |
| `Android.mk` | 713 | ndk-build 脚本 |
| `Application.mk` | 70 | `APP_ABI := arm64-v8a` |
| `imgui/字体.h` | 5933555（文本形式） | 内嵌 TTF，**实际字体数据 1911448 字节** |
| `imgui/backends/imgui_impl_android.cpp` | 16216 | 平台后端（显示尺寸 + 输入，输入部分本 Demo 未用） |
| `imgui/backends/imgui_impl_opengl3.cpp` | 48401 | GL3 渲染后端 |
| `imgui/backends/imgui_Input_android.cpp/.h` | 499 / 290 | **本 Demo 实际使用的输入注入** |
| `ytbl/*` | 20 个源文件 + LICENSE/NOTICE/README/使用说明 | 液态玻璃组件库 |
| Java：`FloatWindow.java` / `MainActivity.java` / `ImGuiTextureView.java` / `NativeLib.java` | — | 悬浮窗 + Surface 投递 |

ImGui 版本：`imgui/imgui.h:30` → `#define IMGUI_VERSION       "1.90.7"`。

`Android.mk:17-21` 用通配符收集全部源码（`jni/*.c*`、`imgui/*.c*`、`imgui/backends/*.c*`、`ytbl/*.c*`），
`Android.mk:23-24`：

```make
LOCAL_LDFLAGS := -landroid -lEGL -lGLESv3
LOCAL_LDFLAGS += -Wl,--strip-debug
```

---

# A. 界面长什么样

## A.0 三层结构总览

```
[1] Java Activity（普通 Activity，非全屏）
      └ 两个系统 Button：启动悬浮窗 / 彻底关闭悬浮窗
[2] Java 悬浮窗（TYPE_APPLICATION_OVERLAY，浮在系统桌面上）
      ├ renderView : ImGuiTextureView(TextureView)  MATCH_PARENT，FLAG_NOT_TOUCHABLE
      └ touchView  : 空白 View                    1x1 → 按需膨胀到 ImGui 窗口矩形
[3] native GL 线程（持 Surface → EGL → GLES3，120fps 自节流）
      ├ 主 ImGui 窗口「主题 / Theme」860x600（普通 ImGui 窗口，非玻璃）
      │     └ ImGui TabBar「##tabs」→ 两页：主题 / 液态玻璃
      └ 二级 ImGui 窗口「二级液态玻璃 / Secondary Liquid Glass」（整窗玻璃面板）
            └ 一列可滚动的组件演示内容（见 A.4）
```

## A.1 Java 层可见界面

`res/layout/activity_main.xml:1-21`（全文）：

```xml
<LinearLayout ... android:orientation="vertical"
    android:gravity="center"
    android:padding="24dp">
    <Button android:id="@+id/btnStart" android:layout_width="match_parent"
        android:layout_height="wrap_content" android:text="@string/start"/>
    <Button android:id="@+id/btnClose" android:layout_width="match_parent"
        android:layout_height="wrap_content" android:text="@string/close"/>
</LinearLayout>
```

文案 `res/values/strings.xml:3-8`：

```xml
<string name="app_name">C++液态玻璃</string>
<string name="start">启动悬浮窗</string>
<string name="close">彻底关闭悬浮窗</string>
<string name="started">悬浮窗已启动</string>
<string name="closed">悬浮窗已彻底关闭</string>
<string name="no_permission">未授予悬浮窗权限, 请允许"显示在其他应用上层"后重试</string>
```

主题：`res/values/styles.xml:3` → `parent="android:Theme.Material.Light.NoActionBar"`。
权限：`AndroidManifest.xml:5` → `android.permission.SYSTEM_ALERT_WINDOW`。

悬浮窗参数（`FloatWindow.java:37-53`）：

```java
renderParams.format = PixelFormat.TRANSPARENT;
renderParams.width  = WindowManager.LayoutParams.MATCH_PARENT;
renderParams.height = WindowManager.LayoutParams.MATCH_PARENT;
renderParams.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE;
...
touchParams.width  = 1;   touchParams.height = 1;
touchParams.x = 100;      touchParams.y = 100;
touchParams.flags |= WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL;
```

公共 flag（`FloatWindow.java:95-108`）：`TYPE_APPLICATION_OVERLAY`（O 及以上）/`TYPE_SYSTEM_ALERT`、
`TOP|LEFT`、`FLAG_NOT_FOCUSABLE | FLAG_HARDWARE_ACCELERATED | FLAG_LAYOUT_NO_LIMITS |
FLAG_LAYOUT_IN_SCREEN`、`LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`。
`ImGuiTextureView.java:14` → `setOpaque(false)`（TextureView 透明底）。

## A.2 主窗口「主题 / Theme」（普通 ImGui 窗口，非玻璃）

`main.cpp:153-193` 原文（含硬编码文案）：

```cpp
static void DrawMenuContent() {
    ImGui::SetNextWindowSize(ImVec2(860, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin(T("主题", "Theme"));
    const ImVec2 menuPos = ImGui::GetWindowPos();
    const ImVec2 menuSize = ImGui::GetWindowSize();
    g_MenuRect[0].store(menuPos.x, std::memory_order_relaxed);
    ... // 把窗口矩形发布给 Java 触摸窗（见 B/A.6）
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem(T("主题", "Theme"))) {
            bool lightTheme = g_LightTheme;
            if (ImGui::Checkbox(T("白色主题(浅色)", "Light theme"), &lightTheme)) {
                g_LightTheme = lightTheme;
                ApplyHostTheme(lightTheme);
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(T("液态玻璃", "Glass"))) {
            const ImVec2 anchorPos = ImGui::GetWindowPos();
            const ImVec2 anchorSize = ImGui::GetWindowSize();
            secondary_imgui::SetAnchorRect(anchorPos.x, anchorPos.y, anchorSize.x, anchorSize.y);

            bool secondaryVisible = secondary_imgui::IsVisible();
            if (ImGui::Checkbox(T("二级液态玻璃窗口", "Secondary liquid glass window"),
                                &secondaryVisible)) {
                secondary_imgui::SetVisible(secondaryVisible);
            }
            if (secondary_imgui::IsVisible()) {
                secondary_imgui::DrawHostControls();
            } else {
                ImGui::TextDisabled("%s", T("勾选后打开二级液态玻璃窗口",
                                            "Enable to open the secondary window"));
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}
```

要点：
- 有**标签页**：`ImGui::BeginTabBar("##tabs")`，两页 —— 「主题」与「液态玻璃」。
- 「液态玻璃」页把**主窗口自身的矩形**作为二级窗口的 anchor 传给 ytbl 用户代码（`main.cpp:175`）。
- 只有勾选「二级液态玻璃窗口」后，二级窗口才出现；同一页顺带显示 `DrawHostControls()`（三个折叠段）。
- 该窗口是**纯 ImGui 窗口**（有标题栏、可拖动/缩放），**不是**玻璃面板。

主题色（`main.cpp:136-151`）：

```cpp
static bool g_LightTheme = false;
static void ApplyHostTheme(bool light) {
    if (light) ImGui::StyleColorsLight();
    else       ImGui::StyleColorsDark();
    lgx::GlassColors colors;
    if (light) { colors.primary = lgx::Rgba(0.98f, 0.98f, 0.99f, 1.0f);
                 colors.secondary = lgx::Rgba(0.90f, 0.92f, 0.96f, 1.0f); }
    else       { colors.primary = lgx::Rgba(0.07f, 0.08f, 0.10f, 1.0f);
                 colors.secondary = lgx::Rgba(0.11f, 0.12f, 0.16f, 1.0f); }
    lgx::setGlassColors(colors, true);
}
```
（与 `使用说明.md:107-118` 的推荐值完全一致。）
初始 `g_LightTheme = false`（`main.cpp:136`）→ 启动即深色，`ApplyHostTheme(g_LightTheme)` 在 `RenderLoop` 里调一次（`main.cpp:241`）。

## A.3 二级玻璃窗口（`secondary_imgui.cpp:737-827`）

几何（原文 `secondary_imgui.cpp:742-765`）：

```cpp
const float screenW = fmaxf(1.0f, io.DisplaySize.x);
const float screenH = fmaxf(1.0f, io.DisplaySize.y);
float defW = fminf(760.0f, screenW - 32.0f);
float defH = fminf(700.0f, screenH - 96.0f);
float defX = 16.0f;
float defY = 16.0f;
{   // 若宿主给了 anchor（主窗口矩形），窗口贴到 anchor 内
    if (s_AnchorValid) {
        const float margin = lgx::px(8.0f);
        defW = fminf(defW, s_Anchor[2] - margin * 2.0f);
        defH = fminf(defH, s_Anchor[3] - margin * 2.0f);
        defX = s_Anchor[0] + margin;
        defY = s_Anchor[1] + margin;
    }
}
defW = fmaxf(240.0f, defW);
defH = fmaxf(180.0f, defH);
defX = fmaxf(0.0f, fminf(defX, screenW - defW));
defY = fmaxf(0.0f, fminf(defY, screenH - defH));
```
→ 默认就是 `(16,16)` 起步、最大 760x700、最小 240x180；因为 anchor 是主窗口内部区域，实际
等效「贴在主窗口里」。用 `ImGuiCond_FirstUseEver`（`:764-765`），之后由用户拖动决定。

窗口本体（原文 `secondary_imgui.cpp:767-780`）：

```cpp
bool open = s_Visible.load(std::memory_order_relaxed);
const ImGuiWindowFlags windowFlags = s_WindowGlassEnabled
    ? ImGuiWindowFlags_NoBackground : 0;
if (s_WindowGlassEnabled) {
    const ImU32 kTitleStrip = lgx::glassIsLight()
        ? IM_COL32(244, 246, 250, 170) : IM_COL32(26, 30, 37, 150);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTitleStrip);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTitleStrip);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTitleStrip);
}
const bool begun = ImGui::Begin(Text("二级液态玻璃", "Secondary Liquid Glass"), &open,
                                windowFlags);
```
⇒ 有标准 ImGui 标题栏与**关闭按钮**（`&open`），标题背景被改成半透明条。
标题栏文案：中文 `二级液态玻璃` / 英文 `Secondary Liquid Glass`。

整窗玻璃参数（原文 `secondary_imgui.cpp:800-823`）：

```cpp
if (s_WindowGlassEnabled) {
  lgx::PanelStyle windowStyle;
  windowStyle.corners = lgx::Corners::rounded(lgx::px(28.0f));          // 圆角 28dp
  windowStyle.optics.colorBoost = true;
  windowStyle.optics.blurPx = lgx::px(s_WindowBlurDp);                  // 10dp
  windowStyle.optics.lensHeightPx = lgx::px(s_WindowLensHeightDp);      // 18dp
  windowStyle.optics.lensAmountPx = lgx::px(s_WindowLensAmountDp);      // 30dp
  windowStyle.optics.depthAmount = s_WindowDepth ? 1.0f : 0.0f;         // true
  windowStyle.optics.spectral = s_WindowChromatic ? s_WindowDispersion : 0.0f; // 0.70
  const float windowOpacity = s_WindowOpacityEnabled ? s_WindowOpacity : 0.0f; // 默认 false → 0
  windowStyle.surface = lgx::glassIsLight()
      ? lgx::Rgba(0.96f, 0.97f, 0.99f, windowOpacity)
      : lgx::Rgba(0.10f, 0.11f, 0.14f, windowOpacity);
  windowStyle.surfaceMode = lgx::SurfaceMode::Overlay;
  const ImVec4& pageBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
  windowStyle.fallback = lgx::Rgba(pageBg.x, pageBg.y, pageBg.z, 1.0f);
  windowStyle.edgeOn = true;
  windowStyle.edge = lgx::EdgeLight::gradient();
  windowStyle.shadowOn = true;
  windowStyle.shadow = lgx::DropShadow(
      24.0f, (static_cast<unsigned int>(255.0f * 0.14f + 0.5f) << 24));  // 24dp, α≈0.14 黑
  windowStyle.backdropOn = true;
  lgx::drawPanel(windowStyle, lgx::Box(pos.x, pos.y, max.x, max.y));
}
```
⇒ **整窗是一块液态玻璃面板**，内容用透明窗口画在它上面。

窗口矩形会被发布到 `s_TouchRect`（`secondary_imgui.cpp:782-792`），供 Java 触摸窗同步。

## A.4 二级窗口内容 —— 逐个可见组件清单

`DrawContents()` = `secondary_imgui.cpp:327-694`。以下「行号」均为该函数内位置。
所有尺寸都是 `lgx::px(v)` = `v * setScale(2.0f)`（`Runtime.cpp:311` + `main.cpp:227`），
即 **1 单位 = 1dp，2 单位 = 1px**（density=2.0 的等效 dp；本机真实 density 由宿主决定，
Demo 硬编码 2.0）。字号另有 `ImGui::GetStyle().ScaleAllSizes(2.0f)`（`main.cpp:127`）。

| # | 组件类型 | 文案（中/英） | 尺寸 | 行号 |
| --- | --- | --- | --- | --- |
| 1 | 段落标题（ImGui Text） | `液态按键 PillButton` / `PillButton` | 行高 | 331 |
| 2 | **PillButton**（ytbl） | `液态玻璃按钮` / `Liquid Glass` | 160x48dp | 332-337 |
| 3 | 文本 | `点击 %d` / `Clicks %d`（累计点击数） | SameLine | 338-339 |
| 4 | **PillButton** tinted | `着色` / `Tinted`，tint=`Rgba(0,0.478,1,1)`=#0079FF | 140x48dp | 342-348 |
| 5 | **PillButton** surfaced | `表面色` / `Surface`（surface=白 30% / 深 12%） | 140x48dp | 350-357 |
| 6 | **PillButton** 小方形 | `+` | 48x48dp | 359-362 |
| 7 | 文本 | `%d`（小按钮点击数） | SameLine | 363-364 |
| 8 | Separator | — | — | 366-368 |
| 9 | 段落标题 | `液态开关 Toggle` / `Toggle` | — | 369 |
| 10 | **Toggle** | accent `0xFF30D158`（绿），右侧文字 `ON`/`OFF` | 64x28dp | 370-375 |
| 11 | **Toggle** | accent `0xFFFF4343`（红），右侧 `ON`/`OFF` | 64x28dp | 377-382 |
| 12 | **Toggle** | 初始 `setOn(true,false)`，accent 绿，右侧 `(默认开)` / `(default on)` | 64x28dp | 384-394 |
| 13 | 段落标题 | `液态滑块 Slider` / `Slider` | — | 399 |
| 14 | **Slider** 0~1 | accent `0xFF0091FF`(暗)/`0xFF0088FF`(亮)；下方文本 `0~1 值: %.3f` / `0~1 value: %.3f` | 宽=可用宽, 高 24dp | 400-408 |
| 15 | **Slider** 0~100 | `setRange(0,100)`；文本 `0~100 值: %.1f` / `0~100 value: %.1f` | 宽=可用宽, 高 24dp | 410-419 |
| 16 | 段落标题 | `胶囊弹出菜单 PopMenu` / `PopMenu` | — | 424 |
| 17 | **PopMenu** 胶囊本体 | 5 行，capText 跟随 `cursorRow`（初值 1 = "新建标签页"） | min(可用宽,180dp) x 44dp | 425-451 |
| 18 | **PopMenu.drawOverlay()** | 弹出层（见 A.5） | 依 `MENU_DEFAULT_W_DP=200` | 683 |
| 19 | 段落标题 | `液态底部标签 TabStrip` / `TabStrip` | — | 455 |
| 20 | ImGui Checkbox | `显示标签` / `Show tabs`（默认勾选） | — | 456 |
| 21 | **TabStrip** | 4 列：`首页`/`发现`/`消息`/`我的`（Home/Explore/Messages/Profile），图标 iconHome/iconSearch/iconMessage/iconProfile | 宽=可用宽, 高 64dp | 457-470 |
| 22 | 段落标题 | `玻璃形状 Corners` / `Corners` | — | 475 |
| 23 | 3 个自绘玻璃 chip（`DrawGlassChip`） | `胶囊`/`capsule`、`圆角16`/`r16`、`24/8/24/8` | 各 (可用宽-32)/3 x 44dp，间隔 16dp | 476-495 |
| 24 | 段落标题 | `边光样式 EdgeLight` / `EdgeLight` | — | 498 |
| 25 | 3 个自绘玻璃 chip | `Gradient` / `Ambient` / `Plain` | 44dp 高，三等分 | 499-516 |
| 26 | 段落标题 | `液态玻璃卡片` / `Glass card (live refraction)` | — | 521 |
| 27 | 2 个 ImGui Checkbox（互斥） | `弹窗动画一`/`Dialog animation 1: from button`、`弹窗动画二`/`Dialog animation 2: center glass orb` | — | 522-531 |
| 28 | **PillButton** 打开弹窗 | `打开液态弹窗` / `Open liquid dialog`，`visualScale = s_Dialog.sourceScale()` | 180x48dp | 532-551 |
| 29 | ImGui SliderFloat | `模糊半径 dp` / `Blur dp`（0~10, %.1f） | 内建 | 554 |
| 30 | ImGui SliderFloat | `折射高度 dp` / `Lens height dp`（0~40） | | 555 |
| 31 | ImGui SliderFloat | `折射强度 dp` / `Refraction dp`（0~60） | | 556 |
| 32 | ImGui Checkbox | `色散(色差)` / `Dispersion`（默认勾选） | | 557 |
| 33 | 卡片本体（**自绘条纹 + 玻璃面板**） | 文字 `折射测试内容 REFRACTION` / `REFRACTION TEST`；背景 6 色竖条纹 | 宽=可用宽 x 90dp | 559-587 |
| 34 | 段落标题 | `玻璃实验场` / `Glass Playground` | — | 589 |
| 35 | ImGui Checkbox | `显示参数面板` / `Show controls`（默认勾选） | — | 590 |
| 36 | Playground 画布 | 6 色条纹底 + 中央**可拖动/可缩放**玻璃圆角方块 | 宽=可用宽 x 280dp；方块默认 256dp（96~256） | 591-667 |
| 37 | Playground 内嵌参数面板 | `圆角比例`/`Corner radius` 0.1~1、`模糊半径 dp` 0~10、`折射高度比例`/`Refraction height` 0~1、`折射强度比例`/`Refraction amount` 0~1、`实验场色散`/`Playground dispersion` 0~2、`大小`/`Size` 96~256、按钮 `重置实验场`/`Reset playground` | — | 669-682 |
| 38 | **Modal 弹窗** | 见 A.5，最后绘制 | 420 上限 | 687-693 |

条纹配色（`secondary_imgui.cpp:76-80`，卡片与 Playground 共用）：

```cpp
static const ImU32 kStripeColors[] = {
  IM_COL32(235, 64, 52, 255), IM_COL32(255, 165, 0, 255),
  IM_COL32(255, 214, 0, 255), IM_COL32(76, 175, 80, 255),
  IM_COL32(33, 150, 243, 255), IM_COL32(156, 39, 176, 255),
};
```

卡片玻璃配方（`secondary_imgui.cpp:570-586`）：

```cpp
cardStyle.corners = lgx::Corners::rounded(lgx::px(24.0f));
cardStyle.optics.colorBoost = true;
cardStyle.optics.blurPx = lgx::px(s_BlurDp);              // 10dp（可调 0~10）
cardStyle.optics.lensHeightPx = lgx::px(s_LensHeightDp);  // 12dp（可调 0~40）
cardStyle.optics.lensAmountPx = lgx::px(s_LensAmountDp);  // 24dp（可调 0~60）
cardStyle.optics.depthAmount = 1.0f;
cardStyle.optics.spectral = s_Chromatic ? s_CardDispersion : 0.0f;   // 0.75
cardStyle.edgeOn = true;  cardStyle.edge = lgx::EdgeLight::gradient();
cardStyle.edge.widthDp = 1.5f;  cardStyle.edge.softnessDp = 0.75f;
cardStyle.insetOn = true;
cardStyle.inset = lgx::InsetShadow(6.0f, 0.0f, 6.0f, lgx::rgba(0.15f, 0x000000u), 1.0f);
```

小 chip 玻璃配方（`secondary_imgui.cpp:310-325`）：blur 2dp、lensHeight 8dp、lensAmount 16dp、
edge gradient、widthDp 1.5、softnessDp 0.75、`fallback = lgx::frostedFallbackColor()`。

## A.5 弹窗（Modal）与弹出菜单（PopMenu）细节

**Modal**（`secondary_imgui.cpp:532-551` + `687-693`）：

```cpp
s_DialogButton.label = Text("打开液态弹窗", "Open liquid dialog");
s_DialogButton.visualScale = s_Dialog.sourceScale();
const ImVec2 dialogButtonMin = ImGui::GetCursorScreenPos();
const ImVec2 dialogButtonSize(lgx::px(180.0f), lgx::px(48.0f));
if (s_DialogButton.draw(...)) {
    const char* dialogTitle = Text("弹窗标题", "Dialog Title");
    const char* dialogMessage = Text(
        "这是一个带有入场、退场、折射和双按钮交互的液态玻璃弹窗。",
        "This dialog uses animated dimming, refraction, and two actions.");
    if (s_DialogAnimationFromButton) {
      s_Dialog.openFrom(dialogTitle, dialogMessage, /*按钮矩形*/);
    } else {
      s_Dialog.open(dialogTitle, dialogMessage);   // 从中心长出
    }
}
...
if (s_Dialog.visible) {
    const ImVec2 dialogPos = ImGui::GetWindowPos();
    const ImVec2 dialogSize = ImGui::GetWindowSize();
    s_Dialog.draw(lgx::Box(dialogPos.x, dialogPos.y,
                           dialogPos.x + dialogSize.x,
                           dialogPos.y + dialogSize.y));
}
```

- 弹窗**按钮文字是英文 `Cancel` / `OK`**：Demo 从未赋值 `cancelLabel`/`confirmLabel`，
  用的是 `GlassUI.h:575-576` 的默认值 `const char* cancelLabel = "Cancel"; const char* confirmLabel = "OK";`
  ⇒ **中文界面下也是 Cancel / OK**（重建时必须照搬这个事实）。
- 弹窗尺寸常量（`GlassConfig.h:483-505`）：`DIALOG_MAX_W_DP=420`、`MIN_W=260`、`H=252`、
  标题字号 24dp、正文 15dp、按钮高 48dp、间隙 16dp、`DIALOG_GLASS_BLUR_DP=10`。
- 结果码不用返回值，`s_Dialog.result` 字段保存（`GlassUI.h:578`），Demo 里 return 被忽略
  （`secondary_imgui.cpp:690` 直接丢弃返回值）。
- 确认键是**着色胶囊**（`Dialog.cpp:466-470` `tinted=true, tint=DIALOG_BUTTON_CONFIRM_*`），
  取消键是**表面色胶囊**（`Dialog.cpp:438-447`），两者都 `contourEdge = true`（`Dialog.cpp:448/472`）。

**PopMenu**（`secondary_imgui.cpp:425-451`，叠加层在 `683-686`）：

```cpp
s_Menu.count = 5;
s_Menu.entries[0].kind = lgx::EntryKind::Heading;  s_Menu.entries[0].caption = Text("常用","QUICK");
s_Menu.entries[1].caption = Text("新建标签页","New Tab"); s_Menu.entries[1].shortcut = "Ctrl+T";
s_Menu.entries[1].painter = lgx::iconStar;
s_Menu.entries[2].caption = Text("复制页面地址","Copy Address");
s_Menu.entries[2].detail  = Text("连标题与来源一起复制","with title and source");
s_Menu.entries[2].painter = lgx::iconHeart;
s_Menu.entries[3].kind = lgx::EntryKind::Divider;
s_Menu.entries[4].caption = Text("固定到首页","Pin to Home"); s_Menu.entries[4].painter = lgx::iconGear;
```
初值：`setOpen(false,false)`、`cursorRow = 1`（`secondary_imgui.cpp:427-428`）。
宿主边界：`lgx::setHostBounds(ImGui::GetCurrentWindow()->InnerRect)`（`secondary_imgui.cpp:447-448`），
菜单靠它做 `clampToHost`。
`drawOverlay()` 放在本帧内容**最末尾**（卡片/实验场之后、Modal 之前）。
选中行回写：`if (s_Menu.activated >= 0 ...) s_Menu.cursorRow = s_Menu.activated;`（`684-686`）。

## A.6 交互 / 动画清单

| 交互 | 位置 | 说明 |
| --- | --- | --- |
| 主窗口拖动/缩放/折叠 | ImGui 内建 | `main.cpp:155` 普通 ImGui 窗口 |
| 二级窗口拖动/缩放/关闭 | ImGui 内建 + `&open` | `secondary_imgui.cpp:777` |
| 段落折叠（3 段） | `DrawSectionHeader` `secondary_imgui.cpp:267-308` | `InvisibleButton` 38dp 高，点击翻转 `*expanded`；箭头为两个三角形，**无缓动**（直接切换） |
| PillButton 按压 | `PillButton.cpp:125` `detail::probeBox(...)` | 弹簧 glow，`clicks` 计数 |
| Toggle 点击/拖动切换 | `Toggle.cpp:101` | 弹簧；`setOn(v,withMotion)` |
| Slider 拖动 | `Slider.cpp:113` | `animatedValue()` 弹簧值 |
| PopMenu 展开 | `PopupMenu.cpp` + `SpineSolver.cpp` | 4 条弹簧 `MENU_UNFOLD/WIDEN/DEEPEN/GREET_STIFFNESS`（`GlassConfig.h:250-257`），骨骼求解 + 液滴/颈缩形变 |
| PopMenu 展开时阻挡穿透 | `PopupMenu.cpp:335` `detail::blockPointer(...)` | 宿主 `onBlockPointer` → `ImGui::SetHoveredID("##lgx_pointer_shield")` + `ClearActiveID()`（`secondary_imgui.cpp:235-241`） |
| TabStrip 指示器 | `TabBar.cpp:201` | 弹簧滑动指示器；`activeIndex` 自动更新 |
| Modal 开/合 | `Dialog.cpp` | progress 弹簧 + pop 弹簧 + 宽高分别生长的弹簧 + 模糊/玻璃上升下降时间 + 色散 + 圆环/光环 + 按钮 `visualScale`；常量在 `GlassConfig.h:259-325` |
| Playground 拖动/缩放 | `secondary_imgui.cpp:610-643` | 两个 `InvisibleButton`（24dp 缩放手柄 + 方块本体），`IsItemActivated/Active/Deactivated` + `io.MousePos` 直接改 `s_PlaygroundOffset/SizeDp`（**无缓动**），范围钳制 96~256dp |
| 主题切换动画 | `ColorAdapter.cpp` + `config::Color::SWITCH_SECONDS = 0.34f`（`GlassConfig.h:139`） | `setGlassColors(..., true)` 带动画过渡 |
| 关闭二级窗口时清理拖拽态 | `secondary_imgui.cpp:706-713` | `SetVisible(false)` → 复位 dragging/resizing 并清触摸矩形 |

**注意**：`s_Tabs`（TabStrip）**只切换自己的 `activeIndex`，不切换任何页面内容** ——
`secondary_imgui.cpp` 全文没有读取 `s_Tabs.activeIndex` / `s_Tabs.changed`（已 grep 确认）。
所以「首页/发现/消息/我的」是**纯组件演示**，不是真的多页面路由。

## A.7 全部硬编码中文文案（原文）

### `secondary_imgui.cpp`（全部，行号:原文）

```
331  "液态按键 PillButton"
332  "液态玻璃按钮"
339  "点击"
342  "着色"
350  "表面色"
369  "液态开关 Toggle"
394  "(默认开)"
399  "液态滑块 Slider"
405  "0~1 值: %.3f"
416  "0~100 值: %.1f"
424  "胶囊弹出菜单 PopMenu"
432  "常用"
433  "新建标签页"
436  "复制页面地址"
437  "连标题与来源一起复制"
440  "固定到首页"
455  "液态底部标签 TabStrip"
456  "显示标签"
458  "首页"   459 "发现"   460 "消息"   461 "我的"
475  "玻璃形状 Corners"
482  "胶囊"   487 "圆角16"
498  "边光样式 EdgeLight"
521  "液态玻璃卡片"
524  "弹窗动画一"
528  "弹窗动画二"
532  "打开液态弹窗"
539  "弹窗标题"
541  "这是一个带有入场、退场、折射和双按钮交互的液态玻璃弹窗。"
554  "模糊半径 dp"
555  "折射高度 dp"
556  "折射强度 dp"
557  "色散(色差)"
569  "折射测试内容 REFRACTION"
589  "玻璃实验场"
590  "显示参数面板"
671  "圆角比例"
672  "模糊半径 dp"
673  "折射高度比例"
674  "折射强度比例"
675  "实验场色散"
676  "大小"
677  "重置实验场"
777  "二级液态玻璃"                          ← 二级窗口标题
832  "二级窗口玻璃参数"
834  "整窗液态玻璃"
835  "窗口模糊半径 dp"
837  "窗口折射高度 dp"
839  "窗口折射强度 dp"
841  "窗口色散强度"
843  "调节窗口透明度"
845  "开关关闭 = 窗口最透明"
848  "窗口透明度"
851  "窗口景深折射"
852  "窗口启用色散"
855  "液态玻璃卡片参数"
857  "卡片模糊半径 dp"
859  "卡片折射高度 dp"
861  "卡片折射强度 dp"
863  "卡片色散强度"
865  "卡片启用色散"
868  "液态玻璃实验场参数"
870  "实验场圆角比例"
872  "实验场模糊半径 dp"
874  "实验场折射高度比例"
876  "实验场折射强度比例"
878  "实验场色散强度"
880  "实验对象大小 dp"
882  "显示实验场内置面板"
884  "重置实验场参数"
```

### `main.cpp`

```
155  "主题"（主窗口标题）
164  "主题"（Tab 1）
166  "白色主题(浅色)"
172  "液态玻璃"（Tab 2）
178  "二级液态玻璃窗口"
185  "勾选后打开二级液态玻璃窗口"
```

### `ytbl/` 内

**没有任何中文 UI 文案** —— 所有中文只出现在每个 .cpp/.h 顶部的 Apache 许可注释块里
（如 `GlassUI.h:1-35`）。唯一的英文默认文案是 `GlassUI.h:575-576` 的 `"Cancel"` / `"OK"`。

### Java 层

见 A.1（`strings.xml:3-8`）。

## A.8 「用户看到的界面」文字重建

> 手机桌面之上浮着一个全屏透明层。屏幕左上角有一个深色标题栏的窗口，标题「主题」，
> 里面是一条两页的标签栏：「主题」「液态玻璃」。
>
> 「主题」页只有一个复选框「白色主题(浅色)」。
>
> 切到「液态玻璃」页，有一个复选框「二级液态玻璃窗口」，勾上后该页下方出现三个可折叠段：
> 「二级窗口玻璃参数」「液态玻璃卡片参数」「液态玻璃实验场参数」（每段是 38dp 高的圆角条，
> 左侧一个三角箭头指示展开状态）。
>
> 同时在主窗口内部偏左上位置出现第二个窗口：标题「二级液态玻璃」，带关闭按钮与半透明标题条。
> 这个窗口**整块是液态玻璃**（圆角 28dp，背景被折射/模糊/带 0.70 色散与边光），里面是一列
> 可滚动的内容：先是「液态按键 PillButton」标题 + 一个 160x48dp 的玻璃胶囊按钮「液态玻璃按钮」
> 与「点击 N」；接着一行「着色」蓝色胶囊、「表面色」胶囊、48x48 的「+」按钮；
> 分隔线后是「液态开关 Toggle」：三个 64x28 的玻璃开关（绿/红/绿默认开），
> 每个后面跟着 ON/OFF 文字；再一条分隔线后是「液态滑块 Slider」：两条 24dp 高的玻璃滑块
> （0~1 与 0~100），下面各有一行数值文字；接着「胶囊弹出菜单 PopMenu」：一个 180x44dp 的胶囊，
> 上面写着当前选中项名，点它会向上/下展开一条液态玻璃菜单（5 行：小标题「常用」、
> 「新建标签页 Ctrl+T」带星形图标、「复制页面地址 / 连标题与来源一起复制」带心形图标、
> 一条分隔线、「固定到首页」带齿轮图标）；接着「液态底部标签 TabStrip」+ 复选框「显示标签」，
> 勾选时显示一条宽满、64dp 高的玻璃底部标签栏（首页/发现/消息/我的，各带图标）；
> 接着「玻璃形状 Corners」下面并排 3 个 44dp 高的玻璃小块（胶囊 / 圆角16 / 24-8-24-8 四角不同）；
> 「边光样式 EdgeLight」并排 3 个玻璃小块（Gradient / Ambient / Plain）；
> 「液态玻璃卡片」：两个互斥复选框（弹窗动画一/二）+ 一个 180x48 的「打开液态弹窗」胶囊按钮，
> 点击后弹出一个液态玻璃弹窗，标题「弹窗标题」，正文「这是一个带有入场、退场、折射和双按钮
> 交互的液态玻璃弹窗。」，下方两个按钮 **Cancel / OK**（英文），弹窗可以「从按钮里长出来」
> 或「从中心长出来」两种动画；再往下是三个 ImGui 原生滑块（模糊半径/折射高度/折射强度 dp）
> 和一个「色散(色差)」复选框，紧接着一张 90dp 高的玻璃卡片盖在 6 色竖条纹上，
> 卡片左上写着「折射测试内容 REFRACTION」—— 拖动上面三个滑块能实时看到卡片背后条纹的
> 模糊与折射变化；最后是「玻璃实验场」+「显示参数面板」复选框，一片 280dp 高的条纹区域，
> 中央有一个可以**拖动**、右下角有手柄可以**横向拖动缩放**的玻璃圆角方块，区域内嵌
> 6 个滑块与「重置实验场」按钮。

---

# B. 渲染路径

## B.1 上屏链路

```
Java: WindowManager.addView(ImGuiTextureView)         FloatWindow.java:43-44
  └ TextureView（SurfaceTexture 后端）                ImGuiTextureView.java:10
      └ onSurfaceTextureAvailable → new Surface(surface)
          → NativeLib.setSurface(...)                 ImGuiTextureView.java:19-24
              └ JNI setSurface(Surface)               main.cpp:371
                  → ANativeWindow_fromSurface(env,surface)   main.cpp:295
                      → eglCreateWindowSurface(...)          main.cpp:72
                          → glViewport(0,0,W,H)              main.cpp:79
                              → ImGui GL3 绘制 + ytbl 玻璃 Pass
                                  → eglSwapBuffers               main.cpp:270
```

关键原文 `main.cpp:42-81`（EGL 初始化）：

```cpp
const EGLint attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT,
    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
    EGL_NONE };
g_EglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
if (!eglInitialize(g_EglDisplay, 0, 0)) return false;
... eglChooseConfig ... eglGetConfigAttrib(..., EGL_NATIVE_VISUAL_ID, &format);
ANativeWindow_acquire(g_Window);
ANativeWindow_setBuffersGeometry(g_Window, 0, 0, format);
const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
g_EglContext = eglCreateContext(g_EglDisplay, config, EGL_NO_CONTEXT, ctxAttribs);
g_EglSurface  = eglCreateWindowSurface(g_EglDisplay, config, g_Window, nullptr);
if (!eglMakeCurrent(g_EglDisplay, g_EglSurface, g_EglSurface, g_EglContext)) return false;
```

要点：
- 8bit alpha 的 EGL config + `glClearColor(0,0,0,0)`（`main.cpp:267`）⇒ 悬浮层是**真透明**叠加。
- 上下文是 **ES 3.0**（`EGL_CONTEXT_CLIENT_VERSION, 3`）。
- 渲染在**独立 std::thread** 上（`main.cpp:300` `std::thread(RenderLoop).detach();`），
  不在 UI 线程；线程退出靠两个 atomic 握手（`main.cpp:285-301`）。
- 没有调 `eglSwapInterval`（默认 1 = vsync），循环另外用 `1000000/120` 自节流到 ~120fps
  （`main.cpp:244`）。

## B.2 `imgui_impl_android.cpp` 到底干什么

它**不碰 Java**，只持有 `ANativeWindow*`：

- `ImGui_ImplAndroid_Init(ANativeWindow*)` — `imgui_impl_android.cpp:264-276`：
  只 `g_Window = window;`、设时间基准、`io.BackendPlatformName = "imgui_impl_android";`
- `ImGui_ImplAndroid_NewFrame()` — `:284-304`：

```cpp
int32_t window_width  = ANativeWindow_getWidth(g_Window);
int32_t window_height = ANativeWindow_getHeight(g_Window);
io.DisplaySize = ImVec2((float)window_width, (float)window_height);
if (window_width > 0 && window_height > 0)
    io.DisplayFramebufferScale = ImVec2((float)display_width / window_width,
                                        (float)display_height / window_height);
struct timespec current_timespec;
clock_gettime(CLOCK_MONOTONIC, &current_timespec);
double current_time = ...;
io.DeltaTime = g_Time > 0.0 ? (float)(current_time - g_Time) : (float)(1.0f / 60.0f);
```
  即：**显示尺寸 + 帧间隔**。`display_width/height` 就等于 window 尺寸 ⇒
  `DisplayFramebufferScale` 恒为 `(1,1)`（`imgui_impl_android.cpp:291-296`）。
- `ImGui_ImplAndroid_HandleInputEvent(const AInputEvent*)` — `:159-262`（键鼠/触摸映射，
  用 `AInputEvent_getType` / `AMotionEvent_getX` / `AKeyEvent_getKeyCode` 等）
  **在本 Demo 中从未被调用**（grep 确认：`main.cpp` 只 include 它，不调用）。
- 头文件注释自己声明缺失能力（`:8-14`）：无剪贴板、无手柄、无鼠标指针形状、
  软键盘与 Unicode 输入需应用自己喂。

**本 Demo 真正用的输入注入**是另一个小文件 `imgui_Input_android.cpp:4-16`（全文）：

```cpp
int32_t ImGui_ImplLinux_HandleInputEvent(ImGuInput_LinuxEvent event) {
    ImGuiIO& io = ImGui::GetIO();
    if (event.Action_type == IM_DOWN || event.Action_type == IM_UP) {
        io.MouseDown[0] = (event.Action_type == IM_DOWN);
        io.MousePos = event.pos;
    } else if (event.Action_type == IM_MOVE) {
        io.MousePos = event.pos;
        io.MouseDown[0] = true;
    } else {
        io.MousePos = event.pos;
    }
    return 0;
}
```
（`IM_DOWN=0 / IM_UP=1 / IM_MOVE=2`，见 `imgui_Input_android.h:3-11`。）

调用入口 `main.cpp:303-309`：

```cpp
static void JniMotionEventClick(JNIEnv*, jclass, jint action, jfloat x, jfloat y) {
    if (!g_Initialized) return;
    ImGuInput_LinuxEvent event;
    event.Action_type = action;
    event.pos = ImVec2(x, y);
    ImGui_ImplLinux_HandleInputEvent(event);
}
```
Java 侧 `FloatWindow.java:57-74` 用 `event.getRawX()/getRawY()`（**屏幕绝对坐标**）
发 0/2/1 三种 action。

## B.3 每帧调用顺序（`main.cpp:233-283` 全文引用）

```cpp
static void RenderLoop() {
    if (!InitEGL()) { g_Running = false; g_ThreadAlive = false; return; }
    InitImGui();
    InitGlass();
    ApplyHostTheme(g_LightTheme);
    g_Initialized = true;

    const std::chrono::microseconds kFrameDuration(1000000 / 120 + 1);
    auto nextFrame = std::chrono::steady_clock::now();

    while (g_Running) {
        const int w = ANativeWindow_getWidth(g_Window);
        const int h = ANativeWindow_getHeight(g_Window);
        if (w > 0 && h > 0 && (w != g_LastW || h != g_LastH)) {
            g_LastW = w; g_LastH = h;
            glViewport(0, 0, w, h);
        }

        lgx::beginFrame(ImGui::GetIO().DeltaTime);   // ① 玻璃库帧开始（清任务/推进动画）
        ImGui_ImplOpenGL3_NewFrame();                // ②
        ImGui_ImplAndroid_NewFrame();                // ③ 填 DisplaySize/DeltaTime
        ImGui::NewFrame();                           // ④
        SyncGlassFrameState();                       // ⑤ setSurface + setPointer

        DrawMenuContent();                           // ⑥ 主窗口

        secondary_imgui::SetLanguage(g_HasCJK);      // ⑦
        secondary_imgui::Draw();                     // ⑧ 二级玻璃窗口

        ImGui::Render();                             // ⑨ 生成 draw data（含玻璃回调）
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);        // ⑩
        glClear(GL_COLOR_BUFFER_BIT);                // ⑩
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());  // ⑪ 真正出图
        eglSwapBuffers(g_EglDisplay, g_EglSurface);  // ⑫

        nextFrame += kFrameDuration;
        auto now = std::chrono::steady_clock::now();
        if (now - nextFrame > kFrameDuration) nextFrame = now + kFrameDuration;
        std::this_thread::sleep_until(nextFrame);
    }

    g_Initialized = false;
    lgx::shutdown();
    ShutdownImGui();
    ShutdownEGL();
    g_ThreadAlive = false;
}
```

`SyncGlassFrameState()`（`main.cpp:204-223`）：

```cpp
lgx::Surface surface;
surface.width  = io.DisplaySize.x;
surface.height = io.DisplaySize.y;
surface.pixelScaleX = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
surface.pixelScaleY = io.DisplayFramebufferScale.y > 0.0f ? io.DisplayFramebufferScale.y : 1.0f;
lgx::setSurface(surface);

lgx::PointerState pointer;
pointer.x = io.MousePos.x;  pointer.y = io.MousePos.y;
pointer.down     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
pointer.pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
pointer.released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
lgx::setPointer(pointer);
```

`InitGlass()`（`main.cpp:225-231`）：

```cpp
static bool InitGlass() {
    secondary_imgui::InstallGlassBackend();   // → lgx::attachCanvasBackend(&s_Backend)
    lgx::setScale(2.0f);
    lgx::setBackdropEnabled(true);
    lgx::setTextMeasurer(MeasureGlassText);
    return lgx::startup();
}
```

`InitImGui()`（`main.cpp:99-128`）：

```cpp
IMGUI_CHECKVERSION();
ImGui::CreateContext();
ImGuiIO& io = ImGui::GetIO();
ImGui::StyleColorsDark();
ImGui_ImplAndroid_Init(g_Window);
ImGui_ImplOpenGL3_Init("#version 300 es");
#if defined(HAS_EMBEDDED_FONT)
    g_HasCJK = io.Fonts->AddFontFromMemoryTTF(
            (void*)font_v, font_v_size, 40.0f, nullptr,
            io.Fonts->GetGlyphRangesChineseSimplifiedCommon()) != nullptr;
#else
    /* /system/fonts/NotoSansCJK-Regular.ttc 等 4 个候选路径，全部失败则 AddFontDefault() */
#endif
ImGui::GetStyle().ScaleAllSizes(2.0f);
```

## B.4 字体

- **内嵌字体**：`imgui/字体.h`（`main.cpp:21-24` 用 `__has_include("字体.h")` 探测，
  定义 `HAS_EMBEDDED_FONT`）。
  实际内容（文件头两行）：

```cpp
static const unsigned int font_v_size = 1911448;
static const unsigned int font_v[1911448/4] = { 0x00000100, 0x00011200, ... };
```
  即 **1911448 字节（≈1.82 MiB）的 TTF**，以 `unsigned int[]` 十六进制字面量内嵌
  （文件之所以 5.9MB，是因为十六进制文本展开）。首 12 字节 `00 01 00 00 ... GPOS(0x534f5047)`
  说明是标准 sfnt/TrueType 轮廓字体（含 GPOS/GSUB/glyf/head 等表）。
- 加载方式：`AddFontFromMemoryTTF(font_v, 1911448, 40.0f, nullptr, GetGlyphRangesChineseSimplifiedCommon())`
  （`main.cpp:109-111`），**字号 40.0f**（配合 `ScaleAllSizes(2.0f)` 与 `setScale(2.0f)`，
  等效 20dp 正文）。
- 回退链（`main.cpp:113-124`）：`/system/fonts/NotoSansCJK-Regular.ttc` →
  `NotoSerifCJK-Regular.ttc` → `DroidSansFallback.ttf` → `DroidSansFallbackFull.ttf` →
  `AddFontDefault()`。是否成功写进 `g_HasCJK`（`main.cpp:29/40`），
  用 `T(zh,en)` 宏在**中英文案之间切换**（本机有内嵌字体，所以永远走中文）。
- **ytbl 不认识字体**：所有文字宽度都通过宿主注册的 `TextMeasurer` 回调
  （`main.cpp:195-202` → `ImGui::GetFont()->CalcTextSizeA(size, 1e6f, 0.0f, content)`），
  见 `使用说明.md:32-40`、`Runtime.cpp:135-138`（`textMetrics`）。
  组件用 `detail::drawTextCentered`（`Runtime.cpp:157-163`）先量宽再居中。

## B.5 ytbl 的玻璃合成路径（关键，移植必读）

1. `lgx::drawPanel(style, box)`（`Runtime.cpp:345-351`）：
   `submitPanel()` 登记一个 `PanelJob` → `detail::panelCapture(idx)` → `detail::panelPaint(idx)`。
2. 这两步**不立即画**，而是往 `ImDrawList` 里插两个回调
   （`secondary_imgui.cpp:204-213`）：

```cpp
void onPanelCapture(int index) override {
    ImGui::GetWindowDrawList()->AddCallback(&PanelCaptureCallback, (void*)(intptr_t)index);
    ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
void onPanelPaint(int index) override {
    ImGui::GetWindowDrawList()->AddCallback(&PanelPaintCallback, (void*)(intptr_t)index);
    ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}
```
   `ResetRenderState` 的意义：ytbl 的 GL pass 会改 program/blend/scissor，
   靠 ImGui 的 `imgui_impl_opengl3.cpp:589-590` 重新 `SetupRenderState` 恢复。
3. **捕获**（`GlassPanel.cpp:326-376` + `Context.cpp:664-680`）：
   把面板矩形向外扩 `capturePadding()`（`GlassPanel.cpp:78-92`，base 3px + blur*2 + lens +
   色散 pad，上限 640px）→ `glCopyTexSubImage2D` 从**当前绑定的 FBO/后缓冲**里抠一块：

```cpp
const int glY = viewH - (y + h);
glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, x, glY, w, h);
```
4. **模糊**（`Context.cpp:696-800`）：`glRunGather()` 两趟 FBO 高斯（12 taps，
   按 sigma 做降采样上限 16x，FBO 对齐 32，RGBA8），横向→纵向。
5. **回绘**（`Context.cpp:804-898`）：`glPaintPanel()` 绑 `uScene`（捕获纹理）+
   `uGathered`（模糊结果），设 40+ 个 uniform，`glScissor` 裁剪，`glBlendFuncSeparate(...)`，
   `glDrawArrays(GL_TRIANGLES,0,6)` 画一个整块四边形，片元里做圆角 SDF、透镜折射、
   棱镜色散（`PRISM_*` 常量，`GlassConfig.h:100-108`）、边光、阴影。
   顶点着色器**不用 VBO**，用 `gl_VertexID` 造四边形（`Shaders.cpp:49-51`）：
```glsl
float pickX = (gl_VertexID == 1 || gl_VertexID == 2 || gl_VertexID == 4) ? 1.0 : 0.0;
float pickY = (gl_VertexID == 2 || gl_VertexID == 4 || gl_VertexID == 5) ? 1.0 : 0.0;
```
   ⇒ **ES 3.0 的 `gl_VertexID`**，且**没有自建 VAO**：ES3 下 `glDrawArrays` 必须有 VAO 绑定，
   Demo 里提供 VAO 的是 ImGui（`imgui_impl_opengl3.cpp:467/538`）。
   **移植到纯 ELF 时这是必须自己补的一环。**

未确认：`glRunGather` 的 FBO 在 ImGui 的 render pass 中途切换后能否在所有驱动上正确恢复
（代码有 `probeTarget()/restoreTarget()`，`Context.cpp:369-380/725-729/787`，逻辑上闭环）。

---

# C. ytbl 的接入面

## C.1 `attachCanvasBackend()` 需要的虚函数**完整清单**

`GlassUI.h:177-210` 原文（20 个纯虚 + 1 个虚析构）：

```cpp
class CanvasBackend {
public:
    virtual ~CanvasBackend() = default;

    virtual Box clipRect() = 0;
    virtual void onFillRect(const Box& box, float radius, unsigned int color) = 0;
    virtual void onStrokeRect(const Box& box, float radius, float thickness,
                              unsigned int color) = 0;
    virtual void onFillPoly(const Point* points, int count, unsigned int color) = 0;
    virtual void onFillConcave(const Point* points, int count, unsigned int color) = 0;
    virtual void onStrokePoly(const Point* points, int count, float thickness,
                              unsigned int color) = 0;
    virtual void onLine(const Point& from, const Point& to, unsigned int color,
                        float thickness) = 0;
    virtual void onFillCircle(const Point& center, float radius, unsigned int color,
                              int segments) = 0;
    virtual void onFillTriangle(const Point& a, const Point& b, const Point& c,
                                unsigned int color) = 0;
    virtual void onFillArc(const Point& center, float radius, float fromRadians,
                           float toRadians, unsigned int color, int segments) = 0;
    virtual void onText(const Point& position, float size, unsigned int color,
                        const char* content, float wrapWidth) = 0;
    virtual void onImage(unsigned int texture, const Box& box, const Point& uvMin,
                         const Point& uvMax, unsigned int color) = 0;
    virtual void onPushClip(const Box& box) = 0;
    virtual void onPopClip() = 0;
    virtual void onPanelCapture(int index) = 0;
    virtual void onPanelPaint(int index) = 0;
    virtual PointerProbe onProbe(const Box& box, const void* owner, bool interactive) = 0;
    virtual void onBlockPointer(const Box& box) = 0;
    virtual void onClearPointerBlock() = 0;
};

void attachCanvasBackend(CanvasBackend* backend);
```

逐条含义（Demo 参考实现 `secondary_imgui.cpp:111-263`）：

| 虚函数 | 语义 | Demo 实现 |
| --- | --- | --- |
| `clipRect()` | 返回当前裁剪矩形（`Box`） | `GetWindowDrawList()->GetClipRectMin/Max()`（:113-118） |
| `onFillRect(box,radius,color)` | 圆角实心矩形；`radius` 为角半径（px） | `AddRectFilled(..., radius, 0)`（:120-124） |
| `onStrokeRect(box,radius,thickness,color)` | 圆角描边 | `AddRect(..., radius, 0, thickness)`（:126-131） |
| `onFillPoly(points,count,color)` | 凸多边形填充（count≥3） | `AddConvexPolyFilled`（:133-138） |
| `onFillConcave(points,count,color)` | **凹**多边形填充 | `AddConcavePolyFilled`（:140-145）⇒ 需要 ImGui ≥1.90（本仓库 1.90.7 有） |
| `onStrokePoly(points,count,thickness,color)` | 折线描边（count≥2） | `AddPolyline(..., 0, thickness)`（:147-153） |
| `onLine(from,to,color,thickness)` | 线段 | `AddLine`（:155-159） |
| `onFillCircle(center,radius,color,segments)` | 圆填充 | `AddCircleFilled`（:161-165） |
| `onFillTriangle(a,b,c,color)` | 三角填充 | `AddTriangleFilled`（:167-171） |
| `onFillArc(center,radius,fromRad,toRad,color,segments)` | **扇形/环形填充** | `PathArcTo` + `PathFillConvex`（:173-178） |
| `onText(pos,size,color,content,wrapWidth)` | 画文字；`wrapWidth=0` 表示不换行 | `AddText(GetFont(), size, pos, color, content, nullptr, wrapWidth)`（:180-186） |
| `onImage(tex,box,uvMin,uvMax,color)` | 贴纹理（texture 为 GL 名） | `AddImage((ImTextureID)(intptr_t)texture, ...)`（:188-195） |
| `onPushClip(box)` / `onPopClip()` | 裁剪栈 | `PushClipRect(...,true)` / `PopClipRect()`（:197-202） |
| `onPanelCapture(index)` | **玻璃面板：捕获背景**，成对出现的第一个 | `AddCallback(&PanelCaptureCallback, index)`（:204-208） |
| `onPanelPaint(index)` | **玻璃面板：回绘**，成对出现的第二个 | `AddCallback(&PanelPaintCallback, index)`（:210-213） |
| `onProbe(box,owner,interactive)` | 返回该矩形上的指针状态；`owner` 是组件 `this`（做 ID）；`interactive=false` 时只占位不吃事件 | `InvisibleButton("##lgx_probe", size, MouseButtonLeft)`，`held=IsItemActive()`、`activated=IsItemActivated()`、`clicked=返回值`（:215-233） |
| `onBlockPointer(box)` | 请求屏蔽指针（菜单展开时） | `SetHoveredID(ImHashStr("##lgx_pointer_shield"))` + `ClearActiveID()`（:235-241） |
| `onClearPointerBlock()` | 每帧清屏蔽（`beginFrame` 会调，`Runtime.cpp:443`） | 空实现（:243） |

`PointerProbe` 结构（`GlassUI.h:171-175`）：`{ bool held; bool activated; bool clicked; }`。

`PanelCaptureCallback/PanelPaintCallback` 最终落到 `lgx::runPanelCapture/runPanelPaint`
（`secondary_imgui.cpp:248-258`，`GlassUI.h:366-367`），裁剪矩形来自 `cmd->ClipRect`
（`secondary_imgui.cpp:260-262`）。

## C.2 `bindBackdropTexture()` 的用法与**重要陷阱**

声明（`GlassUI.h:154-157`）：

```cpp
void setBackdropEnabled(bool on);
void bindBackdropTexture(unsigned int texture, int width, int height);
void releaseBackdropTexture();
Rgba frostedFallbackColor();
```
实现（`Runtime.cpp:317/339-343` → `GlassPanel.cpp:431-441`）：只是记下 GL 纹理名与宽高。

**陷阱（必须告知宿主）**：一旦绑定了 backdrop 纹理，

1. `captureScene()` 直接把它当成背景采样区域，**不再做 `glCopyTexSubImage2D` 抓帧**
   （`GlassPanel.cpp:331-339`）：
```cpp
if (backdropReady()) {
    job.sampleOriginX = 0.0f;  job.sampleOriginY = 0.0f;
    job.sampleSizeX = (float)g_backdropWidth;  job.sampleSizeY = (float)g_backdropHeight;
    job.sampleStrideX = (float)g_backdropWidth; job.sampleStrideY = (float)g_backdropHeight;
    job.sampleReady = true;
}
```
2. `paintJob()` **跳过 gather 模糊**（`GlassPanel.cpp:391` 的 `!backdropReady()` 条件）：
```cpp
if (job.shadeMode <= 0.5f && !backdropReady() && sceneTexture != 0u
        && job.blurRadius >= config::Render::BLUR_MIN_SIGMA) { glRunGather(...); }
```
3. 片元着色器于是在 `uGatheredOn == 0` 分支**直接采样 `uScene`**（`Shaders.cpp:219-226`）：
```glsl
vec4 gatherRead(vec2 pixel) {
    if (uGatheredOn > 0.5) { ... texture(uGathered, ...); }
    return texture(uScene, sceneUv(pixel));
}
```
   ⇒ **绑定了 backdrop 就等于关掉了 blur**（`blurPx` 参数失效）。
   **宿主想要「模糊的桌面背景」，必须自己先把抓屏纹理模糊好再 `bindBackdropTexture`。**
4. 是否参与合成还取决于 `style.backdropOn && setBackdropEnabled(true)`
   （`GlassPanel.cpp:261` `job.sceneOn = (style.backdropOn && backdropEnabledFlag()) ? 1.0f : 0.0f;`）。
5. Demo **完全没用** `bindBackdropTexture`（grep 全仓：只有 `GlassUI.h`/`Runtime.cpp` 的定义
   和文档；`main.cpp:228`、`secondary_imgui.cpp:799` 只调了 `setBackdropEnabled(true)`）。
   所以 Demo 里玻璃折射的是**它自己刚画在 framebuffer 上的内容**（黑色底 + ImGui 图形）。

## C.3 初始化顺序（宿主必须遵守）

`使用说明.md:29-49` + Demo `main.cpp:233-241` 的实际顺序：

```
1. EGL/GLES 上下文 current                       main.cpp:75
2. ImGui::CreateContext()                        main.cpp:101
3. 平台后端 Init(ANativeWindow)                  main.cpp:105   ← 可替换
4. ImGui_ImplOpenGL3_Init("#version 300 es")     main.cpp:106   ← 必须在 GL current 下
5. 字体 AddFontFromMemoryTTF(...)                main.cpp:109-111
6. lgx::attachCanvasBackend(&backend)            secondary_imgui.cpp:699
7. lgx::setScale(2.0f)                           main.cpp:227
8. lgx::setBackdropEnabled(true)                 main.cpp:228
9. lgx::setTextMeasurer(fn)                      main.cpp:229
10. lgx::startup()   ← 编译 panel/gather 两个 program，失败返回 false   Runtime.cpp:428-432 / Context.cpp:604-639
11. （任何时候）lgx::setGlassColors(colors, true)   main.cpp:150
每帧： lgx::beginFrame(dt) → NewFrame → NewFrame → ImGui::NewFrame → setSurface/setPointer
      → 画 UI → ImGui::Render → RenderDrawData → eglSwapBuffers
退出： lgx::shutdown()（要求 GL 仍 current）→ ImGui 后端 Shutdown → DestroyContext → EGL 清理
      main.cpp:279-281
```

`startup()` 本质（`Runtime.cpp:428-432`）：

```cpp
bool startup() {
    if (!detail::glStartup()) return false;
    detail::beginStamp();
    return detail::glReady();
}
```
`glStartup()`（`Context.cpp:604-639`）只做两件事：`linkPair(quadVertex, panelFragment)` 与
`linkPair(quadVertex, gatherFragment)`，然后预置 uniform 与棱镜常量数组。

`shutdown()`（`Runtime.cpp:434-438`）：`glShutdown()` + `releaseBackdrop()` + `resetJobs()`。

## C.4 ytbl 对 ImGui 有没有依赖？—— **没有**（逐文件确认）

grep `imgui|ImGui|IMGUI`（大小写三种写法）跨整个 `ytbl/` 目录，命中 **40 处**，
分布如下：

| 文件 | 命中数 | 性质 |
| --- | --- | --- |
| `ytbl/使用说明.md` | 34 | **文档里的示例代码**（`#include "ytbl/GlassUI.h"` 的示例，含 `ImGui::GetCursorScreenPos()` 等） |
| `ytbl/README.md` | 6 | 只是截图文件名 `Screenshot_..._com.demo.imguifloat.jpg` |
| **全部 15 个 .cpp + 5 个 .h** | **0** | — |

每个 `.cpp` 的 include 只有三样：自己的头、`<math.h>`、以及 `Context.cpp:38-39` 的
`<GLES3/gl3.h>`：例如 `Runtime.cpp:36-38`、`GlassPanel.cpp:36-38`、`Toggle.cpp:36-38`。
`GlassUI.h:36-38`：

```cpp
#pragma once

#include "GlassConfig.h"
```

⇒ **ytbl 是零 ImGui 依赖、零 Java/JNI 依赖、零 Android 依赖（无 `<android/*.h>`、无
`__android_log_print`、无 `AAssetManager`）的纯 C++/GLES3 库。**
`使用说明.md:32-40` 里用 `ImGui::GetFont()->CalcTextSizeA(...)` 只是**示例宿主**，
`TextMeasurer` 是 `lgx::TextSize (*)(const char*, float)` 的函数指针（`GlassUI.h:131`），
任何字体引擎都能接。

**唯一需要 ImGui 的地方是 Demo 自己写的 `secondary_imgui.cpp`**，它同时用 ImGui
（窗口/布局/滚动/裁剪）和 ytbl（玻璃），桥就是那个 `ImGuiCanvasBackend`。

## C.5 GL 版本 / 扩展要求

- **GLES 3.0 核心，零扩展**。证据：
  - 三个 shader 全部 `#version 300 es` + `precision highp float`（`Shaders.cpp:43-44`、`60-61`、`102-103`）。
  - 顶点着色器用 **`gl_VertexID`**（`Shaders.cpp:49-50`）——ES 3.0 特性，ES 2.0 没有。
  - 用到 FBO：`glGenFramebuffers` / `glBindFramebuffer` / `glFramebufferTexture2D` /
    `glCheckFramebufferStatus`（`Context.cpp:524-535`、`767`、`775`、`377`）。
  - 纹理格式 `GL_RGBA8` + `GL_UNSIGNED_BYTE`（`Context.cpp:440/459/480/517`）。
  - 抓帧用 **`glCopyTexSubImage2D`**（`Context.cpp:672`）——ES 3.0 核心。
  - `glBlitFramebuffer` / `glTexStorage*` / `GL_RGBA16F` / `GL_HALF_FLOAT` /
    `GL_TEXTURE_EXTERNAL_OES` / `glReadPixels` **都没有用到**（grep 确认无命中）。
- 运行时要求：**一个 current 的 ES3 上下文**；且（见 B.5）**一个已绑定的 VAO**。
- Demo 的 EGL 请求：`EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT`，`EGL_CONTEXT_CLIENT_VERSION 3`
  （`main.cpp:45/68`）—— 只用 ES3。
- `Application.mk:3` → `APP_PLATFORM := android-21`（ES3 自 API 18 起可用）。

---

# D. 与「纯 ELF、无 Java」的差距清单

目标形态：自己建 SurfaceFlinger 图层 + 自己 `eglGetDisplay/eglCreateWindowSurface` +
GLES3 上屏，没有 Java、没有 Activity、没有 SurfaceTexture。

## D.1 可以直接复用（原样编进 ELF，一个字不用改）

| 资产 | 证据 | 备注 |
| --- | --- | --- |
| **整个 `ytbl/`（15 cpp + 5 h）** | 只 include 自身头 + `<math.h>` + `<GLES3/gl3.h>`（各文件 :36-38 行） | 零 ImGui / 零 Java / 零 Android 头。**这是最大的一块可复用资产。** |
| `imgui/` 核心（imgui.cpp / draw / widgets / tables / demo / stb 头） | `main.cpp:8` 起的用法；无平台 API | 纯 C++ |
| `imgui/backends/imgui_impl_opengl3.cpp/.h` | `imgui_impl_opengl3.h:49-62`：`#if defined(__ANDROID__) → #define IMGUI_IMPL_OPENGL_ES3` | 只要求 GLES3 + 一个 ANativeWindow 无关的 FBO 环境 |
| `imgui/字体.h`（1911448B TTF） | `main.cpp:21-24/109-111` | 纯数组，无依赖 |
| `imgui/backends/imgui_Input_android.cpp/.h` | `imgui_Input_android.cpp:1-16` | **只写 `io.MousePos/io.MouseDown`**，不碰 Android API ⇒ 换事件源即可直接复用 |
| 全部界面布局与文案（`secondary_imgui.cpp` 的 `DrawContents/DrawHostControls/Draw`） | `secondary_imgui.cpp:327-694/737-890` | 只要宿主提供「ImGui + CanvasBackend」两个东西，**逻辑与像素级外观可 1:1 复刻** |
| ytbl 的主题/参数（`ApplyHostTheme`、`setGlassColors`） | `main.cpp:136-151` | 与 `使用说明.md:107-118` 一致 |

## D.2 必须替换

| 原物 | 位置 | 替换成什么 |
| --- | --- | --- |
| `JNI_OnLoad` + `RegisterNatives` + `com/demo/imguifloat/NativeLib` | `main.cpp:370-389` | 删掉整个 JNI 层；`main()` 直接起线程/主循环 |
| `ANativeWindow_fromSurface(env, surface)` | `main.cpp:295` | 自建 SurfaceFlinger 图层拿到 buffer，或用自己的 `ANativeWindow`/`eglCreateWindowSurface` 路径 |
| `JniSetSurface` 的线程启停握手（3 个 atomic + 200×10ms 轮询） | `main.cpp:285-301` | 直接 `RenderLoop()`；或换成信号驱动 |
| `ImGui_ImplAndroid_Init(g_Window)` / `NewFrame()` | `main.cpp:105/257`、`imgui_impl_android.cpp:264-304` | 自己每帧填 `io.DisplaySize`（固定分辨率）+ `io.DeltaTime`（`clock_gettime`）。**这两件事就是它全部的贡献。** |
| 输入：`JniMotionEventClick` + Java `MotionEvent.getRawX/Y` | `main.cpp:303-309`、`FloatWindow.java:55-75` | 换成自己的触摸源（InputReader/InputFlinger 或 evdev），坐标口径必须与 `io.DisplaySize` 一致 |
| `ImGuiCanvasBackend`（若决定不用 ImGui） | `secondary_imgui.cpp:111-263` | 换成自己的 2D 绘制后端实现 `lgx::CanvasBackend` 的 20 个纯虚函数（C.1 清单）。**若继续用 ImGui 则本文件可原样复用。** |
| 透明/触摸的双窗结构 | `FloatWindow.java:37-76` | 纯 ELF 里没有 WindowManager：图层透明靠 `PixelFormat.RGBA_8888` + SurfaceFlinger blending；触摸拦截靠自建 input window（本项目另有 `docs/TOUCH_INTERCEPTION_SF.md` 路线） |
| 触摸窗矩形同步 `GetImGuiwinsize()` | `main.cpp:311-358`、`FloatWindow.java:110-137` | 纯 ELF 里没有意义（不需要把矩形回传给 Java）；但**如果还要做「只在窗口范围内拦截触摸」，这套「窗口矩形扫描」逻辑要保留**（g_ MenuRect / GetTouchRect 的并集算法在 `main.cpp:337-354`） |
| `Android.mk` / `Application.mk` | 全文 | ndk-build 换成自己的构建脚本；`-lEGL -lGLESv3` 仍需要 |
| Java 侧 `MainActivity` / `FloatWindow` / `ImGuiTextureView` / `NativeLib` / `activity_main.xml` / `strings.xml` | 见 A.1 | 全部丢弃（纯 ELF 无 Activity、无 Toast、无 SYSTEM_ALERT_WINDOW 权限模型） |
| ImGui 提供的 VAO | `imgui_impl_opengl3.cpp:467/538` | ytbl 自己**不建 VAO**（`Context.cpp` 无 `glGenVertexArrays`），若不走 ImGui 渲染器，必须自己在 `glDrawArrays` 前绑定一个 VAO |
| ImGui 的 `ImDrawCallback_ResetRenderState` 状态恢复 | `secondary_imgui.cpp:207/212`、`imgui_impl_opengl3.cpp:589-590` | ytbl 的玻璃 pass 会改 program/blend/scissor 且只 `glUseProgram(0)` 收尾（`Context.cpp:897`）；非 ImGui 宿主必须**自己保存/恢复 GL 状态** |

## D.3 会缺失（Demo 里本来就没有，或依赖 Java 才能有）

| 缺失项 | 证据 | 影响 |
| --- | --- | --- |
| **任何 Java 运行时** | `main.cpp:295/382` | 没有 `ANativeWindow_fromSurface`、没有 `FindClass`；纯 ELF 必须自建上屏 |
| **SurfaceTexture / TextureView 的自动尺寸与生命周期** | `ImGuiTextureView.java:19-38` | 分辨率变化、surface 销毁重建都要自己处理；Demo 里靠 `ANativeWindow_getWidth/Height` 每帧探测（`main.cpp:248-253`） |
| **输入法 / 剪贴板 / Unicode 文本输入** | `imgui_impl_android.cpp:8-14` 明确列为 Missing | Demo 根本没有文字输入框；纯 ELF 若要输入法需另做（本项目另有 `research/ime_result.md` 路线） |
| **触摸拦截能力** | `FloatWindow.java:42` `FLAG_NOT_TOUCHABLE` + 独立 1x1 触摸窗 | Demo 是「透明层 + 一个可变大小的小触摸窗」，**只在触摸窗覆盖范围内收事件**；纯 ELF 需要自己建 input window |
| **窗口矩形 → 输入区域 的自动跟随** | `main.cpp:337-354` 取主窗口与二级窗口矩形的并集 | 纯 ELF 需要重新实现（或按本项目已有方案） |
| **真实桌面背景折射** | Demo 只用 `setBackdropEnabled(true)`（`main.cpp:228`），从不 `bindBackdropTexture` | Demo 折射的是自己 framebuffer 的内容 ⇒ 想折射桌面必须**自己抓屏 + 自己模糊**再 `bindBackdropTexture`（原因见 C.2） |
| **DPI/密度自适应** | `main.cpp:127/227` 硬编码 2.0 | 换设备要自己换算（本项目约定 density = 3.0，需相应调 `setScale`） |
| **多显示器 / 旋转 / cutout** | Java 层 `LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES`（`FloatWindow.java:104-107`） | 纯 ELF 无对应概念 |
| **`eglSwapInterval` 之外的帧率策略** | `main.cpp:244` 自节流 120fps | 纯 ELF 需自己决定 vsync/自节流 |

## D.4 一句话结论

> **ytbl（20 个文件）+ ImGui 核心 + ImGui GL3 后端 + 内嵌字体** 这四块可以近乎原样编进纯 ELF；
> **`main.cpp` 的 JNI/EGL 启动段、`imgui_impl_android.cpp` 的 NewFrame、Java 的全部 4 个类**
> 必须替换成「自建 SurfaceFlinger 图层 + 自建 EGL + 自己的事件源 + 自己填 DisplaySize/DeltaTime」；
> `secondary_imgui.cpp` 的**界面与文案可以 100% 照搬**，前提是新宿主提供 (a) ImGui 上下文
> 或等价 2D 绘制层、(b) `lgx::CanvasBackend` 的 20 个实现、(c) 一个绑定的 VAO、
> (d) 玻璃 pass 前后的 GL 状态保存/恢复、(e) 若要让玻璃折射**桌面**，需自己抓屏并**预先模糊**
> 后 `bindBackdropTexture`（绑定该纹理会关掉 ytbl 内置的 gather 模糊）。

---

# E. 未确认项（明确标注，未编造）

1. `imgui/字体.h` 里那 1911448 字节 TTF 的**具体字体名/授权**：只能从 sfnt 表头判断是
   TrueType 轮廓 + GPOS/GSUB；**字体名未确认**（未解析 name 表）。
2. `ImGui_ImplAndroid_HandleInputEvent`（`imgui_impl_android.cpp:159-262`）是否是**给别的
   Demo 复用**的遗留代码、还是原作者打算切换而未切换：**未确认**（本仓库无调用点）。
3. `config::Layout::DIALOG_*` 里用到的 `DIALOG_BUTTON_CANCEL_DARK_*` /
   `DIALOG_BUTTON_CONFIRM_*` 具体数值未逐条抄录（`GlassConfig.h:526-545` 区间），
   需要精确配色时应再读该区间。
4. Demo 是否在真机上**实际跑起来过**、以及 `secondary_imgui` 两个窗口叠在一起时的
   z 序观感：无截图为证，**未确认**（`ytbl/README.md:24-29` 引用了 6 张
   `Screenshot_2026-10-04-*_com.demo.imguifloat.jpg` 截图，但**这些 jpg 文件不在本仓库目录内**，
   glob 未找到）。
5. `menifest` 未声明 `android:hardwareAccelerated`，但悬浮窗 params 里有
   `FLAG_HARDWARE_ACCELERATED`（`FloatWindow.java:101`）——对 TextureView 路径的实际影响**未确认**。
