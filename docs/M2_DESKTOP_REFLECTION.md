# M2：桌面实时反射（含自反馈修复）

> 本文记录「让液态玻璃真实反射桌面程序画面」的实现链路、实测数据，
> 以及一个必须先解决的陷阱：**自反馈（无限镜像）**。

## 一、结论

在纯 native ELF 内，已实现「抓取真实桌面 → 上传为 GL 纹理 → 玻璃 shader 采样折射」
的完整链路，并已消除自反馈。

| 指标 | 实测值 |
| --- | --- |
| 抓帧吞吐（`screencap` 未压缩 RGBA 1080x2376） | **6.0 fps / 166.7 ms 每帧** |
| 抓帧最快 / 最慢 | 135.5 ms / 196.0 ms |
| 有效带宽 | 61.6 MB/s |
| 纹理上传 `glTexImage2D` 1080x2376 | **9.6 ms**（Adreno 830） |
| 端到端渲染（含每帧抓帧） | 4.5 ~ 4.7 fps |
| 自反馈收敛检测 | 相邻帧像素差 5.24 → 0.36 → **0.00** |

## 二、自反馈（无限镜像）：必须优先解决的陷阱

### 2.1 现象

玻璃面板内部出现无穷递归的套娃画面（德罗斯特效应），越往中心越密。

### 2.2 成因

链路是「抓整屏 → 当玻璃背景」。但**面板自己也画在屏幕上**，于是：

```text
第 N 帧:  抓屏(含面板) → 面板显示这张图
第 N+1 帧: 抓屏(含"显示着第 N 帧画面"的面板) → 面板显示这张图
...每帧再套一层 → 无穷递归
```

这不是偶然故障，而是这条链路的结构性缺陷：**采样源包含了采样者自身**。

### 2.3 修复

在上传纹理之前，**把面板自身占据的屏幕区域从帧里抹掉**，等价于「采样时看不见自己」：

```cpp
// 面板在屏幕上的位置
int px = (kScreenW - panelW) / 2;
int py = (kScreenH - panelH) / 2;
for (int row = 0; row < panelH; ++row) {
    int y = py + row;
    if (y < 0 || y >= kScreenH) continue;
    uint8_t* line = frameBuf + ((size_t)y * kScreenW + px) * 4;
    // 用左侧紧邻面板的一列像素填充，玻璃边缘折射时取到的是真实桌面，不会露空洞
    const uint8_t* src = (px > 0)
        ? frameBuf + ((size_t)y * kScreenW + (px - 1)) * 4 : nullptr;
    for (int col = 0; col < panelW; ++col) {
        uint8_t* d = line + (size_t)col * 4;
        if (src) { d[0]=src[0]; d[1]=src[1]; d[2]=src[2]; d[3]=255; }
        else     { d[0]=0; d[1]=0; d[2]=0; d[3]=255; }
    }
}
glTexSubImage2D(...);   // 抹除之后再上传
```

### 2.4 验证方法（重要）

**截图无法作为自反馈的判据**：截图必然包含本进程图层自身，看到的套娃并不能区分
「面板内部真的在递归」还是「截图这张图本身也含面板」。可靠的判据有两个：

1. **收敛检测**：连续 `glReadPixels` 读回面板中央区域，比较相邻帧差异。
   若有自反馈，每帧都会再叠加一层，差异会持续偏高；
   若正常，静止桌面下会迅速收敛到 0。
   ```
   第 1 次比对: 5.24
   第 2 次比对: 0.36
   第 3~6 次比对: 0.00   => 收敛，无自反馈
   ```
2. **占位色替换判据**：先把纹理填成纯红作为占位。若面板首帧呈红，说明纹理采样生效；
   抓帧上传后红色应被真实桌面完全替换。实测「偏红 = 0 / 总 558000」，证明桌面数据
   确实进入了面板。

## 三、抓帧路线选型（含放弃的方案）

| 方案 | 结果 |
| --- | --- |
| `screencap` 抓帧（当前采用） | ✅ 6.0 fps，实现简单，已验证 |
| `SurfaceComposerClient::mirrorSurface` | ❌ 该 ROM 上两个 `SurfaceControl*` 参数都会崩在 `getControlBlock` 的 vtable 负偏移处；传 null 则返回空 |
| `SurfaceComposerClient::getDefault()` | ❌ SIGSEGV（见 `docs/ROOT_ELF_LAYER_BLOCKER.md`） |
| `ScreenshotClient::captureDisplay` / `captureDisplayById` | ⚠️ 符号存在，但需要 `sp<IScreenCaptureListener>`；`BnScreenCaptureListener` 构造函数吃 2 个参数（this + vtable 数组），需手写整个 AIDL 基类布局，风险高，暂缓 |
| `getPhysicalDisplayIds()` | ✅ 可用。**注意调用约定**：它按值返回 `std::vector`，必须声明为「返回同布局结构体」的形式，编译器才会正确设置 x8；声明成 `void(*)(Vec3*)` 会崩在 +48（详见 `build/recon/probe_sret.cpp`） |

## 四、已知待优化项

1. **帧率**：4.5 fps 偏低，瓶颈是 `screencap` 的 166 ms。
   提升方向：改用 `captureDisplay` 零拷贝（需解决 §三 的 listener 问题），
   或让 `screencap` 常驻并通过管道连续输出。
2. **抓帧期间的渲染阻塞**：当前抓帧与渲染在同一线程顺序执行，
   抓帧期间不渲染。应改为独立采集线程 + 双缓冲纹理。
3. **抹除区域的视觉痕迹**：当前用左侧一列像素填充，面板靠近屏幕边缘时会退化为黑色。
   更稳的做法是用面板外围的环形像素做镜像填充。

## 五、复现

```powershell
clang++ --target=aarch64-linux-android24 -std=c++20 -O2 `
  -o build/recon/probe_m2_full build/recon/probe_m2_full.cpp `
  -lEGL -lGLESv3 -llog -ldl

adb push build/recon/probe_m2_full /data/local/tmp/
adb shell su -c "chmod 755 /data/local/tmp/probe_m2_full; /data/local/tmp/probe_m2_full 620 900 60"
# 参数: 面板宽 面板高 渲染帧数
```

运行约 13 秒后自动退出并打印自反馈收敛检测结果。
