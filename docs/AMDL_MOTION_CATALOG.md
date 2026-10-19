# AMDL / AndroidLiquidGlass 弹窗与交互动效目录

> 面向目标环境：**纯 C++ + OpenGL ES + Dear ImGui**，无 Compose、无 Android 动画框架、无 Skia RuntimeShader。
>
> 分析对象（本地源码，未联网）：
> - `D:\纯c图形基座\build\recon\ref_kyant\AMDL.tar\AMDL-master\` —— Kyant0/AMDL
> - `D:\纯c图形基座\build\recon\ref_kyant\AndroidLiquidGlass.tar\AndroidLiquidGlass-kmp\` —— Kyant0/AndroidLiquidGlass
>
> 文中所有 `文件:行号` 均相对上述两个根目录。

---

## 0. 阅读指南

### 0.1 参数可信度标注约定

本文严格区分三类数值，每条参数后都带标记：

| 标记 | 含义 |
|---|---|
| `[源码]` | 该数值在源码里字面写出，行号可核对 |
| `[库默认]` | 源码调用处**没写**该参数，走的是 Compose 标准库默认值。数值来自 Compose 官方库定义（不在本仓库内），可核对但需查 Compose 源码 |
| `[推断]` | 源码里**没有**这个动效或数值，是我为了让它在 ImGui 里成立而补的建议值 |

### 0.2 盘点结论（重要，先说结论）

**AMDL 本身没有实现任何弹窗、对话框、Popup、BottomSheet 或场景转场动效。** 这不是遗漏，是逐文件核实的结果：

| 检索项 | 命中数 | 证据 |
|---|---|---|
| `AlertDialog` / `BasicAlertDialog` / `Dialog` / `Popup` / `ModalBottomSheet` | **0** | 对整个 `com\kyant\amdl\` 目录做正则检索 |
| `AnimatedContent` / `AnimatedVisibility` / `updateTransition` / `animateContentSize` | **0** | 同上 |
| `animateFloatAsState` / `animateDpAsState` | **0** | 同上 |
| 场景切换动画 | **0** | `MainActivity.kt:47-53` 是对 `navBackStack.last()` 的裸 `when` 分支，切场景是**瞬切**，无交叉淡化、无位移、无缩放 |
| 主题切换过渡 | **0** | `ui\Palette.kt:13-28` 是 `isSystemInDarkTheme()` 的三目表达式，颜色**瞬变**，无插值 |

AMDL 里真正含动画的文件**只有 4 个**（其余 29 个 `.kt` 文件均为 API / 下载 / 存储 / 布局，零动画）：

| 文件 | 行数 | 动画内容 |
|---|---|---|
| `app\src\main\java\com\kyant\amdl\ui\Indication.kt` | 151 | 按压/悬停/聚焦的透明度反馈（无涟漪） |
| `app\src\main\java\com\kyant\amdl\ui\Switch.kt` | 65 | 开关拨动弹簧 |
| `app\src\main\java\com\kyant\amdl\ui\VerticalGradient.kt` | 70 | 底部渐隐遮罩（缓动曲线，非时间动画） |
| `app\src\main\java\com\kyant\amdl\scene\LoginScene.kt` | 268 | 加载进度环弹簧 + 键盘避让位移 |

**因此：任务里说的「AMDL 的弹窗/对话框动效」在 AMDL 中不存在。** 真正的弹窗/面板动效全部在 **AndroidLiquidGlass** 里，而且其 `DialogContent.kt` **也只是一个静态的玻璃对话框外观演示，没有出场/入场动画、没有拖动关闭**（详见本目录 **M-17**）。

本目录因此分两部分：
- **第一部分（§3 的 M-01 – M-07）**：AMDL 全部动效，含 2 个「缺失动效」的反面案例。
- **第二部分（§3 的 M-09 – M-21）**：AndroidLiquidGlass 的全部动效 —— **这里才是真正值得复刻的宝库**，`ControlCenterContent.kt` 是整份代码里最有价值的单文件。

> §3 的编号 `M-xx` 与 §6 汇总表、§5 优先级表一致，可直接互查。（`M-08` 是 AMDL 部分的小结表，不是独立动效。）

### 0.3 单位换算

- 源码里的 `dp` 全部按 **1dp = 1px @ density 1** 记录。ImGui 里建议引入 `kDp = io.DisplayFramebufferScale.x * dpiScale`（Android mdpi=1、xhdpi=2、xxhdpi=3）。若目标是桌面 GLES，直接令 `kDp = 1.0f` 或按窗口 DPI 缩放。
- 源码里的 `sp` 是字号，与动效无关，忽略。
- 源码里 `x.dp.toPx()` = `x * density`，本文一律写成 `x dp`。

---

## 1. ImGui 移植基础层（每节复用，先读这一节）

ImGui 没有动画系统，**一切都得自己积分**。以下 5 个基建件在后续每一节都被引用。

### 1.1 弹簧求解器（核心）

Compose 的 `spring(dampingRatio, stiffness, visibilityThreshold)` 对应阻尼谐振子：

```
x'' = -k·(x - target) - 2·ζ·ωn·x'
其中 ωn = sqrt(k)，ζ = dampingRatio
```

注意 **Compose 的 `stiffness` 直接就是 k，不是角频率**。这一点极易搞错：`stiffness = 300` 意味着 `ωn = 17.3 rad/s`（周期 ≈ 363ms），而不是 300 rad/s。

```cpp
// Spring.h —— 与 Compose spring() 语义 1:1 对应
struct Spring {
    float value   = 0.0f;
    float target  = 0.0f;
    float velocity= 0.0f;   // 单位：value-units / 秒（与 Compose initialVelocity 同量纲）
    float dampingRatio = 1.0f;    // ζ
    float stiffness    = 1500.0f; // k
    float threshold    = 0.01f;   // visibilityThreshold

    void Snap(float v)        { value = v; target = v; velocity = 0.0f; }
    void SetTarget(float t)   { target = t; }

