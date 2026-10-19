# 全局实时玻璃与精确触摸修复 — 2026-10-13

交付物是单文件 `aurora_overlay`，已经替换手机上的
`/data/local/tmp/aurora_overlay`。旧版备份为同目录的
`aurora_overlay.before_realtime`。本地正式副本在
`build/recon/aurora_overlay`，构建原始产物在 `build/elf-arm64/bin/aurora_overlay`。

文件大小 9,801,064 字节；SHA-256：

```text
e34df0b347e4c0ad130187a52ab1bf475209951cad08355554d33e09dd5e84a0
```

## 背景链路

此前每一帧启动 screencap，把完整 RGBA 读回 CPU，再翻转、降采样、上传 GL。
新默认路径是：

```text
ELF 内嵌 Dex + 极小 JNI 库
  → 启动一个持续 app_process 采集辅助进程
  → IWindowManager.captureDisplay(逻辑 displayId, CaptureArgs, listener)
  → WMS 选定整个目标显示的根图层
  → SurfaceFlinger GPU 降采样 / 返回 HardwareBuffer
  → Unix socket 传递 AHardwareBuffer 的 FD/句柄
  → ELF 渲染线程导入 EGLImage
  → 一次 GPU 绘制垂直翻转，写入原有 ytbl 背景纹理
  → 原有玻璃模糊、折射、组件动效
```

这取得的是显示根图层里的桌面和前台应用内容，适用于跨应用悬浮层。
`eSkipScreenshot` 仍在覆盖层创建 flags 中，采集图像排除覆盖层本身。
ytbl 的 shader、玻璃参数和组件动效没有改写。

交付仍只有一个 ELF；运行时存在 ELF 与采集辅助进程两个进程。
采集用的 Dex/JNI 临时释放，正常退出时删除。此路径不安装 APK，
不要求 MediaProjection 授权。IME 仍沿用既有独立桥接，测试时 `--no-ime`。

系统 ScreenCapture JNI 等待生产 fence 后才把 HardwareBuffer 交给 Java。
接收方与 Renderer 各有明确引用；Renderer 只保留最新待上传帧。
GLES 通过 GLsync 在 GPU 用完后回收导入资源，待回收 AHB 最多四份。
正常路径没有像素 CPU 读回，也没有每帧 fork、PNG 编码和 CPU 行翻转。

## 系统触摸区域

旧版启动时固定 `(0,0)-(800,2160)`，把空白间隙和透明圆角也包含进去。
新区域来自当前已提交渲染帧：标题、玻璃体、主题窗、真实弹窗、滚动条、缩放柄。
保留各自的圆角与 clip，转换成整数行带并集，不再取包络框。
拖动、折叠、隐藏时替换整个 Region，因此不会累积历史位置。

修复了两个独立 ABI 问题：

1. 设备 `WindowInfo::addTouchableRegion` 的 `ADD x0,x0,#0xf8`
   与构造函数均证明 Region 起点为 `info()+248`，不是旧代码的 `+184`。
   `+280` 是其内联矩形数据，不能当作 Region 对象起点。
   启动时校验设备 thunk，然后用真实已构造对象的 clear/add/getArray 操作；
   对多矩形 canonical 并集回读一致后才提交 SF。
2. `setInputWindowInfo` 按值参数是一个拥有引用的 sp 槽，设备实现会移动并清空它。
   旧 POD 参数复制不增加引用，第一次事务释放后留下悬空 WindowInfo。
   现在 Overlay 保持独立引用，每次提交额外取得参数引用，移交给真实事务。

每个 Overlay 复用一个真实 Transaction 并 clear，避免动态更新持续泄漏。
区域提交失败、输入通道断开或采集失败会终止并清理覆盖层，返回错误状态。

## 真机证据

设备：测试机 / Android 16 / Adreno 830。
虚拟显示本轮逻辑 ID / layerStack 为 24，SF ID 为 `<display-id>`。

