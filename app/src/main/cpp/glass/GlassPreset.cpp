#include "GlassPreset.h"

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace aurora {
namespace glass {
namespace {

// v1（旧版"按组件分组的平铺参数表"）的组名 → v2 的调试目标 ID。
// 这张表是**旧面板真实存在过的分组**（secondary_imgui.cpp 的
// DrawHostControls：window / card / playground / button / toggle / slider /
// menu / tabs / chip / dialog），不是凭空造的。
struct LegacyGroupRule {
  const char* group;
  const char* target;
};

const LegacyGroupRule kLegacyGroups[] = {
    {"window", "secondary_window"},
    {"card", "secondary_window.card"},
    {"playground", "secondary_window.playground"},
    {"button", "secondary_window.pill_button_1"},
    {"button_primary", "secondary_window.pill_button_1"},
    {"button_tint", "secondary_window.pill_button_2"},
    {"button_surface", "secondary_window.pill_button_3"},
    {"button_small", "secondary_window.pill_button_4"},
    {"toggle", "secondary_window.toggle_1"},
    {"toggle_1", "secondary_window.toggle_1"},
    {"toggle_2", "secondary_window.toggle_2"},
    {"toggle_3", "secondary_window.toggle_3"},
    {"slider", "secondary_window.slider_1"},
    {"slider_1", "secondary_window.slider_1"},
    {"slider_2", "secondary_window.slider_2"},
    {"menu", "secondary_window.popmenu"},
    {"tabs", "secondary_window.navigation"},
    {"navigation", "secondary_window.navigation"},
    {"chip", "secondary_window.chip_1"},
    {"dialog", "secondary_window.dialog"},
};

// 旧字段名 → 新参数 ID。只收录**确实改过名**的（同名的返回 nullptr）。
// 这张表是兼容层的唯一入口：以后任何参数改名，都必须在这里补一条，
// 否则老配置里那个字段会被 Apply 当成"无此参数"跳过（并且会**报告**出来，
// 不会静默丢失 —— 见 Registry::Apply 的 skipped）。
struct RenameRule {
  const char* from;
  const char* to;
};

const RenameRule kRenames[] = {
    // 旧版按组平铺时用的短名（省略了单位后缀）
    {"blur", "blurDp"},
    {"lensHeight", "lensHeightDp"},
    {"lensAmount", "lensAmountDp"},
    {"width", "widthDp"},
    {"height", "heightDp"},
    {"corner", "cornerTlDp"},
    {"cornerRadius", "cornerTlDp"},
    {"radius", "cornerTlDp"},
    {"sizeDp", "widthDp"},
    {"shadow", "shadowOn"},
    {"innerShadow", "insetOn"},
    {"innerShadowRadiusDp", "insetRadiusDp"},
    {"innerShadowOn", "insetOn"},
    // 语义改名的（同一件事换了名字）
    {"spectral", "dispersion"},
    {"chromatic", "chromaticOn"},
    {"opacityEnabled", "opacityOn"},
    {"alphaScale", "alpha"},
    {"convexity", "depthAmount"},
    {"windowOpacityEnabled", "opacityOn"},
    {"windowOpacity", "opacity"},
    {"windowBlurDp", "blurDp"},
    {"windowLensHeightDp", "lensHeightDp"},
    {"windowLensAmountDp", "lensAmountDp"},
    {"windowDispersion", "dispersion"},
};

// 查表辅助（v1 迁移用）：真正的规则表在 LegacyGroupTarget / MigrateParamId 里

// 值与类型无关地写进 JSON：用显式 kind 字段，读回来时才能还原成同一个 ParamKind
// （否则 "1" 到底是 float 1.0 还是 int 1 / bool true 就分不清了）
void WriteValue(const ParamValue& value, JsonValue* out) {
  out->Set("kind", JsonValue::Str(ParamKindName(value.kind)));
  switch (value.kind) {
    case ParamKind::Float:
      out->Set("f", JsonValue::Number(static_cast<double>(value.f)));
      break;
    case ParamKind::Int:
    case ParamKind::Enum:
      out->Set("i", JsonValue::Number(static_cast<double>(value.i)));
      break;
    case ParamKind::Bool:
      out->Set("b", JsonValue::Bool(value.b));
      break;
    case ParamKind::Color: {
      JsonValue channels = JsonValue::Array();
      for (int c = 0; c < 4; ++c) {
        channels.Push(JsonValue::Number(static_cast<double>(value.rgba[c])));
      }
      out->Set("c", channels);
      break;
    }
  }
}

// 按 descriptor 决定的类型把 JSON 节点还原成参数值。
// ★ 类型以**注册表的 descriptor** 为准，JSON 里的 kind 只是提示：
//   这样"导出的文件被手工改过类型"也不会把渲染字段写坏。
bool ReadValue(const JsonValue& node, ParamKind expected, ParamValue* out) {
  out->kind = expected;
  switch (expected) {
    case ParamKind::Float:
      if (node.Has("f")) out->f = node["f"].AsFloat();
      else if (node.Has("i")) out->f = static_cast<float>(node["i"].AsInt());
      else if (node.Has("b")) out->f = node["b"].AsBool() ? 1.0f : 0.0f;
      else return false;
      return true;
    case ParamKind::Int:
    case ParamKind::Enum:
      if (node.Has("i")) out->i = node["i"].AsInt();
      else if (node.Has("f")) out->i = static_cast<int>(node["f"].AsFloat());
      else if (node.Has("b")) out->i = node["b"].AsBool() ? 1 : 0;
      else return false;
      return true;
    case ParamKind::Bool:
      if (node.Has("b")) out->b = node["b"].AsBool();
      else if (node.Has("i")) out->b = node["i"].AsInt() != 0;
      else if (node.Has("f")) out->b = node["f"].AsFloat() != 0.0f;
      else return false;
      return true;
    case ParamKind::Color: {
      const JsonValue& channels = node["c"];
      if (channels.type() != JsonValue::Type::Array) return false;
      for (int c = 0; c < 4; ++c) {
        out->rgba[c] = c < static_cast<int>(channels.size())
            ? channels.At(static_cast<std::size_t>(c)).AsFloat()
            : 0.0f;
      }
      return true;
    }
  }
  return false;
}

}  // namespace

const char* LegacyGroupTarget(const char* group) {
  for (const LegacyGroupRule& rule : kLegacyGroups) {
    if (std::strcmp(rule.group, group) == 0) return rule.target;
  }
  return nullptr;
}

const char* MigrateParamId(const char* param) {
  for (const RenameRule& rule : kRenames) {
    if (std::strcmp(rule.from, param) == 0) return rule.to;
  }
  return nullptr;
}

// -----------------------------------------------------------------------------
// 文档 <-> JSON
// -----------------------------------------------------------------------------
JsonValue PresetToJson(const PresetDocument& document) {
  JsonValue root = JsonValue::Object();
  root.Set("format", JsonValue::Str("aurora.glass.params"));
  root.Set("version", JsonValue::Number(document.version));
  root.Set("active", JsonValue::Str(document.active));
  if (!document.source.empty()) root.Set("source", JsonValue::Str(document.source));

  JsonValue presets = JsonValue::Object();
  for (const Preset& preset : document.presets) {
    // 先把同一个目标的参数收集到一张表里，再整体塞进 JSON
    // （JsonValue 是值语义，逐条 Set 到嵌套节点上才能保证写回）
    std::map<std::string, JsonValue> byTarget;
    for (const SnapshotEntry& entry : preset.values.entries) {
      auto it = byTarget.find(entry.target);
      if (it == byTarget.end()) {
        it = byTarget.emplace(entry.target, JsonValue::Object()).first;
      }
      JsonValue node = JsonValue::Object();
      WriteValue(entry.value, &node);
      it->second.Set(entry.param, node);
    }
    JsonValue targets = JsonValue::Object();
    for (const auto& item : byTarget) targets.Set(item.first, item.second);

    JsonValue presetNode = JsonValue::Object();
    if (!preset.note.empty()) presetNode.Set("note", JsonValue::Str(preset.note));
    presetNode.Set("savedAt", JsonValue::Number(static_cast<double>(preset.savedAt)));
    presetNode.Set("targets", targets);
    presets.Set(preset.name, presetNode);
  }
  root.Set("presets", presets);
  return root;
}

bool PresetFromJson(const JsonValue& root, PresetDocument* out, ImportReport* report) {
  if (out == nullptr) return false;
  ImportReport local;
  ImportReport& rep = report != nullptr ? *report : local;
  if (root.type() != JsonValue::Type::Object) {
    rep.error = "根节点不是对象";
    return false;
  }
  const int version = root.Has("version") ? root["version"].AsInt() : 1;
  rep.sourceVersion = version;
  out->version = kPresetFormatVersion;
  out->active = root.Has("active") ? root["active"].AsString() : "默认";
  out->source = root.Has("source") ? root["source"].AsString() : "";
  out->presets.clear();

  if (version >= 2) {
    const JsonValue& presets = root["presets"];
    if (presets.type() != JsonValue::Type::Object) {
      rep.error = "v2 文档缺少 presets 对象";
      return false;
    }
    for (const auto& item : presets.fields()) {
      Preset preset;
      preset.name = item.first;
      const JsonValue& node = item.second;
      preset.note = node.Has("note") ? node["note"].AsString() : "";
      preset.savedAt = node.Has("savedAt") ? static_cast<std::int64_t>(node["savedAt"].AsNumber()) : 0;
      const JsonValue& targets = node["targets"];
      if (targets.type() != JsonValue::Type::Object) {
        rep.notes.push_back("预设 " + preset.name + "：缺少 targets，已跳过");
        ++rep.skipped;
        continue;
      }
      for (const auto& targetItem : targets.fields()) {
        const JsonValue& params = targetItem.second;
        if (params.type() != JsonValue::Type::Object) {
          rep.notes.push_back("目标 " + targetItem.first + "：参数表不是对象，已跳过");
          ++rep.skipped;
          continue;
        }
        for (const auto& paramItem : params.fields()) {
          SnapshotEntry entry;
          entry.target = targetItem.first;
          entry.param = paramItem.first;
          // 类型在应用阶段按 descriptor 决定；这里只把**原始通道**读出来
          const JsonValue& node2 = paramItem.second;
          ParamKind kind = ParamKind::Float;
          if (node2.Has("kind")) {
            const std::string& text = node2["kind"].AsString();
            if (text == "int") kind = ParamKind::Int;
            else if (text == "bool") kind = ParamKind::Bool;
            else if (text == "enum") kind = ParamKind::Enum;
            else if (text == "color") kind = ParamKind::Color;
          } else if (node2.Has("b")) {
            kind = ParamKind::Bool;
          } else if (node2.Has("c")) {
            kind = ParamKind::Color;
          } else if (node2.Has("i")) {
            kind = ParamKind::Int;
          }
          if (!ReadValue(node2, kind, &entry.value)) {
            rep.notes.push_back("参数 " + entry.target + "." + entry.param + "：值无法解析，已跳过");
            ++rep.skipped;
            continue;
          }
          preset.values.entries.push_back(std::move(entry));
        }
      }
      out->presets.push_back(std::move(preset));
    }
  } else {
    // ---- v1 迁移：{"groups":{"window":{...},"card":{...}}} ----
    rep.migratedFrom = 1;
    rep.notes.push_back("检测到 v1 配置（按组件分组的一层平铺），已按组名迁移到调试目标 ID");
    const JsonValue& groups = root.Has("groups") ? root["groups"] : root;
    if (groups.type() != JsonValue::Type::Object) {
      rep.error = "v1 文档里找不到 groups 对象";
      return false;
    }
    Preset preset;
    preset.name = "迁移自 v1";
    preset.note = "由 v1 平铺配置自动迁移";
    for (const auto& groupItem : groups.fields()) {
      if (groupItem.first == "version" || groupItem.first == "format") continue;
      const char* targetId = LegacyGroupTarget(groupItem.first.c_str());
      if (targetId == nullptr) {
        rep.notes.push_back("v1 组 " + groupItem.first + "：没有对应的调试目标，已跳过");
        ++rep.skipped;
        continue;
      }
      const JsonValue& params = groupItem.second;
      if (params.type() != JsonValue::Type::Object) {
        rep.notes.push_back("v1 组 " + groupItem.first + "：不是对象，已跳过");
        ++rep.skipped;
        continue;
      }
      for (const auto& paramItem : params.fields()) {
        SnapshotEntry entry;
        entry.target = targetId;
        const char* renamed = MigrateParamId(paramItem.first.c_str());
        entry.param = renamed != nullptr ? renamed : paramItem.first;
        if (renamed != nullptr) {
          rep.notes.push_back("字段改名：" + groupItem.first + "." + paramItem.first + " → " +
                              entry.param);
        }
        const JsonValue& node = paramItem.second;
        if (node.type() == JsonValue::Type::Bool) {
          entry.value = ParamValue::OfBool(node.AsBool());
        } else if (node.type() == JsonValue::Type::Number) {
          entry.value = ParamValue::OfFloat(node.AsFloat());
        } else if (node.type() == JsonValue::Type::String) {
          // v1 里颜色可能被写成 "#RRGGBBAA"
          const std::string& text = node.AsString();
          if (text.size() >= 7 && text[0] == '#') {
            unsigned value = 0;
            for (std::size_t i = 1; i < text.size() && i <= 8; ++i) {
              const char digit = text[i];
              value <<= 4;
              if (digit >= '0' && digit <= '9') value |= static_cast<unsigned>(digit - '0');
              else if (digit >= 'a' && digit <= 'f') value |= static_cast<unsigned>(digit - 'a' + 10);
              else if (digit >= 'A' && digit <= 'F') value |= static_cast<unsigned>(digit - 'A' + 10);
            }
            const std::size_t digits = text.size() - 1;
            const float r = static_cast<float>((value >> (digits >= 8 ? 24 : 16)) & 0xFFu) / 255.0f;
            const float g = static_cast<float>((value >> (digits >= 8 ? 16 : 8)) & 0xFFu) / 255.0f;
            const float b = static_cast<float>((value >> (digits >= 8 ? 8 : 0)) & 0xFFu) / 255.0f;
            const float a = digits >= 8 ? static_cast<float>(value & 0xFFu) / 255.0f : 1.0f;
            entry.value = ParamValue::OfColor(r, g, b, a);
          } else {
            rep.notes.push_back("v1 字段 " + groupItem.first + "." + paramItem.first +
                                "：字符串值无法解释，已跳过");
            ++rep.skipped;
            continue;
          }
        } else {
          rep.notes.push_back("v1 字段 " + groupItem.first + "." + paramItem.first +
                              "：值类型不支持，已跳过");
          ++rep.skipped;
          continue;
        }
        preset.values.entries.push_back(std::move(entry));
      }
    }
    out->presets.push_back(std::move(preset));
    out->active = out->presets.front().name;
  }

  if (out->presets.empty()) {
    Preset fallback;
    fallback.name = "默认";
    out->presets.push_back(std::move(fallback));
    rep.notes.push_back("文档里没有任何预设，已创建空的「默认」预设");
  }
  if (out->active.empty()) out->active = out->presets.front().name;
  return true;
}

// -----------------------------------------------------------------------------
// 文件
// -----------------------------------------------------------------------------
bool FileExists(const std::string& path) {
  if (path.empty()) return false;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;
  std::fclose(file);
  return true;
}

bool RemoveFile(const std::string& path) {
  if (path.empty()) return false;
  return std::remove(path.c_str()) == 0;
}

bool ReadTextFile(const std::string& path, std::string* text, std::string* error) {
  if (text == nullptr) return false;
  text->clear();
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    if (error != nullptr) *error = "打不开文件：" + path;
    return false;
  }
  char buffer[4096];
  std::size_t read = 0;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text->append(buffer, read);
  }
  const bool bad = std::ferror(file) != 0;
  std::fclose(file);
  if (bad) {
    if (error != nullptr) *error = "读取失败：" + path;
    return false;
  }
  return true;
}

