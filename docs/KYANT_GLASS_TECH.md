# Kyant0「液态玻璃」精确技术参数与 SDF 数学

> 分析对象：本地已下载源码
> - `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/`（Kotlin/Compose Multiplatform，主实现）
> - `build/recon/ref_kyant/Shapes.tar/Shapes-master/`（连续曲率圆角算法）
> - `build/recon/ref_kyant/GlassExplorer.tar/GlassExplorer-main/`（iOS 私有 API 玻璃展示）
> - `build/recon/ref_kyant/AMDL.tar/AMDL-master/`（已确认与本主题**无关**，是 Apple Music 下载器，正文不再引用）
>
> 本文所有 GLSL/AGSL 均为**逐字摘录**。每条结论标注来源文件与行号。
> 标记约定：**【源码】**= 源码里明确写的；**【推断】**= 我的推理，源码未直接写；**【未找到】**= 源码中确实没有。
>
> 2026-02 补充：本文对 `clock_sdf.webp` 做了逐像素解码（VP8L 无损 WebP，数值可信），
> 用于反推「预烘焙 SDF 贴图」的通道语义与归一化尺度。这部分标为**【实测】**。

---

## 0. 路径别名表（正文中大量使用）

| 别名 | 绝对路径（相对 `D:\纯c图形基座\`） |
|---|---|
| `SDF-COMMON` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/commonMain/kotlin/com/kyant/backdrop/catalog/utils/SdfShader.kt` |
| `SDF-ANDROID` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/androidMain/kotlin/com/kyant/backdrop/catalog/utils/SdfShader.android.kt` |
| `SDF-SKIKO` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/skikoMain/kotlin/com/kyant/backdrop/catalog/utils/SdfShader.skiko.kt` |
| `SHADERS` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/backdrop/src/commonMain/kotlin/com/kyant/backdrop/internal/Shaders.kt` |
| `BK/` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/backdrop/src/commonMain/kotlin/com/kyant/backdrop/` |
| `BK-ANDROID/` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/backdrop/src/androidMain/kotlin/com/kyant/backdrop/` |
| `APP/` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/commonMain/kotlin/com/kyant/backdrop/catalog/` |
| `APP-ANDROID/` | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/androidMain/kotlin/com/kyant/backdrop/catalog/` |
| `SHAPES/` | `build/recon/ref_kyant/Shapes.tar/Shapes-master/shapes/src/commonMain/kotlin/com/kyant/shapes/` |
| `GE/` | `build/recon/ref_kyant/GlassExplorer.tar/GlassExplorer-main/GlassExplorer/` |
| 资源 | `build/recon/ref_kyant/AndroidLiquidGlass.tar/AndroidLiquidGlass-kmp/app/src/commonMain/composeResources/drawable/clock_sdf.webp` |

**重要前提**：Kyant 写的是 **AGSL**（Android Graphics Shading Language，跑在 `android.graphics.RuntimeShader` 上），
不是 GLSL ES。语法差异见 §12.1，移植时必须映射。

---

## 1. 【最高优先级】SDF Shader 逐字完整摘录

### 1.1 `SDF-COMMON`（全文，共 75 行）

```kotlin
package com.kyant.backdrop.catalog.utils

import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.unit.dp
import com.kyant.backdrop.BackdropEffectScope
import org.intellij.lang.annotations.Language
import org.jetbrains.compose.resources.DrawableResource
import org.jetbrains.compose.resources.imageResource

@Composable
fun rememberSdfShader(drawableResource: DrawableResource): SdfShader {
    val imageResource = imageResource(drawableResource)
    return remember(imageResource) { SdfShader(imageResource) }
}

expect fun SdfShader(imageBitmap: ImageBitmap): SdfShader

@Immutable
interface SdfShader {

    val width: Int

    val height: Int

    fun BackdropEffectScope.apply(
        refractionHeight: Float = 48f.dp.toPx(),
        lightAngle: Float = 45f
    )
}
```

（以上为 `SDF-COMMON:1-32`）

`SDF-COMMON:34-75` —— **这就是你要的 SDF shader 全文**：

```glsl
@Language("AGSL")
internal const val SdfShaderString =
    """
uniform shader content;
uniform shader sdfTex;

uniform float2 size;
uniform float2 sdfTexSize;
uniform float refractionHeight;
uniform float lightAngle;

float circleMap(float x) {
    return 1.0 - sqrt(1.0 - x * x);
}

half4 main(float2 coord) {
    half2 p = coord / size * sdfTexSize;
    if (p.x < 0.0 || p.y < 0.0 || p.x >= sdfTexSize.x || p.y >= sdfTexSize.y) {
        return half4(0.0);
    }
    half4 v = sdfTex.eval(p);
    float sd = v.r * 2.0 - 1.0;
    v.a = smoothstep(0.5, 1.0, v.a);
    if (v.a <= 0.0) {
        return half4(0.0);
    }
    if (v.a < 1.0) {
        sd = 0.0;
    }
    float2 normal = normalize(v.gb * 2.0 - 1.0);
    
    float intensity = circleMap(1.0 - min(1.0, -sd * 1.5));
    float2 refractedCoord = coord - intensity * refractionHeight * normal;

    half4 color = content.eval(refractedCoord) * v.a;
    float2 lightDir = float2(cos(lightAngle * 3.1415926 / 180.0), sin(lightAngle * 3.1415926 / 180.0));
    float bevelIntensity = clamp(dot(normal, lightDir), 0.0, 1.0);
    color.rgb *= 1.0 + 0.5 * intensity * bevelIntensity;
    bevelIntensity = clamp(dot(normal, -lightDir), 0.0, 1.0);
    color.rgb *= 1.0 + 0.5 * bevelIntensity * min(1.0, smoothstep(1.0, 0.0, abs(intensity - 0.25) * 6.0));
    return color;
}"""
```

（`SDF-COMMON:34-75`，逐字，包含行 51/57/63/64 的空行缩进与 `    ` 行 64 的尾随空格）

### 1.2 `SDF-ANDROID`（全文，共 43 行）

```kotlin
package com.kyant.backdrop.catalog.utils

import android.graphics.BitmapShader
import android.graphics.Shader
import androidx.compose.runtime.Immutable
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import com.kyant.backdrop.BackdropEffectScope
import com.kyant.backdrop.asAndroidRuntimeShader
import com.kyant.backdrop.effects.runtimeShaderEffect
import com.kyant.backdrop.isRuntimeShaderSupported

actual fun SdfShader(imageBitmap: ImageBitmap): SdfShader {
    return SdfShaderImpl(imageBitmap)
}

@Immutable
private class SdfShaderImpl(val sdfBitmap: ImageBitmap) : SdfShader {

    private val sdfTexture =
        BitmapShader(sdfBitmap.asAndroidBitmap(), Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)

    override val width: Int
        get() = sdfBitmap.width

    override val height: Int
        get() = sdfBitmap.height

