# 连续曲率圆角（Apple squircle）Kotlin → C++ 移植数值等价性审计

- 审计执行：2026-10-12 本机实测（环境见 §5.2）
- 被审对象：`build/recon/kyant_corner_check.cpp`（SHA256 `8B22F9111746311354090A9CF61B67C259919B551DA42270951C1D66C571384B`）
- 权威参考：`build/recon/ref_kyant/Shapes.tar/Shapes-master/shapes/src/commonMain/kotlin/com/kyant/shapes/ContinuousCurvatureRoundedRectangleCornerBuilder.kt`（SHA256 `D9CD59A1B563B55E690CB71CE0971E3DB07CED3BBBF04C3CA8054C85EFC75C74`）
- 调用方参考：同目录 `RoundedRectangleOutline.kt`

> **标注约定**
> - **【已核实】**＝通过逐行文本对照或实机逐位比对确认的事实。
> - **【推断】**＝由结构/数学论证得出，且有数值佐证，但没有形式化证明。

---

## 0. 结论速览

| 审计项 | 结论 |
|---|---|
| `solveCubicSingle`（C++ L31-38 vs Kotlin L150-156） | **【已核实】逐字符一致**（仅标识符同名，运算符、括号、结合顺序、常量全同） |
| `solveDepressedQuarticSingle`（C++ L41-53 vs Kotlin L158-170） | **【已核实】一致**；Kotlin 里 `val r` 遮蔽形参 `r` 的陷阱被正确复现为 `rr` |
| 构造函数 `k0/k1/k2/k3`（C++ L68-79 vs Kotlin L24-33） | **【已核实】四个表达式逐项一致**（含 `27.0*(...)*cot` 的左结合顺序） |
| `even()`（C++ L91-120 vs Kotlin L69-98） | **【已核实】一致** |
| `uneven()`（C++ L122-165 vs Kotlin L100-142） | **【已核实】一致** |
| 派发与缓存（C++ L83-86 vs Kotlin L35-67） | 数值**行为等价**；C++ 去掉了 memo 缓存（非数值差异，见 §2.7） |
| **tW/tH 方向是否被交换** | **【已核实】没有交换。** 第一个实参（调用方的 `tW`）驱动 `kH` → **水平/x 方向**，与几何自洽性一致（§3.2 三条证据） |
| `even(t)` 是否等价于 `uneven(t,t)` | **数学上等价【推断】，逐位不等价【已核实】**（差 ≤1.22e-15，20 个值里 2~4 个位模式不同）→ `tW==tH → even` 的派发**必须保留** |
| 20 个 double 与调用方索引 | **【已核实】完全自洽**（10 个点、3 段三次贝塞尔、P3/P6 为共享端点） |
| 端到端逐位比对（Java 重写参照） | **1138 个算法量全部逐位相同，0 差异**（-O2 / -O0 / `-march=native -ffp-contract=off`） |
| 潜在缺陷 | **算法本身未发现数值缺陷**；风险全部来自**编译开关**（FMA 收缩 / -Ofast）、**跨 libm 契约**、**参数命名歧义**、**现有自检覆盖不足** |

---

## 1. 审计范围与方法

### 1.1 三路独立证据

1. **源码级逐行对照**：把 Kotlin 的每个表达式与 C++ 对应表达式按"逐个系数、逐个运算符、逐个括号、结合顺序"对齐比较（§2）。
2. **可执行参照物逐位比对**：本机没有 kotlinc/gradle，因此把 Kotlin **逐行重写成 Java**（`audit_corner/CornerRef.java`），与被审 C++ 在同一组输入下逐位比较（§5）。
3. **编译开关敏感性**：同一份 C++ 在 `-O0/-O2/-march=native/-ffp-contract=off/-Ofast` 下分别与参照比对，量化"编译器把结果改掉"的风险（§5.6）。

### 1.2 为什么这套方法能证明"数值等价"（而不是只证明"看起来对"）

- 被审 C++ 与 Java 参照的**算术算子序列**都被逐行核对为同一串 IEEE754 双精度运算；
- 只要这一串算子的输入逐位相同，输出就必然逐位相同（IEEE754 的 `+ - * /` 与 `sqrt` 都是确定性舍入）；
- 唯一可能引入分歧的输入是 6 个超越函数调用点：`sqrt`（IEEE 强制正确舍入，两语言必然一致）、`cbrt`、`acos`、`cos`、`sin`、`tan`。后 5 个的实参在每一档运行里都被打印出来（`ctor.cos/sin/cot`、`case.*.kappa_*`、`lambda.*`），因此"分歧点在哪"是可定位的。
- 结论因此可以精确表述为：**在本次实测的 20 个用例与 11 个对称性用例上，两个实现的全部中间量与输出逐位相同**；而不是泛泛的"误差小于 1e-12"。

### 1.3 关键审计技巧：探针直接包含被审文件

`audit_corner/corner_probe.cpp` 用

```cpp
#define private public
#define main audited_original_main
#include "../kyant_corner_check.cpp"
#undef main
#undef private
```

直接 `#include` **被审文件本体**，并通过 `private → public` 读取 `k0_/k1_/k2_/k3_/c_/s_/cot_/…`、直接调用私有的 `even()`/`uneven()`。
好处：探针执行的是**被审代码的二进制本体**，不存在"审计者抄了一份副本、副本抄对了但原件抄错了"的漏洞。

---

## 2. 逐函数逐系数核对

### 2.1 常量与构造期派生量 —— 一致

| 量 | Kotlin | C++ | 结论 |
|---|---|---|---|
| `SQRT_2` | `1.4142135623730951` (L172) | `kSqrt2` (L25) | 一致 |
| `FRAC_PI_4` | `0.7853981633974483` (L173) | `kFracPi4` (L27) | 一致 |
| `FRAC_1_SQRT_2` | `0.7071067811865476` (L174) | `kFrac1Sqrt2` (L28) | 一致 |
| `theta` | `(1.0 - arcFraction) * FRAC_PI_4` (L15) | 同式 (L59) | 一致（`arcFraction` 默认 0.5，两侧相同） |
| `cos/sin/cot` | L16-18 | L60-62 | 一致（`cot = 1.0/tan(theta)`） |
| `cos2/sin2/cos3/sin3` | `cos*cos`、`sin*sin`、`cos2*cos`、`sin2*sin` (L19-22) | 同式 (L63-66) | 一致 |

