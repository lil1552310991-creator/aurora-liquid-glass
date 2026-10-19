# GlassExplorer 技术分析：玻璃组件实现方式与视觉效果清单

> 分析对象：`D:\纯c图形基座\build\recon\ref_kyant\GlassExplorer.tar\GlassExplorer-main\`
> 分析日期：本轮会话
> 标注约定：**【源码】** = 仓库源码里能直接读到的事实（附 `文件:行号`）；**【推断】** = 我的推理，仓库里没有对应代码或注释。

---

## 0. 首要结论：任务前提需要修正

任务书假定这是一个 Kotlin / Jetpack Compose 项目（要求"递归列出所有 `.kt` 文件"）。**实测不成立。**

**【源码】** 全树递归搜索结果：

| 检查项 | 结果 |
| --- | --- |
| `.kt` 文件数量 | **0**（`glob **/*.kt` 返回 "No files found"） |
| 实际语言 | Objective-C（`.h/.m`）、Objective-C++（`.mm`）、Swift（`.swift`） |
| 构建系统 | Xcode 工程（`GlassExplorer.xcodeproj/project.pbxproj`），无 Gradle |
| 部署目标 | `IPHONEOS_DEPLOYMENT_TARGET = 26.0`，`SWIFT_VERSION = 6.0`（`project.pbxproj`） |
| 是否有 shader 源码 | **无**。全树 grep `shader\|glsl\|metal\|msl\|agsl\|fragment\|uniform\|vec2\|precision\|half4\|float4\|kernel\|renderEffect\|graphicsLayer\|CIFilter\|createBlur\|MTLLibrary` → **0 命中** |
| 单文件体积 | 最大的源码文件是 `ConfigurationView.swift`（12382 B）；其余全部 < 4 KB |

**【源码】** 项目自述（`README.md:1-12`）：

```markdown
# GlassExplorer 🔍✨

A fun little iOS app that plays around with UIKit's private glass effect APIs. Basically lets you mess with glass blur effects on views and see what happens.

## What it does
- Shows a draggable glass effect overlay
- Lets you tweak glass properties in real-time 
- Uses some spicy private APIs (don't ship this to the App Store lol)
```

**【源码】** 作者署名：多数文件头为 `Created by ktiays on 2025/6/10`；`CAFilter.h:2` 为 `Created by Cyandev on 2025/6/11`。`ktiays` 即 Kyant0 的另一个 GitHub 账号。

**【推断】** 这个项目的**定位是"参数探针 / 逆向实验台"，不是玻璃渲染实现**。它自己一行绘制代码都没有：玻璃的全部视觉效果由 iOS 26 系统私有的 `UIGlassEffect` + `_UIViewGlass` 完成，本项目只负责（a）把私有对象拼起来，（b）用 KVC 往渲染树深处的私有 `CAFilter` 里写数值，（c）提供一个 SwiftUI 面板实时改这些数字。

**【推断】** 任务书里描述的"Kotlin / Compose 的 Kyant0 玻璃项目"实际是同一作者的另一个仓库 —— `AndroidLiquidGlass`（`com.kyant.backdrop`），它就在同级目录 `build\recon\ref_kyant\AndroidLiquidGlass.tar\`，**那个项目才有真正的 AGSL shader 源码**。本文件第 5 章做对比，附录 A.2 逐字摘录它相关的 shader，因为那才是 GlassExplorer 所开关效果的**开源参考实现**。

---

## 1. 项目结构概览

全树共 26 个文件（含 2 个二进制图片）。`.kt` 文件：**0 个**。

```
GlassExplorer-main\
├── README.md                                          441 B   项目自述
├── .gitignore                                        1968 B
├── Artworks\
│   └── Screenshot.png                             1645602 B   效果截图（1206x2622）
├── GlassExplorer.xcodeproj\
│   ├── project.pbxproj                              12155 B   构建设置（iOS 26.0 / Swift 6.0）
│   └── project.xcworkspace\contents.xcworkspacedata   135 B
└── GlassExplorer\
    ├── main.m                                         461 B   UIApplicationMain
    ├── AppDelegate.h / .m                        190 / 1324 B   标准 App 生命周期（无实质逻辑）
    ├── SceneDelegate.h / .m                      241 / 2142 B   标准 Scene 生命周期（无实质逻辑）
    ├── ViewController.h                               174 B
    ├── ViewController.mm                             3727 B   ★ 宿主：背景图 + 玻璃面板 + 拖拽
    ├── ConfigurationView.swift                      12382 B   ★ 核心：参数面板 + 私有 CAFilter KVC
    ├── _UIViewGlass.h                                2232 B   ★ 私有类声明（18 个属性 + 9 个 init）
    ├── UIGlassEffect+Glass.h                          285 B   ★ 私有分类：effectWithGlass:
    ├── UIView+GlassEffect.h                           291 B   ★ 私有 setter：_setGlassEffect:
    ├── CAFilter.h                                     224 B   ★ 私有类桩声明
    ├── GlassExplorer-Bridging-Header.h                213 B   把上面 4 个私有头暴露给 Swift
    ├── Info.plist                                     681 B
    ├── Base.lproj\Main.storyboard                    1598 B
    ├── Base.lproj\LaunchScreen.storyboard            1665 B
    └── Assets.xcassets\
        ├── Contents.json / AccentColor.colorset\ / AppIcon.appiconset\
        └── TimCook.imageset\TimCook.png            358616 B   背景测试图
```

**【源码】** `GlassExplorer-Bridging-Header.h:5-8` 把四个私有头全部桥接给 Swift，这是 Swift 侧能碰私有 API 的前提：

```objc
#import "CAFilter.h"
#import "_UIViewGlass.h"
#import "UIGlassEffect+Glass.h"
#import "UIView+GlassEffect.h"
```

---

## 2. 核心实现与整体架构

### 2.1 用什么绘制？—— 都不是

任务书列了四个候选（Compose `graphicsLayer` / `RenderEffect` / 自定义 shader / AGSL）。**四个都不适用**，因为这不是 Compose 项目，而且项目自身不做任何像素运算。

**【源码】** 真正的绘制调用只有三条，全部委托给系统私有 API：

`ConfigurationView.swift:292-312`（唯一的"应用配置"入口）：

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

`UIGlassEffect+Glass.h:12-16` 声明了私有构造器：

```objc
@interface UIGlassEffect ()

+ (instancetype)effectWithGlass:(_UIViewGlass *)glass;

@end
```

`UIView+GlassEffect.h:10-14` 声明了私有 setter：

```objc
@interface UIView ()

@property (nonatomic, strong, nullable, setter=_setGlassEffect:) UIGlassEffect *_glassEffect;

@end
```

**【源码】** 宿主视图（`ViewController.mm:21-35`）只做三件事：铺一张背景图、建一个 200×200 的圆角 UIView、挂拖拽手势。**玻璃效果不是在 `viewDidLoad` 里加的**，而是在面板第一次改配置时才通过 `_glassEffect` 注入。

```objc
- (void)viewDidLoad {
    [super viewDidLoad];
    
    _imageView = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"TimCook"]];
    _imageView.contentMode = UIViewContentModeScaleAspectFill;
    [self.view addSubview:_imageView];
    
    _glassView = [UIView new];
    _glassView.layer.cornerRadius = 30;
    _glassView.layer.cornerCurve = kCACornerCurveContinuous;
    [self.view addSubview:_glassView];
    
    auto panGesture = [[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(_handlePanGesture:)];
    [_glassView addGestureRecognizer:panGesture];
}
```

注意 `cornerCurve = kCACornerCurveContinuous`（`ViewController.mm:30`）—— 即 Apple 的连续曲率圆角（squircle），与仓库内已有的 `docs/M3_YTBL_GLASS_INTEGRATION.md:112-169` 讨论的 Kyant0 连续曲率圆角是同一类形状。**【推断】** 这里只是借用 CALayer 的现成开关，不涉及任何曲线数学。

### 2.2 GLSL / AGSL shader 源码 —— **未找到**

**【源码】** 结论：**GlassExplorer 仓库内不存在任何 shader 源码**，无论 GLSL、AGSL、Metal MSL 还是 SkSL。

依据（全部为实际执行的检索）：

1. `glob **/*.metal, *.glsl, *.frag, *.vert, *.shader, *.agsl, *.js` → 只匹配到 `.json/.plist/.storyboard/.pbxproj`，无 shader 文件。
2. 全树内容 grep：`shader|glsl|metal|msl|agsl|fragment|uniform|vec2|vec3|vec4|precision|half4|float4|kernel|renderEffect|graphicsLayer|CIFilter|createBlur|MTLLibrary` → **`No matches found`**。
3. 全树只有 26 个文件，已逐个列出（见第 1 章），其中不含任何 shader 载体。

**【推断】** 原因很直接：玻璃着色完全发生在 iOS 系统进程内（CoreAnimation / Metal 管线），源码不随 app 分发。本项目能做的只是**通过 KVC 修改系统 CAFilter 的输入参数**（见 2.5），拿不到 filter 的实现体。

> 同一作者的开源实现里**有** AGSL 源码，见第 5 章与附录 A.2。任务书要求"如果有，逐字完整摘录"——GlassExplorer 满足不了；我把能真正指导 GLES 复刻的那份源码逐字附上，并明确标注它来自另一个仓库。

### 2.3 模糊是怎么实现的？—— 未找到算法，只找到参数名

**【源码】** GlassExplorer 不创建模糊。它既没有 `RenderEffect.createBlurEffect`，也没有 `CIGaussianBlur`，也没有 radius 公式。它能做的只是改系统 filter 的 6 个模糊相关输入（`ConfigurationView.swift:239-251`）：

| KVC key | 取值范围（源码给定） | 行号 |
| --- | --- | --- |
| `inputBlurRadius` | 0 … 100 | `ConfigurationView.swift:251` |
| `inputBlurDistance0` … `inputBlurDistance4` | -200 … 200 | `ConfigurationView.swift:241-245` |
| `inputBlurOpacity0` … `inputBlurOpacity4` | 0 … 1，步长 0.01 | `ConfigurationView.swift:246-250` |
| `inputBleedBlurRadius` | 0 … 200 | `ConfigurationView.swift:239` |

**【推断】** 由参数命名结构可合理推断 Apple 的模糊模型（**这是推断，不是源码事实**）：

- `blurRadius` 是基准模糊半径；
- `blurDistance0..4` + `blurOpacity0..4` 是**成对的 5 组**"位移量 + 权重"。成对出现强烈暗示这不是单一高斯，而是**5 层不同偏移/半径的模糊采样按权重叠加**（多尺度模糊栈，或 5-tap 方向的位移模糊）。这解释了为什么距离可以是**负数**（-200）——负距离代表反向位移。
- 也就是"一条边的模糊可能由 5 个不同距离的模糊版本混合而成"，这正是液态玻璃边缘那种"从清晰到模糊的渐变带"的来源。

**【推断】** 能否在 GLES 复刻：**能，且这是最容易复刻的一项**。做法：对背景纹理跑两趟可分离高斯得到 mip 链，再按 `blurDistanceN`/`blurOpacityN` 做 5 次偏移采样加权求和。仓库内已有的 ytbl 管线（`docs/M3_YTBL_GLASS_INTEGRATION.md:21-31`）里 `kGatherFragmentSource` 已经在做"12-tap 可分离高斯 + `uBoxHalf` 盒式预滤波"，与这个模型天然契合。

### 2.4 有没有用到 `Modifier.graphicsLayer` 的 `renderEffect` 链？—— 不适用

**【源码】** 没有。`Modifier.graphicsLayer` / `renderEffect` 是 **Compose（Kotlin）API**，本项目是 iOS ObjC/Swift，全树 grep `graphicsLayer|renderEffect` → 0 命中。

**【源码】** iOS 侧的等价物是 CALayer 的 `filters` 数组。本项目确实找到了那个数组，但**只写不建**——它替换掉系统已装好的 filter 的**第一个**元素（`ConfigurationView.swift:154-161`）：

```swift
        } set: { newValue in
            guard let backdropLayer = backdropLayer(from: glassView),
                  let filter = backdropLayer.filters?.first as? CAFilter else {
                return
            }
            backdropLayer.filters = []
            write(newValue, to: filter)
            backdropLayer.filters = [filter]
            onChange(newValue)
        }
