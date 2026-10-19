# Aurora Overlay Android 输入法实现方案

本文说明 Aurora 如何在 Android Overlay 模式下把 ImGui 输入框接入系统软键盘。当前实现同时支持 Activity 的 `AuroraSurfaceView` 和 Overlay 的 `AuroraTouchProxyView`，但 Overlay 使用触摸代理作为真正的输入法宿主。

## 1. 问题背景

Overlay 的渲染 Surface 被设计为全屏透明且 `FLAG_NOT_TOUCHABLE`，它适合绘制但不适合接收输入法焦点。全屏 `AuroraTouchProxyView` 输入盾才是实际接收触摸和输入法连接的 View；输入盾消费面板外触摸但不向 Native 转发。

如果仍然把 `InputConnection` 绑定在不可触摸的渲染 Surface 上，会出现：

- 输入框点击能被 Native Runtime 看到，但代理窗口没有焦点；
- `InputMethodManager.showSoftInput()` 找不到有效的编辑器窗口；
- 软键盘不弹出；
- 文本提交回调不会进入正确的 View。

因此 Overlay 的 IME 桥必须挂到 `AuroraTouchProxyView`。

## 2. 状态流

```text
触摸代理收到 ACTION_DOWN
        |
        v
Native InputRouter -> ImGui InputText 激活
        |
        v
Renderer::wantsTextInput() == true
        |
        v
Java syncTouchProxy()
  移除 FLAG_NOT_FOCUSABLE
  更新代理窗口布局
  AuroraTouchProxyView.setImeRequested(true)
        |
        v
代理 View requestFocus()
        |
        v
onCreateInputConnection()
        |
        v
restartInput() + showSoftInput()
```

当输入框失焦或切换到其他页面时，流程反向执行：

```text
Renderer::wantsTextInput() == false
        |
        v
隐藏软键盘
清除代理 View 焦点
恢复 FLAG_NOT_FOCUSABLE
```

## 3. Native 请求状态

实现位置：

- `app/src/main/cpp/AuroraUi.cpp`
- `app/src/main/cpp/Renderer.cpp`
- `app/src/main/cpp/AuroraJni.cpp`

`AuroraUi` 只在输入页面的实际 `InputText` 控件激活后设置：

```cpp
textInputRequested_ = ImGui::IsItemActive() || textInputRequested_;
```

不要在进入 Input 页面时无条件设置请求状态，否则页面切换会自动弹出软键盘。

`Renderer` 将 UI 状态和 ImGui 的状态合并：

```cpp
wantsTextInput_.store(
    io.WantTextInput || ui_.textInputRequested(),
    std::memory_order_relaxed);
```

JNI 对外提供：

```text
AuroraTouchProxyView_nativeWantsTextInput()
AuroraSurfaceView_nativeWantsTextInput()
```

## 4. 代理 View 的 InputConnection

实现位置：

```text
app/src/main/java/com/aurora/runtime/AuroraTouchProxyView.java
```

代理 View 实现：

```java
@Override
public boolean onCheckIsTextEditor() {
    return imeRequested;
}
```

当 `imeRequested == true` 时，`onCreateInputConnection()` 返回 `BaseInputConnection`，并设置：

```java
outAttrs.inputType = TYPE_CLASS_TEXT | TYPE_TEXT_FLAG_MULTI_LINE;
outAttrs.imeOptions = EditorInfo.IME_ACTION_DONE;
```

支持的回调：

| Android 回调 | Native 行为 |
|---|---|
| `commitText` | `nativeText()`，进入 `Renderer::textInput()` |
| `setComposingText` | `nativeText()`，支持输入法组合文本 |
| `deleteSurroundingText` | 多次 `nativeDeleteBackward()` |
| `sendKeyEvent(KEYCODE_DEL)` | `nativeDeleteBackward()` |
| `sendKeyEvent` Unicode | 转为 Java 字符串后调用 `nativeText()` |

## 5. 焦点和窗口参数

输入代理窗口默认配置：

```text
FLAG_NOT_FOCUSABLE
SOFT_INPUT_ADJUST_NOTHING
SOFT_INPUT_STATE_ALWAYS_HIDDEN
```

