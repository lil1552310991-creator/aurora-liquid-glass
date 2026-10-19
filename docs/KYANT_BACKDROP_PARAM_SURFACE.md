# Kyant0 `backdrop` 库 —— 完整可视参数面规格（用于驱动调试面板实现）

> 本文档的目标：把 `Kyant0/AndroidLiquidGlass` 的 `backdrop` 库**每一个会影响最终像素的参数**，
> 以及应用层成品组件暴露的参数，逐条抄成一份**可直接照此实现调试面板**的规格。
>
> 全部结论来自本地源码，**未联网**。

---

## 0. 阅读指南

### 0.1 标记约定（沿用 `docs/AMDL_MOTION_CATALOG.md:21-23`）

| 标记 | 含义 |
| --- | --- |
| `[源码]` | 该数值/名字在源码里**字面写出**，行号可核对 |
| `[库默认]` | 调用处**没写**该参数，走的是**本仓库内的库默认值**（`Highlight.kt` / `Shadow.kt` 等 data class 的默认实参），或 Compose/Android 平台库默认（后者会注明「平台库定义，本仓库无」） |
| `[推断]` | 源码里**没有**这个数值或结论，是为落地而补的判断 |
| `[未核实]` | 我无法从本地材料确认，**不编造** |

### 0.2 源码根（下文所有相对路径都以此为根）

```
KMP = D:\纯c图形基座\build\recon\ref_kyant\AndroidLiquidGlass.tar\AndroidLiquidGlass-kmp\
```

本文中：

- `backdrop/...` → `KMP\backdrop\src\commonMain\kotlin\com\kyant\backdrop\...`
  （若写 `backdrop/src/androidMain/...` / `src/skikoMain/...` 则显式注明平台）
- `app/...` → `KMP\app\src\commonMain\kotlin\com\kyant\backdrop\catalog\...`
- `ytbl/...` → `D:\纯c图形基座\app\src\main\cpp\third_party\liquidglass\ytbl\...`
- `secondary_imgui.cpp` → `D:\纯c图形基座\app\src\main\cpp\third_party\liquidglass\secondary_imgui.cpp`

行号**全部来自本次实际读取**（文件总行数已在各表头标注），未核实的一律写「未核实」。

### 0.3 版本信息（决定"哪些 API 存在"）

| 事实 | 值 | 出处 |
| --- | --- | --- |
| `backdrop` 库坐标/版本 | `io.github.kyant0:backdrop:2.0.1` | `backdrop/build.gradle.kts:84` `[源码]` |
| 连续曲率形状库 | `io.github.kyant0:shapes:1.2.1` | `gradle/libs.versions.toml:8,18` `[源码]` |
| Compose 版本 | `1.12.0` | `gradle/libs.versions.toml:7` `[源码]` |
| RenderEffect 支持门 | `Build.VERSION.SDK_INT >= S (31)` | `backdrop/src/androidMain/.../Platform.kt:7` `[源码]` |
| RuntimeShader（AGSL）支持门 | `Build.VERSION.SDK_INT >= TIRAMISU (33)` | `backdrop/src/androidMain/.../Platform.kt:10` `[源码]` |
| `shapes` 库源码 | **不在本 tar 内**（是外部 Maven 依赖） | `backdrop/build.gradle.kts:43` `[源码]`；本地已有移植 `build/recon/squircle_path.h` |

> ★ **重要前提**：`com.kyant.shapes` 的源码不在本地 tar 里。
> 因此凡涉及「连续曲率圆角」的**参数名**，本文只能标 `[未核实]`，
> 但本项目已在 `build/recon/squircle_path.h` + `docs/SQUIRCLE_PATH_FINDINGS.md` 里做过数值等价移植。

### 0.4 ★ 渲染管线事实（决定"改了参数要不要重编译"）

**不是多 pass FBO 链，而是「1 个离屏 GraphicsLayer + RenderEffect 链」** `[源码]`：

```
DrawBackdropModifier.kt:246-250   effectScope（保存 padding / renderEffect）
DrawBackdropModifier.kt:362-368   updateEffects(): effectScope.apply(effects); graphicsLayer?.renderEffect = effectScope.renderEffect
DrawBackdropModifier.kt:285-304   drawBackdropLayer(): 按 padding 扩边记录图层
```

`RenderEffect.chain` 的**实际语义**（Android 实现）：

```kotlin
// backdrop/src/androidMain/kotlin/com/kyant/backdrop/internal/RenderEffect.kt:13-22
internal actual fun RenderEffect?.chain(other: RenderEffect): RenderEffect {
    return if (this != null) {
        android.graphics.RenderEffect.createChainEffect(
            other.asAndroidRenderEffect(),     // ← outer：最后作用
            this.asAndroidRenderEffect()       // ← inner：先作用
        ).asComposeRenderEffect()
    } else {
        other
    }
}
```

⇒ **`effects { }` 块里的书写顺序 = 从内到外的施加顺序**。
所以 `LiquidButton.kt:58-62` 的 `vibrancy(); blur(2dp); lens(12dp,24dp)` 实际是
`lens( blur( vibrancy(content) ) )`。`[源码]`（`createChainEffect(outer, inner)` 的参数语义是 Android 平台定义，本仓库无；此处结论 `[推断]` 自参数名 `outer`/`inner`）

`BackdropEffectScope.apply()` **每次都会清零**：

```kotlin
// backdrop/src/commonMain/.../BackdropEffectScope.kt:59-63
fun apply(effects: BackdropEffectScope.() -> Unit) {
    padding = 0f
    renderEffect = null
    effects()
}
```

并且 `updateEffects()` 有一道总门禁：`if (!isRenderEffectSupported()) return`
（`DrawBackdropModifier.kt:363`）——**S 以下整条效果链被跳过，只剩原始 backdrop 内容**。

**Shader 缓存**：`RuntimeShaderCacheImpl.obtainRuntimeShader(key, string)` 用 `key` 做 map 键
（`RuntimeShaderCache.kt:14-16`），key 是 `"Refraction"` / `"RefractionWithDispersion"` /
`"Default"` / `"Ambient"` 这类**字符串字面量**。⇒
**同一 key 的参数只走 `setFloatUniform`，不重编译；换 key（= 换 shader 字符串）才产生一次新编译。**

---

# 第一部分：`backdrop` 库的完整可视参数面

---

## 1.1 Blur（模糊）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/effects/Blur.kt`（**共 29 行**）

```kotlin
// Blur.kt:9-29（全文）
fun BackdropEffectScope.blur(
    @FloatRange(from = 0.0) radius: Float,
    edgeTreatment: TileMode = TileMode.Clamp
) {
    if (!isRenderEffectSupported()) return
    if (radius <= 0f) return

    if (edgeTreatment != TileMode.Clamp || renderEffect != null) {
        if (radius > padding) {
            padding = radius
        }
    }

    renderEffect =
        BlurEffect(
            renderEffect,
            radius,
            radius,
            edgeTreatment
        )
}
```

| 参数 | 源码位置 | 类型 | 范围/取值 | 默认值 | 在 shader 里的作用 | 对应调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `radius` | `Blur.kt:10`（形参）；`:23-27`（传给 `BlurEffect`，X/Y 同值） | `Float`（px，调用点用 `.dp.toPx()`） | `>= 0`；`<=0` 直接 return（`Blur.kt:14`） | **无库默认**（必填实参）`[源码]` | **没有自定义 shader**：走平台 `BlurEffect`（`RenderEffect.createBlurEffect`）`[源码]` | `slider 0..10 dp`（ytbl 侧上限 `BLUR_RADIUS_MAX_DP=10.0`，`GlassConfig.h:69`）`[源码]`；Kyant 组件实际用到 **16 dp**（见下表，超出 ytbl 上限） |
| `edgeTreatment` | `Blur.kt:11` | `androidx.compose.ui.graphics.TileMode` | `Clamp`（默认）/ `Decal` / `Repeat` / `Mirror` | `TileMode.Clamp` `[源码]` | 决定 `createBlurEffect` 越界采样方式；同时**决定是否扩 `padding`** | `combo: Clamp / Decal / Repeat / Mirror` |
| `padding`（隐式副作用） | `Blur.kt:16-20` | `Float`（px，`BackdropEffectScope.padding`，`BackdropEffectScope.kt:18`） | `0..radius` | 起始 `0f`（`BackdropEffectScope.kt:29`、`:60`） | 不是 shader uniform，而是**离屏图层向外扩的像素数**（`DrawBackdropModifier.kt:290-302`）。`edgeTreatment != Clamp` **或** 已存在前序 effect 时，`padding = max(padding, radius)` | `slider 0..16 px`（只读显示更有用） |
| 模糊类型（Gaussian/Box/…） | —— | —— | —— | —— | **不存在** `[源码]`：库只调 `BlurEffect`，没有模糊核选择 | 不要做这个控件 |
| 降采样（downsample / 采样率） | —— | —— | —— | —— | **库层不存在** `[源码]`。降采样是平台 `RenderEffect` 的内部行为 `[推断]`。ytbl **有**编译期常量 `BLUR_DECIMATE_LIMIT=16` / `BLUR_TARGET_MIN_SPAN=8` / `BLUR_STEP_FROM_SIGMA=0.25` / `BLUR_TAPS=12`（`GlassConfig.h:56-63`）`[源码]`，但**不在 `PanelStyle` 里** | 若要做：只能做成 ytbl 编译期/初始化期参数，见 §4.3 |

### 各处实际用到的模糊半径（**这些就是"每种效果的样子"**）

| 值（dp） | 调用点 | 标记 |
| --- | --- | --- |
| `2f.dp` | `app/.../components/LiquidButton.kt:60` `blur(2f.dp.toPx())` | `[源码]` |
| `8f.dp * (1 - pressProgress)` | `app/.../components/LiquidToggle.kt:160` | `[源码]` |
| `8f.dp * (1 - progress)` | `app/.../components/LiquidSlider.kt:169` | `[源码]` |
| `8f.dp`（外层容器）/ `8f.dp`（内层）/ `0`（指示器无 blur） | `app/.../components/LiquidBottomTabs.kt:171` / `:209` / `:244-251` | `[源码]` |
| `4f.dp` | `app/.../destinations/GlassPlaygroundContent.kt:132` | `[源码]` |
| `blurRadiusDp`（面板初值 `0f`，滑杆 `0f..32f`） | `GlassPlaygroundContent.kt:55, 71, 159` | `[源码]` |
| 亮 `16f.dp` / 暗 `8f.dp` | `app/.../destinations/DialogContent.kt:66` | `[源码]` ★ **超出 ytbl 10dp 上限** |
| `4f.dp` | `app/.../destinations/ProgressiveBlurContent.kt:40` | `[源码]` |
| `lerp(8,16,l)` / `lerp(8,2,-l)` | `app/.../destinations/AdaptiveLuminanceGlassContent.kt:104-107` | `[源码]` |
| `2f.dp` | `app/.../destinations/LockScreenContent.kt:64` | `[源码]` |
| `4f.dp * progress`（**整屏**，`graphicsLayer.renderEffect`，不是 backdrop） | `app/.../destinations/ControlCenterContent.kt:197-200` | `[源码]` |

---

## 1.2 Lens（折射 / 色散 / 景深）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/effects/Lens.kt`（**共 120 行**）

```kotlin
// Lens.kt:16-60
fun BackdropEffectScope.lens(
    @FloatRange(from = 0.0) refractionHeight: Float,
    @FloatRange(from = 0.0) refractionAmount: Float,
    depthEffect: Boolean = false,
    chromaticAberration: Boolean = false
) {
    if (!isRuntimeShaderSupported()) return
    if (refractionHeight <= 0f || refractionAmount <= 0f) return

    if (padding > 0f) {
        padding = (padding - refractionHeight).fastCoerceAtLeast(0f)
    }

    val cornerRadii = cornerRadii
    val effect =
        if (cornerRadii != null) {
            val shader =
                if (!chromaticAberration) {
                    obtainRuntimeShader("Refraction", RoundedRectRefractionShaderString)
                } else {
                    obtainRuntimeShader(
                        "RefractionWithDispersion",
                        RoundedRectRefractionWithDispersionShaderString
                    )
                }
            shader.apply {
                setFloatUniform("size", size.width, size.height)
                setFloatUniform("offset", -padding, -padding)
                setFloatUniform("cornerRadii", cornerRadii)
                setFloatUniform("refractionHeight", refractionHeight)
                setFloatUniform("refractionAmount", -refractionAmount)   // ★ 取负
                setFloatUniform("depthEffect", if (depthEffect) 1f else 0f)
                if (chromaticAberration) {
                    setFloatUniform("chromaticAberration", 1f)            // ★ 恒为 1
                }
            }
            RuntimeShaderEffect(shader, "content")
        } else {
            throwUnsupportedSDFException()
        }
    effect(effect)
}
```

| 参数 | 源码位置 | 类型 | 范围/取值 | 默认值 | 在 shader 里的作用 | 对应调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `refractionHeight` | `Lens.kt:17`；喂给 uniform 在 `:48` | `Float`（px） | `> 0`（`<=0` 直接 return，`:23`）；ytbl 侧上限 `LENS_REACH_MAX = 128.0`（`GlassConfig.h:70`） | **无库默认**（必填）`[源码]` | `uniform float refractionHeight`：折射**影响带宽度**。`if (-sd >= refractionHeight) return content.eval(coord);`（`Shaders.kt:73`）——只有距边缘 `refractionHeight` 以内的像素才被折射，内部原样透出 | `slider 0..40 dp`（ytbl 现有控件范围，`secondary_imgui.cpp:570`）；`→ uLensHeight` |
| `refractionAmount` | `Lens.kt:18`；`:49` | `Float`（px） | `> 0`（`<=0` return）；ytbl 上限 128 px | **无库默认**（必填）`[源码]` | `uniform float refractionAmount`，**传负值**：`float d = circleMap(1.0 - -sd / refractionHeight) * refractionAmount;`（`Shaders.kt:78`）→ `refractedCoord = coord + d * grad`（`:82`） | `slider 0..60 dp`（`secondary_imgui.cpp:571`）；`→ uLensAmount` |
| `depthEffect` | `Lens.kt:19`；`:50` | `Boolean` → `0f/1f` | `false`/`true` | **`false`** `[源码]`（`Lens.kt:19`） | `grad = normalize(gradSdRoundedRect(...) + depthEffect * normalize(centeredCoord))`（`Shaders.kt:80`）——为真时法线混入「从中心指向外」的径向分量，产生**凸透镜纵深** | `checkbox 景深`；`→ uLensDepth`（ytbl 是 `float depthAmount`，0..1 连续） |
| `chromaticAberration` | `Lens.kt:20`；`:51-53` | `Boolean` → `1f` | `false`/`true` | **`false`** `[源码]` | **★ 不是单纯的 uniform 开关**：它**换了一整个 shader 字符串**（`Lens.kt:33-43`，`Shaders.kt:51-84` vs `:87-158`），并在色散 shader 里把 `chromaticAberration` 恒设为 `1f`（`:52`） | `checkbox 色散`；`→ uSpectral > 0`（ytbl 的 `uSpectral` 是 0..2 连续量，更灵活，见 §4.2） |
| `padding`（隐式副作用） | `Lens.kt:25-27` | `Float`（px） | `[0, 前序 padding]` | —— | 若已有 `padding`，**减去 `refractionHeight`**（因为折射只在内部采样，不需要外扩）。`offset` uniform 直接吃 `-padding`（`Lens.kt:46`） | 只读显示 |

### 1.2.1 「边缘处理」到底是什么

- **没有**独立的边缘处理参数 `[源码]`。折射的径向量**硬编码**为 `circleMap`：
  ```glsl
  // Shaders.kt:63-65
  float circleMap(float x) {
      return 1.0 - sqrt(1.0 - x * x);
  }
  ```
  配合 `float d = circleMap(1.0 - -sd / refractionHeight) * refractionAmount;`（`Shaders.kt:78`）：
  `x = 1 - |sd|/height ∈ [0,1]`，`x=0`（边缘）→ `d=0`，`x=1`（影响带内边界）→ `d=amount`。`[源码]`+`[推断]`
- **法线场的圆角放大系数**硬编码为 **1.5**：
  ```glsl
  // Shaders.kt:79（折射 shader）
  float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
  ```
  同式出现在 `Shaders.kt:116`（色散版）与 `Shaders.kt:175` / `:197`（两种高光 shader）。`[源码]`
  ⇒ **`1.5` 是编译期常量，改它要改 shader 字符串**。
- **法线退化兜底**：`gradSdRoundedRect` 在矩形内部返回轴向法线（`Shaders.kt:40-47`），所以面板正中的 `d` 靠 `depthEffect` 才有方向。

### 1.2.2 支持哪些形状（决定"圆角参数"能不能进 lens）

```kotlin
// Lens.kt:62-114（节选）
private val BackdropEffectScope.cornerRadii: FloatArray?
    get() = when (val shape = shape) {
        is RoundedRectangularShape -> { ... corners.topLeft/topRight/bottomRight/bottomLeft ... }
        is AbsoluteRoundedCornerShape -> { ... 每角独立，clamp 到 size.minDimension/2f ... }
        is CornerBasedShape -> { ... 支持 LTR/RTL 镜像 ... }
        else -> null
    }

// Lens.kt:116-120
private fun throwUnsupportedSDFException(): Nothing {
    throw UnsupportedOperationException(
        "Only RoundedRectangularShape or CornerBasedShape is supported in lens effects."
    )
}
```

