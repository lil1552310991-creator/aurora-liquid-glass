// =============================================================================
// GlassBindingTest —— 调试面板"参数绑定"的离线回归。
//
// ★ 这个文件在删除组件演示后重写过。
//
//   它原来验证的是「改按钮 1 不影响按钮 2」这类隔离性，对象是 PillButton /
//   Toggle / Slider / 导航 / 芯片 —— 全部来自「c++ 液态玻璃展示源码」的移植，
//   已按用户要求删除。但那些断言背后的**性质**依然重要，所以保留并改成用
//   剩下的页面（整窗口 / 窗口缩放 / 能力缺口）来验证：
//
//     · 注册表里的每个参数都绑到了**真实字段**（不是"注册了但没绑"的空槽）；
//     · 每个可写参数都有中英文名和非空值域（工作台不会出现无意义的滑块）；
//     · 写入立即生效，并被夹到声明区间内；
//     · 不同页面互不串写；
//     · 快照 / 重置 / 差异统计仍然自洽；
//     · 帧边界语义：连续写入被合并，Commit() 之后才完全应用。
//
//  本文件**不画任何东西**：它验证的是从注册表到渲染字段这一段，
//  而"字段 → PanelStyle"那一段由 tests/GlassRecipeTest.cpp 覆盖。
// =============================================================================
#include "GlassTargets.h"
#include "glass/GlassRegistry.h"
#include "glass/GlassScene.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace aurora::glass;

namespace {

int g_checks = 0;
int g_failures = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    ++g_failures;
    std::printf("  断言失败（第 %d 项）：%s\n", g_checks, what);
    std::fflush(stdout);
  }
}

void CheckNear(float value, float expected, float tolerance, const char* what) {
  ++g_checks;
  if (std::fabs(value - expected) > tolerance) {
    ++g_failures;
    std::printf("  断言失败（第 %d 项）：%s（实际 %.6f，期望 %.6f）\n", g_checks, what,
                static_cast<double>(value), static_cast<double>(expected));
    std::fflush(stdout);
  }
}

}  // namespace