```

**【源码】** 注意这里的三步写法：先 `filters = []` 清空、再 `setValue:forKey:`、最后 `filters = [filter]` 装回。**【推断】** 这是为了绕开 CALayer 对 filter 参数的缓存，强制 CoreAnimation 重新读参数（否则 KVC 改了值但不重绘）。这是本项目最有价值的一条**工程技巧**，与视觉无关但决定实验能否生效。

**【源码】** 实际的 `renderEffect` 链在系统的 `_UIViewGlass`/`UIGlassEffect` 内部，源码不可见。**【推断】** 由 `inputInnerRefraction*` / `inputOuterRefraction*` / `inputBlur*` / `inputBleed*` 四组共存可以看出，系统内部确实是一条**多级 effect 链**（折射 → 模糊 → 渗出），但链的构造代码在系统里，不在本仓库。

### 2.5 本项目真正的"技术"：私有 API 触达链路

这是 GlassExplorer 唯一原创的部分，值得单独说明。它有两条独立的触达路径：

**路径 A：配置对象路径（改整体外观）**

```
GlassConfiguration (SwiftUI @Observable, ConfigurationView.swift:85-103)
  → ConfigurationHostingController.updateProperties()  (ConfigurationView.swift:292-312)
    → _UIViewGlass.init(variant:size:smoothness:)       (_UIViewGlass.h:33)
    → UIGlassEffect(glass:)                             (UIGlassEffect+Glass.h:14)
    → UIView._glassEffect = glassEffect                 (UIView+GlassEffect.h:12)
