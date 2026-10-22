// ime_probe —— 纯 native ELF 拉起 Android 系统输入法的分阶段真机探针。
//
// 它做的事和在正式程序里完全一样：用 aurora::InputBridge（纯 ELF，无 Java 依赖）
// 释放内嵌辅助 APK、app_process 引导 Java 桥接、通过回环 TCP 与输入法宿主 Service
// 通信。区别只是它把每个阶段都打印出来，并且可以停在某一阶段等人工检查。
//
// 用法：
//   ime_probe [--display N] [--temp-root DIR]
//             [--hold-window MS]      SYNC 之后停这么久（先核对窗口落在哪个显示）
//             [--show]                真的调 showSoftInput
//             [--yes-physical]        允许在物理屏（--display 0）上做 --show 实验
//             [--show-ms MS]          键盘保持多久
//             [--text UTF8]           同步进输入框的初始文本
//             [--dump]                每个阶段顺带跑一次 dumpsys 诊断
//
// 安全闸门：
//   --display 0 且 --show 时，必须显式再加 --yes-physical，否则直接拒绝运行。
//   理由见 research/ime_result.md：系统输入法只肯绑定 top-focused 显示上的窗口，
//   而 测试虚拟显示（带 FLAG_STEAL_TOP_FOCUS_DISABLED）永远不是 top-focused，
//   所以「键盘真的弹出来」只能发生在物理屏上 —— 那是用户的日常手机，
//   不能靠手滑触发。
//
// 退出时关掉子进程 stdin ⇒ Java 侧 pm uninstall 辅助包 ⇒ 删除临时目录。

#include "InputBridge.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <unistd.h>

namespace {

long long nowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

const long long kStart = nowMs();

void say(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    // 只写 stdout（调用方用 `> log 2>&1` 收集，Java 侧的 stderr 也会落在同一份里）
    std::printf("[probe +%5lldms] %s\n", nowMs() - kStart, buf);
    std::fflush(stdout);
}

/** 跑一条 shell 命令并把输出逐行打出来（用于阶段间诊断）。 */
void shell(const char* what, const char* cmd) {
    say("---- %s ----", what);
    FILE* p = ::popen(cmd, "r");
    if (!p) {
        say("  | (popen 失败)");
        return;
    }
    char line[1024];
    int n = 0;
    while (std::fgets(line, sizeof line, p) && n < 30) {
        size_t len = std::strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (len > 400) line[400] = 0;      // dumpsys 的单行可能极长
        say("  | %s", line);
        ++n;
    }
    ::pclose(p);
    if (n == 0) say("  | (无输出)");
    say("---- %s 结束 ----", what);
}

/** 辅助包的 uid（dumpsys 里按 mUid 过滤用）。 */
int auxUid() {
    FILE* p = ::popen("stat -c %u /data/data/com.aurora.imebridge 2>/dev/null", "r");
    if (!p) return -1;
    char buf[64] = {0};
    if (!std::fgets(buf, sizeof buf, p)) {
        ::pclose(p);
        return -1;
    }
    ::pclose(p);
    return std::atoi(buf);
}

/** IMMS 里与本辅助包相关的客户端状态。 */
void dumpImeClient(int uid) {
    char cmd[512];
    if (uid > 0) {
        std::snprintf(cmd, sizeof cmd,
                      "dumpsys input_method 2>/dev/null | grep -A3 -E 'mUid=%d ' | head -30", uid);
        shell("IMMS 中的本包客户端", cmd);
    }
    shell("IMMS 全局状态",
          "dumpsys input_method 2>/dev/null | grep -E "
          "'mCurMethodId|mCurId=|mCurToken|mShowRequested|mInputShown|mImeWindowVis|"
          "mHaveConnection|mBoundToMethod|mCurClient|mImeWindowVis' | head -20");
    shell("IME 窗口落在哪个显示",
          "dumpsys window windows 2>/dev/null | grep -B6 -A2 'InputMethod}' | "
          "grep -E 'Window\\{|mDisplayId=|mOwnerUid=|isVisible=' | head -20");
}

class ProbeCallbacks final : public aurora::ImeCallbacks {
public:
    void onEdit(int session, const std::string& text, int start, int end) override {
        say("事件 EDIT  session=%d start=%d end=%d text=%zu 字节 [%s]", session, start, end,
            text.size(), text.c_str());
    }
    void onKeyboard(int session, bool visible) override {
        say("事件 IME   session=%d visible=%s", session, visible ? "1（键盘可见）" : "0");
    }
    void onHidden(int session) override {
        say("事件 HIDDEN session=%d", session);
    }
    void onClipboard(const std::string& text) override {
        say("事件 CLIP  %zu 字节", text.size());
    }
};

int parseInt(const char* s, int fallback) {
    if (!s) return fallback;
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s) return fallback;
    return static_cast<int>(v);
}

