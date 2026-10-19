# 横竖屏背景与悬浮层几何同步修复（2026-10-13）

用户报告：竖屏转横屏后玻璃背景异常；直接横屏启动也异常，返回竖屏不能自行恢复。
本轮在保留持续 mirror 采集、120 Hz 目标、原始 ytbl 光学配方及逐面板 Z-order 的基础上修复。

## 三个根因

1. 启动时读取 DMS 的自然尺寸 `mBaseDisplayInfo`，没有优先使用当前逻辑方向的 `mOverrideDisplayInfo`。因此直接横屏启动时仍以 1080×2376 建立宿主，而当前逻辑屏是 2376×1080。
2. 采集 mirror/VirtualDisplay/ImageReader 和 native viewport、背景资源、输入区域各自保留旧尺寸；旧代帧还可能在新布局中被导入或重新发布触摸形状。180° 旋转宽高相同，单独比较宽高也无法发现。
3. **仅修改父层 crop 与 ANativeWindow 缓冲尺寸不足以修复整体拉伸。** `SurfaceControl::getSurface()` 创建的 BLAST 子层带 `updateDestinationFrame=true`，它的默认目的区域仍为旧竖屏尺寸。实际 SF dump 曾同时出现父层 2376×1080、BLAST `geomContentCrop` 1080×2376，UI 被横向压缩、纵向拉长。修复必须更新这个缓冲队列的默认尺寸。

第三项源码证据：仓库的 `research/aosp/a16_native_SurfaceControl.cpp:138-175`。
`generateSurfaceLocked()` 创建 BLAST 并按 `mWidth/mHeight` 更新，`updateDefaultBufferSize()` 在锁内修改默认宽高并调用 `mBbq->update()`。真机已核实导出符号 `_ZN7android14SurfaceControl23updateDefaultBufferSizeEjj`。

## 实现与资源生命周期

- `root-host/elf/DisplayGeometry.h` 解析所选 logical display，优先有效 override；校验宽高与 rotation。启动不再猜固定 1080×2376。
- `CaptureHelper.java` 直接读 `IDisplayManager.getDisplayInfo`，以名义 100 ms 间隔检查逻辑宽高和 rotation，避免 headless helper 的 DisplayManagerGlobal 缓存滞留。几何变化才递增 generation，并重新建立 mirror/Reader；同尺寸 180° 也更新。
- mode 6/7/8 使用 80 字节头，末尾携带 logical width、height、rotation、generation；idle 消息也携带。接收端保留旧 mode 的兼容解析。正常镜像内容更新仍按目标速率运行，几何检查不是 10 fps 采集。
- 旧 mirror 先停生产并移除镜像层；仍有 GPU 租约的旧 Reader/Image 留到对应 sequence 的 GPU fence ACK 后再关闭。sequence 跨代唯一，ACK 能找到退役 Reader，避免旋转时提前复用采样中的像素。
- native 以一个帧代数同步 viewport/投影/背景/触摸形状。尺寸切换清掉待处理旧帧、取消旧按压，并在渲染线程重置鼠标边沿。旧代渲染结果不能重新发布新布局的触摸区域。
- `Overlay::resize()` 调用 `SurfaceControl::updateDefaultBufferSize()`，并强制提交父层 crop、内容尺寸及空输入区域，即便该区域本来已空。GLES 渲染线程同步 `ANativeWindow_setBuffersGeometry()`，随后按新尺寸更新资源。保持 EGL context 与既有光学实现。
- 更新缺失符号/缓冲尺寸失败时正常退出并释放输入，避免留下冻结或失效的拦截区。GLES resize 故障会通知主循环，以退出码 14 收尾。

坐标使用**逻辑屏空间**，由 SurfaceFlinger 做物理旋转；采集画面不额外叠加一个 90° 旋转。原有一次 GPU 垂直翻转继续保留。

## 真机验证

所有原图均为未经缩放的 `screencap` raw 转 PNG；完整证据位于 `research/rotation_run/`。
坐标网格验收显式使用 `AURORA_LATENCY_TEST=1`，将整窗四项光学值归零以排除折射，正式默认仍为 **10 / 18 / 30 / 0.70**。