    // 返回 false 表示已静止（可以停止请求重绘）
    bool Update(float dt) {
        if (dt <= 0.0f) return true;
        if (dt > 1.0f/30.0f) dt = 1.0f/30.0f;      // 掉帧保护，防爆炸
        const float h = 1.0f / 240.0f;             // 固定子步长
        int steps = (int)ceilf(dt / h); if (steps < 1) steps = 1; if (steps > 16) steps = 16;
        const float sub = dt / (float)steps;
        const float wn  = sqrtf(stiffness);
        for (int i = 0; i < steps; ++i) {
            const float x  = value - target;
            const float a  = -stiffness * x - 2.0f * dampingRatio * wn * velocity;
            velocity += a * sub;      // 半隐式欧拉：先速度后位置，比显式欧拉稳定得多
            value    += velocity * sub;
        }
        if (fabsf(velocity) < threshold && fabsf(value - target) < threshold) {
            value = target; velocity = 0.0f; return false;
        }
        return true;
    }
};
```

**为什么必须固定子步长**：`stiffness = 1000` 时 `ωn = 31.6 rad/s`，显式欧拉在 `dt > 2/ωn ≈ 63ms` 时发散。`dt = 1/60 = 16.7ms` 勉强安全，但 30fps 掉帧就会炸。子步到 240Hz 后可覆盖到 `stiffness ≈ 10000`。

### 1.2 弹簧参数换算速查表

| Compose 写法 | ζ | k | ωn (rad/s) | 临界阻尼 1% 稳定时间 | 出现位置 |
|---|---|---|---|---|---|
| `spring(1f, 1500f)` `[库默认]` | 1.0 | 1500 | 38.7 | ≈ 171 ms | `Animatable.animateTo()` 未传 spec 时（`GlassPlaygroundContent.kt:217-219`） |
| `spring(1f, 1000f, thr)` | 1.0 | 1000 | 31.6 | ≈ 210 ms | `DampedDragAnimation.kt:32` |
| `spring(1f, 600f)` | 1.0 | 600 | 24.5 | ≈ 271 ms | `Switch.kt:35` |
| `spring(1f, 500f, 0.01f)` | 1.0 | 500 | 22.4 | ≈ 297 ms | `Indication.kt:146` |
| `spring(1f, 300f, 0.01f)` | 1.0 | 300 | 17.3 | ≈ 384 ms | `ControlCenterContent.kt:174,182` |
| `spring(1f, 300f)` | 1.0 | 300 | 17.3 | ≈ 384 ms | `LoginScene.kt:124` |
| `spring(1f, 300f, 0.5f)` | 1.0 | 300 | 17.3 | ≈ 384 ms | `LiquidBottomTabs.kt:118` |
| `spring(0.5f, 300f, …)` | 0.5 | 300 | 17.3 | ≈ 531 ms（会过冲 ≈ 16%） | `ControlCenterContent.kt:172`、`InteractiveHighlight.kt:28,30`、`DampedDragAnimation.kt:34` |
| `spring(0.6f, 250f, 0.001f)` | 0.6 | 250 | 15.8 | ≈ 485 ms | `DampedDragAnimation.kt:38`（scaleX） |
| `spring(0.7f, 250f, 0.001f)` | 0.7 | 250 | 15.8 | ≈ 415 ms | `DampedDragAnimation.kt:40`（scaleY） |

临界阻尼稳定时间公式：`t ≈ 6.64 / ωn`（ζ=1）；欠阻尼：`t ≈ 4.6 / (ζ·ωn)`。

**给 ImGui 的一句话建议**：默认用 `ζ=1.0, k=300`（≈380ms，稳重）；想要 iOS 味道用 `ζ=0.5, k=300`（有回弹）；跟手拖拽用 `ζ=0.5~0.7, k=250~300`。

### 1.3 速度追踪（拖动关闭必需）

源码用 Compose 的 `VelocityTracker`（`DampedDragAnimation.kt:10,55,130-134`）：最小二乘二次拟合最近 100ms 的采样点，求 t=0 处导数。ImGui 侧给一个够用的实现：

```cpp
struct VelocityTracker2D {
    struct S { double t; float x, y; };
    S buf[32]; int n = 0;
    void Reset() { n = 0; }
    void Add(double t, float x, float y) {
        if (n == 32) { memmove(buf, buf+1, sizeof(S)*31); --n; }
        buf[n++] = { t, x, y };
    }
    void Calc(float& vx, float& vy, double now, double horizon = 0.1) {
        vx = vy = 0.0f;
        // 找 horizon 内的最早样本，做差分（等价于一次线性拟合）
        int i0 = n - 1;
        while (i0 > 0 && now - buf[i0].t < horizon) --i0;
        if (n - i0 < 2) return;
        double dt = buf[n-1].t - buf[i0].t;
        if (dt <= 1e-6) return;
        vx = (float)((buf[n-1].x - buf[i0].x) / dt);
        vy = (float)((buf[n-1].y - buf[i0].y) / dt);
    }
};
```

ImGui 输入侧取点：`io.MouseDelta`（每帧位移，px/frame）+ `io.DeltaTime` → `vel_px_s = MouseDelta / DeltaTime`。也可以直接喂 `Add(ImGui::GetTime(), io.MousePos.x, io.MousePos.y)`。

### 1.4 ImGui 能力映射总表

| 需要的效果 | Compose 里的做法 | ImGui 对应能力 |
|---|---|---|
| 时间步进 | `withFrameNanos` | `ImGui::GetIO().DeltaTime`（自己 clamps 到 ≤1/30） |
| 弹簧/缓动 | `Animatable` / `animateFloatAsState` | §1.1 的 `Spring` 结构体，每帧 `Update(dt)` |
| 窗口位置 | `Modifier.graphicsLayer{translationX/Y}` | `ImGui::SetNextWindowPos(pos, ImGuiCond_Always)` |
| 窗口尺寸 | `Modifier.layout` / `size` | `ImGui::SetNextWindowSize(sz, ImGuiCond_Always)` |
| 整窗透明 | `Modifier.alpha` | `ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a)` |
| 圆角 | `RoundedRectangle(x.dp)` | `ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, r)` |
| 圆角变化的矩形绘制 | `drawRect(..., cornerRadius)` | `ImDrawList::AddRectFilled(p0,p1,col,r,ImDrawFlags_RoundCornersAll)` |
| 遮罩层 scrim | `Modifier.drawWithContent{ drawRect(dimColor) }` | `ImGui::GetForegroundDrawList()->AddRectFilled(全屏, IM_COL32(0,0,0,alpha*255))`（或 `GetBackgroundDrawList`，看层序） |
| 触发重绘判定 | 框架自动 | `ImGui::GetIO().WantCaptureMouse` + 自己维护 `needsRedraw`（若用 `ImGuiBackendFlags`/手写循环，务必在弹簧未静止时持续 `glfwPostEmptyEvent` 或 disabled vsync 连续渲染） |
| 缩放整块 UI | `Modifier.graphicsLayer{scaleX/Y}` | **ImGui 做不到把窗口整体缩放**。三条路：① 只插值窗口 rect（内容重排，最省，见 §2.6）；② 用 `ImDrawList::AddCallback` 在顶点着色器里做 2D 仿射（文字也跟着缩放，推荐）；③ 把对话框渲染进 FBO，再当贴图画（能顺便做模糊，最贵） |
| 背景模糊 | `Backdrop` + `BlurEffect` | 自建 FBO + 两趟高斯/降采样链，再画带圆角 SDF 的贴图四边形（§1.5） |
| 圆角 SDF 遮罩 | `RoundedRectangle` shape + shader | 顶点着色器传本地 UV + halfSize + radius，片元里算 SDF |
| 折射/镜片（lens） | `lens()` RuntimeShader | 从圆角 SDF 梯度求法线，UV 偏移采样模糊贴图（§1.5） |
| 拖动 | `pointerInput{ detectDragGestures }` | `ImGui::IsItemActive() && ImGui::IsMouseDragging(0)` + `io.MouseDelta` |
| 拖动阈值判定 | `onDragStopped(velocity)` | §1.3 速度追踪 + 位移阈值 |
| 高光/描边 | `Highlight` + `BlendMode.Plus` | `AddRect` + 混合模式 `GL_ONE, GL_ONE`（Plus 加法混合） |

### 1.5 模糊 / 圆角 / 镜片的 GLES 实现路径（§3 多处依赖）

AndroidLiquidGlass 的玻璃效果链路是 `vibrancy + blur(r) + lens(h, amount)`。GLES 侧的最小可行方案：

1. **背景捕获**：把整个场景（壁纸 + 已绘制内容）渲染进 `FBO_scene`。
2. **降采样 + 模糊**：`FBO_scene → FBO_blurA (1/4)` → 横向高斯 → `FBO_blurB` → 纵向高斯。半径小时直接 9-tap 高斯；半径大用 mip 链。
3. **圆角矩形绘制**：画一个覆盖目标 rect 的四边形，片元着色器：
   ```glsl
   // vLocal: 以矩形中心为原点的像素坐标; vHalf: 矩形半尺寸; uRadius: 圆角半径
   float sdRoundRect(vec2 p, vec2 b, float r) {
       vec2 q = abs(p) - b + vec2(r);
       return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
   }
   void main() {
       float d = sdRoundRect(vLocal, vHalf, uRadius);
       float mask = 1.0 - smoothstep(-1.0, 1.0, d);   // 1px 抗锯齿
       if (mask <= 0.0) discard;
       vec2 uv = (vLocal + vHalf) / uSize + uRectOffset;  // 取背景图对应位置
       // 折射：用 SDF 梯度当法线
       vec2 n = normalize(vec2(
           sdRoundRect(vLocal + vec2(1,0), vHalf, uRadius) - d,
           sdRoundRect(vLocal + vec2(0,1), vHalf, uRadius) - d) + 1e-5);
       float prof = smoothstep(-uRefractionHeight, 0.0, d);   // 边缘 1，中心 0
       uv -= n * uRefractionAmount * prof;
       vec3 col = texture(uBlurTex, uv).rgb;
       col = mix(col, uSurfaceColor.rgb, uSurfaceColor.a);
       FragColor = vec4(col, mask);
   }
   ```
   其中 `uRefractionHeight` 对应源码的 `refractionHeight`，`uRefractionAmount` 对应 `refractionAmount`。源码里 `setFloatUniform("refractionAmount", -refractionAmount)`（`Lens.kt:49`）说明其方向取负，移植时若发现外扩改成内缩，把符号翻回来。
4. **色散（chromaticAberration）**：`uRefractionAmount` 乘 1.0 / 0.98 / 1.02 采样三次 R/G/B 通道再合成（源码用独立 shader `RefractionWithDispersion`，`Lens.kt:38-43`）。
5. **高光（Highlight）**：加法混合画一圈圆角矩形描边。参考默认值 `width = 0.5dp`、`blurRadius = width/2`（`Highlight.kt:11-12`）、颜色 `White @ 0.38`（`HighlightStyle.Plain`，`HighlightStyle.kt:34`）或 `White @ 0.5` + `angle 45°` + `falloff 1`（`HighlightStyle.Default`，`HighlightStyle.kt:46-49`）。
6. **vibrancy**：近似为「提高饱和度 + 轻微提亮」。可用 `col = mix(vec3(luma), col, 1.5)` + `col *= 1.05` 近似 `[推断]`。

---

## 2. 动效逐条目录 —— 第一部分：AMDL

### M-01 按压 / 悬停 / 聚焦的透明高亮（无涟漪 Indication）

- **来源**：`app\src\main\java\com\kyant\amdl\ui\Indication.kt`（全文件 151 行）
- **视觉描述**：不是 Android 那种从触点扩散的圆形涟漪，而是**整个组件矩形整体淡入一层纯色蒙版**。按下瞬间**直接跳到**目标透明度（不渐变），松手后**至少保持 80ms** 再弹回 0。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 弹簧 | `spring(dampingRatio=1.0, stiffness=500, visibilityThreshold=0.01)` | `[源码] Indication.kt:146` |
| 按下透明度 | `0.2` | `[源码] Indication.kt:148` |
| 悬停透明度 | `0.15` | `[源码] Indication.kt:149` |
| 聚焦透明度 | `0.2` | `[源码] Indication.kt:150` |
| 最短按压保持 | `80 ms` | `[源码] Indication.kt:151` |
| 按下时是否 snap | 是（`snapTo`，跳过动画） | `[源码] Indication.kt:62-63, 96` |
| 优先级 | press > hover > focus | `[源码] Indication.kt:48-55` |
| 绘制 | 全矩形 `drawRect(color, alpha=alpha)` | `[源码] Indication.kt:138-141` |

- **关键行为细节**（`Indication.kt:70-87`）：松手后起一个 `releaseJob`，等 `max(0, 80ms - 已按住时长)` 再回弹；若期间又按下则取消。这意味着**快速点击也会有完整的 80ms 高亮**，手感不"跳"。
- **ImGui 移植**：
  ```cpp
  struct Indication {
      Spring alpha;                 // k=500, zeta=1.0, thr=0.01
      bool  down=false, hovered=false, focused=false;
      double pressStart = 0.0;
      float Target() const { return down ? 0.20f : hovered ? 0.15f : focused ? 0.20f : 0.0f; }
      void Frame(bool isDown, bool isHovered, bool isFocused, float dt) {
          if (isDown && !down) { alpha.Snap(0.20f); pressStart = ImGui::GetTime(); }  // 按下 snap
          down=isDown; hovered=isHovered; focused=isFocused;
          double held = ImGui::GetTime() - pressStart;
          if (!down && held < 0.080) alpha.SetTarget(alpha.value);   // 80ms 内不回弹
          else                        alpha.SetTarget(Target());
          alpha.Update(dt);
      }
      void Draw(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col, float rounding) {
          if (alpha.value <= 0.001f) return;
          dl->AddRectFilled(p0, p1,
              (col & 0x00FFFFFF) | ((ImU32)(alpha.value*255.0f) << IM_COL32_A_SHIFT), rounding);
      }
  };
  ```
  用法：每个可点控件持有 `Indication`；用 `ImGui::IsItemHovered()` / `ImGui::IsItemActive()` / `ImGui::IsItemFocused()` 喂三个布尔量。注意 ImGui 的 `ButtonBehavior` 在 `IsItemActive()` 时就已经在按下态，能直接对应。
- **难度**：**简单**（纯 alpha 弹簧，无几何变化）

---

### M-02 开关拨动（Switch）

- **来源**：`app\src\main\java\com\kyant\amdl\ui\Switch.kt:33-63`
- **视觉描述**：56×32dp 胶囊轨道，24dp 圆形滑块从左侧 4dp 滑到右侧 28dp；同时轨道颜色从 `background` 渐变到 `accent`（通过叠加 `accent` 的 alpha 实现，而不是颜色插值）。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 弹簧 | `spring(dampingRatio=1.0, stiffness=600)`（无显式阈值 → 用库默认 `visibilityThreshold`） | `[源码] Switch.kt:35`；默认阈值 `[库默认]` |
| 轨道尺寸 | 56 × 32 dp，`Capsule()` | `[源码] Switch.kt:46-47` |
| 滑块尺寸 | 24 × 24 dp，`CircleShape` | `[源码] Switch.kt:57,62` |
| 滑块 X 起止 | `lerp(4dp, 28dp, t)` → 起点 4，终点 28，行程 **24 dp** | `[源码] Switch.kt:59` |
| 滑块 Y | 固定 `4 dp` | `[源码] Switch.kt:60` |
| 轨道着色 | 先画 `background` 实色，再叠 `accent.copy(alpha = t)` | `[源码] Switch.kt:50-51` |
| 动画驱动 | `snapshotFlow(checked).drop(1).collectLatest` —— **丢弃初始值**，避免首帧动画 | `[源码] Switch.kt:36-40` |

- **ImGui 移植**：
  ```cpp
  Spring sw; sw.stiffness = 600.0f; sw.dampingRatio = 1.0f; sw.threshold = 0.01f;
  // 点击时 sw.SetTarget(on ? 1.f : 0.f)
  float t = sw.value;
  ImVec2 p = ImGui::GetCursorScreenPos();
  dl->AddRectFilled(p, p + ImVec2(56*kDp, 32*kDp), bgCol, 16*kDp);
  dl->AddRectFilled(p, p + ImVec2(56*kDp, 32*kDp), accentColWithAlpha(t), 16*kDp); // 叠加
  float x = p.x + ImLerp(4*kDp, 28*kDp, t);
  dl->AddCircleFilled(ImVec2(x + 12*kDp, p.y + 4*kDp + 12*kDp), 12*kDp, cardCol, 32);
  ```
  注意「叠加 accent 而非颜色 lerp」这一点要照抄：背景是 `Palette.background`（暗色=纯黑，亮色=#F2F2F7），叠加半透明 accent 出来的中间色比直接 `ImLerp` 两个颜色更"脏"一点，正是原版观感。
- **难度**：**简单**

---

### M-03 加载进度环（弧线扫描）

- **来源**：`app\src\main\java\com\kyant\amdl\scene\LoginScene.kt:121-142`
- **视觉描述**：48dp 圆环，从 12 点方向顺时针扫出弧线，扫描角随 WebView 加载进度走。进度跳变（WebView 回调是整数百分比）时用弹簧平滑。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 弹簧 | `spring<Float>(dampingRatio=1.0, stiffness=300)` | `[源码] LoginScene.kt:124` |
| 圆环尺寸 | 48 dp | `[源码] LoginScene.kt:132` |
| 起始角 | `-90f`（12 点方向） | `[源码] LoginScene.kt:137` |
| 扫描角 | `360f * progress` | `[源码] LoginScene.kt:138` |
| 描边 | `Stroke(2dp, cap = StrokeCap.Round)` | `[源码] LoginScene.kt:140` |
| 进度源 | `webViewProgress = newProgress / 100f`（0..1） | `[源码] LoginScene.kt:257-259` |

- **ImGui 移植**：`ImDrawList::PathArcTo(center, radius, a_min, a_max, 0)` + `PathStroke(col, 0, 2*kDp)`。圆头线帽 ImGui 没有原生支持 —— 在弧的两个端点各 `AddCircleFilled(端点, 1*kDp, col, 8)` 补上即可。
- **难度**：**简单**（弧线绘制）+ **简单**（弹簧）

---

### M-04 底部渐隐遮罩（列表底部淡出）

- **来源**：`app\src\main\java\com\kyant\amdl\ui\VerticalGradient.kt`（全文件）+ 使用点 `scene\MainScene.kt:178-190`
- **视觉描述**：屏幕底部 104dp 高的竖直渐变，把滚动内容淡出到背景色，下方再接一块纯色安全区。**这是静态绘制的渐变，不是时间动画**，但属于"视觉手段"清单，且难度低收益高。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 渐变高度 | `104 dp` | `[源码] MainScene.kt:182` |
| 起始透明度 | `0f`（顶部完全透明） | `[源码] MainScene.kt:183` |
| 结束透明度 | `1f` | `[源码] MainScene.kt:183` |
| 缓动 | `EaseOut` | `[源码] MainScene.kt:183` |
| 基础色 | `Palette.background.copy(0.9f)`（alpha 0.9） | `[源码] MainScene.kt:178` |
| 每像素 alpha | `lerp(startOpacity, stopOpacity, EaseOut.transform(t))`，`t = y/(H-1)` | `[源码] VerticalGradient.kt:20-22, 60-64` |
| `EaseOut` 曲线 | `CubicBezierEasing(0.0, 0.0, 0.58, 1.0)`（等价 CSS `ease-out`） | `[库默认]`（Compose `animation.core.Easing`，本仓库无定义） |

- **ImGui 移植**：`ImDrawList::AddRectFilledMultiColor(p0, p1, topCol, topCol, botCol, botCol)`。要精确复刻 `EaseOut` 需要在 8~16 段上手动切分矩形逐段插值 alpha（`AddRectFilledMultiColor` 是线性的）：
  ```cpp
  const int N = 12;
  for (int i = 0; i < N; ++i) {
      float t0 = i/(float)N, t1 = (i+1)/(float)N;
      float a0 = EaseOut(t0) * 0.9f, a1 = EaseOut(t1) * 0.9f;
      dl->AddRectFilledMultiColor(
          ImVec2(x0, y0 + t0*H), ImVec2(x1, y0 + t1*H),
          A(col,a0), A(col,a0), A(col,a1), A(col,a1));
  }
  // EaseOut(t) = cubic-bezier(0,0,0.58,1) —— 预计算 33 点 LUT 查表即可
  ```
- **难度**：**简单**

---

### M-05 键盘避让位移

- **来源**：`app\src\main\java\com\kyant\amdl\scene\LoginScene.kt:82, 90-92`
- **视觉描述**：输入法弹出时整个页面向上位移键盘高度的一半。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 位移量 | `-imeBottomPadding * 0.5f` | `[源码] LoginScene.kt:91` |
| 有无动画 | **无**，直接跟随 `imePadding` 变化 | `[源码] LoginScene.kt:90-92`（`graphicsLayer` 里直接赋值，无 `Animatable`） |

- **ImGui 移植**：桌面端无 IME 高度概念。若做软键盘/输入面板，建议**加**弹簧（原版没有）：`k = 300, ζ = 1.0` `[推断]`，目标 = `-panelHeight * 0.5f`。位移整体 UI 可用 `ImGui::SetNextWindowPos` 或对绘制做 `ImDrawList::AddCallback` 平移。
- **难度**：**简单**

---

### M-06 ⚠️ 缺失动效：场景切换是瞬切（反面案例）

- **来源**：`app\src\main\java\com\kyant\amdl\MainActivity.kt:47-53`
- **现状**：
  ```kotlin
  when (appState.navBackStack.last()) {
      Scene.SetupWizard -> SetupWizardScene(appState)
      Scene.Login       -> LoginScene(appState)
      Scene.Main        -> MainScene(appState)
      Scene.Settings    -> SettingsScene(appState)
      Scene.Language    -> LanguageScene(appState)
  }
  ```
  没有任何 `AnimatedContent` / `Crossfade` / `SharedTransitionLayout`。返回栈变化 = 一帧内硬切，**没有转场**。
- **这为什么值得写进目录**：它是"最值得补的一个动效"，而且是 ImGui 里最容易做到比原版更好的地方。
- **ImGui 移植建议** `[推断]`：

  | 层 | 参数建议 | 说明 |
  |---|---|---|
  | 新页面入场 | 弹簧 `ζ=1.0, k=300`；`translationX` 从 `+32dp` → `0`；`alpha` 0→1 | "从右侧滑入" |
  | 旧页面退场 | 弹簧 `ζ=1.0, k=300`；`translationX` 从 `0` → `-16dp`；`alpha` 1→0 | 退场位移小于入场，是 Material 惯例 |
  | 层级 | 新页面在上 | 旧页面在下方被覆盖 |
  | 遮罩 | 不需要 | |

  状态机：`navStack` 存 `{sceneId, Spring enter, Spring exit}`；`push` 时给新场景 `enter.Snap(0); enter.SetTarget(1)`，给被压住的老场景 `exit.SetTarget(1)`；`pop` 反向。转场中若再次 push，**不要重置进度**，让它从中途继续（弹簧天然支持打断）——这正是 `DampedDragAnimation` 反复强调的可打断特性。
- **难度**：**中等**（需要引入"两个场景同时渲染"的架构；ImGui 里就是同一帧画两次窗口，用 alpha 混合）

---

### M-07 ⚠️ 缺失动效：主题色切换是瞬变（反面案例）

- **来源**：`app\src\main\java\com\kyant\amdl\ui\Palette.kt:13-28`
- **现状**：全部靠 `if (isSystemInDarkTheme()) ... else ...` 三目，**颜色瞬时切换，无插值**。四个 token：

  | token | 暗色 | 亮色 | 来源 |
  |---|---|---|---|
  | `background` | `Color.Black` = `#000000` | `#F2F2F7` | `Palette.kt:13` |
  | `content` | `Color.White` = `#FFFFFF` | `Color.Black` = `#000000` | `Palette.kt:18` |
  | `card` | `#1D1D1F` | `#FFFFFF` | `Palette.kt:23` |
  | `accent` | `#0A84FF` | `#007AFF` | `Palette.kt:28` |