补充说明：Kotlin 用 `private val cos = cos(theta)`，属性名与 `kotlin.math.cos` 同名，初始化式里解析到的是**函数**（属性是 Double，不可调用）；C++ 用 `c_`/`s_` 无歧义。两侧语义相同，且 `k0..k3` 用的是**派生量**而不是重新调用三角函数 —— C++ 同样如此。【已核实】

### 2.2 `k0/k1/k2/k3` —— 四个表达式全部一致

- **k0**：Kotlin L24-26 ↔ C++ L68-71。逐项核对：`(SQRT_2 - 6.0*cos + 6.0*SQRT_2*cos2 - 4.0*cos3)*cot`、`2.0*sin*(-9.0 + 2.0*(SQRT_2-2.0*sin)*sin3 + 2.0*SQRT_2*cos*(9.0+sin2) - 2.0*cos2*(9.0+2.0*sin2))`，包括最外层 `27.0 * (…) * cot` 的**左结合顺序**都相同。
- **k1**：Kotlin L27-29 ↔ C++ L72-76。`-81.0*(…)*cot - 4.0*sin*(…)`、`4.0*(-1.0+SQRT_2)*cos`、`(-2.0+SQRT_2)*cos*(9.0+sin2)` 全部同序同号。
- **k2**：Kotlin L30-31 ↔ C++ L77-78。`9.0*(9.0*(-4.0+3.0*SQRT_2+(-6.0+4.0*SQRT_2)*cos)*cot + (-6.0+4.0*SQRT_2)*sin)`，一致。
- **k3**：Kotlin L32-33 ↔ C++ L79。`27.0*(10.0-7.0*SQRT_2)*cot`，一致。
- 实机佐证：`ctor.k0..k3` 的位模式两侧完全相同（§5.3，17/17 项一致）。

【已核实】无任何系数被抄错、无括号位移、无符号错。注：`k0..k3` 的量级约 `k0≈-3.3374`、`k2≈-15.7279`，若任一系数差 1 位（如 `27→27.0000001`），κ 会出现 ~1e-7 级偏差，本次 0 差异可直接排除这类错误。

### 2.3 `solveCubicSingle` —— 一致

Kotlin L151-155 ↔ C++ L32-37，逐字符对应：

```text
f = ((3.0*c/a) - (b*b)/(a*a)) / 3.0                                 两侧同
g = ((2.0*b*b*b)/(a*a*a) - (9.0*b*c)/(a*a) + (27.0*d)/a) / 27.0     两侧同
h = g*g/4.0 + f*f*f/27.0                                            两侧同
return cbrt(-g/2.0 + sqrtH) + cbrt(-g/2.0 - sqrtH) - b/(3.0*a)      两侧同
```

`2.0*b*b*b`、`9.0*b*c` 在两种语言里都是**左结合**（`((2.0*b)*b)*b`、`((9.0*b)*c)`），不存在结合顺序差异。
唯一语义注意点：`kotlin.math.cbrt`（JVM 上即 `java.lang.Math.cbrt`）与 C 运行库 `cbrt` 都是**实数立方根**（负数输出负数），语义一致；但两者**都不是** IEEE 正确舍入函数，跨平台有 1 ulp 级差异（§5.5）。【已核实】

### 2.4 `solveDepressedQuarticSingle` —— 一致，且正确复现了变量遮蔽陷阱

Kotlin L158-170 ↔ C++ L41-53。**本函数是全文件最容易埋暗雷的地方**：

```kotlin
private fun solveDepressedQuarticSingle(p: Double, q: Double, r: Double): Double {
    val b = -p / 2.0
    val c = -r
    val d = r * p / 2.0 - q * q / 8.0        // ← 这里的 r 仍是【形参】
    ...
    val r = sqrt(-f * f * f / 27.0)          // ← 同名局部 val，遮蔽形参 r（Kotlin 允许）
    val phi = acos(-g / (2.0 * r))           // ← 这里的 r 是【新局部 r = sqrt(-f³/27)】
```

Kotlin 的局部 `val r` 从声明点起遮蔽形参。C++ 把它重命名为 `rr`（L47），并让 `phi` 使用 `2.0 * rr`（L48），`d`（L44）仍使用形参 `r` —— **遮蔽的前后语义都被正确保留**。
如果移植者"顺手改名"成 `val sqrtTerm = sqrt(...)` 而让 `phi` 继续用形参 `r`，κ/λ 会整条错下去，而端点切线与单调性检查**仍然会通过**（因为它们只约束端点，不约束中点）。本次核对：**没有犯这个错**。【已核实】

其余逐项：`f = ((3.0*c) - (b*b))/3.0`、`g = ((2.0*b*b*b) - (9.0*b*c) + (27.0*d))/27.0`、`y = 2.0*sqrt(-f/3.0)*cos(phi/3.0)`、`z = y - b/3.0`、`u = sqrt(2.0*z - p)`、`return (u - sqrt(u*u - 4.0*(z + q/(2.0*u))))/2.0` —— 与 C++ L45-52 逐字符一致（含 `-f*f*f/27.0` 的 `((-f)*f)*f` 解析）。【已核实】

### 2.5 `buildEvenCornerBezierPoints` vs `even()` —— 一致

Kotlin L69-98 ↔ C++ L91-120：

