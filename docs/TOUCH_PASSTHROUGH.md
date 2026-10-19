# Aurora Overlay 全屏触摸拦截方案

本文说明 Aurora 当前 Android Overlay 宿主如何防止触摸穿透和后方应用输入法被误触发。方案适用于公开 `TYPE_APPLICATION_OVERLAY` 宿主，不依赖第三方进程注入、私有 InputFlinger API 或透明像素自动命中。

## 1. 目标与边界

Overlay 需要同时满足以下行为：

- Aurora 面板内的触摸进入 Aurora Native Runtime；
- 面板外的触摸也由 Aurora 输入盾消费，不交给后方应用或桌面；
- 从 Aurora 面板开始的滑动/多指序列保持捕获，即使手指移出面板仍不穿透；
- 触摸代理不在服务启动时抢焦点，不自动拉起 Aurora 或后方应用输入法；
- Surface 尚未就绪、面板隐藏或服务销毁时，Native pointer 状态和输入法状态清理。

普通 Android Overlay 的透明像素不会自动决定触摸命中，因此视觉窗口和输入窗口必须职责分离；要防止穿透，输入窗口必须覆盖整个显示区域。

## 2. 双窗口模型

实现位置：`app/src/main/java/com/aurora/runtime/AuroraOverlayService.java`

### 2.1 渲染窗口

`AuroraOverlayService` 创建一个全屏 `AuroraSurfaceView`：

```text
尺寸：MATCH_PARENT x MATCH_PARENT
类型：TYPE_APPLICATION_OVERLAY
格式：TRANSLUCENT
关键标志：FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCHABLE
```

这个窗口只负责显示，不建立输入通道。`AuroraSurfaceView` 使用透明像素清屏，只有绘制的玻璃组件产生可见内容。

### 2.2 全屏输入盾

服务随后创建一个全屏透明 `AuroraTouchProxyView`：

```text
尺寸：MATCH_PARENT x MATCH_PARENT
类型：TYPE_APPLICATION_OVERLAY
格式：TRANSPARENT
默认标志：FLAG_NOT_FOCUSABLE | FLAG_LAYOUT_IN_SCREEN
软输入：SOFT_INPUT_ADJUST_NOTHING | SOFT_INPUT_STATE_ALWAYS_HIDDEN
```

输入盾没有 `FLAG_NOT_TOUCH_MODAL`，并覆盖显示区域，因此不把触摸交给后方窗口。它的 `onTouchEvent()` 对每个事件返回 `true`：

- `ACTION_DOWN` 在 Aurora 面板内：开始转发序列；
- `ACTION_DOWN` 在面板外：只消费，不调用 Native；
- 已经在面板内开始的序列：继续转发 `MOVE`、`POINTER_DOWN/UP`、`UP` 和 `CANCEL`；
- 序列结束后释放捕获状态。

这样即使用户从面板滑出，后方桌面程序也不会收到同一触摸序列，更不会因为这次触摸获得焦点并拉起自己的输入法。

## 3. 动态交互区域

Native 每帧输出实际 Aurora 面板矩形。Java 端每约 `16ms` 读取归一化矩形并转换到输入盾坐标系，但只更新 `AuroraTouchProxyView` 的命中区域，不缩小输入盾的窗口尺寸：

```text
Native panel rect -> OverlayGeometry.map() -> input shield local rect
```

区域无效时，输入盾仍消费触摸但不向 Native 转发。服务销毁时先移除输入盾，再移除渲染 Surface。

液态玻璃 Showcase 使用物理像素绘制，Native 在写入宿主交互区域前会按设备 density 转回逻辑坐标；Java 再将归一化坐标映射到输入盾的屏幕像素坐标。

## 4. 坐标转发

输入盾和渲染 Surface 都是全屏窗口，但 Java 端仍分别读取它们的屏幕原点，以处理厂商窗口原点或显示 cutout 差异：

```text
proxy local + proxy screen origin - render screen origin = render local
```

Native 接收的坐标保持与 ImGui `io.DisplaySize` 同一坐标系。

## 5. 输入法边界

输入盾默认不可聚焦。只有 `nativeWantsTextInput()` 在 Aurora 自己的 InputText 控件实际激活后返回 true，服务才：

1. 移除 `FLAG_NOT_FOCUSABLE`；
2. 更新输入窗口布局；
3. 调用 `setImeRequested(true)`、`requestFocus()`；
4. 重启 `InputConnection` 并请求显示系统输入法。

输入框失焦或页面切换后恢复 `FLAG_NOT_FOCUSABLE`，隐藏输入法并清除焦点。面板外触摸不会转发到后方应用，因此不会触发后方应用的输入法。

## 6. 生命周期清理

`AuroraOverlayService.onDestroy()`：

1. 停止区域同步；
2. 移除全屏输入盾；
3. 移除渲染 Surface；
4. 停止投屏服务、前台通知并释放 Native Surface。

Native 的 `surfaceDestroyed()` 会取消所有 pointer，投屏源在 `AuroraProjectionService.onDestroy()` 中释放 `VirtualDisplay` 和 `ImageReader`。

## 7. 已知 Android 限制

- 公开 Overlay 只能使用系统允许的 `TYPE_APPLICATION_OVERLAY`；
- Root 身份不自动授予任意私有输入接口或 MediaProjection token；
- 系统导航手势、锁屏和厂商安全层可能拥有更高输入优先级；
- 不同 ROM 可能对全屏透明 Overlay、通知层和遮挡触摸规则有差异；
- 本方案防止 Aurora Overlay 自身的触摸穿透，不声明绕过 Android 安全策略或接管任意第三方窗口。

## 8. 验证方式

本地验证：

```text
tests/OverlayGeometryTest.java
tests/InputRouterTest.cpp
tests/GestureRouterTest.cpp
```

真机验证：

1. 启动 `showcase=glass` Overlay；
2. 面板外点击文本应用，确认后方应用没有收到触摸；
3. 面板内点击 Aurora InputText，确认只拉起 Aurora 的输入法连接；
4. 从面板内滑出，确认序列仍由 Aurora 消费；
5. 切换页面或停止服务，确认键盘收起、服务退出且后续触摸恢复。