- **对比参考**：AndroidLiquidGlass 里**有**一个真正做主题过渡的实现 —— `AdaptiveLuminanceGlassContent.kt:70-78` 用 `tween(1000)` 对亮度与文字颜色做 1 秒插值（详见 M-19）。这是本仓库唯一显式的 `tween`。
- **ImGui 移植建议** `[推断]`：给每个 token 一个 `float t`（0=亮，1=暗）弹簧，`ζ=1.0, k=300`（≈380ms）或照抄 LiquidGlass 的 `tween(1000)` 线性 + 缓动。颜色用 `ImLerp` 逐通道插值（注意 ImGui 的颜色是 sRGB 空间直接 lerp，与 Compose 的 `Color` lerp 行为一致，都在 gamma 空间）。
- **难度**：**简单**

---

### M-08 AMDL 动效盘点小结

| ID | 名称 | 弹簧 | 难度 | 是否值得复刻 |
|---|---|---|---|---|
| M-01 | Indication 高亮 | `1.0 / 500` | 简单 | ★★★★★ 必做，所有控件的反馈基座 |
| M-02 | Switch 拨动 | `1.0 / 600` | 简单 | ★★★★☆ |
| M-03 | 进度环 | `1.0 / 300` | 简单 | ★★★☆☆ |
| M-04 | 底部渐隐 | 无（静态） | 简单 | ★★★★☆ |
| M-05 | 键盘避让 | 无 | 简单 | ★★☆☆☆ |
| M-06 | 场景切换 | **缺失** | 中等 | ★★★★★ 需要自己设计 |
| M-07 | 主题切换 | **缺失** | 简单 | ★★★★☆ 参考 M-19 |

---

## 3. 动效逐条目录 —— 第二部分：AndroidLiquidGlass（真正的宝库）

### M-09 ⭐ 控制中心进出场（拖拽 + 遮罩 + 模糊 + 缩放 + 液体拉伸）