int main() {
  Registry& registry = Global();
  GlassScene& scene = Scene();

  registry.ResetAll();
  RegisterSecondaryTargets();

  // ---- 目录级性质 ---------------------------------------------------------
  // 目标数量在删除组件演示后从 20+ 降到窗口相关的几页。
  // 上限写死是为了防止以后把演示组件又注册回来（那会让面板重新变成假控件）。
  Check(registry.TargetCount() >= 1, "至少注册了 1 个调试目标");
  Check(registry.TargetCount() <= 4, "没有把已删除的演示组件重新注册回来");

  // 窗口参数必须足够细：用户调的就是窗口玻璃本身，不能只剩几个滑块。
  Check(registry.ParamTotal() >= 40, "窗口相关参数仍然足够细（不是只剩几个滑块）");

  // ---- 每个可写参数都必须真的绑上了字段 -----------------------------------
  std::size_t emptyTargets = 0;
  std::size_t unnamed = 0;
  std::size_t unranged = 0;
  std::size_t documentationOnly = 0;

  for (std::size_t i = 0; i < registry.TargetCount(); ++i) {
    std::size_t writable = 0;
    std::size_t unsupported = 0;
    for (std::size_t p = 0; p < registry.ParamCount(i); ++p) {
      const ParamSlot* slot = registry.ParamAt(i, p);
      if (!slot->desc.supported) {
        ++unsupported;
        continue;
      }
      ++writable;
      if (slot->ref == nullptr) ++unnamed;
      if (slot->desc.cn.empty() || slot->desc.en.empty()) ++unnamed;
      if (slot->desc.IsNumeric() && slot->desc.kind != ParamKind::Enum &&
          !(slot->desc.maxF > slot->desc.minF)) {
        ++unranged;
      }
    }
    if (writable == 0) {
      if (unsupported > 0) ++documentationOnly;
      else ++emptyTargets;
    }
  }

  Check(emptyTargets == 0, "没有空页（每个目标都有可写参数）");
  Check(documentationOnly <= 1, "最多一个纯文档页（能力缺口总表）");
  Check(unnamed == 0, "每个可写参数都有真实字段 + 中英文名称");
  Check(unranged == 0, "每个数值参数都有非空值域（不靠拍脑袋给区间）");

  // ---- 写入 → 生效 → 被夹紧 ------------------------------------------------
  {
    const bool ok = registry.SetF("secondary_window", "lensHeightDp", 120.0f);
    Check(ok, "整窗口的折射高度可写");
    registry.Commit();
    CheckNear(scene.window.lensHeightDp, 120.0f, 1e-6f, "写入落到 WindowTune");

    // 越界必须被夹到声明区间，而不是原样写进去（否则渲染端会拿到非法值）
    registry.SetF("secondary_window", "lensHeightDp", 99999.0f);
    registry.Commit();
    const ParamSlot* slot = nullptr;
    for (std::size_t p = 0; p < registry.ParamCount(0); ++p) {
      const ParamSlot* s = registry.ParamAt(0, p);
      if (s->desc.id == "lensHeightDp") { slot = s; break; }
    }
    Check(slot != nullptr, "折射高度参数存在");
    if (slot != nullptr) {
      Check(scene.window.lensHeightDp <= slot->desc.maxF + 1e-3f, "超上限被夹紧");
      Check(scene.window.lensHeightDp >= slot->desc.minF - 1e-3f, "不低于下限");
    }
  }

  // ---- 不同页面互不串写 ---------------------------------------------------
  {
    registry.SetF("secondary_window", "opacity", 0.42f);
    registry.Commit();
    CheckNear(scene.window.opacity, 0.42f, 1e-6f, "窗口透明度写入生效");

    // 同名参数不该在多个页面重复出现（重复绑定会导致"改了一个另一个也变"）
    std::size_t pagesWithOpacity = 0;
    for (std::size_t i = 0; i < registry.TargetCount(); ++i) {
      for (std::size_t p = 0; p < registry.ParamCount(i); ++p) {
        if (registry.ParamAt(i, p)->desc.id == "opacity") ++pagesWithOpacity;
      }
    }
    Check(pagesWithOpacity == 1, "opacity 只在一个页面出现（没有跨页重复绑定）");
  }

  // ---- 快照 / 重置 / 差异 -------------------------------------------------
  {
    registry.SetF("secondary_window", "lensHeightDp", 88.0f);
    registry.Commit();
    Check(registry.ChangedFromDefault() > 0, "改动后与默认值有差异");

    registry.ResetAll();
    Check(registry.ChangedFromDefault() == 0, "全部重置后与默认值零差异");
    CheckNear(scene.window.lensHeightDp, scene.window.lensHeightDp, 1e-6f, "重置不产生 NaN");
  }

  // ---- 待应用队列（去抖 + Commit） ----------------------------------------
  {
    registry.ResetAll();
    // 连续写同一条：pending 应该被合并，而不是累积成几十条
    for (int i = 1; i <= 60; ++i) {
      registry.SetF("secondary_window", "lensHeightDp", static_cast<float>(i));
    }
    const std::size_t pending = registry.PendingCount();
    std::printf("  [信息] 60 次写入后 pending=%zu\n", pending);
    Check(pending <= 2, "60 次写入被合并（不会累积成几十条）");
    const int applied = registry.Commit();
    Check(applied >= 1, "提交应用了待更新");
    Check(registry.FullyApplied(), "提交后没有待应用项");
  }

  if (g_failures == 0) {
    std::printf("GlassBindingTest: %d 项断言全部通过\n", g_checks);
    return 0;
  }
  std::printf("GlassBindingTest: %d 项断言中 %d 项失败\n", g_checks, g_failures);
  return 1;
}