bool SaveTextAtomic(const std::string& path, const std::string& text, std::string* error) {
  if (path.empty()) {
    if (error != nullptr) *error = "保存路径为空";
    return false;
  }
  const std::string temp = path + ".tmp";
  std::FILE* file = std::fopen(temp.c_str(), "wb");
  if (file == nullptr) {
    if (error != nullptr) *error = "无法写入临时文件：" + temp;
    return false;
  }
  const std::size_t written = text.empty() ? 0 : std::fwrite(text.data(), 1, text.size(), file);
  if (written != text.size()) {
    std::fclose(file);
    std::remove(temp.c_str());
    if (error != nullptr) *error = "写入不完整（磁盘满？）";
    return false;
  }
  if (std::fflush(file) != 0) {
    std::fclose(file);
    std::remove(temp.c_str());
    if (error != nullptr) *error = "flush 失败";
    return false;
  }
#if defined(_WIN32)
  _commit(_fileno(file));
#else
  fsync(fileno(file));   // 断电/被杀时保证临时文件内容真的落盘，rename 才有意义
#endif
  std::fclose(file);
  // rename 在同一文件系统内是原子的：要么看到旧文件，要么看到新文件，不会看到半份
  if (std::rename(temp.c_str(), path.c_str()) != 0) {
    std::remove(temp.c_str());
    if (error != nullptr) *error = "替换目标文件失败（errno=" + std::to_string(errno) + "）";
    return false;
  }
  return true;
}