> **这是整份源码里最值得复刻的一个动效。** 一个文件里同时包含：可打断的拖拽进度、速度决定的落点、遮罩淡入、背景模糊、过拖时的橡皮筋缩放与行距拉伸、液体镜片随进度生长。

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\ControlCenterContent.kt`（全文件 443 行）
- **视觉描述**：
  1. 一个"控制中心"面板（一堆玻璃方块）。`progress = 1` 是完全显示，`progress = 0` 是完全隐藏。
  2. **向上拖** → `progress` 减小 → 面板 `translationY` 向上移 48dp、整体 `alpha` 按 `EaseIn` 淡出、背景模糊从 4dp 降到 0、全屏遮罩从黑 40% 淡到 0。松手按速度决定是回 0 还是回 1。
  3. **向下拖过 1** → 进入过拖区（`progress > 1`），面板**等比例缩小**（`scaleX/Y /= 1 + 0.1*超出量`），方块之间的**行距被撑开**（16dp → 最多 48dp），产生"被拉长"的液体感；超出量经过 `ProgressConverter` 的指数饱和，永远不会失控。
  4. 玻璃块的镜片折射半径随进度生长：`lens(24dp*progress, 48dp*progress, depthEffect=true)`。

- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 主进度动画 | `Animatable(1f)`（初始 = 完全显示） | `[源码] :83` |
| 安全进度动画 | `Animatable(1f)`，被 clamp 到 `0..1` | `[源码] :84, :156` |
| **拖拽映射** | `targetProgress = value + delta / maxDragHeight`，`maxDragHeight = 1000f` | `[源码] :95, :150` |
| **过拖转换** | `p>1` 时 → `1 + (1 - exp(-(p-1)))`（指数饱和，上界 2.0） | `[源码] :89-92` + `ProgressConverter.kt:13-16` |
| **松手落点判定** | `velocity < 0 → 0`；`velocity > 0 → 1`；否则 `value < 0.5 ? 0 : 1` | `[源码] :162-166` |
| **展开回弹弹簧** | `spring(dampingRatio=0.5, stiffness=300, visibilityThreshold=0.5f/1000f)` —— **有回弹** | `[源码] :172` |
| **收起回弹弹簧** | `spring(dampingRatio=1.0, stiffness=300, visibilityThreshold=0.01f)` —— **无回弹** | `[源码] :174` |
| 安全进度弹簧 | `spring(1.0, 300, 0.01f)` | `[源码] :182` |
| 速度传递 | `animateTo(target, spec, initialVelocity = velocity / maxDragHeight)` | `[源码] :176` |
| **面板位移** | `translationY = -48dp * (1 - progress)` | `[源码] :110` |
| **面板透明度** | `alpha = EaseIn.transform(safeProgress)` | `[源码] :111` |
| `EaseIn` 曲线 | `CubicBezierEasing(0.42, 0.0, 1.0, 1.0)`（等价 CSS `ease-in`） | `[库默认]`（Compose 库定义） |
| **过拖缩放** | `scaleX /= 1 + 0.1*max(progress-1, 0)`；Y 同理 | `[源码] :112-113` |
| 全屏遮罩色 | `Color.Black.copy(0.4f)` | `[源码] :65` |
| **遮罩透明度** | `alpha = 0.4 * safeProgress`（`drawRect(dimColor.copy(dimColor.alpha * progress))`） | `[源码] :192` |
| **背景模糊** | `BlurEffect(4dp*progress, 4dp*progress)` | `[源码] :197-200` |
| 玻璃块镜片 | `lens(24dp*progress, 48dp*progress, depthEffect=true)` | `[源码] :119-123` |
| 玻璃块高明度 | `vibrancy()`，表面 `Color.Black.copy(0.05f)` | `[源码] :118, :64` |
| 方块尺寸 / 间距 | `itemSize = 68dp`，`itemSpacing = 16dp` | `[源码] :67-68` |
| 方块圆角 | `RoundedRectangle(itemSize/2) = 34dp`（全圆角胶囊） | `[源码] :70` |
| 内层图标尺寸 / 缩放 | `innerItemSize = 56dp`，`innerItemIconScale = 0.8f` | `[源码] :72, :74` |
| **大行距拉伸** | `height = 16dp + 32dp * max(progress-1, 0)` → 16dp ~ 48dp | `[源码] :129-134` |
| **小行距拉伸** | `height = 16dp + 16dp * max(progress-1, 0)` → 16dp ~ 32dp | `[源码] :139-144` |
| 顶部内边距 | `80 dp` | `[源码] :206` |
| 高光角度 | `uiSensor.gravityAngle`（重力感应），`falloff = 2f` | `[源码] :99-106` |

- **`ProgressConverter` 的确切公式**（`utils\ProgressConverter.kt:13-16`）：
  ```kotlin
  ProgressConverter { progress -> (1f - exp(-abs(progress))) * progress.sign }
  ```
  —— 即 `f(x) = sign(x) * (1 - e^{-|x|})`。`x=1` → 0.632；`x=2` → 0.865；`x=3` → 0.950。**这是"过拖永远拉不长"的关键**，ImGui 里必须照抄。

- **ImGui 移植（完整状态机）**：
  ```cpp
  struct ControlCenterMotion {
      Spring enter;        // 主进度，1 = 完全显示。k=300
      Spring safe;         // clamp 到 0..1，驱动淡化/模糊/遮罩。k=300
      bool   dragging = false;
      float  dragStartValue = 1.0f;
      float  dragStartY = 0.0f;
      VelocityTracker2D vt;
      const float maxDragHeight = 1000.0f;

      void Init() { enter.Snap(1); safe.Snap(1); }

      static float Convert(float p) {                 // ProgressConverter.kt:15
          return (1.0f - expf(-fabsf(p))) * (p < 0 ? -1.0f : 1.0f);
      }
      float Progress() const {                        // ControlCenterContent.kt:85-94
          float p = enter.value;
          if (p < 0.0f) return Convert(p);
          if (p <= 1.0f) return p;
          return 1.0f + Convert(p - 1.0f);
      }

      void Frame(float dt, bool mouseDown, ImVec2 mousePos) {
          if (mouseDown && !dragging) {
              dragging = true; vt.Reset();
              dragStartValue = enter.value; dragStartY = mousePos.y;
          }
          if (dragging) {
              vt.Add(ImGui::GetTime(), mousePos.x, mousePos.y);
              float delta = mousePos.y - dragStartY;              // Compose: 向下为正
              float target = dragStartValue + delta / maxDragHeight;
              enter.Snap(target);
              safe.Snap(ImClamp(target, 0.0f, 1.0f));
          }
          if (!mouseDown && dragging) {
              dragging = false;
              float vx, vy; vt.Calc(vx, vy, ImGui::GetTime());
              float target = (vy < 0.0f) ? 0.0f : (vy > 0.0f) ? 1.0f
                                                          : (enter.value < 0.5f ? 0.0f : 1.0f);
              if (target > 0.5f) { enter.dampingRatio = 0.5f; enter.threshold = 0.5f / maxDragHeight; }
              else               { enter.dampingRatio = 1.0f; enter.threshold = 0.01f; }
              enter.velocity = vy / maxDragHeight;                // ControlCenterContent.kt:176
              enter.SetTarget(target);
              safe.SetTarget(target);
          }
          enter.Update(dt);
          if (!dragging) {
              safe.value = ImClamp(safe.value, 0.0f, 1.0f);
              safe.SetTarget(ImClamp(enter.target, 0.0f, 1.0f));
              safe.Update(dt);
          }
      }
  };

  // 渲染侧
  const float p     = cc.Progress();                       // 可 > 1
  const float sp    = cc.safe.value;                       // 0..1
  const float easeIn = sp * sp * sp;                       // 近似 CubicBezier(0.42,0,1,1) [推断]
  const float over   = ImMax(p - 1.0f, 0.0f);

  float panelAlpha = easeIn;                               // :111
  float panelDY    = -48*kDp * (1.0f - p);                 // :110
  float panelScale = 1.0f / (1.0f + 0.1f * over);          // :112-113
  float scrimA     = 0.40f * sp;                           // :192
  float blurR      = 4.0f*kDp * sp;                        // :197
  float lensH      = 24.0f*kDp * sp, lensA = 48.0f*kDp * sp; // :119-123
  float gapLarge   = 16.0f*kDp + 32.0f*kDp * over;         // :131
  float gapSmall   = 16.0f*kDp + 16.0f*kDp * over;         // :141
  ```
  **渲染顺序**：① 背景场景 → FBO；② 按 `blurR` 模糊该 FBO；③ 全屏画 scrim `AddRectFilled(IM_COL32(0,0,0, scrimA*255))`；④ 在模糊贴图上按 `lensH/lensA` 做折射，画每个玻璃方块（圆角 34dp），叠加 `panelDY`、围绕面板中心施加 `panelScale`、乘 `panelAlpha`。
  `panelScale` 的实现：把每个方块的 rect 相对面板中心做 `p' = center + (p - center) * panelScale`，不要试图缩放 ImGui 窗口。
- **难度**：**困难**（数值全都好抄，难在模糊 FBO、折射着色器和"面板整体缩放"这三件事。可以先把模糊/折射降级成"半透明+描边"，几何与弹簧部分 100% 还原，收益已经很大）

---

### M-10 通用阻尼拖拽动画（DampedDragAnimation）—— 所有交互件的引擎

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\utils\DampedDragAnimation.kt`（全文件 137 行）
- **视觉描述**：不是一个具体动效，而是被 **LiquidToggle / LiquidSlider / LiquidBottomTabs** 三处共用的"拖拽 + 弹簧 + 速度 + 按压缩放"基础设施。值得**整体移植成一个 C++ 类**。
- **确切参数（全部 5 条 spring spec，逐字抄）**：

| spec | 值 | 来源 |
|---|---|---|
| `valueAnimationSpec`（主值） | `spring(dampingRatio=1.0, stiffness=1000, visibilityThreshold=<构造参数>)` | `[源码] :31-32` |
| `velocityAnimationSpec`（速度平滑） | `spring(dampingRatio=0.5, stiffness=300, visibilityThreshold*10)` | `[源码] :33-34` |
| `pressProgressAnimationSpec`（按压进度） | `spring(dampingRatio=1.0, stiffness=1000, visibilityThreshold=0.001)` | `[源码] :35-36` |
| `scaleXAnimationSpec` | `spring(dampingRatio=0.6, stiffness=250, visibilityThreshold=0.001)` | `[源码] :37-38` |
| `scaleYAnimationSpec` | `spring(dampingRatio=0.7, stiffness=250, visibilityThreshold=0.001)` | `[源码] :39-40` |

| 其它参数 | 值 | 来源 |
|---|---|---|
| `velocityAnimation` 初值 / 阈值 | `0f` / `5f` | `[源码] :44-45` |
| `valueAnimation` 阈值 | = `visibilityThreshold` 构造参数 | `[源码] :42-43` |
| `progress` | `(value - range.start) / (range.end - range.start)` | `[源码] :58` |
| 松手前等待阈值 | `(range.end - range.start) * 0.025f`（**区间的 2.5%**） | `[源码] :97` |
| 速度归一化 | `tracker.calculateVelocity().x / (range.end - range.start)` | `[源码] :134` |
| 按下行为 | 并行启动 pressProgress→1、scaleX→pressedScale、scaleY→pressedScale | `[源码] :84-91` |
| 松手行为 | 若主值未到目标，**先等**主值进入目标 2.5% 区间，**再**并行回落 press/scale | `[源码] :93-106` |

- **两个非常值得学的手感细节**：
  1. **`release()` 的"等一等再弹回"**（`:96-101`）：松手时如果滑块还在滑向目标，先 `snapshotFlow{value}.filter{ |v-target| < 2.5%区间 }.first()` 等它接近，才开始把 `pressProgress` 与 `scaleX/Y` 收回去。效果是**"先落位，再松开手指"**，避免"手指松了但东西还在飞"的割裂感。
  2. **速度是独立平滑的**（`:129-136`）：速度不是直接量出来的，而是喂给一个 `spring(0.5, 300)` 的 `Animatable` 平滑跟踪——所以它天然带一点迟滞，用来拉伸形变时不会抖。
- **ImGui 移植**：把 §1.1 的 `Spring` 扩成一个类，字段与源码一一对应：
  ```cpp
  class DampedDragAnimation {
  public:
      Spring value, velocityAnim, pressProgress, scaleX, scaleY;
      float  rangeStart = 0.0f, rangeEnd = 1.0f;
      bool   isDragging = false;

      void Configure(float initialValue, float lo, float hi, float visThr,
                     float initialScale, float pressedScale) {
          rangeStart = lo; rangeEnd = hi;
          value          = { initialValue, initialValue, 0, 1.0f, 1000.0f, visThr };
          velocityAnim   = { 0,0,0, 0.5f,  300.0f, visThr*10.0f };
          pressProgress  = { 0,0,0, 1.0f, 1000.0f, 0.001f };
          scaleX         = { initialScale, initialScale, 0, 0.6f, 250.0f, 0.001f };
          scaleY         = { initialScale, initialScale, 0, 0.7f, 250.0f, 0.001f };
          m_pressedScale = pressedScale; m_initialScale = initialScale;
      }
      float Progress() const { return (value.value - rangeStart) / (rangeEnd - rangeStart); }

      void Press() {                       // :84-91
          pressProgress.SetTarget(1.0f);
          scaleX.SetTarget(m_pressedScale);
          scaleY.SetTarget(m_pressedScale);
      }
      void Release() {                     // :93-106
          m_pendingRelease = true;         // 帧循环里检查 2.5% 条件后再真正回落
      }
      void Update(float dt) {
          if (m_pendingRelease) {          // :97
              const float thr = (rangeEnd - rangeStart) * 0.025f;
              if (fabsf(value.value - value.target) < thr || value.value == value.target) {
                  pressProgress.SetTarget(0.0f);
                  scaleX.SetTarget(m_initialScale);
                  scaleY.SetTarget(m_initialScale);
                  m_pendingRelease = false;
              }
          }
          value.Update(dt); velocityAnim.Update(dt);
          pressProgress.Update(dt); scaleX.Update(dt); scaleY.Update(dt);
      }
      void PushVelocity(float unitsPerSec) { velocityAnim.SetTarget(unitsPerSec); }  // :134-135
  private:
      float m_pressedScale = 1.5f, m_initialScale = 1.0f;
      bool  m_pendingRelease = false;
  };
  ```
- **难度**：**中等**（结构简单，但"2.5% 等待"和"速度独立平滑"两个细节漏了手感就废了）

---

### M-11 ⭐ 液体玻璃开关（LiquidToggle）—— 按压时的"液滴铺开"

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\components\LiquidToggle.kt`（全文件 203 行）
- **视觉描述**：64×28dp 的玻璃轨道 + 40×24dp 的玻璃滑块。没按的时候滑块像一颗**竖直的细针**（`scaleY = 0`！），手指一按，它从针状**"融化铺开"**成完整圆角胶囊（`scaleY → 0.75`）；同时表面白色蒙版从"不透明"淡到"透明"，露出背后的玻璃折射；松手后再收回去。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| `valueRange` | `0f..1f` | `[源码] :72` |
| `visibilityThreshold` | `0.001f` | `[源码] :73` |
| `initialScale` | `1f` | `[源码] :74` |
| `pressedScale` | `1.5f` | `[源码] :75` |
| 拖拽行程 `dragWidth` | `20 dp` | `[源码] :64` |
| 拖动换算 | `delta = dragAmount.x / dragWidth` | `[源码] :91` |
| 落点判定 | `targetValue >= 0.5f ? 1f : 0f` | `[源码] :79` |
| 轨道尺寸 | `64 × 28 dp`，`Capsule()` | `[源码] :129` |
| 滑块尺寸 | `40 × 24 dp`，`Capsule()` | `[源码] :200` |
| 滑块内边距 | `2 dp` | `[源码] :136` |
| **滑块位移** | `translationX = lerp(2dp, 22dp, fraction)`（2+20=22） | `[源码] :138` |
| 轨道颜色 | 亮 `#34C759` / 暗 `#30D158`（选中） | `[源码] :56-57` |
| 轨道底色 | 亮 `#787878 @ 0.2` / 暗 `#787880 @ 0.36` | `[源码] :59-60` |
| **滑块"针状"形变** | `scaleX = lerp(2/3, 0.75, pressProgress)`；`scaleY = lerp(0, 0.75, pressProgress)` | `[源码] :150-151` |
| **按压时背景模糊** | `blur(8dp * (1 - pressProgress))` | `[源码] :160` |
| **按压时镜片** | `lens(5dp*pp, 10dp*pp, chromaticAberration=true)` | `[源码] :161-165` |
| **按压时高光** | `Highlight.Ambient` 缩放：`width/1.5`、`blurRadius/1.5`、`alpha = pp` | `[源码] :169-173` |
| 静态投影 | `Shadow(radius=4dp, Black @ 0.05)` | `[源码] :176-179` |
| 内阴影 | `InnerShadow(radius = 4dp * pp, alpha = pp)` | `[源码] :182-186` |
| **速度拉伸** | `v = velocity / 50f`；`scaleX /= 1 - clamp(v*0.75, -0.2, 0.2)`；`scaleY *= 1 - clamp(v*0.25, -0.2, 0.2)` | `[源码] :191-193` |
| **表面白色蒙版** | `drawRect(White, alpha = 1 - pressProgress)` | `[源码] :195-198` |
| 轨道填充色插值 | `lerp(trackColor, accentColor, fraction)` | `[源码] :127` |