| 验证 | 结果与证据 |
| --- | --- |
| 直接横屏启动，随后四方向与回竖屏 | `accept.log`：初始 2376×1080/rotation 1，随后 0→2→3→0；24 秒 2182 个采集帧，导入失败 0，正常退出；`accept_helper.log` 的旧代 Reader 均在 leases=0 后释放 |
| 连续旋转、用户移动窗口 | `fixed.log`：35 秒 3138 个采集帧，导入失败 0，退出 0；覆盖 rotation 0/1/2/3；`fixed_helper.log` 可见退役时仍有 1～2 租约，后续归零释放 |
| 背景坐标对齐 | `analyze_geometry.py` / `geometry_analysis.json`：mirror 横屏、返回竖屏，GPU 横屏、返回竖屏、快速切换后竖屏，共 5 张图、25 条窗内网格边界；全部峰值与屏幕预期边界同坐标，测得偏差 0 像素。仅覆盖选定无文字的采样带，不代表全部图像像素差异为零 |
| 旋转后的精确输入 | `accept_touch.log`：横屏窗内 (350,62) 不增加后方 DOWN；窗外 (2200,900)、圆角外 (18,18) 增加后方 DOWN；回竖屏窗内仍拦截，窗外 (980,2200) 穿透。覆盖层只收到两次窗内点击的 4 个 DOWN/UP 事件，固定窗口会话无其他输入 |
| GPU snapshot 备用路径 | `gpu.log`：16 秒 1775 个采集帧，导入失败 0，退出 0。直接横屏启动、回竖屏及连续 0.1 秒方向请求后最终竖屏恢复正确；中间请求可能被 Android 合并，不宣称每个短暂方向都呈现过 |
| 正式 SH、完整玻璃效果 | `full.log`：不设置零光学环境变量，竖屏→横屏→竖屏，22 秒 2426 个采集帧、373 个触摸事件、导入失败 0、退出 0。`full_portrait_start.png` 显示原始 10/18/30/0.70；热态源约 120 fps，CPU 界面约 120 Hz，实际呈现采样约 111～120 fps，不能称全程锁定 120 |
| 宿主回归 | `tests/run_host_tests.ps1`：18 项通过，0 失败/跳过；新增 DisplayGeometry 12 检查，InputRouter 增补取消未消费 DOWN 的两条路径 |
| helper 协议诊断 | `DisplayGeometryProtocolProbe.java`：24 检查通过，覆盖 180°、奇数尺寸舍入、80 字节头几何偏移 |
| 正式包真机验收 | `sh_delivery.log`：16 项通过、0 失败；实际物理屏 12 秒、351 个采集帧、退出 0，无残留采集/覆盖层进程及临时解包目录。包装验收使用脚本的 30/30 参数，不作为 120 Hz 性能测试 |

早期 `cycle_landscape.png` 是只改 ANativeWindow、尚未修 BLAST 的失败证据，不能当通过截图。
`baseline` 和第一轮 `rotation_*` 部分截图是在自动退出后取得，不能作为覆盖层 A/B。

100 ms 是**源几何检查的名义节流间隔**，不是旋转后恢复时间承诺。还需等待取帧、Binder、镜像/Reader 重建、首帧 fence 和 native 呈现；无初帧兼容回退最多等待 2 秒。本轮验证最终恢复和布局正确，未量化过渡阶段逐帧延迟。

## 最终交付

- ELF：`build/elf-arm64/bin/aurora_overlay`，**10665888 bytes**，SHA256 `62377f44040ccc177566b8aa94328d65b373a91f7e79f4257e76373e1aa25d90`。
- SH：`dist/aurora_overlay.sh`，**5467551 bytes**，SHA256 `0cda0c7fe7408a54ad0392bbf82ed17b170942e5c67f4db323838975fdd16453`。
- 本机 `build/recon/aurora_overlay`、手机 `/data/local/tmp/aurora_overlay` 同步为上述 ELF；手机正式单文件包 `/sdcard/Download/aurora_overlay.sh`。
- SH 默认仍为物理屏、120/120、300 秒自动退出、无 IME。离线校验确认 LF/无 BOM、解包逐字节与 ELF 一致。
- 旋转修复前备份：手机 `/data/local/tmp/aurora_overlay.before_rotation`，本机 `research/rotation_run/aurora_overlay.before_rotation`，SHA256 `384a8bffaffdfd06d4b3058680da2bd36206d6b0106c2948c60d36c1cc5515d7`。更早 `before_power` 备份继续保留。

先前延迟/工作量优化继续有效，见 `docs/GLASS_LATENCY_POWER_OPTIMIZATION.md`。本轮没有以关闭模糊/折射作为正式修复，也没有做长期温升实验。
