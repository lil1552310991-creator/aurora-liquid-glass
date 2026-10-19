// =============================================================================
//  "注册表 → 真实渲染字段" 的离线回归
//
//  这是整套调试工作台最关键的一条证明：**主窗口里拖的滑块，到底写没写进
//  渲染器真正读的那个字段**。做法是把生产代码里的两条真实链路接起来：
//
//      GlassTargets.cpp  注册（把 descriptor 绑到 &Scene().toggle[0].tune.xxx）
//      GlassScene.h/cpp  存储（渲染端每帧读的就是这些字段）
//
//  然后逐条断言：
//    · 注册后的参数总数 = 绑定表里登记的条数（没有"注册了但没绑"的空槽）；
//    · 每个目标都能被独立读写，且**写按钮 1 不会动按钮 2**；
//    · 值域与算法一致（blur ≤ 10dp、圆角 ≤ …），越界被夹紧；
//    · 有子层的组件（开关/进度条/导航栏）父层与子层各自独立；
//    · 帧边界语义：blur 这类高成本参数在 Commit() 之前不改变渲染读到的值。
//
//  本文件**不画任何东西**：它验证的是从注册表到渲染字段这一段，
//  而"字段 → PanelStyle"那一段由 tests/GlassRecipeTest.cpp 覆盖。
// =============================================================================
#include "GlassTargets.h"
#include "glass/GlassRegistry.h"
#include "glass/GlassScene.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurora::glass;

namespace {

int g_checks = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    std::printf("  断言失败（第 %d 项）：%s\n", g_checks, what);
    std::fflush(stdout);
    assert(condition);
  }
}

void CheckNear(float actual, float expected, float epsilon, const char* what) {
  ++g_checks;
  if (!(std::fabs(actual - expected) <= epsilon)) {
    std::printf("  断言失败（第 %d 项）：%s（实际 %.6f，期望 %.6f）\n", g_checks, what,
                static_cast<double>(actual), static_cast<double>(expected));
    std::fflush(stdout);
    assert(false);
  }
}

}  // namespace