- **uniform `cornerRadii` 的数组顺序 = `[topLeft, topRight, bottomRight, bottomLeft]`**（`Lens.kt:66-71`）。`[源码]`
- 半径上限 `size.minDimension / 2f`（`Lens.kt:76`、`:91`）。`[源码]`
- `Capsule()` 能用于 `lens()`（`LiquidButton.kt:57,61` 同时使用），说明 `com.kyant.shapes.Capsule` 实现了 `RoundedRectangularShape` 或 `CornerBasedShape` `[推断]`。

### 1.2.3 各处实际用到的透镜参数（**成品效果即此**）

| 折射高度 / 强度 | 形状 | 其它开关 | 调用点 |
| --- | --- | --- | --- |
| `12dp` / `24dp` | `Capsule()` | 无 | `LiquidButton.kt:61` |
| `5dp*pp` / `10dp*pp` | `Capsule()` | `chromaticAberration=true` | `LiquidToggle.kt:161-165` |
| `10dp*pp` / `14dp*pp` | `Capsule()` | `chromaticAberration=true` | `LiquidSlider.kt:170-174` |
| `24dp` / `24dp` | `Capsule()` | 无 | `LiquidBottomTabs.kt:172` |
| `24dp*pp` / `24dp*pp` | `Capsule()` | 无（内层，`:210-213`） | `LiquidBottomTabs.kt:210-213` |
| `10dp*pp` / `14dp*pp` | `Capsule()` | `chromaticAberration=true`（指示器，`:246-250`） | `LiquidBottomTabs.kt:246-250` |
| `16dp` / `32dp` | `RoundedRectangle(32dp)` | 无 | `GlassPlaygroundContent.kt:133`（Sheet） |
| `heightFrac*minDim*0.5` / `amountFrac*minDim` | `RoundedRectangle(128dp*frac)` | `depthEffect=true`，色散按 `chromaticAberration > 0f` | `GlassPlaygroundContent.kt:72-77` |
| `24dp` / `48dp` | `RoundedRectangle(48dp)` | `depthEffect=true` | `DialogContent.kt:67` |
| `24dp` / `size.minDimension/2f` | `RoundedRectangle(24dp)` | `depthEffect=true` | `AdaptiveLuminanceGlassContent.kt:108` |
| `8dp` / `24dp` | `Capsule()` | `depthEffect=true, chromaticAberration=true` | `MagnifierContent.kt:87-92` |
| `16dp` / `32dp` | `RoundedRectangle(32dp)` | 无 | `ScrollContainerContent.kt:43`、`LazyScrollContainerContent.kt:43` |
| `24dp*progress` / `48dp*progress` | `RoundedRectangle(34dp)` | `depthEffect=true` | `ControlCenterContent.kt:119-123` |

---

## 1.3 ColorFilter（色彩处理：亮度/对比度/饱和度/透明度）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/effects/ColorFilter.kt`（**共 75 行**）

```kotlin
// ColorFilter.kt:17-45
fun BackdropEffectScope.opacity(@FloatRange(from = 0.0, to = 1.0) alpha: Float) {
    val colorMatrix = ColorMatrix(
        floatArrayOf(
            1f, 0f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f, 0f,
            0f, 0f, 1f, 0f, 0f,
            0f, 0f, 0f, alpha, 0f
        )
    )
    colorFilter(ColorMatrixColorFilter(colorMatrix))
}

fun BackdropEffectScope.colorControls(
    brightness: Float = 0f,
    contrast: Float = 1f,
    saturation: Float = 1f
) {
    if (brightness == 0f && contrast == 1f && saturation == 1f) {
        return
    }
    colorFilter(colorControlsColorFilter(brightness, contrast, saturation))
}

private val VibrantColorFilter = colorControlsColorFilter(saturation = 1.5f)

fun BackdropEffectScope.vibrancy() {
    colorFilter(VibrantColorFilter)
}
```

核心矩阵（**逐字**）：

```kotlin
// ColorFilter.kt:47-74
private fun colorControlsColorFilter(
    brightness: Float = 0f, contrast: Float = 1f, saturation: Float = 1f
): ColorFilter {
    val invSat = 1f - saturation
    val r = 0.213f * invSat
    val g = 0.715f * invSat
    val b = 0.072f * invSat

    val c = contrast
    val t = (0.5f - c * 0.5f + brightness) * 255f     // ★ 8bit 域的偏移
    val s = saturation

    val cr = c * r
    val cg = c * g
    val cb = c * b
    val cs = c * s

    val colorMatrix = ColorMatrix(
        floatArrayOf(
            cr + cs, cg,      cb,      0f, t,
            cr,      cg + cs, cb,      0f, t,
            cr,      cg,      cb + cs, 0f, t,
            0f,      0f,      0f,      1f, 0f
        )
    )
    return ColorMatrixColorFilter(colorMatrix)
}
```

| 参数 | 源码位置 | 类型 | 范围/取值 | 默认值 | 在 shader 里的作用 | 对应调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `brightness` | `ColorFilter.kt:30` | `Float` | 无硬约束；组件用到 `0.2 / 0.5 / -0.2 / -0.1 / 0.1` | **`0f`** `[源码]` | 进入 `t = (0.5 - c*0.5 + brightness) * 255f`（`:58`），即矩阵第 5 列偏移 | `slider -1.0 .. 1.0`；`→ uToneCtrl.x` |
| `contrast` | `ColorFilter.kt:31` | `Float` | 无硬约束；组件用到 `1.0 / 0.75 / 0..1` | **`1f`** `[源码]` | 乘到 RGB 三行系数（`c * r` 等，`:61-64`）**并**进入 `t` 的 `-c*0.5` 项（`:58`） | `slider 0.0 .. 2.0`；`→ uToneCtrl.y`（ytbl 侧 clamp `[0,2]`，`GlassPanel.cpp:250`） |
| `saturation` | `ColorFilter.kt:32` | `Float` | 无硬约束；组件用到 `1.5` | **`1f`** `[源码]` | `invSat = 1 - s`；`r=0.213*invSat, g=0.715*invSat, b=0.072*invSat`（`:52-55`） | `slider 0.0 .. 3.0`；`→ uToneCtrl.z`（ytbl 侧 clamp `[0,3]`，`GlassPanel.cpp:251`） |
| 亮度/对比度/饱和度权重 | `ColorFilter.kt:53-55` | 常量 | `0.213 / 0.715 / 0.072` | —— | 灰度化权重（Rec.709 系） | **编译期/代码常量**；ytbl 已用同值（`Shaders.cpp:276`） |
| `alpha`（`opacity()`） | `ColorFilter.kt:17` | `Float` | `[0,1]`（`@FloatRange`，但**无运行时 clamp**） | **无库默认**（必填） | 矩阵第 4 行第 4 列（`:23`） | `slider 0.0 .. 1.0`；`→ uPanelOpacity` |
| `colorFilter`（通用入口） | `ColorFilter.kt:11` | Compose `ColorFilter` | 任意 | —— | 直接包成 `ColorFilterEffect`，**链在当前 `renderEffect` 外层**（`ColorFilterEffect(renderEffect, colorFilter)`，`:14`） | 无需控件（内部 API） |
| `vibrancy()` | `ColorFilter.kt:41-45` | 无参 | 等价 `saturation = 1.5f` | —— | `colorControlsColorFilter(saturation = 1.5f)` | `checkbox 高明度(vibrancy)`；ytbl 对应 `optics.colorBoost`（`GlassUI.h:296`），shader 里 `if (uToneBoost > 0.5) ctrl.z *= 1.5;`（`Shaders.cpp:370`）**完全等价** `[源码]` |
| **色相（hue）/ 色调叠加（tint）** | —— | —— | —— | —— | **库层不存在** `[源码]`。Kyant 把 tint 放在**应用层**：`LiquidButton.kt:91-99` 的 `onDrawSurface`：先 `drawRect(tint, blendMode = BlendMode.Hue)`，再 `drawRect(tint.copy(alpha = 0.75f))`。ytbl 已把它做成 `SurfaceMode::Hue`（`GlassUI.h:310`；`Shaders.cpp:384-389`） | 若要做：控件为 `color picker` + `slider alpha 0..1`（Kyant 硬编码 0.75） |

### 各处实际用到的色彩参数

| 值 | 调用点 | 标记 |
| --- | --- | --- |
| `colorControls(brightness = 0.2 或 0, saturation = 1.5)` | `DialogContent.kt:62-65` | `[源码]` |
| `brightness ∈ [lerp(0.1,0.5,l)] / [lerp(0.1,-0.2,-l)]`，`contrast = lerp(1,0,l)`，`saturation = 1.5` | `AdaptiveLuminanceGlassContent.kt:95-103` | `[源码]` |
| `colorControls(brightness = -0.1, contrast = 0.75, saturation = 1.5)` | `LockScreenContent.kt:63` | `[源码]` |
| `vibrancy()` | `LiquidButton.kt:59`、`LiquidBottomTabs.kt:170, 208`、`GlassPlaygroundContent.kt:70, 131`、`ControlCenterContent.kt:118`、`ScrollContainerContent.kt:42`、`LazyScrollContainerContent.kt:42` | `[源码]` |

---

## 1.4 Highlight / EdgeLight（边缘高光）

### 1.4.1 数据模型

```kotlin
// Highlight.kt:9-28（全文）
@Immutable
data class Highlight(
    val width: Dp = 0.5f.dp,
    val blurRadius: Dp = width / 2f,
    @param:FloatRange(from = 0.0, to = 1.0) val alpha: Float = 1f,
    val style: HighlightStyle = HighlightStyle.Default
) {
    companion object {
        @Stable val Default: Highlight = Highlight()
        @Stable val Ambient: Highlight = Highlight(style = HighlightStyle.Ambient)
        @Stable val Plain: Highlight = Highlight(style = HighlightStyle.Plain)
    }
}
```

| 参数 | 源码位置 | 类型 | 范围/取值 | 默认值 | 作用 | 调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `width` | `Highlight.kt:11` | `Dp` | `> 0`（`HighlightModifier.kt:89` 里 `width.value <= 0f` 时整条高光不画） | **`0.5.dp`** `[库默认]` | 描边宽度。**注意实现细节**：`paint.strokeWidth = ceil(width.toPx().fastCoerceAtMost(size.minDimension / 2f)) * 2f`（`HighlightModifier.kt:153`）——**实际描边宽 = 2×width**，并以轮廓为中心左右各摊 `width` | `slider 0.0 .. 3.0 dp`；`→ EdgeLight.widthDp` |
| `blurRadius` | `Highlight.kt:12` | `Dp` | `>= 0` | **`width / 2f`**（跟随 width）`[库默认]` | `paint.blur(blurRadius.toPx())`（`HighlightModifier.kt:154`）；Android 侧为 `BlurMaskFilter(radius, Blur.NORMAL)`（`src/androidMain/.../internal/Paint.kt:8-12`） | `slider 0.0 .. 1.5 dp`；`→ EdgeLight.softnessDp` |
| `alpha` | `Highlight.kt:13` | `Float` | `[0,1]`（`@FloatRange`） | **`1f`** `[库默认]` | `highlightLayer.alpha = highlight.alpha`（`HighlightModifier.kt:117`），整层透明度 | `slider 0.0 .. 1.0`；`→ EdgeLight.alpha` |
| `style` | `Highlight.kt:14` | `HighlightStyle` | `Plain` / `Default` / `Ambient` | **`HighlightStyle.Default`** `[库默认]` | 决定颜色、混合模式、以及**是否/用哪个 shader** | `combo: Gradient(Default) / Ambient / Plain`；`→ uEdgeMode` |

### 1.4.2 三种 Style 的完整参数（**逐字**）

```kotlin
// HighlightStyle.kt:32-42  ── Plain
@Immutable
data class Plain(
    override val color: Color = Color.White.copy(alpha = 0.38f),
    override val blendMode: BlendMode = BlendMode.Plus
) : HighlightStyle {
    override fun DrawScope.createShader(shape: Shape, runtimeShaderCache: RuntimeShaderCache): RuntimeShader? = null
}
```

```kotlin
// HighlightStyle.kt:44-71  ── Default（= 面板里的 "Gradient"）
@Immutable
data class Default(
    override val color: Color = Color.White.copy(alpha = 0.5f),
    override val blendMode: BlendMode = BlendMode.Plus,
    val angle: Float = 45f,
    @param:FloatRange(from = 0.0) val falloff: Float = 1f
) : HighlightStyle {
    override fun DrawScope.createShader(shape: Shape, runtimeShaderCache: RuntimeShaderCache): RuntimeShader? {
        return if (isRuntimeShaderSupported()) {
            runtimeShaderCache.obtainRuntimeShader("Default", DefaultHighlightShaderString).apply {
                setFloatUniform("size", size.width, size.height)
                setFloatUniform("cornerRadii", getCornerRadii(shape))
                setColorUniform("color", color.copy(alpha = 1f))     // ★ alpha 强制 1
                setFloatUniform("angle", angle * (PI / 180f).toFloat())
                setFloatUniform("falloff", falloff)
            }
        } else null
    }
}
```

```kotlin
// HighlightStyle.kt:73-100  ── Ambient
@Immutable
data class Ambient(
    @param:FloatRange(from = 0.0, to = 1.0) val intensity: Float = 0.38f
) : HighlightStyle {
    override val color: Color = Color.White.copy(alpha = intensity)
    override val blendMode: BlendMode = DrawScope.DefaultBlendMode      // ★ 不是 Plus
    override fun DrawScope.createShader(shape: Shape, runtimeShaderCache: RuntimeShaderCache): RuntimeShader? {
        return if (isRuntimeShaderSupported()) {
            runtimeShaderCache.obtainRuntimeShader("Ambient", AmbientHighlightShaderString).apply {
                setFloatUniform("size", size.width, size.height)
                setFloatUniform("cornerRadii", getCornerRadii(shape))
                setFloatUniform("angle", 45f * (PI / 180f).toFloat())   // ★ 硬编码
                setFloatUniform("falloff", 1f)                          // ★ 硬编码
            }
        } else null
    }
}
```

| Style | 参数 | 源码位置 | 类型 | 范围 | 默认 | shader 里的作用 | 控件建议 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Default` | `color` | `HighlightStyle.kt:46` | `Color` | —— | `White @ 0.5` `[库默认]` | `setColorUniform("color", color.copy(alpha = 1f))`（`:63`）→ 进 `DefaultHighlightShaderString` 的 `return color * intensity;`（`Shaders.kt:180`）。**alpha 由 paint 承担**（见 §1.4.3） | `color picker`（默认 `#80FFFFFF`）；`→ uEdgeTint` |
| `Default` | `blendMode` | `HighlightStyle.kt:47` | `BlendMode` | —— | `BlendMode.Plus` `[源码]` | `highlightLayer.blendMode = highlight.style.blendMode`（`HighlightModifier.kt:118`） | `combo: Plus / SrcOver / Screen / …` |
| `Default` | `angle` | `HighlightStyle.kt:48`；`:64` | `Float`（度） | 任意 | **`45f`** `[源码]` | `float2 normal = float2(cos(angle), sin(angle)); float d = dot(grad, normal);`（`Shaders.kt:177-178`） | `slider -180 .. 180 deg`；`→ uEdgeAngle`。**★ 有动态来源**：`ControlCenterContent.kt:99-106` 用 `uiSensor.gravityAngle`（重力感应角） |
| `Default` | `falloff` | `HighlightStyle.kt:49`；`:65` | `Float` | `>= 0`（`@FloatRange(from = 0.0)`） | **`1f`** `[源码]` | `float intensity = pow(abs(d), falloff);`（`Shaders.kt:179`） | `slider 0.0 .. 8.0`；`→ uEdgeFalloff`。组件实际用 `2f`（`ControlCenterContent.kt:103`）、ytbl Contour 预设 `1.35`（`GlassConfig.h:91`） |
| `Ambient` | `intensity` | `HighlightStyle.kt:75`；`:78` | `Float` | `[0,1]` | **`0.38f`** `[库默认]` | 同时决定 `color.alpha`（`:78`）与 shader 里的乘法 `return half4(t,t,t,1.0) * intensity;`（`Shaders.kt:203`） | `slider 0.0 .. 1.0` |
| `Ambient` | `angle` / `falloff` | `HighlightStyle.kt:93-94` | —— | —— | **硬编码 `45°` / `1f`** `[源码]` | —— | ★ **不可调**（改需改 Kotlin 代码） |
| `Ambient` | `blendMode` | `HighlightStyle.kt:80` | —— | —— | `DrawScope.DefaultBlendMode`（= `SrcOver`）`[源码]` | —— | ytbl 侧**没有 SrcOver 分支**，见 §4.3 缺口 |
| `Plain` | `color` | `HighlightStyle.kt:34` | `Color` | —— | `White @ 0.38` `[库默认]` | **无 shader**（`createShader` 返回 null，`:41`）→ 纯描边圆环 + `BlurMaskFilter` | 同 `uEdgeTint`（ytbl `EDGE_TINT_PLAIN = 0x61FFFFFF` = 0.38，`GlassConfig.h:127`）`[源码]` |
| `Plain` | `blendMode` | `HighlightStyle.kt:35` | —— | —— | `BlendMode.Plus` `[源码]` | —— | —— |

### 1.4.3 绘制实现里的隐式常量（**改它们要改 Kotlin 代码**）