/** 泵 IO 若干毫秒，顺带把回调事件打出来（回调自己会 say）。 */
void pumpFor(aurora::InputBridge& bridge, int ms) {
    const long long until = nowMs() + ms;
    while (nowMs() < until) {
        if (bridge.pump(200) < 0) {
            say("(连接已断开)");
            return;
        }
        ::usleep(100000);
    }
}

}  // namespace

int main(int argc, char** argv) {
    int display = 0;
    int holdWindowMs = 0;
    int showMs = 3000;
    bool doShow = false;
    bool doDump = false;
    bool yesPhysical = false;
    std::string tempRoot = "/data/adb";
    std::string text = "";

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!std::strcmp(a, "--display") && v) { display = parseInt(v, 0); ++i; }
        else if (!std::strcmp(a, "--temp-root") && v) { tempRoot = v; ++i; }
        else if (!std::strcmp(a, "--hold-window") && v) { holdWindowMs = parseInt(v, 0); ++i; }
        else if (!std::strcmp(a, "--show-ms") && v) { showMs = parseInt(v, 3000); ++i; }
        else if (!std::strcmp(a, "--text") && v) { text = v; ++i; }
        else if (!std::strcmp(a, "--show")) { doShow = true; }
        else if (!std::strcmp(a, "--dump")) { doDump = true; }
        else if (!std::strcmp(a, "--yes-physical")) { yesPhysical = true; }
    }

    // ---- 安全闸门：物理屏上弹键盘必须显式确认 ----
    if (doShow && display == 0 && !yesPhysical) {
        std::printf("\n");
        std::printf("========================================================\n");
        std::printf("  已拒绝运行：--show 且 --display 0 会在这台手机\n");
        std::printf("  【物理屏】上弹出系统输入法，并在数秒内接管输入法焦点。\n");
        std::printf("\n");
        std::printf("  这是用户的日常主力机。按项目纪律，物理屏实验必须先征得\n");
        std::printf("  同意，并且只做最少次数。\n");
        std::printf("\n");
        std::printf("  确认要做，请显式加 --yes-physical：\n");
        std::printf("    ime_probe --display 0 --show --show-ms 4000 --dump --yes-physical\n");
        std::printf("\n");
        std::printf("  只想安全核对（虚拟显示，不碰物理屏）：\n");
        std::printf("    ime_probe --display 20 --dump --hold-window 2000\n");
        std::printf("========================================================\n");
        return 3;
    }

    say("================ ime_probe 开始 ================");
    say("参数: display=%d tempRoot=%s holdWindow=%dms show=%d showMs=%d text=%zu字节",
        display, tempRoot.c_str(), holdWindowMs, doShow ? 1 : 0, showMs, text.size());

    ProbeCallbacks cb;
    aurora::InputBridge bridge;
    bridge.setCallbacks(&cb);
    bridge.setTempRoot(tempRoot.c_str());
    bridge.setDisplay(display);

    say("UID=%d GID=%d", static_cast<int>(::getuid()), static_cast<int>(::getgid()));
    if (doDump) {
        shell("SELinux 域", "id 2>&1; cat /proc/self/attr/current 2>/dev/null");
        shell("辅助包是否已安装", "pm path com.aurora.imebridge 2>&1");
    }

    // ---- 阶段 1：释放 APK + 引导 Java + 安装辅助包 ----
    say("[阶段1] bridge.open() —— 释放内嵌 APK、app_process 引导、安装辅助包、等待回环连接");
    const long long t1 = nowMs();
    if (!bridge.open()) {
        say("[阶段1] 失败: %s", bridge.lastError());
        say("临时目录: %s", bridge.tempDir().c_str());
        if (doDump) {
            shell("残留", "ls -l /data/adb/aurora-ime-* 2>&1; pm path com.aurora.imebridge 2>&1");
        }
        say("================ ime_probe 失败退出 ================");
        return 1;
    }
    say("[阶段1] 成功，用时 %lldms  临时目录=%s", nowMs() - t1, bridge.tempDir().c_str());
    const int uid = auxUid();
    say("[阶段1] 辅助包 uid=%d", uid);
    if (doDump) {
        shell("辅助包安装结果", "pm path com.aurora.imebridge 2>&1");
        shell("悬浮窗 AppOp", "appops get com.aurora.imebridge SYSTEM_ALERT_WINDOW 2>&1");
    }

    // ---- 阶段 2：只同步文本（创建 1×1 窗，但不拉键盘） ----
    aurora::ImeState st;
    st.session = 1;
    st.text = text;
    st.start = static_cast<int>(text.size());
    st.end = static_cast<int>(text.size());
    say("[阶段2] 发送 SYNC（文本 %zu 字节）—— Service 应创建 1×1 窗", text.size());
    if (!bridge.sync(st)) {
        say("[阶段2] 发送失败: %s", bridge.lastError());
        bridge.close();
        return 1;
    }
    pumpFor(bridge, 8000);
    say("[阶段2] 完成");

    if (doDump) {
        shell("1×1 窗落在哪个显示",
              "dumpsys window windows 2>/dev/null | grep -E "
              "'aurora-ime-bridge|mDisplayId=|fl=' | head -12");
        shell("各显示的焦点窗口",
              "dumpsys window displays 2>/dev/null | grep -E "
              "'Display: mDisplayId|mCurrentFocus' | head -10");
        dumpImeClient(uid);
    }

    if (holdWindowMs > 0) {
        say("[停顿] 保持 %dms，供外部核对窗口归属显示", holdWindowMs);
        pumpFor(bridge, holdWindowMs);
        say("[停顿] 结束");
    }

    // ---- 阶段 3：showSoftInput ----
    if (!doShow) {
        say("[阶段3] 未指定 --show，跳过 showSoftInput（窗口已建好，仅用于安全核对显示归属）");
    } else {
        say("[阶段3] 发 SHOW —— Service 会移除 FLAG_NOT_FOCUSABLE、requestFocus、循环 showSoftInput");
        bridge.showKeyboard(st.session);
        pumpFor(bridge, showMs);
        say("[阶段3] 观察窗口结束（%dms）", showMs);
        if (doDump) {
            dumpImeClient(uid);
            shell("IME 窗口是否真的在屏幕上",
                  "dumpsys window windows 2>/dev/null | grep -E "
                  "'Window\\{.*InputMethod|mDisplayId=|isOnScreen|isVisible=' | head -24");
        }
    }

    // ---- 收尾：HIDE + 关闭（会触发 pm uninstall） ----
    say("[收尾] HIDE + close()（关子进程 stdin ⇒ Java 侧卸载辅助包）");
    bridge.hide();
    pumpFor(bridge, 1000);
    const long long t9 = nowMs();
    bridge.close();
    say("[收尾] close() 返回，用时 %lldms", nowMs() - t9);

    if (doDump) {
        shell("清理结果", "pm path com.aurora.imebridge 2>&1; ls -ld /data/adb/aurora-ime-* 2>&1");
    }
    say("================ ime_probe 正常结束 ================");
    return 0;
}