```

**【源码】** `_UIViewGlass.h:8-48` 声明了全部可触达能力（**这是本项目信息密度最高的文件**）：18 个属性、9 个 init 重载、4 个自适应亮度方法、1 个材质解析方法。

**路径 B：图层树路径（改底层 filter 数值）**

**【源码】** `ConfigurationView.swift:127-135` 用一条硬编码的 4 级 `superlayer/sublayer` 下钻路径，从自己的 glassView 走到系统内部的 backdrop layer：

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

**【推断】** 这段路径的含义（源码无注释，纯推断）：`glassView.layer.superlayer` → 系统为玻璃建的 `multiLayerRoot`；取其 `sublayers.last` → `glassGroupView`（与 `allowsGrouping` 呼应）；`glassGroupView.sublayers.first` → `materialProvider`（出材质的层）；`materialProvider.sublayers.first` → `backdrop`（真正持有模糊/折射 filter 的层）。层级命名是我按变量名反推的，实际类名未知。

**【源码】** 拿到 backdrop layer 后，用 KVC 读写 filter 的第一个元素（`ConfigurationView.swift:191-200`）：

```swift
    func read(from filter: CAFilter) -> Double {
        guard let number = filter.value(forKey: key) as? NSNumber else {
            return 0
        }
        return number.doubleValue
    }
    
    func write(_ value: Double, to filter: CAFilter) {
        filter.setValue(NSNumber(value: value), forKey: key)
    }
