# 全局液态玻璃：延迟与性能优化（2026-10-13）

本轮针对用户反馈的「120 帧仍有背景延迟、高速运动割裂、持续发热」，保留原有玻璃配方、模糊/折射/色散 shader、阴影、按压与拖动动效。正式 ELF 的默认背景后端改为 `mirror`，采集与界面目标仍为 120 Hz，降采样仍为 2；没有用减小模糊或降低默认帧率换取性能。

## 实现

1. **持续镜像替代逐次截图请求**：`MirrorCapture.java` 通过 WMS `mirrorDisplay` 克隆应用/桌面所在的 windowing 子树，放到独立、请求 120 Hz 的虚拟显示与 ImageReader。背景没有变化时不再强制每秒做 120 次 ScreenCapture；变化时取 `acquireLatestImage()`，旧的待交接帧被替换。覆盖层仍在这个子树外，既不递归采集自身，也保留系统截图/录屏中的窗口。
2. **明确 GPU 租约**：AHardwareBuffer 引用只保护分配，不阻止 ImageReader 重写像素。发送端持有 Image，接收端在 GPU fence 完成后按 sequence ACK；没有提前归还正在采样的 Image。8 个 Image 槽用于在途租约与 acquireLatest 余量，不能当成 8 帧 FIFO 来压缩。
3. **有限导入缓存**：按 AHB allocation ID 缓存 EGLImage/GL texture，最多 8 项；只淘汰没有在途 GPU 使用的项。实机检查复用、淘汰、外部引用释放、失败路径及退出回调。明确使用 `std::exchange` 取走 completion，避免把 std::function move 后的源槽当成必然为空。
4. **在提交前取最新背景**：仅透明 GLES + SceneComposite 路径在 CPU 界面构建结束后导入 AHB，并再次检查最新槽。旧未读帧立即归还，新帧用于本次全部玻璃回调。刷新 descriptor、背景绑定及 damage generation；不再次推进弹簧或清空面板 jobs。目标节拍仍有上限，不增加通知触发的额外绘制。
5. **避免重复工作**：完整比较 draw data 和面板视觉输入；只有像素输入、背景代数、尺寸均相同且前次提交成功时，跳过 GPU 合成/swap。CPU 输入与动画检查仍保持目标节拍。未知 callback、未知纹理或不支持的 backend 采取保守重绘。屏外面板/阴影按实际 clip 分别剔除；只合并没有中间绘制且 clip/状态相同的 capture/paint；缓存 scene FBO 完整性检查。Toggle 轨道和圆钮之间的绘制仍保持 Z-order。
6. **减少非图像开销**：MemAvailable 每秒读一次；输入形状真正变化才构造像素条带并提交完整区域替换。诊断数字以 4 Hz 更新，实际动画时钟仍为目标速率。透明宿主不分配用不到的不透明背景资源。

镜像创建失败，或 2 秒内完全没有收到第一张 Image 时，释放镜像并切回 WMS GPU snapshot。已经收到镜像帧后的运行故障仍正常退出，避免保留冻结玻璃或失效触摸区。回退帧通过协议区分，仍按设置帧率限速。

## 真机证据与限度

所有原始结果位于 `research/latency_power_run/`，以下结论刻意区分目标循环、源画面更新与真实呈现。

| 检查 | 结果 | 范围 |
| --- | --- | --- |
| Gray code 截图对照 | Snapshot 六张均落后 4 个源 draw；Mirror 六张均落后 2；Mirror + swap0 六张仍为 2 | 18/18 有效，零输入、固定窗口、多行码一致；源更新约 60 Hz，不能按界面 120 Hz 换算成 8.3 ms/draw |
| 实际 120 Hz 源 | `mirror40.log` 热态背景约 120 fps、实际呈现约 120 fps、导入失败 0 | 物理屏动态源，用户有移动/缩放窗口；不作为同图功耗 A/B |
| 静止背景 | `physical_static_new.log` 多个采样窗口 gpuSubmit=0，界面检查约 120 Hz，idleSkipped 持续增加 | 后续触摸及通知触发刷新；不是把所有场景的刷新率固定降为 0 |
| GPU buffer 探针 | 76 帧、3 个预期失败，79 次 completion 恰好一次；复用 hit=60，miss=17，缓存上限 8，pending=0 | `gpu_import_probe.cpp`；包含 3 槽持续复用、不同 ID 淘汰、时间戳不一致及退出 |
| 视觉优化 A/B | 固定棋盘背景、原始玻璃参数下，关闭/开启 batch+cull+FBO cache 的 270×594 诊断图逐字节相同，160380 像素差异 0 | `RenderVisualProbe.cpp` 在私有显示绘制实际 Renderer/GLES，4 倍诊断缩小，未覆盖所有交互/全分辨率 HDR 场景 |
| A/B 工作量 | visual probe 的 scene FBO 检查由 52 次到 1 次，开启组 combined=76、culledPaint=80 | 相同静态背景与原始效果，不把初始化次数当每秒工作量 |
| 宿主回归 | 17 项通过，0 失败，0 跳过 | 包含 exact damage 54 检查、真实 ImDrawList 合并 7 场景、精确触摸区域、引用与 Z-order |