| 量 | 表达式 | 结论 |
|---|---|---|
| `k` | `extendedFraction * t` | 一致 |
| `kappa` | `solveCubicSingle(k3, k2, k1 + 8.0*(-k)*sin3*sin, k0)` | 一致（`8.0*(-k)` 先算，再乘 `sin3`、`sin`） |
| `x3` | `FRAC_1_SQRT_2 + (-FRAC_1_SQRT_2 + sin)/kappa` | 一致 |
| `y3` | `1.0 - FRAC_1_SQRT_2 + (FRAC_1_SQRT_2 - cos)/kappa` | 一致（注意是 `1.0 - F + (F-cos)/κ` 而非 `1.0 - (F + …)`） |
| `x2` | `x3 - y3*cot` | 一致 |
| `x1` | `x2 - 1.5*kappa*y3*y3/sin3` | 一致（左结合 `((1.5*κ)*y3)*y3`，再 `/sin3`） |
| `x0` | `-k` | 一致 |
| `x6,y6,y7,y8,y9` | `1.0-y3, 1.0-x3, 1.0-x2, 1.0-x1, 1.0-x0` | 一致（镜像反射） |
| `a,g,x36,y36,c` | `1.5*kappa`、`cos2-sin2`、`x6-x3`、`y6-y3`、`-(cos*y36 - sin*x36)` | 一致 |
| `lambda` | `(-g + sqrt(g*g - 4.0*a*c)) / (2.0*a)` | 一致（取 **+** 根） |
| `x4,y4,x5,y5` | `x3+λ*cos`、`y3+λ*sin`、`x6-λ*sin`、`y6-λ*cos` | 一致 |
| 返回数组 | `{x0,0,x1,0,x2,0,x3,y3,x4,y4,x5,y5,x6,y6,1,y7,1,y8,1,y9}` | 逐元素一致 |

实机佐证：`evq.t=*.even.p00..p19` 共 220 个值，两侧位模式全同。

### 2.6 `buildUnevenCornerBezierPoints` vs `uneven()` —— 一致

Kotlin L100-142 ↔ C++ L122-165：

| 量 | 表达式 | 结论 |
|---|---|---|
| `kH,kV` | `extendedFraction * tH` / `* tV` | 一致 |
| `kappa3,kappa6` | 分别用 `(-kH)`、`(-kV)` 代入 `k1 + 8.0*(-k)*sin3*sin` | 一致 |
| `x3,y3,x2,x1,x0` | 与 even 同式但用 `kappa3`；`x0 = -kH` | 一致 |
| `x3p,y3p,x2p,x1p,x0p` | 用 `kappa6`；`x0p = -kV` | 一致 |
| `x6,y6,y7,y8,y9` | `1.0-y3p, 1.0-x3p, 1.0-x2p, 1.0-x1p, 1.0-x0p` | 一致（注意 x6 由 **y3p** 得到） |
| `a,b` | `1.5*kappa3`、`1.5*kappa6` | 一致 |
| `c,d` | `-(cos*y36 - sin*x36)`、`sin*y36 - cos*x36` | 一致（**d 没有外层负号**，与 c 不同，C++ 也对） |
| `p,q,r` | `2.0*(d/b)`、`g*g*g/(a*b*b)`、`(a*d*d + c*g*g)/(a*b*b)` | 一致（分母都是 `(a*b*b)`，即 `(a*b)*b` 左结合） |
| `lambda6` | `solveDepressedQuarticSingle(p,q,r)` | 一致 |
| `lambda3` | `(-d - b*lambda6*lambda6)/g` | 一致（**分子是 `-d - bλ₆²`**，符号关键） |
| `x4,y4,x5,y5` | `x3+λ3*cos`、`y3+λ3*sin`、`x6-λ6*sin`、`y6-λ6*cos` | 一致（**λ3 与 λ6 分属两个不同点**，未串用） |
| 返回数组 | 同 even | 一致 |

实机佐证：20 个用例中 **16 个走 uneven 分支**（`arg1 != arg2`），另 4 个（`p_1_1`、`p_0_0`、`p_half_half`、`p_sqrt2`）因 `tW == tH` 走 even/缓存路径 —— 其中 `p_1_0`、`p_0_1` 正好命中 Kotlin 的 `cache[1][0]`、`cache[0][1]` 两个缓存项，用于验证"缓存值 == 直算值"。
`case.*.p00..p19` 共 **400 项**（20 用例 × 20 个控制点分量，另有用例级 120 项实参/k/κ，合计 520 项）全部逐位相同；其中 `case.*.kappa_from_arg1/2`（走 `cbrt` 路径）也全部逐位相同 —— 说明 `uneven()` 里**没有任何系数抄错**。【已核实】

### 2.7 派发与缓存 —— 数值行为等价，有一处非数值差异

| 项 | Kotlin | C++ | 结论 |
|---|---|---|---|
| 缓存 | `cache = [[even(0), uneven(0,1)], [uneven(1,0), even(1)]]`（L35-45） | **无缓存**（L83-86 每次现算） | 数值等价（同一输入→同一输出）；**非数值差异** |
| 取值 | `when(tH){0→0; 1→1; else→buildCornerBezierPoints}`，`tV` 同理（L47-59） | `if (tW==tH) even(tW); else uneven(tW,tH)` | 等价（见下） |
| 分派 | `buildCornerBezierPoints`: `tH==tV → even(tH)`，否则 `uneven`（L61-67） | 同 | 一致 |

逐一验证缓存与直算的一致性（每个 key 都查过）：

| (tH,tV) | Kotlin 走哪条 | C++ 走哪条 | 是否同值 |
|---|---|---|---|
| (0,0) | `cache[0][0] = even(0)` | `tW==tH → even(0)` | 是 |
| (1,1) | `cache[1][1] = even(1)` | `even(1)` | 是 |
| (0,1) | `cache[0][1] = uneven(0,1)` | `uneven(0,1)` | 是 |
| (1,0) | `cache[1][0] = uneven(1,0)` | `uneven(1,0)` | 是 |
| (0, 0.5) | i=0, j≠0/1 → `uneven(0,0.5)`（**不走缓存**） | `uneven(0,0.5)` | 是（用例 `p_00_05` 实测） |
| (0.5,0.5) | `even(0.5)` | `even(0.5)` | 是 |

- 缓存带来的唯一可观察差异：Kotlin 对 4 个缓存 key **返回同一个数组实例**（调用方若原地修改会污染缓存）；C++ 每次返回新 vector，更安全。Kotlin 的调用方未修改，故实际无差异。
- 另一处只影响可读性：Kotlin 默认参数 `(tH=1.0, tV=1.0)`，C++ `points(tW=1.0, tH=1.0)` 默认值相同。【已核实】

