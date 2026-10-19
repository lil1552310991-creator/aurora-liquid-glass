// =============================================================================
//  Liquid Glass Inspector —— 参数模型（纯逻辑，零 Android / ImGui 依赖）
//
//  为什么单独一层：调试工作台要能把"渲染实现里真正存在的字段"暴露成可读写、
//  可序列化、可撤销的参数。这一层只描述**参数长什么样**与**值怎么存**，
//  不关心谁来画控件、也不关心渲染对象是什么类型 —— 因此可以在 PC 上离线回归。
//
//  ★ 一条硬约束：**描述符里的值域必须来自算法本身**（GlassConfig.h 的常量、
//    shader uniform 的实际读取范围、或调用点的 clamp），不能为了"看起来能拖"
//    随便给个 0..100。凡是实现里会被 clamp 掉的上限，都在 clamp 处取值。
// =============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aurora {
namespace glass {

// -----------------------------------------------------------------------------
// 参数类型：决定用哪种控件、以及绑定到渲染字段时的 C++ 类型
// -----------------------------------------------------------------------------
enum class ParamKind : std::uint8_t {
  Float,   // float*      → 进度条 / 数值输入
  Int,     // int*        → 整数进度条 / 数值输入
  Bool,    // bool*       → 开关
  Enum,    // int*        → 选项菜单（enumLabels 给名字）
  Color,   // float[4]    → 颜色编辑器（RGBA，0..1，与 ImGui ColorEdit4 同域）
};

const char* ParamKindName(ParamKind kind);

// -----------------------------------------------------------------------------
// 修改该参数的性能成本 —— 决定更新策略（见 Registry::Commit）
//
//  这不是"估计"，而是按渲染路径实测分类：
//    Uniform     只写进 PanelStyle，随后被 appendJob 拷成 uniform ⇒ 当帧生效，零额外成本
//    EffectChain 会改变效果链**结构**（多一个投影面板 / 圆角 SDF / 采样区间）⇒ 帧边界提交
//    Pipeline    会改变采样来源或重建采集/离屏目标（模糊 gather、背景源开关）⇒ 帧边界提交 + 合并
// -----------------------------------------------------------------------------
enum class ParamCost : std::uint8_t {
  Uniform,
  EffectChain,
  Pipeline,
};

const char* ParamCostName(ParamCost cost);

// 参数来源：值是被谁改的。主窗口改 = Ui；二级窗口组件自己改 = Component（双向绑定要用）
enum class ChangeOrigin : std::uint8_t { Ui, Component, Preset, Reset, Test };

const char* ChangeOriginName(ChangeOrigin origin);

// -----------------------------------------------------------------------------
// 参数值：一个标签联合。故意做成 POD 风格（拷贝便宜、能进 static 表）
// -----------------------------------------------------------------------------
struct ParamValue {
  ParamKind kind = ParamKind::Float;
  float f = 0.0f;
  int i = 0;
  bool b = false;
  float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};

  ParamValue() = default;

  static ParamValue OfFloat(float v) {
    ParamValue out;
    out.kind = ParamKind::Float;
    out.f = v;
    return out;
  }
  static ParamValue OfInt(int v) {
    ParamValue out;
    out.kind = ParamKind::Int;
    out.i = v;
    return out;
  }
  static ParamValue OfBool(bool v) {
    ParamValue out;
    out.kind = ParamKind::Bool;
    out.b = v;
    return out;
  }
  static ParamValue OfEnum(int v) {
    ParamValue out;
    out.kind = ParamKind::Enum;
    out.i = v;
    return out;
  }
  static ParamValue OfColor(float r, float g, float b, float a) {
    ParamValue out;
    out.kind = ParamKind::Color;
    out.rgba[0] = r;
    out.rgba[1] = g;
    out.rgba[2] = b;
    out.rgba[3] = a;
    return out;
  }

  // 数值语义下的标量（Bool 取 0/1，Color 取 R）—— 只用于比较/日志，不用于写回
  float Scalar() const;
  bool Equals(const ParamValue& other, float epsilon = 1.0e-6f) const;
  std::string ToText() const;   // 供快照/日志/导出用（人类可读，不是 JSON）
};

// -----------------------------------------------------------------------------
// 参数描述符：调试 UI 的全部元数据都来自这里
// -----------------------------------------------------------------------------
struct ParamDesc {
  std::string id;        // 稳定标识（同 target 内唯一），也是持久化键的一部分
  std::string cn;        // 中文名称
  std::string en;        // 英文原始名称（尽量与 ytbl/Kyant 的字段名一致）
  std::string group;     // 分组（形状 / 折射 / 模糊 / 色彩 / 表面 / 光影 / 交互 / 背景 …）
  std::string unit;      // "dp" / "sp" / "px" / "×" / "" …
  std::string help;      // 一句话说明（含"这个值在源码哪里"的线索）
  std::string constraint;// 与其他参数的约束关系（例如"仅在 !capsule 时生效"）

  ParamKind kind = ParamKind::Float;
  ParamCost cost = ParamCost::Uniform;

  float minF = 0.0f;
  float maxF = 1.0f;
  float stepF = 0.01f;

  int minI = 0;
  int maxI = 1;
  int stepI = 1;

