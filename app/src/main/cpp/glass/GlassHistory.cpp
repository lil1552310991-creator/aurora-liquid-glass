#include "GlassHistory.h"

#include <chrono>

namespace aurora {
namespace glass {

History::History(Registry& registry, std::size_t limit)
    : registry_(registry), limit_(limit == 0 ? 1 : limit) {}

double History::NowMs() {
  using namespace std::chrono;
  return static_cast<double>(
             duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()) /
         1000.0;
}

void History::Clear() {
  undo_.clear();
  redo_.clear();
  batching_ = false;
  batchLabel_.clear();
  coalesced_ = 0;
  ++revision_;
}

void History::BeginBatch(const std::string& label) {
  batching_ = true;
  batchLabel_ = label;
}

void History::EndBatch() {
  if (batching_ && !undo_.empty() && undo_.back().batch) {
    undo_.back().batch = true;
    if (!batchLabel_.empty()) undo_.back().label = batchLabel_;
    undo_.back().stampMs = 0.0;   // 批次结束后不再与后续修改合并
  }
  batching_ = false;
  batchLabel_.clear();
}

void History::Record(const ParamChange& change) {
  if (replaying_) return;
  const double now = NowMs();

  Change item;
  item.target = change.target;
  item.param = change.param;
  item.before = change.previous;
  item.after = change.value;

  if (!undo_.empty()) {
    Step& top = undo_.back();
    if (batching_ && top.batch) {
      // 批次内：同一参数覆盖上一次，不同参数追加 ⇒ 整批一个步骤
      for (Change& existing : top.changes) {
        if (existing.target == item.target && existing.param == item.param) {
          existing.after = item.after;
          lastStampMs_ = now;
          ++coalesced_;
          ++revision_;
          return;
        }
      }
      top.changes.push_back(std::move(item));
      lastStampMs_ = now;
      ++revision_;
      return;
    }
    if (!top.batch && top.changes.size() == 1 && coalesceMs_ > 0.0 &&
        (now - lastStampMs_) <= coalesceMs_ && top.changes.front().target == item.target &&
        top.changes.front().param == item.param) {
      // 拖动合并：同一个参数、时间窗口内 ⇒ 只更新终值，起点保持不变
      top.changes.front().after = item.after;
      lastStampMs_ = now;
      ++coalesced_;
      ++revision_;
      return;
    }
  }

  Step step;
  step.changes.push_back(std::move(item));
  step.label = change.target + "." + change.param;
  step.stampMs = now;
  step.batch = batching_;
  if (step.batch && !batchLabel_.empty()) step.label = batchLabel_;
  lastStampMs_ = now;
  undo_.push_back(std::move(step));
  if (undo_.size() > limit_) {
    undo_.erase(undo_.begin(), undo_.begin() + static_cast<std::ptrdiff_t>(undo_.size() - limit_));
  }
  redo_.clear();
  ++revision_;
}

bool History::Replay(const Step& step, bool forward) {
  const bool wasReplaying = replaying_;
  replaying_ = true;
  bool ok = true;
  if (forward) {
    for (const Change& change : step.changes) {
      // 回放走 Registry 的正常写入口 ⇒ 校验/夹紧/订阅通知/帧边界提交全部照旧。
      // 撤销也必须经过同一条渲染路径，不能绕过任何一层。
      if (!registry_.Set(change.target, change.param, change.after, ChangeOrigin::Reset)) {
        ok = false;
      }
    }
  } else {
    // 倒序回退：同一个参数在一批里只出现一次，但倒序更符合直觉且对约束联动更安全
    for (std::size_t i = step.changes.size(); i-- > 0;) {
      const Change& change = step.changes[i];
      if (!registry_.Set(change.target, change.param, change.before, ChangeOrigin::Reset)) {
        ok = false;
      }
    }
  }
  replaying_ = wasReplaying;
  return ok;
}

bool History::Undo() {
  if (undo_.empty()) return false;
  Step step = std::move(undo_.back());
  undo_.pop_back();
  Replay(step, /*forward=*/false);
  redo_.push_back(std::move(step));
  ++revision_;
  return true;
}

bool History::Redo() {
  if (redo_.empty()) return false;
  Step step = std::move(redo_.back());
  redo_.pop_back();
  Replay(step, /*forward=*/true);
  undo_.push_back(std::move(step));
  ++revision_;
  return true;
}

const std::string& History::LastLabel() const {
  return undo_.empty() ? empty_ : undo_.back().label;
}

const std::string& History::NextLabel() const {
  return redo_.empty() ? empty_ : redo_.back().label;
}

std::size_t History::LastChangeCount() const {
  return undo_.empty() ? 0 : undo_.back().changes.size();
}

}  // namespace glass
}  // namespace aurora
