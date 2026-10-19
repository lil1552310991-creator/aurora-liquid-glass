// =============================================================================
//  GlassRegistry 离线回归（纯逻辑，不需要手机、不需要 GPU）
//
//  覆盖任务书 §12 的验收项里**可以在宿主上证明**的那几条：
//    1. 每个玻璃组件能独立出现在列表（动态注册 / 注销）
//    3. 调整按钮 1 不会意外改变按钮 2（参数按目标隔离）
//    5. 滑块显示的数值与底层实际值一致（值就是字段本身）
//    6. 快速滑动时高频写入被合并，只应用最新值
//    9. 重置可以准确恢复初始配置
//    12. 不支持的参数被标记出来，且不可写
// =============================================================================
#include "glass/GlassRegistry.h"

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
    std::fflush(stdout);   // 断言会 abort，缓冲区必须立刻刷出去，否则看不到是哪一条
    assert(condition);
  }
}

// 模拟渲染实现里的字段（真实项目里这些字段住在 Toggle::Tune / PanelStyle 里）
struct FakeToggleTune {
  float widthDp = 64.0f;
  float heightDp = 28.0f;
  float thumbWidthDp = 40.0f;
  float lensHeightDp = 5.0f;
  float lensAmountDp = 10.0f;
  float blurRestDp = 4.0f;
  float blurPressDp = 1.0f;
  int edgeMode = 2;
  bool spectralOn = true;
  float accent[4] = {0.19f, 0.82f, 0.35f, 1.0f};
};

struct FakeButtonTune {
  float widthDp = 160.0f;
  float blurDp = 2.0f;
  float lensHeightDp = 12.0f;
  bool shadowOn = true;
};

FakeToggleTune g_toggle1;
FakeToggleTune g_toggle2;
FakeButtonTune g_button1;
FakeButtonTune g_button2;

// 注：组件自己改自己的那条反向通道在组 6 末尾验证（不需要单独的全局状态）。

void RegisterToggle(Registry& registry, const char* id, const char* name, FakeToggleTune* tune,
                    int order) {
  TargetDesc desc;
  desc.id = id;
  desc.name = name;
  desc.type = "Toggle";
  desc.window = "secondary";
  desc.parent = "secondary_window";
  desc.order = order;
  TargetBuilder b(registry, desc);
  if (!b.created()) return;
  b.Group("尺寸");
  b.Float("widthDp", "宽度", "trackWidth", &tune->widthDp, 16.0f, 200.0f, "dp", "轨道宽");
  b.Float("heightDp", "高度", "trackHeight", &tune->heightDp, 8.0f, 120.0f, "dp", "轨道高");
  b.Float("thumbWidthDp", "圆钮宽", "thumbWidth", &tune->thumbWidthDp, 8.0f, 160.0f, "dp",
          "圆钮宽");
  b.Group("折射");
  b.Float("lensHeightDp", "折射高度", "lensHeight", &tune->lensHeightDp, 0.0f, 40.0f, "dp",
          "按压时透镜高度");
  b.Float("lensAmountDp", "折射强度", "lensAmount", &tune->lensAmountDp, 0.0f, 60.0f, "dp",
          "按压时折射位移");
  b.Group("模糊");
  b.Float("blurRestDp", "静止模糊", "glassBlurFloor", &tune->blurRestDp, 0.0f, 10.0f, "dp", "");
  b.Float("blurPressDp", "按压模糊", "glassBlur", &tune->blurPressDp, 0.0f, 10.0f, "dp", "");
  b.Group("光影");
  b.Enum("edgeMode", "边光模式", "edgeMode", &tune->edgeMode, 0, 4,
         "关闭\0Gradient\0Ambient\0Plain\0Contour\0", "边光分支");
  b.Bool("spectralOn", "启用色散", "spectral", &tune->spectralOn, "色散开关");
  b.Color("accent", "开启色", "accentColor", tune->accent, "轨道混色目标");
  b.Unsupported("continuousCorner", "连续曲率圆角", "continuousCorner",
                "ytbl 的圆角 SDF 是标准圆角，逐像素连续曲率没有实现", "形状");
}

void RegisterButton(Registry& registry, const char* id, const char* name, FakeButtonTune* tune,
                    int order) {
  TargetDesc desc;
  desc.id = id;
  desc.name = name;
  desc.type = "PillButton";
  desc.window = "secondary";
  desc.parent = "secondary_window";
  desc.order = order;
  TargetBuilder b(registry, desc);
  if (!b.created()) return;
  b.Group("尺寸");
  b.Float("widthDp", "宽度", "width", &tune->widthDp, 40.0f, 400.0f, "dp", "");
  b.Group("模糊");
  b.Float("blurDp", "模糊半径", "blur", &tune->blurDp, 0.0f, 10.0f, "dp", "");
  b.Group("折射");
  b.Float("lensHeightDp", "折射高度", "lensHeight", &tune->lensHeightDp, 0.0f, 40.0f, "dp", "");
  b.Group("光影");
  b.Bool("shadowOn", "外阴影", "shadowOn", &tune->shadowOn, "");
}

}  // namespace