```

**【源码】** `CAFilter` 在仓库里只是个空桩（`CAFilter.h:9-11`）：

```objc
NS_SWIFT_UI_ACTOR
@interface CAFilter : NSObject
@end
```

**【推断】** 空桩的作用是让 Swift 编译器接受这个类型；真正的能力全靠 `value(forKey:)`/`setValue(_:forKey:)` 动态派发。这是一种典型的"编译器不校验、运行时才炸"的私有 API 用法，也正是 `README.md:12` 说"别拿去上架"的原因。

---

## 3. 玻璃视觉效果清单

**【推断】** 重要前提：**GlassExplorer 自身不实现任何视觉效果**。下面这张表是"Apple Liquid Glass 的**参数 → 视觉效果**映射表"，是本项目通过穷举参数暴露出来的信息。每一行的"视觉描述"来自参数命名 + 截图观察 + 我的推断，不是源码注释。

参照截图 `Artworks/Screenshot.png`（1206×2622）：背景是 Tim Cook 头像照，中央一个约 200×200 的连续曲率圆角玻璃面板，面板内部图像被**明显放大并向中心/边缘挤压**（脸部特征在面板内被放大、错位），面板边缘有一圈亮边，面板外沿有模糊过渡带。底部是黑色的配置 sheet。

### 3.1 面板级参数（`_UIViewGlass`）

| # | 名称 | 来源 `文件:行号` | 视觉描述 | 关键技术手段与参数 | GLES fragment shader 复刻 |
| --- | --- | --- | --- | --- | --- |
| 1 | `variant` | `_UIViewGlass.h:10`；`ConfigurationView.swift:28,88` | **未找到**语义。仅知是可枚举的玻璃预设，UI 范围 0…10，默认 1 | 私有不透明枚举，无文档 | **不能**。语义未知，只能当查表常量；除非逐一截图比对反推 |
| 2 | `size` | `_UIViewGlass.h:11`；`ConfigurationView.swift:34,89` | **未找到**。UI 范围 0…5，默认 0 | 同上 | **不能**（同上） |
| 3 | `smoothness` | `_UIViewGlass.h:13`；`ConfigurationView.swift:50,102` | **【推断】** 折射剖面的"圆滑度"：值越高，边缘形变越平滑连续、越少硬折 | UI 0…1，步长 0.01，默认 0.0 | **能**。对应折射剖面函数 `circleMap(x)=1-sqrt(1-x²)`（参考实现见附录 A.2 `Shaders.kt:63-65`）的指数/混合系数 |
| 4 | `contentLensing` | `_UIViewGlass.h:17`；`ConfigurationView.swift:39,91` | **【推断】** 面板**自身子视图内容**也参与折射。截图里面板内图像被放大错位，很可能就是该开关的效果 | 布尔 | **难**。需要把面板内容层作为折射源纹理再自采样，即"自反馈/自引用"，必须额外 FBO + 两趟渲染并处理边界 |
| 5 | `highlightsDisplayAngle` | `_UIViewGlass.h:18`；`ConfigurationView.swift:40,92` | 边缘镜面高光的**显示角度** | 布尔（只暴露开关，角度值不可设） | **能**。SDF 梯度 · 光方向 + `pow(abs(d),falloff)`，参考实现见附录 A.2 `Shaders.kt:170-181` |
| 6 | `boostWhitePoint` | `_UIViewGlass.h:26`；`ConfigurationView.swift:41,100` | **【推断】** 抬升白点，让玻璃区的亮部更亮/更"通透" | 布尔 | **能**。纯算术 tone mapping（乘/加白点），一行代码 |
| 7 | `tintColor` | `_UIViewGlass.h:14` | 玻璃染色 | `UIColor`，UI 未暴露 | **能**。`mix(color, tint, tint.a)` |
| 8 | `controlTintColor` | `_UIViewGlass.h:15` | 控件元素的染色 | `UIColor`，UI 未暴露 | **能**（同上） |
| 9 | `subvariant` | `_UIViewGlass.h:16` | **未找到**语义。`NSString` 预设名，UI 未暴露 | 私有字符串枚举 | **不能**（语义未知） |
| 10 | `identifier` | `_UIViewGlass.h:12` | **未找到**。整数标识，UI 未暴露 | `NSInteger` | **不能**（语义未知） |
| 11 | `backdropGroupName` | `_UIViewGlass.h:28` | **【推断】** 指定该玻璃采样的"背景组"，决定它模糊哪一层背景 | `NSString`，UI 未暴露 | **能但工程量大**。等价于给采样源加一层 group 掩码 |
| 12 | `adaptive` / `setAdaptiveStyle:` / `setAdaptiveFixedLuminance:` / `setAdaptiveInitialLuminance:` | `_UIViewGlass.h:41-44` | **【推断】** 根据背景亮度自适应调整玻璃亮度（亮背景→玻璃变暗，暗背景→玻璃变亮），保证对比度 | 3 个 setter + 2 个亮度参数，UI 未暴露 | **能**。采样背景平均亮度 → 映射到玻璃亮度，一轮降采样 + 一次 mix |
| 13 | `_resolvedMaterialUsingTraitCollection:size:` | `_UIViewGlass.h:46` | **【推断】** 按 trait（深/浅色）+ 尺寸解析出实际材质，是 variant/size 到具体材质的查表入口 | 私有方法，UI 未暴露 | **不能**（返回的是私有材质对象） |
| 14 | `subdued:` / `state:` init 变体 | `_UIViewGlass.h:34-39` | **【推断】** `subdued` 大概是"弱化/灰化"态（如非活跃窗口）；`state` 是控件状态机（normal/highlighted/…） | 私有枚举 | **不能**（语义未知） |

### 3.2 布局与分组参数

| # | 名称 | 来源 | 视觉描述 | 技术手段 | GLES 复刻 |
| --- | --- | --- | --- | --- | --- |
| 15 | `allowsGrouping` | `_UIViewGlass.h:27`；`ConfigurationView.swift:64,101,309` | **【推断】** 允许相邻玻璃**成组**、互相吸附/融合（即任务书问的"形态融合"） | 布尔开关，**无算法** | **难**。见 4.1 |
| 16 | `flexible` | `_UIViewGlass.h:24`；`ConfigurationView.swift:63,98,299` | **【推断】** 玻璃尺寸随内容弹性伸缩，而非固定 frame | 布尔 | **能**（顶点/尺寸层，非 fragment） |
| 17 | `_flexVariant` | `_UIViewGlass.h:25`；`ConfigurationView.swift:69,99,300` | **【推断】** 弹性伸缩的具体模式，UI 范围 0…5 | 私有枚举 | **不能**（语义未知） |

### 3.3 排除类参数（`excluding*`）

| # | 名称 | 来源 | 视觉描述 | GLES 复刻 |
| --- | --- | --- | --- | --- |
| 18 | `excludingPlatter` | `_UIViewGlass.h:19`；`ConfigurationView.swift:55,93` | **【推断】** 底层的 "platter"（承载玻璃的底盘层）不参与玻璃采样，避免自我采样 | **能但工程量大**，需图层掩码 |
| 19 | `excludingForeground` | `_UIViewGlass.h:20`；`ConfigurationView.swift:56,94` | **【推断】** 前景内容不参与背景采样 | 同上 |
| 20 | `excludingShadow` | `_UIViewGlass.h:21`；`ConfigurationView.swift:57,95` | **【推断】** 阴影层不参与采样 | 同上 |
| 21 | `excludingControlLensing` | `_UIViewGlass.h:22`；`ConfigurationView.swift:58,96` | **【推断】** 控件不受内容透镜影响 | 同上 |
| 22 | `excludingControlDisplacement` | `_UIViewGlass.h:23`；`ConfigurationView.swift:59,97` | **【推断】** 控件不受折射位移影响（只被模糊，不被推挤） | 同上 |

**【源码】** 这五个排除开关的存在本身就是一条重要情报：它说明 Apple 的玻璃采样是**分层可屏蔽**的，而且区分了 "lensing（透镜）" 与 "displacement（位移）" 两种形变 —— 与我方把折射当单一 UV 偏移的做法不同。

### 3.4 底层 CAFilter 数值输入（18 项）—— 本项目的核心情报

**【源码】** 完整清单，逐字来自 `ConfigurationView.swift:233-252`：

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

按语义分四组（**分组与解读为推断**）：

| 组 | key | 范围 | 推断的视觉含义 | GLES 复刻 |
| --- | --- | --- | --- | --- |
| **内折射** | `inputInnerRefractionAmount` | -200…200 | 内圈折射的**位移强度**（可为负 = 反方向，即凹/凸切换） | **能** |
| | `inputInnerRefractionHeight` | 0…100 | 内圈折射**影响带的高度**（从边缘往内多少像素） | **能** |
| **外折射** | `inputOuterRefractionAmount` | -200…200 | 外圈折射位移强度 | **能** |
| | `inputOuterRefractionHeight` | 0…100 | 外圈折射影响带高度 | **能** |
| **渗出** | `inputBleedAmount` | 0…200 | **【推断】** 玻璃的颜色/内容向面板**外**扩散的距离（边缘外的一圈光晕） | **能**（外扩模糊 + 衰减遮罩） |
| | `inputBleedBlurRadius` | 0…200 | 渗出部分的模糊半径 | **能** |
| | `inputBleedOpacity` | 0…1 | 渗出部分的透明度 | **能** |
| **多尺度模糊** | `inputBlurDistance0..4` | -200…200 | **【推断】** 5 层模糊各自的采样位移（负值 = 反向） | **能** |
| | `inputBlurOpacity0..4` | 0…1 | 5 层模糊各自的权重 | **能** |
| | `inputBlurRadius` | 0…100 | 基准模糊半径 | **能** |

**【推断】** 最有价值的结构性发现：**内/外折射是成对出现的（各有一个 Amount 和一个 Height）**。这说明 Apple 的折射不是一个 SDF 剖面，而是**两条独立的折射带**——外圈（贴近边缘、窄、强）与内圈（更靠内、宽、弱）。这直接对应任务书第 4 条问的"玻璃厚度模拟"：两带折射在视觉上等价于**光线穿过两个界面**（前表面 + 后表面），是有厚度玻璃的经典近似。详见 4.3。

---

## 4. 四个专项问题的回答

### 4.1 玻璃的"形态融合"（两面板靠近时像水滴粘连）

**【源码】** 仓库内**只有开关，没有算法**。

- `_UIViewGlass.h:27` 声明 `@property (nonatomic, assign) BOOL allowsGrouping;`
- `ConfigurationView.swift:64` UI 上的 `Toggle("Allows Grouping", isOn: $configuration.allowsGrouping)`
- `ConfigurationView.swift:101` 默认 `false`
- `ConfigurationView.swift:309` 应用：`glass.allowsGrouping = configuration.allowsGrouping`

**【源码】** 相关的还有 `flexible`（`_UIViewGlass.h:24`）与 `_flexVariant`（`_UIViewGlass.h:25`），以及 2.5 节推断出的图层树里的 `glassGroupViewLayer`——层级名与"group"呼应。

**【推断】** 真正的融合数学（若是 metaball 就是 SDF 的 `smin`）在系统内部，**本仓库未找到**任何 `smin` / 平滑最小值 / 距离场融合代码。而且截图里只有一个玻璃面板，**无法从截图观察到融合效果**。

**【推断】** GLES 复刻：**难，但可自研**。Apple 的实现拿不到；可行路径是自己写 `smin(a,b,k) = min(a,b) - h²k/4`（h = clamp(0.5+0.5*(b-a)/k, 0, 1)）把两个圆角矩形 SDF 平滑并起来，再对合并后的 SDF 做一次统一的折射采样。代价是：合并后轮廓不再解析，边缘高光与折射剖面都要基于合并 SDF 重算。

**【推断】** 交叉参考：另一仓库 `AndroidLiquidGlass` 全树 grep `smin|smoothMin|metaball|liquidMerge` → **0 命中**，说明那个项目也没有做真正的 SDF 融合，它的"液体感"来自形状/圆角插值 + 位移。详见第 5 章。

### 4.2 边缘的镜面高光（specular rim）如何计算？

**【源码】** 仓库内**只有开关，没有公式**。

- `_UIViewGlass.h:18` `@property (nonatomic, assign) BOOL highlightsDisplayAngle;`
- `ConfigurationView.swift:40` `Toggle("Highlights Display Angle", isOn: $configuration.highlightsDisplayAngle)`
- `ConfigurationView.swift:302` `glass.highlightsDisplayAngle = configuration.highlightsDisplayAngle`

**【源码】** 注意这个属性名：`highlightsDisplayAngle` —— 是"**高光显示角度**"的开关，不是"高光角度"的数值。也就是说 Apple 暴露给这个 app 的只是一个"是否按角度显示高光"的布尔量，**光方向本身不可设**。

**【推断】** 从截图观察：面板上边缘/左上角有一圈亮白描边，越靠近角部越亮，直边中段较暗——这是典型的"边缘法线 · 光方向"型高光。

**【推断】** GLES 复刻：**能，而且这是性价比最高的一项**。参考实现（另一个仓库，逐字见附录 A.2 `Shaders.kt:170-181`）给出了完整解法：

```
grad  = gradSdRoundedRect(centeredCoord, halfSize, gradRadius)   // SDF 梯度 ≈ 边缘法线
d     = dot(grad, float2(cos(angle), sin(angle)))                // 与光方向点积
i     = pow(abs(d), falloff)                                     // 角度锐化
return color * i
```

关键细节：`gradRadius = min(radius*1.5, min(halfSize.x, halfSize.y))`（`Shaders.kt:175`）—— 梯度不是在真实圆角半径上求，而是用 1.5 倍半径，这样高光带会**略微溢出到直边**，形成 Apple 那种"亮边沿直线延伸"的观感。这条参数很值得直接搬。另外 `abs(d)` + `pow` 让**相对的两个边缘同时亮**（上边和对应下边），符合截图。

### 4.3 玻璃对背景的缩放/位移（backdrop scale / offset）

**【源码】** **没有直接的缩放参数。**18 个 filter 输入里没有 `scale`、`zoom`、`magnification` 之类的 key（`ConfigurationView.swift:233-252` 逐条核对）。`_UIViewGlass.h:8-48` 也没有缩放属性。

**【源码】** 与位移相关的是：`inputBlurDistance0..4`（`ConfigurationView.swift:241-245`）、`inputInnerRefractionAmount` / `inputOuterRefractionAmount`（`ConfigurationView.swift:234,236`）。

**【推断】** 但截图里**明显能看到缩放**：面板内的 Tim Cook 脸部比面板外大得多，且向中心挤压。这个视觉效果是由**折射位移**产生的，不是由独立缩放参数产生的——正折射在边缘处把 UV 往外推，等价于对面板中央区域做了一次放大（凸透镜）。所以：

- 任务书问的 "backdrop scale" → **未找到独立参数**；视觉上的缩放是折射的副产品。
- 任务书问的 "backdrop offset" → 有，就是 `inputBlurDistanceN`（模糊采样偏移）与两个 `RefractionAmount`（折射 UV 偏移）。

**【推断】** GLES 复刻：**能**。凸透镜式放大不需要单独的 scale uniform，直接用"边缘 UV 外推 + 中心不偏移"的折射剖面即可。若确实要独立的放大倍数，在采样前对 UV 做 `uv = center + (uv-center)*magnify` 一行即可，但这会破坏"边缘无缝"（面板边界处会有接缝），必须配合边缘衰减。

**【推断】** 补充证据：另一个仓库 `AndroidLiquidGlass` 的 `lens(refractionHeight, refractionAmount, depthEffect, chromaticAberration)`（`effects/Lens.kt:16-21`）**也没有 scale 参数**，只有折射高度与折射量。两个独立项目都选了"用折射位移代替缩放"，这基本可以确认 Apple 液态玻璃也是这个模型。

### 4.4 有无"玻璃厚度"的模拟（多层折射）？

**【源码】** 参数层面**有**，算法层面**未找到**。

- 内折射：`inputInnerRefractionAmount` / `inputInnerRefractionHeight`（`ConfigurationView.swift:234-235`）
- 外折射：`inputOuterRefractionAmount` / `inputOuterRefractionHeight`（`ConfigurationView.swift:236-237`）

**【推断】** 这是本仓库最重要的一条线索。**内/外两带折射**正是"有厚度的玻璃板"的经典近似：

```
入射光线
   ↓  ← 前表面（外折射带，窄而强，位于最外缘）
  [玻璃体]
   ↓  ← 后表面（内折射带，宽而弱，位于更内侧）
  背景