```kotlin
// HighlightModifier.kt:151-165
private fun DrawScope.configurePaint(highlight: Highlight) {
    paint.color = highlight.style.color
    paint.strokeWidth = ceil(highlight.width.toPx().fastCoerceAtMost(size.minDimension / 2f)) * 2f
    paint.blur(highlight.blurRadius.toPx())
    if (isRuntimeShaderSupported()) {
        val shader = with(highlight.style) {
            createShader(shape = shapeProvider.shape, runtimeShaderCache = runtimeShaderCache)
        }
        paint.setRuntimeShader(shader)
    }
}
```

| 隐式常量 | 位置 | 值 | 说明 |
| --- | --- | --- | --- |
| 描边宽度倍率 | `HighlightModifier.kt:153` | `* 2f` | 实际描边 = 2×`width` |
| 半径上限 | `HighlightModifier.kt:153` | `size.minDimension / 2f` | 超过则被夹 |
| 图层安全边 | `HighlightModifier.kt:101-105, 120, 129` | `+2 px`、`translate(1f, 1f)` / `translate(-1f, -1f)` | 防止描边被裁 |
| `Default` 的 gradRadius 倍率 | `Shaders.kt:175` | `radius * 1.5` | ★ ytbl 缺失此 ×1.5，见 §4.3 |
| `Ambient` 的 gradRadius 倍率 | `Shaders.kt:197` | `radius * 1.5` | 同上 |
| `Ambient` 的硬编码角度/衰减 | `Shaders.kt:199-202` | `float2(cos(angle), sin(angle))`；`float t = step(0.0, d);` | 半空间阶跃 + 幂衰减 |
| Skia 的 paint alpha 会调制 shader 输出 | —— | —— | `[推断]`（Skia 行为，本仓库无源码）。若成立，`Ambient` 的强度会被平方（0.38² ≈ 0.144）；`[未核实]` |

---

## 1.5 Shadow（外阴影）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/shadow/Shadow.kt`（**共 27 行**）

```kotlin
// Shadow.kt:13-27
@Immutable
data class Shadow(
    val radius: Dp = 24f.dp,
    val offset: DpOffset = DpOffset(0f.dp, radius / 6f),
    val color: Color = Color.Black.copy(alpha = 0.1f),
    @param:FloatRange(from = 0.0, to = 1.0) val alpha: Float = 1f,
    val blendMode: BlendMode = DrawScope.DefaultBlendMode
) {
    companion object { @Stable val Default: Shadow = Shadow() }
}
```

| 参数 | 源码位置 | 类型 | 范围 | 默认值 | 在 shader/绘制里的作用 | 调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `radius` | `Shadow.kt:15` | `Dp` | `>= 0` | **`24f.dp`** `[库默认]` | 两个用途：① `paint.blur(shadow.radius.toPx())`（`ShadowModifier.kt:131`）；② 决定图层外扩 `shadowSize = size + radius*4 + offset`（`ShadowModifier.kt:85-88`） | `slider 0.0 .. 48.0 dp`；`→ DropShadow.radiusDp` |
| `offset` | `Shadow.kt:16` | `DpOffset` | 任意 | **`DpOffset(0f.dp, radius / 6f)`**（= 4dp @ radius 24）`[库默认]` | 阴影层平移（`ShadowModifier.kt:96`），并用 `BlendMode.Clear` 挖掉本体（`:99-101`） | 两个 `slider -48 .. 48 dp`（X/Y）；`→ DropShadow.offsetXDp/offsetYDp` |
| `color` | `Shadow.kt:17` | `Color` | —— | **`Black @ 0.1`** `[库默认]` | `paint.color = shadow.color`（`ShadowModifier.kt:130`） | `color picker`（默认 `#1A000000`）；ytbl `SHADOW_TINT_SOFT = 0x0D000000u`（`GlassConfig.h:122`）`[源码]` |
| `alpha` | `Shadow.kt:18` | `Float` | `[0,1]` | **`1f`** `[库默认]` | `shadowLayer.alpha = shadow.alpha`（`ShadowModifier.kt:93`） | `slider 0.0 .. 1.0`；`→ DropShadow.alpha` |
| `blendMode` | `Shadow.kt:19` | `BlendMode` | —— | `DrawScope.DefaultBlendMode`（SrcOver）`[源码]` | `shadowLayer.blendMode`（`ShadowModifier.kt:94`） | `combo`；**ytbl 缺口**（`DropShadow` 无 blendMode 字段） |
| **`spread`** | —— | —— | —— | —— | **不存在** `[源码]`：`Shadow.kt` 没有 spread 参数，`ShadowModifier` 也没有对应的 MaskFilter/几何外扩。**不要凭空加这个控件**（若加，标 `[推断]`） | —— |

**代码常量**：`radius * 4f` 的外扩量（`ShadowModifier.kt:86-87`）、`radius * 2f` 的绘制平移（`:96`、`:105`）——**改要改 Kotlin**。

---

## 1.6 InnerShadow（内阴影）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/shadow/InnerShadow.kt`（**共 41 行**）

```kotlin
// InnerShadow.kt:16-30
@Immutable
data class InnerShadow(
    val radius: Dp = 24f.dp,
    val offset: DpOffset = DpOffset(0f.dp, radius),
    val color: Color = Color.Black.copy(alpha = 0.15f),
    @param:FloatRange(from = 0.0, to = 1.0) val alpha: Float = 1f,
    val blendMode: BlendMode = DrawScope.DefaultBlendMode
) {
    companion object { @Stable val Default: InnerShadow = InnerShadow() }
}
```

| 参数 | 源码位置 | 类型 | 范围 | 默认值 | 作用 | 调试控件建议 |
| --- | --- | --- | --- | --- | --- | --- |
| `radius` | `InnerShadow.kt:18` | `Dp` | `>= 0`（`0` → `renderEffect = null`，`InnerShadowModifier.kt:107-115`） | **`24f.dp`** `[库默认]` | `BlurEffect(radius, radius, TileMode.Decal)` 施加到离屏层（`InnerShadowModifier.kt:110`） | `slider 0.0 .. 32.0 dp`；`→ InsetShadow.radiusDp` |
| `offset` | `InnerShadow.kt:19` | `DpOffset` | 任意 | **`DpOffset(0f.dp, radius)`**（= 24dp @ radius 24）`[库默认]` | 挖洞平移（`InnerShadowModifier.kt:121-123`） | `slider -32 .. 32 dp` |
| `color` | `InnerShadow.kt:20` | `Color` | —— | **`Black @ 0.15`** `[库默认]` | `paint.color`（`InnerShadowModifier.kt:152`；**注意 `configurePaint` 只有 color，没有 blur**） | `color picker`（默认 `#26000000`）；ytbl `InsetShadow.color` 默认正是 `0x26000000u`（`GlassUI.h:285`）`[源码]` |
| `alpha` | `InnerShadow.kt:21` | `Float` | `[0,1]` | **`1f`** `[库默认]` | `shadowLayer.alpha`（`InnerShadowModifier.kt:105`） | `slider 0.0 .. 1.0` |
| `blendMode` | `InnerShadow.kt:22` | `BlendMode` | —— | SrcOver `[源码]` | `shadowLayer.blendMode`（`:106`） | `combo` |
| `TileMode` | `InnerShadowModifier.kt:110` | 枚举 | `Decal` | **硬编码 `Decal`** `[源码]` | 模糊越界处理 | ★ 与外层 `blur()` 的默认 `Clamp` **不同**，值得做一个 combo |
| **`spread`** | —— | —— | —— | —— | **不存在** `[源码]` | —— |

**组件实际用法**：`LiquidToggle.kt:181-187` `InnerShadow(radius = 4.dp * pp, alpha = pp)`；`LiquidSlider.kt:190-196` 同；`LiquidBottomTabs.kt:260-266` `radius = 8.dp * pp`；`MagnifierContent.kt:94` `InnerShadow(radius = 16.dp)`；`LiquidBottomTabs.kt:258` `Shadow(alpha = progress)`（其余全默认 → `radius=24dp, offset=(0,4dp), Black@0.1`）`[源码]`。

---

## 1.7 Surface / Tint（表面色）

| 项 | 结论 | 出处 |
| --- | --- | --- |
| 库层是否有 surface/tint 参数 | **没有** `[源码]`。`drawBackdrop` 提供的是 `onDrawSurface: (DrawScope.() -> Unit)?` 这个**绘制回调**，颜色与混合模式由调用者自己写 | `DrawBackdropModifier.kt:91`（形参）、`:323`（调用时机：在 backdrop 之后、内容之前） |
| 实际用法（组件） | `LiquidButton.kt:91-99`：`drawRect(tint, blendMode = BlendMode.Hue)` + `drawRect(tint.copy(alpha = 0.75f))` + `drawRect(surfaceColor)` | `[源码]` |
| 其它容器的 surface | `LiquidBottomTabs.kt:180, 219` `drawRect(containerColor)`；`LiquidToggle.kt:197`/`LiquidSlider.kt:206` `drawRect(Color.White.copy(alpha = 1f - pressProgress))`；`LiquidBottomTabs.kt:274-282` 两段（`Black/White @ 0.1, alpha=1-pp` + `Black @ 0.03*pp`）；`DialogContent.kt:70` `drawRect(containerColor)`；`ControlCenterContent.kt:64, 115` `drawRect(Color.Black.copy(0.05f))` | `[源码]` |
| 混合模式 | 由 `onDrawSurface` 内部 `drawRect(..., blendMode=...)` 决定，**不是库参数** | `[源码]` |
| ytbl 对应能力 | `PanelStyle.surface: Rgba` + `surfaceMode: SurfaceMode{None,Hue,Overlay}`（`GlassUI.h:332-333`）→ shader `uSurfaceTint` / `uSurfaceMode`（`Shaders.cpp:142-144`，逻辑 `:374-389`） | `[源码]` — **ytbl 比库层更强** |

---

## 1.8 形状（圆角 / 连续曲率 / 各角独立 / 变换）

### 1.8.1 形状参数从哪来

| 项 | 结论 | 出处 |
| --- | --- | --- |
| 形状入口 | `drawBackdrop(shape = { ... })` / `drawPlainBackdrop(shape = { ... })`，类型 `() -> Shape` | `DrawBackdropModifier.kt:82` / `:47` |
| 形状如何进 shader | `Lens.kt:62-114` 从 `Shape` 反解出 **4 个角的像素半径**，顺序 `[topLeft, topRight, bottomRight, bottomLeft]`，各自 clamp 到 `minDimension/2` | `[源码]` |
| 形状如何进绘制 | `ShapeProvider.shape.createOutline(...)` → `canvas.clipOutline(outline, clipPath)`（高光/内阴影）/ `canvas.drawOutline(outline, paint)`（阴影） | `ShapeProvider.kt:22-43`；`Outline.kt:7-18`；`HighlightModifier.kt:107-124`；`ShadowModifier.kt:89-101`；`InnerShadowModifier.kt:95-124` |
| 图层裁剪 | `layoutLayerBlock = { clip = true; shape = shapeProvider.shape; compositingStrategy = Offscreen }` | `DrawBackdropModifier.kt:254-258` `[源码]` |
| 库用到的三个形状 | `com.kyant.shapes.RoundedRectangle(radius)`、`Capsule()`、`Rectangle` | `[源码]`：全文出现的形状调用点见 §1.8.2 |
| 连续曲率（squircle）参数 | **调用点只传一个 `radius`，没有 smoothing 参数** `[源码]`（所有 `RoundedRectangle(...)` 调用点见 §1.8.2，全部单实参）。`com.kyant.shapes` 的 `RoundedRectangle` 本身就是**连续曲率**圆角（本仓库已按 `RoundedRectangleOutline.kt` 数值移植，见 `build/recon/squircle_path.h:6-7, 87`） | 平滑度是否可调 → `[未核实]`（`shapes` 库源码不在本地） |
| 各角独立半径 | 库层通过 `AbsoluteRoundedCornerShape` / `CornerBasedShape` 支持（`Lens.kt:74-111`）；ytbl 用 `Corners(tl,tr,br,bl)`（`GlassUI.h:104-105`） | `[源码]` |

### 1.8.2 全部形状调用点（**逐字**，用于确定控件默认值）

| 形状表达式 | 调用点 |
| --- | --- |
| `Capsule()` | `LiquidButton.kt:57`、`LiquidToggle.kt:124, 157`、`LiquidSlider.kt:114, 133, 166`、`LiquidBottomTab.kt:31`、`LiquidBottomTabs.kt:168, 205, 243`、`ControlCenterContent.kt:73`、`MagnifierContent.kt:72, 85`、`DialogContent.kt:106, 122` |
| `RoundedRectangle(24f.dp)` | `AdaptiveLuminanceGlassContent.kt:92` |
| `RoundedRectangle(32f.dp)` | `GlassPlaygroundContent.kt:129`、`ScrollContainerContent.kt:40`、`LazyScrollContainerContent.kt:40`、`MagnifierContent.kt:57`、`ToggleContent.kt:48`、`SliderContent.kt:50` |
| `RoundedRectangle(48f.dp)` | `DialogContent.kt:60` |
| `RoundedRectangle(itemSize / 2f)`（= 34dp） | `ControlCenterContent.kt:70` |
| `RoundedRectangle(256f.dp / 2f * cornerRadiusFrac)`（`cornerRadiusFrac ∈ [0,1]`） | `GlassPlaygroundContent.kt:67` |
| `Rectangle` | `LockScreenContent.kt:61` |

### 1.8.3 变换（`layerBlock` / `Backdrop` 变换）

| 项 | 结论 | 出处 |
| --- | --- | --- |
| `layerBlock: (GraphicsLayerScope.() -> Unit)?` | 直接透传给 `Modifier.graphicsLayer(layerBlock)`，可设 `scaleX/scaleY/translationX/translationY/rotationZ/rotationX/rotationY/transformOrigin/alpha/colorFilter/blendMode` 等 | `DrawBackdropModifier.kt:59-64, 87, 96-102` `[源码]` |
| `Backdrop` 侧的逆变换 | `LayerBackdrop.drawBackdrop` 用 `InverseLayerScope.inverseTransform(density, layerBlock)` 对 `scaleX/scaleY/rotationZ` 做**逆变换**（`scale(1/scaleX, 1/scaleY, Offset.Zero)`；有旋转时用 2×2 矩阵求逆），使贴图内容与图层对齐 | `backdrops/LayerBackdrop.kt:53-67`；`internal/InverseLayerScope.kt:96-129` `[源码]` |
| 实际用到的变换参数 | 见 §2 各组件 |

---

## 1.9 RuntimeShader 的 uniform 清单（★ 移植到 GLES 的关键）

### 1.9.1 `RoundedRectRefractionShaderString`（key `"Refraction"`）

源码：`backdrop/src/commonMain/kotlin/com/kyant/backdrop/internal/Shaders.kt:51-84`

| # | uniform 名 | 类型 | 声明行 | 赋值处 | 语义 |
| --- | --- | --- | --- | --- | --- |
| 1 | `content` | `shader` | `Shaders.kt:52` | `RuntimeShaderEffect(shader, "content")`（`Lens.kt:55`） | 输入图层（被折射的 backdrop） |
| 2 | `size` | `float2` | `Shaders.kt:54` | `Lens.kt:45` `size.width, size.height` | 面板像素尺寸（含 padding 后的记录尺寸） |
| 3 | `offset` | `float2` | `Shaders.kt:55` | `Lens.kt:46` `-padding, -padding` | padding 造成的坐标系补偿 |
| 4 | `cornerRadii` | `float4` | `Shaders.kt:56` | `Lens.kt:47`（`[tl,tr,br,bl]` 像素值） | 四角半径，供 `radiusAt()` 选角 |
| 5 | `refractionHeight` | `float` | `Shaders.kt:57` | `Lens.kt:48` | 折射影响带宽度 |
| 6 | `refractionAmount` | `float` | `Shaders.kt:58` | `Lens.kt:49`（**取负**） | 折射位移幅度 |
| 7 | `depthEffect` | `float` | `Shaders.kt:59` | `Lens.kt:50`（`0f/1f`） | 景深开关 |

关键公式（**逐字**）：

```glsl
// Shaders.kt:67-84
half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 centeredCoord = (coord + offset) - halfSize;
    float radius = radiusAt(coord, cornerRadii);

    float sd = sdRoundedRect(centeredCoord, halfSize, radius);
    if (-sd >= refractionHeight) {
        return content.eval(coord);
    }
    sd = min(sd, 0.0);

    float d = circleMap(1.0 - -sd / refractionHeight) * refractionAmount;
    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 grad = normalize(gradSdRoundedRect(centeredCoord, halfSize, gradRadius) + depthEffect * normalize(centeredCoord));

    float2 refractedCoord = coord + d * grad;
    return content.eval(refractedCoord);
}
```

### 1.9.2 `RoundedRectRefractionWithDispersionShaderString`（key `"RefractionWithDispersion"`）

源码：`Shaders.kt:87-158`

| # | uniform 名 | 类型 | 声明行 | 赋值处 | 语义 |
| --- | --- | --- | --- | --- | --- |
| 1–7 | 同 §1.9.1 | | `Shaders.kt:89-95` | `Lens.kt:45-50` | 同上 |
| 8 | `chromaticAberration` | `float` | `Shaders.kt:96` | `Lens.kt:52`，**恒 `1f`** | 色散强度（这里只当 0/1 用；类型是 float，**可以直接接滑杆**） |

色散权重表（**编译期常量，★ 改不了**）：

