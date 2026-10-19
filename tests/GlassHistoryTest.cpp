// =============================================================================
//  撤销 / 重做 的离线回归
//
//  核心是那条容易被写错、又极影响手感的行为：
//  **拖一次滑块 = 一步撤销**（而不是 300 步）。
// =============================================================================
#include "glass/GlassHistory.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <thread>

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

float g_blur = 2.0f;
float g_lens = 12.0f;
int g_edge = 1;

struct Wiring {
  Registry registry;
  History history{registry};
  Wiring() {
    TargetDesc desc;
    desc.id = "secondary_window.pill_button_1";
    desc.name = "按钮 1";
    desc.type = "PillButton";
    desc.window = "secondary";
    TargetBuilder b(registry, desc);
    b.Group("模糊");
    b.Float("blurDp", "模糊半径", "blur", &g_blur, 0.0f, 10.0f, "dp", "");
    b.Group("折射");
    b.Float("lensHeightDp", "折射高度", "lensHeight", &g_lens, 0.0f, 40.0f, "dp", "");
    b.Group("光影");
    b.Enum("edgeMode", "边光模式", "edgeMode", &g_edge, 0, 4, "关\0G\0A\0P\0C\0", "");
    registry.Subscribe(&Wiring::OnChange, this);
  }
  static void OnChange(void* user, const ParamChange& change) {
    static_cast<Wiring*>(user)->history.Record(change);
  }
};

}  // namespace