- **`scaleY = lerp(0, 0.75, pp)` 是全局最巧的一处**：默认状态滑块被压成零高度（只剩一条线），按下去才展开。ImGui 里就是 `h = 24*kDp * lerp(0, 0.75, pp)`，然后以滑块中心为锚点画圆角矩形。
- **速度拉伸公式（三处复用，注意分母不同）**：

  | 位置 | 速度分母 | 来源 |
  |---|---|---|
  | LiquidToggle | `velocity / 50f` | `[源码] :191` |
  | LiquidSlider | `velocity / 10f` | `[源码] :200` |
  | LiquidBottomTabs | `velocity / 10f` | `[源码] :270` |

  ImGui 通用写法：
  ```cpp
  float v = dampedDrag.velocityAnim.value / denom;      // denom: 50 或 10
  sx /= 1.0f - ImClamp(v * 0.75f, -0.2f, 0.2f);
  sy *= 1.0f - ImClamp(v * 0.25f, -0.2f, 0.2f);
  ```
- **ImGui 移植要点**：`scaleX/scaleY` 要**围绕滑块自身中心**施加，`velocity` 拉伸要**围绕滑块与轨道接触点**施加（否则会看起来在"甩"）。用 `ImDrawList::AddCallback` 或手工把 rect 相对锚点缩放。
- **难度**：**中等**（几何与弹簧简单；难在玻璃/镜片/高光三层效果，可先降级成"半透明白 + 内阴影"）

---

### M-12 液体玻璃滑块（LiquidSlider）—— 同款液滴 + 点击跳转

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\components\LiquidSlider.kt`（全文件 212 行）
- **视觉描述**：与 M-11 同源的液滴语言：6dp 细轨道 + 40×24dp 玻璃滑块，静止时滑块被压成一条竖线（`scaleY = 0`），按下铺开（`scaleY → 1`）。额外支持**点击轨道任意位置弹簧迁移**。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| `initialScale` / `pressedScale` | `1f` / `1.5f` | `[源码] :82-83` |
| 轨道高度 | `6 dp` | `[源码] :127, :135` |
| 滑块尺寸 | `40 × 24 dp` | `[源码] :209` |
| 已填充宽度 | `constraints.maxWidth * progress` | `[源码] :138` |
| **滑块位移** | `(-size.width/2 + trackWidth*progress)`，clamp 到 `(-size.width/4, trackWidth - size.width*3/4)` | `[源码] :150-151` |
| **滑块形变** | `scaleX = lerp(2/3, 1, pp)`；`scaleY = lerp(0, 1, pp)` | `[源码] :159-160` |
| 背景模糊 | `blur(8dp * (1-pp))` | `[源码] :169` |
| 镜片 | `lens(10dp*pp, 14dp*pp, chromaticAberration=true)` | `[源码] :170-174` |
| 高光 | `Highlight.Ambient` 缩小 1/1.5，`alpha = pp` | `[源码] :178-182` |
| 投影 | `Shadow(4dp, Black @ 0.05)` | `[源码] :186-187` |
| 内阴影 | `InnerShadow(4dp*pp, alpha=pp)` | `[源码] :192-195` |
| 速度拉伸 | `v = velocity/10f`，同 M-11 公式 | `[源码] :200-202` |
| 表面蒙版 | `drawRect(White, alpha = 1-pp)` | `[源码] :206` |
| 轨道色 | 亮 `#787878 @ 0.2` / 暗 `#787880 @ 0.36` | `[源码] :62-63` |
| 强调色 | 亮 `#0088FF` / 暗 `#0091FF` | `[源码] :59-60` |
| 点击 | `detectTapGestures` → `animateToValue(target)` + `onValueChange` | `[源码] :117-125` |
| 拖动换算 | `delta = rangeSpan * (dragAmount.x / trackWidth)` | `[源码] :94` |

- **ImGui 移植**：注意 `translationX` 的 clamp（`:151`）—— 这是**防止滑块在两端越界**的关键，起止两端滑块各有一半在轨道外是设计意图（`-size.width/4` 允许一半露出）。照抄：
  ```cpp
  float halfW = 20.0f*kDp;
  float x = -halfW + trackW * progress;
  x = ImClamp(x, -halfW*0.5f, trackW - halfW*1.5f);
  ```
  点击轨道：`ImGui::IsMouseClicked(0) && ImGui::IsItemHovered()` → `mouseX / trackW` 换算成 value → `animateToValue`（走 `DampedDragAnimation::animateToValue` 语义：先 `press()`、再动画、再 `release()`，见 `DampedDragAnimation.kt:115-127`）。
- **难度**：**中等**

---

### M-13 ⭐ 底部标签栏"选中胶囊迁移"（可用于弹窗形变的母版）

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\components\LiquidBottomTabs.kt`（全文件 288 行）
- **视觉描述**：一排 4 个 tab，一个玻璃胶囊指示器在 tab 之间迁移。**迁移过程中胶囊会被速度横向拉长**（橡皮筋）。按下时整个面板横向膨胀（`scaleX/Y += 16dp/size.width`）、tab 图标放大到 1.2 倍、玻璃折射出现、投影与内阴影浮现。**这套"一个矩形从一个位置长到另一个位置"的逻辑，就是"弹窗从按钮长出来"的现成母版。**
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| `valueRange` | `0f..(tabsCount-1)` | `[源码] :106` |
| `visibilityThreshold` | `0.001f` | `[源码] :107` |
| `initialScale` | `1f` | `[源码] :108` |
| **`pressedScale`** | **`78f / 56f` = 1.392857**（注意这个奇怪的分数比值） | `[源码] :109` |
| tab 宽度 | `(maxWidth - 8dp) / tabsCount` | `[源码] :84` |
| 面板高度（外） | `64 dp` | `[源码] :183` |
| 面板高度（内） | `56 dp` | `[源码] :222, :284` |
| 面板左右内边距 | `4 dp` | `[源码] :185, :224, :233` |
| **面板按压膨胀** | `scale = lerp(1f, 1f + 16dp/size.width, pp)`，X/Y 同值 | `[源码] :176-178` |
| **tab 图标缩放** | `lerp(1f, 1.2f, pp)` | `[源码] :192` |
| **指示器迁移** | `translationX = value * tabWidth + panelOffset` | `[源码] :235-238` |
| **面板回弹旋转（panelOffset）** | `4dp * sign(f) * EaseOut.transform(|f|)`，`f = clamp(offsetAnim/maxWidth, -1, 1)` | `[源码] :88-95` |
| **`offsetAnimation` 回零弹簧** | `spring(dampingRatio=1.0, stiffness=300, visibilityThreshold=0.5f)` | `[源码] :116-119` |
| 拖动时 offset 累加 | `offsetAnimation += dragAmount.x` | `[源码] :128` |
| 面板玻璃 | `vibrancy(); blur(8dp); lens(24dp, 24dp)` | `[源码] :170-172` |
| 指示器玻璃 | `lens(10dp*pp, 14dp*pp, chromaticAberration=true)` | `[源码] :246-250` |
| 内层高光 | `Highlight.Default.copy(alpha = pp)` | `[源码] :217, :254` |
| 投影 | `Shadow(alpha = pp)`（默认 radius 24dp, Black@0.1） | `[源码] :258` + `Shadow.kt:15-17` |
| 内阴影 | `InnerShadow(radius = 8dp*pp, alpha = pp)` | `[源码] :262-265` |
| 速度拉伸 | `v = velocity/10f`，同 M-11 公式 | `[源码] :270-272` |
| **指示器表面** | `drawRect(亮?Black:White @ 0.1, alpha = 1-pp)` **加** `drawRect(Black @ 0.03*pp)` | `[源码] :274-282` |
| 容器色 | 亮 `#FAFAFA @ 0.4` / 暗 `#121212 @ 0.4` | `[源码] :72-74` |
| 强调色 | 亮 `#0088FF` / 暗 `#0091FF` | `[源码] :69-71` |
| 落点取整 | `targetValue.roundToInt().coerceIn(0, tabsCount-1)` | `[源码] :112` |

- **ImGui 移植（"弹窗从按钮长出来"的通用配方）** `[推断]`：
  源码里指示器是"同一个矩形在位置间迁移 + 速度拉伸"。把它抽象成通用形变：
  ```cpp
  struct MorphTarget {            // 起止两个矩形 + 圆角
      ImVec2 pos, size; float rounding;
  };
  // 单个弹簧 p: 0 = 源（按钮），1 = 目标（弹窗）
  ImVec2 pos  = ImLerp(src.pos,  dst.pos,  p);
  ImVec2 size = ImLerp(src.size, dst.size, p);
  float  r    = ImLerp(src.rounding, dst.rounding, p);
  // 速度拉伸（照抄 LiquidBottomTabs.kt:270-272 的形式，但作用在尺寸上）
  float v = spring.velocity / 1000.0f;
  size.x *= 1.0f + ImClamp(v * 0.75f, -0.2f, 0.2f);
  size.y *= 1.0f + ImClamp(v * 0.25f, -0.2f, 0.2f);
  ```
  **关键点**：源码的拉伸是**横向 0.75 / 纵向 0.25 的不对称权重**，这个比例直接决定了观感是"果冻"还是"橡胶"。照抄这个 3:1 比例。
  **锚点**：形变要以**源按钮的中心**和**目标弹窗的中心**之间插值，不要用左上角（否则会看起来在"平移+缩放"两件事叠加，不自然）。
- **难度**：**中等**（几何形变本身不难，难在"弹窗内容随尺寸变化重排"——ImGui 每帧重排，天然支持，反而是优势）

---