```glsl
// Shaders.kt:119-157（节选，逐字）
float2 refractedCoord = coord + d * grad;
float dispersionIntensity = chromaticAberration * ((centeredCoord.x * centeredCoord.y) / (halfSize.x * halfSize.y));
float2 dispersedCoord = d * grad * dispersionIntensity;

half4 color = half4(0.0);

half4 red = content.eval(refractedCoord + dispersedCoord);
color.r += red.r / 3.5;
color.a += red.a / 7.0;

half4 orange = content.eval(refractedCoord + dispersedCoord * (2.0 / 3.0));
color.r += orange.r / 3.5;
color.g += orange.g / 7.0;
color.a += orange.a / 7.0;

half4 yellow = content.eval(refractedCoord + dispersedCoord * (1.0 / 3.0));
color.r += yellow.r / 3.5;
color.g += yellow.g / 3.5;
color.a += yellow.a / 7.0;

half4 green = content.eval(refractedCoord);
color.g += green.g / 3.5;
color.a += green.a / 7.0;

half4 cyan = content.eval(refractedCoord - dispersedCoord * (1.0 / 3.0));
color.g += cyan.g / 3.5;
color.b += cyan.b / 3.0;
color.a += cyan.a / 7.0;

half4 blue = content.eval(refractedCoord - dispersedCoord * (2.0 / 3.0));
color.b += blue.b / 3.0;
color.a += blue.a / 7.0;

half4 purple = content.eval(refractedCoord - dispersedCoord);
color.r += purple.r / 7.0;
color.b += purple.b / 3.0;
color.a += purple.a / 7.0;

return color;
```

⇒ **7 次纹理采样 / 像素**，采样偏移比例 `{1, 2/3, 1/3, 0, -1/3, -2/3, -1}`，权重 `/3.5`(主导通道)、`/7.0`(次通道/alpha)、`/3.0`(蓝)。
ytbl 把这些提成了 uniform 数组（`uPrismDist[3]` / `uPrismEvenRed[3]` …，`Shaders.cpp:131-138`），默认值来自 `GlassConfig.h:100-108` —— **等价的数值表**。`[源码]`（两侧数值对照见 §4.2）

### 1.9.3 `DefaultHighlightShaderString`（key `"Default"`）

源码：`Shaders.kt:161-181`

| # | uniform 名 | 类型 | 声明行 | 赋值处 | 语义 |
| --- | --- | --- | --- | --- | --- |
| 1 | `size` | `float2` | `Shaders.kt:162` | `HighlightStyle.kt:61` | 尺寸 |
| 2 | `cornerRadii` | `float4` | `Shaders.kt:163` | `HighlightStyle.kt:62`（`getCornerRadii`，`:115-138`） | 四角半径 |
| 3 | `color` | `layout(color) half4` | `Shaders.kt:164` | `HighlightStyle.kt:63`（**alpha 强制 1**） | 高光颜色 |
| 4 | `angle` | `float` | `Shaders.kt:165` | `HighlightStyle.kt:64`（弧度 = 度 × π/180） | 光方向 |
| 5 | `falloff` | `float` | `Shaders.kt:166` | `HighlightStyle.kt:65` | 幂指数 |

```glsl
// Shaders.kt:170-181
half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 centeredCoord = coord - halfSize;
    float radius = radiusAt(coord, cornerRadii);

    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 grad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
    float2 normal = float2(cos(angle), sin(angle));
    float d = dot(grad, normal);
    float intensity = pow(abs(d), falloff);
    return color * intensity;
}
```

### 1.9.4 `AmbientHighlightShaderString`（key `"Ambient"`）

源码：`Shaders.kt:184-204`

| # | uniform 名 | 类型 | 声明行 | 赋值处 | 语义 |
| --- | --- | --- | --- | --- | --- |
| 1 | `size` | `float2` | `Shaders.kt:185` | `HighlightStyle.kt:91` | 尺寸 |
| 2 | `cornerRadii` | `float4` | `Shaders.kt:186` | `HighlightStyle.kt:92` | 四角半径 |
| 3 | `angle` | `float` | `Shaders.kt:187` | `HighlightStyle.kt:93`，**恒 `45°→弧度`** | 光方向（硬编码） |
| 4 | `falloff` | `float` | `Shaders.kt:188` | `HighlightStyle.kt:94`，**恒 `1f`** | 幂指数（硬编码） |

```glsl
// Shaders.kt:192-204
half4 main(float2 coord) {
    float2 halfSize = size * 0.5;
    float2 centeredCoord = coord - halfSize;
    float radius = radiusAt(coord, cornerRadii);

    float gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y));
    float2 grad = gradSdRoundedRect(centeredCoord, halfSize, gradRadius);
    float2 normal = float2(cos(angle), sin(angle));
    float d = dot(grad, normal);
    float intensity = pow(abs(d), falloff);
    float t = step(0.0, d);
    return half4(t, t, t, 1.0) * intensity;
}
```

### 1.9.5 SDF 公共函数（**没有 uniform**，是编译期代码）

`Shaders.kt:22-48`（`RoundedRectSDF`）逐字：

```glsl
float radiusAt(float2 coord, float4 radii) {
    if (coord.x >= 0.0) {
        if (coord.y <= 0.0) return radii.y;
        else return radii.z;
    } else {
        if (coord.y <= 0.0) return radii.x;
        else return radii.w;
    }
}

float sdRoundedRect(float2 coord, float2 halfSize, float radius) {
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius));
    float outside = length(max(cornerCoord, 0.0)) - radius;
    float inside = min(max(cornerCoord.x, cornerCoord.y), 0.0);
    return outside + inside;
}

float2 gradSdRoundedRect(float2 coord, float2 halfSize, float radius) {
    float2 cornerCoord = abs(coord) - (halfSize - float2(radius));
    if (cornerCoord.x >= 0.0 || cornerCoord.y >= 0.0) {
        return sign(coord) * normalize(max(cornerCoord, 0.0));
    } else {
        float gradX = step(cornerCoord.y, cornerCoord.x);
        return sign(coord) * float2(gradX, 1.0 - gradX);
    }
}
```

**结论**：`radii.x/y/z/w` = `topLeft / topRight / bottomRight / bottomLeft`（`[推断]` 自 `y` 轴向下的 AGSL 坐标约定 + `Lens.kt:66-71` 的数组顺序）。

### 1.9.6 应用层额外出现的 uniform（**不在 `backdrop` 库里，但同属"最终像素参数"**）

| Shader | uniform | 类型 | 赋值 | 出处 |
| --- | --- | --- | --- | --- |
| `SdfShaderString`（SDF 贴图折射，锁屏钟） | `content` / `sdfTex` / `size` / `sdfTexSize` / `refractionHeight` / `lightAngle` | `shader, shader, float2, float2, float, float` | `refractionHeight = 48f.dp.toPx()`（默认）、`lightAngle = 45f`（默认）；`apply()` 调用点未传参 | `app/.../utils/SdfShader.kt:36-43`（声明）、`:28-31`（接口默认）、`:65-73`（公式）、`LockScreenContent.kt:65` `with(sdfShader) { apply() }` |
| `AlphaMask`（渐进模糊，内联） | `content` / `size` / `tint`(`layout(color) half4`) / `tintIntensity` | `shader, float2, half4, float` | `size`；`tint = tintColor`；`tintIntensity = 0.8f` | `ProgressiveBlurContent.kt:41-60`（`setFloatUniform("tintIntensity", 0.8f)` 在 `:59`） |
| `InteractiveHighlight`（按压光斑，内联） | `size` / `color`(`layout(color) half4`) / `radius` / `position` | `float2, half4, float, float2` | `radius = size.minDimension * 1.5f`；`color = White.copy(0.15f * progress)`；均匀底 `White.copy(0.08f * progress)` | `app/.../utils/InteractiveHighlight.kt:44-54`（声明）、`:71-78`（赋值）、`:66`（均匀底） |

---

## 1.10 库级"基础设施"参数（会影响像素但容易漏）

| 参数 | 源码位置 | 类型 | 默认 | 作用 | 控件建议 |
| --- | --- | --- | --- | --- | --- |
| `padding` | `BackdropEffectScope.kt:18`；`DrawBackdropModifier.kt:269-283, 290-302` | `Float` | `0f`（每次 `apply` 重置，`:60`） | 离屏图层向外扩的像素数；同时作为 `offset` uniform 传给折射 shader | 只读 |
| `size` / `layoutDirection` / `density` | `BackdropEffectScope.kt:12, 14`；`:38-57` | —— | 由 `DrawScope` 更新 | 在任何 `lens()` 里都能读到 | —— |
| `shape` | `BackdropEffectScope.kt:16`；`DrawBackdropModifier.kt:249` | `Shape` | 由 `shapeProvider.innerShape` 提供 | 决定 `cornerRadii` | 见 §1.8 |
| `renderEffect` | `BackdropEffectScope.kt:20`；`DrawBackdropModifier.kt:366` | `RenderEffect?` | `null` | 效果链的容器 | —— |
| 离屏合成策略 | `DrawBackdropModifier.kt:257` | `CompositingStrategy.Offscreen` | 硬编码 | 保证 RenderEffect 正确 | —— |
| `exportedBackdrop` | `DrawBackdropModifier.kt:88, 327-334` | `LayerBackdrop?` | `null` | 把「backdrop + surface + front」录成一个可复用的 `LayerBackdrop`（Sheet 嵌套用） | —— |
| `onDrawBehind` / `onDrawBackdrop` / `onDrawSurface` / `onDrawFront` | `DrawBackdropModifier.kt:89-92, 316-335` | 回调 | `null` / 恒等 | 绘制时序钩子：`onDrawBehind` → **backdrop** → `onDrawSurface` → **content** → `onDrawFront` | —— |
| `Backdrop.isCoordinatesDependent` | `Backdrop.kt:10`；`LayerBackdrop.kt:40`（true）/`CanvasBackdrop.kt:26`（false）/`EmptyBackdrop.kt:17`（false）/`CombinedBackdrop.kt:46-47, 66-69, 87-88`（或） | `Boolean` | —— | true 时每帧上报 `layoutCoordinates` 用于对齐 | —— |

---

# 第二部分：成品组件暴露的参数

> 这一节只列**函数签名里的参数** + **签名外、写死在函数体里的玻璃参数**。
> 后者才是「每种效果的样子」，也是控件候选。

## 2.1 `LiquidButton`（`app/.../components/LiquidButton.kt`，共 122 行）

### 2.1.1 函数签名参数（`LiquidButton.kt:35-44`）

```kotlin
@Composable
fun LiquidButton(
    onClick: () -> Unit,
    backdrop: Backdrop,
    modifier: Modifier = Modifier,
    isInteractive: Boolean = true,
    tint: Color = Color.Unspecified,
    surfaceColor: Color = Color.Unspecified,
    content: @Composable RowScope.() -> Unit
)
```

| # | 参数 | 类型 | 默认值 | 含义 | 出处 | 控件候选 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `onClick` | `() -> Unit` | 无 | 点击回调 | `:37` | — |
| 2 | `backdrop` | `Backdrop` | 无 | 背景源 | `:38` | — |
| 3 | `modifier` | `Modifier` | `Modifier` | 外部修饰 | `:39` | — |
| 4 | `isInteractive` | `Boolean` | `true` | 关掉后不挂 `InteractiveHighlight`，且改用 `LocalIndication` 涟漪 | `:40`, `:101-115` | `checkbox 可交互` |
| 5 | `tint` | `Color` | `Color.Unspecified` | 指定后走 `BlendMode.Hue` + `alpha 0.75` 叠色 | `:41`, `:91-95` | `color picker` + `slider tintAlpha (硬编码 0.75 → 需改代码)` |
| 6 | `surfaceColor` | `Color` | `Color.Unspecified` | 表面实色（`drawRect(surfaceColor)`） | `:42`, `:96-98` | `color picker` |
| 7 | `content` | `@Composable RowScope.() -> Unit` | 无 | 内容 | `:43` | — |

### 2.1.2 签名外的玻璃参数（`[源码]`）

| 项 | 值 | 行号 | 控件候选 |
| --- | --- | --- | --- |
| 形状 | `Capsule()` | `:57` | combo 形状 |
| 色彩 | `vibrancy()` | `:59` | `checkbox 高明度` |
| 模糊 | `blur(2f.dp.toPx())` | `:60` | `slider 0..10 dp` |
| 透镜 | `lens(12f.dp.toPx(), 24f.dp.toPx())` | `:61` | `slider 高 0..40 / 强 0..60` |
| 高光 / 阴影 / 内阴影 | **未传** → 走 `drawBackdrop` 默认 `Highlight.Default` + `Shadow.Default`，`innerShadow = null` | `DrawBackdropModifier.kt:84-86` | `checkbox 高光/阴影` |
| 按压高度膨胀 | `scale = lerp(1f, 1f + 4f.dp.toPx() / size.height, pressProgress)` | `:69` | `slider 0..8 dp` |
| 位移饱和 | `translation = maxOffset * tanh(0.05f * offset / maxOffset)`，`maxOffset = size.minDimension` | `:71-75` | `slider 0..0.2` |
| 拉伸上限 | `maxDragScale = 4f.dp.toPx() / size.height` | `:77` | `slider 0..8 dp` |
| 纵横比修正 | X 乘 `(width/height).coerceAtMost(1f)`；Y 乘 `(height/width).coerceAtMost(1f)` | `:82`, `:86` | `checkbox` |
| 高度 | `48f.dp` | `:116` | `slider 32..64 dp` |
| 水平内边距 | `16f.dp` | `:117` | `slider 0..32 dp` |
| 内容间距 | `Arrangement.spacedBy(8f.dp)` | `:118` | `slider 0..24 dp` |
| tint 叠色 alpha | `0.75f` | `:94` | 需改代码 |

## 2.2 `LiquidToggle`（`app/.../components/LiquidToggle.kt`，共 203 行）

### 2.2.1 函数签名参数（`LiquidToggle.kt:47-53`）

```kotlin
@Composable
fun LiquidToggle(
    selected: () -> Boolean,
    onSelect: (Boolean) -> Unit,
    backdrop: Backdrop,
    modifier: Modifier = Modifier
)
```

| # | 参数 | 类型 | 默认 | 含义 | 出处 |
| --- | --- | --- | --- | --- | --- |
| 1 | `selected` | `() -> Boolean` | 无 | 当前值（读 lambda，便于快照订阅） | `:49` |
| 2 | `onSelect` | `(Boolean) -> Unit` | 无 | 值变化回调 | `:50` |
| 3 | `backdrop` | `Backdrop` | 无 | 背景源 | `:51` |
| 4 | `modifier` | `Modifier` | `Modifier` | 外部修饰 | `:52` |

> ★ **`LiquidToggle` 没有任何视觉参数**（没有 tint/size/blur 入参）。全部写死在函数体内。

### 2.2.2 签名外的玻璃参数（`[源码]`）

| 项 | 值 | 行号 | 控件候选 |
| --- | --- | --- | --- |
| 强调色（亮/暗） | `Color(0xFF34C759)` / `Color(0xFF30D158)` | `:56-57` | `color picker` |
| 轨道底色（亮/暗） | `Color(0xFF787878).copy(0.2f)` / `Color(0xFF787880).copy(0.36f)` | `:59-60` | `color picker` |
| 拖动行程 | `dragWidth = 20f.dp.toPx()` | `:64` | `slider 0..32 dp` |
| `DampedDragAnimation` | `valueRange=0..1`，`visibilityThreshold=0.001f`，`initialScale=1f`，`pressedScale=1.5f` | `:72-75` | `slider pressedScale 1..2` |
| 轨道尺寸 | `size(64f.dp, 28f.dp)` + `Capsule()` | `:124, 129` | `slider 宽 40..96 / 高 20..40` |
| 滑块内边距 | `padding = 2f.dp.toPx()` | `:136` | `slider 0..8 dp` |
| 滑块位移 | `translationX = lerp(2dp, 2dp + 20dp, fraction)` | `:137-139` | `slider 行程 0..32 dp` |
| 滑块尺寸 | `size(40f.dp, 24f.dp)` | `:200` | `slider` |
| 滑块 backdrop 挤压 | `scaleX = lerp(2f/3f, 0.75f, pp)`；`scaleY = lerp(0f, 0.75f, pp)` | `:150-151` | 两个 `slider 0..1` |
| 按压模糊 | `blur(8f.dp.toPx() * (1f - pp))` | `:160` | `slider 0..10 dp`（**范围上限**） |
| 按压透镜 | `lens(5f.dp.toPx() * pp, 10f.dp.toPx() * pp, chromaticAberration = true)` | `:161-165` | `slider` ×2 + `checkbox 色散` |
| 高光 | `Highlight.Ambient.copy(width = /1.5f, blurRadius = /1.5f, alpha = pp)` → 实际 `width=0.3333dp, blurRadius=0.1667dp` | `:167-174` | `slider width 0..1 dp` / `slider softness` |
| 阴影 | `Shadow(radius = 4f.dp, color = Black.copy(0.05f))`，其余默认（offset = `(0, 4/6 dp)`、alpha=1） | `:175-180` | `slider 0..16 dp` + `color picker` |
| 内阴影 | `InnerShadow(radius = 4f.dp * pp, alpha = pp)`，offset 默认 = `(0, radius)` | `:181-187` | `slider 0..16 dp` |
| 速度拉伸 | `v = velocity / 50f`；`scaleX /= 1 - clamp(v*0.75, ±0.2)`；`scaleY *= 1 - clamp(v*0.25, ±0.2)` | `:191-193` | `slider 0..1` ×2 + `slider 除数 10..100` |
| 表面白蒙版 | `drawRect(White.copy(alpha = 1f - pp))` | `:195-198` | `checkbox` |

## 2.3 `LiquidSlider`（`app/.../components/LiquidSlider.kt`，共 212 行）

### 2.3.1 函数签名参数（`LiquidSlider.kt:48-56`）

