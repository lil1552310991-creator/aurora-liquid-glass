// =============================================================================
//  参数配置持久化（纯逻辑，零 Android / ImGui 依赖）
//
//  · 版本化 JSON（当前 v2），带 v1 迁移：旧版是"按组件分组的一层平铺"
//    （{"groups":{"window":{...},"card":{...}}}），v2 是"按调试目标分页 +
//    目标内按参数 ID"的两层结构。两者结构不同，必须真的迁移而不是硬读。
//  · 参数改名表：旧字段名 → 新参数 ID（例如 spectral/dispersion 归一成 dispersion）。
//  · 原子保存：先写临时文件 + fsync，再 rename 覆盖 —— 意外退出不会留下半个文件。
//  · 多预设：一份文档里存多套参数，带 active 名字。
// =============================================================================
#pragma once

#include "GlassJson.h"
#include "GlassRegistry.h"

#include <cstdint>
#include <string>
#include <vector>

namespace aurora {
namespace glass {

inline constexpr int kPresetFormatVersion = 2;

struct Preset {
  std::string name;
  std::string note;
  std::int64_t savedAt = 0;   // unix 秒
  Snapshot values;
};

// 导入结果：不只是"成功/失败"，必须能说清**哪些条目被丢掉了、为什么**
struct ImportReport {
  int sourceVersion = 0;
  int migratedFrom = 0;            // 非 0 表示做过版本迁移
  std::size_t applied = 0;
  std::size_t skipped = 0;         // 结构上无法解析、被丢弃的条目
  std::size_t unmatched = 0;       // 结构合法但当前注册表里没有对应目标/参数
  std::vector<std::string> notes;   // 迁移说明 / 跳过原因（每条一行）
  std::string error;                // 非空 ⇒ 整个导入失败

  bool ok() const { return error.empty(); }
};

struct PresetDocument {
  int version = kPresetFormatVersion;
  std::string active = "默认";
  std::string source;              // 生成者描述（设备/版本），仅诊断用
  std::vector<Preset> presets;
};

// ---- 文档 <-> JSON ----
JsonValue PresetToJson(const PresetDocument& document);
bool PresetFromJson(const JsonValue& root, PresetDocument* out, ImportReport* report);

// ---- 迁移辅助（离线可测）----
// 旧版按组件分组的组名 → 新的调试目标 ID
const char* LegacyGroupTarget(const char* group);
// 旧版字段名 → 新参数 ID（返回 nullptr 表示同名）
const char* MigrateParamId(const char* param);

// ---- 文件（原子保存）----
bool SaveTextAtomic(const std::string& path, const std::string& text, std::string* error);
bool ReadTextFile(const std::string& path, std::string* text, std::string* error);
bool FileExists(const std::string& path);
bool RemoveFile(const std::string& path);

// 导出时代表"屏幕当前值"的预设名。导入方按 active 应用一次即可 1:1 还原。
inline constexpr const char* kWorkingSetName = "当前工作区";

// -----------------------------------------------------------------------------
// 预设仓库：绑定一个 Registry，负责"保存/应用/导入/导出/多预设"
// -----------------------------------------------------------------------------
class PresetStore {
 public:
  explicit PresetStore(Registry& registry);

  void SetPath(const std::string& path) { path_ = path; }
  const std::string& Path() const { return path_; }

  std::size_t Count() const { return document_.presets.size(); }
  const Preset& At(std::size_t index) const { return document_.presets[index]; }
  int IndexOf(const std::string& name) const;
  const std::string& ActiveName() const { return document_.active; }
  bool SetActiveName(const std::string& name);

  // 用**当前注册表的值**建立/覆盖一个预设
  bool CaptureAs(const std::string& name, bool note = false);
  bool ApplyPreset(const std::string& name, std::vector<std::string>* skipped = nullptr,
                   std::vector<std::string>* adjusted = nullptr);
  bool Rename(std::size_t index, const std::string& name);
  bool Remove(std::size_t index);

  // 导出/导入（整份文档文本；导入默认合并预设）
  // includeWorkingSet=true 时额外快照一份"当前工作区"并设为 active ⇒ 可 1:1 还原屏幕
  std::string ExportText(bool includeWorkingSet = true) const;
  bool ImportText(const std::string& text, ImportReport* report, bool keepExisting = true);

  bool Save(std::string* error);
  // ★ 去抖自动保存专用：序列化在调用线程（无 I/O），写盘 + fsync 交给后台线程。
  //   为什么必须分开：Save() 里的 fsync 在真机上可达几毫秒到几十毫秒，而它原本
  //   长在渲染线程的每帧绘制路径里 ⇒ 每 2 秒一次掉帧（120fps 下就是几十帧）。
  //   返回 false 只在"后台线程起不来"时发生，此时调用方应回退到 Save()。
  bool SaveAsync(std::string* error);
  // 后台保存失败时的原因（空 = 没有失败）。UI 可以据此提示。
  static std::string LastAsyncError();
  bool Load(std::string* error);
  bool loadedFromDisk() const { return loaded_; }

  bool dirty() const { return dirty_; }
  void MarkDirty() { dirty_ = true; }
  std::int64_t lastSaveUnix() const { return lastSave_; }

  // 诊断：注册表里当前值与预设的差异条目数
  std::size_t DiffFromPreset(const std::string& name) const;

  // 「默认」预设是隐式的（值就是注册表现场的默认值），这条判断给 UI 与导出用
  bool IsImplicitDefault(std::size_t index) const;

 private:
  void EnsureDefaultPreset();

  Registry& registry_;
  PresetDocument document_;
  std::string path_;
  bool dirty_ = false;
  bool loaded_ = false;
  std::int64_t lastSave_ = 0;
};

std::int64_t UnixSecondsNow();

}  // namespace glass
}  // namespace aurora
