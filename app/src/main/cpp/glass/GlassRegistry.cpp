#include "GlassRegistry.h"

#include <algorithm>
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include <cstring>

namespace aurora {
namespace glass {
namespace {

std::unordered_map<std::string, SourceRef>& SourceTable() {
  static std::unordered_map<std::string, SourceRef> table;
  return table;
}

std::string SourceKey(const std::string& target, const std::string& param) {
  return target + "|" + param;
}

bool ContainsFold(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) return true;
  const std::size_t n = needle.size();
  if (haystack.size() < n) return false;
  for (std::size_t i = 0; i + n <= haystack.size(); ++i) {
    std::size_t k = 0;
    while (k < n) {
      const unsigned char a = static_cast<unsigned char>(haystack[i + k]);
      const unsigned char b = static_cast<unsigned char>(needle[k]);
      const unsigned char la = (a >= 'A' && a <= 'Z') ? static_cast<unsigned char>(a + 32) : a;
      const unsigned char lb = (b >= 'A' && b <= 'Z') ? static_cast<unsigned char>(b + 32) : b;
      if (la != lb) break;
      ++k;
    }
    if (k == n) return true;
  }
  return false;
}

}  // namespace

void SetSourceRef(const std::string& target, const std::string& param, const SourceRef& ref) {
  SourceTable()[SourceKey(target, param)] = ref;
}

const SourceRef* GetSourceRef(const std::string& target, const std::string& param) {
  auto& table = SourceTable();
  auto it = table.find(SourceKey(target, param));
  return it == table.end() ? nullptr : &it->second;
}

// =============================================================================
// TargetBuilder
// =============================================================================
TargetBuilder::TargetBuilder(Registry& registry, const TargetDesc& desc) : registry_(registry) {
  target_ = registry_.AddTarget(desc, &created_);
}

TargetBuilder& TargetBuilder::Group(const char* name) {
  if (target_ == nullptr) return *this;
  const char* label = name != nullptr ? name : "";
  for (std::size_t i = 0; i < target_->groups.size(); ++i) {
    if (target_->groups[i].name == label) {
      groupIndex_ = static_cast<std::uint32_t>(i);
      return *this;
    }
  }
  ParamGroup group;
  group.name = label;
  group.first = static_cast<std::uint32_t>(target_->params.size());
  group.count = 0;
  target_->groups.push_back(group);
  groupIndex_ = static_cast<std::uint32_t>(target_->groups.size() - 1);
  return *this;
}

ParamSlot& TargetBuilder::Push(ParamDesc desc, void* ref) {
  // ★ 兜底：调用方忘了先 Group() 时，自动建一个「常规」分组。
  //   没有这一步，第一个参数会挂在一个不存在的分组上 ⇒ 参数面板里根本不显示它
  //   （这种 bug 很隐蔽：注册成功、读写正常，就是看不见）。
  if (target_->groups.empty()) Group("常规");
  ParamSlot slot;
  slot.desc = std::move(desc);
  slot.ref = ref;
  slot.index = static_cast<std::uint32_t>(target_->params.size());
  slot.groupIndex = groupIndex_;
  // ★ 默认值在注册这一刻从渲染字段现场捕获 ⇒ reset 一定回到"组件自己初始化的值"，
  //   不需要在任何地方重复写一遍默认值（写两遍就会漂）。
  slot.def = slot.Read();
  target_->params.push_back(std::move(slot));
  if (groupIndex_ < target_->groups.size()) {
    target_->groups[groupIndex_].count =
        static_cast<std::uint32_t>(target_->params.size()) - target_->groups[groupIndex_].first;
  }
  return target_->params.back();
}

TargetBuilder& TargetBuilder::Float(const char* id, const char* cn, const char* en, float* ref,
                                    float lo, float hi, const char* unit, const char* help,
                                    float step, ParamCost cost, const char* constraint) {
  if (target_ == nullptr || ref == nullptr) return *this;
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.unit = unit != nullptr ? unit : "";
  desc.help = help != nullptr ? help : "";
  desc.constraint = constraint != nullptr ? constraint : "";
  desc.group = target_->groups.empty() ? "常规" : target_->groups[groupIndex_].name;
  desc.kind = ParamKind::Float;
  desc.cost = cost;
  desc.minF = lo;
  desc.maxF = hi;
  desc.stepF = step;
  desc.realtime = cost == ParamCost::Uniform;
  Push(std::move(desc), ref);
  return *this;
}

TargetBuilder& TargetBuilder::Int(const char* id, const char* cn, const char* en, int* ref,
                                  int lo, int hi, const char* unit, const char* help,
                                  ParamCost cost, const char* constraint) {
  if (target_ == nullptr || ref == nullptr) return *this;
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.unit = unit != nullptr ? unit : "";
  desc.help = help != nullptr ? help : "";
  desc.constraint = constraint != nullptr ? constraint : "";
  desc.group = target_->groups.empty() ? "常规" : target_->groups[groupIndex_].name;
  desc.kind = ParamKind::Int;
  desc.cost = cost;
  desc.minI = lo;
  desc.maxI = hi;
  desc.realtime = cost == ParamCost::Uniform;
  Push(std::move(desc), ref);
  return *this;
}

TargetBuilder& TargetBuilder::Bool(const char* id, const char* cn, const char* en, bool* ref,
                                   const char* help, ParamCost cost, const char* constraint) {
  if (target_ == nullptr || ref == nullptr) return *this;
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.help = help != nullptr ? help : "";
  desc.constraint = constraint != nullptr ? constraint : "";
  desc.group = target_->groups.empty() ? "常规" : target_->groups[groupIndex_].name;
  desc.kind = ParamKind::Bool;
  desc.cost = cost;
  desc.minI = 0;
  desc.maxI = 1;
  desc.realtime = true;   // 开关本身是瞬时的；它引发的重建由 cost 表达
  Push(std::move(desc), ref);
  return *this;
}

TargetBuilder& TargetBuilder::Enum(const char* id, const char* cn, const char* en, int* ref,
                                   int lo, int hi, const char* labels, const char* help,
                                   ParamCost cost, const char* constraint) {
  if (target_ == nullptr || ref == nullptr) return *this;
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.help = help != nullptr ? help : "";
  desc.constraint = constraint != nullptr ? constraint : "";
  desc.group = target_->groups.empty() ? "常规" : target_->groups[groupIndex_].name;
  desc.kind = ParamKind::Enum;
  desc.cost = cost;
  desc.minI = lo;
  desc.maxI = hi;
  desc.enumLabels = labels != nullptr ? labels : "";
  desc.realtime = true;
  Push(std::move(desc), ref);
  return *this;
}

TargetBuilder& TargetBuilder::Color(const char* id, const char* cn, const char* en, float* ref,
                                    const char* help, ParamCost cost, const char* constraint) {
  if (target_ == nullptr || ref == nullptr) return *this;
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.help = help != nullptr ? help : "";
  desc.constraint = constraint != nullptr ? constraint : "";
  desc.group = target_->groups.empty() ? "常规" : target_->groups[groupIndex_].name;
  desc.kind = ParamKind::Color;
  desc.cost = cost;
  desc.realtime = true;
  Push(std::move(desc), ref);
  return *this;
}

TargetBuilder& TargetBuilder::Unsupported(const char* id, const char* cn, const char* en,
                                          const char* reason, const char* group) {
  if (target_ == nullptr) return *this;
  if (group != nullptr) Group(group);
  ParamDesc desc;
  desc.id = id != nullptr ? id : "";
  desc.cn = cn != nullptr ? cn : "";
  desc.en = en != nullptr ? en : "";
  desc.group = target_->groups.empty() ? "缺口" : target_->groups[groupIndex_].name;
  desc.help = reason != nullptr ? reason : "";
  desc.kind = ParamKind::Float;
  desc.minF = 0.0f;
  desc.maxF = 1.0f;
  desc.supported = false;
  desc.realtime = false;
  desc.unsupportedReason = reason != nullptr ? reason : "本实现没有这条能力";
  Push(std::move(desc), nullptr);   // 故意不绑定：它永远不会被写进渲染
  return *this;
}

TargetBuilder& TargetBuilder::Source(const char* file, const char* symbol) {
  if (target_ == nullptr || target_->params.empty()) return *this;
  SourceRef ref;
  ref.file = file != nullptr ? file : "";
  ref.symbol = symbol != nullptr ? symbol : "";
  SetSourceRef(target_->desc.id, target_->params.back().desc.id, ref);
  return *this;
}

TargetBuilder& TargetBuilder::Deferred(void* applyRef) {
  if (target_ == nullptr || target_->params.empty()) return *this;
  ParamSlot& slot = target_->params.back();
  slot.applyRef = applyRef;
  // 生效值必须从当前请求值起步，否则"还没提交过"的那一帧渲染会读到一个未初始化
  // 的值（这个坑很隐蔽：拖动前画面是对的，第一次拖动后才出问题）。
  if (applyRef != nullptr && applyRef != slot.ref) {
    slot.Write(slot.def);   // ref 此刻就是 def（注册时刚捕获过）
    ParamSlot shadow = slot;
    shadow.ref = applyRef;
    shadow.Write(slot.def);
  }
  return *this;
}

// =============================================================================
// Registry
// =============================================================================
Registry::Registry() = default;
Registry::~Registry() = default;

Target* Registry::AddTarget(const TargetDesc& desc, bool* created) {
  auto it = byId_.find(desc.id);
  if (it != byId_.end()) {
    if (created != nullptr) *created = false;
    Target* target = it->second;
    // 名称/顺序可能随语言或布局变化，允许刷新，但**参数表不重建**
    // （重建会丢掉用户已经调好的值）。
    target->desc.name = desc.name;
    target->desc.type = desc.type;
    target->desc.window = desc.window;
    target->desc.parent = desc.parent;
    target->desc.note = desc.note;
    target->desc.order = desc.order;
    target->desc.live = target->desc.live && desc.live;
    target->lastSeenFrame = frame_;
    return target;
  }
  auto owned = std::make_unique<Target>();
  owned->desc = desc;
  owned->lastSeenFrame = frame_;
  Target* raw = owned.get();
  targets_.push_back(std::move(owned));
  byId_[desc.id] = raw;
  InvalidateDisplayOrder();
  if (created != nullptr) *created = true;
  return raw;
}

const Target* Registry::FindTarget(const std::string& id) const {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : it->second;
}

Target* Registry::MutableTarget(const std::string& id) {
  auto it = byId_.find(id);
  return it == byId_.end() ? nullptr : it->second;
}

int Registry::IndexOfTarget(const std::string& id) const {
  for (std::size_t i = 0; i < targets_.size(); ++i) {
    if (targets_[i]->desc.id == id) return static_cast<int>(i);
  }
  return -1;
}

bool Registry::RemoveTarget(const std::string& id) {
  auto it = byId_.find(id);
  if (it == byId_.end()) return false;
  Target* target = it->second;
  // 从 pending 队列里剔除（否则会写进已释放的目标）
  std::vector<PendingEntry> kept;
  kept.reserve(pending_.size());
  for (const PendingEntry& entry : pending_) {
    if (entry.target != target) kept.push_back(entry);
  }
  if (kept.size() != pending_.size()) {
    pending_.swap(kept);
    pendingIndex_.clear();
    for (std::size_t i = 0; i < pending_.size(); ++i) pendingIndex_[pending_[i].uid] = i;
  }
  byId_.erase(it);
  targets_.erase(std::remove_if(targets_.begin(), targets_.end(),
                                [target](const std::unique_ptr<Target>& item) {
                                  return item.get() == target;
                                }),
                 targets_.end());
  InvalidateDisplayOrder();
  return true;
}

void Registry::Clear() {
  targets_.clear();
  byId_.clear();
  pending_.clear();
  pendingIndex_.clear();
  subscribers_.clear();
  displayOrder_.clear();
  displayOrderDirty_ = true;
  frame_ = 0;
  editVersion_ = 0;
  appliedVersion_ = 0;
  mergedUpdates_ = 0;
  SourceTable().clear();
}

std::vector<const Target*> Registry::FindByType(const std::string& type) const {
  std::vector<const Target*> out;
  for (const auto& item : targets_) {
    if (item->desc.type == type) out.push_back(item.get());
  }
  return out;
}

std::vector<const Target*> Registry::FindByWindow(const std::string& window) const {
  std::vector<const Target*> out;
  for (const auto& item : targets_) {
    if (item->desc.window == window) out.push_back(item.get());
  }
  return out;
}

std::vector<const Target*> Registry::ChildrenOf(const std::string& parent) const {
  std::vector<const Target*> out;
  for (const auto& item : targets_) {
    if (item->desc.parent == parent) out.push_back(item.get());
  }
  return out;
}

std::vector<const Target*> Registry::Search(const std::string& keyword) const {
  std::vector<const Target*> out;
  if (keyword.empty()) {
    for (const auto& item : targets_) out.push_back(item.get());
    return out;
  }
  for (const auto& item : targets_) {
    bool hit = ContainsFold(item->desc.id, keyword) || ContainsFold(item->desc.name, keyword) ||
               ContainsFold(item->desc.type, keyword) || ContainsFold(item->desc.note, keyword);
    if (!hit) {
      for (const ParamSlot& slot : item->params) {
        if (ContainsFold(slot.desc.id, keyword) || ContainsFold(slot.desc.cn, keyword) ||
            ContainsFold(slot.desc.en, keyword)) {
          hit = true;
          break;
        }
      }
    }
    if (hit) out.push_back(item.get());
  }
  return out;
}

const std::vector<std::size_t>& Registry::DisplayOrder() {
  if (displayOrderDirty_) {
    displayOrder_.clear();
    displayOrder_.reserve(targets_.size());
    for (std::size_t i = 0; i < targets_.size(); ++i) displayOrder_.push_back(i);
    std::stable_sort(displayOrder_.begin(), displayOrder_.end(),
                     [this](std::size_t a, std::size_t b) {
                       const Target& ta = *targets_[a];
                       const Target& tb = *targets_[b];
                       if (ta.desc.order != tb.desc.order) return ta.desc.order < tb.desc.order;
                       return ta.desc.id < tb.desc.id;
                     });
    displayOrderDirty_ = false;
  }
  return displayOrder_;
}

std::size_t Registry::ParamCount(std::size_t targetIndex) const {
  return targetIndex < targets_.size() ? targets_[targetIndex]->params.size() : 0;
}

const ParamSlot* Registry::ParamAt(std::size_t targetIndex, std::size_t paramIndex) const {
  if (targetIndex >= targets_.size()) return nullptr;
  const Target& target = *targets_[targetIndex];
  return paramIndex < target.params.size() ? &target.params[paramIndex] : nullptr;
}

ParamSlot* Registry::SlotOf(const std::string& target, const std::string& param,
                            int* targetIndex) {
  Target* t = MutableTarget(target);
  if (t == nullptr) return nullptr;
  if (targetIndex != nullptr) *targetIndex = IndexOfTarget(target);
  return t->FindParam(param);
}

ParamValue Registry::Get(const std::string& target, const std::string& param) const {
  const Target* t = FindTarget(target);
  if (t == nullptr) return ParamValue();
  const ParamSlot* slot = t->FindParam(param);
  return slot != nullptr ? slot->Read() : ParamValue();
}

void Registry::MarkDirty(Target& target, ParamSlot& slot) {
  const std::uint64_t uid = nextUid_++;
  auto it = pendingIndex_.find(uid);
  (void)it;
  // 合并：同一个 slot 只保留最新一次请求。slot.index 与 target 唯一确定一个参数。
  for (std::size_t i = 0; i < pending_.size(); ++i) {
    if (pending_[i].target == &target && pending_[i].paramIndex == slot.index) {
      ++mergedUpdates_;           // 上一次请求还没被应用就被这次覆盖了 ⇒ 记一笔（诊断用）
      pending_[i].uid = uid;
      return;
    }
  }
  PendingEntry entry;
  entry.target = &target;
  entry.paramIndex = slot.index;
  entry.uid = uid;
  pending_.push_back(entry);
}

bool Registry::Set(const std::string& target, const std::string& param, const ParamValue& value,
                   ChangeOrigin origin) {
  int targetIndex = -1;
  ParamSlot* slot = SlotOf(target, param, &targetIndex);
  if (slot == nullptr || !slot->desc.supported || slot->ref == nullptr) return false;
  ParamValue sanitized = value;
  Sanitize(slot->desc, sanitized);
  const ParamValue previous = slot->Read();
  if (previous.Equals(sanitized)) {
    // 值没变：不写、不通知、不排队 —— 高频拖动里绝大多数帧都属于这种，省掉全部下游开销。
    return false;
  }
  slot->Write(sanitized);
  ++editVersion_;
  MarkDirty(*targets_[static_cast<std::size_t>(targetIndex)], *slot);
  // ★ 诊断：确认"控件 → 注册表"这一跳真的发生了。
  //   参数调了没反应时，先看这条有没有出现：
  //     没有 ⇒ 控件没写入（targetId/param 名不匹配，或 desc.supported 为假）；
  //     有   ⇒ 问题在下游（Commit 或渲染读取）。
#if defined(__ANDROID__)
  {
    // 同一开关：AURORA_PARAM_TRACE=1 才记录（默认关，避免刷满 logcat）。
    static const bool setTrace = [] {
      const char* value = std::getenv("AURORA_PARAM_TRACE");
      return value != nullptr && value[0] == '1';
    }();
    static int setLogBudget = 200;
    if (setTrace && setLogBudget > 0) {
      --setLogBudget;
      char valueText[64] = {};
      switch (slot->desc.kind) {
        case ParamKind::Float: snprintf(valueText, sizeof valueText, "%.4f", sanitized.f); break;
        case ParamKind::Int:
        case ParamKind::Enum: snprintf(valueText, sizeof valueText, "%d", sanitized.i); break;
        case ParamKind::Bool: snprintf(valueText, sizeof valueText, "%d", sanitized.b ? 1 : 0); break;
        case ParamKind::Color:
          snprintf(valueText, sizeof valueText, "%.3f,%.3f,%.3f,%.3f",
                   sanitized.rgba[0], sanitized.rgba[1], sanitized.rgba[2], sanitized.rgba[3]);
          break;
      }
      __android_log_print(ANDROID_LOG_INFO, "AuroraParam",
                          "SET %s/%s = %s (applyRef=%s)",
                          target.c_str(), param.c_str(), valueText,
                          slot->applyRef != nullptr ? (slot->applyRef != slot->ref ? "separate" : "same")
                                                    : "null");
    }
  }

#endif
  ParamChange change;
  change.target = target;
  change.param = param;
  change.value = sanitized;
  change.previous = previous;
  change.origin = origin;
  change.version = editVersion_;
  Notify(change);
  return true;
}

bool Registry::SetRaw(const std::string& target, const std::string& param,
                      const ParamValue& value, ChangeOrigin origin) {
  int targetIndex = -1;
  ParamSlot* slot = SlotOf(target, param, &targetIndex);
  if (slot == nullptr || slot->ref == nullptr) return false;
  const ParamValue previous = slot->Read();
  if (previous.Equals(value)) return false;
  slot->Write(value);
  ++editVersion_;
  MarkDirty(*targets_[static_cast<std::size_t>(targetIndex)], *slot);
  ParamChange change;
  change.target = target;
  change.param = param;
  change.value = value;
  change.previous = previous;
  change.origin = origin;
  change.version = editVersion_;
  Notify(change);
  return true;
}

bool Registry::SetF(const std::string& target, const std::string& param, float value,
                    ChangeOrigin origin) {
  return Set(target, param, ParamValue::OfFloat(value), origin);
}

bool Registry::SetI(const std::string& target, const std::string& param, int value,
                    ChangeOrigin origin) {
  return Set(target, param, ParamValue::OfInt(value), origin);
}

bool Registry::SetB(const std::string& target, const std::string& param, bool value,
                    ChangeOrigin origin) {
  return Set(target, param, ParamValue::OfBool(value), origin);
}

bool Registry::SetColor(const std::string& target, const std::string& param, float r, float g,
                        float b, float a, ChangeOrigin origin) {
  return Set(target, param, ParamValue::OfColor(r, g, b, a), origin);
}

bool Registry::ResetParam(const std::string& target, const std::string& param,
                          ChangeOrigin origin) {
  int targetIndex = -1;
  ParamSlot* slot = SlotOf(target, param, &targetIndex);
  if (slot == nullptr || slot->ref == nullptr || !slot->desc.supported) return false;
  return Set(target, param, slot->def, origin);
}

bool Registry::ResetTarget(const std::string& target, ChangeOrigin origin) {
  Target* t = MutableTarget(target);
  if (t == nullptr) return false;
  bool any = false;
  for (ParamSlot& slot : t->params) {
    if (!slot.desc.supported || slot.ref == nullptr) continue;
    if (Set(target, slot.desc.id, slot.def, origin)) any = true;
  }
  return any;
}

bool Registry::ResetGroup(const std::string& target, const std::string& group,
                          ChangeOrigin origin) {
  Target* t = MutableTarget(target);
  if (t == nullptr) return false;
  bool any = false;
  for (ParamSlot& slot : t->params) {
    if (slot.desc.group != group) continue;
    if (!slot.desc.supported || slot.ref == nullptr) continue;
    if (Set(target, slot.desc.id, slot.def, origin)) any = true;
  }
  return any;
}

std::size_t Registry::ResetAll(ChangeOrigin origin) {
  std::size_t count = 0;
  for (const auto& item : targets_) {
    for (ParamSlot& slot : item->params) {
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      if (Set(item->desc.id, slot.desc.id, slot.def, origin)) ++count;
    }
  }
  return count;
}

Snapshot Registry::Capture() const {
  Snapshot snapshot;
  for (const auto& item : targets_) {
    for (const ParamSlot& slot : item->params) {
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      SnapshotEntry entry;
      entry.target = item->desc.id;
      entry.param = slot.desc.id;
      entry.value = slot.Read();
      snapshot.entries.push_back(std::move(entry));
    }
  }
  return snapshot;
}

Snapshot Registry::CaptureTarget(const std::string& target) const {
  Snapshot snapshot;
  const Target* t = FindTarget(target);
  if (t == nullptr) return snapshot;
  for (const ParamSlot& slot : t->params) {
    if (!slot.desc.supported || slot.ref == nullptr) continue;
    SnapshotEntry entry;
    entry.target = target;
    entry.param = slot.desc.id;
    entry.value = slot.Read();
    snapshot.entries.push_back(std::move(entry));
  }
  return snapshot;
}

Snapshot Registry::CaptureGroup(const std::string& target, const std::string& group) const {
  Snapshot snapshot;
  const Target* t = FindTarget(target);
  if (t == nullptr) return snapshot;
  for (const ParamSlot& slot : t->params) {
    if (slot.desc.group != group) continue;
    if (!slot.desc.supported || slot.ref == nullptr) continue;
    SnapshotEntry entry;
    entry.target = target;
    entry.param = slot.desc.id;
    entry.value = slot.Read();
    snapshot.entries.push_back(std::move(entry));
  }
  return snapshot;
}

Snapshot Registry::CaptureDiffFromDefault() const {
  Snapshot snapshot;
  for (const auto& item : targets_) {
    for (const ParamSlot& slot : item->params) {
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      const ParamValue current = slot.Read();
      if (current.Equals(slot.def)) continue;
      SnapshotEntry entry;
      entry.target = item->desc.id;
      entry.param = slot.desc.id;
      entry.value = current;
      snapshot.entries.push_back(std::move(entry));
    }
  }
  return snapshot;
}

Snapshot Registry::CaptureDefaults() const {
  Snapshot snapshot;
  for (const auto& item : targets_) {
    for (const ParamSlot& slot : item->params) {
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      SnapshotEntry entry;
      entry.target = item->desc.id;
      entry.param = slot.desc.id;
      entry.value = slot.def;
      snapshot.entries.push_back(std::move(entry));
    }
  }
  return snapshot;
}

std::size_t Registry::Apply(const Snapshot& snapshot, ChangeOrigin origin,
                            std::vector<std::string>* skipped,
                            std::vector<std::string>* adjusted) {
  std::size_t applied = 0;
  for (const SnapshotEntry& entry : snapshot.entries) {
    const Target* t = FindTarget(entry.target);
    if (t == nullptr) {
      if (skipped != nullptr) skipped->push_back(entry.target + "." + entry.param + " (无此目标)");
      continue;
    }
    const ParamSlot* slot = t->FindParam(entry.param);
    if (slot == nullptr) {
      if (skipped != nullptr) skipped->push_back(entry.target + "." + entry.param + " (无此参数)");
      continue;
    }
    if (!slot->desc.supported || slot->ref == nullptr) {
      if (skipped != nullptr) skipped->push_back(entry.target + "." + entry.param + " (不支持)");
      continue;
    }
    ParamValue value = entry.value;
    const bool clean = Sanitize(slot->desc, value);
    if (!clean && adjusted != nullptr) {
      adjusted->push_back(entry.target + "." + entry.param + " (值不合法，已夹紧)");
    }
    if (Set(entry.target, entry.param, value, origin)) ++applied;
  }
  return applied;
}

void Registry::Subscribe(ChangeSink sink, void* user) {
  if (sink == nullptr) return;
  Subscriber candidate{sink, user};
  for (const Subscriber& existing : subscribers_) {
    if (existing == candidate) return;
  }
  subscribers_.push_back(candidate);
}

void Registry::Unsubscribe(ChangeSink sink, void* user) {
  Subscriber candidate{sink, user};
  subscribers_.erase(std::remove(subscribers_.begin(), subscribers_.end(), candidate),
                     subscribers_.end());
}

void Registry::Notify(const ParamChange& change) {
  // 订阅者可能在回调里再次写参数（例如联动/约束）⇒ 用下标遍历并容忍增长。
  for (std::size_t i = 0; i < subscribers_.size(); ++i) {
    subscribers_[i].sink(subscribers_[i].user, change);
  }
}

void Registry::BeginFrame() { ++frame_; }

std::size_t Registry::Commit() {
  if (pending_.empty()) {
    appliedVersion_ = editVersion_;
    return 0;
  }
  // 队列里每个 slot 最多一条（MarkDirty 已经合并）⇒ 这里就是"只应用最新值"。
  std::size_t applied = 0;
  for (PendingEntry& entry : pending_) {
    if (entry.target == nullptr) continue;
    if (entry.paramIndex >= entry.target->params.size()) continue;
    ParamSlot& slot = entry.target->params[entry.paramIndex];
    if (slot.applyRef != nullptr && slot.applyRef != slot.ref) {
      const ParamValue requested = slot.Read();
      // 生效值单独存：渲染读 applyRef，UI 读 ref。两者类型一致，直接内存拷贝。
      switch (slot.desc.kind) {
        case ParamKind::Float:
          *static_cast<float*>(slot.applyRef) = requested.f;
          break;
        case ParamKind::Int:
        case ParamKind::Enum:
          *static_cast<int*>(slot.applyRef) = requested.i;
          break;
        case ParamKind::Bool:
          *static_cast<bool*>(slot.applyRef) = requested.b;
          break;
        case ParamKind::Color: {
          float* dst = static_cast<float*>(slot.applyRef);
          for (int c = 0; c < 4; ++c) dst[c] = requested.rgba[c];
          break;
        }
      }
    }
    ++applied;
  }
  pending_.clear();
  pendingIndex_.clear();
  appliedVersion_ = editVersion_;
  return applied;
}

void Registry::MarkSeen(const std::string& id) {
  Target* t = MutableTarget(id);
  if (t != nullptr) t->lastSeenFrame = frame_;
}

std::size_t Registry::Sweep(int keepFrames) {
  std::vector<std::string> doomed;
  for (const auto& item : targets_) {
    if (!item->desc.dynamic) continue;
    if (item->lastSeenFrame < 0) continue;          // 从未确认过 ⇒ 留给第一次注册
    if (frame_ - item->lastSeenFrame > keepFrames) doomed.push_back(item->desc.id);
  }
  for (const std::string& id : doomed) RemoveTarget(id);
  return doomed.size();
}

std::size_t Registry::ParamTotal() const {
  std::size_t total = 0;
  for (const auto& item : targets_) total += item->params.size();
  return total;
}

std::size_t Registry::ChangedFromDefault() const {
  std::size_t changed = 0;
  for (const auto& item : targets_) {
    for (const ParamSlot& slot : item->params) {
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      if (!slot.Read().Equals(slot.def)) ++changed;
    }
  }
  return changed;
}

std::size_t Registry::UnsupportedCount() const {
  std::size_t count = 0;
  for (const auto& item : targets_) {
    for (const ParamSlot& slot : item->params) {
      if (!slot.desc.supported) ++count;
    }
  }
  return count;
}

Registry& Global() {
  static Registry registry;
  return registry;
}

}  // namespace glass
}  // namespace aurora