  std::string enumLabels;   // Enum 专用：'\0' 分隔的名字表（ImGui::Combo 格式）

  bool realtime = true;     // 是否支持实时更新（false ⇒ 需要重建/下一帧才生效）

  // 实现里没有这条能力（或平台无法安全动态修改）时置 false：
  // UI 会把它画成**只读行并显示原因**，绝不做一个转了没反应的假滑块。
  bool supported = true;
  std::string unsupportedReason;

  bool IsNumeric() const {
    return kind == ParamKind::Float || kind == ParamKind::Int || kind == ParamKind::Enum;
  }
};

// -----------------------------------------------------------------------------
// 参数槽：描述符 + 默认值 + **指向真实渲染字段的指针**
//
//  ★ 关键设计：ref 直接指向渲染实现里那个字段（例如 lgx::Toggle::tune 的成员）。
//    "调试 UI 显示的值"与"渲染器读到的值"因此是**同一块内存**，
//    结构上不可能出现"滑块动了但渲染没变"或"显示值与实际值不一致"。
//
//  ★ applyRef 用于**成本高**的参数（Pipeline / 部分 EffectChain）：
//    UI 改的是 ref（请求值），渲染读的是 applyRef（生效值），
//    Commit() 在帧边界把请求值拷进生效值 ⇒ 高频拖动只应用最新一次。
//    applyRef == nullptr 表示"当帧直接生效"（绝大多数参数都是这样）。
// -----------------------------------------------------------------------------
struct ParamSlot {
  ParamDesc desc;
  ParamValue def;              // 注册时从 ref 现场捕获 ⇒ reset 一定回到初始配置
  void* ref = nullptr;         // 真实字段
  void* applyRef = nullptr;    // 延迟生效字段（nullptr ⇒ 与 ref 同一处）
  std::uint32_t index = 0;     // 在 target 内的序号
  std::uint32_t groupIndex = 0;

  ParamValue Read() const;
  void Write(const ParamValue& value);   // 已校验/夹紧
};

// 一个分组（参数面板里的折叠块）：连续同 group 的参数归为一组
struct ParamGroup {
  std::string name;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};

// -----------------------------------------------------------------------------
// 调试目标：一个可以被独立读写的液态玻璃对象（或其子层）
// -----------------------------------------------------------------------------
struct TargetDesc {
  std::string id;       // 稳定唯一 ID，例如 secondary_window.toggle_1.thumb
  std::string name;     // 显示名（中文），例如 "开关 1 · 圆钮"
  std::string type;     // 组件类型：Window / PillButton / Toggle / Slider / Navigation / ...
  std::string window;   // 所属窗口：primary / secondary
  std::string parent;   // 父目标 ID（空 = 顶层）
  std::string note;     // 说明（出现在分页标题的提示里）
  int order = 0;        // 分页顺序（小的在前）
  bool live = true;     // 是否支持实时修改
  bool dynamic = true;  // 是否由渲染端动态注册（会被 Sweep 回收）
};

struct Target {
  TargetDesc desc;
  std::vector<ParamSlot> params;
  std::vector<ParamGroup> groups;
  std::int64_t lastSeenFrame = -1;

  const ParamSlot* FindParam(const std::string& id) const;
  ParamSlot* FindParam(const std::string& id);
  float GetF(const std::string& id, float fallback) const;
  int GetI(const std::string& id, int fallback) const;
  bool GetB(const std::string& id, bool fallback) const;
};

// -----------------------------------------------------------------------------
// 变化记录 / 快照
// -----------------------------------------------------------------------------
struct ParamChange {
  std::string target;
  std::string param;
  ParamValue value;
  ParamValue previous;
  ChangeOrigin origin = ChangeOrigin::Ui;
  std::uint64_t version = 0;
};

using ChangeSink = void (*)(void* user, const ParamChange& change);

struct SnapshotEntry {
  std::string target;
  std::string param;
  ParamValue value;
};

// 参数快照：getSnapshot / applySnapshot 的载体，也是预设与撤销重做的基本单位
struct Snapshot {
  std::vector<SnapshotEntry> entries;

  std::size_t size() const { return entries.size(); }
  bool empty() const { return entries.empty(); }
  const SnapshotEntry* Find(const std::string& target, const std::string& param) const;
};

// -----------------------------------------------------------------------------
// 校验与夹紧：所有写入口都必须过这里
//
//  · 浮点 NaN/Inf ⇒ 回落默认值
//  · 数值 ⇒ 夹进 [min,max]，并按 step 吸附（step<=0 不吸附）
//  · 整数/枚举 ⇒ 夹进 [minI,maxI]
//  · 颜色 ⇒ 逐通道夹进 [0,1]
//  返回 false 表示输入非法、已按上面规则修正（调用方据此决定要不要记日志）。
// -----------------------------------------------------------------------------
bool Sanitize(const ParamDesc& desc, ParamValue& value);

// 归一化 ↔ 物理值：进度条用的是 0..1，实际算法用的是 min..max
float Normalize(const ParamDesc& desc, const ParamValue& value);
ParamValue Denormalize(const ParamDesc& desc, float t);

// 值域标签（给 UI 显示 "0.0 ~ 10.0 dp" 这类提示）
std::string RangeText(const ParamDesc& desc);

}  // namespace glass
}  // namespace aurora