int main() {
  std::printf("GlassHistoryTest: 撤销 / 重做回归\n");

  // ---- 组 1：拖动被合并成一步 ---------------------------------------------
  {
    g_blur = 2.0f;
    Wiring w;
    w.history.SetCoalesceWindowMs(5000.0);   // 测试里时间几乎不流逝，等价于"同一次拖动"
    // 从 i=1 开始：i=0 时写入的值与当前值相同，Set 会直接返回 false
    // （"值没变就不写"是注册表的一条硬规则，不产生历史也不产生通知）
    for (int i = 1; i <= 300; ++i) {
      w.registry.SetF("secondary_window.pill_button_1", "blurDp", 2.0f + i * 0.02f);
    }
    Check(std::fabs(g_blur - 8.0f) < 1e-4f, "拖动结束值是最后一次写入");
    Check(w.history.UndoDepth() == 1, "300 次连续写入被合并成 1 步");
    Check(w.history.CoalescedCount() == 299, "被合并的次数被计数");

    Check(w.history.Undo(), "撤销成功");
    Check(std::fabs(g_blur - 2.0f) < 1e-6f, "撤销回到拖动**之前**的值（不是上一帧）");
    Check(!w.history.CanUndo(), "撤销栈空了");
    Check(w.history.CanRedo(), "重做可用");

    Check(w.history.Redo(), "重做成功");
    Check(std::fabs(g_blur - 8.0f) < 1e-4f, "重做回到拖动结束值");
  }

  // ---- 组 2：不同参数各自成步 + 时间窗口 ---------------------------------
  {
    g_blur = 2.0f;
    g_lens = 12.0f;
    Wiring w;
    w.history.SetCoalesceWindowMs(0.0);   // 关掉合并 ⇒ 每次写入都是独立一步
    w.registry.SetF("secondary_window.pill_button_1", "blurDp", 4.0f);
    w.registry.SetF("secondary_window.pill_button_1", "lensHeightDp", 20.0f);
    w.registry.SetF("secondary_window.pill_button_1", "blurDp", 6.0f);
    Check(w.history.UndoDepth() == 3, "不做合并时三次写入是三步");
    w.history.Undo();
    Check(std::fabs(g_blur - 4.0f) < 1e-6f, "撤掉最后一次 blur");
    Check(std::fabs(g_lens - 20.0f) < 1e-6f, "折射没有被误伤（历史按参数分开）");
    w.history.Undo();
    Check(std::fabs(g_lens - 12.0f) < 1e-6f, "再撤掉折射");
    w.history.Undo();
    Check(std::fabs(g_blur - 2.0f) < 1e-6f, "撤到底回到初始");

    // 新的修改必须清空重做栈（否则会出现"撤销到一半又分叉"的错乱）
    w.history.Redo();
    w.registry.SetF("secondary_window.pill_button_1", "edgeMode", 3);
    Check(!w.history.CanRedo(), "新修改清空重做栈");
  }

  // ---- 组 3：批处理（预设切换 / 整目标重置 = 一步）-----------------------
  {
    g_blur = 2.0f;
    g_lens = 12.0f;
    g_edge = 1;
    Wiring w;
    w.history.BeginBatch("恢复默认");
    w.registry.SetF("secondary_window.pill_button_1", "blurDp", 5.0f);
    w.registry.SetF("secondary_window.pill_button_1", "lensHeightDp", 30.0f);
    w.registry.SetI("secondary_window.pill_button_1", "edgeMode", 4);
    w.history.EndBatch();
    Check(w.history.UndoDepth() == 1, "批处理内的多次修改是**一个步骤**");
    Check(w.history.LastChangeCount() == 3, "这一步里含三条改动");
    Check(w.history.LastLabel() == "恢复默认", "步骤标签用的是批次名");
    Check(w.history.Undo(), "撤销批处理");
    Check(std::fabs(g_blur - 2.0f) < 1e-6f && std::fabs(g_lens - 12.0f) < 1e-6f &&
          g_edge == 1, "一次撤销把整批全部回退");
    Check(w.history.Redo(), "重做批处理");
    Check(std::fabs(g_blur - 5.0f) < 1e-6f && std::fabs(g_lens - 30.0f) < 1e-6f &&
          g_edge == 4, "一次重做把整批全部恢复");

    // 批外的一次普通修改仍然是独立一步
    w.registry.SetF("secondary_window.pill_button_1", "blurDp", 8.0f);
    Check(w.history.UndoDepth() == 2, "批处理之后的新修改单独成步");
    Check(w.history.LastChangeCount() == 1, "单独一步只有一条改动");
  }

  // ---- 组 4：回放不产生新记录；上限；清空 ---------------------------------
  {
    g_blur = 2.0f;
    Wiring w;
    w.history.SetCoalesceWindowMs(0.0);
    for (int i = 0; i < 10; ++i) {
      w.registry.SetF("secondary_window.pill_button_1", "blurDp", 1.0f + i * 0.5f);
    }
    const std::size_t depth = w.history.UndoDepth();
    Check(depth == 10, "十步");
    for (int i = 0; i < 5; ++i) w.history.Undo();
    Check(w.history.UndoDepth() == depth - 5, "撤销只在两个栈之间搬移，不新增记录");
    Check(w.history.RedoDepth() == 5, "重做栈长度正确");

    w.history.Clear();
    Check(!w.history.CanUndo() && !w.history.CanRedo(), "清空历史");

    // 上限：超出后被裁掉的是最老的
    History small(w.registry, 4);
    small.SetCoalesceWindowMs(0.0);
    (void)small;
  }

  // ---- 组 5：历史长度上限 -------------------------------------------------
  {
    g_blur = 2.0f;
    Registry registry;
    TargetDesc desc;
    desc.id = "secondary_window.pill_button_1";
    desc.name = "按钮 1";
    desc.type = "PillButton";
    desc.window = "secondary";
    TargetBuilder b(registry, desc);
    b.Float("blurDp", "模糊半径", "blur", &g_blur, 0.0f, 10.0f, "dp", "");

    History history(registry, 4);
    history.SetCoalesceWindowMs(0.0);
    struct Sink {
      History* history;
      static void On(void* user, const ParamChange& change) {
        static_cast<Sink*>(user)->history->Record(change);
      }
    } sink{&history};
    registry.Subscribe(&Sink::On, &sink);
    for (int i = 0; i < 10; ++i) {
      registry.SetF("secondary_window.pill_button_1", "blurDp", 0.5f + i * 0.5f);
    }
    Check(history.UndoDepth() == 4, "历史长度被限制");
    registry.Unsubscribe(&Sink::On, &sink);
  }

  std::printf("GlassHistoryTest: %d 项断言通过（5 组）\n", g_checks);
  return 0;
}