std::int64_t UnixSecondsNow() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// -----------------------------------------------------------------------------
// 后台原子写：把"写盘 + fsync + rename"从渲染线程挪走
//
// 背景（实测过的成本）：参数文件 ~200KB，`Save()` 要做整份 JSON 序列化
// （调用线程，必须做 —— Registry 不是线程安全的）**加上** fopen/fwrite/fsync/
// rename（内核 I/O，几毫秒到几十毫秒）。后者原本发生在
// `DrawGlassWorkbench()` 里 ⇒ 每改一次参数、停手 2 秒就掉一次帧。
//
// 设计取舍：
//   · 单槽"最新覆盖"而不是队列：连续改参数时只有最后一份需要落盘；
//   · 写盘失败不清 dirty（这里只记录原因，UI 下帧会再提交一次）；
//   · 进程退出时不做等待 —— 文件是原子替换的，最坏情况是"最后几百毫秒的改动
//     没落盘"，绝不会留下半份文件；
//   · 每个路径最后一次写入完成前，新的提交会覆盖旧的（同一路径无需保序）。
// -----------------------------------------------------------------------------
namespace {

class AtomicTextWriter {
 public:
  static AtomicTextWriter& Instance() {
    static AtomicTextWriter writer;
    return writer;
  }

  // 只有"线程起不来"才返回 false（调用方据此回退到同步 Save）。
  bool Submit(std::string path, std::string text) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!started_) {
        started_ = true;
        thread_ = std::thread([this] { Run(); });
        if (!thread_.joinable()) {
          started_ = false;
          return false;
        }
      }
      path_ = std::move(path);
      text_ = std::move(text);
      hasPending_ = true;
    }
    cv_.notify_one();
    return true;
  }

  std::string LastError() {
    std::lock_guard<std::mutex> lock(mutex_);
    return lastError_;
  }

  // ★★ 必须显式停止后台线程。
  //
  //   原来是隐式析构：`std::thread` 销毁时若仍 joinable 就直接
  //   std::terminate() ⇒ SIGABRT。而 Run() 的退出条件 `stop_` 从来没被置位，
  //   线程一直阻塞在 cv_.wait 上 —— 于是**每次正常退出都会崩**。
  //   真机实测：跑满 1893 帧正常收尾，紧接着 SIGABRT，栈是
  //     AtomicTextWriter::~AtomicTextWriter() ← main
  //
  //   修法：置 stop_ → 唤醒 → join。写入本身是原子 rename，丢掉最后几百毫秒
  //   的改动是可接受的（原注释已如此约定），但绝不能崩。
  ~AtomicTextWriter() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
      cv_.wait(lock, [this] { return hasPending_ || stop_; });
      if (stop_) return;
      std::string path = std::move(path_);
      std::string text = std::move(text_);
      hasPending_ = false;
      lock.unlock();
      std::string error;
      const bool ok = SaveTextAtomic(path, text, &error);
      lock.lock();
      if (!ok) {
        lastError_ = error;
        lastFailed_ = true;
      } else {
        lastError_.clear();
      }
    }
  }

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::string path_;
  std::string text_;
  bool hasPending_ = false;
  bool started_ = false;
  bool stop_ = false;
  bool lastFailed_ = false;
  std::string lastError_;
};

}  // namespace