视觉 A/B 两份文件的 SHA256 均为 `c6f88d9658a222949f14f6784e1e16d7ca49547ac245a5845742a1c877456e94`；结果为 `visual_probe_analysis.json`。Gray 严格解码与原图为 `analyze_gray.py` / `gray_analysis.json`，混入文字或控件颜色的采样行被拒绝。

早期物理 CPU 采样看到覆盖层+采集辅助进程从 41.05% 单核到 8.62%，但旧轮 472 个触摸事件、新轮 145 个，窗口尺寸/位置和通知内容不同，因此**不能宣称同场景省了 79% CPU 或相同百分比电量**。旧私有显示没有成功承载测试 Activity，镜像无初帧，相关失败资料也不用于收益结论。新的私有视觉探针直接提供固定背景，不依赖该 Activity。

两轮 swap interval A/B 没有进一步降低帧差，正式版本保留 interval 1。`AURORA_SWAP_INTERVAL=0` 仅作诊断。120 Hz 是目标和投票；实机 Android/OEM 当前模式会改变实际呈现与源更新率。此轮已验证 120 Hz 热态路径，但没有宣称每个应用恒为 120，更没有把这台最高 120 Hz 的手机称作实测 144/165 Hz。

尚有两个源帧的相对延迟；镜像并非同一 SurfaceFlinger 合成阶段里的直接背景采样。这轮没有持续半小时的受控温升/电量实验，**不能保证长期不发热**。已经验证的是减少静止场景重复采集/GPU 工作、减少 buffer 导入分配，并降低这组截图的背景帧差。

## 用户指定的透明延迟测试

用户指出，把二级窗口四个光学参数都设为 0，可以直接比较窗内外的移动色块与帧号。新增显式诊断环境变量：

```sh
AURORA_LATENCY_TEST=1 sh /sdcard/Download/aurora_overlay.sh
```

它开启整窗玻璃，并把二级窗口模糊半径、折射高度、折射强度、色散设为 0；滑块仍可手动编辑。**正式默认效果仍是 10 / 18 / 30 / 0.70**。该开关用于排除光学形变的延迟对照，不能把零效果测试的 GPU 成本作为完整玻璃成本。红蓝移动图/Gray code 均来自本仓库的可移除测试 App `dev.aurora.fixture`，覆盖层不依赖这个 APK。

其他 A/B 开关：`AURORA_LATE_LATCH=0`、`AURORA_PANEL_BATCH=0`、`AURORA_PANEL_CULL=0`、`AURORA_SCENE_FBO_CACHE=0`。GPU timer/详细呈现诊断用 `AURORA_FRAME_DIAGNOSTICS=1`，平时关闭以免持续测量本身增加成本。`--capture-backend gpu` 可直接回到逐次 GPU snapshot。

## 交付

**后续旋转修复已经叠加到本优化，并替换了正式产物。** 最新旋转实现和证据见 [`GLASS_ROTATION_FIX.md`](GLASS_ROTATION_FIX.md)。

正式 ELF：`build/elf-arm64/bin/aurora_overlay`，10665888 bytes，SHA256 `62377f44040ccc177566b8aa94328d65b373a91f7e79f4257e76373e1aa25d90`。

自解包 SH：`dist/aurora_overlay.sh`，5467551 bytes，SHA256 `0cda0c7fe7408a54ad0392bbf82ed17b170942e5c67f4db323838975fdd16453`，正式手机位置 `/sdcard/Download/aurora_overlay.sh`。默认参数更新为物理屏、120/120、300 秒退出、无 IME。载荷 LF/无 BOM、原始字节数及解包 ELF 完整性已验证。最新部署记录 `research/rotation_run/sh_delivery.log`：**16 项通过、0 失败**，其中 T7 使用验收脚本的 30/30 参数实际跑物理屏 12 秒，351 个采集帧、触摸事件 0、退出码 0；T8 检查无残留进程/临时文件。优化阶段记录 `research/latency_power_run/sh_delivery_physical.log` 为 15 项通过；更早 `sh_delivery.log` 没有虚拟显示，T7 被跳过，不能当作实际运行证据。

手机 `/data/local/tmp/aurora_overlay` 与本机 `build/recon/aurora_overlay` 已同步最终 ELF，哈希与 SH 内嵌载荷相同。

原机 ELF 备份 `/data/local/tmp/aurora_overlay.before_power`（SHA256 `cd8429d7ef77ec2a69c6259b301e7d65cd1b80473310db3e9d4aec3e54038b88`）；本机同样保留 `research/latency_power_run/before_power.elf`。