### M-14 触摸点高亮（InteractiveHighlight）—— 玻璃按钮的"手指光斑"

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\utils\InteractiveHighlight.kt`（全文件 121 行）
- **视觉描述**：手指按下时，在**触点位置**出现一个跟随手指的圆形光斑，边缘柔和；松手后光斑位置**弹回**按下时的起点并淡出。同时整个表面加一层均匀的 `White @ 0.08` 亮度。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 按压进度弹簧 | `spring(dampingRatio=0.5, stiffness=300, visibilityThreshold=0.001f)` | `[源码] :27-28` |
| 位置弹簧 | `spring(dampingRatio=0.5, stiffness=300, visibilityThreshold=Offset.VisibilityThreshold)` | `[源码] :29-30` |
| 均匀亮度层 | `drawRect(White @ 0.08 * progress, blendMode = Plus)` | `[源码] :65-68` |
| **光斑色** | `White @ 0.15 * progress` | `[源码] :72` |
| **光斑半径** | `size.minDimension * 1.5f` | `[源码] :73` |
| 光斑位置 clamp | `position.x/y` clamp 到组件矩形内 | `[源码] :76-77` |
| **光斑衰减** | `intensity = smoothstep(radius, radius * 0.5, dist)` | `[源码] :50-54` |
| 降级方案（无 RuntimeShader） | `drawRect(White @ 0.25 * progress, Plus)` 全屏均匀 | `[源码] :85-88` |
| 按下时位置 | `snapTo(startPosition)`（瞬间） | `[源码] :102` |
| 拖动时位置 | `snapTo(change.position)`（每帧直接跟手） | `[源码] :118` |
| 松手时位置 | `animateTo(startPosition, positionAnimationSpec)`（弹簧回起点） | `[源码] :108, :114` |

- **ImGui 移植**：
  ```cpp
  // progress: Spring(ζ=0.5, k=300, thr=0.001)
  // posAnim : Spring 对 x/y 各一个 (ζ=0.5, k=300, thr=0.01)
  if (ImGui::IsItemActive()) {
      posX.Snap(io.MousePos.x); posY.Snap(io.MousePos.y);
      press.SetTarget(1.0f);
  } else {
      posX.SetTarget(anchorX); posY.SetTarget(anchorY);
      press.SetTarget(0.0f);
  }
  if (press.value > 0.001f) {
      float r = ImMin(w, h) * 1.5f;
      float d = sqrtf(dx*dx + dy*dy);
      float intensity = ImSmoothstep(r, r*0.5f, d);     // 注意 ImGui 的 Smoothstep 参数是反的
      dl->AddRectFilled(p0, p1, IM_COL32(255,255,255, (int)(0.08f*press.value*255)), rounding); // 均匀层
      // 径向光斑：用若干同心圆近似，或写一个小 shader
      for (int i = 8; i >= 1; --i) {
          float rr = r * i / 8.0f;
          float a  = 0.15f * press.value * (1.0f - ImSmoothstep(r*0.5f, r, rr));
          dl->AddCircleFilled(ImVec2(cx, cy), rr, IM_COL32(255,255,255,(int)(a*255)), 32);
      }
  }
  ```
  **加法混合**（`BlendMode.Plus`）：ImGui 的 `AddRectFilled` 走普通 alpha 混合。要真加法混合得用 `ImDrawList::AddCallback` 切到 `glBlendFunc(GL_ONE, GL_ONE)` 再切回来。若嫌麻烦，用 `White @ 0.15` 的 alpha 混合视觉差距不大，可接受降级。
- **难度**：**中等**（加法混合 + 径向渐变）

---

### M-15 液体玻璃按钮的"液滴跟随"（LiquidButton）

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\components\LiquidButton.kt`（全文件 122 行）
- **视觉描述**：按下并拖动时，按钮会朝手指方向"流淌"过去，但位移**饱和**（永远不会真的跟到手指上）；朝手指方向的那一维会**拉伸**。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 玻璃效果 | `vibrancy(); blur(2dp); lens(12dp, 24dp)` | `[源码] :59-61` |
| 高度 | `48 dp` | `[源码] :116` |
| 左右内边距 | `16 dp` | `[源码] :117` |
| 内容间距 | `8 dp` | `[源码] :118` |
| **按压高度膨胀** | `scale = lerp(1f, 1f + 4dp/size.height, pp)` | `[源码] :69` |
| **位移饱和系数** | `initialDerivative = 0.05f` | `[源码] :72` |
| **位移公式** | `translation = maxOffset * tanh(0.05 * offset / maxOffset)`，`maxOffset = size.minDimension` | `[源码] :71-75` |
| **拉伸上限** | `maxDragScale = 4dp / size.height` | `[源码] :77` |
| 拉伸方向权重 | 按 `atan2(offset.y, offset.x)` 的 cos/sin 分配 | `[源码] :78-86` |
| 纵横比修正 | X 乘 `min(width/height, 1)`；Y 乘 `min(height/width, 1)` | `[源码] :82, :86` |
| tint 渲染 | 先 `drawRect(tint, blendMode = Hue)`，再 `drawRect(tint @ 0.75)` | `[源码] :93-94` |
| 表面色 | `drawRect(surfaceColor)` | `[源码] :97` |

- **`tanh` 饱和的数值感受**：`tanh(0.05) ≈ 0.04996`。也就是**在小位移下，视觉位移约为手指位移的 5%**；`offset = maxOffset` 时约 4.97%；`offset = 10*maxOffset` 时才到 46%。所以按钮几乎是"黏在原地轻微晃动"，这是刻意的克制。
- **ImGui 移植**：
  ```cpp
  float maxOffset = ImMin(w, h);
  dx = maxOffset * tanhf(0.05f * offX / maxOffset);
  dy = maxOffset * tanhf(0.05f * offY / maxOffset);
  float maxDragScale = 4.0f * kDp / h;
  float ang = atan2f(offY, offX);
  sx = scale + maxDragScale * fabsf(cosf(ang) * offX / ImMax(w,h)) * ImMin(w/h, 1.0f);
  sy = scale + maxDragScale * fabsf(sinf(ang) * offY / ImMax(w,h)) * ImMin(h/w, 1.0f);
  ```
  把 `sx, sy` 围绕**按钮中心**作用到 rect 上。
- **难度**：**中等**（公式直白，但 `tanh` 的饱和参数决定了整个手感，务必照抄 0.05）

---

### M-16 涟漪参数（Ripple）—— 玻璃库的备选反馈

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\Ripple.kt:56-65`
- **视觉描述**：从 Android Material 借来的标准涟漪，仅作为 `LocalIndication` 在 `MainContent.kt:33` 注册。**AndroidLiquidGlass 的目录页用它，但所有玻璃组件都自己实现了 M-14/M-15 的反馈，绕过涟漪。**
- **确切参数**（`RippleDefaults.RippleAlpha`，`Ripple.kt:58-64`）：

| 状态 | alpha | 来源 |
|---|---|---|
| `pressedAlpha` | `0.1f` | `[源码] Ripple.kt:60` |
| `focusedAlpha` | `0.1f` | `[源码] Ripple.kt:61` |
| `draggedAlpha` | `0.16f` | `[源码] Ripple.kt:62` |
| `hoveredAlpha` | `0.08f` | `[源码] Ripple.kt:63` |
| 颜色 | 亮 `Color.Black` / 暗 `Color.White` | `[源码] MainContent.kt:33` |

- **涟漪的扩散时长/缓动**：本文件**没有**写，走的是 `Material` 库默认（`createRippleModifierNode`，`Ripple.kt:22,162`）。Compose Material 涟漪默认 ≈ 225ms 扩张 / 150ms 淡出，`LinearOutSlowInEasing` `[库默认]`（数值来自 Material 库，本仓库无定义）。
- **ImGui 移植**：如果需要 Material 风格涟漪，用 `AddCircleFilled(pressPos, maxRadius * easeOut(t), col)`；`t` 用 `Spring(ζ=1.0, k=500)` 或直接 `t = 1 - pow(1-x, 3)`。**但更推荐直接用 M-14**：整份源码的玻璃组件都选了"触点光斑"而不是涟漪，这是风格取向。
- **难度**：**简单**

---

### M-17 ⚠️ 玻璃对话框（DialogContent）—— 只有外观，没有动效

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\DialogContent.kt`（全文件 139 行）
- **重要结论**：**这个文件里没有任何 `Animatable` / `animate*` / `graphicsLayer` 动画**（唯一的 `graphicsLayer` 是 `:89` 的 `blendMode = BlendMode.Plus`，是混合模式不是动画）。遮罩是**静态** `drawRect(dimColor)`（`:52`），对话框没有出场/入场、没有拖动关闭、没有圆角变化。它是"最终外观的静态展示"。
- **它提供的视觉规格（非常有用，是弹窗美术的现成规格书）**：

| 参数 | 值 | 来源 |
|---|---|---|
| **遮罩色（亮）** | `#29293A @ 0.23` | `[源码] :46` |
| **遮罩色（暗）** | `#121212 @ 0.56` | `[源码] :47` |
| 对话框外边距 | `40 dp` | `[源码] :57` |
| **对话框圆角** | `RoundedRectangle(48dp)` | `[源码] :60` |
| 玻璃：亮度/饱和度 | 亮主题 `brightness=0.2, saturation=1.5`；暗主题 `brightness=0, saturation=1.5` | `[源码] :62-65` |
| **玻璃：模糊半径** | 亮 `16 dp` / 暗 `8 dp` | `[源码] :66` |
| **玻璃：镜片** | `lens(refractionHeight=24dp, refractionAmount=48dp, depthEffect=true)` | `[源码] :67` |
| 高光 | `Highlight.Plain`（`White @ 0.38`，加法混合） | `[源码] :69` + `HighlightStyle.kt:34` |
| **容器色** | 亮 `#FAFAFA @ 0.6` / 暗 `#121212 @ 0.4` | `[源码] :43-44` |
| 标题内边距 | `(28, 24, 28, 12) dp` | `[源码] :76` |
| 标题字号 | `24 sp`，`FontWeight.Medium` | `[源码] :77` |
| 正文字号 / 颜色 | `15 sp` / 内容色 `@ 0.68` | `[源码] :93` |
| 正文最大行 | `5` | `[源码] :94` |
| 正文暗色额外处理 | `graphicsLayer(blendMode = Plus)` 提亮 | `[源码] :89` |
| 按钮行内边距 | `(24, 12, 24, 24) dp` | `[源码] :99` |
| 按钮间距 | `16 dp` | `[源码] :101` |
| 按钮高度 | `48 dp`，`Capsule()` | `[源码] :106-109, :122-125` |
| 取消按钮底色 | `containerColor @ 0.2` | `[源码] :107` |
| 确认按钮底色 | `accent`：亮 `#0088FF` / 暗 `#0091FF` | `[源码] :39-41, :123` |
| 按钮文字 | `16 sp` | `[源码] :117, :133` |

- **ImGui 移植（补全出场动效）** `[推断]` —— 数值取自本仓库其他地方已验证的常数，组合成一个完整弹窗：

  | 阶段 | 参数 | 依据 |
  |---|---|---|
  | 主进度 `p` | 单弹簧 `ζ=1.0, k=300, thr=0.01`，0=隐藏 1=显示 | `ControlCenterContent.kt:174,182` |
  | 遮罩 alpha | `0.56 * p`（暗）/ `0.23 * p`（亮） | `DialogContent.kt:46-47` × `ControlCenterContent.kt:192` 的线性映射法 |
  | 缩放 | `scale = lerp(0.90, 1.0, p)`，围绕对话框中心 | `[推断]`（源码无；0.90 是 Material/iOS 惯例） |
  | 位移 | `translationY = (1 - p) * 24dp` | `[推断]`（参考 `ControlCenterContent.kt:110` 的 48dp 按弹窗体量折半） |
  | 透明度 | `alpha = clamp(p * 1.5, 0, 1)`（前 2/3 进度淡入完） | `[推断]` |
  | 圆角 | `rounding = lerp(28dp, 48dp, p)` | 终值 `[源码] DialogContent.kt:60`；起值 `[推断]` |
  | 玻璃模糊 | `blur = lerp(0, 16dp, p)`（亮）/ `lerp(0, 8dp, p)`（暗） | `DialogContent.kt:66` 的终值 |
  | 镜片 | `lens(24dp * p, 48dp * p, depthEffect=true)` | `DialogContent.kt:67` × `ControlCenterContent.kt:119-123` 的乘法模式 |
  | 按钮错峰入场 | 第 1 个按钮延迟 `0ms`，第 2 个延迟 `40ms` | `[推断]`（源码无错峰，全库都没有） |

  ```cpp
  // ImGui 渲染（对话框窗口 + 遮罩）
  float p = dlg.progress.value;                 // 0..1 弹簧
  if (p > 0.001f) {
      // 1) 遮罩
      ImGui::GetForegroundDrawList()->AddRectFilled(
          ImVec2(0,0), io.DisplaySize,
          IM_COL32(0x12,0x12,0x12, (int)(0.56f * p * 255)));
      // 2) 对话框：ImGui 不能整体缩放窗口，用 rect 插值 + 圆角插值
      ImVec2 base = ImVec2((io.DisplaySize.x - dlgW) * 0.5f,
                           (io.DisplaySize.y - dlgH) * 0.5f);
      ImVec2 c    = ImVec2(base.x + dlgW*0.5f, base.y + dlgH*0.5f);
      float  s    = ImLerp(0.90f, 1.0f, p);
      ImVec2 sz   = ImVec2(dlgW * s, dlgH * s);
      ImVec2 pos  = ImVec2(c.x - sz.x*0.5f, c.y - sz.y*0.5f + (1.0f - p) * 24.0f * kDp);
      float  rnd  = ImLerp(28.0f, 48.0f, p) * kDp;

      ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
      ImGui::SetNextWindowSize(sz, ImGuiCond_Always);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rnd);
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28*kDp, 24*kDp));
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImClamp(p * 1.5f, 0.0f, 1.0f));
      ImGui::Begin("##dlg", nullptr,
          ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse);
      // ... 内容
      ImGui::End();
      ImGui::PopStyleVar(3);
  }
  ```
  **注意 `ImGuiStyleVar_Alpha` 不会让窗口背景变透明到能看见玻璃模糊** —— 要真正的玻璃效果，必须把对话框单独画进 FBO/或对背景 FBO 采样（§1.5）。若只求"看得过去"，用 `PushStyleColor(ImGuiCol_WindowBg, IM_COL32(18,18,18, 0.4*255*p))` 就够了。