```

两块折射带各自有独立的"高度（影响带宽度）"和"量（位移强度）"，说明系统确实在模拟两个折射界面。此外 `blurDistance0..4` + `blurOpacity0..4` 的 5 层结构可能也与厚度有关（不同深度对应不同模糊度，即**渐进模糊/景深**）。

**【推断】** GLES 复刻：**能**。做法：对同一个圆角矩形 SDF 取两条不同偏移量的剖面：

```
d_outer = profile(clamp(-sd / outerHeight, 0, 1)) * outerAmount   // 外部薄环
d_inner = profile(clamp(-sd / innerHeight, 0, 1)) * innerAmount   // 内部厚带
uv += (d_outer + d_inner) * grad
```

注意 `Amount` 的 UI 范围是 **-200…200**（负值可用）—— 负值可让内/外折射反向，从而模拟"凸起"与"凹陷"两种玻璃厚度形态。这条必须保留符号。

**【推断】** 交叉参考：`AndroidLiquidGlass` 里有对应的 `depthEffect` 布尔量（`effects/Lens.kt:19`），其 shader 实现是把**指向中心的径向向量**混进梯度（`Shaders.kt:80`）：`grad = normalize(gradSdRoundedRect(...) + depthEffect * normalize(centeredCoord))`。这就是"厚度感"的一种低成本实现——给边缘法线加一点朝向中心的倾斜分量，让折射看起来像穿过一个有体积的透镜而不是一张纸。**这个技巧很值得直接搬到我方 shader。**

### 4.5 动画状态下玻璃参数怎么变？

**【源码】** 仓库内**唯一的动画是面板位移**，玻璃参数本身不做动画。

`ViewController.mm:74-100`（拖拽）：

```objc
- (void)_handlePanGesture:(UIPanGestureRecognizer *)gesture {
    switch (gesture.state) {
        case UIGestureRecognizerStateBegan:
            _lastCenter = _glassView.center;
            break;
        case UIGestureRecognizerStateChanged: {
            CGPoint translation = [gesture translationInView:_glassView];
            CGPoint center = *_lastCenter;
            center.x += translation.x;
            center.y += translation.y;
            [UIView animateWithSpringDuration:0.3
                                       bounce:0.12
                        initialSpringVelocity:0
                                        delay:0
                                      options:(UIViewAnimationOptionAllowUserInteraction)
                                   animations:^{
                self->_glassView.center = center;
            } completion:nil];
        } break;
        case UIGestureRecognizerStateEnded:
        case UIGestureRecognizerStateCancelled:
            _lastCenter = _glassView.center;
            break;
        default:
            break;
    }
}
```

**【源码】** 弹簧参数：`duration = 0.3`、`bounce = 0.12`、`initialSpringVelocity = 0`（`ViewController.mm:84-86`）。每次 `Changed` 都重新触发一次 0.3s 弹簧，形成跟随手感。

**【源码】** 玻璃参数变更时**没有插值**：`updateProperties()`（`ConfigurationView.swift:292-312`）每次都是**先置 `nil` 再重建**整个 `UIGlassEffect`：

```swift
        glassView?._glassEffect = nil
        ... 重建 _UIViewGlass 与 UIGlassEffect ...
        glassView?._glassEffect = glassEffect