```kotlin
@Composable
fun LiquidSlider(
    value: () -> Float,
    onValueChange: (Float) -> Unit,
    valueRange: ClosedFloatingPointRange<Float>,
    visibilityThreshold: Float,
    backdrop: Backdrop,
    modifier: Modifier = Modifier
)
```

| # | 参数 | 类型 | 默认 | 含义 | 出处 | 控件候选 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `value` | `() -> Float` | 无 | 当前值 | `:50` | — |
| 2 | `onValueChange` | `(Float) -> Unit` | 无 | 变更回调 | `:51` | — |
| 3 | `valueRange` | `ClosedFloatingPointRange<Float>` | 无 | 值域（组件用它做拖动换算与点击定位） | `:52`, `:94`, `:118` | `slider min/max` |
| 4 | `visibilityThreshold` | `Float` | 无 | 传给 `DampedDragAnimation`，动画可见阈值 | `:53`, `:81` | `slider 1e-4..0.01` |
| 5 | `backdrop` | `Backdrop` | 无 | 背景源 | `:54` | — |
| 6 | `modifier` | `Modifier` | `Modifier` | 外部修饰 | `:55` | — |

### 2.3.2 签名外的玻璃参数（`[源码]`）

| 项 | 值 | 行号 |
| --- | --- | --- |
| 强调色（亮/暗） | `Color(0xFF0088FF)` / `Color(0xFF0091FF)` | `:59-60` |
| 轨道底色 | `Color(0xFF787878).copy(0.2f)` / `Color(0xFF787880).copy(0.36f)` | `:62-63` |
| 轨道高度 | `height(6f.dp)`（底 + 已填充两段） | `:127`, `:135` |
| 已填充宽度 | `constraints.maxWidth * progress` | `:138` |
| `DampedDragAnimation` | `initialScale=1f`，`pressedScale=1.5f` | `:82-83` |
| 滑块位移 | `(-size.width/2 + trackWidth*progress)` clamp 到 `(-size.width/4, trackWidth - size.width*3/4)` | `:150-151` |
| 滑块尺寸 | `size(40f.dp, 24f.dp)` | `:209` |
| 滑块 backdrop 挤压 | `scaleX = lerp(2f/3f, 1f, pp)`；`scaleY = lerp(0f, 1f, pp)` | `:159-160` |
| 按压模糊 | `blur(8f.dp.toPx() * (1f - pp))` | `:169` |
| 按压透镜 | `lens(10f.dp.toPx() * pp, 14f.dp.toPx() * pp, chromaticAberration = true)` | `:170-174` |
| 高光 | `Highlight.Ambient.copy(width/1.5f, blurRadius/1.5f, alpha = pp)` | `:176-183` |
| 阴影 | `Shadow(radius = 4f.dp, color = Black.copy(0.05f))` | `:184-189` |
| 内阴影 | `InnerShadow(radius = 4f.dp * pp, alpha = pp)` | `:190-196` |
| 速度拉伸 | `v = velocity / 10f`，其余同 Toggle | `:200-202` |
| 表面蒙版 | `drawRect(White.copy(alpha = 1f - pp))` | `:204-207` |

## 2.4 `LiquidBottomTabs` + `LiquidBottomTab`

### 2.4.1 `LiquidBottomTabs` 签名（`LiquidBottomTabs.kt:59-67`）

```kotlin
@Composable
fun LiquidBottomTabs(
    selectedTabIndex: () -> Int,
    onTabSelected: (index: Int) -> Unit,
    backdrop: Backdrop,
    tabsCount: Int,
    modifier: Modifier = Modifier,
    content: @Composable RowScope.() -> Unit
)
```

| # | 参数 | 类型 | 默认 | 含义 | 出处 | 控件候选 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `selectedTabIndex` | `() -> Int` | 无 | 当前 tab | `:61` | — |
| 2 | `onTabSelected` | `(index: Int) -> Unit` | 无 | 切换回调 | `:62` | — |
| 3 | `backdrop` | `Backdrop` | 无 | 背景源 | `:63` | — |
| 4 | `tabsCount` | `Int` | 无 | 决定 tab 宽度 `(maxWidth - 8dp)/tabsCount` | `:64`, `:84` | `slider 2..6` |
| 5 | `modifier` | `Modifier` | `Modifier` | 外部修饰 | `:65` | — |
| 6 | `content` | `@Composable RowScope.() -> Unit` | 无 | tab 内容 | `:66` | — |

### 2.4.2 `LiquidBottomTab` 签名（`LiquidBottomTab.kt:22-27`）

```kotlin
@Composable
fun RowScope.LiquidBottomTab(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
)
```

唯一的视觉相关机制是 `LocalLiquidBottomTabScale`（`LiquidBottomTab.kt:19-20`，`staticCompositionLocalOf { { 1f } }`），由 `LiquidBottomTabs` 提供：`lerp(1f, 1.2f, pressProgress)`（`LiquidBottomTabs.kt:191-193`）。

### 2.4.3 `LiquidBottomTabs` 签名外的玻璃参数（`[源码]`）

| 项 | 值 | 行号 |
| --- | --- | --- |
| 强调色 / 容器色 | `#0088FF`/`#0091FF`；`#FAFAFA@0.4`/`#121212@0.4` | `:69-74` |
| tab 宽度 | `(maxWidth - 8dp) / tabsCount` | `:83-85` |
| 面板回弹偏移 | `4f.dp.toPx() * fraction.sign * EaseOut.transform(abs(fraction))`，`f = clamp(offsetAnim/maxWidth, -1, 1)` | `:88-95` |
| `DampedDragAnimation` | `visibilityThreshold=0.001f`，`pressedScale = 78f/56f ≈ 1.3929` | `:106-109` |
| 回零弹簧 | `spring(1f, 300f, 0.5f)` | `:116-119` |
| **外层容器**玻璃 | 形状 `Capsule()`；`vibrancy(); blur(8dp); lens(24dp, 24dp)`；`drawRect(containerColor)` | `:168-180` |
| 外层高度/内边距 | `height(64f.dp)`、`padding(4f.dp)` | `:183, 185` |
| 面板按压膨胀 | `scale = lerp(1f, 1f + 16f.dp.toPx()/size.width, pp)` | `:176-178` |
| **内层（着色 tab 层）** | `vibrancy(); blur(8dp); lens(24dp*pp, 24dp*pp)`；`Highlight.Default.copy(alpha = pp)`；`drawRect(containerColor)`；`ColorFilter.tint(accentColor)`；高度 `56f.dp` | `:206-225` |
| **指示器** | 形状 `Capsule()`；`lens(10dp*pp, 14dp*pp, chromaticAberration=true)`；`Highlight.Default.copy(alpha=pp)`；`Shadow(alpha=pp)`（其余默认 → radius 24dp / Black@0.1）；`InnerShadow(radius=8dp*pp, alpha=pp)`；表面两段 `Black|White@0.1, alpha=1-pp` + `Black@0.03*pp`；高度 `56f.dp`；`fillMaxWidth(1f/tabsCount)`；`padding(horizontal = 4f.dp)` | `:231-286` |
| 指示器位移 | `translationX = value * tabWidth + panelOffset`（LTR） | `:235-238` |
| tab 图标缩放 | `lerp(1f, 1.2f, pressProgress)` | `:192` |

## 2.5 其余成品组件的参数（destinations 层，供"每组件一份"参考）

| 组件 / 页面 | 全部玻璃参数 | 出处 |
| --- | --- | --- |
| **Dialog** | `shape=RoundedRectangle(48dp)`；`colorControls(brightness=亮0.2/暗0, saturation=1.5)`；`blur(亮16dp/暗8dp)`；`lens(24dp, 48dp, depthEffect=true)`；`highlight=Highlight.Plain`；`onDrawSurface=drawRect(containerColor)`（亮 `#FAFAFA@0.6`/暗 `#121212@0.4`）；遮罩亮 `#29293A@0.23`/暗 `#121212@0.56`；外边距 `40dp` | `DialogContent.kt:42-70` |
| **GlassPlayground** | 5 个可调量：`cornerRadiusFrac 0.5`（滑杆 `0..1`）、`blurRadiusDp 0f`（`0..32`）、`refractionHeightFrac 0.2`（`0..1`）、`refractionAmountFrac 0.2`（`0..1`）、`chromaticAberration 0f`（`0..1`）；`lens(height = frac*minDim*0.5, amount = frac*minDim, depthEffect=true, chromaticAberration = v>0)`；`highlight=Highlight.Plain`；Sheet：`RoundedRectangle(32dp)` + `vibrancy(); blur(4dp); lens(16dp,32dp)` + `Highlight.Plain` + `White@0.5` surface | `GlassPlaygroundContent.kt:52-58, 65-90, 127-138, 146-193` |
| **ControlCenter** | 每块玻璃共用：`RoundedRectangle(itemSize/2f=34dp)`；`Highlight(style = HighlightStyle.Default(angle = uiSensor.gravityAngle, falloff = 2f))`；`vibrancy(); lens(24dp*progress, 48dp*progress, depthEffect=true)`；`shadow = null`；`drawRect(Color.Black.copy(0.05f))`；`translationY = -48dp*(1-progress)`、`alpha = EaseIn.transform(safeProgress)`、过拖 `scaleX/Y /= 1 + 0.1*max(progress-1,0)`；整屏 `BlurEffect(4dp*progress, 4dp*progress)`；遮罩 `Black@0.4 * progress`；`itemSize=68dp`、`itemSpacing=16dp`、`innerItemSize=56dp`、`innerItemIconScale=0.8f`、`maxDragHeight=1000f` | `ControlCenterContent.kt:64-124, 126-201` |
| **Magnifier** | 镜片 `Capsule()` + `lens(8dp, 24dp, depthEffect=true, chromaticAberration=true)` + `InnerShadow(radius=16dp)`；`onDrawBackdrop` 里 `withTransform { scale(1.5f,1.5f); translate(top=-80dp) }`；尺寸 `128×96 dp`；光标 `4×24 dp` 胶囊；`backdrop = rememberCombinedBackdrop(backdrop, contentBackdrop, cursorBackdrop)` | `MagnifierContent.kt:83-105` |
| **ProgressiveBlur** | `drawPlainBackdrop(shape=RectangleShape)` + `blur(4dp)` + `runtimeShaderEffect("AlphaMask", …, "content")`：`tintIntensity = 0.8f`，`tint = 亮 White / 暗 #808080`；高度 `128dp` | `ProgressiveBlurContent.kt:36-63` |
| **AdaptiveLuminance** | `RoundedRectangle(24dp)`；`colorControls(brightness=…, contrast=…, saturation=1.5)`；`blur(l>0 ? lerp(8,16,l) : lerp(8,2,-l))`；`lens(24dp, minDimension/2, depthEffect=true)`；`highlight=Highlight.Plain`；亮度采样 5×5，`tween(1000)`；尺寸 `160dp` | `AdaptiveLuminanceGlassContent.kt:88-151` |
| **LockScreen** | `drawPlainBackdrop(shape=Rectangle)` + `colorControls(brightness=-0.1, contrast=0.75, saturation=1.5)` + `blur(2dp)` + `with(sdfShader){ apply() }`；`onDrawBackdrop` 叠 `White@0.25`；背景 `Black@0.3`；左右 `48dp`，最大宽 `400dp` | `LockScreenContent.kt:41-74` |
| **ScrollContainer / LazyScrollContainer** | `RoundedRectangle(32dp)` + `vibrancy(); lens(16dp, 32dp)`；高 `160dp`；间距 `16dp`；20 / 100 项 | `ScrollContainerContent.kt:38-47`；`LazyScrollContainerContent.kt:38-47` |
| **Toggle / Slider 页** | 两个示例：一个直接吃页面 backdrop；一个吃 `rememberCanvasBackdrop { drawRect(backgroundColor) }`（纯色背景），包在 `RoundedRectangle(32dp)` + `background` 的卡片里 | `ToggleContent.kt:38-57`；`SliderContent.kt:38-61` |
| **Buttons 页** | 4 个 `LiquidButton`：默认 / `surfaceColor = White@0.3` / `tint = #0088FF` / `tint = #FF8D28` | `ButtonsContent.kt:22-60` |
| **BottomTabs 页** | 2 组：`tabsCount = 3` 与 `tabsCount = 4`，均 `padding(horizontal = 36.dp)` | `BottomTabsContent.kt:42-88` |

### 2.5.1 交互工具类的可调参数（**动效侧的"效果"**）

| 类 | 参数 | 默认 | 出处 |
| --- | --- | --- | --- |
| `DampedDragAnimation` | `initialValue` / `valueRange` / `visibilityThreshold` / `initialScale` / `pressedScale` / `onDragStarted` / `onDragStopped` / `onDrag` | —— | `utils/DampedDragAnimation.kt:19-29` |
| ↳ 内部弹簧 | `valueAnimationSpec = spring(1f, 1000f, visibilityThreshold)`；`velocityAnimationSpec = spring(0.5f, 300f, vt*10f)`；`pressProgressAnimationSpec = spring(1f, 1000f, 0.001f)`；`scaleXAnimationSpec = spring(0.6f, 250f, 0.001f)`；`scaleYAnimationSpec = spring(0.7f, 250f, 0.001f)` | | `:31-40` |
| ↳ 其它 | 松手前阈值 `(range.span) * 0.025f`；速度归一化 `calculateVelocity().x / range.span` | | `:97`, `:134` |
| `InteractiveHighlight` | `position: (size: Size, offset: Offset) -> Offset`（默认 `{ _, offset -> offset }`）；`pressProgressAnimationSpec = spring(0.5f, 300f, 0.001f)`；`positionAnimationSpec = spring(0.5f, 300f, Offset.VisibilityThreshold)`；均匀底 `White@0.08*progress`；光斑色 `White@0.15*progress`；光斑半径 `size.minDimension * 1.5f`；衰减 `smoothstep(radius, radius*0.5, dist)` | | `utils/InteractiveHighlight.kt:22-30, 60-93` |
| `ProgressConverter.Default` | `(1f - exp(-abs(p))) * p.sign` | | `utils/ProgressConverter.kt:13-16` |
| `SdfShader.apply` | `refractionHeight = 48f.dp.toPx()`、`lightAngle = 45f` | | `utils/SdfShader.kt:28-31` |

---

# 第三部分：★ 调试面板的控件清单

## 3.0 现有面板的分组风格（先对齐）

`secondary_imgui.cpp` 现有的 3 个分组由同一个 `DrawSectionHeader` 渲染：

```cpp
// secondary_imgui.cpp:270-311（节选）
static bool DrawSectionHeader(const char* id, const char* title, bool* expanded);
// :891  DrawSectionHeader("window",     Text("二级窗口玻璃参数", "Secondary window"),          &s_WindowControlsExpanded)
// :914  DrawSectionHeader("card",       Text("液态玻璃卡片参数", "Liquid glass card"),        &s_CardControlsExpanded)
// :927  DrawSectionHeader("playground", Text("液态玻璃实验场参数", "Liquid glass playground"), &s_PlaygroundControlsExpanded)
```

每组是「一个可折叠标题 + 一串 `ImGui::SliderFloat/Checkbox`」，
参数存 **文件级 `static` 变量**（`secondary_imgui.cpp:45-77`），
组内控件用**中文主标签 + 英文副标签**（`Text(chinese, english)`，`:85`）。

现有三组 = 「三个被调试的**对象**」：

| 组 | id | 变量 | 控件（行号） |
| --- | --- | --- | --- |
| 二级窗口玻璃参数 | `window` | `s_Window*`（`:45-53`） | 整窗开关 / 模糊 0..10 / 折射高 0..200 / 折射强 0..200 / 色散 0..2 / 透明度开关 + 0.05..0.85 / 景深 / 启用色散（`:893-911`） |
| 液态玻璃卡片参数 | `card` | `s_BlurDp, s_LensHeightDp, s_LensAmountDp, s_CardDispersion, s_Chromatic`（`:55-59`） | 模糊 0..10 / 折射高 0..48 / 折射强 0..72 / 色散 0..2 / 启用色散（`:916-924`） |
| 液态玻璃实验场参数 | `playground` | `s_Playground*`（`:61-73`） | 圆角比例 0.1..1 / 模糊 0..10 / 折射高比例 0..1 / 折射强比例 0..1 / 色散 0..2 / 对象尺寸 96..256 / 显示内置面板 / 重置按钮（`:929-945`） |

### ★ 新面板应该怎么扩（结论）

**不要**把每组展开成"每组件一份"（会立刻膨胀成 6 组 × 10 控件且互相污染），
也不要把所有参数塞进一个巨型组。**推荐的做法是三层**：

1. **保留现有 3 组不动**（它们锚定的是"宿主对象"，变量名/范围已被用户熟悉）。
2. **把每一组内部展开成"参数域子组"**——即在 `DrawSectionHeader` 展开后，
   再插一排 `ImGui::SeparatorText()` / 二级 `CollapsingHeader`：
   `形状 → 光学(Blur/Lens) → 边光(Highlight) → 阴影(Shadow/InnerShadow) → 色彩(ColorFilter/Surface) → 运动(layerBlock + 弹簧)`。
   **顺序固定、组内控件顺序在所有组里保持一致**，这样"三个对象"可以逐列对照。
3. **新增一个横向组 `lytbl-gap`「backdrop 全参数域 / ytbl 缺口」**，
   把「Kyant 有、ytbl 没有」的参数集中列出（灰显 + 难度标签），
   作为**路线图面板**，而不是可调面板。

### 3.1 实现建议（避免 6 组 × N 控件互相污染）