### 2.8 差异汇总

被审文件与 Kotlin 原版**不存在任何数值差异**。非数值差异仅两条：

1. `points()` 去掉了 4 项 memo 缓存（L83-86）——不影响输出。
2. C++ 把形参名从 Kotlin 的 `tH/tV` 改成了 `tW/tH`（L83，`uneven` 内部仍叫 `tH/tV`，L122）——**命名不一致本身是风险点**，见 §3.2。

---

## 3. 参数语义

### 3.1 Kotlin 的形参名 `tH/tV` 是**误导性命名**

`getCornerBezierPoints(tH: Double, tV: Double)`（L47）里，按字母直觉应读作 "t Horizontal / t Vertical"，但实测语义是：

- `kH = extendedFraction * tH` → `kappa3` → 产出 `x0,x1,x2,x3,y3` 与 `P4`；
- `kV = extendedFraction * tV` → `kappa6` → 产出 `x0p..x3p` 及镜像的 `x6,y6,y7,y8,y9` 与 `P5`。

也就是说 **`tH` 其实对应"水平方向的余量"、`tV` 对应"竖直方向的余量"**；而调用方 `RoundedRectangleOutline.kt` 里的局部变量 `tW/tH` 是 **t-Width / t-Height**（宽度/高度导出）。两套命名在同一个调用点相撞：`getCornerBezierPoints(tW, tH)` —— **形参 `tH` 收到了实参 `tW`**。这正是本次怀疑的源头。

### 3.2 第一个实参 `tW` 对应内部的 `tH` 还是 `tV`？—— 明确结论：对应 `tH`（即 `kH`、水平/x 方向），**没有交换**

**证据一（语法，确定无疑）**：`RoundedRectangleOutline.kt` L108 / L210 / L235 / L260 / L285 都是 `cornerBuilder.getCornerBezierPoints(tW, tH)`，**没有使用命名实参**（Kotlin 若写 `getCornerBezierPoints(tV = tH, …)` 才叫命名传参）。位置传参 ⇒ 形参 `tH` := 实参 `tW`。这是**唯一**可能的绑定方式。【已核实】

**证据二（结构/公式，可读代码直接得出）**：
- 输出点 `P0=(x0,0)`、`P1=(x1,0)`、`P2=(x2,0)` 的 y 全为 0 ⇒ 曲线起点落在**顶边**、起始切线**水平**；
- `P7=(1,y7)`、`P8=(1,y8)`、`P9=(1,y9)` 的 x 全为 1 ⇒ 终点落在**右边**、终止切线**竖直**；
- `x0 = -kH` 是**起点的 x 坐标**，`y9 = 1 - x0p = 1 + kV` 是**终点的 y 坐标**；
- 因此 `kappa3`（由 `kH` 决定）那一族管的是"沿顶边能伸出多远"＝**水平占位**，`kappa6`（由 `kV`）那一族管的是**竖直占位**。
⇒ 驱动 `kH` 的参数必须是**宽度导出量**。而按证据一，`kH` 收的正是第一个实参 `tW`。**方向正确。**【已核实】

**证据三（几何自洽性实测，最能说服人）**：把角放在真实矩形上量它的实际占位。
角在局部坐标从 `P0` 走到 `P9`；绝对坐标 `x = (w-r) + px*r`、`y = py*r`。
于是：水平占位 `= w - ((w-r) + p[0]*r) = r*(1 - p[0])`；竖直占位 `= r*p[19]`。
可用空间：`w/2`、`h/2`。**占位必须 ≤ 可用**，否则相邻两个角会重叠/越界。

| 矩形 (w×h, r) | tW | tH | 正确映射 `points(tW,tH)` | 假想交换 `points(tH,tW)` |
|---|---|---|---|---|
| 300×1000, r=100 | 0.5 | 1.0 | 水平 1.3333r ≤ 1.5r ✓ **通过** | 水平 1.6667r > 1.5r ✗ **顶角互相重叠 33.3px** |
| 1000×300, r=100 | 1.0 | 0.5 | 竖直 1.3333r ≤ 1.5r ✓ **通过** | 竖直 1.6667r > 1.5r ✗ **越界** |
| 200×400, r=100 | **0.0** | 1.0 | 水平 1.0000r = 1.0r ✓ **精确相切**（顶边直段长度恰为 0） | 水平 1.6667r > 1.0r ✗ **越界** |
| 400×200, r=100 | 1.0 | **0.0** | 竖直 1.0000r = 1.0r ✓ **精确相切** | 竖直 1.6667r > 1.0r ✗ **越界** |
| 300×300, r=75 | 1.0 | 1.0 | 通过 | 通过（两侧同值，无区分度） |

其中 `w=2r`（`tW=0`）这一行是**判决性**的：宽度恰好等于 2r 时，左右两个顶角必须**精确在 x=w/2 处相遇**，即水平占位必须恰好等于 `r`。只有"第一个实参 → kH"能给出这个结果（`kH=0 ⇒ p[0]=-0.0 ⇒ 占位=r`）。若交换，占位会变成 `5r/3`，角必然重叠。

⇒ **结论：没有交换。** 但请务必注意，这个"正确"依赖于**位置传参**这一事实；把 C++ 的 `points(double tW, double tH)` 误当作 Kotlin 形参顺序（`tH, tV`）来调用，就会真的交换。**【建议】把 C++ 形参改名 `points(double tHoriz, double tVert)` 或加注释固定约定。**

### 3.3 `even(t)` 与 `uneven(t,t)` 是否等价？—— 数学等价【推断】，逐位不等价【已核实】

**实测（11 个 t，20 个输出值逐一比对，C++ 与 Java 结果完全一致）：**

