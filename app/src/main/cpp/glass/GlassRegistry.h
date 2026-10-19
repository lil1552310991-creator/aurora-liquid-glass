// =============================================================================
//  Liquid Glass Inspector —— 组件注册表 + 参数总线（纯逻辑，零 Android / ImGui 依赖）
//
//  职责（与任务书 §5 的接口语义一一对应）：
//    enumerateTargets / enumerateParameters / getParameter / setParameter /
//    resetParameter / resetTarget / resetAll / getSnapshot / applySnapshot /
//    subscribeChanges
//
//  设计要点：
//   ① 值是**指针绑定**到渲染实现里的真实字段的 ⇒ 显示值与渲染值结构上是同一块内存，
//      不可能出现"只改 UI 不改 shader"。
//   ② 高频写入走一个**合并队列**（同一个 (target,param) 只保留最新一次），
//      帧边界 Commit() 统一应用 ⇒ UI 线程绝不等待 GPU，也不会把中间值全部提交。
//   ③ 目标用 unique_ptr 存储 ⇒ 地址稳定，pending 队列里可以安全持有指针；
//      动态目标可以被 Sweep 回收，不影响其它目标。
// =============================================================================
#pragma once

#include "GlassParam.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aurora {
namespace glass {

// 参数面板里的"源码位置"提示：把 descriptor 与实现文件/行对上，
// 最终报告要用它生成《参数 ↔ 代码位置》对照表。
struct SourceRef {
  std::string file;   // 相对仓库根，例如 app/src/main/cpp/.../Toggle.cpp
  std::string symbol; // 常量名或字段名，例如 TOGGLE_LENS_HEIGHT_DP
};
void SetSourceRef(const std::string& target, const std::string& param, const SourceRef& ref);
const SourceRef* GetSourceRef(const std::string& target, const std::string& param);

// -----------------------------------------------------------------------------
// 写参数的句柄：把一个 Target 的参数批量绑定到渲染字段上
//
//  用法：
//      TargetBuilder b(reg, {"secondary_window.toggle_1.thumb", ...});
//      if (b.created()) {                    // 只在第一次注册时建表
//        b.Group("折射");
//        b.Float("lensHeightDp", "折射高度", "lensHeight", &tune.lensHeightDp,
//                0.0f, 40.0f, "dp", "TOGGLE_LENS_HEIGHT_DP");
//      }
// -----------------------------------------------------------------------------
class Registry;

class TargetBuilder {
 public:
  TargetBuilder(Registry& registry, const TargetDesc& desc);

  bool created() const { return created_; }
  Target& target() { return *target_; }

  TargetBuilder& Group(const char* name);

  TargetBuilder& Float(const char* id, const char* cn, const char* en, float* ref,
                       float lo, float hi, const char* unit, const char* help,
                       float step = 0.01f, ParamCost cost = ParamCost::Uniform,
                       const char* constraint = nullptr);

  TargetBuilder& Int(const char* id, const char* cn, const char* en, int* ref,
                     int lo, int hi, const char* unit, const char* help,
                     ParamCost cost = ParamCost::Uniform, const char* constraint = nullptr);

  TargetBuilder& Bool(const char* id, const char* cn, const char* en, bool* ref,
                      const char* help, ParamCost cost = ParamCost::Uniform,
                      const char* constraint = nullptr);

  TargetBuilder& Enum(const char* id, const char* cn, const char* en, int* ref, int lo, int hi,
                      const char* labels, const char* help,
                      ParamCost cost = ParamCost::Uniform, const char* constraint = nullptr);

  TargetBuilder& Color(const char* id, const char* cn, const char* en, float* ref,
                       const char* help, ParamCost cost = ParamCost::Uniform,
                       const char* constraint = nullptr);

  // 声明一条"参考项目有、本实现没有（或不能安全动态改）"的能力。
  // UI 会画成只读行 + 原因 ⇒ 满足"不得提供无作用的假控件"。
  TargetBuilder& Unsupported(const char* id, const char* cn, const char* en,
                             const char* reason, const char* group = nullptr);

  // 与该参数绑定的源码位置（用于生成对照表）
  TargetBuilder& Source(const char* file, const char* symbol);

  // 把**刚刚登记的那个参数**标记为"帧边界才生效"：
  //   applyRef 指向渲染端真正读的字段，UI 改的是 ref（请求值）。
  //   注册表在 Commit() 时把请求值拷进生效值 ⇒ 高频拖动只应用最新一次。
  //   只对 cost = Pipeline / EffectChain 的参数使用（普通 uniform 不需要）。
  TargetBuilder& Deferred(void* applyRef);

 private:
  ParamSlot& Push(ParamDesc desc, void* ref);
  Registry& registry_;
  Target* target_ = nullptr;
  bool created_ = false;
  std::uint32_t groupIndex_ = 0;
};

// -----------------------------------------------------------------------------
// Registry
// -----------------------------------------------------------------------------
class Registry {
 public:
  Registry();
  ~Registry();
  Registry(const Registry&) = delete;
  Registry& operator=(const Registry&) = delete;

  // ---- 注册 / 注销 ----
  Target* AddTarget(const TargetDesc& desc, bool* created);
  const Target* FindTarget(const std::string& id) const;
  Target* MutableTarget(const std::string& id);
  int IndexOfTarget(const std::string& id) const;
  bool RemoveTarget(const std::string& id);
  void Clear();

  std::size_t TargetCount() const { return targets_.size(); }
  const Target& TargetAt(std::size_t index) const { return *targets_[index]; }
  Target& MutableTargetAt(std::size_t index) { return *targets_[index]; }