- **难度**：**中等**（外观 100% 可复刻；真正的背景模糊/折射是**困难**）

---

### M-18 ⚠️ 玻璃面板展开/收起（GlassPlayground 的 sheet）—— 瞬切，需要补

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\GlassPlaygroundContent.kt`
- **现状**：`:52` `var isSheetExpanded by remember { mutableStateOf(true) }`，`:120` `if (isSheetExpanded) { ... }` —— **纯瞬切，无动画**。这是全库离"弹窗动效"最近但**没做**的地方。
- **它的静态规格（可用于面板外观）**：

| 参数 | 值 | 来源 |
|---|---|---|
| 面板圆角 | `RoundedRectangle(32dp)` | `[源码] :129` |
| 面板内边距 | 外 `16dp`，底 `72dp`，内 `24dp` | `[源码] :124, :125, :139` |
| 面板玻璃 | `vibrancy(); blur(4dp); lens(16dp, 32dp)` | `[源码] :131-133` |
| 面板表面色 | `White @ 0.5` | `[源码] :137` |
| 元素间距 | `16 dp`（组间）/ `8 dp`（标签与滑块间） | `[源码] :142, :144` |
| 悬浮球位置 | 左下角，内边距 `20dp` + 导航栏内边距 | `[源码] :203-205` |
| 拖拽变换球 | `256 dp` 方块，顶部居中，顶部内边距 `48dp` | `[源码] :63, :115-116` |
| 变换初值 | `offset=Zero, zoom=1f, rotation=0f` | `[源码] :48-50` |
| 复位时长 | `Animatable.animateTo(...)` 无显式 spec → `spring(1.0, 1500)` | `[库默认] :217-219` |
| 面板参数初值 | `cornerRadiusFrac=0.5, blurRadiusDp=0, refractionHeightFrac=0.2, refractionAmountFrac=0.2, chromaticAberration=0` | `[源码] :54-58` |
| 缩放/旋转手势 | `detectTransformGestures` → `snapTo`（直接跟手） | `[源码] :99-113` |

- **ImGui 移植（补展开/收起）** `[推断]`：

  | 项 | 建议值 | 依据 |
  |---|---|---|
  | 主进度弹簧 | `ζ=1.0, k=300` | `ControlCenterContent.kt:182` |
  | 位移 | `translationY = (1-p) * (sheetHeight + 72dp)` | 从屏幕下方滑入 |
  | 透明度 | `alpha = clamp(p*1.6, 0, 1)` | 快速淡入避免"看到重排" |
  | 圆角 | `rounding = lerp(32dp, 20dp, p)` 或恒定 32dp | 终值 `[源码] :129` |
  | 拖动关闭 | 向下拖动 > `sheetHeight * 0.25` 或速度 > `600 px/s` 则关闭 | 阈值 `[推断]`；速度判定的**结构**照抄 `ControlCenterContent.kt:162-166` |
  | 拖动关闭时的遮罩联动 | 遮罩 alpha 与拖动进度反向联动 | 同 `ControlCenterContent.kt:192` |

- **难度**：**中等**

---

### M-19 亮度自适应主题过渡（唯一的 tween）

- **来源**：`app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\AdaptiveLuminanceGlassContent.kt`
- **视觉描述**：每帧把玻璃面板渲染结果缩到 5×5 缩略图，算平均亮度，然后**用 1 秒的 tween** 平滑地改变玻璃的提亮、对比度、模糊半径，同时把上面的文字颜色在**黑与白之间 1 秒渐变**。这是整个代码库里**唯一一处显式的 `tween`**。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| **主题过渡时长** | `tween(1000)` = **1000 ms** | `[源码] :72, :77` |
| tween 缓动 | `tween()` 默认 = `FastOutSlowInEasing` = `CubicBezier(0.4, 0.0, 0.2, 1.0)` | `[库默认]`（Compose 库定义，本仓库无显式 easing） |
| 亮度采样 | 5×5 缩略图 → `IntArray(25)` | `[源码] :57, :60` |
| **亮度公式** | Rec.709：`0.2126*R + 0.7152*G + 0.0722*B` | `[源码] :67` |
| 文字变黑阈值 | `averageLuminance > 0.5f` → 黑，否则白 | `[源码] :71` |
| 文字色动画 | `Animatable(Color.Black / Color.White)` + `tween(1000)` | `[源码] :53-55, :70-73` |
| 亮度值动画 | `Animatable(亮?1:0)` + `tween(1000)` | `[源码] :50-52, :75-78` |
| **亮度对称化** | `l = sign(x) * x * x`，其中 `x = lum*2 - 1` | `[源码] :94` |
| 提亮 | `l > 0`：`brightness = lerp(0.1, 0.5, l)` | `[源码] :96-98` |
| 压暗 | `l <= 0`：`brightness = lerp(0.1, -0.2, -l)` | `[源码] :98` |
| 对比度 | `l > 0`：`contrast = lerp(1, 0, l)`；否则 `contrast = 1` | `[源码] :99-101` |
| 饱和度 | 恒 `1.5` | `[源码] :102` |
| **模糊半径** | `l > 0`：`lerp(8dp, 16dp, l)`；否则 `lerp(8dp, 2dp, -l)` | `[源码] :104-107` |
| 镜片 | `lens(24dp, size.minDimension/2, depthEffect=true)` | `[源码] :108` |
| 玻璃圆角 | `RoundedRectangle(24dp)` | `[源码] :92` |
| 面板尺寸 | `160 dp` | `[源码] :151` |
| 变换原点 | `TransformOrigin(0.5, 0.5)` | `[源码] :120` |

- **ImGui 移植**：
  ```cpp
  // tween 就是最简单的线性/缓动计时器，不是弹簧
  struct Tween {
      float from = 0, to = 0, value = 0, dur = 1.0f, t = 1.0f;
      void To(float target, float duration) {
          if (target == to) return;
          from = value; to = target; dur = duration; t = 0.0f;
      }
      void Update(float dt) {
          if (t >= 1.0f) return;
          t = ImMin(1.0f, t + dt / dur);
          value = ImLerp(from, to, FastOutSlowIn(t));   // cubic-bezier(0.4,0,0.2,1)
      }
  };
  // FastOutSlowIn 用预计算 LUT（33 点）+ 线性插值，精度足够
  ```
  亮度采样：ImGui 侧最便宜的做法是**不采样画面**，改为让用户/主题系统直接给出目标亮度；若确实要采样，用 `glReadPixels` 采 5×5（或 `glGenerateMipmap` 后读顶层 1×1）—— 注意 `glReadPixels` 会强制 GPU 同步，**每帧调用代价极高**，建议降到每 200ms 一次 `[推断]`。
- **难度**：**中等**（tween 本身简单；实时亮度采样在 GLES 上是性能陷阱）

---

### M-20 放大镜 / 锁屏的 2D 拖拽（无回弹，直接跟手）

- **来源**：
  - `app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\MagnifierContent.kt`
  - `app\src\commonMain\kotlin\com\kyant\backdrop\catalog\destinations\LockScreenContent.kt`
- **视觉描述**：一个玻璃"放大镜"在文字上拖动，镜片内是**放大了 1.5 倍且向上偏移 80dp** 的背景内容；锁屏是一个玻璃时钟跟着手指 2D 移动。**两者都是纯直接跟手，松手不回弹、没有惯性**。
- **确切参数**：

| 参数 | 值 | 来源 |
|---|---|---|
| 放大镜镜片尺寸 | `128 × 96 dp`，`Capsule()` | `[源码] MagnifierContent.kt:105, :85` |
| 镜片内缩放 | `scale(1.5, 1.5)` | `[源码] MagnifierContent.kt:98` |
| 镜片内容偏移 | `translate(top = -80dp)` | `[源码] MagnifierContent.kt:99` |
| 镜片与光标竖直距离 | `translationY = offset.y - 80dp` | `[源码] MagnifierContent.kt:81` |
| 镜片效果 | `lens(8dp, 24dp, depthEffect=true, chromaticAberration=true)` | `[源码] MagnifierContent.kt:87-92` |
| 镜片内阴影 | `InnerShadow(radius = 16dp)` | `[源码] MagnifierContent.kt:94` |
| 光标尺寸 | `4 × 24 dp`，`Capsule()`，强调色 | `[源码] MagnifierContent.kt:72-73` |
| 光标圆角 | `RoundedRectangle(32dp)`（背面文字块） | `[源码] MagnifierContent.kt:57` |
| 锁屏时钟模糊 | `blur(2dp)`，`colorControls(brightness=-0.1, contrast=0.75, saturation=1.5)` | `[源码] LockScreenContent.kt:63-64` |
| 锁屏时钟叠加 | `White @ 0.25` | `[源码] LockScreenContent.kt:69` |
| 锁屏时钟形状 | `Rectangle`（直角） | `[源码] LockScreenContent.kt:61` |
| 锁屏背景 | `Black @ 0.3` | `[源码] LockScreenContent.kt:41` |
| 锁屏时钟左右内边距 | `48 dp`，最大宽度 `400dp` | `[源码] LockScreenContent.kt:52, :72` |
| 拖动实现 | `draggable2D { delta -> offset += delta }` | `[源码] MagnifierContent.kt:70`, `LockScreenContent.kt:58` |
| 有无弹簧 | **无** | 两文件均无 `Animatable` |

- **ImGui 移植**：
  ```cpp
  if (ImGui::IsItemActive())
      offset = ImVec2(offset.x + io.MouseDelta.x, offset.y + io.MouseDelta.y);
  ```
  放大镜的"放大且偏移"内容：在 §1.5 的 `uv` 计算里改成 `uv = (vLocal + vHalf)/uSize * 1.5 + uContentOffset` 即可（`uContentOffset` 对应 `-80dp`）。
- **难度**：**简单**（拖动）+ **中等**（镜片折射着色器）

---

### M-21 静态视觉 token 汇总（不是动效，但动效要用）

这些是动效的**起止值**，必须精确：

| token | 值 | 来源 |
|---|---|---|
| `Highlight` 默认宽度 | `0.5 dp` | `Highlight.kt:11` |
| `Highlight` 默认模糊 | `width / 2 = 0.25 dp` | `Highlight.kt:12` |
| `HighlightStyle.Plain` 色 | `White @ 0.38`，加法混合 | `HighlightStyle.kt:34-35` |
| `HighlightStyle.Default` 色 | `White @ 0.5`，加法混合 | `HighlightStyle.kt:46-47` |
| `HighlightStyle.Default` 角度/衰减 | `angle = 45f`，`falloff = 1f` | `HighlightStyle.kt:48-49` |
| `HighlightStyle.Ambient` 强度 | `intensity = 0.38`（即 `White @ 0.38`） | `HighlightStyle.kt:75, :78` |
| `HighlightStyle.Ambient` 角度/衰减 | `angle = 45f`，`falloff = 1f`（硬编码） | `HighlightStyle.kt:93-94` |
| `Shadow` 默认半径 | `24 dp` | `Shadow.kt:15` |
| `Shadow` 默认偏移 | `(0, radius/6)` | `Shadow.kt:16` |
| `Shadow` 默认色 | `Black @ 0.1` | `Shadow.kt:17` |
| `InnerShadow` 默认半径 | `24 dp` | `InnerShadow.kt:18` |
| `InnerShadow` 默认偏移 | `(0, radius)` | `InnerShadow.kt:19` |
| `InnerShadow` 默认色 | `Black @ 0.15` | `InnerShadow.kt:20` |
| `blur` 的 padding 规则 | `if (radius > padding) padding = radius`；即 `radius == 0` 时直接 return，不做任何事 | `Blur.kt:14-20` |
| `lens` 的 padding 规则 | `padding = max(padding - refractionHeight, 0)` | `Lens.kt:25-27` |
| `lens` 的 `refractionAmount` 符号 | 传 uniform 时取负：`-refractionAmount` | `Lens.kt:49` |
| 通用强调色 | 亮 `#0088FF` / 暗 `#0091FF` | `ControlCenterContent.kt:62-63` 等 6 处一致 |