| t | max\|even − uneven\| | 20 个值中位模式不同的个数 |
|---|---|---|
| 0.00 | 7.77e-16 | 4 |
| 0.05 | 1.11e-16 | 4 |
| 0.10 | 2.22e-16 | 4 |
| 0.25 | 1.11e-16 | 3 |
| 0.40 | 1.11e-16 | 3 |
| 0.50 | 3.33e-16 | 4 |
| 0.60 | 1.22e-15 | 4 |
| 0.75 | 2.22e-16 | 3 |
| 0.90 | 2.22e-16 | 3 |
| 0.99 | 5.55e-16 | 4 |
| 1.00 | 1.67e-16 | 2 |

⇒ **不是逐位等价**（最大差 1.22e-15，约 5 ulp；即便 t=1 也有 2 个值位模式不同）。

**为什么数学上等价【推断】**：
1. `tH = tV = t` ⇒ `kH = kV` ⇒ `kappa3` 与 `kappa6` 两次 `solveCubicSingle` 调用**实参逐位相同** ⇒ 返回值逐位相同 ⇒ `a = b = 1.5κ`；
2. 此时构型关于 45° 对角线镜像对称。`even()` 里 `x6 = 1-y3, y6 = 1-x3` 正是 `P3` 的镜像，而 `x5 = x6-λ·sin, y5 = y6-λ·cos` 正是 `x4 = x3+λ·cos, y4 = y3+λ·sin` 的镜像 —— 即 `even()` **把一个未知量 λ 同时用于两段**；
3. `uneven()` 有 `λ3、λ6` 两个未知量（由降次四次方程 + 回代求得）。在 `a=b` 的对称构型下 `λ3=λ6=λ`，两个方程退化为一个，四次方程退化为二次方程 —— 于是两条路线解**同一个 λ**，只是代数路径不同。
4. 数值佐证：从输出反解 λ（`λ = (x4-x3)/cos`、`λ6 = (x6-x5)/sin`），11 个 t 上 `even` 的 λ 与 `uneven` 的 λ 最大只差 ~5 ulp（且反解本身还引入了一次除法舍入，所以真实差更小）。

注意第 4 点是**佐证而非证明**：反解要除以 `cos/sin`，自带舍入，无法用作逐位判据。

**这条结论的关键工程含义**：
> Kotlin 在 `tH == tV` 时走 `even()`（包括 `(0,0)`、`(1,1)` 两个缓存点）。**C++ 的 `if (tW == tH) return even(tW);` 是必须保留的**。若有人图省事改成"一律调 uneven(t,t)"，那么最常见的正方形/等半径场景会整体偏离 Kotlin 若干 ulp —— 端点、切线、单调性**全都照旧通过**，这正是"上一轮验证不够"的一个具体例子。

### 3.4 调用方的浮点提升 —— 一致，但有实现陷阱

`RoundedRectangleOutline.kt` L106-107：

```kotlin
val tW = ((width * 0.5 - r) / r).fastCoerceIn(0.0, 1.0)   // width: Float, 0.5: Double, r: Double
```

Kotlin 的 `Float.times(Double)` 存在且返回 `Double`，所以 `width` 先被**精确提升**为 double，整个 `tW` 是**双精度**运算的结果。【已核实（语言规则）】

【建议】C++ 生产代码里务必写成 `double tW = ((double)width * 0.5 - r) / r;`。若写成 `float tW = (width*0.5f - r)/r;`，`tW` 会有 ~1e-7 相对偏差，进而让 κ 与全部控制点偏离 Kotlin（虽仍满足端点/切线/单调性）。此外 `tW == tH` 的分派在 float 下**仍然**成立（同式同舍入），所以不会错分支，只是数值不再逐位相同。

### 3.5 `fastCoerceIn` 的归属 —— 与 Kotlin 设计一致

Kotlin 的角点构造器**不做夹取**，夹取在调用方（`fastCoerceIn(0.0,1.0)`）。C++ 的 `points()` 同样不夹取，语义一致。**【建议】** 在 C++ 调用侧保留同样的夹取：当 `r > w/2`（如非正方形且半径过大）时 `tW` 为负，不夹取会得到越界形状。本次审计的用例已覆盖 `tW=0`（`p_00_05`、`p_05_00`）与 `tW=1` 边界。

---

## 4. 输出语义：20 个 double 与调用方索引是否自洽

Kotlin L97 / L141 返回 `doubleArrayOf(x0, 0.0, x1, 0.0, x2, 0.0, x3, y3, x4, y4, x5, y5, x6, y6, 1.0, y7, 1.0, y8, 1.0, y9)`（C++ L118-119 / L163-164 逐元素相同）。

它编码的是**一个角的 10 个控制点** `P0..P9`，`P_i = (p[2i], p[2i+1])`：

| i | 数组下标 | 含义 | 调用方用法（`RoundedRectangleOutline.kt` L113-128） |
|---|---|---|---|
| P0 | p[0],p[1] | 起点，在顶边（y=0），切线水平 | `moveTo(p[0], p[1])` |
| P1 | p[2],p[3] | 第 1 段 cubic 控制点 1（y=0） | `cubicTo(p[2],p[3], …)` |
| P2 | p[4],p[5] | 第 1 段 cubic 控制点 2（y=0） | 同上 |
| **P3** | **p[6],p[7]** | **第 1 段终点＝第 2 段起点** | 第 1 段 `cubicTo` 的终点；第 2 段起点由 `cubicTo` 隐含 |
| P4 | p[8],p[9] | 第 2 段控制点 1 | `cubicTo(p[8],p[9], …)` |
| P5 | p[10],p[11] | 第 2 段控制点 2 | 同上 |
| **P6** | **p[12],p[13]** | **第 2 段终点＝第 3 段起点** | 第 2 段终点 |
| P7 | p[14],p[15] | 第 3 段控制点 1（x=1） | `cubicTo(p[14],p[15], …)` |
| P8 | p[16],p[17] | 第 3 段控制点 2（x=1） | 同上 |
| P9 | p[18],p[19] | 终点，在右边（x=1），切线竖直 | 第 3 段终点 |