默认不可聚焦，避免 Overlay 启动时抢走系统焦点或自动拉起键盘。

当 Native 报告需要输入法时，`AuroraOverlayService.syncTouchProxy()`：

1. 移除 `FLAG_NOT_FOCUSABLE`；
2. 调用 `WindowManager.updateViewLayout()`；
3. 调用 `AuroraTouchProxyView.setImeRequested(true)`；
4. 代理 View `requestFocus()`；
5. 在 View 获得 Window focus 后再次延迟调用 `restartInput()` 和 `showSoftInput()`。

`setImeRequested(true)` 中的延迟重试是必要的，因为窗口参数更新、View attach、Window focus 和 IME 服务响应不是同一个同步调用阶段。

## 6. 文本数据线程边界

Java 的 JNI 回调不会直接修改 ImGui 字符串。它们进入 `Renderer` 的线程安全队列：

```text
nativeText()
    -> Renderer::textInput()
    -> pendingText_
    -> renderFrame() swap
    -> AuroraUi::appendText()
```

退格使用 `pendingBackspaces_` 计数，同样在渲染线程帧开始时消费。这样输入法 Binder 回调线程不会和 ImGui 渲染线程同时访问 UI 状态。

## 7. Activity 与 Overlay 的差异

### Activity

`AuroraSurfaceView` 自身是可聚焦的编辑器 View，保留原有：

- `onCheckIsTextEditor()`；
- `onCreateInputConnection()`；
- `syncIme()`；
- `InputMethodManager.restartInput()`；
- `InputMethodManager.showSoftInput()`。

### Overlay

Overlay 的渲染 Surface 不负责输入法，所有 IME 回调由 `AuroraTouchProxyView` 接管。两者都调用相同 Native JNI 语义，因此 C++ UI Core 不感知宿主类型。

## 8. 关闭键盘与异常清理

以下情况必须清理输入法状态：

- 输入框失焦；
- 页面切换；
- Overlay 服务停止；
- Surface 销毁；
- Activity pause；
- 系统取消触摸；
- 代理窗口被 WindowManager 移除。

代理 View 在 `setImeRequested(false)` 中执行：

```java
hideSoftInputFromWindow(getWindowToken(), 0);
clearFocus();
setFocusable(false);
setFocusableInTouchMode(false);
```

Native 侧在 Surface 销毁和 pause 时调用 `InputRouter::cancelAll()`，避免残留按下状态或输入焦点。

## 9. 常见错误

### 错误一：输入盾默认可聚焦或把触摸代理设为不可触摸

输入盾必须默认 `FLAG_NOT_FOCUSABLE`，否则 Overlay 启动可能抢焦点并拉起键盘；它不能设置 `FLAG_NOT_TOUCHABLE`，否则第一次点击无法进入 View。只有 Native 确认 Aurora 自己的 InputText 激活时，才临时移除 `FLAG_NOT_FOCUSABLE`。

### 错误二：进入 Input 页面就弹键盘

页面可见不等于输入框激活。必须使用 `ImGui::IsItemActive()` 或等价的稳定焦点状态决定 `wantsTextInput`。

### 错误三：在 JNI 回调里直接改 ImGui 字符串

输入法回调可能运行在 Binder 线程。必须进入 Renderer 队列，由渲染线程消费。

### 错误四：只调用 `showSoftInput()` 不请求焦点

Android IME 需要一个已附着、可聚焦并返回 `InputConnection` 的 View。正确顺序是更新窗口焦点、请求 View focus、重启输入连接，再显示键盘。

## 10. 验证方式

本地验证：

```text
:app:assembleDebug
tests/InputRouterTest.cpp
tests/OverlayGeometryTest.java
```

测试机 真机验证：

1. 进入 Input 页面；
2. 点击文本框；
3. 确认软键盘出现；
4. 输入普通文本、中文组合文本和 Emoji；
5. 测试退格；
6. 切换页面，确认键盘收起；
7. 停止 Overlay 服务，确认键盘和输入连接都释放。