bool PresetStore::SaveAsync(std::string* error) {
  // 序列化留在调用线程：只读 registry，很快（~200KB 文本，无 I/O）。
  const std::string text = ExportText();
  if (!AtomicTextWriter::Instance().Submit(path_, text)) {
    if (error != nullptr) *error = "后台保存线程不可用";
    return false;
  }
  dirty_ = false;
  loaded_ = true;
  lastSave_ = UnixSecondsNow();
  return true;
}

std::string PresetStore::LastAsyncError() {
  return AtomicTextWriter::Instance().LastError();
}

// -----------------------------------------------------------------------------
// PresetStore
// -----------------------------------------------------------------------------
PresetStore::PresetStore(Registry& registry) : registry_(registry) { EnsureDefaultPreset(); }

// ★ 「默认」预设的语义 = **注册时捕获的默认值**，而不是"创建 PresetStore 那一刻的现场"。
//
// 为什么必须这样：PresetStore 完全可能在组件注册**之前**被构造（本项目的
// GlassWorkbench 就是这么初始化的），如果默认预设是构造时快照，它会是空的 ——
// 于是"恢复默认"什么都不做、导出的文件里也没有基线，而且这种 bug 在真机上
// 表现为"按了没反应"，很难查。绑到 registry 的 def 上就与创建顺序无关了。
void PresetStore::EnsureDefaultPreset() {
  if (!document_.presets.empty()) return;
  Preset preset;
  preset.name = "默认";
  preset.note = "组件注册时的原始参数（由注册表现场提供，不随创建顺序变化）";
  // values 故意留空：空 ⇒ "用注册表的默认值"，在 ApplyPreset / ExportText 里展开
  document_.presets.push_back(std::move(preset));
  document_.active = "默认";
}