**结论：【已核实】完全自洽。**
- `p[6],p[7]` **既是第 1 段 cubic 的终点、也是第 2 段 cubic 的起点**（`cubicTo` 的起点永远是"当前点"，无需重复给出）——所以数组里只需要给出 10 个点而不是 12 个；
- 20 个数 = 10 点 × 2 分量，没有多余或缺失的元素（调用方 `if (p.size < 20) return@apply` 的守卫也因此成立）；
- 三个不变量在实测中精确成立：`p[1]=p[3]=p[5]=+0.0`（顶边）、`p[14]=p[16]=p[18]=+1.0`（右边，位模式 `3ff0000000000000`）、`p[7]<p[9]<p[11]<p[13]<p[15]<p[17]<p[19]`；
- 调用方把**同一个数组复用四次**（靠取负/镜像画四个角）。成立的隐式前提是"角关于 45° 对角线镜像对称"，该前提由 `x6=1-y3`、`y6=1-x3` 这一对式子保证；镜像只交换左右、不交换 x/y，故 `tW→水平` 的对应关系在四个角上保持不变；
- 细节：`tW = 0` 时 `p[0] = -0.0`（位模式 `8000000000000000`，实测确有），`(w-r) + (-0.0)*r = w-r`，无影响；Kotlin 侧同样是 `-0.0`。

---

## 5. 独立数值验证：方案与实际执行结果

### 5.1 方案（在只有 JDK 17、没有 kotlinc/gradle 的前提下）

**选择：把 Kotlin 逐行重写成 Java，作为可执行参照。** 理由：

1. Kotlin/JVM 的 `kotlin.math.{sqrt,cbrt,acos,cos,sin,tan}` 直接映射到 `java.lang.Math` 同名方法，语义一一对应；`Double` 就是 IEEE754 binary64；运算符优先级/结合性与 Java 相同的部分我逐行核对过（§2）。
2. JDK 的 `Math`/`StrictMath` 实现是 fdlibm 派生的**纯 Java 实现**，不依赖操作系统的 libm，因此它比任何 C++ 实现都更适合当"稳定参照系"。
3. 不需要安装任何东西，`javac`/`java` 开箱可用（本机路径含空格，见 §5.2 的命令写法）。

配套的三个程序（均在 `build/recon/audit_corner/`）：

| 文件 | 作用 |
|---|---|
| `CornerRef.java` | Kotlin 的 Java 逐行重写（含 `when` 分派、4 项 memo 缓存、`fastCoerceIn` 语义） |
| `corner_probe.cpp` | 探针，`#include` **被审文件本体**，用同一份 `cases.txt` 驱动 |
| `Compare.java` | 按 IEEE754 位模式逐位比对两份 trace，输出分组统计与 ULP 距离 |
| `cases.txt` | 20 个用例（两程序共用，保证输入逐位相同） |

**输出即证据**：两侧都打印 `名字 <16 位十六进制位模式>`，比对是**位级**的（不是"误差 < 1e-9"这类模糊判据）。

### 5.2 环境与构建命令

| 项 | 值 |
|---|---|
| C++ 编译器 | g++ (MinGW-W64 x86_64-**ucrt**-posix-seh) 16.1.0 —— 走 UCRT，与 MSVC 共用 `ucrtbase.dll` 的数学函数 |
| JVM | OpenJDK 17.0.20 Temurin（`C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot`） |
| CPU | AMD Ryzen 5 7600（具备 FMA3/AVX2，故 `-march=native` 会启用 FMA 收缩） |

```powershell
$J='C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot\bin'
& "$J\javac.exe" -encoding UTF-8 -d "$env:TEMP\corner_audit" `
    "D:\纯c图形基座\build\recon\audit_corner\CornerRef.java" `
    "D:\纯c图形基座\build\recon\audit_corner\Compare.java"
& g++ -O2 -std=c++17 -o "$env:TEMP\corner_audit\probe_O2.exe" `
    "D:\纯c图形基座\build\recon\audit_corner\corner_probe.cpp"
# 两程序读同一个 cases.txt（用文件重定向，避免控制台编码干扰）
# 下面 <cases>/<out> 为占位符，完整可运行版本见 §7.1
Start-Process "$env:TEMP\corner_audit\probe_O2.exe" -RedirectStandardInput  <cases> -RedirectStandardOutput cpp_O2.txt -Wait -NoNewWindow
Start-Process "$J\java.exe" -ArgumentList '-cp','<out>','CornerRef' -RedirectStandardInput <cases> -RedirectStandardOutput java.txt -Wait -NoNewWindow
& "$J\java.exe" -cp <out> Compare cpp_O2.txt java.txt
```

### 5.3 主对比结果（-O2，实测，逐字引用比对器输出）

```
=== 逐位比对结果 ===
C++ 侧键数 = 4210，Java 侧键数 = 4210
  ctor 构造常量          共    17 项，逐位不同     0 项
  case 20 个控制点       共   520 项，逐位不同     0 项
  evq 汇总             共    22 项，逐位不同     0 项
  evq 逐点             共   495 项，逐位不同     0 项
  map 调用方语义         共    84 项，逐位不同     0 项
  libm 采样            共  3072 项，逐位不同   402 项
  合计 4210 项，逐位不同 402 项，逐位一致率 90.451306%