    override fun BackdropEffectScope.apply(
        refractionHeight: Float,
        lightAngle: Float,
    ) {
        if (isRuntimeShaderSupported()) {
            runtimeShaderEffect("SdfShader", SdfShaderString, "content") {
                asAndroidRuntimeShader().setInputBuffer("sdfTex", sdfTexture)
                setFloatUniform("size", size.width, size.height)
                setFloatUniform("sdfTexSize", sdfBitmap.width.toFloat(), sdfBitmap.height.toFloat())
                setFloatUniform("refractionHeight", refractionHeight)
                setFloatUniform("lightAngle", lightAngle)
            }
        }
    }
}
```

（`SDF-ANDROID:1-43`）

### 1.3 `SDF-SKIKO`（桌面/iOS 同源实现，用于对照）

`SDF-SKIKO:16-40` 与 Android 版几乎相同，差别只有：
- `SDF-SKIKO:18`：`private val sdfTexture = sdfBitmap.asSkiaBitmap().makeShader()`
- `SDF-SKIKO:32`：`asSkikoRuntimeShader().child("sdfTex", sdfTexture)`

其余 uniform 设置与 Android 完全一致（`SDF-SKIKO:33-38`）。

### 1.4 唯一调用点（SDF 贴图路径）

`APP/destinations/LockScreenContent.kt:37-74`：

```kotlin
val sdfShader = rememberSdfShader(Res.drawable.clock_sdf)
...
.drawPlainBackdrop(
    backdrop = backdrop,
    shape = { Rectangle },
    effects = {
        colorControls(brightness = -0.1f, contrast = 0.75f, saturation = 1.5f)
        blur(2f.dp.toPx())
        with(sdfShader) { apply() }
    },
    onDrawBackdrop = { drawBackdrop ->
        drawBackdrop()
        drawRect(Color.White.copy(alpha = 0.25f))
    }
)
.widthIn(max = 400f.dp)
.aspectRatio(sdfShader.width.toFloat() / sdfShader.height.toFloat())
```

- `APP/destinations/LockScreenContent.kt:37` —— 加载 `clock_sdf` 贴图
- `APP/destinations/LockScreenContent.kt:63` —— `colorControls(brightness=-0.1, contrast=0.75, saturation=1.5)`
- `APP/destinations/LockScreenContent.kt:64` —— `blur(2f.dp.toPx())`
- `APP/destinations/LockScreenContent.kt:65` —— `with(sdfShader) { apply() }`（默认参数：`refractionHeight = 48dp`，`lightAngle = 45f`，见 `SDF-COMMON:29-31`）
- `APP/destinations/LockScreenContent.kt:69` —— 在 backdrop 上再叠一层 `White @ 0.25α`
- `APP/destinations/LockScreenContent.kt:72` —— 最大宽度 400dp

---

## 2. 问题 1：连续曲率圆角的 SDF 到底怎么算？

### 2.1 直接结论（三条，务必先看）

1. **Kyant 的液态玻璃库（`backdrop`）在 shader 里根本没有「连续曲率圆角 SDF」。**
   `SHADERS.kt` 里的 `sdRoundedRect`（`SHADERS:33-38`）是**标准圆角矩形 SDF**（四角为**正圆**，半径由 `cornerRadii` 逐角给出）。
   `lens()` 走的是这条解析式路径，不是查表，也不是 Bézier。
2. **「查表」路径只在一个演示里出现**：`LockScreenContent`（锁屏时钟），用的是**预烘焙 RGBA 贴图** `clock_sdf.webp`（1599×515，VP8L 无损）。
   贴图**不是仓库里生成的**——仓库里没有生成器。见 §2.4。
3. **没有任何代码把「10 个 Bézier 控制点」变成距离场。**
   Bézier 那 20 个 double（10 个点）只被用来 `cubicTo` 画**轮廓路径**（`SHAPES/RoundedRectangleOutline.kt:108-188`），
   和 shader 的距离场**没有任何连接**。见 §2.3。

**【未找到】**：仓库中不存在把 G2 连续曲率轮廓转成 SDF 的代码、也不存在 `clock_sdf.webp` 的生成脚本/工具。

### 2.2 参数化面板实际用的 SDF：逐字摘录 `SHADERS.kt`

`SHADERS:21-48`（被 4 个 shader 复用的 SDF 函数块）：

```glsl
@Language("AGSL")
private const val RoundedRectSDF = """
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
}"""
```

**数学表达式（源码明确）**

- `radii` 的打包顺序是 **(x<0,y≤0)=radii.x、(x≥0,y≤0)=radii.y、(x≥0,y>0)=radii.z、(x<0,y>0)=radii.w**
  —— 即 **TL, TR, BR, BL**（`SHADERS:23-31`；调用侧 `BK/effects/Lens.kt:66-71` 传的是 `topLeft, topRight, bottomRight, bottomLeft`）。
  注意：**分界点是 `coord.y <= 0.0` 走 radii.y/z**，而 `centeredCoord` 的 y 轴向下（Compose 屏幕坐标）——
  所以 `radii.y` 在 `y<=0`（上半）是 topRight，`radii.z` 在 `y>0`（下半）是 bottomRight。**自洽**。
- `sdRoundedRect` 是 Inigo Quilez 标准圆角矩形 SDF：
  `q = |p| - (halfSize - r)`；`sd = length(max(q,0)) - r + min(max(q.x,q.y),0)`。
  **四角是半径 r 的正圆**，不是 G2。
- `gradSdRoundedRect` 是同一 SDF 的梯度：
  - 角区（`q.x≥0 或 q.y≥0`）：`sign(p) * normalize(max(q,0))`（单位向量）
  - 边区（`q.x<0 且 q.y<0`）：返回 `sign(p) * (1,0)` 或 `sign(p) * (0,1)`，由 `step(q.y, q.x)` 选择
  - **梯度方向 = 指向形状外部**（用于折射时 `d` 取负实现向内采样，见 §5）

### 2.3 「10 个 Bézier 控制点」在源码里的确切含义与用法

`SHAPES/ContinuousCurvatureRoundedRectangleCornerBuilder.kt`（全文关键部分，逐字）：

```kotlin
internal class ContinuousCurvatureRoundedRectangleCornerBuilder(
    val extendedFraction: Double = 2.0 / 3.0,
    val arcFraction: Double = 0.5
) {

    private val theta = (1.0 - arcFraction) * FRAC_PI_4
    private val cos = cos(theta)
    private val sin = sin(theta)
    private val cot = 1.0 / tan(theta)
    private val cos2 = cos * cos
    private val sin2 = sin * sin
    private val cos3 = cos2 * cos
    private val sin3 = sin2 * sin
```

（`SHAPES/ContinuousCurvatureRoundedRectangleCornerBuilder.kt:10-22`）
`k0/k1/k2/k3` 常数见 `SHAPES/...:24-33`，三次/四次方程单根求解器见 `SHAPES/...:150-170`。
返回值是 **20 个 double = 10 个点**（`SHAPES/...:97` 和 `:141`）：

```kotlin
return doubleArrayOf(x0, 0.0, x1, 0.0, x2, 0.0, x3, y3, x4, y4, x5, y5, x6, y6, 1.0, y7, 1.0, y8, 1.0, y9)
```

点的语义（配合 `SHAPES/RoundedRectangleOutline.kt:113-128` 反推，**【推断】**但唯一自洽解读）：

| 索引 | 含义 |
|---|---|
| p[0..1] | 曲线起点（在切线上，x 方向） |
| p[2..3], p[4..5] | 第 1 段 cubic 的两个控制点 |
| p[6..7] | 第 1 段终点 = 第 2 段起点 |
| p[8..9], p[10..11] | 第 2 段 cubic 的两个控制点 |
| p[12..13] | 第 2 段终点 = 第 3 段起点 |
| p[14..15], p[16..17] | 第 3 段 cubic 的两个控制点 |
| p[18..19] | 曲线终点 |

即 **一个 90° 角由 3 段三次 Bézier 拼成（9 个控制点 + 1 个起点 = 10 个点）**，坐标是**以角半径 r 归一化**的（用的时候乘 r，再加角基点偏移）。
`SHAPES/RoundedRectangleOutline.kt:111-128` 的用法：

```kotlin
var x = w - r
var y = 0.0
moveTo((x + p[0] * r).toFloat(), (y + p[1] * r).toFloat())
cubicTo(
    (x + p[2] * r).toFloat(), (y + p[3] * r).toFloat(),
    (x + p[4] * r).toFloat(), (y + p[5] * r).toFloat(),
    (x + p[6] * r).toFloat(), (y + p[7] * r).toFloat()
)
cubicTo( ... p[8..13] ... )
cubicTo( ... p[14..19] ... )
```

因此：**Bézier → 轮廓路径 → `Outline.Generic(Path)` → `clipPath`**（`BK/internal/Outline.kt:16-17` 走 `Outline.Generic -> clipPath(outline.path)`）。
**没有任何一步生成距离场。**

另外注意 `SHAPES/RoundedRectangleOutline.kt:106-107`：

```kotlin
val tW = ((width * 0.5 - r) / r).fastCoerceIn(0.0, 1.0)
val tH = ((height * 0.5 - r) / r).fastCoerceIn(0.0, 1.0)
```

即 Bézier 形状**随宽高比变化**（`tW/tH` 传入 builder，`SHAPES/...:47-59` 有 corner case 缓存）。

### 2.4 `clock_sdf.webp` 的真实格式（**【实测】**，逐像素解码）

文件：`app/src/commonMain/composeResources/drawable/clock_sdf.webp`
- 容器：RIFF/WEBP，子块 **`VP8L`（无损）**，86986 字节 ⇒ **通道数值无压缩误差，可直接当参考真值**
- 尺寸：**1599 × 515**，RGBA8

**通道语义（由 shader 解码式反推 + 实测验证）**

| 通道 | 语义 | 实测取值范围 | 编码公式（实测拟合） |
|---|---|---|---|
| **R** | 有符号距离，**仅形状内部有效** | 0 … **128/255 = 0.5020**（外部全部钉在 128，即 0.502） | `R8 = clamp(round(128 - 2.36 * d_in), 0, 128)` |
| **G** | 外法线 `.x` | 0 … 255 | `G8 = round(127.5 + 127.5 * n.x)` |
| **B** | 外法线 `.y` | 0 … 255 | `B8 = round(127.5 + 127.5 * n.y)` |
| **A** | **向外的覆盖度斜坡**（不是简单地 AA 1px） | 0 … 255（内部恒 255，外部线性衰减） | `A8 = clamp(round(255 - 18.4 * d_out), 0, 255)` |

**实测证据链**

1. 沿一条水平边（`y=336`，左边界在 `x≈891.5`，该处法线 `n=(-0.843,-0.537)`）扫描：
   `x=892 → R8=127`，`x=893 → 125`，`x=894 → 123` … 每像素 **−2.0**；
   换算到**垂直法线方向**：`2.0 / 0.843 = 2.37 R8-units / px`。
2. 沿一条斜边（`x=1058`，上边界在 `y≈74`，法线同为 `(-0.843,-0.537)`）扫描：
   每像素 **−1.24**；换算 `1.24 / 0.537 = 2.31 R8-units / px`。
   ⇒ 两个方向归一后一致：**≈ 2.34 R8-units / 法向像素**。
   全局统计 `median(|∇R|) = 0.0098 /px`，与 `2.34/255 = 0.0092` 吻合。
3. 因此 `R = clamp(0.5 - d/S, 0, 0.5)`，其中 **`S = 1 / 0.00925 ≈ 108 纹理像素`**；
   等价地 **`R` 在 `d ≈ 54 纹理像素` 处归零**。
   （实测：`R8 ≤ 0.001` 的像素只出现在 4 个点，其向内距离 ≈ 54–55 px。）
4. 法线通道验证：像素 `(1050,100)` 的 `n=(-0.843,-0.537)`，其最近边界点 `(1044,98)` 处 `n_bd=(-0.843,-0.537)` ⇒
   **G/B 存的是「边界上最近点的外法线」，不是距离场逐像素梯度**（所以内部法线是分片常数、可见块状）。
5. A 通道向外衰减：`d_out` 每 +2px，A 均值降 ≈0.14 ⇒ **−0.072/px = 18.4/255 per px**，在 `d_out ≈ 13.9px` 处归零。
   `d_out = 0` 处 A ≈ 0.95（不是 1.0）。

**把上面代回 shader（`SDF-COMMON:55-73`）得到的实际数值行为（【推断】但由实测直接推出）**

| 位置 | `sd = 2R-1` | `v.a = smoothstep(0.5,1,A)` | `intensity` |
|---|---|---|---|
| 形状边界（`d_in = 0`） | 0 | 1 | **1.0**（最大位移） |
| 向内 `d_in = 36 px` | −36/54 = −0.667 | 1 | **0.0**（无折射） |
| 向内 `d_in > 36 px` | < −0.667 | 1 | 0.0 |
| 向外 `d_out ∈ (0, 6.9px)` | 被强制为 0 | 1→0 | **1.0**（整个边缘带都满位移） |
| 向外 `d_out > 6.9px` | 任意 | 0 | 直接 `return half4(0.0)` |

- 折射带的宽度：`-sd * 1.5 < 1  ⟺  d_in < 2S/3 = 36.1 纹理像素`（由 `SDF-COMMON:65` 的 `* 1.5` 与 `S=108` 推出）
- 软边宽度：`smoothstep(0.5,1.0,A)` 把 A 的 0.5 点映射成 `v.a=0`，即**形状向外 6.9 纹理像素**（`SDF-COMMON:56`）
- 位移量：`|Δ| = intensity * refractionHeight * normal`，默认 `refractionHeight = 48dp`（`SDF-COMMON:30`、`SDF-COMMON:66`）

**纹理坐标映射**（`SDF-COMMON:50-53`）：

```glsl
half2 p = coord / size * sdfTexSize;
if (p.x < 0.0 || p.y < 0.0 || p.x >= sdfTexSize.x || p.y >= sdfTexSize.y) {
    return half4(0.0);
}
```

- **无宽高比校正**：贴图被拉伸铺满控件（`coord/size` 是控件归一化坐标，乘回贴图尺寸）。
  调用侧靠 `aspectRatio(sdfShader.width / sdfShader.height)`（`APP/destinations/LockScreenContent.kt:73`）保证不变形。
- Android 侧用 `BitmapShader(..., TileMode.CLAMP, TileMode.CLAMP)`（`SDF-ANDROID:20-21`），
  **外加**上面这段手写边界判断。

**关键未知项（【未找到】）**：`clock_sdf.webp` 的**生成器不在仓库里**。
因此 `S = 108` 究竟是「生成器写死的常数」还是「按本形状最大内切深度归一化」**无法从源码确定**。
判断方法：若你能拿到 Kyant 另一个形状的 SDF 资产，量一下它的 `ΔR8/px`；
若同样是 2.36，则是固定常数；若不同，则是按形状归一化。

### 2.5 给 C++/GLES 移植的落地方案（**【推断/建议】**，非源码内容）

你有两种自洽做法。**推荐 A**，因为只有 A 能和你的轮廓严格一致。

#### 方案 A：离线/初始化时把 Bézier 轮廓烘成 SDF 贴图（复刻 Kyant 的查表路径）

1. 用现有移植版算出 20 个 double，按 `SHAPES/RoundedRectangleOutline.kt:111-188` 的写法拼出闭合路径
   （4 个角各 3 段 cubic + 4 条直线；注意 `tW/tH` 依赖控件宽高比，所以**每个尺寸都要重算**，
   或按 `tW/tH` 网格烘 4 张角贴图再选）。
2. 在贴图分辨率下光栅化覆盖掩码（建议解析法直接算 Bézier 的有符号覆盖，避免走 GPU 读回）。
3. 求欧氏距离变换（内 + 外）。推荐 Felzenszwalb 两遍平方距离变换（O(N)，精度足够）。
4. 写 RGBA8：
   - `R8 = clamp(round(128 - 2.36 * d_in), 0, 128)`（若要复刻 Kyant 的 `S≈108`；也可自选尺度，见下）
   - `G8 = round(127.5 + 127.5 * n.x)`，`B8 = round(127.5 + 127.5 * n.y)`，
     其中 `n` = **边界上最近点的单位外法线**（不是像素梯度！否则块状感与 Kyant 不同）
   - `A8 = clamp(round(255 - 18.4 * d_out), 0, 255)`（内部 255）
5. 把 §1.1 的 shader 按 §12.1 映射成 GLSL ES 300 即可，`S` 的选取直接决定折射带宽度：
   `band_texpx = 2S/3`。**若你想让折射带宽度在屏幕空间可控，更实用的做法是令 `S = band_screen_px / (2/3) * (texW / screenW)`**。

#### 方案 B：完全不烘贴图，在片元里解析求距（**【推断】**）

- **B1（Kyant 自己的做法，最省）**：直接用 §2.2 的圆角矩形 SDF。四角是正圆，与 G2 轮廓**不严格吻合**，
  但 Kyant 本人就是这么做的（`SHADERS:31/69-72` 用圆角 SDF，`SHAPES` 用 G2 画轮廓），差异集中在 45° 角区，
  肉眼在 16–32dp 圆角上几乎看不出来。**建议先按 B1 落地，再对比截图决定要不要升级。**
- **B2（精确但贵）**：距离 3 段 cubic 的精确最近点需要解 5 次方程；实践上每段做 8–16 次 Newton 迭代 ×4 角 = 太贵。
  工程折中：**预烘一张 256×256 的单角 SDF 图集**（只需 `tW=tH=1` 一种），运行时
  `sd = max(sd_core, sd_corner_sampled)`（`sd_core` = 内缩矩形的 SDF），双线性采样即可。
- **B3（超椭圆近似）**：`|x/a|^n + |y/b|^n = 1`，`n≈4~5`。**注意**：Kyant/iOS 的真实构造是 3 段 cubic 链，
  超椭圆只是近似；`n` 需要你按 `SHAPES` 的曲线做最小二乘拟合，**源码没有给这个 n**。【未找到】

---

## 3. 问题 2：用到了哪些 shader pass？各自输入输出

### 3.1 架构：**不是多 pass FBO 链，而是「1 个离屏 GraphicsLayer + RenderEffect 链」**

【源码】`BK/DrawBackdropModifier.kt`：
- `:254-258` 离屏层：`clip = true; shape = shapeProvider.shape; compositingStrategy = Offscreen`
- `:285-304` `drawBackdropLayer`：把 backdrop 内容录进 `graphicsLayer`，层尺寸 = `size + 2*padding`，`layer.topLeft = (-padding, -padding)`
- `:362-368` `updateEffects()`：`effectScope.apply(effects)` 后 `graphicsLayer?.renderEffect = effectScope.renderEffect`
- `:316-335` 绘制顺序：`onDrawBehind → drawBackdropLayer() → onDrawSurface → drawContent() → onDrawFront`
  （另有 `exportedBackdrop` 在 `:327-334` 再录一份不含 content 的层，供其它玻璃面板当背景用）

**【源码】链的拼接顺序**（关键）：`BK/effects/RenderEffect.kt:13-17`

```kotlin
fun BackdropEffectScope.effect(effect: RenderEffect) {
    if (!isRenderEffectSupported()) return

    renderEffect = renderEffect.chain(effect)
}
```

`BK-ANDROID/internal/RenderEffect.kt:13-22`

```kotlin
internal actual fun RenderEffect?.chain(other: RenderEffect): RenderEffect {
    return if (this != null) {
        android.graphics.RenderEffect.createChainEffect(
            other.asAndroidRenderEffect(),
            this.asComposeRenderEffect()
        ).asComposeRenderEffect()
    } else {
        other
    }
}
```

`createChainEffect(outer, inner)` 的语义是「先 inner 再 outer」，而这里 `outer = other`（新加的），
`inner = this`（已累积的）。⇒ **effects 闭包按书写顺序依次串接**，后写的作用在前者的输出上。
（【推断】基于 Android `RenderEffect.createChainEffect(outer, inner)` 的 API 语义；源码本身没有注释说明。）

### 3.2 pass 清单

| # | pass | 实现 | 输入 | 输出 / 参数 |
|---|---|---|---|---|
| 0 | **背景层录制** | `recordLayer` + `drawLayer` | 屏幕/图片内容的 `LayerBackdrop`（`BK/backdrops/LayerBackdrop.kt:46-68`，含逆变换对齐） | 尺寸 `size+2*padding` 的离屏纹理 |
| 1 | **颜色滤镜（vibrancy / colorControls / opacity）** | `android.graphics.RenderEffect.createColorFilterEffect`（`BK-ANDROID/internal/RenderEffect.kt:36-49`） | 上一层输出 | 5×4 `ColorMatrix`；见 §8 |
| 2 | **模糊** | `androidx.compose.ui.graphics.BlurEffect`（`BK/effects/Blur.kt:22-28`） | 上一层输出 | `BlurEffect(prev, radius, radius, edgeTreatment)` |
| 3 | **折射（透镜）** | `RuntimeShaderEffect`，AGSL 见 §5.1 | `content` = 上一层输出（`uniform shader content`） | 位移后的采样结果 |
| 3' | **折射 + 色散** | AGSL 见 §5.2 | 同上 | 7 次 `content.eval` 加权 |
| 4 | **边缘高光（bevel highlight）** | 独立离屏层 + `BlendMode.Plus`（`BK/highlight/HighlightModifier.kt:117-131`），着色器见 §6.2 | 图形层（仅描边） | 叠加到最终画面 |
| 5 | **外阴影** | 独立离屏层 + `BlurEffect(radius, radius, TileMode.Decal)`（`BK/shadow/ShadowModifier.kt:131`） | 描边轮廓 | 画在内容之后、clip 之内 |
| 6 | **内阴影** | 独立离屏层 + `BlendMode.Clear` 遮罩 + `BlurEffect(..., TileMode.Decal)`（`BK/shadow/InnerShadowModifier.kt:107-125`） | 轮廓 + 偏移后的 Clear 描边 | 只保留内侧一圈 |
| 7 | **surface 覆盖色** | `onDrawSurface { drawRect(...) }`（如 `APP/components/LiquidButton.kt:91-99`、`APP/components/LiquidTabs.kt:180`） | — | 普通 `drawRect`，非 shader |

**`content` 这个 uniform 的含义**：AGSL 的 `uniform shader content` 是 Android `createRuntimeShaderEffect(shader, "content")` 注入的
「上一层 RenderEffect 结果」的 shader（`BK-ANDROID/internal/RenderEffect.kt:24-33`），
在片元里用 `content.eval(coord)` 采样。它**不是**纹理句柄，port 到 GLES 时就是「上一层 FBO 的 sampler2D」。

---

## 4. 问题 3：模糊是怎么做的？

### 4.1 【源码】只用平台模糊，没有自定义 blur shader

`BK/effects/Blur.kt:9-29`（全文）：

```kotlin
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

- **半径**：调用方给的 `radius`（px），同时用作 X/Y 半径（各向同性），`TileMode` 默认 `Clamp`
- **padding 机制**：`radius > padding` 时把 `padding = radius`（`:17-20`），
  离屏层尺寸因此变成 `size + 2*radius`（`BK/DrawBackdropModifier.kt:292-295`）
- **sigma / taps / 是否降采样**：**【未找到】** —— `BlurEffect` 最终交给 Android `RenderEffect.createBlurEffect`，
  内部由 Skia/硬件实现，仓库里没有任何 sigma、tap 数、降采样倍率的代码。

### 4.2 【源码】渲染器不支持时的降级路径

`APP/utils/InteractiveHighlight.kt:84-89`：没有 RuntimeShader 时用

```kotlin
drawRect(
    Color.White.copy(0.25f * progress),
    blendMode = BlendMode.Plus
)
```

代替高光 shader。这是全仓库唯一一处「非 shader 降级」，**不是模糊的降级**。

### 4.3 各处实际用的模糊半径（【源码】，单位 dp，写在调用点）

| 场景 | 半径 | 来源 |
|---|---|---|
| LiquidButton | `blur(2f.dp.toPx())` | `APP/components/LiquidButton.kt:60` |
| LiquidSlider（拖动块） | `blur(8f.dp.toPx() * (1f - progress))` | `APP/components/LiquidSlider.kt:169` |
| LiquidToggle（拖动块） | `blur(8f.dp.toPx() * (1f - progress))` | `APP/components/LiquidToggle.kt:160` |
| LiquidBottomTabs（外栏 & 选中块） | `blur(8f.dp.toPx())` | `APP/components/LiquidTabs.kt:171`、`:209` |
| 锁屏 SDF 时钟 | `blur(2f.dp.toPx())` | `APP/destinations/LockScreenContent.kt:64` |
| Dialog | `blur(16dp)` 浅色 / `blur(8dp)` 深色 | `APP/destinations/DialogContent.kt:66` |
| 设置面板 | `blur(4f.dp.toPx())` | `APP/destinations/GlassPlaygroundContent.kt:132` |
| 滚动容器卡片 | 无（只有 `vibrancy()` + `lens`） | `APP/destinations/ScrollContainerContent.kt:42-43` |
| 程序化渐进模糊 | `blur(4f.dp.toPx())` + 下面的 alpha 遮罩 | `APP/destinations/ProgressiveBlurContent.kt:40` |
| ControlCenter 背景 | `BlurEffect(4dp*progress, 4dp*progress)`（直接给 graphicsLayer） | `APP/destinations/ControlCenterContent.kt:197-200` |
| 自适应亮度玻璃 | `blur(lerp(8dp,16dp,l))` 或 `blur(lerp(8dp,2dp,-l))` | `APP/destinations/AdaptiveLuminanceGlassContent.kt:104-107` |

**渐进模糊（唯一带遮罩的模糊）** `APP/destinations/ProgressiveBlurContent.kt:41-60`（逐字）：

```glsl
    uniform shader content;
    
    uniform float2 size;
    layout(color) uniform half4 tint;
    uniform float tintIntensity;
    
    half4 main(float2 coord) {
        float blurAlpha = smoothstep(size.y, size.y * 0.5, coord.y);
        float tintAlpha = smoothstep(size.y, size.y * 0.5, coord.y);
        return mix(content.eval(coord) * blurAlpha, tint * tintAlpha, tintIntensity);
    }
```

调用参数：`setFloatUniform("size", ...)`、`setColorUniform("tint", tintColor)`、`setFloatUniform("tintIntensity", 0.8f)`
（`APP/destinations/ProgressiveBlurContent.kt:57-59`）；`tintColor` = 浅色 `Color.White` / 深色 `Color(0xFF808080)`（`:27`）。

---

## 5. 问题 4：折射 / 色散 / 边缘高光的确切参数与公式

### 5.1 折射（无像差）—— `SHADERS:50-84` 逐字

```glsl
@Language("AGSL")
internal const val RoundedRectRefractionShaderString = """
uniform shader content;

uniform float2 size;
uniform float2 offset;
uniform float4 cornerRadii;
uniform float refractionHeight;
uniform float refractionAmount;
uniform float depthEffect;

$RoundedRectSDF

float circleMap(float x) {
    return 1.0 - sqrt(1.0 - x * x);
}

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
}"""
```

（`$RoundedRectSDF` 是 Kotlin 字符串模板，展开内容见 §2.2）

**公式（源码明确）**

设 `t = (-sd) / refractionHeight = d_in / refractionHeight ∈ [0,1]`，`d_in` 为到边界的向内距离（px）：

```
circleMap(x)          = 1 - sqrt(1 - x^2)
d                     = circleMap(1 - t) * refractionAmount
                      = (1 - sqrt(1 - (1-t)^2)) * refractionAmount
                      = (1 - sqrt(t*(2-t))) * refractionAmount
grad                  = normalize( gradSdRoundedRect(centered, halfSize, min(1.5*r, min(halfSize.x,halfSize.y)))
                                 + depthEffect * normalize(centered) )
refractedCoord        = coord + d * grad
```

- `t = 0`（边界）⇒ `d = refractionAmount`（最大）；`t = 1` ⇒ `d = 0`。**距离衰减正好是「圆弓形」profile**
- `d_in ≥ refractionHeight` 的像素**完全不采样**，直接 `return content.eval(coord)`（`:73-75`，早退）
- `sd = min(sd, 0.0)`（`:76`）把边界外的值钳到 0
- **`refractionAmount` 在 Kotlin 侧被取负传入**：`BK/effects/Lens.kt:49`
  ```kotlin
  setFloatUniform("refractionAmount", -refractionAmount)
  ```
  ⇒ `d ≤ 0` ⇒ `refractedCoord` 沿 `grad`（指向外部）**反向**偏移，即**向形状内部采样 ⇒ 放大（透镜）效果**
- `gradRadius = min(radius * 1.5, min(halfSize.x, halfSize.y))`：防止大圆角（如 Capsule）把梯度算歪
- `depthEffect`：0/1 开关（`BK/effects/Lens.kt:50`，`if (depthEffect) 1f else 0f`）。
  =1 时向梯度里叠加 `normalize(centeredCoord)`（**从中心向外的径向分量**），模拟「凸透镜穹顶」，
  使中心区域也产生轻微径向偏移。【推断】这是为了让面板看起来有体积感，源码无注释。
- `offset = -padding`（`BK/effects/Lens.kt:46`）；`padding` 的变化见 `BK/effects/Lens.kt:25-27`：
  ```kotlin
  if (padding > 0f) {
      padding = (padding - refractionHeight).fastCoerceAtLeast(0f)
  }
  ```
  ⇒ **先 blur 后 lens 时，lens 会把 blur 撑出来的 padding 收回 `refractionHeight` 这么多**（因为折射只向内采样，不需要额外外边距）。

**默认与用法数值（【源码】）**

| 组件 | `refractionHeight` | `refractionAmount` | `depthEffect` | `chromaticAberration` | 来源 |
|---|---|---|---|---|---|
| `LiquidButton` | `12dp` | `24dp` | false | false | `APP/components/LiquidButton.kt:61` |
| `GlassPlaygroundContent` 主玻璃 | `frac*minDim*0.5`（默认 0.2） | `frac*minDim`（默认 0.2） | **true** | 开关 | `APP/destinations/GlassPlaygroundContent.kt:72-77` |
| 同一页面的 sheet | `16dp` | `32dp` | false | false | `APP/destinations/GlassPlaygroundContent.kt:133` |
| `LiquidSlider` 拖动块 | `10dp * pressProgress` | `14dp * pressProgress` | false | **true** | `APP/components/LiquidSlider.kt:170-174` |
| `LiquidToggle` 拖动块 | `5dp * pressProgress` | `10dp * pressProgress` | false | **true** | `APP/components/LiquidToggle.kt:161-165` |
| `LiquidBottomTabs` 外栏 | `24dp` | `24dp` | false | false | `APP/components/LiquidTabs.kt:172` |
| `LiquidBottomTabs` 颜色层 | `24dp * pressProgress` | `24dp * pressProgress` | false | false | `APP/components/LiquidTabs.kt:210-213` |
| `LiquidBottomTabs` 选中指示块 | `10dp * pressProgress` | `14dp * pressProgress` | false | **true** | `APP/components/LiquidTabs.kt:246-250` |
| Dialog | `24dp` | `48dp` | **true** | false | `APP/destinations/DialogContent.kt:67` |
| ControlCenter 每块 | `24dp * progress` | `48dp * progress` | **true** | false | `APP/destinations/ControlCenterContent.kt:119-123` |
| 自适应亮度玻璃 | `24dp` | `size.minDimension / 2f` | **true** | false | `APP/destinations/AdaptiveLuminanceGlassContent.kt:108` |
| 放大镜 | `8dp` | `24dp` | **true** | **true** | `APP/destinations/MagnifierContent.kt:87-92` |
| 滚动卡片 | `16dp` | `32dp` | false | false | `APP/destinations/ScrollContainerContent.kt:43` |

**形状限制（【源码】）**：`BK/effects/Lens.kt:57` / `:116-120`
—— 只支持 `RoundedRectangularShape` / `AbsoluteRoundedCornerShape` / `CornerBasedShape`，
否则抛 `UnsupportedOperationException("Only RoundedRectangularShape or CornerBasedShape is supported in lens effects.")`。
`cornerRadii` 的提取见 `BK/effects/Lens.kt:62-114`，四种形状分支都做 `fastCoerceAtMost(maxRadius)`，`maxRadius = size.minDimension / 2f`。

### 5.2 色散（chromatic aberration）—— `SHADERS:86-158` 逐字

```glsl
@Language("AGSL")
internal val RoundedRectRefractionWithDispersionShaderString = """
uniform shader content;

uniform float2 size;
uniform float2 offset;
uniform float4 cornerRadii;
uniform float refractionHeight;
uniform float refractionAmount;
uniform float depthEffect;
uniform float chromaticAberration;

$RoundedRectSDF

float circleMap(float x) {
    return 1.0 - sqrt(1.0 - x * x);
}

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
}"""
```

**公式与权重（源码明确）**

```
dispersionIntensity = chromaticAberration * ( cx * cy ) / ( halfSize.x * halfSize.y )
                      // cx,cy = centeredCoord；四角处 |值|≈1，坐标轴上为 0
dispersedCoord      = d * grad * dispersionIntensity
```
7 次采样，偏移系数：`+1, +2/3, +1/3, 0, -1/3, -2/3, -1`（沿 `dispersedCoord` 方向）

| tap | 偏移 | R 权重 | G 权重 | B 权重 | A 权重 |
|---|---|---|---|---|---|
| red | +1 | 1/3.5 | – | – | 1/7 |
| orange | +2/3 | 1/3.5 | 1/7 | – | 1/7 |
| yellow | +1/3 | 1/3.5 | 1/3.5 | – | 1/7 |
| green | 0 | – | 1/3.5 | – | 1/7 |
| cyan | −1/3 | – | 1/3.5 | 1/3 | 1/7 |
| blue | −2/3 | – | – | 1/3 | 1/7 |
| purple | −1 | 1/7 | – | 1/3 | 1/7 |

- 权重归一：R 总权重 `3/3.5 + 1/7 = 1.0`；G 总权重 `1/7 + 3/3.5 = 1.0`；B 总权重 `3/3 = 1.0`；A 总权重 `7/7 = 1.0` ✔
  （注意 B 用 `/3.0` 而其它用 `/3.5`，是为了让三种颜色总量都归到 1）
- `chromaticAberration` uniform 在 Kotlin 侧恒传 `1f`（`BK/effects/Lens.kt:51-53`），**开关是编译期选的另一个 shader**
  （`BK/effects/Lens.kt:33-43`：`if (!chromaticAberration)` 选 `"Refraction"`，否则 `"RefractionWithDispersion"`）
- **色散强度随位置变化**：`(cx*cy)/(hx*hy)` 在四条边中点为 0，在四个角最大 ⇒ **色散只出现在角上**
- 色散方向沿 `dispersedCoord`，而 `d ≤ 0`（amount 取负）、`grad` 指向外，所以位移方向在四个象限交替翻转
- 代价：**7 次纹理采样/像素**（无像差版是 1 次）

### 5.3 边缘高光 —— `SHADERS:160-204` 逐字

```glsl
@Language("AGSL")
internal const val DefaultHighlightShaderString = """
uniform float2 size;
uniform float4 cornerRadii;
layout(color) uniform half4 color;
uniform float angle;
uniform float falloff;

$RoundedRectSDF

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
}"""
```

```glsl
@Language("AGSL")
internal const val AmbientHighlightShaderString = """
uniform float2 size;
uniform float4 cornerRadii;
uniform float angle;
uniform float falloff;

$RoundedRectSDF

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
}"""
```

**公式（源码明确）**

```
grad      = gradSdRoundedRect(centeredCoord, halfSize, min(1.5*r, min(halfSize.x, halfSize.y)))
normal    = (cos(angle), sin(angle))
d         = dot(grad, normal)          // ∈ [-1, 1]
intensity = pow(abs(d), falloff)       // Default：两侧对称
Ambient   : t = step(0.0, d)           // 只保留朝向光的一侧
            返回 half4(t,t,t,1) * intensity
```

**样式与默认值（`BK/highlight/Highlight.kt`、`BK/highlight/HighlightStyle.kt`）**

| 风格 | 颜色 | 混合模式 | angle | falloff | 来源 |
|---|---|---|---|---|---|
| `Plain` | `White α=0.38` | `BlendMode.Plus` | – | – | `BK/highlight/HighlightStyle.kt:33-42` |
| `Default` | `White α=0.5` | `BlendMode.Plus` | `45f` | `1f` | `BK/highlight/HighlightStyle.kt:45-71` |
| `Ambient` | `White α=intensity(0.38)` | `DefaultBlendMode`(SrcOver) | 固定 `45°` | 固定 `1f` | `BK/highlight/HighlightStyle.kt:74-100` |

`Highlight` 数据结构（`BK/highlight/Highlight.kt:10-15`）：

```kotlin
data class Highlight(
    val width: Dp = 0.5f.dp,
    val blurRadius: Dp = width / 2f,
    @param:FloatRange(from = 0.0, to = 1.0) val alpha: Float = 1f,
    val style: HighlightStyle = HighlightStyle.Default
)
```

**高光的栅格化方式（很重要，容易被忽略）** —— `BK/highlight/HighlightModifier.kt`：

```kotlin
paint.color = highlight.style.color
paint.strokeWidth = ceil(highlight.width.toPx().fastCoerceAtMost(size.minDimension / 2f)) * 2f
paint.blur(highlight.blurRadius.toPx())
```

（`BK/highlight/HighlightModifier.kt:152-154`）

- 用 **描边（`PaintingStyle.Stroke`，`:78-80`）** 画轮廓，**描边宽度 = `2 * ceil(width_px)`**（注意这个 ×2！）
- 描边以路径为中心 ⇒ 一半在内一半在外，外面那半被 `canvas.clipOutline(outline, clipPath)` 裁掉（`:123-125`）
  ⇒ 实际可见的是一条**内侧 bevel 高光**，宽度 ≈ `ceil(width_px)`
- 模糊半径 = `blurRadius`，Android 实现是 `BlurMaskFilter(radius, Blur.NORMAL)`（`BK-ANDROID/internal/Paint.kt:8-12`）
- 高光层整体 `alpha = highlight.alpha`、`blendMode = highlight.style.blendMode`（`:117-118`）
- 层尺寸 `safeSize = ceil(size)+2`，录制时 `translate(1f,1f)`，绘制时 `translate(-1f,-1f)`（`:101-131`）——
  为了给模糊留 1px 边界

**高光角度可以是重力方向**（`APP/destinations/ControlCenterContent.kt:97-106`）：

```kotlin
val uiSensor = rememberUISensor()
val glassHighlight = {
    Highlight(
        style = HighlightStyle.Default(
            angle = uiSensor.gravityAngle,
            falloff = 2f
        )
    )
}
```

重力角公式（`APP-ANDROID/utils/UISensor.kt:48-56`）：

```kotlin
val x = event.values[0]
val y = event.values[1]
val norm = sqrt(x * x + y * y + 9.81f * 9.81f)

val alpha = 0.5f
gravityAngle = gravityAngle * (1f - alpha) + atan2(y, x) * (180f / PI).toFloat() * alpha
gravity = gravity * (1f - alpha) + Offset(x / norm, y / norm) * alpha
```

- 默认 `gravityAngle = 45f`（`APP-ANDROID/utils/UISensor.kt:36`）
- 一阶低通，`alpha = 0.5f`，`SENSOR_DELAY_UI`（`:63`）

**自定义高光示例**：`APP/components/LiquidSlider.kt:176-183` 与 `APP/components/LiquidToggle.kt:167-174`

```kotlin
highlight = {
    val progress = dampedDragAnimation.pressProgress
    Highlight.Ambient.copy(
        width = Highlight.Ambient.width / 1.5f,
        blurRadius = Highlight.Ambient.blurRadius / 1.5f,
        alpha = progress
    )
},
```

⇒ 按压时 `Ambient` 高光的宽度与模糊半径各缩到 **1/1.5 ≈ 0.667 倍**，透明度 = `pressProgress`。

### 5.4 内阴影 / 外阴影（玻璃厚度感的另一半）

| 项 | 默认值 | 来源 |
|---|---|---|
| `Shadow.radius` | `24dp` | `BK/shadow/Shadow.kt:15` |
| `Shadow.offset` | `(0, radius/6)` | `BK/shadow/Shadow.kt:16` |
| `Shadow.color` | `Black α=0.1` | `BK/shadow/Shadow.kt:17` |
| `InnerShadow.radius` | `24dp` | `BK/shadow/InnerShadow.kt:18` |
| `InnerShadow.offset` | `(0, radius)` | `BK/shadow/InnerShadow.kt:19` |
| `InnerShadow.color` | `Black α=0.15` | `BK/shadow/InnerShadow.kt:20` |

内阴影实现（`BK/shadow/InnerShadowModifier.kt:107-125`）：在离屏层里
① 用 `BlurEffect(radius, radius, TileMode.Decal)` 模糊；② 画轮廓填充；③ `translate(offset)` 后用
`ShadowMaskPaint`（`BlendMode.Clear`，`:156-158`）**挖掉偏移后的轮廓**；④ 在 clip 内 `drawLayer`。
⇒ 只留下「内侧、偏移反方向」的一圈暗边。

组件里的用法（`APP/components/LiquidSlider.kt:184-196`、`APP/components/LiquidToggle.kt:175-187`）：

```kotlin
shadow = {
    Shadow(
        radius = 4f.dp,
        color = Color.Black.copy(alpha = 0.05f)
    )
},
innerShadow = {
    val progress = dampedDragAnimation.pressProgress
    InnerShadow(
        radius = 4f.dp * progress,
        alpha = progress
    )
},
```

`LiquidBottomTabs` 的选中块用 `InnerShadow(radius = 8f.dp * progress, alpha = progress)`（`APP/components/LiquidTabs.kt:260-266`），
放大镜用固定 `InnerShadow(radius = 16f.dp)`（`APP/destinations/MagnifierContent.kt:94`）。

---

## 6. 问题 5：玻璃形态融合（多个玻璃面板靠近时粘连）？

### 6.1 结论：**【未找到】**

在整个 `ref_kyant` 下检索 `metaball|gooey|smin|smoothMin|smoothUnion|fuse|stick|merge` **均无命中**（只有 `BlendMode` 相关命中）。
`SHADERS.kt` 里没有任何 min/max 平滑并集，SDF shader 只处理**单个**圆角矩形。

### 6.2 Kyant 实际怎么让多个面板「看起来是一组」

1. **共享同一个 backdrop 层**：多个面板各自 `drawBackdrop(backdrop = 同一个LayerBackdrop)`，
   于是它们的折射采样同一张三背景图，视觉上属于同一块玻璃
   —— `APP/destinations/ControlCenterContent.kt:216-440`（所有格子共用 `backdrop`，间距 `itemSpacing = 16f.dp`，`:67`）
2. **`exportedBackdrop` 嵌套**：面板 A 把自己的渲染结果导出成 `LayerBackdrop`，
   面板 B 用它当背景（`BK/DrawBackdropModifier.kt:327-334` 录制，`:151`/`:136` 传参）
   —— `APP/destinations/GlassPlaygroundContent.kt:121` + `:136`（sheet 导出给内部 slider 用）
   —— `APP/destinations/MagnifierContent.kt:84`：`rememberCombinedBackdrop(backdrop, contentBackdrop, cursorBackdrop)` 三层叠加
3. **组合 backdrop**：`BK/backdrops/CombinedBackdrop.kt:40-99` 只是把多个 backdrop 依序画一遍
   （`drawBackdrop` 逐个调用，`:49-56` / `:71-79` / `:90-98`），**不是数学并集**
4. **整体拉伸代替融合**：`LiquidBottomTabs` 拖动时整条面板会横向位移，量很小：
   ```kotlin
   val panelOffset by remember(density) {
       derivedStateOf {
           val fraction = (offsetAnimation.value / constraints.maxWidth).fastCoerceIn(-1f, 1f)
           with(density) {
               4f.dp.toPx() * fraction.sign * EaseOut.transform(abs(fraction))
           }
       }
   }
   ```
   （`APP/components/LiquidTabs.kt:88-95`，最大位移 **4dp**，用 `EaseOut` 缓动，方向由 `sign` 决定）
   然后应用在整条 Row 的 `translationX = panelOffset`（`:163-165`、`:200-202`、`:236`）。
   **这是「整体被拖着走」的弹性感，不是 metaball 粘连。** 回弹用 `spring(1f, 300f, 0.5f)`（`:116-119`）。
5. **选中块与底栏的视觉连接**靠三个独立图层：
   外栏（`blur(8dp)+lens(24dp,24dp)`，`:166-181`）、
   颜色层（`:203-228`，`ColorFilter.tint(accentColor)`，`:225`）、
   选中指示块（`:241-286`，带 `lens + chromaticAberration`）。
   选中块移动时靠 `interactiveHighlight`（位置跟随，`APP/components/LiquidTabs.kt:148-159`）和
   `LocalLiquidBottomTabScale`（`:190-194`，图标放大 `lerp(1f, 1.2f, pressProgress)`）营造连接感。

**【推断】** 如果你想要 iOS 那种「两个玻璃面板靠近时粘连」的效果，**必须自己实现**
（常见做法：`smin(a,b,k) = -log(exp(-k*a)+exp(-k*b))/k` 或 `(a+b-sqrt((a-b)^2+k^2))/2`），
Kyant 的代码里没有可直接抄的实现。

---

## 7. 问题 6：颜色 / 亮度 / 饱和度处理的确切系数

### 7.1 `BK/effects/ColorFilter.kt`（全文，75 行）

```kotlin
package com.kyant.backdrop.effects

import androidx.annotation.FloatRange
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.ColorMatrix
import androidx.compose.ui.graphics.ColorMatrixColorFilter
import com.kyant.backdrop.BackdropEffectScope
import com.kyant.backdrop.internal.ColorFilterEffect
import com.kyant.backdrop.isRenderEffectSupported

fun BackdropEffectScope.colorFilter(colorFilter: ColorFilter) {
    if (!isRenderEffectSupported()) return

    renderEffect = ColorFilterEffect(renderEffect, colorFilter)
}

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

private fun colorControlsColorFilter(
    brightness: Float = 0f,
    contrast: Float = 1f,
    saturation: Float = 1f
): ColorFilter {
    val invSat = 1f - saturation
    val r = 0.213f * invSat
    val g = 0.715f * invSat
    val b = 0.072f * invSat

    val c = contrast
    val t = (0.5f - c * 0.5f + brightness) * 255f
    val s = saturation

    val cr = c * r
    val cg = c * g
    val cb = c * b
    val cs = c * s

    val colorMatrix = ColorMatrix(
        floatArrayOf(
            cr + cs, cg, cb, 0f, t,
            cr, cg + cs, cb, 0f, t,
            cr, cg, cb + cs, 0f, t,
            0f, 0f, 0f, 1f, 0f
        )
    )
    return ColorMatrixColorFilter(colorMatrix)
}
```

（`BK/effects/ColorFilter.kt:1-75`，UTF-8 全文）

**确切系数（源码明确）**

- 亮度权重：`0.213 / 0.715 / 0.072`（**Rec. 709 的 R/G/B 亮度系数**，注意不是 0.2126/0.7152/0.0722，是三位近似）
- 饱和度矩阵：`M = c * (L + s * (I - L))`，其中 `L` 是每行都等于 `(0.213,0.715,0.072)` 的矩阵
- 对比度：整体乘 `c = contrast`，并加偏置 `t = (0.5 - 0.5*c + brightness) * 255`
  ⇒ **对比度围绕 0.5 灰点收缩，亮度直接加在偏置上，单位是 0–255**
- `vibrancy()` ≡ `colorControls(saturation = 1.5f)`（`BK/effects/ColorFilter.kt:41`）
  ⇒ 矩阵为 `a=0.213,b=0.715,c=0.072`，`invSat=-0.5`，`r=-0.1065, g=-0.3575, b=-0.036`，
  `cr+cs = -0.1065+1.5 = 1.3935`，非对角 `cg=-0.3575, cb=-0.036`，`t = 0`
- `opacity(alpha)` 只改 alpha 行第 4 项（`BK/effects/ColorFilter.kt:17-27`）

### 7.2 各处的实际颜色处理参数（【源码】）

| 场景 | 参数 | 来源 |
|---|---|---|
| `vibrancy()`（最常用） | `saturation = 1.5f` | `BK/effects/ColorFilter.kt:41`；用于 `GlassPlaygroundContent.kt:70`、`LiquidButton.kt:59`、`LiquidTabs.kt:170`、`:208`、`ScrollContainerContent.kt:42`、`ControlCenterContent.kt:118` |
| Dialog | 浅色 `brightness=0.2f, saturation=1.5f`；深色 `saturation=1.5f` | `APP/destinations/DialogContent.kt:62-65` |
| SDF 锁屏时钟 | `brightness=-0.1f, contrast=0.75f, saturation=1.5f` | `APP/destinations/LockScreenContent.kt:63` |
| 自适应亮度玻璃 | `l = sign(x)*x*x, x = luminance*2-1`；`l>0` 时 `brightness=lerp(0.1,0.5,l)`、`contrast=lerp(1,0,l)`；`l≤0` 时 `brightness=lerp(0.1,-0.2,-l)`、`contrast=1`；`saturation=1.5f` | `APP/destinations/AdaptiveLuminanceGlassContent.kt:94-103` |
| 自适应亮度采样 | 取 5×5 缩略图 25 像素，亮度 `0.2126*r + 0.7152*g + 0.0722*b`，阈值 `>0.5` 选黑字否则白字，`tween(1000)` | `APP/destinations/AdaptiveLuminanceGlassContent.kt:57-79` |
| 按钮 tint | `drawRect(tint, blendMode = BlendMode.Hue)` 后再 `drawRect(tint.copy(alpha = 0.75f))` | `APP/components/LiquidButton.kt:93-94` |
| surface 覆盖色 | `containerColor` 例如浅色 `Color(0xFFFAFAFA).copy(0.4f)` / 深色 `Color(0xFF121212).copy(0.4f)`，Dialog 用 `.copy(0.6f)` | `APP/components/LiquidTabs.kt:72-74`、`APP/destinations/DialogContent.kt:42-44` |
| 底栏选中块覆盖 | 浅色 `Black α=0.1` / 深色 `White α=0.1`，`alpha = 1 - pressProgress`；再叠 `Black α=0.03*pressProgress` | `APP/components/LiquidTabs.kt:274-282` |
| 拖动块覆盖 | `drawRect(Color.White.copy(alpha = 1f - progress))` | `APP/components/LiquidSlider.kt:204-207`、`LiquidToggle.kt:195-198` |

---

## 8. 次要任务 7：`GlassExplorer` 的玻璃组件、视觉效果清单与参数

### 8.1 项目性质

**【源码】** 这是 **Objective-C++ / Swift 的 iOS 私有 API 逆向演示**，不是 shader 项目：
- 文件清单：`AppDelegate.h/.m`、`SceneDelegate.h/.m`、`main.m`、`ViewController.h/.mm`、
  `ConfigurationView.swift`、`CAFilter.h`、`_UIViewGlass.h`、`UIGlassEffect+Glass.h`、`UIView+GlassEffect.h`
- **【未找到】没有任何 `.metal` 文件，也没有任何 Metal/MSL shader 字符串**（已用 glob 全量枚举 + 全文检索确认）。
  所有玻璃效果都由系统私有 API（`UIGlassEffect` + `_UIViewGlass` + 私有 `CAFilter`）实现。

### 8.2 视觉参数清单（**【源码】全部来自 `GE/ConfigurationView.swift` 与 `GE/_UIViewGlass.h`）**

`GE/_UIViewGlass.h:10-28` 的属性表（逐字）：

```objc
@property (nonatomic, readonly) NSInteger variant;
@property (nonatomic, readonly) NSInteger size;
@property (nonatomic, assign) NSInteger identifier;
@property (nonatomic) CGFloat smoothness;
@property (nonatomic, strong) UIColor *tintColor;
@property (nonatomic, strong) UIColor *controlTintColor;
@property (nonatomic, copy) NSString *subvariant;
@property (nonatomic, assign) BOOL contentLensing;
@property (nonatomic, assign) BOOL highlightsDisplayAngle;
@property (nonatomic, assign) BOOL excludingPlatter;
@property (nonatomic, assign) BOOL excludingForeground;
@property (nonatomic, assign) BOOL excludingShadow;
@property (nonatomic, assign) BOOL excludingControlLensing;
@property (nonatomic, assign) BOOL excludingControlDisplacement;
@property (nonatomic, assign) BOOL flexible;
@property (nonatomic, assign) NSInteger _flexVariant;
@property (nonatomic, assign) BOOL boostWhitePoint;
@property (nonatomic, assign) BOOL allowsGrouping;
@property (nonatomic, copy) NSString *backdropGroupName;
```

`GE/ConfigurationView.swift:86-103` 的默认值（逐字）：

```swift
@Observable
final class GlassConfiguration {
    
    var variant: Int = 1
    var size: Int = 0
    
    var isContentLensingEnabled: Bool = false
    var highlightsDisplayAngle: Bool = false
    var excludingPlatter: Bool = false
    var excludingForeground: Bool = false
    var excludingShadow: Bool = false
    var excludingControlLensing: Bool = false
    var excludingControlDisplacement: Bool = false
    var flexible: Bool = false
    var flexVariant: Int = 0
    var boostWhitePoint: Bool = false
    var allowsGrouping: Bool = false
    var smoothness: Double = 0.0
}
```

取值范围（`GE/ConfigurationView.swift:28-70`）：
`variant ∈ 0...10`、`size ∈ 0...5`、`flexVariant ∈ 0...5`、`smoothness ∈ 0...1`（步长 0.01），其余为 `Toggle`。

### 8.3 私有 CAFilter 的输入参数名与取值范围（**最有价值的部分**）

`GE/ConfigurationView.swift:233-252` 逐字：

```swift
    private static let inputs: [any GlassFilterInputItem] = [
        GlassFilterNumberInputItem(key: "inputInnerRefractionAmount", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputInnerRefractionHeight", minimumValue: 0, maximumValue: 100),
        GlassFilterNumberInputItem(key: "inputOuterRefractionAmount", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputOuterRefractionHeight", minimumValue: 0, maximumValue: 100),
        GlassFilterNumberInputItem(key: "inputBleedAmount", minimumValue: 0, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBleedBlurRadius", minimumValue: 0, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBleedOpacity", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurDistance0", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBlurDistance1", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBlurDistance2", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBlurDistance3", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBlurDistance4", minimumValue: -200, maximumValue: 200),
        GlassFilterNumberInputItem(key: "inputBlurOpacity0", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurOpacity1", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurOpacity2", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurOpacity3", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurOpacity4", minimumValue: 0, maximumValue: 1, step: 0.01),
        GlassFilterNumberInputItem(key: "inputBlurRadius", minimumValue: 0, maximumValue: 100),
    ]
```

**【推断】** 这张表揭示了 Apple 玻璃滤镜的结构（数值**不是**源码写的默认值，只是 slider 范围）：
- **折射分内外两侧**：`inputInnerRefractionAmount/Height`、`inputOuterRefractionAmount/Height`
  ⇒ Apple 同时做「向内」和「向外」的折射，Kyant 的实现对应其中的 Inner（见 §5.1，`amount` 取负 = 向内采样）。
  Kyant 没有对应的 Outer refraction。
- **5 段模糊环**：`inputBlurDistance0..4` + `inputBlurOpacity0..4` + `inputBlurRadius`
  ⇒ 一变种「渐进/分层模糊」：5 个不同距离处的模糊强度叠加。
  Kyant 的做法完全不同（单一半径 + 可选 alpha 遮罩，见 §4.3）。
- **Bleed（渗出）三件套**：`inputBleedAmount / inputBleedBlurRadius / inputBleedOpacity`
  ⇒ 玻璃色向外渗出的辉光。Kyant 没有对应实现（**【未找到】**）。

`GE/ConfigurationView.swift:268-312` 的装配代码（逐字，说明参数如何送进系统）：

```swift
    override func updateProperties() {
        glassView?._glassEffect = nil
        let glass: _UIViewGlass = .init(
            variant: configuration.variant,
            size: configuration.size,
            smoothness: configuration.smoothness
        )
        glass.flexible = configuration.flexible
        glass._flexVariant = configuration.flexVariant
        glass.contentLensing = configuration.isContentLensingEnabled
        glass.highlightsDisplayAngle = configuration.highlightsDisplayAngle
        glass.excludingPlatter = configuration.excludingPlatter
        glass.excludingForeground = configuration.excludingForeground
        glass.excludingShadow = configuration.excludingShadow
        glass.excludingControlLensing = configuration.excludingControlLensing
        glass.excludingControlDisplacement = configuration.excludingControlDisplacement
        glass.boostWhitePoint = configuration.boostWhitePoint
        glass.allowsGrouping = configuration.allowsGrouping
        let glassEffect = UIGlassEffect(glass: glass)
        glassView?._glassEffect = glassEffect
    }
```

滤镜的挂载位置（`GE/ConfigurationView.swift:127-135`，逐字）：

```swift
@MainActor
private func backdropLayer(from glassView: UIView) -> CALayer? {
    let multiLayerRoot = glassView.layer.superlayer
    let glassGroupViewLayer = multiLayerRoot?.sublayers?.last?.sublayers?.first
    let materialProviderLayer = glassGroupViewLayer?.sublayers?.first
    let backdropLayer = materialProviderLayer?.sublayers?.first
    
    return backdropLayer
}
```

**【推断】** 即：玻璃滤镜挂在 `glassView.layer.superlayer → sublayers.last → sublayers.first → sublayers.first → sublayers.first`
这五层深度的 `backdropLayer.filters[0]` 上。

### 8.4 宿主视图参数（`GE/ViewController.mm`）

```objc
_glassView = [UIView new];
_glassView.layer.cornerRadius = 30;
_glassView.layer.cornerCurve = kCACornerCurveContinuous;
```

（`GE/ViewController.mm:28-31`）
- **`cornerRadius = 30`，`cornerCurve = kCACornerCurveContinuous`** ⇒ **Apple 的「连续曲率」是系统原生的 `cornerCurve`**，
  这也印证了 `Shapes` 里 G2 圆角的定位（对应 iOS 的 `.continuous`）
- 玻璃尺寸 `CGSizeMake(200, 200)`（`:69`）
- 拖动手势回弹动画：`animateWithSpringDuration:0.3 bounce:0.12 initialSpringVelocity:0`（`:84-88`）
- **【未找到】**：`GE/AppDelegate.m`、`GE/SceneDelegate.m`、`GE/main.m` 中**没有任何** glass/blur/filter 相关代码
  （已全文检索 `filter|Glass|glass|blur|Blur`，零命中）

### 8.5 `GlassExplorer` 的 README（逐字）

```markdown
# GlassExplorer 🔍✨

A fun little iOS app that plays around with UIKit's private glass effect APIs. Basically lets you mess with glass blur effects on views and see what happens.

<div align="center">
    <img src="./Artworks/Screenshot.png" height=400>
</div>

## What it does
- Shows a draggable glass effect overlay
- Lets you tweak glass properties in real-time 
- Uses some spicy private APIs (don't ship this to the App Store lol)
```

（`build/recon/ref_kyant/GlassExplorer.tar/GlassExplorer-main/README.md:1-12`）

---

## 9. 次要任务 8：`DampedDragAnimation` / `InteractiveHighlight`（交互动效）

### 9.1 `DampedDragAnimation.kt` 全文逐字摘录

`APP/utils/DampedDragAnimation.kt:1-137`：

```kotlin
package com.kyant.backdrop.catalog.utils

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.spring
import androidx.compose.foundation.MutatorMutex
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.util.VelocityTracker
import androidx.compose.ui.unit.IntSize
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.filter
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlin.math.abs
import kotlin.time.Clock

class DampedDragAnimation(
    private val animationScope: CoroutineScope,
    val initialValue: Float,
    val valueRange: ClosedRange<Float>,
    val visibilityThreshold: Float,
    val initialScale: Float,
    val pressedScale: Float,
    val onDragStarted: DampedDragAnimation.(position: Offset) -> Unit,
    val onDragStopped: DampedDragAnimation.() -> Unit,
    val onDrag: DampedDragAnimation.(size: IntSize, dragAmount: Offset) -> Unit,
) {

    private val valueAnimationSpec =
        spring(1f, 1000f, visibilityThreshold)
    private val velocityAnimationSpec =
        spring(0.5f, 300f, visibilityThreshold * 10f)
    private val pressProgressAnimationSpec =
        spring(1f, 1000f, 0.001f)
    private val scaleXAnimationSpec =
        spring(0.6f, 250f, 0.001f)
    private val scaleYAnimationSpec =
        spring(0.7f, 250f, 0.001f)

    private val valueAnimation =
        Animatable(initialValue, visibilityThreshold)
    private val velocityAnimation =
        Animatable(0f, 5f)
    private val pressProgressAnimation =
        Animatable(0f, 0.001f)
    private val scaleXAnimation =
        Animatable(initialScale, 0.001f)
    private val scaleYAnimation =
        Animatable(initialScale, 0.001f)

    private val mutatorMutex = MutatorMutex()

    private val velocityTracker = VelocityTracker()

    val value: Float get() = valueAnimation.value
    val progress: Float get() = (value - valueRange.start) / (valueRange.endInclusive - valueRange.start)
    val targetValue: Float get() = valueAnimation.targetValue
    val pressProgress: Float get() = pressProgressAnimation.value
    val scaleX: Float get() = scaleXAnimation.value
    val scaleY: Float get() = scaleYAnimation.value
    val velocity: Float get() = velocityAnimation.value

    val modifier: Modifier = Modifier.pointerInput(Unit) {
        inspectDragGestures(
            onDragStart = { down ->
                onDragStarted(down.position)
                press()
            },
            onDragEnd = {
                onDragStopped()
                release()
            },
            onDragCancel = {
                onDragStopped()
                release()
            }
        ) { change, dragAmount ->
            onDrag(size, dragAmount)
        }
    }

    fun press() {
        velocityTracker.resetTracking()
        animationScope.launch {
            launch { pressProgressAnimation.animateTo(1f, pressProgressAnimationSpec) }
            launch { scaleXAnimation.animateTo(pressedScale, scaleXAnimationSpec) }
            launch { scaleYAnimation.animateTo(pressedScale, scaleYAnimationSpec) }
        }
    }

    fun release() {
        animationScope.launch {
            awaitFrame()
            if (value != targetValue) {
                val threshold = (valueRange.endInclusive - valueRange.start) * 0.025f
                snapshotFlow { valueAnimation.value }
                    .filter { abs(it - valueAnimation.targetValue) < threshold }
                    .first()
            }
            launch { pressProgressAnimation.animateTo(0f, pressProgressAnimationSpec) }
            launch { scaleXAnimation.animateTo(initialScale, scaleXAnimationSpec) }
            launch { scaleYAnimation.animateTo(initialScale, scaleYAnimationSpec) }
        }
    }

    fun updateValue(value: Float) {
        val targetValue = value.coerceIn(valueRange)
        animationScope.launch {
            launch { valueAnimation.animateTo(targetValue, valueAnimationSpec) { updateVelocity() } }
        }
    }

    fun animateToValue(value: Float) {
        animationScope.launch {
            mutatorMutex.mutate {
                press()
                val targetValue = value.coerceIn(valueRange)
                launch { valueAnimation.animateTo(targetValue, valueAnimationSpec) }
                if (velocity != 0f) {
                    launch { velocityAnimation.animateTo(0f, velocityAnimationSpec) }
                }
                release()
            }
        }
    }

    private fun updateVelocity() {
        velocityTracker.addPosition(
            Clock.System.now().toEpochMilliseconds(),
            Offset(value, 0f)
        )
        val targetVelocity = velocityTracker.calculateVelocity().x / (valueRange.endInclusive - valueRange.start)
        animationScope.launch { velocityAnimation.animateTo(targetVelocity, velocityAnimationSpec) }
    }
}
```

**关键参数汇总（【源码】）**

| 用途 | spring 参数（Compose `spring(dampingRatio, stiffness, visibilityThreshold)`） | 行号 |
|---|---|---|
| 值动画（位移） | `spring(1f, 1000f, visibilityThreshold)`（**临界阻尼**，刚度 1000） | `:31-32` |
| 速度动画 | `spring(0.5f, 300f, visibilityThreshold * 10f)` | `:33-34` |
| 按压进度 | `spring(1f, 1000f, 0.001f)` | `:35-36` |
| scaleX | `spring(0.6f, 250f, 0.001f)` | `:37-38` |
| scaleY | `spring(0.7f, 250f, 0.001f)` | `:39-40` |
| 速度跟踪的 Animatable 阈值 | `Animatable(0f, 5f)` | `:44-45` |
| 松手回弹前的等待阈值 | 值域的 **2.5%**：`threshold = (end - start) * 0.025f` | `:97` |

- `progress = (value - start) / (end - start)`（`:58`）
- `press()`：按压进度 → 1，`scaleX/scaleY → pressedScale`（`:84-91`）
- `release()`：先等值动画到位（差值 < 2.5% 值域）再回弹（`:93-106`）——**松手后缩放不会立刻弹回，等位移走完**
- `animateToValue()`：`press()` → 动画 → 速度归零 → `release()`，全程被 `MutatorMutex` 串行化（`:115-127`）
- **手势识别**用的是自己写的 `inspectDragGestures`（`APP/utils/DragGestureInspector.kt:15-40`），
  特点：**没有 touch slop**，按下立刻 `onDragStart` 并触发一次 `onDrag(Offset.Zero)`（`:27-28`），
  支持多指切换（`DragGestureInspector.kt:71-77`）——这也是「玻璃立刻响应」的原因

### 9.2 `InteractiveHighlight.kt` 全文逐字摘录

`APP/utils/InteractiveHighlight.kt:22-121`（含内嵌 AGSL shader）：

```kotlin
class InteractiveHighlight(
    val animationScope: CoroutineScope,
    val position: (size: Size, offset: Offset) -> Offset = { _, offset -> offset }
) {

    private val pressProgressAnimationSpec =
        spring(0.5f, 300f, 0.001f)
    private val positionAnimationSpec =
        spring(0.5f, 300f, Offset.VisibilityThreshold)

    private val pressProgressAnimation =
        Animatable(0f, 0.001f)
    private val positionAnimation =
        Animatable(Offset.Zero, Offset.VectorConverter, Offset.VisibilityThreshold)

    private var startPosition = Offset.Zero
    val pressProgress: Float get() = pressProgressAnimation.value
    val offset: Offset get() = positionAnimation.value - startPosition

    private val shader =
        if (isRuntimeShaderSupported()) {
            RuntimeShader(
                """
uniform float2 size;
layout(color) uniform half4 color;
uniform float radius;
uniform float2 position;

half4 main(float2 coord) {
    float dist = distance(coord, position);
    float intensity = smoothstep(radius, radius * 0.5, dist);
    return color * intensity;
}"""
            )
        } else {
            null
        }

    val modifier: Modifier =
        Modifier.drawWithContent {
            val progress = pressProgressAnimation.value
            if (progress > 0f) {
                if (shader != null) {
                    drawRect(
                        Color.White.copy(0.08f * progress),
                        blendMode = BlendMode.Plus
                    )
                    shader.apply {
                        val position = position(size, positionAnimation.value)
                        setFloatUniform("size", size.width, size.height)
                        setColorUniform("color", Color.White.copy(0.15f * progress))
                        setFloatUniform("radius", size.minDimension * 1.5f)
                        setFloatUniform(
                            "position",
                            position.x.fastCoerceIn(0f, size.width),
                            position.y.fastCoerceIn(0f, size.height)
                        )
                    }
                    drawRect(
                        ShaderBrush(shader.asComposeShader()),
                        blendMode = BlendMode.Plus
                    )
                } else {
                    drawRect(
                        Color.White.copy(0.25f * progress),
                        blendMode = BlendMode.Plus
                    )
                }
            }

            drawContent()
        }

    val gestureModifier: Modifier =
        Modifier.pointerInput(animationScope) {
            inspectDragGestures(
                onDragStart = { down ->
                    startPosition = down.position
                    animationScope.launch {
                        launch { pressProgressAnimation.animateTo(1f, pressProgressAnimationSpec) }
                        launch { positionAnimation.snapTo(startPosition) }
                    }
                },
                onDragEnd = {
                    animationScope.launch {
                        launch { pressProgressAnimation.animateTo(0f, pressProgressAnimationSpec) }
                        launch { positionAnimation.animateTo(startPosition, positionAnimationSpec) }
                    }
                },
                onDragCancel = {
                    animationScope.launch {
                        launch { pressProgressAnimation.animateTo(0f, pressProgressAnimationSpec) }
                        launch { positionAnimation.animateTo(startPosition, positionAnimationSpec) }
                    }
                }
            ) { change, _ ->
                animationScope.launch { positionAnimation.snapTo(change.position) }
            }
        }
}
```

**关键参数（【源码】）**

| 项 | 值 | 行号 |
|---|---|---|
| 按压进度 spring | `spring(0.5f, 300f, 0.001f)` | `:27-28` |
| 位置 spring | `spring(0.5f, 300f, Offset.VisibilityThreshold)` | `:29-30` |
| 半径 | `size.minDimension * 1.5f` | `:73` |
| 底色叠加 | `White @ (0.08 * progress)`，`BlendMode.Plus` | `:65-68` |
| 光斑叠加 | `White @ (0.15 * progress)`，`BlendMode.Plus` | `:72`、`:80-83` |
| 无 shader 降级 | `White @ (0.25 * progress)` | `:85-88` |
| 位置钳制 | `fastCoerceIn(0f, size.width/height)` | `:74-78` |
| 光斑衰减 | `smoothstep(radius, radius * 0.5, dist)` | shader `:52` |

**【推断】** 这是 Kyant 认为玻璃面板应有的交互反馈：
**「按下 → 整块玻璃轻微放大（`pressedScale`）+ 在触点位置出现一圈软光斑 + 高光增强」**，
松手后光斑向起始点收回（`positionAnimation.animateTo(startPosition, ...)`）。

### 9.3 `ProgressConverter.kt`（全文，18 行）

`APP/utils/ProgressConverter.kt:7-17`：

```kotlin
fun interface ProgressConverter {

    fun convert(progress: Float): Float

    companion object {

        val Default: ProgressConverter =
            ProgressConverter { progress ->
                (1f - exp(-abs(progress))) * progress.sign
            }
    }
}
```

公式：`f(x) = (1 - e^{-|x|}) · sign(x)`。用于 ControlCenter 的**超范围拖动阻尼**（`APP/destinations/ControlCenterContent.kt:85-94`）：
`progress < 0` 或 `> 1` 的部分被这个 saturating 函数压缩，产生「拉过头会变沉」的手感。

---

## 10. 次要任务 9：`Liquid*` 组件里玻璃与交互状态的结合

### 10.1 结论概览

四个组件的共同套路：**玻璃参数不是固定的，而是 `pressProgress`（0→1）的线性插值**。
按下去时玻璃「变薄、变亮、变得更凸、并出现色散」。

| 组件 | 形状 | 静止态 | 按压态（`progress = pressProgress`） | 来源 |
|---|---|---|---|---|
| `LiquidButton` | `Capsule()` | `vibrancy(); blur(2dp); lens(12dp, 24dp)` | 玻璃参数**不变**；改的是 `layerBlock` 的缩放/位移 + `InteractiveHighlight` | `APP/components/LiquidButton.kt:55-90` |
| `LiquidSlider` | `Capsule()` | `blur(8dp); lens(0,0)`（progress=0） | `blur(8dp*(1-p)); lens(10dp*p, 14dp*p, chromaticAberration=true)` | `APP/components/LiquidSlider.kt:167-175` |
| `LiquidToggle` | `Capsule()` | 同 Slider | `blur(8dp*(1-p)); lens(5dp*p, 10dp*p, chromaticAberration=true)` | `APP/components/LiquidToggle.kt:158-166` |
| `LiquidBottomTabs` 外栏 | `Capsule()` | `vibrancy(); blur(8dp); lens(24dp,24dp)` | 同上（不变）；缩放 `1 → 1+16dp/width` | `APP/components/LiquidTabs.kt:166-181` |
| `LiquidBottomTabs` 选中块 | `Capsule()` | `lens(0,0)` | `lens(10dp*p, 14dp*p, chromaticAberration=true)` | `APP/components/LiquidTabs.kt:241-251` |

### 10.2 `LiquidBottomTab.kt` 全文（49 行）

`APP/components/LiquidBottomTab.kt:1-49`：

```kotlin
package com.kyant.backdrop.catalog.components

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.Composable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.kyant.shapes.Capsule

internal val LocalLiquidBottomTabScale =
    staticCompositionLocalOf { { 1f } }

@Composable
fun RowScope.LiquidBottomTab(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable ColumnScope.() -> Unit
) {
    val scale = LocalLiquidBottomTabScale.current
    Column(
        modifier
            .clip(Capsule())
            .clickable(
                interactionSource = null,
                indication = null,
                role = Role.Tab,
                onClick = onClick
            )
            .fillMaxHeight()
            .weight(1f)
            .graphicsLayer {
                val scale = scale()
                scaleX = scale
                scaleY = scale
            },
        verticalArrangement = Arrangement.spacedBy(2f.dp, Alignment.CenterVertically),
        horizontalAlignment = Alignment.CenterHorizontally,
        content = content
    )
}
```

**玻璃关系**：`LiquidBottomTab` 自己**完全没有玻璃**（只有 `clip(Capsule())` + 缩放）。
缩放系数由父级通过 `LocalLiquidBottomTabScale`（`:19-20`）注入，父级给的实现是：

```kotlin
CompositionLocalProvider(
    LocalLiquidBottomTabScale provides {
        lerp(1f, 1.2f, dampedDragAnimation.pressProgress)
    }
)
```

（`APP/components/LiquidTabs.kt:190-194`）⇒ **按压时图标/tab 内容放大到 1.2 倍**。

> **【未找到】**：`LiquidBottomTab.kt` / `LiquidTabs.kt` **没有**任何 metaball / 粘连 / 融合实现。
> 选中块与底栏之间的「连接感」来自：① `panelOffset`（最大 4dp，整体平移，`LiquidTabs.kt:88-95`）；
> ② 选中块本身带 `lens + chromaticAberration` 的独立玻璃（`:241-251`）；
> ③ 图标放大 1.2×（`:190-194`）；④ `InteractiveHighlight` 光斑跟随（`:148-159`、`:182`、`:221`）。

### 10.3 `LiquidButton.kt` 全文（122 行）

`APP/components/LiquidButton.kt:35-122`：

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
) {
    val animationScope = rememberCoroutineScope()

    val interactiveHighlight = remember(animationScope) {
        InteractiveHighlight(
            animationScope = animationScope
        )
    }

    Row(
        modifier
            .drawBackdrop(
                backdrop = backdrop,
                shape = { Capsule() },
                effects = {
                    vibrancy()
                    blur(2f.dp.toPx())
                    lens(12f.dp.toPx(), 24f.dp.toPx())
                },
                layerBlock = if (isInteractive) {
                    {
                        val width = size.width
                        val height = size.height

                        val progress = interactiveHighlight.pressProgress
                        val scale = lerp(1f, 1f + 4f.dp.toPx() / size.height, progress)

                        val maxOffset = size.minDimension
                        val initialDerivative = 0.05f
                        val offset = interactiveHighlight.offset
                        translationX = maxOffset * tanh(initialDerivative * offset.x / maxOffset)
                        translationY = maxOffset * tanh(initialDerivative * offset.y / maxOffset)

                        val maxDragScale = 4f.dp.toPx() / size.height
                        val offsetAngle = atan2(offset.y, offset.x)
                        scaleX =
                            scale +
                                    maxDragScale * abs(cos(offsetAngle) * offset.x / size.maxDimension) *
                                    (width / height).fastCoerceAtMost(1f)
                        scaleY =
                            scale +
                                    maxDragScale * abs(sin(offsetAngle) * offset.y / size.maxDimension) *
                                    (height / width).fastCoerceAtMost(1f)
                    }
                } else {
                    null
                },
                onDrawSurface = {
                    if (tint.isSpecified) {
                        drawRect(tint, blendMode = BlendMode.Hue)
                        drawRect(tint.copy(alpha = 0.75f))
                    }
                    if (surfaceColor.isSpecified) {
                        drawRect(surfaceColor)
                    }
                }
            )
            .clickable(
                interactionSource = null,
                indication = if (isInteractive) null else LocalIndication.current,
                role = Role.Button,
                onClick = onClick
            )
            .then(
                if (isInteractive) {
                    Modifier
                        .then(interactiveHighlight.modifier)
                        .then(interactiveHighlight.gestureModifier)
                } else {
                    Modifier
                }
            )
            .height(48f.dp)
            .padding(horizontal = 16f.dp),
        horizontalArrangement = Arrangement.spacedBy(8f.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically,
        content = content
    )
}
```

**交互 → 玻璃的映射公式（源码明确，`:63-90`）**

```
progress   = interactiveHighlight.pressProgress                  // 0..1
scale      = lerp(1, 1 + 4dp/height, progress)                   // 竖直方向 +4dp 的等比放大
maxOffset  = size.minDimension
translationX = maxOffset * tanh(0.05 * offset.x / maxOffset)     // 软饱和的位置跟随
translationY = maxOffset * tanh(0.05 * offset.y / maxOffset)
maxDragScale = 4dp / height
offsetAngle  = atan2(offset.y, offset.x)
scaleX = scale + maxDragScale * |cos(offsetAngle) * offset.x / size.maxDimension| * min(width/height, 1)
scaleY = scale + maxDragScale * |sin(offsetAngle) * offset.y / size.maxDimension| * min(height/width, 1)
```

- **`tanh(0.05*x/maxOffset)`**：手指移动 1:1 跟随的位置被软饱和，位移越大越「拉不动」（`initialDerivative = 0.05f`）
- **`scale` 增量 = `4dp / height`**：绝对增量固定 4dp，与尺寸无关
- **各向异性拉伸**：按拖动方向分解到 X/Y，再乘宽高比补偿（`fastCoerceAtMost(1f)` 防止反向放大）
- 尺寸：`height(48dp)`、`padding(horizontal = 16dp)`（`:116-117`）
- 非交互模式（`isInteractive = false`）用系统 ripple（`indication = LocalIndication.current`，`:103`）
- tint 用 `BlendMode.Hue` 再叠 0.75α（`:93-94`）

### 10.4 `LiquidSlider.kt` 玻璃与状态（关键片段）

`APP/components/LiquidSlider.kt:146-210`：

```kotlin
        Box(
            Modifier
                .graphicsLayer {
                    translationX =
                        (-size.width / 2f + trackWidth * dampedDragAnimation.progress)
                            .fastCoerceIn(-size.width / 4f, trackWidth - size.width * 3f / 4f) * if (isLtr) 1f else -1f
                }
                .then(dampedDragAnimation.modifier)
                .drawBackdrop(
                    backdrop = rememberCombinedBackdrop(
                        backdrop,
                        rememberBackdrop(trackBackdrop) { drawBackdrop ->
                            val progress = dampedDragAnimation.pressProgress
                            val scaleX = lerp(2f / 3f, 1f, progress)
                            val scaleY = lerp(0f, 1f, progress)
                            scale(scaleX, scaleY) {
                                drawBackdrop()
                            }
                        }
                    ),
                    shape = { Capsule() },
                    effects = {
                        val progress = dampedDragAnimation.pressProgress
                        blur(8f.dp.toPx() * (1f - progress))
                        lens(
                            10f.dp.toPx() * progress,
                            14f.dp.toPx() * progress,
                            chromaticAberration = true
                        )
                    },
                    highlight = {
                        val progress = dampedDragAnimation.pressProgress
                        Highlight.Ambient.copy(
                            width = Highlight.Ambient.width / 1.5f,
                            blurRadius = Highlight.Ambient.blurRadius / 1.5f,
                            alpha = progress
                        )
                    },
                    shadow = { Shadow(radius = 4f.dp, color = Color.Black.copy(alpha = 0.05f)) },
                    innerShadow = {
                        val progress = dampedDragAnimation.pressProgress
                        InnerShadow(radius = 4f.dp * progress, alpha = progress)
                    },
                    layerBlock = {
                        scaleX = dampedDragAnimation.scaleX
                        scaleY = dampedDragAnimation.scaleY
                        val velocity = dampedDragAnimation.velocity / 10f
                        scaleX /= 1f - (velocity * 0.75f).fastCoerceIn(-0.2f, 0.2f)
                        scaleY *= 1f - (velocity * 0.25f).fastCoerceIn(-0.2f, 0.2f)
                    },
                    onDrawSurface = {
                        val progress = dampedDragAnimation.pressProgress
                        drawRect(Color.White.copy(alpha = 1f - progress))
                    }
                )
                .size(40f.dp, 24f.dp)
        )
```

（逐字节选自 `APP/components/LiquidSlider.kt:146-210`，仅把 `shadow = {...}` 压成单行以便阅读，其余原样）

**参数（源码明确）**

| 项 | 值 | 行号 |
|---|---|---|
| 轨道高度 | `6dp`；轨道色 浅 `0xFF787878 @0.2` / 深 `0xFF787880 @0.36` | `:127`、`:61-63` |
| 高亮色 | 浅 `0xFF0088FF` / 深 `0xFF0091FF` | `:58-60` |
| 拖动块尺寸 | `40dp × 24dp` | `:209` |
| `pressedScale` | `1.5f` | `:83` |
| 拖动映射 | `delta = (end-start) * (dragAmount.x / trackWidth)` | `:94` |
| 拖动块位置 | `-w/2 + trackWidth*progress`，钳制到 `[-w/4, trackWidth - 3w/4]` | `:149-151` |
| 拖动时轨道放大 | `scaleX = lerp(2/3, 1, p)`，`scaleY = lerp(0, 1, p)` | `:159-160` |
| 速度引起的倾斜 | `velocity = v/10`；`scaleX /= 1 - clamp(0.75v, -0.2, 0.2)`；`scaleY *= 1 - clamp(0.25v, -0.2, 0.2)` | `:200-202` |
| 静止态表面 | `White @ (1 - p)`（松手后白色覆盖完全消失，露出玻璃） | `:205-206` |

### 10.5 `LiquidToggle.kt` 玻璃与状态（关键片段）

`APP/components/LiquidToggle.kt:132-201`：

```kotlin
        Box(
            Modifier
                .graphicsLayer {
                    val fraction = dampedDragAnimation.value
                    val padding = 2f.dp.toPx()
                    translationX =
                        if (isLtr) lerp(padding, padding + dragWidth, fraction)
                        else lerp(-padding, -(padding + dragWidth), fraction)
                }
                .semantics { role = Role.Switch }
                .then(dampedDragAnimation.modifier)
                .drawBackdrop(
                    backdrop = rememberCombinedBackdrop(
                        backdrop,
                        rememberBackdrop(trackBackdrop) { drawBackdrop ->
                            val progress = dampedDragAnimation.pressProgress
                            val scaleX = lerp(2f / 3f, 0.75f, progress)
                            val scaleY = lerp(0f, 0.75f, progress)
                            scale(scaleX, scaleY) { drawBackdrop() }
                        }
                    ),
                    shape = { Capsule() },
                    effects = {
                        val progress = dampedDragAnimation.pressProgress
                        blur(8f.dp.toPx() * (1f - progress))
                        lens(
                            5f.dp.toPx() * progress,
                            10f.dp.toPx() * progress,
                            chromaticAberration = true
                        )
                    },
                    ... // highlight / shadow / innerShadow / layerBlock 与 Slider 同构
                    onDrawSurface = {
                        val progress = dampedDragAnimation.pressProgress
                        drawRect(Color.White.copy(alpha = 1f - progress))
                    }
                )
                .size(40f.dp, 24f.dp)
        )
```

**参数（源码明确）**

| 项 | 值 | 行号 |
|---|---|---|
| 轨道尺寸 | `64dp × 28dp` | `:129` |
| 拖动块尺寸 | `40dp × 24dp` | `:200` |
| 拖动宽度基准 | `dragWidth = 20dp`；`delta = dragAmount.x / dragWidth` | `:64`、`:91-94` |
| 内边距 | `padding = 2dp`；`translationX = lerp(padding, padding+dragWidth, fraction)` | `:136-139` |
| 轨道颜色插值 | `lerp(trackColor, accentColor, fraction)` | `:127` |
| 强调色 | 浅 `0xFF34C759` / 深 `0xFF30D158` | `:55-57` |
| `pressedScale` | `1.5f`；`valueRange = 0f..1f`，`visibilityThreshold = 0.001f` | `:72-75` |
| 超过 50% 判定为开 | `if (targetValue >= 0.5f) 1f else 0f` | `:79` |
| 拖动时轨道放大 | `scaleX = lerp(2/3, 0.75, p)`，`scaleY = lerp(0, 0.75, p)` | `:150-151` |
| 速度项 | `velocity = v/50`（比 Slider 的 `/10` 小 5 倍） | `:191` |

### 10.6 `LiquidBottomTabs.kt` 玻璃与状态（关键片段）

`APP/components/LiquidTabs.kt:83-286`，逐字节选：

```kotlin
        val tabWidth = with(density) {
            (constraints.maxWidth.toFloat() - 8f.dp.toPx()) / tabsCount
        }
```
（`:83-85`）

```kotlin
        val panelOffset by remember(density) {
            derivedStateOf {
                val fraction = (offsetAnimation.value / constraints.maxWidth).fastCoerceIn(-1f, 1f)
                with(density) {
                    4f.dp.toPx() * fraction.sign * EaseOut.transform(abs(fraction))
                }
            }
        }
```
（`:88-95`）

```kotlin
        val dampedDragAnimation = remember(animationScope) {
            DampedDragAnimation(
                animationScope = animationScope,
                initialValue = selectedTabIndex().toFloat(),
                valueRange = 0f..(tabsCount - 1).toFloat(),
                visibilityThreshold = 0.001f,
                initialScale = 1f,
                pressedScale = 78f / 56f,
                ...
```
（`:102-109`，`pressedScale = 78/56 ≈ 1.392857`）

```kotlin
                    animationScope.launch {
                        offsetAnimation.animateTo(
                            0f,
                            spring(1f, 300f, 0.5f)
                        )
                    }
```
（`:115-120`）

```kotlin
        val interactiveHighlight = remember(animationScope) {
            InteractiveHighlight(
                animationScope = animationScope,
                position = { size, offset ->
                    Offset(
                        if (isLtr) (dampedDragAnimation.value + 0.5f) * tabWidth + panelOffset
                        else size.width - (dampedDragAnimation.value + 0.5f) * tabWidth + panelOffset,
                        size.height / 2f
                    )
                }
            )
        }
```
（`:148-159`——**光斑位置被重写成跟随选中项中心 + panelOffset，不是手指位置**）

外栏（`:161-188`）：

```kotlin
        Row(
            Modifier
                .graphicsLayer { translationX = panelOffset }
                .drawBackdrop(
                    backdrop = backdrop,
                    shape = { Capsule() },
                    effects = {
                        vibrancy()
                        blur(8f.dp.toPx())
                        lens(24f.dp.toPx(), 24f.dp.toPx())
                    },
                    layerBlock = {
                        val progress = dampedDragAnimation.pressProgress
                        val scale = lerp(1f, 1f + 16f.dp.toPx() / size.width, progress)
                        scaleX = scale
                        scaleY = scale
                    },
                    onDrawSurface = { drawRect(containerColor) }
                )
                .then(interactiveHighlight.modifier)
                .height(64f.dp)
                .fillMaxWidth()
                .padding(4f.dp),
```

颜色层（`:195-228`）：`.alpha(0f)` 只为录进 `tabsBackdrop`，用 `ColorFilter.tint(accentColor)`（`:225`）染色，
`lens(24dp*p, 24dp*p)` + `Highlight.Default.copy(alpha = p)`。

选中指示块（`:231-286`）：

```kotlin
        Box(
            Modifier
                .padding(horizontal = 4f.dp)
                .graphicsLayer {
                    translationX =
                        if (isLtr) dampedDragAnimation.value * tabWidth + panelOffset
                        else size.width - (dampedDragAnimation.value + 1f) * tabWidth + panelOffset
                }
                ...
                .drawBackdrop(
                    backdrop = rememberCombinedBackdrop(backdrop, tabsBackdrop),
                    shape = { Capsule() },
                    effects = {
                        val progress = dampedDragAnimation.pressProgress
                        lens(10f.dp.toPx() * progress, 14f.dp.toPx() * progress, chromaticAberration = true)
                    },
                    highlight = { Highlight.Default.copy(alpha = dampedDragAnimation.pressProgress) },
                    shadow = { Shadow(alpha = progress) },
                    innerShadow = { InnerShadow(radius = 8f.dp * progress, alpha = progress) },
                    layerBlock = {
                        scaleX = dampedDragAnimation.scaleX
                        scaleY = dampedDragAnimation.scaleY
                        val velocity = dampedDragAnimation.velocity / 10f
                        scaleX /= 1f - (velocity * 0.75f).fastCoerceIn(-0.2f, 0.2f)
                        scaleY *= 1f - (velocity * 0.25f).fastCoerceIn(-0.2f, 0.2f)
                    },
                    onDrawSurface = {
                        val progress = dampedDragAnimation.pressProgress
                        drawRect(
                            if (isLightTheme) Color.Black.copy(0.1f) else Color.White.copy(0.1f),
                            alpha = 1f - progress
                        )
                        drawRect(Color.Black.copy(alpha = 0.03f * progress))
                    }
                )
                .height(56f.dp)
                .fillMaxWidth(1f / tabsCount)
        )
```

尺寸链（源码明确）：外栏 `height(64dp)` + `padding(4dp)`（`:183-185`）；选中块 `height(56dp)`、
宽 = `maxWidth / tabsCount`（`:284-285`）；`tabWidth = (maxWidth - 8dp) / tabsCount`（`:83-85`）。

### 10.7 `GlassPlaygroundContent.kt` 可调参数清单（**这是最好的「参数面板」参考**）

`APP/destinations/GlassPlaygroundContent.kt:54-58` 的状态与 `:65-90` 的应用：

```kotlin
    var cornerRadiusFrac by remember { mutableFloatStateOf(0.5f) }
    var blurRadiusDp by remember { mutableFloatStateOf(0f) }
    var refractionHeightFrac by remember { mutableFloatStateOf(0.2f) }
    var refractionAmountFrac by remember { mutableFloatStateOf(0.2f) }
    var chromaticAberration by remember { mutableFloatStateOf(0f) }
```

```kotlin
                .drawBackdrop(
                    backdrop = backdrop,
                    shape = { RoundedRectangle(256f.dp / 2f * cornerRadiusFrac) },
                    effects = {
                        val minDimension = size.minDimension
                        vibrancy()
                        blur(blurRadiusDp.dp.toPx())
                        lens(
                            refractionHeight = refractionHeightFrac * minDimension * 0.5f,
                            refractionAmount = refractionAmountFrac * minDimension,
                            depthEffect = true,
                            chromaticAberration = chromaticAberration > 0f
                        )
                    },
                    highlight = { Highlight.Plain },
                    layerBlock = { /* 平移 / 缩放 / 旋转 */ }
                )
                .size(256f.dp)
```

| 参数 | 默认 | 范围 | 映射到实际值 | 行号 |
|---|---|---|---|---|
| 圆角 | `cornerRadiusFrac = 0.5f` | 0…1 | `RoundedRectangle(128dp * frac)`，控件 256dp ⇒ 圆角 0…128dp | `:54`、`:67`、`:115` |
| 模糊半径 | `blurRadiusDp = 0f` | 0…32 | `blurRadiusDp.dp.toPx()` | `:55`、`:71`、`:159` |
| 折射高度 | `refractionHeightFrac = 0.2f` | 0…1 | `frac * size.minDimension * 0.5f` | `:56`、`:73`、`:169` |
| 折射量 | `refractionAmountFrac = 0.2f` | 0…1 | `frac * size.minDimension` | `:57`、`:74`、`:179` |
| 色散 | `chromaticAberration = 0f` | 0…1 | `> 0f` 即切换 shader | `:58`、`:76`、`:189` |
| 高光 | `Highlight.Plain`（固定） | – | – | `:79` |
| 控件尺寸 | `256dp` | – | – | `:115` |
| Reset 恢复值 | `0.5 / 0 / 0.2 / 0.2 / 0` | – | – | `:221-225` |

### 10.8 `AdaptiveLuminanceGlassContent.kt` 可调参数

见 §7.2 表格（`APP/destinations/AdaptiveLuminanceGlassContent.kt:94-108`）。
另：`lens(24f.dp.toPx(), size.minDimension / 2f, depthEffect = true)`（`:108`），
`highlight = { Highlight.Plain }`（`:110`），形状 `RoundedRectangle(24f.dp)`（`:92`），控件 `160dp`（`:151`）。

---

## 11. 与其他参考实现的对照（可选阅读）

工作区里另有一份第三方 GLES/Vulkan 玻璃实现：
`build/reference/imgui-liquid-glass/src/ui/widget/effects/glass/GlassStyle.hpp:44`（`float refractionHeight = 20.0f;`）、
`.../backend/opengl/OpenGLGlassBackend.cpp:197`、`.../backend/vulkan/VulkanGlassBackend.cpp:258`。
它的 `refractionHeight` 与 Kyant 的 `refractionHeight` 同名但默认值不同（20 vs 48dp），
参数含义是否一致**未验证**，本文不据此下结论。

本工作区已有的 Kyant 圆角移植：
- `build/recon/kyant_corner_check.cpp:1-12`（注释明确写了来源是 `ContinuousCurvatureRoundedRectangleCornerBuilder.kt` + `RoundedRectangleOutline.kt`）
- `build/recon/ccrr_path_fill.cpp:51`、`:112`
这两个文件是**轮廓**移植，不含 SDF。

---

## 12. 复刻要点与差异清单

### 12.1 AGSL → GLSL ES 300 映射表

| AGSL（`SDF-COMMON` / `SHADERS`） | GLSL ES 300 写法 | 说明 |
|---|---|---|
| `uniform shader content;` | `uniform sampler2D content;` | `createRuntimeShaderEffect(shader, "content")` 注入的是上一层结果 |
| `uniform shader sdfTex;` + `setInputBuffer` | `uniform sampler2D sdfTex;` | Android 用 `BitmapShader`（`SDF-ANDROID:20-21`） |
| `content.eval(coord)` | `texture(content, coord / size)` | **坐标是像素，必须自己除以 size** |
| `sdfTex.eval(p)` | `texture(sdfTex, p / sdfTexSize)` | 同上；AGSL 的 `.eval` 收像素坐标 |
| `half4 main(float2 coord)` | `void main()` + `out vec4 fragColor;`，`coord = gl_FragCoord.xy` | **注意 `gl_FragCoord` 原点在左下**，Compose 的 y 向下 |
| `half4` / `half2` | `vec4` / `vec2`（或 `mediump`） | |
| `layout(color) uniform half4 c;` + `setColorUniform` | `uniform vec4 c;`（**自己转换 color space**） | `setColorUniform` 会做 sRGB→linear 处理，移植时要决定是否跟随 |
| `float2 / float4` | `vec2 / vec4` | |
| `sign / step / smoothstep / clamp / length / normalize` | 同名 | 一致 |
| `sampler` 采样边界 | 自己处理 `CLAMP_TO_EDGE` + 手写边界判断 | `SDF-COMMON:51-53` |

### 12.2 移植时必须注意的 8 个坑

1. **AGSL 的 `eval` 是像素坐标**，不是归一化 UV。移植后若忘记除 `size`，折射会完全错乱。
2. **`refractionAmount` 传负值**（`BK/effects/Lens.kt:49`）。GLSL 里若直接传正值，采样方向会朝外（缩小而非放大）。
3. **`circleMap(1 - t)` 而不是 `circleMap(t)`**：位移在**边界最大**、向内衰减到 0。
   写成 `circleMap(t)` 会变成中心位移最大，是完全相反的效果。
4. **`gradRadius = min(radius*1.5, min(halfSize.x, halfSize.y))`**：梯度用的半径**不是**绘制用的半径，
   少了这行 Capsule（半径 = 半高）在角上会出问题（`SHADERS:79`、`:116`、`:175`、`:197`）。
5. **早退分支要保留**：`if (-sd >= refractionHeight) return content.eval(coord);`（`SHADERS:73-75`）——
   省掉它会让面板中心区域也做一次采样，白白浪费带宽。
6. **`sd = min(sd, 0.0)`**（`SHADERS:76`）：边界外的值必须钳到 0，否则 `1 - (-sd)/h` 会 > 1 让 `circleMap` 出 NaN。
7. **SDF 贴图路径里 `sd` 的符号约定与解析路径相反**：
   贴图里 `sd = 2R-1 ∈ [-1, 0]`（**内部为负**，`SDF-COMMON:55`），
   解析路径里 `sdRoundedRect` 也是内部为负（`SHADERS:72`）——两者一致 ✔。
   但贴图路径用 `-sd * 1.5` 做 `min(1.0, ...)` 钳制（`SDF-COMMON:65`），
   解析路径用 `-sd / refractionHeight`（`SHADERS:78`）。**两者的「带宽度量」完全不同**，不要混用。
8. **高光描边宽度是 `2 * ceil(width_px)`**（`BK/highlight/HighlightModifier.kt:153`），
   而描边线宽一半在轮廓外会被裁掉 ⇒ 实际可见宽度 ≈ `ceil(width_px)`。
   直接照抄 `strokeWidth = width` 会让高光细一半。

### 12.3 推荐的最小可复刻参数集（来自 Kyant 自己的默认值）

| 参数 | 推荐值 | 依据 |
|---|---|---|
| 形状 | `Capsule` 或 `RoundedRectangle(16~32dp, style = Continuous)` | `SHAPES/RoundedRectangle.kt:14`（默认 `RoundedCornerStyle.Continuous`） |
| 颜色 | `vibrancy()` = `colorControls(saturation = 1.5f)` | `BK/effects/ColorFilter.kt:41` |
| 模糊 | `2~8dp`（按压时线性降到 0） | `LiquidButton.kt:60`、`LiquidSlider.kt:169` |
| 折射高度 | `12~24dp` | `LiquidButton.kt:61`、`LiquidTabs.kt:172` |
| 折射量 | `2.0 × 折射高度` | `LiquidButton.kt:61`(12/24)、`LiquidTabs.kt:172`(24/24)、`DialogContent.kt:67`(24/48) |
| depthEffect | 大面板 true，小控件 false | `DialogContent.kt:67` vs `LiquidButton.kt:61` |
| 色散 | 只在拖动块/放大镜上开 | `LiquidSlider.kt:173`、`MagnifierContent.kt:91` |
| 高光 | `Highlight.Default`（`White α=0.5`, `BlendMode.Plus`, `angle=45°`, `falloff=1`, `width=0.5dp`） | `BK/highlight/HighlightStyle.kt:45-50`、`BK/highlight/Highlight.kt:11-12` |
| 环境高光 | `Highlight.Ambient`（`White α=0.38`, SrcOver），按压时 `width/1.5`、`blurRadius/1.5`、`alpha=pressProgress` | `BK/highlight/HighlightStyle.kt:74-78`、`LiquidSlider.kt:178-182` |
| 外阴影 | `Shadow(radius=4dp, Black α=0.05)` | `LiquidSlider.kt:185-188` |
| 内阴影 | `InnerShadow(radius=4dp*progress, alpha=progress)` | `LiquidSlider.kt:190-196` |
| 按压放大 | `1 → 1 + 4dp/height`（Button）/ `1 → 1.5`（Slider/Toggle 的 `pressedScale`）/ `1 → 78/56`（Tabs） | `LiquidButton.kt:69`、`LiquidSlider.kt:83`、`LiquidTabs.kt:109` |
| 值动画 spring | `spring(damping=1.0, stiffness=1000)` | `DampedDragAnimation.kt:31-32` |
| 缩放 spring | `spring(0.6, 250)` / `spring(0.7, 250)`（X/Y 不同） | `DampedDragAnimation.kt:37-40` |
| 高光 spring | `spring(0.5, 300)` | `InteractiveHighlight.kt:27-28` |
| 松手判定阈值 | 值域的 2.5% | `DampedDragAnimation.kt:97` |

---

## 13. 「未找到」清单（明确不编造）

| 问题 | 结论 |
|---|---|
| 连续曲率圆角的**解析 SDF** | **未找到**。Kyant 只用圆角矩形 SDF，G2 只用于画轮廓。 |
| 把 10 个 Bézier 控制点转成距离场的代码 | **未找到**。仓库内不存在。 |
| `clock_sdf.webp` 的生成器/脚本 | **未找到**。仓库里只有成品贴图。 |
| 模糊的 sigma / taps / 降采样倍率 | **未找到**。`BlurEffect` 交给平台实现（`BK/effects/Blur.kt:22-28`）。 |
| 玻璃形态融合（metaball / gooey / smooth-min） | **未找到**。全仓库零命中。 |
| Apple 玻璃滤镜各 input 的**默认值** | **未找到**。`GE/ConfigurationView.swift:233-252` 只给了 slider 范围，没给默认值。 |
| GlassExplorer 里的 Metal shader | **未找到**。项目里没有任何 `.metal` 文件或 MSL 字符串。 |
| Kyant 库里的 Outer refraction / Bleed | **未找到**。只有向内折射（Apple 有 `inputOuterRefraction*` 与 `inputBleed*`）。 |
| `S=108` 是常数还是按形状归一化 | **不确定**。只有一张 SDF 资产，无法区分。 |
| `depthEffect` 的设计意图注释 | **未找到**。源码无注释，§5.1 的解释标为【推断】。 |
| `RoundedCornerStyle` 的枚举定义 | 文件存在（`SHAPES/RoundedCornerStyle.kt`）但本次未展开；`RoundedRectangle` 默认 `Continuous`（`SHAPES/RoundedRectangle.kt:14`）。 |

---

## 14. 主要源码文件索引（便于复查）

| 文件 | 内容 |
|---|---|
| `SDF-COMMON` | SDF 贴图 shader 全文（AGSL），`refractionHeight=48dp`、`lightAngle=45f` 默认值 |
| `SDF-ANDROID` / `SDF-SKIKO` | 平台绑定：`BitmapShader` / `makeShader()`，uniform 设置 |
| `SHADERS` | `RoundedRectSDF` + 4 个 shader（折射、折射+色散、Default 高光、Ambient 高光） |
| `BK/effects/Lens.kt` | `lens()` 参数装配、`refractionAmount` 取负、形状支持检查 |
| `BK/effects/Blur.kt` / `ColorFilter.kt` | 模糊、颜色矩阵（0.213/0.715/0.072） |
| `BK/effects/RenderEffect.kt` / `BK-ANDROID/internal/RenderEffect.kt` | effect 链的拼接顺序 |
| `BK/DrawBackdropModifier.kt` | 离屏层 + padding + 绘制顺序 + RenderEffect 挂载 |
| `BK/highlight/*` | 高光样式、描边宽度 ×2、模糊、混合模式 |
| `BK/shadow/*` | 外阴影 / 内阴影（Clear 遮罩） |
| `SHAPES/ContinuousCurvatureRoundedRectangleCornerBuilder.kt` | G2 圆角的 20-double（10 点）解析解 |
| `SHAPES/RoundedRectangleOutline.kt` | 10 点 → 3 段 cubic 的路径构造 + `tW/tH` |
| `APP/components/Liquid*.kt` | 玻璃 × 交互状态的 4 个范例组件 |
| `APP/utils/DampedDragAnimation.kt` / `InteractiveHighlight.kt` | 阻尼拖动、弹簧参数、按压光斑 |
| `APP/destinations/GlassPlaygroundContent.kt` | 全部可调参数与默认值 |
| `APP/destinations/ControlCenterContent.kt` | 多面板共享 backdrop、重力高光 |
| `GE/ConfigurationView.swift` | Apple 私有 CAFilter 的 18 个 input 名与范围 |

---

*本文档由源码逐行核对生成；所有【实测】结论来自 `clock_sdf.webp` 的 VP8L 无损解码（探针脚本：`build/recon/sdf_probe.py` ~ `sdf_probe6.py`）。*