```cpp
// [推断] 建议结构：一份参数结构体 + 每个被调试对象一份实例
struct GlassDebugParams {
  // 形状
  float cornerDp = 24.0f;  float corner4[4] = {24,24,24,24};
  // Blur
  float blurDp = 10.0f;    int   tileMode = 0;      // 0=Clamp 1=Decal 2=Repeat 3=Mirror
  // Lens
  float lensHeightDp = 18.0f; float lensAmountDp = 30.0f;
  float lensDepth = 1.0f;     float spectral = 0.70f;   // 0..2
  // Edge（高光）
  int   edgeMode = 1;      // 0=off 1=Gradient 2=Ambient 3=Plain 4=Contour
  float edgeWidthDp = 0.5f, edgeSoftDp = 0.25f, edgeAlpha = 1.0f;
  float edgeAngle = 45.0f,  edgeFalloff = 1.0f;
  unsigned edgeTint = 0x80FFFFFFu;
  // Shadow
  bool  shadowOn = false;  float shadowDp = 24.0f, shadowX = 0.0f, shadowY = 4.0f;
  float shadowAlpha = 1.0f; unsigned shadowTint = 0x1A000000u;
  bool  insetOn = false;   float insetDp = 24.0f, insetX = 0.0f, insetY = 24.0f;
  float insetAlpha = 1.0f; unsigned insetTint = 0x26000000u;
  // 色彩 / 表面
  bool  vibrant = true;    float brightness = 0.0f, contrast = 1.0f, saturation = 1.0f;
  bool  opacityOn = false; float opacity = 1.0f;
  int   surfaceMode = 0;   float surface[4] = {1,1,1,0};
  // backdrop 变换
  float backdropScaleX = 1.0f, backdropScaleY = 1.0f, backdropBlend = 0.0f;
  // 形状变换（layerBlock）
  float translateX = 0, translateY = 0, scaleX = 1, scaleY = 1, rotationZ = 0, alpha = 1;
};

static GlassDebugParams s_DbgWindow, s_DbgCard, s_DbgPlayground, s_DbgButton, s_DbgToggle, s_DbgSlider, s_DbgTabs;
static GlassDebugParams* s_DbgActive = &s_DbgCard;   // 组标题旁放一个 "当前编辑对象" 下拉

static void DrawParamDomain(const char* id, GlassDebugParams& p); // 渲染 §3.2 的全部子组
```

- 每个对象一组 `GlassDebugParams`（**不共享**），编辑目标由顶部一个 `combo` 选择；
- 组内渲染调用**同一个** `DrawParamDomain()`，保证"每组件一份"但不重复代码；
- 每帧把 `GlassDebugParams` 翻译成 `lgx::PanelStyle`（映射见 §4.1）。
- 现有的 `DrawSectionHeader(id, title, expanded)`（`:270`）可直接复用做一级标题，
  二级子组建议用 `ImGui::SeparatorText()` `[推断]`（ImGui 内置，风格与现有扁平控件一致）。

---

## 3.2 扁平控件清单（可直接照抄成代码）

**列定义**：`控件标签 | ImGui 控件 | 范围 | 默认（来源） | 落点（uniform / ytbl 字段） | 实时性`

**实时性标记**：

- `✅实时` = 每帧写 uniform / 重建 `RenderEffect`，**不重编译**（对应 Kyant 的 `setFloatUniform`）；
- `⚠️换程序` = 会切换 shader/effect 变体，**首次**触发一次编译，之后走缓存（Kyant 的 `obtainRuntimeShader(key, string)`）；
- `🛠改代码` = 数值硬编码在 Kotlin/C++ 里，做成控件需要改源码或新增 uniform；
- `❌编译期` = 写死在 shader 字符串/编译期常量里，改它必须改 shader 源码并重编译。

---

### [分组 1] Shape（形状）

```
[分组] Shape（形状）
  圆角半径 dp            slider   0..64     默认 24      → Corners(tl,tr,br,bl) 同值         ✅实时
  左上半径 dp            slider   0..64     默认 24      → Corners.topLeft                 ✅实时
  右上半径 dp            slider   0..64     默认 24      → Corners.topRight                ✅实时
  右下半径 dp            slider   0..64     默认 24      → Corners.bottomRight             ✅实时
  左下半径 dp            slider   0..64     默认 24      → Corners.bottomLeft              ✅实时
  胶囊(capsule)          checkbox            默认 false   → Corners::capsule() (FLT_MAX)    ✅实时
  连续曲率(squircle)     slider   0..1      默认 1.0     → ★ ytbl 缺口，见 §4.3-G1         ❌编译期(SDF 换实现)
  形状类型               combo    Capsule/RoundedRectangle/Rectangle  → 只切换半径预设   ✅实时
```

- 圆角上限：`clampRange(corners.*, 0, shorter*0.5)`（`GlassPanel.cpp:98-101`）`[源码]`；Kyant 侧 `coerceAtMost(size.minDimension/2f)`（`Lens.kt:76`）`[源码]`。
- `Capsule` 在 ytbl 里是 `CAPSULE_RADIUS = 3.4028235e+38f`（`GlassConfig.h:43`）—— ⚠️ 传给 GLES uniform 时是 `FLT_MAX`，若某驱动不吃，需要改成 `shorter*0.5`（`GlassPanel.cpp:97` 已经 clamp 过，安全）。

### [分组 2] Blur（模糊）

```
[分组] Blur（模糊）
  模糊半径 dp            slider   0..16     默认 10      → uGathered / blurRadius          ✅实时
  边缘处理 TileMode      combo    Clamp/Decal/Repeat/Mirror  默认 Clamp  → ★ ytbl 缺口 §4.3-G2  ❌需要改采样代码
  模糊开关               checkbox            默认 true    → 半径=0 即关闭                    ✅实时
  降采样倍率             slider   1..4      默认 1        → ★ ytbl 缺口 §4.3-G3（编译期常量） ❌编译期 BLUR_DECIMATE_LIMIT
  采样数 taps            slider   4..24     默认 12      → ★ 编译期 BLUR_TAPS              ❌编译期(GLES 循环上界)
  步长系数               slider   0.1..0.5  默认 0.25    → ★ 编译期 BLUR_STEP_FROM_SIGMA   ❌编译期
```

★ **范围必须放宽到 16 dp**：`DialogContent.kt:66` 在亮主题下用 `blur(16f.dp.toPx())`，
而 ytbl 的 `BLUR_RADIUS_MAX_DP = 10.0f`（`GlassConfig.h:69`）会把 16dp 截成 10dp。
⇒ 若面板上限保持 10，**面板与 Kyant 成品对不上**。

### [分组 3] Lens（折射 / 色散 / 景深）

```
[分组] Lens（折射）
  启用折射               checkbox            默认 true    → lensOn = uLensHeight>0 && uLensAmount>0  ✅实时
  折射高度 dp            slider   0..40     默认 18      → uLensHeight                     ✅实时
  折射强度 dp            slider   0..60     默认 30      → uLensAmount                     ✅实时
  景深 depthAmount       slider   0..1      默认 1.0     → uLensDepth                      ✅实时
  色散 spectral          slider   0..2      默认 0.70    → uSpectral                       ✅实时
  暗部保底 darkGuard     slider   0..1      默认 1.0     → uDarkGuard (= v * 0.045)        ✅实时
  折射剖面曲线           combo    circleMap / lensCurve  默认 lensCurve → ★ 差异 §4.3-G4  ❌编译期
  法线圆角倍率           slider   1.0..2.0  默认 1.5     → ★ ytbl 缺失 ×1.5，§4.3-G5      ❌编译期
  角半径 ×1.5 开关       checkbox            默认 true    → 同上                           ❌编译期
```

默认值来源对照：

| 控件默认 | 来源 | 标记 |
| --- | --- | --- |
| 折射高度 18 / 强度 30 | `secondary_imgui.cpp:46-47` `s_WindowLensHeightDp = 18.0f; s_WindowLensAmountDp = 30.0f;` | `[源码]` |
| 卡片默认 12 / 24 | `secondary_imgui.cpp:56-57` | `[源码]` |
| 色散 0.70 / 0.75 / 0.90 | `secondary_imgui.cpp:48, 58, 73` | `[源码]` |
| 景深 1.0 | `secondary_imgui.cpp:591` `cardStyle.optics.depthAmount = 1.0f;`；`windowStyle.optics.depthAmount = s_WindowDepth ? 1.0f : 0.0f;`（`:865`） | `[源码]` |
| 组件侧真实值 | Button 12/24（`LiquidButton.kt:61`）；Toggle 5pp/10pp；Slider 10pp/14pp；Tabs 24/24 + 指示器 10pp/14pp；Dialog 24/48；Playground `frac*minDim*0.5` / `frac*minDim` | `[源码]` |

### [分组 4] EdgeLight（高光 / 边光）

```
[分组] EdgeLight（边光）
  模式                   combo    off/Gradient/Ambient/Plain/Contour  默认 Gradient → uEdgeMode(0/1/2/3/4)  ✅实时
  宽度 dp                slider   0..3.0    默认 0.5     → uEdgeWidth (ceil 到整数)         ✅实时
  柔度 dp                slider   0..1.5    默认 0.25    → uEdgeBlur (= softnessDp)        ✅实时
  透明度 alpha           slider   0..1      默认 1.0     → uEdgeAlpha                      ✅实时
  颜色                   color               默认 #80FFFFFF → uEdgeTint + uEdgeTintAlpha   ✅实时
  角度 deg               slider   -180..180 默认 45      → uEdgeAngle (转弧度)             ✅实时
  衰减 falloff           slider   0..8      默认 1.0     → uEdgeFalloff                    ✅实时
  Ambient 强度           slider   0..1      默认 0.38    → 内部换算成 tint alpha            🛠改代码(ytbl 无独立 intensity)
  Ambient 角度/衰减      ——                  恒 45°/1.0   → ★ Kyant 硬编码，不可调          🛠改代码
  Plain 颜色             color               默认 #61FFFFFF（White@0.38）                  ✅实时
  Gradient 颜色          color               默认 #80FFFFFF（White@0.5）                   ✅实时
  Contour 预设           button              一键套用 EDGE_CONTOUR_* 5 个常量               ✅实时
  混合模式               combo    Plus/SrcOver  默认 Plus → ★ ytbl 缺口 §4.3-G6           ❌需要 shader 分支
```

**uEdgeMode 取值映射（[源码]，逐字）**：

```cpp
// GlassPanel.cpp:68-74
float edgeModeValue(const EdgeLight& edge, bool enabled) {
    if (!enabled || edge.widthDp <= 0.0f) return 0.0f;
    if (edge.mode == EdgeMode::Plain) return 3.0f;
    if (edge.mode == EdgeMode::Ambient) return 2.0f;
    if (edge.mode == EdgeMode::Contour) return 4.0f;
    return 1.0f;      // Gradient
}
```

shader 分支：`>3.5` Contour（`Shaders.cpp:402-409`）/ `>2.5` Ambient（`:410-413`）/ `>1.5` Plain（`:414-418`）/ else Gradient（`:419-424`）。
`uEdgeMode<=0.5` 时整段跳过（`Shaders.cpp:390`）。

**★ 已核实的"Kyant↔ytbl 预设对齐"表**：

| Kyant 预设 | 颜色 | width | blurRadius | 角度 | falloff | ytbl 常量 | 是否一致 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `Highlight.Default` | `White@0.5` | `0.5dp` | `0.25dp` | `45°` | `1.0` | `EDGE_TINT=0x80FFFFFF`, `EDGE_ANGLE_DEFAULT=45`, `EDGE_FALLOFF_DEFAULT=1.0` | ✅ 一致 |
| `Highlight.Plain` | `White@0.38` | `0.5dp` | `0.25dp` | — | — | `EDGE_TINT_PLAIN=0x61FFFFFF`, `EDGE_PLAIN_WIDTH=0.5`, `EDGE_PLAIN_SOFTNESS=0.25` | ✅ 一致 |
| `Highlight.Ambient`（库默认） | `White@0.38` | **`0.5dp`** | **`0.25dp`** | 45° | 1.0 | `EDGE_AMBIENT_WIDTH=0.33333334`, `EDGE_AMBIENT_SOFTNESS=0.16666667` | ❌ **差 1.5×** |
| `Highlight.Ambient.copy(width/1.5, blur/1.5)`（组件用法） | `White@pp` | `0.3333dp` | `0.1667dp` | 45° | 1.0 | 同 `EDGE_AMBIENT_*` | ✅ 一致 |
| Kyant **没有** | — | — | — | `-90°` | `1.35` | `EDGE_CONTOUR_*`（`0xCCFFFFFF / 0.6 / 0.30`） | ytbl 独有增强 |

⇒ **结论**：ytbl 的 `EdgeLight::ambient()` 对齐的是**组件里的 Ambient/1.5**，不是库默认的 `Highlight.Ambient`。
做面板时要把这两个区分成两项，或明确采用哪一个。

### [分组 5] Shadow（外阴影）

```
[分组] Shadow（外阴影）
  启用                   checkbox            默认 off(卡片) / on(窗口)  → DropShadow / shadowOn  ✅实时
  半径 dp                slider   0..48     默认 24      → DropShadow.radiusDp             ✅实时
  偏移 X dp              slider   -48..48   默认 0       → DropShadow.offsetXDp            ✅实时
  偏移 Y dp              slider   -48..48   默认 4       → DropShadow.offsetYDp            ✅实时
  颜色                   color               默认 #1A000000 → DropShadow.color              ✅实时
  透明度 alpha           slider   0..1      默认 1.0     → DropShadow.alpha                ✅实时
  混合模式               combo    SrcOver/Plus/...  默认 SrcOver → ★ ytbl 缺口 §4.3-G6     ❌需 shader 分支
  spread                 ——      ★不存在（Kyant/ytbl 都没有）—— 不要加                      ——
```

### [分组 6] InnerShadow（内阴影）

```
[分组] InnerShadow（内阴影）
  启用                   checkbox            默认 off     → InsetShadow / insetOn           ✅实时
  半径 dp                slider   0..32     默认 24      → InsetShadow.radiusDp (uInsetReach) ✅实时
  偏移 X dp              slider   -32..32   默认 0       → uInsetShift.x                   ✅实时
  偏移 Y dp              slider   -32..32   默认 24      → uInsetShift.y                   ✅实时
  颜色                   color               默认 #26000000 → uInsetTint                    ✅实时
  透明度 alpha           slider   0..1      默认 1.0     → uInsetAlpha                     ✅实时
  模糊边缘 TileMode      combo    Decal/Clamp  默认 Decal → ★ Kyant 硬编码 Decal；ytbl 用解析式  🛠改代码
  混合模式               combo                       默认 SrcOver → ★ 缺口 §4.3-G6          ❌需 shader 分支
```

★ **一个必须注意的默认值差异**（`[源码]`）：
- Kyant `InnerShadow.offset` 默认 = `DpOffset(0f.dp, radius)` → **offsetY 等于 radius**（`InnerShadow.kt:19`）；
- ytbl `InsetShadow` 的**字段默认** `radiusDp = 24.0f` 与 `offsetYDp = 24.0f` 一致（`GlassUI.h:282-284`），
  但 `InsetShadow(float radius, float a)` 这个便捷构造把 offsetX/Y 都设成 **0**（`GlassUI.h:289-290`），
  `secondary_imgui.cpp:600` 用的是显式四参构造 `InsetShadow(6.0f, 0.0f, 6.0f, ...)`。
  ⇒ 做控件时若用户把 radius 从 24 拖到 8，**offsetY 不会跟着走**（Kyant 会跟）。这是行为差异，建议在面板上把两者解耦并明确标注。

  ```cpp
  // GlassUI.h:281-293（逐字）
  struct InsetShadow {
      float radiusDp = 24.0f;
      float offsetXDp = 0.0f;
      float offsetYDp = 24.0f;
      unsigned int color = 0x26000000u;
      float alpha = 1.0f;

      InsetShadow() = default;
      InsetShadow(float radius, float a)
          : radiusDp(radius), offsetXDp(0.0f), offsetYDp(0.0f), alpha(a) {}
      InsetShadow(float radius, float offsetX, float offsetY, unsigned int tint, float a)
          : radiusDp(radius), offsetXDp(offsetX), offsetYDp(offsetY), color(tint), alpha(a) {}
  };
  ```

### [分组 7] Optics（色彩）

```
[分组] Optics（色彩）
  高明度 vibrancy        checkbox            默认 true    → optics.colorBoost (uToneBoost)  ✅实时
  亮度 brightness        slider   -1.0..1.0  默认 0.0     → uToneCtrl.x                     ✅实时
  对比度 contrast        slider   0.0..2.0  默认 1.0     → uToneCtrl.y                     ✅实时
  饱和度 saturation      slider   0.0..3.0  默认 1.0     → uToneCtrl.z                     ✅实时
  透明度开关             checkbox            默认 false   → optics.opacityOn                ✅实时
  透明度 opacity         slider   0.05..0.85 默认 0.28    → uPanelOpacity                   ✅实时
  面板总透明度 alpha     slider   0..1      默认 1.0     → PanelStyle.alpha（乘进 panelOpacity） ✅实时
  灰度权重 R/G/B         ——       ★编译期 0.213/0.715/0.072（Kyant 与 ytbl 相同）           ❌编译期
```

**默认值来源**：`secondary_imgui.cpp:867` `windowOpacity = s_WindowOpacityEnabled ? s_WindowOpacity : 0.0f`（`s_WindowOpacity = 0.28f`，`:49`）；`s_WindowOpacityEnabled = false`（`:50`）。`[源码]`
滑块范围 `0.05..0.85` 来自 `:908`。`[源码]`