```

> 分组名的两点说明（比对器的字符串匹配副产物，不影响数字）：
> - `case 20 个控制点` 实际包含每个用例的**全部 26 个量**（2 个实参 + kH + kV + 2 个 κ + 20 个控制点），共 20×26 = 520；
> - `evq 逐点` 495 = 440 个控制点（11 t × 20 × even/uneven）+ 55 个 λ 反解值。

**即：算法相关量共 1138 项（17 + 520 + 22 + 495 + 84），逐位不同 0 项；唯一不同的 402 项全部是 `libm.*` 裸函数采样（不参与算法）。**

### 5.4 控制点输出实例（两侧位模式完全相同）

**（a）`tW = tH = 1`（even 分支；也是被审文件自带自检唯一执行的用例）**

| i | x | y |
|---|---|---|
| P0 | -0.66666666666666660 | 0.00000000000000000 |
| P1 | -0.096298356464908640 | 0.00000000000000000 |
| P2 | 0.18005187355281843 | 0.00000000000000000 |
| P3 | 0.41344463966624760 | 0.096674449083954090 |
| P4 | 0.63515456410533480 | 0.18850970669933810 |
| P5 | 0.81149029330066190 | 0.36484543589466520 |
| P6 | 0.90332555091604590 | 0.58655536033375230 |
| P7 | 1.0000000000000000 | 0.81994812644718150 |
| P8 | 1.0000000000000000 | 1.0962983564649087 |
| P9 | 1.0000000000000000 | 1.6666666666666665 |

（与既有自检程序 `kyant_corner_check.exe` 打印的 6 位小数结果一致：`P4=(0.635155, 0.188510)`、`P9=(1.000000, 1.666667)`。）

**（b）`tW = 0.5, tH = 1.0`（uneven 分支 —— 既有自检从未执行的分支）**

| i | x | y |
|---|---|---|
| P0 | -0.33333333333333330 | 0.00000000000000000 |
| P1 | -0.035398616459069054 | 0.00000000000000000 |
| P2 | 0.18747168538106734 | 0.00000000000000000 |
| P3 | 0.40134302812416030 | 0.088588410767133720 |
| P4 | 0.62057157717714670 | 0.17939584904425607 |
| P5 | 0.80680536921456260 | 0.35353502862731570 |
| P6 | 0.90332555091604590 | 0.58655536033375230 |
| P7 | 1.0000000000000000 | 0.81994812644718150 |
| P8 | 1.0000000000000000 | 1.0962983564649087 |
| P9 | 1.0000000000000000 | 1.6666666666666665 |

注意 `P6..P9` 与 (a) 完全相同：因为 `P6` 之后只由 `kV`（`tH=1`）决定，而 `P3/P4` 由 `kH`（`tW=0.5`）决定 —— 这正是 §3.2 的"哪一族管哪个方向"的直接可视化。

### 5.5 裸 libm 差异（风险量化，非本次移植缺陷）

对 1024 个采样点比较 C 运行库与 JVM：

| 函数 | 逐位不同 | 占比 | 最大 ULP |
|---|---|---|---|
| `cbrt` | 349 / 1024 | 34.1% | 1 |
| `acos` | 8 / 1024 | 0.78% | 1 |
| `cos` | 45 / 1024 | 4.4% | 1 |

但**这 402 个差异没有传播到任何算法输出**：`case.*.kappa_*`（依赖 `cbrt`）、`lambda.*`（依赖 `acos`/`cos`）、以及全部 20 个控制点都逐位相同。也就是说：在这 20 个用例实际用到的实参上，两套 libm 恰好给出相同结果。

**【推断】风险边界**：`cbrt`/`acos` 都不是 IEEE 正确舍入函数，换 glibc / musl / Apple libm / 其它 JDK 版本后，"碰巧一致"不成立的概率不低（采样里 34% 的 `cbrt` 实参就不一致）。如果目标是**跨平台逐位复现**，需要把 fdlibm 的 `cbrt`/`acos` 内置进工程；如果只要求"视觉与几何等价"，当前 1 ulp 级差异完全可以接受（远低于任何渲染精度）。

### 5.6 编译开关敏感性（同一份源码，只改开关）

| 构建 | 与 Java 参照的算法量差异 | 控制点类差异 | 控制点最大 ULP（相对误差） | 结论 |
|---|---|---|---|---|
| `-O0` | **0 / 1138** | 0 / 840 | — | ✓（与 `-O2` 输出文本完全相同） |
| `-O2` | **0 / 1138** | 0 / 840 | — | ✓ 基准 |
| `-O2 -march=native`（启用 FMA） | **133 / 1138** | 89 / 840 | **12 ulp**（≈1.9e-15 相对） | **✗ 会偏离参照** |
| `-O2 -march=native -ffp-contract=off` | **0 / 1138** | 0 / 840 | — | ✓ 修复有效 |
| `-O3 -mfma -ffp-contract=fast` | 133 / 1138 | 89 / 840 | 12 ulp | ✗ 同 FMA 情形 |
| `-Ofast`（`-O3 -ffast-math`） | **613 / 1138** | **543 / 840** | **113536 ulp**（≈1.9e-11 相对，出现在 `p[2]` 这种小量上） | **✗ 危险，禁用** |

> 说明：`1138` = 算法相关键总数（17 构造常量 + 520 用例量 + 517 evq 量 + 84 调用方量）；`840` = 其中控制点分量（`case.*.p00..p19` 共 400 + `evq.*.even/uneven.p00..p19` 共 440）。
> 比对器的"最大 ULP"统计已排除整型计数器（`bitcount_ne`）；但 `maxabsdiff`、`bitcount_ne` 这类**派生小量**（如 1.7e-16）在 FMA 下变成 0 或 2e-16 时，ULP 距离会显示成天文数字，那是"两个极小量作差"的假象，不代表控制点精度 —— 上表因此单列"控制点最大 ULP"。最差项示例（`-march=native`）：`case.p_05_10.p11` 0.35353502862731634 vs 0.35353502862731570（12 ulp）。

原因：`-ffp-contract=fast` 会把 `a*b+c` 融合成 FMA（只舍入一次），而 Java/Kotlin 规范禁止隐式融合（`a*b+c` 必须舍入两次）。基线 `x86-64` 无 FMA 指令，所以默认构建侥幸无事；一旦 `-march=native` / `-mavx2 -mfma` / MSVC `/arch:AVX2`，结果就会漂移。

---

## 6. 风险与修复建议（按优先级）

**P0 — 算法本身：无需修改。** 逐行核对 + 1138 项逐位比对均未发现任何系数、运算符、结合顺序、符号、参数方向的偏差。

1. **【必做】固定浮点收缩行为。**
   - GCC/Clang：编译含此算法的翻译单元时加 `-ffp-contract=off`；
   - MSVC：用 `/fp:strict`（或至少 `/fp:precise` 且不要 `/arch:AVX2` 配 `/fp:fast`）；
   - 依据：§5.6 实测 `-march=native` 会让 89/840 个控制点值偏离 1–12 ulp（相对 ~1.9e-15）。
2. **【必做】禁止 `-ffast-math` / `-Ofast`**（实测 543/840 个控制点偏离，最差相对误差 ~1.9e-11）。
3. **【强烈建议】重命名参数消除歧义。** C++ 的 `CornerBuilder::points(double tW, double tH)` 与 Kotlin 的 `getCornerBezierPoints(tH, tV)` 形参名冲突；建议统一改为 `points(double tHoriz, double tVert)`（或 `tW, tH` 但加注释"第一个参数驱动 kH = 水平/宽度方向"），并在 `uneven()` 里把 `tH/tV` 也一并改名。**当前语义是对的（§3.2），这纯粹是防未来的误用。**
4. **【强烈建议】补上不均匀分支的自检。** 现有 `kyant_corner_check.cpp` 的 3 处调用全是 `points(1.0, 1.0)`（L185、L268、L325）⇒ `uneven()`、`solveDepressedQuarticSingle()` **一次都没被执行过**；`polygonArea()`（L231）、`radiusStats()`（L243）是死代码。请把 `cases.txt` 里那类用例（尤其 `tW≠tH`、`tW=0`、`tV=0`）纳入自检，并加"占位 ≤ 可用半尺寸"这一条**几何不变量**（比单调性/切线强得多）。
5. **【建议】明确 libm 契约。** 若要求跨平台逐位一致，内置 fdlibm 的 `cbrt`/`acos`；否则在文档里写明"允许 ≤2 ulp 差异"。依据：§5.5。
6. **【建议】收敛重复实现。** 工作区里至少有 5 份该算法的近似副本：`kyant_corner_check.cpp`、`corner_shape_dump.cpp:17,61`、`corner_real_geometry.cpp:20,29,63`、`corner_render.cpp:12,56`、`ccrr_fill.cpp:19,27,65`。抽查了这 4 份的 `k0..k3` 与 uneven 中段公式，未发现系数差异（它们把 `cos3` 内联为 `c2*c`，IEEE 下与存储的 `cos3` 等价：乘 4.0 是精确运算，两种写法各自只有一次舍入）；但**未做逐行审计**，建议合并为单一实现。
7. **【建议】调用侧保持与 Kotlin 相同的处理**：`tW/tH` 先夹取到 `[0,1]`，并把 `width/height/radius` **提升为 double** 后再算（§3.4、§3.5）。

---

## 7. 附录

### 7.1 复现步骤（PowerShell，路径含空格与中文均可用）

```powershell
$dir = 'D:\纯c图形基座\build\recon\audit_corner'
$out = "$env:TEMP\corner_audit"; New-Item -ItemType Directory -Force $out | Out-Null
$J = 'C:\Program Files\Eclipse Adoptium\jdk-17.0.20.8-hotspot\bin'
& "$J\javac.exe" -encoding UTF-8 -d $out "$dir\CornerRef.java" "$dir\Compare.java"
& g++ -O2 -std=c++17 -o "$out\probe_O2.exe" "$dir\corner_probe.cpp"
Start-Process "$out\probe_O2.exe" -RedirectStandardInput "$dir\cases.txt" -RedirectStandardOutput "$out\cpp_O2.txt" -NoNewWindow -Wait
Start-Process "$J\java.exe" -ArgumentList @('-cp',$out,'CornerRef') -RedirectStandardInput "$dir\cases.txt" -RedirectStandardOutput "$out\java.txt" -NoNewWindow -Wait
& "$J\java.exe" '-Dfile.encoding=UTF-8' -cp $out Compare "$out\cpp_O2.txt" "$out\java.txt"
```

预期输出：`合计 4210 项，逐位不同 402 项`，且 402 项全部是 `libm.*`（算法量为 0 差异）。

### 7.2 产出文件清单

| 文件 | 说明 |
|---|---|
| `build/recon/audit_corner/CornerRef.java` | Kotlin 的 Java 逐行重写（参照实现），SHA256 `21F20243EF95539F6005E2982959CB253DCA95B6ACA747EED8978E8361D37CC0` |
| `build/recon/audit_corner/corner_probe.cpp` | C++ 探针（`#include` 被审文件本体），SHA256 `9C5DFDBDBD69CF43A8DFCE3CEC46C9159B734385620FFE467F7F2DF6150BDE3B` |
| `build/recon/audit_corner/Compare.java` | 逐位比对器（含 ULP 距离；`--dump`/`--worst` 两种查看模式），SHA256 `BF154F1509293495153428CEEA121B198275C2637D9E139E94CF8F741F5BBF16` |
| `build/recon/audit_corner/cases.txt` | 20 个共用用例，SHA256 `19B9221E99A76FF15BFB1AE354279A56F0C1BAC45A8378D39D664C6403DB79AB` |
| `build/recon/audit_corner/traces/` | 实测 trace 与比对报告：`cpp_O2.txt`/`java.txt`（两侧原始位模式）、`r_final_O2.txt`（主对比）、`r_final_native.txt`、`r_final_native_nocontract.txt`、`r_final_Ofast.txt`、`r_p11.txt`/`r_p0510.txt`（控制点实例）、`r_evq.txt`/`r_lambda.txt`、`r_maptrue.txt`/`r_mapswap.txt`（参数语义证据）、`r_worst_native.txt`/`r_worst_Ofast.txt`、`r_existing.txt`（既有自检输出） |
| `docs/CORNER_PORT_AUDIT.md` | 本文档 |

### 7.3 被审文件自带自检的覆盖缺口（证据）

```
$ grep -n "points(1.0, 1.0)" build/recon/kyant_corner_check.cpp
185:    const std::vector<double> p = builder.points(1.0, 1.0);
268:    const std::vector<double> p = builder.points(1.0, 1.0);
325:        const std::vector<double> pp = b2.points(1.0, 1.0);
```

三处实参都是 `(1.0, 1.0)` ⇒ 走 `tW == tH → even(1.0)` ⇒ `uneven()` 与 `solveDepressedQuarticSingle()` 在既有自检里**从未被执行**。
这正是"控制点单调性 + 端点切线"不足以证明移植正确的原因之一：既没有覆盖一半的代码路径，也没有约束任何中间量（某个系数抄错 1 位时，端点位置/切线方向/单调性可以全部不变）。