```

**【推断】** 这是**硬切换**，不是过渡动画。拖滑动条时玻璃会瞬间跳到新参数。**因此：本仓库未找到"动画态下玻璃参数如何变化"的任何实现**——它没做。

**【推断】** 但这条"先 nil 再设"的写法本身是必要的：`UIView._glassEffect` 是 `strong` 属性（`UIView+GlassEffect.h:12`），直接赋新对象可能被系统判定为同一个 effect 而不刷新；先置 nil 强制走一次"移除 + 添加"路径。与 2.4 节的 `filters = [] / filters = [filter]` 是同一类"强制刷新"技巧。

**【源码】** 另一个动画相关的观察：`ConfigurationHostingController` 是一个 `UIModalPresentationFormSheet`，`modalInPresentation = YES`（`ViewController.mm:53`），可停在 30% 高度的自定义 detent（`ViewController.mm:42-51`）——**面板本身是半透明的玻璃**（截图可见 sheet 上方的列表文字被模糊透过）。所以这个 app 里其实有**两个**玻璃：被拖拽的实验面板 + 系统自带的 sheet 玻璃。**【推断】** 后者由系统实现，不受本项目参数控制。

---

## 5. 与 AndroidLiquidGlass 的差异

**【源码】** 两个仓库同属 Kyant0，但**技术栈、定位、代码性质完全不同**，几乎没有"重叠实现"，只有"效果上的重叠"。

### 5.1 硬事实对照

| 维度 | GlassExplorer | AndroidLiquidGlass |
| --- | --- | --- |
| 目录 | `build\recon\ref_kyant\GlassExplorer.tar\` | `build\recon\ref_kyant\AndroidLiquidGlass.tar\` |
| 平台 | iOS 26.0（`project.pbxproj`: `IPHONEOS_DEPLOYMENT_TARGET = 26.0`） | Android（API 33+）+ Skiko 桌面/JS/Wasm |
| 语言 | ObjC / ObjC++ / Swift 6.0 | Kotlin（Compose Multiplatform） |
| 文件数 | 24（`.kt`：0） | 185（`.kt`：88） |
| 定位 | **参数探针 / 逆向实验台**（`README.md:3`："plays around with UIKit's private glass effect APIs"） | **可发布的库 + 演示 app**（`README.md:5`："A customizable Liquid Glass effect library for Compose Multiplatform"，Maven Central `io.github.kyant0/backdrop`） |
| 玻璃由谁画 | **iOS 系统**（私有 `UIGlassEffect` + `_UIViewGlass` + 内部 CAFilter） | **自己画**（AGSL RuntimeShader） |
| 本项目内的 shader 源码 | **0 处**（全树 grep 无命中） | 7 处 `@Language("AGSL")`，其中 5 处在 `backdrop\src\commonMain\kotlin\com\kyant\backdrop\internal\Shaders.kt`（行 21/50/86/160/183） |
| 绘制入口 | `UIView._glassEffect = UIGlassEffect(glass:)`（`ConfigurationView.swift:310-311`） | `graphicsLayer?.renderEffect = effectScope.renderEffect`（`backdrop\src\commonMain\kotlin\com\kyant\backdrop\DrawBackdropModifier.kt:366`） |
| 平台能力门槛 | iOS 26（系统能力，无降级路径） | `isRuntimeShaderSupported()` = `Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU`（`backdrop\src\androidMain\kotlin\com\kyant\backdrop\Platform.kt:10`），即 **API 33+**；Skiko 恒为 `true`（`backdrop\src\skikoMain\kotlin\com\kyant\backdrop\Platform.kt:5`） |
| 无 shader 时的降级 | 不存在此概念（系统代劳） | **静默跳过**：`lens()` 首行 `if (!isRuntimeShaderSupported()) return`（`effects\Lens.kt:22`），`HighlightStyle` 在 `createShader` 里返回 `null`（`highlight\HighlightStyle.kt:56-69`、`86-99`） |
| 折射实现 | 私有，不可见 | AGSL：圆角矩形 SDF + `circleMap` 剖面（`internal\Shaders.kt:51-84`） |
| 模糊实现 | 私有，只有参数名 | `BlurEffect(radius, radius, tileMode)`（`effects\Blur.kt:23`）；阴影用平台 `BlurMaskFilter` / `MaskFilter.makeBlur`（`androidMain\internal\Paint.kt:8-10`、`skikoMain\internal\Paint.kt:10-12`） |
| 色散 | 无参数暴露 | 有：`RoundedRectRefractionWithDispersionShaderString`，7 次采样（`internal\Shaders.kt:87-158`），由 `chromaticAberration` 布尔量开启（`effects\Lens.kt:20,51-53`） |
| 边缘高光 | `highlightsDisplayAngle` 布尔开关（`_UIViewGlass.h:18`） | SDF 梯度 · 光方向，默认 `angle=45°`、`falloff=1`、`Color.White.copy(alpha=0.5f)`、`BlendMode.Plus`（`highlight\HighlightStyle.kt:45-50`） |
| 形态融合 | `allowsGrouping` 布尔开关（`_UIViewGlass.h:27`），**无算法** | 全树 grep `smin\|smoothMin\|metaball\|liquidMerge` → **0 命中**，**同样无 SDF 融合** |
| 厚度感 | 内/外双折射带（`ConfigurationView.swift:234-237`），**算法不可见** | `depthEffect` 布尔量，实现为梯度混入径向向量（`internal\Shaders.kt:80`） |
| 动画 | 只有面板位移弹簧 `duration 0.3 / bounce 0.12`（`ViewController.mm:84-86`）；玻璃参数**硬切换**（`ConfigurationView.swift:293`） | 形状/参数随进度插值，例如 `blur(8f.dp.toPx() * (1f - progress))` + `lens(..., chromaticAberration = true)`（`LiquidToggle.kt:160-164`、`LiquidSlider.kt:169-173`） |

### 5.2 差异的实质

**【推断】** 三个层次的区别：

1. **不是同一件事**。GlassExplorer **不实现玻璃**，它是"给 Apple 的玻璃调参的遥控器"；AndroidLiquidGlass **实现玻璃**，是"用 AGSL 从零写一个玻璃"。前者是**观测工具**，后者是**渲染器**。任务书猜"GlassExplorer 可能更偏向展示/实验"——**这个猜测正确**，而且比"展示"更极端：它连展示都不做，它做的是**参数测绘**。

2. **信息互补，方向相反**。GlassExplorer 给出的是**参数语义与取值范围**（作者无法给出算法，因为算法在系统里）；AndroidLiquidGlass 给出的是**算法与公式**（作者可以给出实现，因为是他自己写的）。两者合起来恰好补全："Apple 暴露了哪些旋钮" + "这些旋钮大概对应什么数学"。**对 GLES 复刻来说，GlassExplorer 的价值是需求规格，AndroidLiquidGlass 的价值是参考实现。**

3. **可复刻性的差距**。GlassExplorer 里所有效果都在系统进程内，**无法直接移植**；只有在 AndroidLiquidGlass 里才能找到可读、可搬的 shader 代码。因此任务书第 3 条"能否在 GLES fragment shader 里复刻"这一列，我主要是靠 AndroidLiquidGlass 的 AGSL 来判定的（已在正文逐条标注参考出处）。

---

## 6. 对我方 GLES 复刻的可迁移结论

**【推断】** 按优先级排序，以下是从 GlassExplorer 的参数测绘中能直接转化为 GLES 规格的条目：

1. **折射必须做两带（内 + 外），且两带的位移量必须允许负数**。
   依据：`inputInnerRefractionAmount/Height` 与 `inputOuterRefractionAmount/Height` 成对存在，Amount 范围 **-200…200**（`ConfigurationView.swift:234-237`）。我方现有 shader 若只有一个折射剖面，建议改成双剖面叠加，并保留符号以支持凸/凹两种形态。

2. **模糊不是单一高斯，而是 5 组「距离 + 权重」的叠加**。
   依据：`inputBlurDistance0..4`（-200…200）+ `inputBlurOpacity0..4`（0…1）+ `inputBlurRadius`（0…100）（`ConfigurationView.swift:241-251`）。距离可为负说明是**有符号位移采样**，不是纯模糊。我方的 `kGatherFragmentSource`（见 `docs/M3_YTBL_GLASS_INTEGRATION.md:25`）已经是 12-tap 可分离高斯，扩展成"5 个不同偏移的采样按权重混合"代价很低。

3. **玻璃边缘外还有一圈「渗出」(bleed)，是独立的一层**。
   依据：`inputBleedAmount`（0…200）+ `inputBleedBlurRadius`（0…200）+ `inputBleedOpacity`（0…1）（`ConfigurationView.swift:238-240`）。这解释了截图里面板**外侧**还有一圈模糊过渡——不是面板内部的模糊溢出，而是专门的一层。我方若只做面板内部的模糊，边缘会很"干"。

4. **折射剖面用 `circleMap`，高光用 SDF 梯度 · 光方向 `pow(abs(d), falloff)`，梯度半径取 `min(r*1.5, min(halfW, halfH))`**。
   依据：参考实现逐字见附录 A.2（`internal\Shaders.kt:63-65,78,170-181`）。这是**可直接搬运**的代码级资产。

5. **`depthEffect` 用「梯度混入径向向量」伪造厚度**。
   依据：`normalize(gradSdRoundedRect(...) + depthEffect * normalize(centeredCoord))`（附录 A.2 `Shaders.kt:80`），配合 `AndroidLiquidGlass` 的 `lens(..., depthEffect = true)` 开关（`effects\Lens.kt:19,50`）。一行改动就能让玻璃从"一张纸"变成"有体积"，成本极低。

6. **形态融合（水滴粘连）两个参考项目都没做**，需要自研 `smin`。GlassExplorer 只暴露 `allowsGrouping` 布尔量（`_UIViewGlass.h:27`），AndroidLiquidGlass 全树无 `smin` 命中。**不要把这一项列入"有参考实现"的预期**。

7. **不要指望从 GlassExplorer 拿到任何公式**。它连自己的绘制代码都没有。若我方目标是复刻 Apple 的效果，需要的是"行为观测"（逐参数截图比对），而不是"代码阅读"。

---

## 附录 A：逐字源码摘录

### A.1 GlassExplorer 内的全部"关键私有 API"声明（逐字）

`_UIViewGlass.h:8-48`：

```objc
@interface _UIViewGlass : NSObject

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