### [分组 8] Surface（表面色）

```
[分组] Surface（表面色）
  表面色                 color               默认白/黑随主题 → PanelStyle.surface         ✅实时
  表面色 alpha           slider   0..1      默认 0.28 / 0.36 → surface.a                 ✅实时
  表面模式               combo    None/Hue/Overlay  默认 Overlay → uSurfaceMode(0/1/2)    ✅实时
  Hue 模式的桥接系数     ——       ★编译期 0.35 / 1.3（Shaders.cpp:386；GlassConfig.h:180-181 的 HUE_BRIDGE=0.35） ❌编译期
  Hue 模式的混合权重     ——       ★编译期 0.75（对应 Kyant LiquidButton.kt:94 的 alpha=0.75f）  ❌编译期
```

### [分组 9] Backdrop（背景源）

```
[分组] Backdrop（背景源）
  启用 backdrop          checkbox            默认 true    → uSceneOn                        ✅实时
  不透明 backdrop        checkbox            默认 false   → uSolidScene                     ✅实时
  缩放 X                 slider   0..2      默认 1.0     → uStretchScale.x                 ✅实时
  缩放 Y                 slider   0.001..2  默认 1.0     → uStretchScale.y                 ✅实时
  混合量                 slider   0..1      默认 0.0     → uStretchMix                     ✅实时
  缺省色 fallback        color               默认窗口底色 → uAbsentTint                     ✅实时
  多 backdrop 合成       ——      ★ ytbl 缺口 §4.3-G7（Kyant CombinedBackdrop）              ❌需架构改动
  backdrop 旋转/位移     ——      ★ ytbl 缺口 §4.3-G8（Kyant onDrawBackdrop + withTransform）❌需架构改动
```

### [分组 10] LayerBlock（形状/图层变换，对应 Kyant 的 `layerBlock`）

```
[分组] LayerBlock（图层变换）
  平移 X px              slider   -64..64   默认 0       → 由宿主 drawPanel 的 Box 位置模拟  ✅实时（受限，见下）
  平移 Y px              slider   -64..64   默认 0       → 同上                            ✅实时（受限）
  缩放 X                 drag     0.5..2    默认 1.0     → 映射到 backdropScaleX 或 Box 尺寸 ⚠️部分
  缩放 Y                 drag     0.5..2    默认 1.0     → 同上                            ⚠️部分
  rotationZ deg          slider   -180..180 默认 0       → ★ ytbl 缺口 §4.3-G9（Box 轴对齐） ❌需架构改动
  变换原点               combo    Center/TopLeft/...    → ★ 同上                          ❌
  alpha                  slider   0..1      默认 1.0     → PanelStyle.alpha               ✅实时
```

> Kyant 里这些参数的**真实来源**：`LiquidToggle.kt:134-140, 188-194`、`LiquidSlider.kt:148-152, 197-203`、
> `LiquidBottomTabs.kt:163-165, 174-179, 200-202, 234-238, 267-273`、
> `LiquidButton.kt:63-90`、`GlassPlaygroundContent.kt:80-89`、
> `AdaptiveLuminanceGlassContent.kt:111-121`、`ControlCenterContent.kt:107-114`、`MagnifierContent.kt:65-69, 78-82`。
> **推荐做法**：ytbl 侧用「移动 `drawPanel` 的 `Box` + 改 `backdropScaleX/Y`」覆盖平移与缩放两项，
> 旋转/变换原点明确列为缺口，不要做假控件。

### [分组 11] Motion（运动 —— 与 `docs/AMDL_MOTION_CATALOG.md` 的 M-09~M-21 对接）

```
[分组] Motion（运动）
  pressedScale           slider   1.0..2.0  默认 1.5     → DRAG_SCALE (GlassConfig.h:231)   ✅实时
  scaleX 弹簧刚度        slider   100..500  默认 250     → DRAG_SCALE_STIFFNESS (:226)      ✅实时
  scaleX 阻尼            slider   0.1..1.2  默认 0.6     → DRAG_SCALE_X_DAMPING (:227)      ✅实时
  scaleY 阻尼            slider   0.1..1.2  默认 0.7     → DRAG_SCALE_Y_DAMPING (:228)      ✅实时
  速度增益(除以 N)       slider   5..200    默认 10      → TOGGLE/SLIDER/TAB_VELOCITY_GAIN  ✅实时
  拉伸 X 系数            slider   0..1      默认 0.75    → DRAG_STRETCH_X (:233)            ✅实时
  拉伸 Y 系数            slider   0..1      默认 0.25    → DRAG_STRETCH_Y (:234)            ✅实时
  拉伸夹取               slider   0..0.5    默认 0.2     → DRAG_STRETCH_CLAMP (:235)        ✅实时
  按压判定阈值           slider   1e-4..0.01 默认 0.001  → PRESS_VISIBLE / DRAG_VISIBLE     ✅实时
  速度可见阈值           slider   0.1..2.0  默认 0.5     → POINTER_VISIBLE (:218)           ✅实时
  光斑半径倍率           slider   0.5..3.0  默认 1.5     → GLOW_REACH_SCALE (:97)           ✅实时
  光斑均匀底             slider   0..0.3    默认 0.08    → ★编译期(Shaders.cpp:443)          ❌编译期
  光斑点增益             slider   0..0.3    默认 0.15    → ★编译期(Shaders.cpp:443)          ❌编译期
```

（Kyant 侧的对应数值：`DampedDragAnimation.kt:31-40`、`InteractiveHighlight.kt:27-30, 66, 73`；
ytbl 侧常量：`GlassConfig.h:213-235`、`:97`。`[源码]`）

---

## 3.3 逐条实时性判定（**从源码推出的规则**）

| 判定 | 触发条件 | 源码依据 |
| --- | --- | --- |
| `✅实时` | 只走 `setFloatUniform` / `setColorUniform`，或重建 `RenderEffect` 对象 | `Lens.kt:44-54`；`HighlightStyle.kt:60-66, 90-95`；`DrawBackdropModifier.kt:362-368`（每帧 `updateEffects()`） |
| `⚠️换程序` | 参数变化导致 **shader 字符串或 effect 类型**变化 → `obtainRuntimeShader(key, string)` 取到**不同 key**，首次产生一次编译 | `RuntimeShaderCache.kt:14-16`；`Lens.kt:33-43`（`chromaticAberration` 换字符串）；`HighlightStyle.kt:56-70` vs `:82-99`（换 key "Default"/"Ambient"）；`HighlightStyle.kt:41`（Plain 返回 null = 无 shader） |
| `🛠改代码` | 数值硬编码在 Kotlin/Java 侧（不在 shader 里），要做成控件须改源码 | `HighlightStyle.kt:93-94`（Ambient 角度/衰减）；`HighlightModifier.kt:153`（`*2f`）；`LiquidButton.kt:94`（tint alpha 0.75）；`InverseLayerScope.kt` 的逆变换逻辑 |
| `❌编译期` | 数值在 AGSL 字符串 / C++ `constexpr` 里 | `Shaders.kt:64`（`circleMap`）；`Shaders.kt:79,116,175,197`（`radius * 1.5`）；`Shaders.kt:119-157`（色散 7 采样权重与通道分配）；`GlassConfig.h:53-63`（`BLUR_*`）；`Shaders.cpp:443`（glow 的 0.08/0.15） |

★ **一个反直觉但很重要的点**：**色散权重表**在 Kyant 里是编译期常量（`Shaders.kt:123-157`），
但在 ytbl 里已经提成 **uniform 数组**（`uPrismDist[3]` / `uPrismEvenRed[3]` … `Shaders.cpp:131-138`），
其默认值来自 `GlassConfig.h:100-108`。⇒ **ytbl 的色散比 Kyant 更可调**。
如果要给面板加"棱镜权重"控件，ytbl **已经是 `✅实时`**（只需 `glUniform1fv`）。

---

# 第四部分：ytbl 覆盖情况与缺口

## 4.1 「Kyant 参数 → ytbl 字段/uniform」映射总表

| Kyant 参数（库层） | Kyant 有 | ytbl 对应 | ytbl 状态 | 备注 |
| --- | --- | --- | --- | --- |
| `blur(radius)` | ✅ | `Optics.blurPx` → `job.blurRadius` | ✅ 已支持 | 上限 10dp（`GlassConfig.h:69`） |
| `blur(edgeTreatment)` | ✅ | —— | ❌ **缺口 G2** | |
| `lens(refractionHeight)` | ✅ | `Optics.lensHeightPx` → `uLensHeight` | ✅ 已支持 | 上限 `LENS_REACH_MAX=128` px |
| `lens(refractionAmount)` | ✅ | `Optics.lensAmountPx` → `uLensAmount` | ✅ 已支持 | |
| `lens(depthEffect: Boolean)` | ✅ | `Optics.depthAmount: Float` → `uLensDepth` | ✅ 已支持（**更强，0..1 连续**） | `1.0f == depthEffect=true` |
| `lens(chromaticAberration: Boolean)` | ✅ | `Optics.spectral: Float` → `uSpectral` | ✅ 已支持（**更强，0..2 连续**） | `SPECTRAL_MAX=2.0`（`GlassConfig.h:98`）；`0` 关闭 |
| 折射剖面 `circleMap` | ✅ | `lensCurve` | ⚠️ **公式不同，G4** | |
| 法线的 `radius*1.5` | ✅ | —— | ❌ **缺口 G5** | |
| `colorControls(brightness/contrast/saturation)` | ✅ | `Optics.brightness/contrast/saturation` → `uToneCtrl` | ✅ 已支持（权重 `0.213/0.715/0.072` 完全相同） | ytbl 额外 clamp 到 `[-1,1]/[0,2]/[0,3]`（`GlassPanel.cpp:249-251`） |
| `vibrancy()`（saturation 1.5） | ✅ | `Optics.colorBoost` → `uToneBoost`（×1.5） | ✅ 已支持 | `Shaders.cpp:370` 与 `ColorFilter.kt:41` 等价 |
| `opacity(alpha)` | ✅ | `Optics.opacityOn/opacity` → `uPanelOpacity` | ✅ 已支持 | |
| `colorFilter(任意 ColorFilter)` | ✅ | —— | ❌ 缺口（只支持上面几个预设） | 难度：中 |
| `Highlight.width` | ✅ | `EdgeLight.widthDp` → `uEdgeWidth` | ✅ 已支持 | ytbl 多一步 `ceilf` + clamp（`GlassPanel.cpp:119-124`） |
| `Highlight.blurRadius` | ✅ | `EdgeLight.softnessDp` → `uEdgeBlur` | ✅ 已支持（**注意 1.5× 预设差异**，见 §3.2 分组 4） | |
| `Highlight.alpha` | ✅ | `EdgeLight.alpha` → `uEdgeAlpha` | ✅ 已支持 | 内部再乘 `style.alpha`（`GlassPanel.cpp:118`） |
| `HighlightStyle.Default(angle, falloff)` | ✅ | `EdgeLight.angleDegrees/falloff` → `uEdgeAngle/uEdgeFalloff` | ✅ 已支持 | |
| `HighlightStyle.Ambient(intensity)` | ✅ | `EdgeLight::ambient(strength)` | ⚠️ 只作为预设函数入参，**不是 `PanelStyle` 字段** | 要做控件需在面板侧写 tint alpha |
| `HighlightStyle.Plain` | ✅ | `EdgeMode::Plain` | ✅ 已支持 | |
| `Highlight.blendMode` | ✅ | —— | ❌ **缺口 G6** | Gradient/Plain/Ambient 三分支都是加法 |
| `HighlightStyle` 新增（Contour） | ❌ Kyant 无 | `EdgeMode::Contour` | ✅ ytbl 独有增强 | |
| `Shadow.radius/offset/color/alpha` | ✅ | `DropShadow.radiusDp/offsetXDp/offsetYDp/color/alpha` | ✅ 已支持 | `SHADOW_OFFSET_DIVISOR=6.0` 与 Kyant `radius/6f` 一致（`GlassConfig.h:123`） |
| `Shadow.blendMode` | ✅ | —— | ❌ **缺口 G6** | |
| `InnerShadow.radius/offset/color/alpha` | ✅ | `InsetShadow.radiusDp/offsetXDp/offsetYDp/color/alpha` | ✅ 已支持 | 默认 offset 语义有差异，见 §3.2 分组 6 |
| `InnerShadow` 的 `TileMode.Decal` | ✅ | 解析式 `exp(-2t²)`（`Shaders.cpp:432`） | ✅ 效果等价（实现不同） | |
| `Shadow.spread` | ❌ | ❌ | —— | **两边都没有** |
| 形状：4 角独立半径 | ✅ | `Corners(tl,tr,br,bl)` | ✅ 已支持（字段顺序一致） | |
| 形状：连续曲率（squircle） | ✅ | `boxMetric`（标准圆角 SDF） | ❌ **缺口 G1（最大）** | |
| Surface / Tint | ❌ 库层无（app 层有） | `PanelStyle.surface` + `SurfaceMode{None,Hue,Overlay}` | ✅ ytbl 更强 | Hue 分支数学与 `LiquidButton.kt:93-94` 对齐（`Shaders.cpp:384-389`；`HUE_BRIDGE=0.35`，`GlassConfig.h:180-181`） |
| `RuntimeShaderEffect` 自定义 shader | ✅ 公开 API | —— | ❌ 缺口 G10 | |
| `CombinedBackdrop` 多背景合成 | ✅ | —— | ❌ **缺口 G7** | |
| `onDrawBackdrop` 变换钩子 | ✅ | `backdropScaleX/Y/blend` 部分覆盖 | ⚠️ **部分（G8）** | |
| `layerBlock` rotationZ / transformOrigin | ✅ | —— | ❌ **缺口 G9** | |
| `padding`（图层外扩） | ✅ 自动 | `capturePadding()` 自动 | ✅ 已支持 | `CAPTURE_PAD_*`（`GlassConfig.h:72-75`） |
| 按压光斑（InteractiveHighlight 等价物） | ✅ app 层 | `PanelStyle.glow`（`GlowSpot`）→ `uGlow*` | ✅ 已支持且**参数一致** | `add = 0.08*level + 0.15*level*spot`（`Shaders.cpp:443`）↔ `White@0.08*progress` + `White@0.15*progress`（`InteractiveHighlight.kt:66, 72`）；`glowReach = shorter*1.5`（`GlassPanel.cpp:198`）↔ `size.minDimension*1.5f`（`:73`）；衰减 `smoothstep(reach, reach*0.5, dist)`（`Shaders.cpp:441-442`）↔ `smoothstep(radius, radius*0.5, dist)`（`:52`） |
| 主题自适应（`setGlassColors`/亮度带） | ❌ Kyant 库层无 | `Color::LUMA_*` / `setGlassColors` | ✅ ytbl 独有 | `GlassConfig.h:141-143` |
| 液态融合（droplet / feed / strand） | ❌ Kyant 无（`docs/KYANT_GLASS_TECH.md:983-988` 亦记为「未找到」） | `PanelStyle.liquid/drop*/feed*/strand*` | ✅ ytbl 独有 | |

## 4.2 数值等价性抽检（**逐字对照**）

| 项 | Kyant | ytbl | 结论 |
| --- | --- | --- | --- |
| 色调映射 | `t = (0.5f - c*0.5f + brightness) * 255f`（`ColorFilter.kt:58`），矩阵加在 8bit 域 | `lift = 0.5 - contrast*0.5 + brightness`（`Shaders.cpp:277`），直接加在 0..1 域 | ✅ 数学等价 `[推断]` |
| 灰度权重 | `0.213f / 0.715f / 0.072f`（`ColorFilter.kt:53-55`） | `vec3(0.213, 0.715, 0.072)`（`Shaders.cpp:276`） | ✅ 相同 |
| vibrancy 饱和增益 | `saturation = 1.5f`（`ColorFilter.kt:41`） | `ctrl.z *= 1.5`（`Shaders.cpp:370`） | ✅ 相同 |
| 色散棱镜距离 | 采样偏移集合 `{1, 2/3, 1/3, 0, -1/3, -2/3, -1}`（`Shaders.kt:125, 129, 134, 139, 143, 148, 152`） | `PRISM_DIST = {1/3, 2/3, 1}`（`GlassConfig.h:101`），正负对称展开 | ✅ 相同集合（ytbl 用 3 对 + 中心循环展开） |
| 主导通道权重 | `/3.5`（`Shaders.kt:126, 130, 135, 140, 144`）、`/3.0`（`:145, 149, 154`）、`/7.0`（`:127, 131, 136, 141, 146, 150, 155`） | 由 `PRISM_*_RED/GREEN/BLUE`（`GlassConfig.h:102-108`）组合出 `3/21 = 1/7`、`6/21 = 2/7`、`7/42 = 1/6` … | ⚠️ **形式不同**：Kyant 是「7 次采样固定加和」，ytbl 是「3 对前后对称采样 + even/odd 分解」。`[推断]` 视觉近似但**不逐位等价** |
| alpha 累加 | `/7.0` × 7 次（`Shaders.kt` 内 7 处） | `pairAlpha = 1/7`，3 对 + 中心共 7 项（`Shaders.cpp:351-364`） | ✅ 相同（总和同为 7×1/7） |
| 边光带宽 | `strokeWidth = ceil(width)*2` 居中描边（`HighlightModifier.kt:153`） | `band = 1 - smoothstep(bandWidth, bandWidth + bandBlur*2, depth)`（`Shaders.cpp:397`） | ⚠️ 近似（Kyant 向内外各摊 width；ytbl 只向内侧） |
| 内阴影衰减 | `BlurMaskFilter/BlurEffect(radius)` 高斯 | `exp(-2 * t²)`，`t = depth/reach`（`Shaders.cpp:432`） | ⚠️ 近似（ytbl 是解析高斯近似） |

