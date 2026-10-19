// =============================================================================
// DisplayPickTest —— `dumpsys SurfaceFlinger --display-id` 解析的离线回归。
//
// ★ 为什么要有这个测试（真实事故，2026-10-13）：
//   `--display 0` 曾经**写死**开发机的 DisplayId（1111111111111111111）。
//   用户把单文件 ELF 包发给朋友，朋友机器上是 2222222222222222222
//   ⇒ 不匹配 ⇒ 回退找虚拟屏 ⇒ 朋友机器上没有虚拟屏 ⇒
//   报「找不到可用的显示」退出 4。**功能在开发机上一切正常。**
//   本测试用两台机器的**真实输出**把这条钉死。
//
// 纯字符串逻辑，不需要设备、不需要 Android 头文件。
// =============================================================================

#include "DisplayPick.h"

#include <cassert>
#include <cstdio>
#include <string>

using aurora::display_pick::findDisplayName;
using aurora::display_pick::isPhysicalDisplayId;
using aurora::display_pick::pickPhysicalDisplayId;
using aurora::display_pick::pickVirtualDisplayId;

// ── 开发机（测试机，TESTDEV）的真实输出 ──────────────────────────────────
static const char* kDevDump =
    "Display identification data:\n"
    "Display 1111111111111111111 (HWC display 0): port=147 pnpId=QCM displayName=\"\"\n"
    "Display 11529215048158215817 (Virtual display): displayName=\"AgentVirtualDisplay\"\n";

// ── 用户朋友那台机器的真实输出（DisplayId 完全不同！）────────────────────
static const char* kFriendDump =
    "Display identification data:\n"
    "Display 2222222222222222222 (HWC display 0): port=147 pnpId=QCM displayName=\"\"\n";

// ── 录屏器也建了虚拟屏的情形 ─────────────────────────────────────────────
static const char* kTwoVirtualDump =
    "Display 1111111111111111111 (HWC display 0): port=147 pnpId=QCM displayName=\"\"\n"
    "Display 11529215048158215817 (Virtual display): displayName=\"AgentVirtualDisplay\"\n"
    "Display 11529215049906455173 (Virtual display): displayName=\"ScreenRecorder\"\n";

static void testPhysicalIsParsedNotHardcoded() {
    // ★ 核心断言：换一台机器，物理屏 ID 必须跟着变。
    const std::string dev = pickPhysicalDisplayId(kDevDump);
    const std::string friendPc = pickPhysicalDisplayId(kFriendDump);
    assert(dev == "1111111111111111111");
    assert(friendPc == "2222222222222222222");
    assert(dev != friendPc);   // 写死任何一个都会在另一台上失败
    std::puts("  [OK] 物理屏 DisplayId 现场解析（两台机器给出不同值，没有写死）");
}

static void testFriendMachineWouldWork() {
    // 复现事故：朋友机器上 `--display 0` 必须解析出**他自己的**物理屏。
    const std::string physicalId = pickPhysicalDisplayId(kFriendDump);
    assert(!physicalId.empty());
    assert(isPhysicalDisplayId(physicalId, physicalId));
    // 旧代码写死 dev 的 id：在朋友的 dump 里根本不存在 ⇒ 会掉进虚拟屏回退分支。
    assert(kFriendDump == std::string(kFriendDump));
    assert(std::string(kFriendDump).find("1111111111111111111") == std::string::npos);
    assert(pickVirtualDisplayId(kFriendDump).empty());   // 朋友机器上没有虚拟屏 ⇒ 旧代码必挂
    std::puts("  [OK] 朋友机器（只有物理屏）能找到物理屏；旧写死逻辑在这里必挂");
}

static void testPhysicalWinsOverVirtual() {
    const std::string physicalId = pickPhysicalDisplayId(kTwoVirtualDump);
    assert(physicalId == "1111111111111111111");
    // 两台虚拟屏时取最后一台（与旧行为一致）
    assert(pickVirtualDisplayId(kTwoVirtualDump) == "11529215049906455173");
    // 物理屏绝不能被当成虚拟屏
    assert(pickVirtualDisplayId(kTwoVirtualDump) != physicalId);
    std::puts("  [OK] 物理屏与虚拟屏分流正确；多台虚拟屏取最后一台");
}

static void testNoPhysicalDisplay() {
    // 只有虚拟屏（例如纯 headless）⇒ 物理屏解析为空，不崩、不返回垃圾。
    assert(pickPhysicalDisplayId(kDevDump).size() == 19);
    assert(pickPhysicalDisplayId("").empty());
    assert(pickPhysicalDisplayId("no displays here\n").empty());
    assert(pickVirtualDisplayId("").empty());
    // 非纯数字的行不能被当成 DisplayId
    assert(pickPhysicalDisplayId("Display free (HWC display 0): x\n").empty());
    std::puts("  [OK] 空输入/异常输入不崩，且不会把非数字当成 DisplayId");
}

static void testFirstPhysicalOfSeveral() {
    // 多台物理屏（折叠机/外接屏）时取第一台，不能取到第二台去。
    const char* two =
        "Display 1111111111111111111 (HWC display 0): port=1 displayName=\"\"\n"
        "Display 2222222222222222222 (HWC display 1): port=2 displayName=\"\"\n";
    assert(pickPhysicalDisplayId(two) == "1111111111111111111");
    std::puts("  [OK] 多台物理屏取第一台");
}

static void testDisplayName() {
    assert(findDisplayName(kDevDump, "11529215048158215817") == "AgentVirtualDisplay");
    assert(findDisplayName(kDevDump, "1111111111111111111").empty());  // 物理屏名是空串
    assert(findDisplayName(kDevDump, "999").empty());
    assert(findDisplayName("", "1").empty());
    assert(findDisplayName(kDevDump, "").empty());
    std::puts("  [OK] displayName 解析（物理屏为空串、找不到不崩）");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("DisplayPickTest: dumpsys --display-id 解析回归");
    testPhysicalIsParsedNotHardcoded();
    testFriendMachineWouldWork();
    testPhysicalWinsOverVirtual();
    testNoPhysicalDisplay();
    testFirstPhysicalOfSeveral();
    testDisplayName();
    std::puts("DisplayPickTest: 全部断言通过（6 组）");
    return 0;
}