---

## 4. 四个重点模式的结论

### 4.1 弹窗出现/消失的形变（从按钮"长出来"、或缩放淡入）

| 问题 | 结论 |
|---|---|
| AMDL 有吗？ | **没有**。无任何弹窗。 |
| AndroidLiquidGlass 有吗？ | **没有真正的弹窗形变**。`DialogContent.kt` 是静态外观；`GlassPlaygroundContent.kt:120` 的面板是瞬切。 |
| 最接近的母版在哪？ | **`LiquidBottomTabs.kt:231-286`** —— 一个玻璃胶囊在不同 tab 之间迁移，带速度拉伸。这就是"从 A 长到 B"的完整实现。 |
| 可复刻吗？ | **可以，而且是 ImGui 的强项。** ImGui 每帧重排，用 `SetNextWindowPos/Size` 插值矩形即可得到真实形变；不需要 Compose 的 `SharedTransitionLayout`。 |
| 推荐参数 | 单弹簧 `ζ=1.0, k=300`；`pos/size/rounding` 三线性插值；速度拉伸照抄 `v*0.75 / v*0.25` 的 3:1 不对称权重（`LiquidBottomTabs.kt:270-272`）。 |

### 4.2 遮罩层（scrim）的透明度与模糊随时间变化

| 问题 | 结论 |
|---|---|
| AMDL 有吗？ | 只有**静态**的底部渐隐遮罩（M-04，`MainScene.kt:183`，`EaseOut`，104dp，alpha 0→1）。 |
| AndroidLiquidGlass 有吗？ | **有，而且是动态的。** `ControlCenterContent.kt:192` 遮罩 alpha = `0.4 * progress`；`:197-200` 背景模糊 = `4dp * progress`。**遮罩与模糊都与同一个 progress 线性绑定**。 |
| 静止时遮罩色 | 暗 `#121212 @ 0.56`（`DialogContent.kt:47`）；亮 `#29293A @ 0.23`（`DialogContent.kt:46`）；控制中心 `Black @ 0.4`（`ControlCenterContent.kt:65`） |
| 可复刻吗？ | **可以。** ImGui 用 `GetForegroundDrawList()->AddRectFilled` 画全屏遮罩；模糊必须走 FBO（§1.5）。 |
| 推荐参数 | 遮罩 `alpha = scrimMax * progress`（线性即可，别加缓动）；模糊 `r = rMax * progress`，`rMax` 取 4dp（控制中心）/ 8dp（暗色弹窗）/ 16dp（亮色弹窗）。**务必让遮罩先于模糊完成**：若想要更高级的手感，给遮罩一个 `k=500` 的弹簧、模糊用 `k=300`，让遮罩"抢一拍" `[推断]`。 |

### 4.3 拖动关闭（drag to dismiss）的手势与回弹逻辑

| 问题 | 结论 |
|---|---|
| AMDL 有吗？ | **没有。** |
| AndroidLiquidGlass 有吗？ | **有完整的实现，在 `ControlCenterContent.kt:148-187`**，以及被三处复用的 `DampedDragAnimation.kt`（含速度追踪）。 |
| 手势判定结构 | `onDragStopped(velocity)`：`velocity < 0 → 收起`；`velocity > 0 → 展开`；`velocity == 0 → value < 0.5 ? 收起 : 展开`（`ControlCenterContent.kt:162-166`） |
| **速度参与弹簧初速** | `animateTo(target, spec, initialVelocity = velocity / maxDragHeight)`（`:176`）—— **这是手感的关键**：快速轻甩也能关掉，慢速拖到底也能关掉。ImGui 必须实现（把 §1.3 的速度归一化后塞进 `spring.velocity`）。 |
| 展开 vs 收起的弹簧不同 | 展开 `ζ=0.5`（有回弹，像"弹开"）；收起 `ζ=1.0`（无回弹，像"沉下去"）。`ControlCenterContent.kt:172,174` |
| `DampedDragAnimation` 的落点判定 | `targetValue >= 0.5 → 1 : 0`（`LiquidToggle.kt:79`） |
| **`release()` 的等待** | 松手后**先等主值收敛到目标 2.5% 区间内**，再回弹 `pressProgress` 与 `scaleX/Y`（`DampedDragAnimation.kt:96-101`）。**这是全库最值得抄的一个"手感细节"。** |
| 可复刻吗？ | **可以，难度中等。** 需要 §1.3 的速度追踪 + §1.1 的 `initialVelocity` 支持。 |

### 4.4 主题色切换的过渡

| 问题 | 结论 |
|---|---|
| AMDL 有吗？ | **没有。** `Palette.kt:13-28` 是瞬变三目表达式。 |
| AndroidLiquidGlass 有吗？ | **有。** `AdaptiveLuminanceGlassContent.kt:70-78` 是全库**唯一**的 `tween`，时长 **1000 ms**。 |
| 具体做什么过渡 | ① 文字色 `Black ↔ White`；② 玻璃的 `brightness` / `contrast` / `blur` 三个参数随平均亮度联动。 |
| 缓动 | `tween()` 默认 `FastOutSlowInEasing = CubicBezier(0.4, 0.0, 0.2, 1.0)` `[库默认]` |
| 可复刻吗？ | **可以，简单。** 一个 `Tween` 结构体 + 颜色 `ImLerp` 即可。 |
| 推荐参数 | 复用 1000ms + `FastOutSlowIn`；若想要更"Q"的手感改用弹簧 `ζ=1.0, k=300`（≈380ms，比 1000ms 更跟手）`[推断]`。**注意**：Compose 的 `Color` 插值与 ImGui 的 `IM_COL32` 插值都在 gamma 空间直接线性插值，行为一致，无需做线性空间转换。 |

---

## 5. 落地优先级建议

按"投入产出比"排序（都是 `[推断]` 的排序，不是源码结论）：

| 顺序 | 动效 | 为什么先做 | 依赖 | 难度 |
|---|---|---|---|---|
| 1 | **§1.1 Spring 求解器 + §1.3 VelocityTracker** | 后面所有东西的地基 | 无 | 简单 |
| 2 | **M-01 Indication 高亮**（`1.0/500`，press 0.2 / hover 0.15，80ms 最短保持） | 立刻让全 UI 有反馈，代码量极小 | 1 | 简单 |
| 3 | **M-10 DampedDragAnimation**（5 条 spring spec + 2.5% 等待 + 速度平滑） | 一次实现，M-11/12/13 全部解锁 | 1,2 | 中等 |
| 4 | **M-13 底部标签栏胶囊迁移**（`pressedScale = 78/56`，`v*0.75/v*0.25` 拉伸） | 顺便拿到"弹窗形变"母版 | 3 | 中等 |
| 5 | **M-09 控制中心进出场**（`k=300`, 展开 `ζ=0.5` / 收起 `ζ=1.0`, scrim 0.4, blur 4dp, 过拖 exp 饱和） | 全套"动态遮罩+模糊+拖拽关闭"一次到位 | 1,2,3 | 困难 |
| 6 | **M-17 玻璃对话框**（圆角 48dp，scrim 0.56/0.23，blur 16/8dp，镜片 24/48dp） | 直接得到成品弹窗 | 5, §1.5 | 中等（外观）/ 困难（真玻璃） |
| 7 | M-11 / M-12 液体开关与滑块 | 提升质感 | 3 | 中等 |
| 8 | M-19 主题过渡 `tween(1000)` | 收尾润色 | 1 | 简单 |

---

## 6. 附：全部动效一览

| ID | 名称 | 来源文件 | 关键弹簧/时长 | 难度 |
|---|---|---|---|---|
| M-01 | Indication 高亮 | `ui\Indication.kt` | `ζ=1.0, k=500, thr=0.01`；0.2/0.15/0.2；80ms | 简单 |
| M-02 | Switch 拨动 | `ui\Switch.kt:33-63` | `ζ=1.0, k=600`；4dp→28dp | 简单 |
| M-03 | 进度环 | `scene\LoginScene.kt:121-142` | `ζ=1.0, k=300`；-90° 起，360°×p | 简单 |
| M-04 | 底部渐隐遮罩 | `ui\VerticalGradient.kt` + `MainScene.kt:178-190` | 104dp，alpha 0→1，`EaseOut` | 简单 |
| M-05 | 键盘避让 | `scene\LoginScene.kt:90-92` | 无动画，`-ime/2` | 简单 |
| M-06 | ⚠️ 场景切换（缺失） | `MainActivity.kt:47-53` | 无 | 中等 |
| M-07 | ⚠️ 主题切换（缺失） | `ui\Palette.kt:13-28` | 无 | 简单 |
| M-09 | ⭐ 控制中心进出场 | `destinations\ControlCenterContent.kt` | `k=300`；展开`ζ=0.5` / 收起`ζ=1.0`；scrim `0.4*p`；blur `4dp*p`；过拖 exp 饱和；行距 16→48dp | 困难 |
| M-10 | DampedDragAnimation | `utils\DampedDragAnimation.kt` | `1/1000`、`0.5/300`、`1/1000`、`0.6/250`、`0.7/250`；落点阈值 2.5% | 中等 |
| M-11 | ⭐ 液体开关 | `components\LiquidToggle.kt` | `pp→1.5`；`scaleY lerp(0,0.75)`；blur `8dp*(1-pp)`；`v/50` | 中等 |
| M-12 | 液体滑块 | `components\LiquidSlider.kt` | `pp→1.5`；`scaleY lerp(0,1)`；`v/10` | 中等 |
| M-13 | ⭐ 标签栏胶囊迁移（弹窗形变母版） | `components\LiquidBottomTabs.kt` | `pressedScale = 78/56`；`4dp*EaseOut`；`k=300, thr=0.5`；`v/10`；拉伸权重 0.75/0.25 | 中等 |
| M-14 | 触摸点高亮 | `utils\InteractiveHighlight.kt` | `ζ=0.5, k=300`；`r = minDim*1.5`；`White 0.15p + 0.08p` | 中等 |
| M-15 | 液滴按钮 | `components\LiquidButton.kt` | `tanh(0.05)`；`maxDragScale = 4dp/h`；blur 2dp；lens 12/24dp | 中等 |
| M-16 | 涟漪参数 | `Ripple.kt:56-65` | 0.1 / 0.1 / 0.16 / 0.08 | 简单 |
| M-17 | ⚠️ 玻璃对话框（只有静态外观） | `destinations\DialogContent.kt` | 无动画；圆角 48dp；scrim 0.56/0.23；blur 16/8dp；lens 24/48dp | 中等 |
| M-18 | ⚠️ 玻璃面板展开（瞬切） | `destinations\GlassPlaygroundContent.kt:52,120` | 无动画；圆角 32dp；blur 4dp；lens 16/32dp | 中等 |
| M-19 | 亮度自适应主题 | `destinations\AdaptiveLuminanceGlassContent.kt` | **`tween(1000)`**；Rec.709；阈值 0.5 | 中等 |
| M-20 | 放大镜 / 锁屏拖拽 | `MagnifierContent.kt`, `LockScreenContent.kt` | 无弹簧，直接跟手；镜片 1.5×，偏移 -80dp | 简单 |
| M-21 | 静态视觉 token | `Highlight.kt` 等 | 见 M-21 | —— |

> **注**：§3 中 **M-08** 是 AMDL 部分的盘点小结表，不是独立动效，故不列入本表。

---

*本文档所有行号基于 `AMDL-master` 与 `AndroidLiquidGlass-kmp` 当前快照；若上游更新需重新核对。*