int main() {
  std::printf("GlassRegistryTest: 注册表与参数总线回归\n");

  Registry registry;

  // ---- 组 1：动态注册与注销 ------------------------------------------------
  {
    RegisterToggle(registry, "secondary_window.toggle_1", "开关 1", &g_toggle1, 10);
    RegisterButton(registry, "secondary_window.pill_button_1", "按钮 1", &g_button1, 20);
    Check(registry.TargetCount() == 2, "两个目标被注册");
    Check(registry.ParamTotal() >= 10, "参数总数被统计");

    // 幂等：同一帧（或下一帧）再次注册不会重建参数表、更不会丢值
    registry.SetF("secondary_window.toggle_1", "lensHeightDp", 21.0f);
    RegisterToggle(registry, "secondary_window.toggle_1", "开关 1", &g_toggle1, 10);
    Check(registry.TargetCount() == 2, "重复注册是幂等的");
    Check(std::fabs(g_toggle1.lensHeightDp - 21.0f) < 1e-6f, "重复注册不会重置已调好的值");

    // 动态发现：新组件出现后自动出现在列表里
    RegisterToggle(registry, "secondary_window.toggle_2", "开关 2", &g_toggle2, 11);
    Check(registry.TargetCount() == 3, "新组件自动进入注册表");
    const std::vector<const Target*> toggles = registry.FindByType("Toggle");
    Check(toggles.size() == 2, "按类型检索");
    const std::vector<const Target*> secondary = registry.FindByWindow("secondary");
    Check(secondary.size() == 3, "按所属窗口检索");
    Check(registry.Search("按钮").size() == 1, "按名称关键词检索");
    Check(registry.Search("lensHeight").size() == 3, "按参数英文名检索");
    Check(registry.ChildrenOf("secondary_window").size() == 3, "按父子层级检索");

    // 注销：组件不再绘制时应被回收
    registry.RemoveTarget("secondary_window.toggle_2");
    Check(registry.TargetCount() == 2, "注销生效");
    Check(registry.FindTarget("secondary_window.toggle_2") == nullptr, "注销后查不到");
  }

  // ---- 组 2：目标之间的参数隔离（按钮 1 ≠ 按钮 2）--------------------------
  {
    RegisterButton(registry, "secondary_window.pill_button_2", "按钮 2", &g_button2, 21);
    const float blur1 = g_button1.blurDp;
    const float blur2 = g_button2.blurDp;
    Check(std::fabs(blur1 - blur2) < 1e-6f, "两个按钮初始相同");

    registry.SetF("secondary_window.pill_button_1", "blurDp", 9.0f);
    Check(std::fabs(g_button1.blurDp - 9.0f) < 1e-6f, "按钮 1 的模糊真的写进了渲染字段");
    Check(std::fabs(g_button2.blurDp - blur2) < 1e-6f, "按钮 2 完全不受影响");

    // 值就是字段本身 ⇒ "显示值 == 实际值"是结构性保证，不是靠同步
    const ParamValue readBack = registry.Get("secondary_window.pill_button_1", "blurDp");
    Check(std::fabs(readBack.f - g_button1.blurDp) < 1e-6f, "读回值等于渲染字段");
    Check(readBack.Equals(ParamValue::OfFloat(9.0f)), "读回值等于写入值");
  }

  // ---- 组 3：校验与夹紧 ---------------------------------------------------
  {
    registry.SetF("secondary_window.pill_button_1", "blurDp", 999.0f);
    Check(std::fabs(g_button1.blurDp - 10.0f) < 1e-6f, "超上限被夹到 10dp（ytbl 的真实上限）");
    registry.SetF("secondary_window.pill_button_1", "blurDp", -5.0f);
    Check(std::fabs(g_button1.blurDp) < 1e-6f, "负值被夹到 0");
    const float nan = std::nanf("");
    registry.SetF("secondary_window.pill_button_1", "blurDp", nan);
    Check(std::isfinite(g_button1.blurDp), "NaN 不会写进渲染字段");

    // 枚举越界
    registry.SetI("secondary_window.toggle_1", "edgeMode", 99);
    Check(g_toggle1.edgeMode == 4, "枚举越界被夹到最大合法值");
    // 颜色通道越界
    float bad[4] = {2.0f, -1.0f, 0.5f, 1.0f};
    registry.Set("secondary_window.toggle_1", "accent", ParamValue::OfColor(bad[0], bad[1],
                                                                            bad[2], bad[3]));
    Check(g_toggle1.accent[0] == 1.0f && g_toggle1.accent[1] == 0.0f, "颜色通道夹进 0..1");

    // 不支持的参数：可读、不可写、且带原因
    const Target* toggle = registry.FindTarget("secondary_window.toggle_1");
    const ParamSlot* gap = toggle->FindParam("continuousCorner");
    Check(gap != nullptr && !gap->desc.supported, "缺口参数被标记为不支持");
    Check(!gap->desc.unsupportedReason.empty(), "缺口参数带原因说明");
    Check(!registry.SetF("secondary_window.toggle_1", "continuousCorner", 1.0f),
          "不支持的参数写入被拒绝（不是假控件）");
    Check(registry.UnsupportedCount() == 1, "缺口数量被统计");
  }

  // ---- 组 4：默认值与重置 -------------------------------------------------
  {
    registry.SetF("secondary_window.toggle_1", "lensHeightDp", 33.0f);
    registry.SetF("secondary_window.toggle_1", "blurRestDp", 7.5f);
    registry.SetI("secondary_window.toggle_1", "edgeMode", 4);
    Check(registry.ChangedFromDefault() >= 3, "偏离默认值的参数被计数");

    registry.ResetParam("secondary_window.toggle_1", "lensHeightDp");
    Check(std::fabs(g_toggle1.lensHeightDp - 5.0f) < 1e-6f, "单项重置回到注册时的值");
    Check(std::fabs(g_toggle1.blurRestDp - 7.5f) < 1e-6f, "单项重置不影响其它参数");

    registry.ResetGroup("secondary_window.toggle_1", "模糊");
    Check(std::fabs(g_toggle1.blurRestDp - 4.0f) < 1e-6f, "分组重置");
    Check(g_toggle1.edgeMode == 4, "分组重置只动本组");

    // 整目标 / 全部重置
    registry.ResetTarget("secondary_window.toggle_1");
    Check(g_toggle1.edgeMode == 2, "整目标重置");
    Check(g_toggle1.accent[0] == 0.19f && g_toggle1.accent[3] == 1.0f, "颜色也回到默认");
    Check(registry.ResetAll() > 0, "全部重置可调用");
    Check(registry.ChangedFromDefault() == 0, "全部重置后与默认值零差异");
  }

  // ---- 组 5：快照 ---------------------------------------------------------
  {
    registry.SetF("secondary_window.pill_button_1", "blurDp", 6.25f);
    registry.SetF("secondary_window.pill_button_1", "lensHeightDp", 18.0f);
    registry.SetF("secondary_window.toggle_1", "lensAmountDp", 44.0f);
    const Snapshot snapshot = registry.Capture();
    Check(snapshot.size() == registry.ParamTotal() - registry.UnsupportedCount(),
          "快照条目 = 可写参数总数");

    const Snapshot buttonOnly = registry.CaptureTarget("secondary_window.pill_button_1");
    Check(buttonOnly.size() >= 4, "按目标捕获");

    registry.ResetAll();
    Check(std::fabs(g_button1.blurDp - 2.0f) < 1e-6f, "重置后回到默认");

    std::vector<std::string> skipped;
    const std::size_t applied = registry.Apply(snapshot, ChangeOrigin::Preset, &skipped);
    Check(applied > 0, "快照被应用");
    Check(std::fabs(g_button1.blurDp - 6.25f) < 1e-6f, "整窗快照恢复按钮模糊");
    Check(std::fabs(g_toggle1.lensAmountDp - 44.0f) < 1e-6f, "整窗快照恢复开关折射");

    // 差异快照：只保存偏离默认值的项（预设/导出的体积控制）
    const Snapshot diff = registry.CaptureDiffFromDefault();
    Check(diff.size() == 3, "差异快照只含 3 条偏离项");

    // 未知目标/参数必须被跳过并报告，而不是静默丢弃
    Snapshot bogus;
    SnapshotEntry entry;
    entry.target = "secondary_window.不存在";
    entry.param = "blurDp";
    entry.value = ParamValue::OfFloat(1.0f);
    bogus.entries.push_back(entry);
    entry.target = "secondary_window.pill_button_1";
    entry.param = "不存在";
    bogus.entries.push_back(entry);
    skipped.clear();
    registry.Apply(bogus, ChangeOrigin::Preset, &skipped);
    Check(skipped.size() == 2, "未知目标/参数都被报告");
  }

  // ---- 组 6：订阅 + 合并队列（滑块与渲染线程解耦）------------------------
  {
    struct Sink {
      std::vector<ParamChange> changes;
      static void OnChange(void* user, const ParamChange& change) {
        static_cast<Sink*>(user)->changes.push_back(change);
      }
    };
    Sink sink;
    registry.Subscribe(&Sink::OnChange, &sink);
    Check(registry.SubscriberCount() == 1, "订阅成功");

    registry.ResetAll();
    sink.changes.clear();
    registry.Commit();                 // 清空上一段留下的待应用队列
    const std::uint64_t mergedBefore = registry.MergedUpdates();
    registry.BeginFrame();

    // 模拟一次滑块拖动：120 帧里每帧写一个新值（同一参数）
    for (int i = 0; i < 120; ++i) {
      registry.SetF("secondary_window.pill_button_1", "blurDp",
                    static_cast<float>(i) * 0.05f);
    }
    Check(registry.PendingCount() == 1, "同一参数的高频写入被合并成 1 条待应用");
    Check(registry.MergedUpdates() - mergedBefore == 119, "被合并掉的过期更新被计数");
    Check(sink.changes.size() == 120, "订阅者仍然看到每一次变化（UI 即时反馈）");
    Check(std::fabs(g_button1.blurDp - 5.95f) < 1e-6f, "字段里就是**最新**值（不落后）");
    Check(!registry.FullyApplied(), "未提交前处于待应用状态（诊断可读）");

    const std::size_t applied = registry.Commit();
    Check(applied == 1, "提交只应用了 1 次");
    Check(registry.FullyApplied(), "提交后完全应用");
    Check(registry.AppliedVersion() == registry.EditVersion(), "编辑版本与生效版本一致");

    // 松手时最后的值一定被提交 —— 再写一次然后立刻提交
    registry.SetF("secondary_window.pill_button_1", "blurDp", 7.5f);
    registry.Commit();
    Check(std::fabs(g_button1.blurDp - 7.5f) < 1e-6f, "松手值一定被提交");

    registry.Unsubscribe(&Sink::OnChange, &sink);
    Check(registry.SubscriberCount() == 0, "取消订阅");

    // ★ 反向通道：**组件自己改自己**（手指点了开关）也必须回到注册表，
    //   主窗口才能显示最新值。这就是 origin=Component 的用途。
    Sink reverse;
    registry.Subscribe(&Sink::OnChange, &reverse);
    registry.SetB("secondary_window.toggle_1", "spectralOn", false, ChangeOrigin::Component);
    Check(reverse.changes.size() == 1, "组件侧改动产生一次通知");
    Check(reverse.changes.front().origin == ChangeOrigin::Component,
          "变化来源被标记为组件（UI 据此判断要不要回写控件）");
    Check(!g_toggle1.spectralOn, "组件状态回流到同一个渲染字段");
    registry.Unsubscribe(&Sink::OnChange, &reverse);
  }

  // ---- 组 7：动态目标的回收 ----------------------------------------------
  {
    TargetDesc desc;
    desc.id = "secondary_window.ephemeral";
    desc.name = "临时组件";
    desc.type = "Chip";
    desc.window = "secondary";
    bool created = false;
    registry.AddTarget(desc, &created);
    Check(created, "临时目标被创建");
    const std::size_t before = registry.TargetCount();
    // 其它目标一直在绘制（每帧都被确认），只有这个临时目标消失了
    for (int frame = 0; frame < 10; ++frame) {
      registry.BeginFrame();
      registry.MarkSeen("secondary_window.toggle_1");
      registry.MarkSeen("secondary_window.pill_button_1");
      registry.MarkSeen("secondary_window.pill_button_2");
    }
    Check(registry.Sweep(4) == 1, "多帧未见的目标被回收");
    Check(registry.TargetCount() == before - 1, "只回收了消失的那一个");
    Check(registry.FindTarget("secondary_window.ephemeral") == nullptr, "回收后查不到");

    // 持续确认存在的目标不会被回收
    registry.BeginFrame();
    registry.MarkSeen("secondary_window.toggle_1");
    registry.BeginFrame();
    registry.MarkSeen("secondary_window.toggle_1");
    Check(registry.Sweep(2) == 0, "一直在绘制的目标不会被回收");
  }

  // ---- 组 8：分页顺序稳定 ------------------------------------------------
  {
    const std::vector<std::size_t>& order = registry.DisplayOrder();
    Check(order.size() == registry.TargetCount(), "分页条目数与目标数一致");
    int previous = -1;
    bool sorted = true;
    for (std::size_t index : order) {
      const int value = registry.TargetAt(index).desc.order;
      if (value < previous) sorted = false;
      previous = value;
    }
    Check(sorted, "分页按 order 升序");
  }

  std::printf("GlassRegistryTest: %d 项断言通过（8 组）\n", g_checks);
  return 0;
}