int main() {
  std::printf("GlassBindingTest: 注册表 → 真实渲染字段回归\n");

  Registry& registry = Global();
  registry.Clear();
  RegisterSecondaryTargets();

  // =========================================================================
  // 组 1：注册完整性
  // =========================================================================
  {
    Check(registry.TargetCount() >= 20, "二级窗口注册了 20 个以上的调试目标");
    Check(registry.ParamTotal() >= 300,
          "参数总数达到数百条（不是「只有几个常用滑块」）");
    Check(registry.UnsupportedCount() >= 10, "缺口条目被如实登记（只读 + 原因）");

    // 每个目标都必须至少有一个可写参数 —— **除了**纯文档页（能力缺口总表：
    // 它登记的每一条都是"参考项目有、本实现没有"，按设计没有任何可写参数）。
    std::size_t emptyTargets = 0;
    std::size_t documentationOnly = 0;
    for (std::size_t i = 0; i < registry.TargetCount(); ++i) {
      std::size_t writable = 0;
      std::size_t unsupported = 0;
      for (std::size_t p = 0; p < registry.ParamCount(i); ++p) {
        const ParamSlot* slot = registry.ParamAt(i, p);
        if (slot->desc.supported && slot->ref != nullptr) ++writable;
        if (!slot->desc.supported) ++unsupported;
      }
      if (writable == 0) {
        if (unsupported > 0) ++documentationOnly;
        else ++emptyTargets;
      }
    }
    Check(emptyTargets == 0, "没有空页（每个目标都有可写参数）");
    Check(documentationOnly <= 1, "最多一个纯文档页（能力缺口总表）");

    // 每个可写参数都必须绑定了真实字段，并且有中文名 / 英文名 / 值域说明
    std::size_t unnamed = 0;
    std::size_t unranged = 0;
    for (std::size_t i = 0; i < registry.TargetCount(); ++i) {
      for (std::size_t p = 0; p < registry.ParamCount(i); ++p) {
        const ParamSlot* slot = registry.ParamAt(i, p);
        if (!slot->desc.supported) continue;
        if (slot->ref == nullptr) ++unnamed;
        if (slot->desc.cn.empty() || slot->desc.en.empty()) ++unnamed;
        if (slot->desc.IsNumeric() && slot->desc.kind != ParamKind::Enum &&
            !(slot->desc.maxF > slot->desc.minF)) {
          ++unranged;
        }
      }
    }
    Check(unnamed == 0, "每个可写参数都有真实字段 + 中英文名称");
    Check(unranged == 0, "每个数值参数都有非空值域（不靠拍脑袋给区间）");
  }

  // =========================================================================
  // 组 2：写进去的就是渲染读的那个字段
  // =========================================================================
  {
    GlassScene& scene = Scene();
    const float before = scene.toggles[0].tune.lensHeightPressDp;
    Check(registry.SetF("secondary_window.toggle_1.thumb", "lensHeightPressDp", 21.5f),
          "写入开关 1 圆钮的折射高度");
    CheckNear(scene.toggles[0].tune.lensHeightPressDp, 21.5f, 1e-6f,
              "注册表写入直接落在渲染字段上（结构上不可能「只改 UI」）");
    CheckNear(scene.toggles[1].tune.lensHeightPressDp, before, 1e-6f,
              "开关 2 的同一个字段完全没变（每实例一份参数）");
    CheckNear(scene.toggles[2].tune.lensHeightPressDp, before, 1e-6f, "开关 3 也没变");

    // 值域来自算法：TOGGLE_LENS_HEIGHT_DP 的上限给了 40dp
    registry.SetF("secondary_window.toggle_1.thumb", "lensHeightPressDp", 999.0f);
    CheckNear(scene.toggles[0].tune.lensHeightPressDp, 40.0f, 1e-6f, "越界被夹到参数上限");
    registry.SetF("secondary_window.toggle_1.thumb", "lensHeightPressDp", -5.0f);
    CheckNear(scene.toggles[0].tune.lensHeightPressDp, 0.0f, 1e-6f, "负值被夹到下限");
  }

  // =========================================================================
  // 组 3：按钮 1 / 按钮 2 完全隔离（任务书验收项 3）
  // =========================================================================
  {
    GlassScene& scene = Scene();
    const float button2Blur = scene.buttons[2].blurDp;
    const float button2Lens = scene.buttons[2].lensAmountDp;
    const float shadow2 = scene.buttons[2].tune.shadowRadiusDp;

    Check(registry.SetF("secondary_window.pill_button_1", "blurDp", 8.0f), "改按钮 1 模糊");
    Check(registry.SetF("secondary_window.pill_button_1", "lensAmountDp", 50.0f),
          "改按钮 1 折射");
    Check(registry.SetI("secondary_window.pill_button_1", "edgeMode", 4), "改按钮 1 边光模式");
    Check(registry.SetColor("secondary_window.pill_button_1", "shadowTint", 1.0f, 0.0f, 0.0f, 1.0f),
          "改按钮 1 阴影颜色");
    registry.SetF("secondary_window.pill_button_1", "shadowRadiusDp", 40.0f);

    CheckNear(scene.buttons[0].blurDp, 8.0f, 1e-6f, "按钮 1 自己的模糊变了");
    CheckNear(scene.buttons[0].lensAmountDp, 50.0f, 1e-6f, "按钮 1 自己的折射强度变了");
    Check(scene.buttons[0].tune.edge.mode == 4, "按钮 1 的边光模式被改成 Contour");

    CheckNear(scene.buttons[2].blurDp, button2Blur, 1e-6f, "按钮 3 的模糊没有被改到");
    CheckNear(scene.buttons[2].lensAmountDp, button2Lens, 1e-6f, "按钮 3 的折射没有被改到");
    CheckNear(scene.buttons[2].tune.shadowRadiusDp, shadow2, 1e-6f,
              "按钮 3 的阴影半径没有被改到");
    Check(scene.buttons[2].tune.edge.mode != 4, "按钮 3 的边光模式没有被改到");
    CheckNear(scene.buttons[2].tune.shadowTint[0], 0.0f, 1e-6f, "按钮 3 的阴影颜色没被改到");
  }

  // =========================================================================
  // 组 4：整窗口的参数不会覆盖子组件（验收项 2）
  // =========================================================================
  {
    GlassScene& scene = Scene();
    const float cardBlur = scene.card.blurDp;
    const float cardLens = scene.card.lensHeightDp;
    registry.SetF("secondary_window", "lensHeightDp", 120.0f);
    registry.SetF("secondary_window", "cornerDp", 40.0f);
    CheckNear(scene.window.lensHeightDp, 120.0f, 1e-6f, "整窗口的折射高度生效");
    CheckNear(scene.window.cornerDp, 40.0f, 1e-6f, "整窗口的圆角生效");
    CheckNear(scene.card.lensHeightDp, cardLens, 1e-6f, "卡片的折射高度没有被整窗口改到");
    CheckNear(scene.card.blurDp, cardBlur, 1e-6f, "卡片的模糊没有被整窗口改到");
    CheckNear(scene.toggles[0].tune.lensHeightPressDp, 0.0f, 1e-6f,
              "开关圆钮的折射也没有被整窗口改到");
    CheckNear(scene.buttons[1].lensHeightDp, lgx::config::Layout::BUTTON_LENS_HEIGHT_DP, 1e-6f,
              "按钮的折射高度没有被整窗口改到");
  }

  // =========================================================================
  // 组 5：帧边界语义（高成本参数）与合并队列
  // =========================================================================
  {
    GlassScene& scene = Scene();
    registry.Commit();   // 先清空
    const float appliedBefore = scene.window.blurAppliedDp;

    // 连写 60 次（模拟拖一次滑块），中间不提交
    for (int i = 0; i < 60; ++i) {
      registry.SetF("secondary_window", "blurDp", 1.0f + static_cast<float>(i) * 0.1f);
    }
    CheckNear(scene.window.blurDp, 6.9f, 1e-4f, "请求值就是最新值（滑块显示的数）");
    CheckNear(scene.window.blurAppliedDp, appliedBefore, 1e-6f,
              "未提交前，渲染读到的生效值**仍是上一帧的值**（帧边界生效）");
    Check(registry.PendingCount() == 1, "60 次写入被合并成 1 条待应用");
    const std::size_t applied = registry.Commit();
    Check(applied >= 1, "提交应用了待更新");
    CheckNear(scene.window.blurAppliedDp, 6.9f, 1e-4f, "提交后生效值 = 最新请求值");
    Check(registry.FullyApplied(), "提交后没有待应用项");

    // 便宜的 uniform 参数不需要等提交：它们本来就是当帧直读
    registry.SetF("secondary_window", "lensAmountDp", 77.0f);
    CheckNear(scene.window.lensAmountDp, 77.0f, 1e-6f, "普通参数即时写入（零延迟）");
  }

  // =========================================================================
  // 组 6：子层各自独立（验收项 1 的"子层"部分）
  // =========================================================================
  {
    GlassScene& scene = Scene();
    const float thumbWidth = scene.toggles[0].tune.thumbWidthDp;
    Check(registry.SetF("secondary_window.toggle_1.track", "widthDp", 96.0f),
          "改开关 1 的轨道宽度");
    CheckNear(scene.toggles[0].tune.trackWidthDp, 96.0f, 1e-6f, "轨道宽度生效");
    CheckNear(scene.toggles[0].tune.thumbWidthDp, thumbWidth, 1e-6f,
              "圆钮宽度没有被轨道参数改到（父子层隔离）");

    Check(registry.SetF("secondary_window.toggle_1.thumb", "widthDp", 52.0f), "改圆钮宽度");
    CheckNear(scene.toggles[0].tune.thumbWidthDp, 52.0f, 1e-6f, "圆钮宽度生效");
    CheckNear(scene.toggles[0].tune.trackWidthDp, 96.0f, 1e-6f, "轨道宽度保持不变");

    // 导航栏：背景层与指示器层
    Check(registry.SetF("secondary_window.navigation.background", "blurDp", 3.0f),
          "改导航栏背景层模糊");
    Check(registry.SetF("secondary_window.navigation.indicator", "pressLensAmountDp", 33.0f),
          "改导航栏指示器折射");
    CheckNear(scene.tabs.tune.blurDp, 3.0f, 1e-6f, "背景层模糊生效");
    CheckNear(scene.tabs.tune.pressLensAmountDp, 33.0f, 1e-6f, "指示器折射生效");
    CheckNear(scene.tabs.tune.lensAmountDp, lgx::config::Layout::TAB_LENS_AMOUNT_DP, 1e-6f,
              "背景层的折射强度没有被指示器的参数改到");

    // 进度条：轨道与滑杆
    Check(registry.SetF("secondary_window.slider_1.track", "heightDp", 10.0f), "改轨道高度");
    CheckNear(scene.sliders[0].tune.trackHeightDp, 10.0f, 1e-6f, "轨道高度生效");
    CheckNear(scene.sliders[0].tune.thumbHeightDp,
              lgx::config::Layout::SLIDER_THUMB_H_DP, 1e-6f, "滑杆高度没被改到");
  }

  // =========================================================================
  // 组 7：快照 / 重置 / 撤销（把 §8 的语义在这条真实链路上再验一遍）
  // =========================================================================
  {
    GlassScene& scene = Scene();
    registry.ResetAll(ChangeOrigin::Reset);
    registry.Commit();
    Check(registry.ChangedFromDefault() == 0, "全部重置后与默认值零差异");
    CheckNear(scene.window.blurDp, 10.0f, 1e-6f, "整窗口模糊回到注册时的默认值");
    CheckNear(scene.buttons[0].blurDp, lgx::config::Layout::BUTTON_BLUR_DP, 1e-6f,
              "按钮 1 模糊回到默认值");

    registry.SetF("secondary_window.pill_button_1", "blurDp", 4.5f);
    registry.SetF("secondary_window.toggle_2.thumb", "lensGain", 2.0f);
    const Snapshot snapshot = registry.Capture();
    registry.ResetAll(ChangeOrigin::Reset);
    CheckNear(scene.buttons[0].blurDp, lgx::config::Layout::BUTTON_BLUR_DP, 1e-6f,
              "重置后按钮 1 回到默认");

    std::vector<std::string> skipped;
    registry.Apply(snapshot, ChangeOrigin::Preset, &skipped);
    Check(skipped.empty(), "整份快照应用时没有被跳过的条目");
    CheckNear(scene.buttons[0].blurDp, 4.5f, 1e-6f, "快照恢复按钮 1 的值");
    CheckNear(scene.toggles[1].tune.lensGain, 2.0f, 1e-6f, "快照恢复开关 2 圆钮的值");

    // 差异快照只包含偏离项
    const Snapshot diff = registry.CaptureDiffFromDefault();
    Check(diff.size() == 2, "差异快照恰好 2 条");
  }

  // =========================================================================
  // 组 8：检索与层级（分页栏据此生成，验收项 1）
  // =========================================================================
  {
    const std::vector<const Target*> toggles = registry.FindByType("Toggle");
    Check(toggles.size() == 3, "按类型检索到 3 个开关");
    Check(registry.ChildrenOf("secondary_window.toggle_1").size() == 2,
          "开关 1 有 2 个子层（轨道 + 圆钮）");
    Check(registry.Search("折射").size() >= 5, "关键词「折射」能搜到多个目标");
    Check(registry.Search("toggle_2").size() >= 1, "按 ID 片段检索");
    Check(registry.Search("绝对不存在的名字").empty(), "搜不到时返回空（UI 显示「没有匹配」）");

    // 顶层目标（分页栏的内容）= parent == secondary_window
    std::size_t topLevel = 0;
    for (std::size_t i = 0; i < registry.TargetCount(); ++i) {
      if (registry.TargetAt(i).desc.parent == "secondary_window") ++topLevel;
    }
    Check(topLevel >= 15, "顶层调试对象足够多（分页栏需要横向滚动）");
    Check(topLevel < registry.TargetCount(), "子层不在分页栏里（页面内选择）");
  }

  std::printf("GlassBindingTest: %d 项断言通过（8 组）\n", g_checks);
  return 0;
}