## 4.3 ★ ytbl 缺口清单（需要新增实现）+ 难度评估

> 难度分级：**低** = 加一个 `PanelStyle` 字段 + 一条 `glUniform` / 改一行 shader；
> **中** = 需要改 shader 结构或加 pass；**高** = 需要改架构（坐标系统/多 pass/多纹理）。

| ID | 缺口 | Kyant 依据 | ytbl 现状 | 建议实现 | 难度 |
| --- | --- | --- | --- | --- | --- |
| **G1** | **连续曲率圆角（squircle）** | `RoundedRectangle(r)` 全部调用点（§1.8.2）；本地已有移植 `build/recon/squircle_path.h` | `boxMetric()` 用 `length(max(reach,0))-r`，是**标准圆角**（`Shaders.cpp:180-186`） | ① 在 shader 里把 `boxMetric` 换成连续曲率 SDF 的解析近似（Kyant Shapes 的 10 控制点 Bézier 剖面的多项式近似）；② 或按 `docs/M3.5_SQUIRCLE_SDF_BAKE.md` 烘 SDF 贴图、shader 里查表 | **中**（数学移植工作量已由 `squircle_path.h` 覆盖；纯 shader 解析近似是**中高**，因为要保证法线 `boxNormal` 与新 SDF 一致） |
| **G2** | **Blur 的 `TileMode`（Clamp/Decal/Repeat/Mirror）** | `Blur.kt:11`；`InnerShadowModifier.kt:110` 用 `Decal` | gather/blur pass 无边缘模式参数 | 在 `kGatherFragmentSource`（`Shaders.cpp:59`）加一个 `uniform float uEdgeMode`，在越界采样时按模式取 `clamp` / `transparent` / `fract` / 三角折返 | **低** |
| **G3** | **Blur 降采样率 / taps 运行时化** | Kyant 无（平台内部） | `BLUR_TAPS=12` / `BLUR_DECIMATE_LIMIT=16` / `BLUR_STEP_FROM_SIGMA=0.25` 都是 `constexpr`（`GlassConfig.h:56-63`） | `taps` 是 `for` 循环上界，GLES 3.0 要求循环可静态展开 ⇒ **不能直接 uniform**。可行方案：编译多份 shader 变体（taps ∈ {8,12,16}），运行期按档位切 program | **中**（要引入 program 变体选择） |
| **G4** | **折射剖面曲线**：Kyant `circleMap(x)=1-sqrt(1-x²)`（`Shaders.kt:64`）vs ytbl `lensCurve(t)=t*tan(asin(t)*0.5)`（`Shaders.cpp:205-208`） | `Shaders.kt:78` | ytbl 已用 `lensCurve` | 加 `uniform float uLensProfile`，在 shader 里 `mix(circleMap(t), lensCurve(t), uLensProfile)`；或直接以 `uLensProfile=1` 复现 Kyant | **低** |
| **G5** | **法线场的 `× 1.5` 圆角放大** | `Shaders.kt:79, 116, 175, 197`：`min(radius * 1.5, min(halfSize.x, halfSize.y))` | ytbl 三处都是 `min(round, min(half.x, half.y))`，**没有 ×1.5**（`Shaders.cpp:329, 392`） | 把 `bodyRound`/`edgeRound` 改成 `min(round * uGradGain, min(half.x, half.y))`，`uGradGain` 默认按目标对齐取 `1.5`（对齐 Kyant）或 `1.0`（保持现状） | **低**（一行 + 一个 uniform） |
| **G6** | **高光/阴影的 `BlendMode`** | `Highlight.kt:14` / `Shadow.kt:19` / `InnerShadow.kt:22`；`HighlightStyle.kt:35, 47`（Plus）、`:80`（SrcOver） | shader 里全部是加法（`color.rgb += tint*add`，`Shaders.cpp:408, 412, 422`） | 加 `uniform float uEdgeBlend`，在加法 / `mix(color.rgb, vec3(1.0), add)` / `SrcOver` 三种之间分支 | **低-中** |
| **G7** | **多 backdrop 合成（`CombinedBackdrop`）** | `backdrops/CombinedBackdrop.kt:12-99`；`MagnifierContent.kt:84` 一次合成 3 个 | 只有一个 `g_backdropTexture`（`GlassPanel.cpp:63-66`） | ① 把多个 backdrop 画到同一张 FBO 再绑定（推荐，改动最小）；② shader 侧加第二张纹理 + `uniform float uMix2` | **中**（①）/ **高**（②） |
| **G8** | **`onDrawBackdrop` 里的任意变换**（放大镜 1.5× + `translate(top=-80dp)`） | `MagnifierContent.kt:95-103`；`DrawBackdropModifier.kt:271-279` | `backdropScaleX/Y + backdropBlend` 只能做**以中心为基准的等比缩放**（`Shaders.cpp:249-254`） | 加 `uniform vec2 uSampleCenter` + `uniform vec2 uSampleAffine[2]`，把 `moved` 的算法从「中心缩放」改成「任意 2×2 仿射 + 平移」 | **低-中** |
| **G9** | **`layerBlock` 的 `rotationZ` / `transformOrigin`** | `GlassPlaygroundContent.kt:80-89`；`AdaptiveLuminanceGlassContent.kt:111-121` | `drawPanel(style, Box)` 是**轴对齐矩形**（`GlassUI.h:368`） | 对 `drawPanel` 增加「变换后 4 角」入参，再做屏幕空间四边形光栅 + 逆变换采样。**代价高** | **高** |
| **G10** | **用户自定义 shader 注入（`runtimeShaderEffect`）** | `effects/RenderEffect.kt:20-34`；`ProgressiveBlurContent.kt:41-60` 就是这样注入的 | 固定 GLES 管线 | 不建议做（会破坏 "ytbl 是成品组件库" 的定位）；若必须，可开放「后处理 pass 回调」 | **高** |
| **G11** | **`colorFilter(任意 ColorMatrix)`** | `ColorFilter.kt:11-15` | 只有 brightness/contrast/saturation + vibrancy | `PanelStyle` 加 `float toneMatrix[20]` + `bool toneMatrixOn`，shader 里替换 `toneMap()` | **低** |
| **G12** | **`HighlightStyle.Ambient` 的独立强度字段** | `HighlightStyle.kt:75` `intensity: Float` | 只有 `EdgeLight::ambient(strength)` 静态工厂（`GlassUI.h:262`），**不是 `PanelStyle` 的字段** | `EdgeLight` 加 `float ambientIntensity = 0.38f`，`applyEdge` 里合成 tint alpha | **低** |
| **G13** | **`InnerShadow.offsetY` 跟随 `radius`** | `InnerShadow.kt:19` `DpOffset(0f.dp, radius)` | `InsetShadow` 的 radius/offsetY 独立（`GlassUI.h:281-293`） | 面板侧做联动即可（不是库缺口） | **低**（面板侧） |
| **G14** | **组件级参数暴露** | 见第二部分 | `PillButton` / `Toggle` / `Slider` / `TabStrip` / `Modal` / `PopMenu` 只暴露少量字段（`GlassUI.h:372-592`） | 面板要"每组件一份"时，把这些类的字段换成"指向 `GlassDebugParams` 的指针"或增加对应 setter | **低-中** |

### 4.4 反向缺口（**ytbl 有、Kyant 没有**，面板里应保留并标注）

| 能力 | ytbl 字段/uniform | 依据 |
| --- | --- | --- |
| 液态融合（droplet / feed / strand 三路 SDF 融合） | `liquid/dropCenter/dropHalf/dropRadius/feedCenter/feedHalf/feedRadius/feedBlend/strandCenter/strandHalf/strandRadius/strandBlend` | `GlassUI.h:350-361`；`Shaders.cpp:306-340`；`GlassPanel.cpp:143-171` |
| `darkGuard`（暗部保底） | `Optics.darkGuard` → `uDarkGuard` | `GlassUI.h:306`；`GlassPanel.cpp:241`；`Shaders.cpp:231` |
| `EdgeMode::Contour` 轮廓光 | `EdgeLight::contour(...)`；`EDGE_CONTOUR_*` | `GlassUI.h:263-264`；`GlassConfig.h:89-94`；`Shaders.cpp:402-409` |
| 主题/亮度带自适应 | `Color::LUMA_BAND_LO/HI`、`setGlassColors` | `GlassConfig.h:141-143`；`GlassUI.h:150-152` |
| 收缩动画（blur capture shrink） | `BLUR_CAPTURE_SHRINK_RATIO/FRAMES` | `GlassConfig.h:65, 68` |
| 色散棱镜权重运行时可调 | `uPrismDist[3]` / `uPrismEvenRed[3]` … | `Shaders.cpp:131-138` |
| 拖尾/track 层（`trackOn/trackBox/trackRadius/trackColor`） | `PanelStyle.track*` | `GlassUI.h:343-346`；`GlassPanel.cpp:173-186`；`Shaders.cpp:236-243` |
| 菜单/弹窗成套动效 | `PopMenu` / `Modal` 的一整套 `DIALOG_*` / `MENU_*` | `GlassConfig.h:250-348, 572-613`；`GlassUI.h:431-478, 565-592` |

---

# 第五部分：落地检查清单

1. **范围修正**：模糊上限从 `0..10 dp` 放宽到 `0..16 dp`（`DialogContent.kt:66` 用到 16dp，超出 `BLUR_RADIUS_MAX_DP`）。`[源码]`
2. **补三个 uniform**：`uGradGain`（G5，×1.5 对齐）、`uLensProfile`（G4）、`uEdgeBlend`（G6）—— 三个都是**低难度**且影响肉眼可见。
3. **色散默认值对齐**：面板 `spectral` 默认建议 `0.70`（窗口）/ `0.75`（卡片）/ `0.90`（实验场），照抄 `secondary_imgui.cpp:48, 58, 73`。`[源码]`
4. **`Highlight.Ambient` 的 1.5× 陷阱**：面板需要一个明确的"Ambient 预设"选择（库默认 `0.5/0.25` 还是组件用的 `0.3333/0.1667`），否则与 Kyant 成品对不上。
5. **不要做这些控件**：shadow/inner-shadow 的 `spread`（两边都不存在）、blur 的"模糊类型"（库里只有一种平台模糊）、lens 的"色散强度 0..1"（Kyant 是 bool，ytbl 已经是 `0..2` 连续量，直接用后者）。
6. **只读显示（不设控件）**：`padding`、`size`、`renderEffect` 链顺序、`capturePadding()` 的结果 —— 这些是诊断信息，做成 `ImGui::Text` 即可。
7. **不要把 ytbl 缺口的控件做成"假控件"**：G7/G8/G9/G10 四项在实现前应**灰显 + 难度标签**，放在 `lytbl-gap` 组里当路线图。

---

## 附：本文引用的源码文件总表（含行数）

| 文件 | 总行数 | 本文引用处 |
| --- | --- | --- |
| `backdrop/.../effects/Blur.kt` | 29 | §1.1 |
| `backdrop/.../effects/Lens.kt` | 120 | §1.2、§1.8.1、§1.9.1 |
| `backdrop/.../effects/ColorFilter.kt` | 75 | §1.3、§4.2 |
| `backdrop/.../effects/RenderEffect.kt` | 34 | §4.1（runtimeShaderEffect） |
| `backdrop/.../internal/Shaders.kt` | 204 | §1.9 |
| `backdrop/.../highlight/Highlight.kt` | 28 | §1.4.1 |
| `backdrop/.../highlight/HighlightStyle.kt` | 138 | §1.4.2 |
| `backdrop/.../highlight/HighlightModifier.kt` | 166 | §1.4.3 |
| `backdrop/.../shadow/Shadow.kt` | 27 | §1.5 |
| `backdrop/.../shadow/InnerShadow.kt` | 41 | §1.6 |
| `backdrop/.../shadow/ShadowModifier.kt` | 137 | §1.5 |
| `backdrop/.../shadow/InnerShadowModifier.kt` | 158 | §1.6 |
| `backdrop/.../Backdrop.kt` | 17 | §1.10 |
| `backdrop/.../BackdropEffectScope.kt` | 74 | §0.4、§1.10 |
| `backdrop/.../DrawBackdropModifier.kt` | 388 | §0.4、§1.10 |
| `backdrop/.../RuntimeShader.kt`（common） | 26 | §1.9 |
| `backdrop/.../RuntimeShaderCache.kt` | 21 | §0.4、§3.3 |
| `backdrop/.../Platform.kt`（common） | 5 | §0.3 |
| `backdrop/.../internal/ShapeProvider.kt` | 44 | §1.8.1 |
| `backdrop/.../internal/Outline.kt` | 18 | §1.8.1 |
| `backdrop/.../internal/LayerRecorder.kt` | 26 | §1.10 |
| `backdrop/.../internal/InverseLayerScope.kt` | 130 | §1.8.3 |
| `backdrop/.../backdrops/LayerBackdrop.kt` | 74 | §1.8.3 |
| `backdrop/.../backdrops/CanvasBackdrop.kt` | 35 | §1.10 |
| `backdrop/.../backdrops/CombinedBackdrop.kt` | 99 | §1.10、§4.3-G7 |
| `backdrop/.../backdrops/EmptyBackdrop.kt` | 25 | §1.10 |
| `backdrop/.../backdrops/Backdrop.kt` | 37 | §1.10 |
| `backdrop/.../backdrops/LayerBackdropModifier.kt` | 69 | §1.10 |
| `backdrop/src/androidMain/.../Platform.kt` | 10 | §0.3 |
| `backdrop/src/androidMain/.../RuntimeShader.kt` | 70 | §1.9 |
| `backdrop/src/androidMain/.../internal/RenderEffect.kt` | 50 | §0.4、§1.9 |
| `backdrop/src/androidMain/.../internal/Paint.kt` | 16 | §1.4.1 |
| `app/.../components/LiquidButton.kt` | 122 | §2.1 |
| `app/.../components/LiquidToggle.kt` | 203 | §2.2 |
| `app/.../components/LiquidSlider.kt` | 212 | §2.3 |
| `app/.../components/LiquidBottomTab.kt` | 49 | §2.4.2 |
| `app/.../components/LiquidBottomTabs.kt` | 288 | §2.4 |
| `app/.../destinations/GlassPlaygroundContent.kt` | 241 | §2.5 |
| `app/.../destinations/DialogContent.kt` | 139 | §2.5 |
| `app/.../destinations/ControlCenterContent.kt` | 443 | §2.5 |
| `app/.../destinations/MagnifierContent.kt` | 108 | §2.5 |
| `app/.../destinations/ProgressiveBlurContent.kt` | 71 | §2.5 |
| `app/.../destinations/AdaptiveLuminanceGlassContent.kt` | 163 | §2.5 |
| `app/.../destinations/LockScreenContent.kt` | 80 | §2.5 |
| `app/.../destinations/ScrollContainerContent.kt` | 52 | §2.5 |
| `app/.../destinations/LazyScrollContainerContent.kt` | 55 | §2.5 |
| `app/.../destinations/ToggleContent.kt` | 61 | §2.5 |
| `app/.../destinations/SliderContent.kt` | 65 | §2.5 |
| `app/.../destinations/ButtonsContent.kt` | 63 | §2.5 |
| `app/.../destinations/BottomTabsContent.kt` | 91 | §2.5 |
| `app/.../destinations/HomeContent.kt` | 93 | §2.5（页面索引） |
| `app/.../utils/DampedDragAnimation.kt` | 137 | §2.5.1 |
| `app/.../utils/InteractiveHighlight.kt` | 121 | §2.5.1 |
| `app/.../utils/ProgressConverter.kt` | 18 | §2.5.1 |
| `app/.../utils/SdfShader.kt` | 75 | §1.9.6 |
| `app/.../BackdropDemoScaffold.kt` | 12 | §2.5 |
| `app/src/androidMain/.../BackdropDemoScaffold.kt` | 97 | §2.5（背景源 = 壁纸 + 可换图） |
| `ytbl/GlassConfig.h` | 691 | §3、§4 |
| `ytbl/GlassUI.h` | 606 | §3、§4 |
| `ytbl/GlassPanel.cpp` | 507 | §3.2、§4.2 |
| `ytbl/Shaders.cpp` | 459 | §3.3、§4.2 |
| `secondary_imgui.cpp` | 951 | §3.0、§3.1 |

**未核实项汇总**（不编造）：

1. `com.kyant.shapes` 的 `RoundedRectangle` 是否暴露 `smoothing` 之类的连续曲率参数 —— 库源码不在本地 tar（`[未核实]`）。
2. Skia 是否用 `Paint` 的 alpha 调制 `RuntimeShader` 输出（影响 `HighlightStyle.Ambient` 的最终强度是否为 `0.38²`）—— `[未核实]`。
3. `Capsule()` 在 `com.kyant.shapes` 里实现的是 `RoundedRectangularShape` 还是 `CornerBasedShape` —— 从 `LiquidButton.kt:57,61` 能同时用 `Capsule()` + `lens()` 反推「二者之一」`[推断]`，确切类型 `[未核实]`。
4. ytbl 的 `uEdgeMode` 各分支与 Kyant 三种 Style 的**逐像素**等价性 —— 只做了公式级对照（§4.2），未做像素级验证 `[未核实]`。
