// =============================================================================
//  参数清单导出（宿主机工具，**不进设备产物**）
//
//  为什么要有它：任务要求最终报告给出"完整参数清单"与"参数 ↔ 代码位置对照"。
//  手写这份清单必然与代码漂移（改一个绑定就过期）。所以直接从**组件注册表**
//  导出 Markdown —— 报告里的每一行都是注册时真实绑定的那条记录。
//
//  用法（在仓库根目录）：
//      pwsh -File tools/gen_glass_param_report.ps1
//    或者手工：
//      g++ -std=c++20 -O1 -Iapp/src/main/cpp -Iapp/src/main/cpp/third_party/liquidglass \
//          -Iapp/src/main/cpp/third_party/liquidglass/ytbl \
//          -o /tmp/report tools/glass_param_report.cpp tests/host_glass_stubs.cpp \
//          app/src/main/cpp/GlassTargets.cpp app/src/main/cpp/glass/*.cpp \
//          app/src/main/cpp/third_party/liquidglass/ytbl/{GlassRecipes,Toggle,Slider,
//          PillButton,TabBar,PopupMenu,Dialog,SpineSolver,BezierPath,Icons,ColorAdapter,
//          Runtime,SpringSim}.cpp
//      /tmp/report > docs/GLASS_INSPECTOR_PARAMETERS.md
// =============================================================================
#include "GlassTargets.h"
#include "glass/GlassRegistry.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace aurora::glass;

namespace {

std::string Escape(const std::string& text) {
  std::string out;
  for (char ch : text) {
    if (ch == '|') out += "\\|";
    else out += ch;
  }
  return out;
}

const char* CostText(ParamCost cost) {
  switch (cost) {
    case ParamCost::Uniform: return "即时";
    case ParamCost::EffectChain: return "帧边界·效果链";
    case ParamCost::Pipeline: return "帧边界·采样";
  }
  return "?";
}

void PrintRange(const ParamDesc& desc) {
  switch (desc.kind) {
    case ParamKind::Float:
      std::printf("%.3g ~ %.3g%s", static_cast<double>(desc.minF),
                  static_cast<double>(desc.maxF),
                  desc.unit.empty() ? "" : (" " + desc.unit).c_str());
      break;
    case ParamKind::Int:
    case ParamKind::Enum:
      std::printf("%d ~ %d", desc.minI, desc.maxI);
      break;
    case ParamKind::Bool:
      std::printf("开 / 关");
      break;
    case ParamKind::Color:
      std::printf("RGBA 0 ~ 1");
      break;
  }
}

}  // namespace

int main() {
  Registry& registry = Global();
  RegisterSecondaryTargets();

  std::printf("# 液态玻璃参数清单（自动生成）\n\n");
  std::printf("> ★ 本文件由 `tools/glass_param_report.cpp` **从组件注册表直接导出**，"
              "请勿手工编辑。\n");
  std::printf("> 重新生成：`pwsh -File tools/gen_glass_param_report.ps1`\n\n");

  const std::vector<std::size_t>& order = registry.DisplayOrder();
  std::printf("## 总览\n\n");
  std::printf("| 指标 | 数值 |\n| --- | --- |\n");
  std::printf("| 调试目标（组件 / 子层） | %zu |\n", registry.TargetCount());
  std::printf("| 参数条目 | %zu |\n", registry.ParamTotal());
  std::printf("| 其中可读写 | %zu |\n",
              registry.ParamTotal() - registry.UnsupportedCount());
  std::printf("| 能力缺口（只读 + 原因） | %zu |\n", registry.UnsupportedCount());
  std::printf("| 顶层对象（分页栏条目） | ");
  std::size_t topLevel = 0;
  for (std::size_t index : order) {
    if (registry.TargetAt(index).desc.parent == "secondary_window") ++topLevel;
  }
  std::printf("%zu |\n\n", topLevel);

  std::printf("## 目标一览\n\n");
  std::printf("| # | 目标 ID | 名称 | 类型 | 父目标 | 参数数 | 可写 | 缺口 |\n");
  std::printf("| --- | --- | --- | --- | --- | --- | --- | --- |\n");
  int row = 0;
  for (std::size_t index : order) {
    const Target& target = registry.TargetAt(index);
    std::size_t writable = 0;
    std::size_t gaps = 0;
    for (const ParamSlot& slot : target.params) {
      if (slot.desc.supported && slot.ref != nullptr) ++writable;
      if (!slot.desc.supported) ++gaps;
    }
    std::printf("| %d | `%s` | %s | %s | `%s` | %zu | %zu | %zu |\n", ++row,
                target.desc.id.c_str(), Escape(target.desc.name).c_str(),
                target.desc.type.c_str(),
                target.desc.parent.empty() ? "-" : target.desc.parent.c_str(),
                target.params.size(), writable, gaps);
  }
  std::printf("\n");

  std::printf("## 参数明细\n\n");
  std::printf("「实现位置」是参数真正被渲染读取的字段 / 常量所在处；"
              "「成本」决定更新策略（即时 = 当帧；帧边界 = 合并后按帧提交）。\n\n");
  for (std::size_t index : order) {
    const Target& target = registry.TargetAt(index);
    std::printf("### %s（`%s`）\n\n", Escape(target.desc.name).c_str(),
                target.desc.id.c_str());
    if (!target.desc.note.empty()) std::printf("%s\n\n", Escape(target.desc.note).c_str());
    std::printf("| 分组 | 参数 ID | 中文名 | 英文原名 | 类型 | 值域 | 默认值 | 单位 | 成本 | 实现位置 |\n");
    std::printf("| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |\n");
    for (const ParamSlot& slot : target.params) {
      const SourceRef* source = GetSourceRef(target.desc.id, slot.desc.id);
      std::string where = "—";
      if (source != nullptr && !source->file.empty()) {
        where = "`" + source->symbol + "` @ " + source->file;
      }
      if (!slot.desc.supported) {
        std::printf("| %s | `%s` | %s | %s | 缺口 | — | — | — | — | %s |\n",
                    Escape(slot.desc.group).c_str(), slot.desc.id.c_str(),
                    Escape(slot.desc.cn).c_str(), Escape(slot.desc.en).c_str(),
                    Escape(slot.desc.unsupportedReason).c_str());
        continue;
      }
      std::printf("| %s | `%s` | %s | %s | %s | ", Escape(slot.desc.group).c_str(),
                  slot.desc.id.c_str(), Escape(slot.desc.cn).c_str(),
                  Escape(slot.desc.en).c_str(), ParamKindName(slot.desc.kind));
      PrintRange(slot.desc);
      std::printf(" | %s | %s | %s | %s |\n", slot.def.ToText().c_str(),
                  slot.desc.unit.empty() ? "—" : slot.desc.unit.c_str(),
                  CostText(slot.desc.cost), where.c_str());
    }
    std::printf("\n");
  }

  std::printf("## 约束关系\n\n");
  std::printf("| 目标 | 参数 | 约束 |\n| --- | --- | --- |\n");
  for (std::size_t index : order) {
    const Target& target = registry.TargetAt(index);
    for (const ParamSlot& slot : target.params) {
      if (slot.desc.constraint.empty()) continue;
      std::printf("| `%s` | `%s` | %s |\n", target.desc.id.c_str(), slot.desc.id.c_str(),
                  Escape(slot.desc.constraint).c_str());
    }
  }
  std::printf("\n");
  return 0;
}