  // ---- 检索（按 ID / 名称 / 类型 / 所属窗口 / 关键词）----
  std::vector<const Target*> FindByType(const std::string& type) const;
  std::vector<const Target*> FindByWindow(const std::string& window) const;
  std::vector<const Target*> ChildrenOf(const std::string& parent) const;
  std::vector<const Target*> Search(const std::string& keyword) const;
  // 按 order 再按 ID 排序后的索引序列（分页栏用的稳定顺序）
  const std::vector<std::size_t>& DisplayOrder();

  // ---- 参数读写 ----
  std::size_t ParamCount(std::size_t targetIndex) const;
  const ParamSlot* ParamAt(std::size_t targetIndex, std::size_t paramIndex) const;

  ParamValue Get(const std::string& target, const std::string& param) const;
  bool Set(const std::string& target, const std::string& param, const ParamValue& value,
           ChangeOrigin origin = ChangeOrigin::Ui);
  bool SetRaw(const std::string& target, const std::string& param, const ParamValue& value,
              ChangeOrigin origin);   // 不做夹紧（仅供导入/测试显式使用）
  bool SetF(const std::string& target, const std::string& param, float value,
            ChangeOrigin origin = ChangeOrigin::Ui);
  bool SetI(const std::string& target, const std::string& param, int value,
            ChangeOrigin origin = ChangeOrigin::Ui);
  bool SetB(const std::string& target, const std::string& param, bool value,
            ChangeOrigin origin = ChangeOrigin::Ui);
  bool SetColor(const std::string& target, const std::string& param, float r, float g,
                float b, float a, ChangeOrigin origin = ChangeOrigin::Ui);

  // ---- 重置 ----
  bool ResetParam(const std::string& target, const std::string& param,
                  ChangeOrigin origin = ChangeOrigin::Reset);
  bool ResetTarget(const std::string& target, ChangeOrigin origin = ChangeOrigin::Reset);
  bool ResetGroup(const std::string& target, const std::string& group,
                  ChangeOrigin origin = ChangeOrigin::Reset);
  std::size_t ResetAll(ChangeOrigin origin = ChangeOrigin::Reset);

  // ---- 快照 ----
  Snapshot Capture() const;
  Snapshot CaptureTarget(const std::string& target) const;
  Snapshot CaptureGroup(const std::string& target, const std::string& group) const;
  Snapshot CaptureDiffFromDefault() const;
  // 注册时捕获的默认值（"恢复出厂"用的那一份）。
  // ★ 它不依赖任何时刻的现场，因此与"什么时候创建预设仓库"无关。
  Snapshot CaptureDefaults() const;
  // 应用快照。
  //   skipped  —— **没能应用**的条目（目标/参数不存在，或参数不支持）
  //   adjusted —— 应用了、但数值被夹紧的条目（不是失败，是需要告诉用户）
  // 两者分开是刻意的：把"被夹紧"混进"被跳过"会让用户以为配置没生效。
  std::size_t Apply(const Snapshot& snapshot, ChangeOrigin origin = ChangeOrigin::Preset,
                    std::vector<std::string>* skipped = nullptr,
                    std::vector<std::string>* adjusted = nullptr);

  // ---- 订阅 ----
  void Subscribe(ChangeSink sink, void* user);
  void Unsubscribe(ChangeSink sink, void* user);
  std::size_t SubscriberCount() const { return subscribers_.size(); }

  // ---- 帧边界与合并队列 ----
  void BeginFrame();
  std::int64_t Frame() const { return frame_; }
  std::size_t Commit();                       // 返回本次应用（含合并）的参数个数
  std::size_t PendingCount() const { return pending_.size(); }
  std::uint64_t EditVersion() const { return editVersion_; }
  std::uint64_t AppliedVersion() const { return appliedVersion_; }
  std::uint64_t MergedUpdates() const { return mergedUpdates_; }
  bool FullyApplied() const { return pending_.empty(); }

  // ---- 动态发现 ----
  void MarkSeen(const std::string& id);
  std::size_t Sweep(int keepFrames = 4);

  // ---- 统计 ----
  std::size_t ParamTotal() const;
  std::size_t ChangedFromDefault() const;
  std::size_t UnsupportedCount() const;

 private:
  struct PendingEntry {
    Target* target = nullptr;
    std::uint32_t paramIndex = 0;
    std::uint64_t uid = 0;
  };
  struct Subscriber {
    ChangeSink sink = nullptr;
    void* user = nullptr;
    bool operator==(const Subscriber& other) const {
      return sink == other.sink && user == other.user;
    }
  };

  ParamSlot* SlotOf(const std::string& target, const std::string& param, int* targetIndex);
  void Notify(const ParamChange& change);
  void MarkDirty(Target& target, ParamSlot& slot);
  void InvalidateDisplayOrder() { displayOrderDirty_ = true; }

  std::vector<std::unique_ptr<Target>> targets_;
  std::unordered_map<std::string, Target*> byId_;
  std::vector<PendingEntry> pending_;
  std::unordered_map<std::uint64_t, std::size_t> pendingIndex_;
  std::vector<Subscriber> subscribers_;
  std::vector<std::size_t> displayOrder_;
  bool displayOrderDirty_ = true;
  std::int64_t frame_ = 0;
  std::uint64_t editVersion_ = 0;
  std::uint64_t appliedVersion_ = 0;
  std::uint64_t mergedUpdates_ = 0;
  std::uint64_t nextUid_ = 1;
};

// 全局实例：界面层与渲染层共用同一个注册表
Registry& Global();

// 源码位置表（独立于 registry 存储，避免 Target 被回收时丢失对照信息）
}  // namespace glass
}  // namespace aurora