| 验证 | 结果 |
|---|---|
| GPU 采集独立探针 | 120 次请求 / 0.826 秒；平均采集 4.248 ms，总耗时 6.862 ms；热帧约 2–5 ms |
| 生产 GLES 离屏导入 | 60 次全部通过：四色上下方向、完整覆盖、Alpha、GL 状态、尺寸切换、引用回收；失败 0、pending 0 |
| 默认 30 fps，虚拟显示 90 秒 | 2646 采集帧；热态 29.2–29.9 fps；导入失败 0、背景过期 0 |
| 60 fps，虚拟显示 30 秒 | 1713 采集帧；热态 57.6–58.2 fps；设置 → 动画测试程序切换后继续增长；导入失败 0 |
| 用户授权物理屏 40 秒 | 1172 采集帧；热态 29.3–29.6 fps；导入失败 0、背景过期 0，自动退出 |
| 内存快照 | ELF RSS 约 130–137 MB；采集辅助进程首个快照约 171 MB。不是长期功耗/内存压力测试 |

探针的 145 次请求/秒是采集吞吐，不能称为 145 fps 的玻璃显示帧率。
离屏探针耗时包含测试专用 CPU 写入与 readback，也不能当作生产导入耗时。

虚拟显示上的端到端触摸证据：

- `(950,600)` 右侧空白、`(400,723)` 两窗口之间、`(24,700)` 透明底角：
  测试程序收到 DOWN，计数由 0 到 3；覆盖层不收到这些点击。
- `(400,50)` 可见标题：覆盖层收到两条触摸事件，背后计数保持 3。
- 标题拖动约 `(200,170)` 后：系统区域累计更新 33 次，旧位置
  `(400,50)` 传到背后程序计数 4，新标题位置 `(900,220)` 仍由覆盖层接收。
- 最终交付版本重新验证折叠：隐藏原玻璃体后，`(400,500)` 传到背后程序，
  计数 8；恢复并拖动后旧位置再次穿透（计数 9），新标题位置仍接收。
  最终测试日志累计 104 条覆盖层触摸事件、34 次区域更新。

普通 screencap 因 eSkipScreenshot 看不到覆盖层，因此用一次诊断 readback
保存已绘制帧。`research/realtime_touch_run/frame.png` 中玻璃显示动画程序帧数字与彩条；
`settings.png` 中显示后面的系统设置内容，文字上下方向正确。
这次诊断 readback 不在正常采集路径中。

原始记录：`research/realtime_touch_run/rt30.log`、`rt60.log`、`physical.log`、
`touch_final.log`。截图只保存覆盖层自己的 framebuffer，透明外部区域显示为黑色。

## 运行与回归

```sh
# root shell，用户自行在物理屏运行
/data/local/tmp/aurora_overlay --display 0 --yes-physical --dump

# 60 fps；默认 downscale=2，由系统 GPU 降采样
/data/local/tmp/aurora_overlay --display 0 --yes-physical --capture-fps 60 --dump
```

默认 30 fps / downscale 2；`--capture-backend cpu` 为持续进程 CPU 读回备用，
`--capture-backend screencap` 为旧链路 A/B，对它最多请求 5 fps。
`--dump-frame /data/local/tmp/frame.ppm` 只在启动三秒后保存一次诊断帧。
退出用 SIGTERM，保持清理逻辑。

```powershell
pwsh -File tests/run_host_tests.ps1
pwsh -File root-host/elf/realtime_capture/build-helper.ps1
& '<sdk>\cmake\3.22.1\bin\cmake.exe' --build build/elf-arm64 --target aurora_overlay -j 6
```

宿主回归 12 项全部通过，新增精确 Region 八组与 strong ownership 四组。
手机测试 APK 已卸载，测试程序与临时文件已清理，正式 ELF 当前未运行。
确认无 AuroraOverlay 图层、覆盖层/采集进程和采集临时目录残留，`inhibited=0`。
系统诊断仍保留一个已卸载 fixture 的 RequestedLayerState 与 ActivityRecord 记录；
测试应用进程和包已移除，AuroraOverlay 也不在 dumpsys input 中。
未尝试重启 SurfaceFlinger 去清其缓存。

边界：本轮没有逐款运行微信/游戏，没有重测 IME 文本往返；物理屏验收为背景持续更新，
详细触摸穿透/拖动/折叠是在虚拟显示完成。系统禁止采集的 secure/protected 内容
仍受系统限制。输入 Region 的运行时 ABI 校验针对当前厂商 ROM，OTA 不匹配会拒绝运行。
父进程异常崩溃时辅助进程会因 UDS EOF 退出，临时目录的异常崩溃清理尚未补齐。

辅助过程：AI 助手 委派已实际发起，900 秒后失败；其未交付的研究建议未用于实现。
内部工作台 的 task_route 本轮不在已启用 测试工具 工具集中，状态落地于此文档。