- (id)init;
- (id)initWithVariant:(NSInteger)variant;
- (id)initWithVariant:(NSInteger)variant size:(NSInteger)size;
- (id)initWithVariant:(NSInteger)variant size:(NSInteger)size smoothness:(CGFloat)smoothness;
- (id)initWithVariant:(NSInteger)variant size:(NSInteger)size smoothness:(CGFloat)smoothness state:(NSInteger)state;
- (id)initWithVariant:(NSInteger)variant size:(NSInteger)size smoothness:(CGFloat)smoothness subdued:(BOOL)subdued;
- (id)initWithVariant:(NSInteger)variant size:(NSInteger)size state:(NSInteger)state;
- (id)initWithVariant:(NSInteger)variant smoothness:(CGFloat)smoothness;
- (id)initWithVariant:(NSInteger)variant smoothness:(CGFloat)smoothness state:(NSInteger)state;
- (id)initWithVariant:(NSInteger)variant state:(NSInteger)state;

- (void)setAdaptiveFixedLuminance:(CGFloat)luminance;
- (void)setAdaptiveInitialLuminance:(CGFloat)luminance;
- (void)setAdaptive:(BOOL)adpative;
- (void)setAdaptiveStyle:(NSInteger)adaptiveStyle;

- (id)_resolvedMaterialUsingTraitCollection:(UITraitCollection *)traitCollection size:(CGSize)size;

@end
```

`GlassConfiguration`（`ConfigurationView.swift:85-103`）：

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

> **【源码】** 这些默认值就是 GlassExplorer 启动时的初始状态。

### A.2 参考实现：`AndroidLiquidGlass` 的 AGSL shader（**逐字**，来自另一个仓库）

> **来源声明**：以下代码**不属于 GlassExplorer**。它逐字摘自
> `D:\纯c图形基座\build\recon\ref_kyant\AndroidLiquidGlass.tar\AndroidLiquidGlass-kmp\backdrop\src\commonMain\kotlin\com\kyant\backdrop\internal\Shaders.kt`（共 204 行，Apache-2.0，`Shaders.kt:1-15` 为许可证头）。
> 之所以摘录，是因为 GlassExplorer 内 **不存在任何 shader 源码**（见 2.2），而这份代码是 GlassExplorer 所开关效果唯一的开源对照实现。
> **逐字摘录，未作任何改写**（仅去掉了 Kotlin 的 `$RoundedRectSDF` 字符串插值，改为在下方单独给出被插入的 `RoundedRectSDF` 原文，原文位于 `Shaders.kt:22-48`）。

**被复用的 SDF 工具段（`Shaders.kt:22-48`，逐字）：**

```
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

**折射 shader（`Shaders.kt:51-84`，逐字；`$RoundedRectSDF` 处插入上面那段）：**

```
uniform shader content;

uniform float2 size;
uniform float2 offset;
uniform float4 cornerRadii;
uniform float refractionHeight;
uniform float refractionAmount;
uniform float depthEffect;

<RoundedRectSDF 原文>

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
}
```

**带色散的折射 shader（`Shaders.kt:87-158`，逐字；`$RoundedRectSDF` 处插入同一段）：**