bool PresetStore::IsImplicitDefault(std::size_t index) const {
  return index < document_.presets.size() && document_.presets[index].name == "默认" &&
         document_.presets[index].values.empty();
}

int PresetStore::IndexOf(const std::string& name) const {
  for (std::size_t i = 0; i < document_.presets.size(); ++i) {
    if (document_.presets[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

bool PresetStore::SetActiveName(const std::string& name) {
  if (IndexOf(name) < 0) return false;
  document_.active = name;
  dirty_ = true;
  return true;
}

bool PresetStore::CaptureAs(const std::string& name, bool note) {
  if (name.empty()) return false;
  Preset preset;
  preset.name = name;
  preset.savedAt = UnixSecondsNow();
  preset.note = note ? "手动捕获" : "";
  preset.values = registry_.Capture();
  const int existing = IndexOf(name);
  if (existing >= 0) {
    document_.presets[static_cast<std::size_t>(existing)] = std::move(preset);
  } else {
    document_.presets.push_back(std::move(preset));
  }
  document_.active = name;
  dirty_ = true;
  return true;
}

bool PresetStore::ApplyPreset(const std::string& name, std::vector<std::string>* skipped,
                              std::vector<std::string>* adjusted) {
  const int index = IndexOf(name);
  if (index < 0) return false;
  if (IsImplicitDefault(static_cast<std::size_t>(index))) {
    // 「默认」= 注册表里每个参数注册时捕获的 def ⇒ 走 reset 路径，
    // 与"恢复全部默认值"是**同一条代码**，不可能出现两套默认值。
    registry_.ResetAll(ChangeOrigin::Preset);
  } else {
    registry_.Apply(document_.presets[static_cast<std::size_t>(index)].values,
                    ChangeOrigin::Preset, skipped, adjusted);
  }
  document_.active = name;
  dirty_ = true;
  return true;
}

bool PresetStore::Rename(std::size_t index, const std::string& name) {
  if (index >= document_.presets.size() || name.empty()) return false;
  const int clash = IndexOf(name);
  if (clash >= 0 && clash != static_cast<int>(index)) return false;
  // ★ 先记住"被改名的这个是不是当前激活预设"，改完名字再比就永远不相等了
  const bool wasActive = document_.active == document_.presets[index].name;
  document_.presets[index].name = name;
  if (wasActive) document_.active = name;
  dirty_ = true;
  return true;
}

bool PresetStore::Remove(std::size_t index) {
  if (index >= document_.presets.size()) return false;
  if (document_.presets.size() <= 1) return false;   // 至少留一个
  const bool wasActive = document_.active == document_.presets[index].name;
  document_.presets.erase(document_.presets.begin() + static_cast<std::ptrdiff_t>(index));
  if (wasActive) document_.active = document_.presets.front().name;
  dirty_ = true;
  return true;
}

std::string PresetStore::ExportText(bool includeWorkingSet) const {
  PresetDocument copy = document_;
  copy.version = kPresetFormatVersion;
  if (copy.source.empty()) copy.source = "aurora_overlay liquid glass inspector";
  // 隐式默认预设导出时**展开成真实数值**：导出的文件必须自描述，
  // 换一台机器/换一个版本导入时不能依赖"对方也有一套同样的默认值"。
  const Snapshot defaults = registry_.CaptureDefaults();
  for (Preset& preset : copy.presets) {
    if (preset.name == "默认" && preset.values.empty()) preset.values = defaults;
  }
  if (includeWorkingSet) {
    // ★ "导出参数配置"的用户预期是**屏幕上的样子**，而不只是命名预设的列表。
    //   所以在导出瞬间快照一份"当前工作区"，并把它设为 active ⇒
    //   导入方按 active 应用一次就能 1:1 还原。
    Preset working;
    working.name = kWorkingSetName;
    working.note = "导出瞬间的参数值";
    working.savedAt = UnixSecondsNow();
    working.values = registry_.Capture();
    bool replaced = false;
    for (Preset& preset : copy.presets) {
      if (preset.name == working.name) {
        preset = working;
        replaced = true;
        break;
      }
    }
    if (!replaced) copy.presets.push_back(std::move(working));
    copy.active = kWorkingSetName;
  }
  return PresetToJson(copy).Dump(2);
}

bool PresetStore::ImportText(const std::string& text, ImportReport* report, bool keepExisting) {
  ImportReport local;
  ImportReport& rep = report != nullptr ? *report : local;
  std::string error;
  const JsonValue root = JsonValue::Parse(text, &error);
  if (!error.empty()) {
    rep.error = "JSON 解析失败：" + error;
    return false;
  }
  PresetDocument incoming;
  if (!PresetFromJson(root, &incoming, &rep)) return false;

  // ★ 校验一遍"这份配置里的目标/参数在当前注册表里是否存在"。
  //   为什么不在导入时直接丢掉：目标可能是**延迟注册**的（组件在第一次绘制时
  //   才登记），导入发生得更早就会出现"全部被丢掉"的假象。所以这里只统计并
  //   报告，真正的跳过发生在 ApplyPreset（那时注册表已经稳定）。
  //   ★ 必须在下面的 std::move 之前做 —— move 之后 incoming 里的条目就空了。
  std::size_t notedUnknown = 0;
  for (const Preset& preset : incoming.presets) {
    for (const SnapshotEntry& entry : preset.values.entries) {
      const Target* target = registry_.FindTarget(entry.target);
      const bool known = target != nullptr && target->FindParam(entry.param) != nullptr;
      if (known) continue;
      ++rep.unmatched;
      if (notedUnknown < 8) {
        rep.notes.push_back("当前注册表里没有 " + entry.target + "." + entry.param +
                            "（导入时保留，应用时会被跳过）");
        ++notedUnknown;
      }
    }
  }

  if (!keepExisting) document_.presets.clear();
  for (Preset& preset : incoming.presets) {
    const std::string incomingName = preset.name;
    const int existing = IndexOf(incomingName);
    if (existing >= 0) {
      document_.presets[static_cast<std::size_t>(existing)] = std::move(preset);
      rep.notes.push_back("覆盖同名预设：" + incomingName);
    } else {
      document_.presets.push_back(std::move(preset));
    }
  }
  if (IndexOf(incoming.active) >= 0) document_.active = incoming.active;
  EnsureDefaultPreset();
  dirty_ = true;
  return true;
}

bool PresetStore::Save(std::string* error) {
  const std::string text = ExportText();
  if (!SaveTextAtomic(path_, text, error)) return false;
  dirty_ = false;
  loaded_ = true;
  lastSave_ = UnixSecondsNow();
  return true;
}

bool PresetStore::Load(std::string* error) {
  if (!FileExists(path_)) {
    if (error != nullptr) error->clear();
    return false;   // 文件不存在不算错误
  }
  std::string text;
  if (!ReadTextFile(path_, &text, error)) return false;
  ImportReport report;
  if (!ImportText(text, &report, /*keepExisting=*/false)) {
    if (error != nullptr) *error = report.error;
    return false;
  }
  loaded_ = true;
  dirty_ = false;
  return true;
}

std::size_t PresetStore::DiffFromPreset(const std::string& name) const {
  const int index = IndexOf(name);
  if (index < 0) return 0;
  if (IsImplicitDefault(static_cast<std::size_t>(index))) {
    return registry_.ChangedFromDefault();
  }
  const Preset& preset = document_.presets[static_cast<std::size_t>(index)];
  std::size_t diff = 0;
  for (const SnapshotEntry& entry : preset.values.entries) {
    const ParamValue current = registry_.Get(entry.target, entry.param);
    if (!current.Equals(entry.value)) ++diff;
  }
  return diff;
}

}  // namespace glass
}  // namespace aurora
