// =============================================================================
//  撤销 / 重做（纯逻辑，零 Android / ImGui 依赖）
//
//  ★ 一个"步骤"是**一组改动**，不是一个改动。这是必须的：
//    · 拖一次滑块 = 同一个参数的上百次写入 ⇒ 必须合并成一步；
//    · "恢复默认" / 切换预设 = 几十个参数同时变 ⇒ 也必须是一步；
//    两者用同一种数据结构（Step{changes[]}）表达，回放时才不会只回退一半。
//
//  ★ 合并判据是"同一个参数 + 时间窗口"，而不是靠 UI 显式 begin/end ——
//    UI 一旦漏调 begin/end 就会毁掉整条历史，而时间窗口是自愈的。
// =============================================================================
#pragma once

#include "GlassRegistry.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aurora {
namespace glass {

class History {
 public:
  explicit History(Registry& registry, std::size_t limit = 160);

  // 由 registry 的订阅回调喂进来
  void Record(const ParamChange& change);

  bool CanUndo() const { return !undo_.empty(); }
  bool CanRedo() const { return !redo_.empty(); }
  bool Undo();
  bool Redo();

  std::size_t UndoDepth() const { return undo_.size(); }
  std::size_t RedoDepth() const { return redo_.size(); }
  void Clear();

  const std::string& LastLabel() const;
  const std::string& NextLabel() const;
  // 上一批/上一步影响的参数个数（给 UI 显示"撤销 恢复默认（共 37 项）"）
  std::size_t LastChangeCount() const;

  // 把接下来的若干次修改强行并成一步（预设切换、整目标重置这类"一次操作"）
  void BeginBatch(const std::string& label);
  void EndBatch();
  bool Batching() const { return batching_; }

  std::size_t CoalescedCount() const { return coalesced_; }
  // 合并窗口（毫秒）。★ <= 0 表示**关闭合并**（每次都独立成步）——
  // 不能只靠"窗口 = 0"来表达关闭：同一微秒内的两次写入差值恰好是 0，
  // 0 <= 0 会成立，于是合并照旧发生（离线回归抓过这个坑）。
  void SetCoalesceWindowMs(double ms) { coalesceMs_ = ms; }
  std::uint64_t Revision() const { return revision_; }

 private:
  struct Change {
    std::string target;
    std::string param;
    ParamValue before;
    ParamValue after;
  };
  struct Step {
    std::vector<Change> changes;
    std::string label;
    double stampMs = 0.0;
    bool batch = false;
  };

  static double NowMs();
  bool Replay(const Step& step, bool forward);

  Registry& registry_;
  std::size_t limit_;
  std::vector<Step> undo_;
  std::vector<Step> redo_;
  std::string empty_;
  bool replaying_ = false;
  bool batching_ = false;
  std::string batchLabel_;
  double lastStampMs_ = 0.0;
  double coalesceMs_ = 600.0;
  std::size_t coalesced_ = 0;
  std::uint64_t revision_ = 0;
};

}  // namespace glass
}  // namespace aurora