```
uniform shader content;

uniform float2 size;
uniform float2 offset;
uniform float4 cornerRadii;
uniform float refractionHeight;
uniform float refractionAmount;
uniform float depthEffect;
uniform float chromaticAberration;

<RoundedRectSDF 原文>

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
}
```

**边缘高光 shader（`Shaders.kt:161-181`，逐字）：**

```
uniform float2 size;
uniform float4 cornerRadii;
layout(color) uniform half4 color;
uniform float angle;
uniform float falloff;

<RoundedRectSDF 原文>

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

**环境高光 shader（`Shaders.kt:184-204`，逐字）：**

```
uniform float2 size;
uniform float4 cornerRadii;
uniform float angle;
uniform float falloff;

<RoundedRectSDF 原文>

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

**这些 shader 的 uniform 是如何被赋值的（`effects\Lens.kt:44-54`，逐字）——注意 `refractionAmount` 取负号：**

```kotlin
            shader.apply {
                setFloatUniform("size", size.width, size.height)
                setFloatUniform("offset", -padding, -padding)
                setFloatUniform("cornerRadii", cornerRadii)
                setFloatUniform("refractionHeight", refractionHeight)
                setFloatUniform("refractionAmount", -refractionAmount)
                setFloatUniform("depthEffect", if (depthEffect) 1f else 0f)
                if (chromaticAberration) {
                    setFloatUniform("chromaticAberration", 1f)
                }
            }
```

**【源码】** 演示 app 里实际使用的参数值（可作我方初值参考）：

| 用法 | 来源 | 参数 |
| --- | --- | --- |
| 按钮 | `LiquidButton.kt:60-61` | `blur(2.dp)` + `lens(12.dp, 24.dp)` |
| 底部标签栏 | `LiquidBottomTabs.kt:171-172` | `blur(8.dp)` + `lens(24.dp, 24.dp)` |
| 开关（随进度） | `LiquidToggle.kt:160-164` | `blur(8.dp * (1 - progress))` + `lens(..., chromaticAberration = true)` |
| 滑块（随进度） | `LiquidSlider.kt:169-173` | `blur(8.dp * (1 - progress))` + `lens(..., chromaticAberration = true)` |

**【源码】** 高光默认值（`highlight\HighlightStyle.kt:33-50, 74-76`）：

```kotlin
    data class Plain(
        override val color: Color = Color.White.copy(alpha = 0.38f),
        override val blendMode: BlendMode = BlendMode.Plus
    ) : HighlightStyle {
```
```kotlin
    data class Default(
        override val color: Color = Color.White.copy(alpha = 0.5f),
        override val blendMode: BlendMode = BlendMode.Plus,
        val angle: Float = 45f,
        @param:FloatRange(from = 0.0) val falloff: Float = 1f
    ) : HighlightStyle {
```
```kotlin
    data class Ambient(
        @param:FloatRange(from = 0.0, to = 1.0) val intensity: Float = 0.38f
    ) : HighlightStyle {
```

---

## 附录 B：「未找到」清单（明确不编造）

以下问题在本仓库中**确实找不到答案**，逐条列出以免被误读为遗漏：

| # | 问题 | 状态 | 依据 |
| --- | --- | --- | --- |
| B1 | `.kt` 文件 | **未找到**（0 个） | `glob **/*.kt` → No files found |
| B2 | GLSL / AGSL / MSL shader 源码 | **未找到**（0 处） | 全树 grep shader 关键词 → No matches found |
| B3 | 模糊半径公式 | **未找到** | 只有 `inputBlurRadius` 等参数名与范围（`ConfigurationView.swift:241-251`），无公式 |
| B4 | `variant` 0…10 各自对应什么外观 | **未找到** | `_UIViewGlass.h:10` 只有 `NSInteger` 类型，无枚举定义 |
| B5 | `size` 0…5 的语义 | **未找到** | `_UIViewGlass.h:11` |
| B6 | `subvariant` 的合法字符串 | **未找到** | `_UIViewGlass.h:16`；UI 未暴露 |
| B7 | `_flexVariant` 0…5 的语义 | **未找到** | `_UIViewGlass.h:25` |
| B8 | `identifier` 的用途 | **未找到** | `_UIViewGlass.h:12` |
| B9 | `state` / `subdued:` 的枚举取值 | **未找到** | `_UIViewGlass.h:34-39` 只声明了 `NSInteger` / `BOOL` |
| B10 | 形态融合（metaball / SDF smin）算法 | **未找到** | 只有 `allowsGrouping` 布尔量（`_UIViewGlass.h:27`）；全树 grep `smin\|smoothMin\|metaball` → 0 命中 |
| B11 | 镜面高光的具体计算公式 | **未找到** | 只有 `highlightsDisplayAngle` 布尔量（`_UIViewGlass.h:18`） |
| B12 | backdrop 缩放参数 | **未找到**（不存在独立缩放参数） | 18 个 filter key 逐条核对（`ConfigurationView.swift:233-252`）无 scale/zoom |
| B13 | 多尺度模糊 5 层的确切混合方式 | **未找到**（仅由 `Distance0..4`+`Opacity0..4` 成对推断） | `ConfigurationView.swift:241-250` |
| B14 | `bleed` 三参数的视觉语义 | **未找到**（推断为向外渗出） | `ConfigurationView.swift:238-240` |
| B15 | 动画态下玻璃参数的变化规律 | **未找到**（本仓库对玻璃参数做硬切换，无动画） | `ConfigurationView.swift:293` 先置 `nil` 再重建 |
| B16 | backdrop layer 树各层的真实类名 | **未找到**（变量名是作者自取，非系统类名） | `ConfigurationView.swift:127-135` |
| B17 | `updateProperties()` 的触发时机 | **未找到**（仓库无注释）。**【推断】** 是 iOS 26 `UIViewController` 的属性更新生命周期回调 | `ConfigurationView.swift:292` |

---

## 附录 C：可复现的检索命令

本报告所有"未找到"结论均由以下检索支撑，可原样复跑：

```powershell
# 1. 确认无 .kt 文件
Get-ChildItem "D:\纯c图形基座\build\recon\ref_kyant\GlassExplorer.tar" -Recurse -File -Filter *.kt

# 2. 确认无 shader 文件
Get-ChildItem "D:\纯c图形基座\build\recon\ref_kyant\GlassExplorer.tar" -Recurse -File |
  Where-Object { $_.Extension -in '.metal','.glsl','.frag','.vert','.agsl','.shader' }

# 3. 确认无 shader 关键词
#    （用 grep 工具，pattern = shader|glsl|metal|agsl|fragment|uniform|vec2|half4|float4|renderEffect|graphicsLayer|createBlur）
#    -> No matches found

# 4. 确认 AndroidLiquidGlass 无 metaball 融合
#    （grep 工具，pattern = (?i)smin|smoothMin|metaball|smooth.?minimum|liquidMerge|blend.*shape，include=*.kt）
#    -> No matches found
```
