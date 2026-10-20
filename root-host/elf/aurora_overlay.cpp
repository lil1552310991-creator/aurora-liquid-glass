// aurora_overlay —— 纯 native ELF 液态玻璃覆盖层
//
// 交付形态：单个 aarch64 ELF；采集 Dex/JNI 内嵌，运行时临时释放并退出清理。
//
// 渲染/输入由 ELF 处理；内嵌采集辅助进程负责系统镜像和帧交接：
//   ① 多点触控   —— InputChannel 的多指 MOTION 逐触点喂给 Renderer::touch()
//   ② 全局实时背景 —— WMS 连续镜像 → AHardwareBuffer/UDS → EGLImage → ytbl 玻璃采样
//   ③ 可见形状内拦截触摸 —— SF 输入窗口，圆角/裁剪/多窗口并集，空白处穿透
//   ④ 窗口移动时拦截跟随 —— region 随窗口矩形变化重设（与几何同一笔事务）
//   ⑤ 拉起系统输入法 —— InputBridge（内嵌 APK + app_process + 1×1 宿主窗）
//
// 界面**照搬**用户开源项目 `c++液态玻璃展示源码`：
//   ytbl 组件库（保留原始玻璃配方与 shader）+ vendored secondary_imgui
//   渲染栈走 Renderer → GlesBackend（EGL/GLES3 + ImGui + 内嵌 CJK 字体 + 模糊 + 投影纹理）
//
// ★ 安全设计（针对本项目真机上真实发生过的一次「整屏滑动不了」）：
//   - 输入窗口的 touchOcclusionMode 必须是 ALLOW(2)。默认的 BLOCK_UNTRUSTED 会让
//     全屏图层的 frame 把后面 App 的触摸判成「不可信遮挡」后整条丢弃 —— 与 region 无关。
//   - 采集有内存护栏：MemAvailable 低于阈值就停采集，退回中性背景。
//   - GPU 降采样、有限帧率、单帧请求背压与有限 AHB 引用，正常路径不读回像素。
//   - 看门狗：超过 --max-seconds 无条件退出并清理。
//   - 物理屏需要显式 --yes-physical；默认跑在指定显示（默认取 测试虚拟屏）上。
//
// 用法：
//   aurora_overlay [--display N] [--layer-stack N] [--yes-physical]
//                  [--no-capture] [--capture-fps N] [--downscale N]
//                  [--capture-backend mirror|gpu|cpu|screencap] [--max-seconds N] [--no-ime] [--dump]
//                  [--record [路径]] [--record-seconds N] [--verbose]
//                  [--dump-frame /data/local/tmp/frame.ppm]
//   不带 --display 时自动从 `dumpsys SurfaceFlinger --display-id` 里挑一台非物理屏。

#include "CapturePipeline.h"
#include "SelfFeedbackErase.h"
#include "DisplayPick.h"   // 解析 dumpsys 的显示列表（纯字符串逻辑，可离线单测）
#include "DisplayGeometry.h"
#include "CrashReport.h"   // 崩溃取证的纯逻辑（maps 解析 / 手写格式化，可离线单测）
#include "aosp_glue.h"
#include "HapticBinder.h"   // 震动：Binder 直连 vibrator_manager（复用上面的 binder 符号）

#include "FrameStats.h"
#include "GestureRouter.h"
#include "HostProtocol.h"
#include "InputBridge.h"
#include "ImeTextDiff.h"     // 回填文本 ↔ 输入框 的换算（纯逻辑，有离线回归）
#include "InputRouter.h"
#include "LiquidGlassRuntime.h"   // LiquidGlassRegion(): diagnostics / legacy CPU erase bounds
#include "Renderer.h"
#include "OverlayTouchRegion.h"
#include "RealtimeCapture.h"
#include "PanelEfficiencyStats.h"
#include "GlesSceneCompositor.h"   // 全屏接管是否生效（takeover=on/off 判据）
#include "ScreenRecorder.h"   // 录屏开关（--record / 主题窗口按钮，内置 screenrecord）
#include "third_party/liquidglass/secondary_imgui.h"
#include <android/hardware_buffer.h>

#include <android/log.h>
#include <android/native_window.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace {

constexpr char kTag[] = "aurora_overlay";

// 采集的整屏像素尺寸上界（实际宽高从 screencap 的 16 字节头里读，不硬编码）
constexpr int kMaxCaptureW = 4096;
constexpr int kMaxCaptureH = 4096;

// `screencap` 不带 -p 时，raw 输出 = 16 字节头（4 个小端 uint32：w, h, format, x）+ RGBA8888 像素。
// 设备实测：1080x2376 时总长 10264336 = 16 + 1080*2376*4。
constexpr size_t kScreencapHeaderBytes = 16;

// 内存护栏：低于这个可用内存就暂停采集（KB）。本项目真机事故的根因就是内存水位，
// 所以这里必须有一道硬闸门，而不是"应该没事"。
constexpr long long kMinAvailableKbToCapture = 1400LL * 1024LL;

std::atomic<bool> g_running{true};
std::atomic<bool> g_imeUp{false};
std::atomic<int> g_exitStatus{0};

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

/** 记住最后一条日志（崩溃取证的现场信息，定义见下面的崩溃取证段）。 */
void recordLastLog(const char* s);

// ★★ 默认静默：正常运行时**不往屏幕打任何东西**（用户要求：点了就跑，不要日志和参数）。
//    - logcat **照写**：界面上看不见，`adb logcat -s aurora_overlay` 随时可查；
//    - 崩溃取证照旧（它落文件 + 只在崩了之后由 SH 打印）；
//    - 要看详细输出：`--verbose` / `-v` / `AURORA_VERBOSE=1`，或直接用 `--dump`（本身即诊断）。
bool g_quiet = true;

void logf(int prio, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    __android_log_write(prio, kTag, buf);
    if (!g_quiet) {
        std::fprintf(prio >= ANDROID_LOG_WARN ? stderr : stdout, "[overlay] %s\n", buf);
        std::fflush(stdout);
    }
    recordLastLog(buf);
}

/** 只在**失败**时说话：一行给出原因，细节落文件（成功时一个字都不打）。 */
void reportFailure(const char* what, const char* detail) {
    const char* path = std::getenv("AURORA_ERROR_LOG");
    if (path == nullptr || path[0] == '\0') path = "/data/local/tmp/aurora_overlay.error.log";
    if (FILE* f = std::fopen(path, "w")) {
        std::fprintf(f, "aurora_overlay 启动失败\n时间  = %lld\n原因  = %s\n",
                     (long long)nowMs(), what ? what : "(未说明)");
        if (detail != nullptr) std::fprintf(f, "详情  = %s\n", detail);
        std::fclose(f);
    }
    __android_log_write(ANDROID_LOG_ERROR, kTag, what ? what : "启动失败");
    if (!g_quiet) {
        std::fprintf(stderr, "[overlay] %s\n", what ? what : "启动失败");
        std::fflush(stderr);
    }
}

// ===========================================================================
// 崩溃取证
// ===========================================================================
// ★★ 为什么必须有（真实事故，2026-10-13）：
//   朋友的机器（另一台设备 / 厂商 ROM 16）上，日志停在
//       [overlay] 图层参数: 1272x2800 layerStack=0 ...
//   之后直接 `Segmentation fault`，**下一行都打不出来**。设备不在手边、
//   没法 gdb、没法复现 —— 每轮"改一版再让他试"都要靠用户转发，代价极高。
//   所以进程在收到致命信号时，自己把「最后一条日志 + 出错地址 + 命中模块」
//   落到文件与 stderr，再按**默认动作**重新抛出（退出码仍是 139，行为不变）。
//
//   信号处理器里只允许异步信号安全的调用（open/read/write/…）：
//   这里的格式化全部走 CrashReport.h 的手写固定缓冲版本，没有 printf/malloc。
//   纯解析部分（/proc/self/maps）在 tests/CrashReportTest.cpp 里有离线回归。
// （★ 本段与 logf 同处一个匿名命名空间，不要再套一层 namespace {}，
//   否则 recordLastLog 的声明与定义会变成两个作用域 ⇒ 重载歧义，编译不过。）

constexpr char kDefaultCrashLog[] = "/data/local/tmp/aurora_overlay.crash.log";

char g_lastLog[192] = "(还没有输出任何日志)";
char g_crashLogPath[192] = "/data/local/tmp/aurora_overlay.crash.log";
volatile sig_atomic_t g_faulting = 0;
char g_altStack[64 * 1024];   // 栈溢出时处理器自己也需要栈

/** 记住最后一条日志：崩溃现场唯一能回答"走到哪一步了"的信息。 */
void recordLastLog(const char* s) {
    if (s == nullptr) return;
    std::size_t n = 0;
    while (s[n] != '\0' && n + 1 < sizeof g_lastLog) {
        g_lastLog[n] = s[n];
        ++n;
    }
    g_lastLog[n] = '\0';
}

void writeAll(int fd, const char* s, std::size_t n) {
    while (n > 0) {
        const ssize_t k = ::write(fd, s, n);
        if (k <= 0) return;   // EINTR/EAGAIN 也不重试：崩溃现场不冒险
        s += k;
        n -= static_cast<std::size_t>(k);
    }
}

/** 同时写 stderr 与崩溃日志文件（文件打不开就只写 stderr）。 */
void emitBoth(int fileFd, const char* s) {
    writeAll(STDERR_FILENO, s, std::strlen(s));
    if (fileFd >= 0) writeAll(fileFd, s, std::strlen(s));
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGBUS: return "SIGBUS";
        case SIGABRT: return "SIGABRT";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGTRAP: return "SIGTRAP";
        default: return "SIG?";
    }
}

/** si_code 的常见取值（SEGV/BUS 各有自己的一套）。 */
const char* faultKind(int sig, int code) {
    if (sig == SIGSEGV) {
        if (code == SEGV_MAPERR) return "地址未映射";
        if (code == SEGV_ACCERR) return "权限不允许";
    }
    if (sig == SIGBUS) {
        if (code == BUS_ADRALN) return "地址未对齐";
        if (code == BUS_ADRERR) return "物理地址不存在";
    }
    return "";
}

/** 把一行「标签 = 0x… / 模块名」写进固定缓冲。 */
void emitAddressLine(int fileFd, const char* label, unsigned long long addr,
                     const aurora::crash::MapHit* hit) {
    char line[320];
    std::size_t pos = 0;
    line[0] = '\0';
    aurora::crash::AppendStr(line, sizeof line, &pos, label);
    aurora::crash::AppendHex(line, sizeof line, &pos, addr);
    if (hit != nullptr) {
        char base[128];
        aurora::crash::BaseName(hit->path, base, sizeof base);
        aurora::crash::AppendStr(line, sizeof line, &pos, "  在 ");
        aurora::crash::AppendStr(line, sizeof line, &pos, base[0] ? base : "(匿名映射)");
        aurora::crash::AppendStr(line, sizeof line, &pos, " 内 +");
        aurora::crash::AppendHex(line, sizeof line, &pos, addr - hit->start);
        if (hit->executable) aurora::crash::AppendStr(line, sizeof line, &pos, " (可执行)");
    } else {
        aurora::crash::AppendStr(line, sizeof line, &pos, "  不在任何映射里（野指针/栈溢出常见）");
    }
    aurora::crash::AppendStr(line, sizeof line, &pos, "\n");
    emitBoth(fileFd, line);
}

// ---- 崩溃现场的一次性 maps 扫描（流式，不整文件进缓冲）----
//
// ★★ 为什么是流式：真机上 /proc/<pid>/maps 是 **560~665 KB**
//   （system_server 实测 665564 字节）。第一版把它读进 64KB 固定缓冲 ⇒ 被截断
//   ⇒ 找不到栈区间 ⇒ 栈扫描按"sp+32KB"这个常数往上读、越过栈顶未映射页
//   ⇒ 处理器自己再崩一次 ⇒ g_faulting 分支立刻重抛默认动作
//   ⇒ **报告后半段全丢**（真机自测就是这么暴露的：退出码照样 139，很难发现少了东西）。
//
//   所以这里**只读一遍**、逐行处理，并只保留现场真正需要的三样东西：
//     ① 出错地址 / PC / LR / SP 各自落在哪个映射（含模块名）；
//     ② SP 所在映射的 [start,end) —— 栈扫描的**唯一**合法边界；
//     ③ 可执行映射表（栈上"像返回地址的字"要在表里查，不能每个字都重读一遍 maps）。
//   上限 512：真机自测时本进程的可执行映射**超过 256 个**（dlopen 了 libgui/
//   libbinder/EGL/厂商 GPU 驱动等），256 会截断并在报告里打"可能有漏判"。
//   512 × (8+8+64) ≈ 41 KB 静态内存，代价可以忽略。
constexpr int kMaxExecRanges = 512;

struct ExecRange {
    unsigned long long start = 0;
    unsigned long long end = 0;
    char name[64] = {0};
};

struct FaultScan {
    bool stackKnown = false;
    unsigned long long stackStart = 0;
    unsigned long long stackEnd = 0;
    ExecRange exec[kMaxExecRanges];
    int execCount = 0;
    bool execTruncated = false;
    bool mapsRead = false;
};

/** 单行处理：与离线测试共用同一个 ParseMapLine（出厂代码 = 被测代码）。 */
void consumeMapsLine(const char* line, std::size_t len, const unsigned long long* addrs,
                     int addrCount, aurora::crash::MapHit* hits, bool* hitFound, FaultScan* scan) {
    aurora::crash::MapHit h;
    if (!aurora::crash::ParseMapLine(line, len, &h)) return;
    for (int i = 0; i < addrCount; ++i) {
        if (!hitFound[i] && h.start <= addrs[i] && addrs[i] < h.end) {
            hits[i] = h;
            hitFound[i] = true;
        }
    }
    if (h.executable) {
        if (scan->execCount < kMaxExecRanges) {
            ExecRange& r = scan->exec[scan->execCount++];
            r.start = h.start;
            r.end = h.end;
            aurora::crash::BaseName(h.path, r.name, sizeof r.name);
        } else {
            scan->execTruncated = true;
        }
    }
}

void scanMapsForFault(const unsigned long long* addrs, int addrCount,
                      aurora::crash::MapHit* hits, bool* hitFound, FaultScan* scan) {
    const int fd = ::open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    scan->mapsRead = true;

    char buf[4096];
    char line[512];
    std::size_t lineLen = 0;
    bool skipLine = false;
    ssize_t n;
    while ((n = ::read(fd, buf, sizeof buf)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c != '\n') {
                if (skipLine) continue;
                if (lineLen + 1 < sizeof line) {
                    line[lineLen++] = c;
                } else {
                    skipLine = true;   // 超长行（maps 里不该有）：整行丢弃，别拿半行去解析
                    lineLen = 0;
                }
                continue;
            }
            if (!skipLine && lineLen > 0) {
                consumeMapsLine(line, lineLen, addrs, addrCount, hits, hitFound, scan);
            }
            lineLen = 0;
            skipLine = false;
        }
    }
    if (!skipLine && lineLen > 0) {
        consumeMapsLine(line, lineLen, addrs, addrCount, hits, hitFound, scan);
    }
    ::close(fd);
}

void faultHandler(int sig, siginfo_t* info, void* uctx) {
    if (g_faulting) {   // 处理器自己又崩了：交回默认动作，别递归
        ::signal(sig, SIG_DFL);
        ::raise(sig);
        return;
    }
    g_faulting = 1;

    unsigned long long pc = 0, lr = 0, sp = 0;
#if defined(__aarch64__)
    if (uctx != nullptr) {
        // bionic 的 mcontext_t 就是内核 struct sigcontext（见 sys/ucontext.h）
        const ucontext_t* uc = static_cast<const ucontext_t*>(uctx);
        pc = uc->uc_mcontext.pc;
        lr = uc->uc_mcontext.regs[30];
        sp = uc->uc_mcontext.sp;
    }
#else
    (void)uctx;
#endif
    const unsigned long long faultAddr =
        (info != nullptr) ? reinterpret_cast<unsigned long long>(info->si_addr) : 0;

    // 一次扫描同时回答：出错地址/PC/LR/SP 落在哪、栈区间在哪、哪些映射可执行
    const unsigned long long addrs[4] = {faultAddr, pc, lr, sp};
    aurora::crash::MapHit hits[4];
    bool hitFound[4] = {false, false, false, false};
    FaultScan scan;
    scanMapsForFault(addrs, 4, hits, hitFound, &scan);
    if (hitFound[3]) {
        scan.stackKnown = true;
        scan.stackStart = hits[3].start;
        scan.stackEnd = hits[3].end;
    }

    const int logFd = ::open(g_crashLogPath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);

    char line[320];
    std::size_t pos;

    emitBoth(logFd, "\n==== aurora_overlay 崩溃取证 ====\n");
    pos = 0; line[0] = '\0';
    aurora::crash::AppendStr(line, sizeof line, &pos, "信号      = ");
    aurora::crash::AppendStr(line, sizeof line, &pos, signalName(sig));
    aurora::crash::AppendStr(line, sizeof line, &pos, "(");
    aurora::crash::AppendDec(line, sizeof line, &pos, sig);
    aurora::crash::AppendStr(line, sizeof line, &pos, ") si_code=");
    aurora::crash::AppendDec(line, sizeof line, &pos, info ? info->si_code : -1);
    aurora::crash::AppendStr(line, sizeof line, &pos, " ");
    aurora::crash::AppendStr(line, sizeof line, &pos, faultKind(sig, info ? info->si_code : 0));
    aurora::crash::AppendStr(line, sizeof line, &pos, "\n");
    emitBoth(logFd, line);

    if (info != nullptr) {
        emitAddressLine(logFd, "出错地址  = ", faultAddr, hitFound[0] ? &hits[0] : nullptr);
    }
    pos = 0; line[0] = '\0';
    aurora::crash::AppendStr(line, sizeof line, &pos, "最后日志  = ");
    aurora::crash::AppendStr(line, sizeof line, &pos, g_lastLog);
    aurora::crash::AppendStr(line, sizeof line, &pos, "\n");
    emitBoth(logFd, line);

    if (pc != 0) {
        emitAddressLine(logFd, "出错 PC   = ", pc, hitFound[1] ? &hits[1] : nullptr);
        if (lr != 0) emitAddressLine(logFd, "调用 LR   = ", lr, hitFound[2] ? &hits[2] : nullptr);
        pos = 0; line[0] = '\0';
        aurora::crash::AppendStr(line, sizeof line, &pos, "栈指针 SP = ");
        aurora::crash::AppendHex(line, sizeof line, &pos, sp);
        aurora::crash::AppendStr(line, sizeof line, &pos, scan.stackKnown ? "（栈区间已知）" : "（栈区间未知）");
        aurora::crash::AppendStr(line, sizeof line, &pos, "\n");
        emitBoth(logFd, line);

        // 穷人版回溯：栈上"落在可执行映射里"的字就是返回地址。
        // 只为回答「这条路是谁调进来的」，所以限量 8 条。
        //
        // ★★ 扫描上界**必须**由 maps 给出的栈区间决定，绝不能拍一个常数：
        //    第一版写 sp+32KB，而 SP 在浅调用栈里离栈顶很近 ⇒ 越过栈顶未映射页
        //    ⇒ 处理器自己再崩一次 ⇒ 报告后半段全丢。区间未知就**不扫**。
        if (!scan.stackKnown) {
            emitBoth(logFd, "（拿不到 SP 所在映射，跳过栈扫描：绝不用常数猜边界）\n");
        } else {
            unsigned long long scanEnd = sp + 32 * 1024;
            if (scan.stackEnd < scanEnd) scanEnd = scan.stackEnd;
            emitBoth(logFd, "栈上像返回地址的字（最多 8 条）:\n");
            int shown = 0;
            for (unsigned long long a = sp; a + 8 <= scanEnd && shown < 8; a += 8) {
                unsigned long long word = 0;
                const unsigned char* src = reinterpret_cast<const unsigned char*>(a);
                for (int b = 0; b < 8; ++b) {
                    word |= (static_cast<unsigned long long>(src[b]) << (8 * b));
                }
                const ExecRange* owner = nullptr;
                for (int i = 0; i < scan.execCount; ++i) {
                    if (scan.exec[i].start <= word && word < scan.exec[i].end) {
                        owner = &scan.exec[i];
                        break;
                    }
                }
                if (owner == nullptr) continue;
                pos = 0; line[0] = '\0';
                aurora::crash::AppendStr(line, sizeof line, &pos, "  +");
                aurora::crash::AppendHex(line, sizeof line, &pos, a - sp);
                aurora::crash::AppendStr(line, sizeof line, &pos, " -> ");
                aurora::crash::AppendHex(line, sizeof line, &pos, word);
                aurora::crash::AppendStr(line, sizeof line, &pos, "  在 ");
                aurora::crash::AppendStr(line, sizeof line, &pos, owner->name[0] ? owner->name : "(匿名映射)");
                aurora::crash::AppendStr(line, sizeof line, &pos, " +");
                aurora::crash::AppendHex(line, sizeof line, &pos, word - owner->start);
                aurora::crash::AppendStr(line, sizeof line, &pos, "\n");
                emitBoth(logFd, line);
                ++shown;
            }
            if (shown == 0) emitBoth(logFd, "  （没有找到像返回地址的字）\n");
            if (scan.execTruncated) {
                pos = 0; line[0] = '\0';
                aurora::crash::AppendStr(line, sizeof line, &pos, "  （可执行映射超过 ");
                aurora::crash::AppendDec(line, sizeof line, &pos, kMaxExecRanges);
                aurora::crash::AppendStr(line, sizeof line, &pos, " 个，已截断：可能有漏判）\n");
                emitBoth(logFd, line);
            }
        }
    }

    pos = 0; line[0] = '\0';
    aurora::crash::AppendStr(line, sizeof line, &pos, "崩溃日志已写入 ");
    aurora::crash::AppendStr(line, sizeof line, &pos, g_crashLogPath);
    aurora::crash::AppendStr(line, sizeof line, &pos,
                             "\n请把上面这一段**原样**发回给开发机（PC）侧。\n");
    emitBoth(logFd, line);
    emitBoth(logFd, "================================\n");
    if (logFd >= 0) ::close(logFd);

    // 恢复默认动作再重抛：退出码、core dump 行为与不加处理器时一致。
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

/** 装崩溃取证。必须在任何可能崩的调用之前执行。 */
void installCrashForensics() {
    const char* env = std::getenv("AURORA_CRASH_LOG");
    if (env != nullptr && env[0] != '\0') {
        std::size_t n = 0;
        while (env[n] != '\0' && n + 1 < sizeof g_crashLogPath) {
            g_crashLogPath[n] = env[n];
            ++n;
        }
        g_crashLogPath[n] = '\0';
    } else {
        std::strncpy(g_crashLogPath, kDefaultCrashLog, sizeof g_crashLogPath - 1);
    }

    stack_t ss{};
    ss.ss_sp = g_altStack;
    ss.ss_size = sizeof g_altStack;
    ss.ss_flags = 0;
    ::sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = faultHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    ::sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP}) {
        ::sigaction(sig, &sa, nullptr);
    }
}

void onSignal(int) { g_running.store(false); }

/** /proc/meminfo 的 MemAvailable，单位 KB；读不到返回 -1。 */
long long memAvailableKb() {
    FILE* f = std::fopen("/proc/meminfo", "r");
    if (f == nullptr) return -1;
    char line[256];
    long long value = -1;
    while (std::fgets(line, sizeof line, f) != nullptr) {
        if (std::strncmp(line, "MemAvailable:", 13) == 0) {
            value = std::strtoll(line + 13, nullptr, 10);
            break;
        }
    }
    std::fclose(f);
    return value;
}

/** 跑一条命令并取回 stdout 前若干字节（诊断用）。 */
std::string runCapture(const char* cmd, size_t maxBytes = 4096) {
    std::string out;
    FILE* p = ::popen(cmd, "r");
    if (p == nullptr) return out;
    char buf[512];
    while (out.size() < maxBytes && std::fgets(buf, sizeof buf, p) != nullptr) out += buf;
    ::pclose(p);
    return out;
}

/** 从 `dumpsys SurfaceFlinger --display-id` 的输出里挑一台非物理屏的 DisplayId。 */
std::string pickVirtualDisplayId(std::string* allDisplays) {
    const std::string dump = runCapture("dumpsys SurfaceFlinger --display-id 2>/dev/null");
    if (allDisplays != nullptr) *allDisplays = dump;
    return aurora::display_pick::pickVirtualDisplayId(dump);
}

/**
 * 从 `dumpsys SurfaceFlinger --display-id` 的输出里取某台显示的 displayName。
 * 物理屏的 displayName 是空串（`displayName=""`），虚拟屏通常是 "AgentVirtualDisplay"。
 * 这个名字是 `DisplayId → layerStack` 退路查找的钥匙（见 findLayerStack）。
 */
std::string findDisplayName(const std::string& dump, const std::string& displayId) {
    return aurora::display_pick::findDisplayName(dump, displayId);
}

int parseInt(const char* s, int fallback) {
    if (s == nullptr) return fallback;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    return (end == s) ? fallback : static_cast<int>(v);
}

/**
 * 现查某台显示对应的 layerStack。
 *
 * ★ 必须现查、不能硬编码：测试工具 的虚拟显示每次重建 layerStack 都会变
 *   （本项目先后见过 6 / 19 / 20 / 22）。传错值图层就落不到目标显示上，
 *   表现为「程序在跑但屏幕上什么都没有」，很难归因。
 *
 * ★★ 这里有一个实测踩过的坑：**不能拿 SF 的 DisplayId 去 `dumpsys display` 里找**。
 *   `dumpsys display` 用的是**逻辑 displayId**（虚拟屏是 22）与 uniqueId
 *   （`virtual:android,0,AgentVirtualDisplay,19`），**都不含 SF 的 uint64 DisplayId**
 *   （11529215047573656498）。按 DisplayId 匹配永远找不到。
 *
 * 正确做法（两级，按可靠性排序）：
 *   1) **首选**：`dumpsys SurfaceFlinger` 里 `Display <uint64 DisplayId> (virtual, "...")`
 *      这个显示头之后紧跟 `layerFilter={layerStack=N ...}` —— 这是 DisplayId → layerStack
 *      的**直接映射**，不经过任何中间 id 换算。
 *   2) **退路**：按显示**名字**去 `dumpsys display` 的
 *      `mBaseDisplayInfo=DisplayInfo{"<name>" ... }` 块里取 `layerStack N`。
 *      （块的其余部分可能很长，所以按行取即可 —— 实测 layerStack 与其余字段同一行。）
 *
 * displayName 为空表示物理屏（其 displayName=""），此时直接给 0。
 */
int findLayerStack(const std::string& displayId, const std::string& displayName) {
    // ★ displayId 解析失败**不等于**没有可用显示：各厂商 --display-id 的格式不统一，
    //   原来直接 return -1 ⇒ 主流程判死退出；现在走后面的兜底链（物理屏 layerStack 恒 0）。

    // ---- 1) 首选：SurfaceFlinger 的 DisplayId → layerFilter={layerStack=N} ----
    {
        const std::string sf = runCapture("dumpsys SurfaceFlinger 2>/dev/null", 6u << 20);
        const std::string needle = "Display " + displayId + " (";
        size_t pos = 0;
        while ((pos = sf.find(needle, pos)) != std::string::npos) {
            pos += needle.size();
            // 在这个显示头之后的一小段里找 layerStack=（找太远会串到下一台显示）
            const size_t window = 600;
            const size_t end = (pos + window < sf.size()) ? pos + window : sf.size();
            const size_t ls = sf.find("layerStack=", pos);
            if (ls != std::string::npos && ls < end) {
                const int v = parseInt(sf.c_str() + ls + 11, -1);
                if (v >= 0) return v;
            }
        }
    }

    // ---- 2) 退路：按显示名字在 dumpsys display 里找 ----
    if (!displayName.empty()) {
        const std::string dump = runCapture("dumpsys display 2>/dev/null", 2u << 20);
        const std::string key = "DisplayInfo{\"" + displayName + "\"";
        const size_t pos = dump.find(key);
        if (pos != std::string::npos) {
            size_t end = dump.find('\n', pos);
            if (end == std::string::npos) end = dump.size();
            const std::string line = dump.substr(pos, end - pos);
            const size_t ls = line.find("layerStack ");
            if (ls != std::string::npos) {
                const int v = parseInt(line.c_str() + ls + 11, -1);
                if (v >= 0) return v;
            }
        }
    }

    // ---- 3) 物理屏没有 displayName，layerStack 恒为 0 ----
    // ★ 无条件兜底：本程序画的是兄弟图层，落在一台显示上的默认层栈就是 0。
    //   某些 ROM 既解析不出 layerStack= 也没有 displayName，放宽前会直接退出；
    //   放宽后在 0 上至少能跑（显示可能不对，但比完全起不来好），且日志会标注。
    {
        logf(ANDROID_LOG_WARN, "layerStack 未解析出来（DisplayId=%s displayName=%s），兜底用 0",
             displayId.empty() ? "空" : displayId.c_str(),
             displayName.empty() ? "空" : displayName.c_str());
        return 0;
    }
}

// WMS uses a logical display ID; SurfaceFlinger uses a different uint64 ID.
// Resolve it from the active display description rather than equating it with layerStack.
int findLogicalDisplayId(const std::string& displayName, bool physical) {
    if (physical) return 0;
    if (displayName.empty()) return -1;
    const std::string dump = runCapture("dumpsys display 2>/dev/null", 2u << 20);
    const std::string key = "mBaseDisplayInfo=DisplayInfo{\"" + displayName + "\"";
    const size_t start = dump.find(key);
    if (start == std::string::npos) return -1;
    const size_t end = dump.find('\n', start);
    const size_t id = dump.find("displayId ", start);
    if (id == std::string::npos || (end != std::string::npos && id > end)) return -1;
    return parseInt(dump.c_str() + id + 10, -1);
}

// ---------------------------------------------------------------------------
// Legacy screencap backend for explicit A/B diagnostics (--capture-backend screencap).
// Production uses RealtimeCapture and GPU buffers instead of per-frame spawning.
// ---------------------------------------------------------------------------
class ScreenCapture final {
public:
    ScreenCapture(std::string displayId, int downscale)
        : displayId_(std::move(displayId)), downscale_(downscale < 1 ? 1 : downscale) {}

    /** 抓一帧并降采样。成功返回 true，rgba 为缩小后的图像。 */
    bool grabOnce(std::vector<std::uint8_t>* rgba, int* outW, int* outH, std::string* why) {
        int pipeFd[2] = {-1, -1};
        if (::pipe(pipeFd) != 0) { if (why) *why = "pipe 失败"; return false; }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipeFd[0]);
        posix_spawn_file_actions_addclose(&actions, pipeFd[1]);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

        // 与 probe_m3_ytbl.cpp:143 同一写法：-d <DisplayId>，不加 -p（要 raw RGBA）
        const char* argv[] = {"/system/bin/screencap", "-d", displayId_.c_str(), nullptr};
        pid_t child = -1;
        const int rc = posix_spawn(&child, "/system/bin/screencap", &actions, nullptr,
                                   const_cast<char**>(argv), environ);
        posix_spawn_file_actions_destroy(&actions);
        ::close(pipeFd[1]);          // 父进程只留读端，否则 read 永不返回 EOF
        if (rc != 0) {
            ::close(pipeFd[0]);
            if (why) *why = "posix_spawn(screencap) 失败";
            return false;
        }

        std::vector<std::uint8_t> raw;
        raw.reserve(12u << 20);          // 1080x2376x4 ≈ 9.8MB + 头
        std::uint8_t buf[64 * 1024];
        for (;;) {
            const ssize_t n = ::read(pipeFd[0], buf, sizeof buf);
            if (n > 0) {
                raw.insert(raw.end(), buf, buf + n);
                if (raw.size() > kScreencapHeaderBytes + (size_t)kMaxCaptureW * kMaxCaptureH * 4u) {
                    break;               // 上界保护，防止对端异常时无限增长
                }
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        ::close(pipeFd[0]);
        int status = 0;
        ::waitpid(child, &status, 0);

        // ★ screencap 的 raw 输出 = 16 字节头 + RGBA8888 像素。
        //   头是 4 个小端 uint32：{width, height, format, x}。
        //   设备实测（`screencap -d <id> | od -An -tu4 | head`）：
        //     1080 2376 1 1  → 总长 10264336 = 16 + 1080*2376*4
        //
        //   教训：最早的写法按「字节数 == w*h*4」精确匹配分辨率，
        //   **永远匹配不上**（差 16 字节），采集会一直失败 —— 这类坑只有上机才暴露。
        //   头里既然有宽高，就不需要任何硬编码分辨率表了。
        if (raw.size() < kScreencapHeaderBytes + 4u) {
            if (why) *why = "screencap 输出过短（" + std::to_string(raw.size()) + " 字节）";
            return false;
        }
        std::uint32_t headerW = 0, headerH = 0, headerFmt = 0;
        std::memcpy(&headerW, raw.data() + 0, 4);
        std::memcpy(&headerH, raw.data() + 4, 4);
        std::memcpy(&headerFmt, raw.data() + 8, 4);
        const std::uint8_t* pixels = raw.data() + kScreencapHeaderBytes;
        const size_t pixelBytes = raw.size() - kScreencapHeaderBytes;
        const int w = static_cast<int>(headerW);
        const int h = static_cast<int>(headerH);
        if (w <= 0 || h <= 0 || w > kMaxCaptureW || h > kMaxCaptureH ||
            pixelBytes < static_cast<size_t>(w) * static_cast<size_t>(h) * 4u) {
            if (why) {
                *why = "screencap 头非法: " + std::to_string(w) + "x" + std::to_string(h) +
                       " fmt=" + std::to_string(headerFmt) +
                       " 像素字节=" + std::to_string(pixelBytes);
            }
            return false;
        }

        // =====================================================================
        // 后处理管线：降采样 → 抹除（防自反馈）→ 行翻转
        //
        // ★ 整段都搬到了 aurora::PostProcessCaptureFrame
        //   （app/src/main/cpp/CapturePipeline.h），**因为顺序错了会以非常隐蔽的
        //   方式表现出来，而我在这上面错过两次**：
        //     ① 把抹除插到行翻转之后 ⇒ 缓冲已自下而上，抹除却用自上而下的屏幕
        //        坐标 ⇒ 区域垂直镜像，屏幕上边缘残留自己的图层继续反馈
        //     ② 漏掉行翻转 ⇒ 玻璃里折射出上下镜像的桌面
        //   两条都只能靠"看着有点怪"发现。抽成纯函数后就能逐像素断言
        //   （tests/CapturePipelineTest.cpp，7 组）：
        //   **在最终缓冲里，被抹除的区域必须落在它该在的位置** ——
        //   这一条断言同时覆盖①②。
        // =====================================================================
        aurora::CapturePostParams post;
        post.downscale = downscale_;
        post.eraseX = eraseX;
        post.eraseY = eraseY;
        post.eraseW = eraseW;
        post.eraseH = eraseH;
        if (!aurora::PostProcessCaptureFrame(pixels, w, h, post, rgba, outW, outH)) {
            if (why) *why = "后处理失败";
            return false;
        }
        if (eraseW > 0 && eraseH > 0) ++erasedFrames;
        return true;
    }

    // 设置要抹除的界面区域（屏幕坐标，原始分辨率）。宽或高为 0 表示不抹。
    void setEraseRect(int x, int y, int w, int h) {
        eraseX = x;
        eraseY = y;
        eraseW = w;
        eraseH = h;
    }
    long long erasedFrameCount() const { return erasedFrames; }

private:
    std::string displayId_;
    int downscale_ = 2;
    int eraseX = 0;
    int eraseY = 0;
    int eraseW = 0;
    int eraseH = 0;
    long long erasedFrames = 0;
};

// ---------------------------------------------------------------------------
// 输入法桥接：只在 UI 真的要文字输入时才拉起；退出时收起并卸载辅助包。
// ---------------------------------------------------------------------------
class ImeHost final : public aurora::ImeCallbacks {
public:
    void attach(aurora::Renderer* renderer, int display) {
        renderer_ = renderer;
        bridge_.setCallbacks(this);
        bridge_.setDisplay(display);
        bridge_.setTempRoot("/data/adb");
        // 回填明细（退格几次、插入多少字节）默认不打：正常打字一秒能来好几条，
        // 会淹没真正要看的状态转换日志。AURORA_IME_TRACE=1 才打开。
        if (const char* t = std::getenv("AURORA_IME_TRACE"); t != nullptr && t[0] == '1') {
            imeTrace_ = true;
        }
    }

    bool ensureUp() {
        if (up_) return true;
        // ★★ 如果预热已经在路上（冷启动后半段正在被主循环逐帧推进），
        //    这里就把剩下的几步同步走完 —— 绝**不**从头再 open 一次：
        //    重复 extractApk/spawnBridge 会白等一遍，还会留下第二个桥接进程。
        if (warming_) {
            while (warming_) {
                warmUpPoll();
                if (!warming_) break;
                ::usleep(2000);
            }
            if (up_) return true;
            // 预热失败 ⇒ 落到下面的完整路径重试一次（失败原因已由 warmUpPoll 打过）
        }
        const long long t0 = nowMs();
        if (!bridge_.open()) {
            logf(ANDROID_LOG_ERROR, "输入法桥接 open 失败: %s", bridge_.lastError());
            bridge_.close();            // ★ 必须：open() 失败时 sock_ 会泄漏，ready() 会假 true
            return false;
        }
        up_ = true;
        g_imeUp.store(true);
        logf(ANDROID_LOG_INFO, "输入法桥接已就绪，用时 %lldms 临时目录=%s",
             nowMs() - t0, bridge_.tempDir().c_str());
        return true;
    }

    /**
     * ★★★ 启动期**预热**（用户报的"重启后前几次点击要等好几秒"的根因修复）
     *
     *  实测根因：拉桥接分四步，前三步（写 APK / 建监听 / fork 桥接进程）通常 < 100ms，
     *  而第四步"等 Java 侧 pm install + am start-service + TCP 连回"要 **1.5~2.5 秒**
     *  （仓库内已有实测：`research/ime_result.md` 1475ms、`research/ime_physical_run/README.md` 2449ms）。
     *  旧代码把整条 `open()` 放在**用户第一次点输入框**的那次 `show()` 里同步执行
     *  ⇒ 用户看到的就是"点一下，几秒后才出键盘"。
     *
     *  现在把它挪到主循环启动后（窗口已经在屏幕上）**逐帧非阻塞推进**：
     *    · `warmUpBegin()` 只做快的前三步（不阻塞、无感）；
     *    · `warmUpPoll()` 每帧推进一小步（`pump(0)`/`poll(0)`，永不等）；
     *    · 全程**只建窗、不发 SHOW** ⇒ 不会在启动时主动弹键盘（用户要求）。
     *  这样用户第一次点击时桥接通常已经 READY，`ensureUp()` 直接命中 `up_` 快路径。
     */
    void warmUpBegin() {
        if (up_ || warming_) return;
        if (!bridge_.beginOpen()) {
            logf(ANDROID_LOG_WARN, "输入法桥接预热（前三步）失败: %s", bridge_.lastError());
            return;
        }
        warming_ = true;
        warmStartMs_ = nowMs();
    }

    void warmUpPoll() {
        if (!warming_) return;
        const int r = bridge_.openPoll();
        if (r > 0) {
            warming_ = false;
            up_ = true;
            g_imeUp.store(true);
            logf(ANDROID_LOG_INFO,
                 "输入法桥接预热完成，用时 %lldms（首次点击不再承担冷启动）",
                 nowMs() - warmStartMs_);
        } else if (r < 0) {
            warming_ = false;
            logf(ANDROID_LOG_WARN, "输入法桥接预热失败: %s（点击时会重试）", bridge_.lastError());
        }
    }

    bool warming() const { return warming_; }
    bool trace() const { return imeTrace_; }

    /** 拉起键盘。**返回是否真的把请求发出去了** —— 失败必须让调用者知道，
     *  否则"上升沿"会被一次失败永久吃掉（用户报的"必须重复点击"）。 */
    bool show(int session) {
        if (!ensureUp()) return false;
        aurora::ImeState st;
        st.session = session;
        // ★ 起点必须与界面上**真实的输入框内容**一致（不是我们自己缓存的副本）：
        //   两边起点不同的话，之后每一次同步都在补这个差额 ——
        //   用户看到的就是"莫名其妙多一个字"。
        st.text = renderer_ != nullptr ? renderer_->textInputText() : std::string();
        text_ = st.text;
        st.start = static_cast<int>(text_.size());
        st.end = static_cast<int>(text_.size());
        return bridge_.show(st);
    }

    void hide() {
        if (!up_) return;
        bridge_.hide();
    }

    /** 每帧调用（非阻塞）。 */
    void pump() {
        if (!up_) return;
        const int n = bridge_.pump(0);          // ★ 0 = 完全非阻塞，可以放渲染循环
        if (n < 0) {
            logf(ANDROID_LOG_WARN, "输入法连接断开，收尾");
            shutdown();
        }
    }

    void shutdown() {
        if (!up_) { bridge_.close(); return; }
        bridge_.hide();                          // 先收键盘
        for (int i = 0; i < 5; ++i) {
            if (bridge_.pump(50) < 0) break;     // 最多等 250ms 收可见性回调
        }
        bridge_.close();                         // QUIT → 关 stdin → Java 侧 pm uninstall
        up_ = false;
        g_imeUp.store(false);
    }

    bool up() const { return up_; }
    const char* lastError() const { return bridge_.lastError(); }

    /**
     * ★★ 取出并清掉"用户/系统把键盘收起来了"这个事实。
     *
     * 为什么必须有它（本轮修的真实 bug）：键盘被收起之后
     *   · ImGui 的 InputText 仍然处于 active ⇒ `wantsTextInput()` 恒为 true；
     *   · 而拉起的条件是 `wants && !imeWanted` 的**上升沿**，`imeWanted` 没人复位
     *   ⇒ 之后无论怎么点输入框都不会再调 show()，键盘**再也叫不回来**。
     * 复位 `imeWanted` 只是第一步，还得让 UI 真的失焦（见 Renderer::requestTextInputEnd），
     * 否则 `wants` 仍恒真、上升沿仍然不来。
     */
    bool consumeHiddenEvent() {
        const bool pending = hiddenPending_;
        hiddenPending_ = false;
        return pending;
    }

    // ---- aurora::ImeCallbacks（在 pump 的调用线程上同步执行：只写状态，别碰 GL） ----
    void onEdit(int, const std::string& text, int start, int end) override {
        // ★★ 回传的是**整段已提交文本**（Java 侧 reportEdit 发的快照，已排除拼音等
        //    composing 文本），所以这里做的是**设置**而不是增量对齐：
        //    两份文本状态（Java 的 EditText / ImGui 的 InputText）从此始终一致，
        //    "莫名其妙多一个字 / 少一个字" 从结构上消失。
        //    （历史：旧实现按追加语义 `pendingText_ += 整段` ⇒ 敲 ab 变 aab；
        //      上一版换成"公共前缀 + 退格 + 插入"的增量换算 ⇒ 打字/退格正确，
        //      但只要有一侧先动过（选字、粘贴、输入法一次上屏一整句），
        //      基准就会永久错开 —— 用户反馈的"怪怪的"就是这个。）
        const aurora::ImeTextDelta delta = aurora::ComputeImeTextDelta(text_, text);
        text_ = text;
        if (renderer_ == nullptr || delta.identical) return;
        // 选区是 UTF-8 字节偏移（协议口径），ImGui 的光标用 UTF-16 码元 ⇒ 换算。
        const int selStart = aurora::ImeUtf16IndexFromUtf8(text, start);
        const int selEnd = aurora::ImeUtf16IndexFromUtf8(text, end);
        renderer_->setImeText(text.c_str(), selStart, selEnd);
        if (imeTrace_) {
            logf(ANDROID_LOG_INFO, "[输入法] 镜像同步：%zu 字节，选区 %d..%d（UTF-16）",
                 text.size(), selStart, selEnd);
        }
    }
    void onKeyboard(int, bool visible) override {
        logf(ANDROID_LOG_INFO, "键盘可见性 = %d", visible ? 1 : 0);
    }
    void onHidden(int) override {
        hiddenPending_ = true;
        logf(ANDROID_LOG_INFO, "键盘已收起 → 复位请求状态并让输入框失焦（下次点击可以重新拉起）");
    }
    void onImeInsets(int, const aurora::ImeInsetRect& rect) override {
        if (renderer_ != nullptr) {
            renderer_->setImeInsetRect(rect.left, rect.top, rect.right, rect.bottom);
        }
        if (rect.valid() != lastInsetValid_ || rect.top != lastInsetTop_) {
            lastInsetValid_ = rect.valid();
            lastInsetTop_ = rect.top;
            logf(ANDROID_LOG_INFO, "输入法弹层矩形 = (%d,%d)-(%d,%d)（宿主据此挖掉触摸区并避让）",
                 rect.left, rect.top, rect.right, rect.bottom);
        }
    }
    void onClipboard(const std::string&) override {}

private:
    aurora::InputBridge bridge_;
    aurora::Renderer* renderer_ = nullptr;
    std::string text_;
    bool up_ = false;
    bool hiddenPending_ = false;
    bool imeTrace_ = false;
    bool warming_ = false;          // 启动期预热进行中（冷启动后半段）
    long long warmStartMs_ = 0;     // 预热起点，用于日志里的"用时"
    bool lastInsetValid_ = false;
    int lastInsetTop_ = -1;
};
}  // namespace

// ---------------------------------------------------------------------------
// 帧率投票配置
//
// `AURORA_FRAME_RATE_VOTE="fps[,compatibility[,strategy[,priority]]]"`，`0` 关闭。
// 默认 120fps / compat=1 / strategy=1 / priority=0。
//
// ★ 为什么 compat/strategy 做成可配：这两个是 int8_t 枚举，不同 Android 版本
//   的取值有 ±1 的差别（Default/ExactOrMultiple/Exact 与
//   Default/OnlyIfSeamless/Always），不猜，设备上逐个试即可。
//
// ★★ priority 默认必须是 0：实测 priority=2 会让 Transaction::apply 返回
//   -22（EINVAL），投票**从头到尾都没成功过**，而且只打一行 WARN 就继续跑，
//   从外面完全看不出来。同理 strategy=2(Always) 也是 EINVAL，只有 0/1 合法。
//   这两个坑是设备上逐值试出来的，不是文档里写的。
// ---------------------------------------------------------------------------
struct FrameRateVoteConfig {
    bool enabled = true;
    float fps = 120.0f;
    int compatibility = 1;
    int strategy = 1;
    int priority = 0;
};

FrameRateVoteConfig ReadFrameRateVoteConfig() {
    FrameRateVoteConfig cfg;
    const char* raw = std::getenv("AURORA_FRAME_RATE_VOTE");
    if (raw == nullptr) return cfg;                       // 默认开启
    if (raw[0] == '0' && raw[1] == '\0') {                // 显式关闭
        cfg.enabled = false;
        return cfg;
    }
    float v[4] = {cfg.fps, (float)cfg.compatibility, (float)cfg.strategy, (float)cfg.priority};
    const int n = std::sscanf(raw, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]);
    if (n >= 1 && v[0] > 0.0f) cfg.fps = v[0];
    if (n >= 2) cfg.compatibility = (int)v[1];
    if (n >= 3) cfg.strategy = (int)v[2];
    if (n >= 4) cfg.priority = (int)v[3];
    return cfg;
}

// ---------------------------------------------------------------------------
// ImGui 渲染后端选择菜单
//
// 背景：ELF 侧此前**没有任何入口**能选后端 —— setBackendKind() 只被 APK 通过 JNI
// 调过，所以 Vulkan 后端在真机上从来没跑过。这个菜单把它接到命令行上。
//
// ★ 能力边界（如实说明，不含糊）：液态玻璃的光学（折射/色散/边光/内阴影）与逐面板
//   场景合成器都是 **GLES 实现**的，它们通过 ImGui 的绘制回调执行 GL 调用。
//   ImGui 一旦走 Vulkan，那些 GL 调用不再有意义 ⇒ 该后端下不绘制玻璃。
//   它的用途是在同一台设备上量出"玻璃这一层值多少开销"，不是日常使用形态。
//
// 非交互（管道/脚本）时不显示、不等待，直接返回默认值 —— 绝不阻塞自动化。
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // ★ 第一件事就装崩溃取证：后面的每一行都在碰厂商私有 ABI，
    //   远端机器上崩了只能靠这段落盘的信息定位（见上面的崩溃取证段）。
    installCrashForensics();
    aospglue::detail::startupTraceHook = recordLastLog;

    int displayIdArg = -1;
    int layerStackArg = -1;
    std::string displayIdExplicit;   // --display-id <uint64>：直接给 SF 的 DisplayId
    int maxSeconds = 0;
    bool diagnoseOnly = false;   // --diagnose：只做兼容性自检，不建层、不接管输入
    int captureFps = 120;
    int renderFps = 120;
    bool renderFpsExplicit = false;   // 用户是否显式写了 --render-fps（决定能否跟随屏幕）
    int downscale = 2;
    bool downscaleExplicit = false;   // 用户是否显式写了 --downscale（决定能否自动改成 1:1）
    int backendChoice = -1;           // -1=未指定（走启动菜单）；0=OpenGL ES；1=Vulkan
    bool doCapture = true;
    std::string captureBackend = "mirror";
    std::string frameDumpPath;
    bool wantIme = true;
    // Legacy CPU erase remains opt-in. WMS capture/mirror samples the display's
    // windowing subtree; our sibling root overlay stays outside that subtree.
    // System screenshots still include the overlay (skipScreenshot is off).
    bool cpuErase = false;
    bool doDump = false;
    bool verbose = false;    // --verbose/-v：打开全部输出（默认静默，见 g_quiet）
    bool yesPhysical = false;
    // --record [路径] / --record-seconds N：启动即开录（等价于点主题窗口里的「开始录屏」）。
    // 录制由系统 screenrecord 承担，理由见 ScreenRecorder.h 顶部（自带录屏录不到悬浮窗）。
    std::string recordPath;
    bool recordWanted = false;
    int recordSeconds = 0;
    bool probeEgl = false;   // --probe-egl：只做「建图层 → 取 window → EGL 全链路」，逐步打点
    bool crashTest = false;  // --crash-test：故意段错误，用来验证崩溃取证链路真的有效
    // --move-demo <dx> <dy> [秒]：启动若干秒后把**图层**移动 (dx,dy)，用于验证需求④
    // （region 是图层局部坐标，移动图层时 SF 自动跟随，不需要重设）。
    int moveDemoX = 0;
    int moveDemoY = 0;
    int moveDemoAtSeconds = 8;

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        const char* v = (i + 1 < argc) ? argv[i + 1] : nullptr;
        if (!std::strcmp(a, "--display") && v) { displayIdArg = parseInt(v, -1); ++i; }
        else if (!std::strcmp(a, "--display-id") && v) { displayIdExplicit = v; ++i; }
        else if (!std::strcmp(a, "--layer-stack") && v) { layerStackArg = parseInt(v, -1); ++i; }
        else if (!std::strcmp(a, "--max-seconds") && v) { maxSeconds = parseInt(v, 0); ++i; }
        else if (!std::strcmp(a, "--capture-fps") && v) { captureFps = std::clamp(parseInt(v, 120), 1, 240); ++i; }
        else if (!std::strcmp(a, "--render-fps") && v) { renderFps = std::clamp(parseInt(v, 120), 1, 240); renderFpsExplicit = true; ++i; }
        else if (!std::strcmp(a, "--downscale") && v) { downscale = std::clamp(parseInt(v, 2), 1, 8); downscaleExplicit = true; ++i; }
        else if (!std::strcmp(a, "--backend") && v) {
            if (!std::strcmp(v, "vulkan") || !std::strcmp(v, "vk")) backendChoice = 1;
            else if (!std::strcmp(v, "gles") || !std::strcmp(v, "gl") || !std::strcmp(v, "opengl")) backendChoice = 0;
            else { logf(ANDROID_LOG_ERROR, "--backend 只能是 gles 或 vulkan：%s", v); return 2; }
            ++i;
        }
        else if (!std::strcmp(a, "--capture-backend") && v) { captureBackend = v; ++i; }
        else if (!std::strcmp(a, "--dump-frame") && v) { frameDumpPath = v; ++i; }
        else if (!std::strcmp(a, "--no-capture")) { doCapture = false; }
        else if (!std::strcmp(a, "--cpu-erase")) { cpuErase = true; }
        else if (!std::strcmp(a, "--no-ime")) { wantIme = false; }
        else if (!std::strcmp(a, "--probe-egl")) { probeEgl = true; }
        else if (!std::strcmp(a, "--crash-test")) { crashTest = true; }
        else if (!std::strcmp(a, "--try-region-follow")) { /* compatibility: now always enabled */ }
        else if (!std::strcmp(a, "--move-demo") && v) {
            moveDemoX = parseInt(v, 0);
            if (i + 2 < argc) { moveDemoY = parseInt(argv[i + 2], 0); i += 2; }
        }
        else if (!std::strcmp(a, "--move-at") && v) { moveDemoAtSeconds = parseInt(v, 8); ++i; }
        else if (!std::strcmp(a, "--dump")) { doDump = true; }
    else if (!std::strcmp(a, "--diagnose")) { diagnoseOnly = true; }
        else if (!std::strcmp(a, "--verbose") || !std::strcmp(a, "-v")) { verbose = true; }
        else if (!std::strcmp(a, "--record")) {
            recordWanted = true;
            // 路径可选：给了就用它（下一个参数不以 - 开头时），否则自动带时间戳。
            if (v && v[0] != '-') { recordPath = v; ++i; }
        }
        else if (!std::strcmp(a, "--record-seconds") && v) { recordSeconds = parseInt(v, 0); ++i; }
        else if (!std::strcmp(a, "--yes-physical")) { yesPhysical = true; }
    }

    // ★★★ 交付默认（用户 2026-10-19 要求）：**不带任何选项**运行 == 交付形态。
    //
    //   用户的原话：「这些参数作为默认选项 而不是可选选项，也就是说，这些参数本来就如此，
    //   而且不可调整；不希望有那些参数」。
    //   ⇒ 单文件交付包（.sh）里**不再写任何参数**，点一下就跑；这组行为由 ELF 自己承担，
    //     因此也没有"忘了传参数就跑到虚拟屏 / 永不退出"的坑（历史上真实发生过）。
    //
    //   为什么用 `argc <= 1` 当判据（而不是把这三条改成全局默认值）：
    //     · 自动化验收/调试仍然**显式**传 --display-id / --max-seconds / --verbose …，
    //       它们必须保持原有语义（例如不写 --display 时找虚拟屏、maxSeconds=0 不退出）；
    //     · 把"物理屏 + 允许物理屏"变成全局默认会让**任何**漏传参数的脚本
    //       直接占用用户的日常主力屏 —— 那正是 --yes-physical 这道闸门要防的事。
    //     · 裸跑（交付形态）本来就是"用户自己点的那一下"，所以这里放开闸门是安全的。
    //   仍想改交付行为的人：加参数显式运行即可（parameters 只是不再出现在交付文件里）。
    const bool bareInvocation = (argc <= 1);
    if (bareInvocation) {
        displayIdArg = 0;      // 物理屏
        yesPhysical = true;    // 交付形态的闸门由"用户自己点"承担
        maxSeconds = 300;      // 5 分钟后自动退出，不留常驻覆盖层
    }

    // ★ 默认静默：正常跑起来一个字都不打（用户要求"点了就直接跑"）。
    //   打开的三种方式：--verbose / -v、AURORA_VERBOSE=1、或者任何诊断模式
    //   （--dump / --probe-egl / --crash-test / --dump-frame / --move-demo 本身就是来要日志的）。
    if (const char* vb = std::getenv("AURORA_VERBOSE"); vb && vb[0] == '1') verbose = true;
    if (doDump || probeEgl || crashTest || !frameDumpPath.empty() || moveDemoX != 0 || moveDemoY != 0)
        verbose = true;
    g_quiet = !verbose;
    aospglue::detail::startupTraceSilent = g_quiet;
    // ★ 打开日志时把 stdout 改成**行缓冲**。
    //   踩过的坑：`./aurora_overlay --verbose > /data/local/tmp/x.log` 时 stdout 是
    //   块缓冲（4KB 一块），排查时 tail 那个文件经常"什么新东西都没有" ——
    //   其实日志都在缓冲区里还没落盘（真机上我因此误判过"这一帧没走到那段代码"）。
    //   行缓冲之后每行立刻可见，代价只在 --verbose 模式下存在。
    if (verbose) std::setvbuf(stdout, nullptr, _IOLBF, 0);

    if (captureBackend != "mirror" && captureBackend != "gpu" && captureBackend != "cpu" && captureBackend != "screencap") {
        logf(ANDROID_LOG_ERROR, "--capture-backend must be mirror, gpu, cpu or screencap");
        reportFailure("--capture-backend must be mirror, gpu, cpu or screencap", captureBackend.c_str());
        return 2;
    }
    if (cpuErase && (captureBackend == "gpu" || captureBackend == "mirror")) {
        captureBackend = "cpu"; // explicit CPU erase needs pixels, never apply it to the GPU stream
    }
    if (captureBackend == "screencap") captureFps = std::min(captureFps, 5);

    // --crash-test：故意段错误。用来**验证崩溃取证链路本身有效** ——
    // 一台我们碰不到的机器上崩了，唯一能带回来的就是那段自动落盘的现场信息，
    // 所以这条链路必须自己先被证明能跑通（不能靠"应该没问题"）。
    if (crashTest) {
        logf(ANDROID_LOG_WARN, "崩溃取证自测：即将故意空指针写入（--crash-test）");
        volatile int* boom = nullptr;
        *boom = 1;
        return 99;   // 到不了这里
    }

    // Opt-in comparison setup; production optics and the UI's editable values
    // stay at their original defaults unless this diagnostic is requested.
    const char* latencyTest = std::getenv("AURORA_LATENCY_TEST");
    if (latencyTest && std::strcmp(latencyTest, "1") == 0) {
        secondary_imgui::UseLatencyComparisonWindowOptics();
        logf(ANDROID_LOG_INFO, "延迟对照：整窗玻璃开启，模糊/折射高度/折射强度/色散均为0");
    }

    // ---- 渲染后端：默认 GL，不再询问 ----
    // 液态玻璃的光学（折射/色散/边光/内阴影）与场景合成器都是 GLES 实现，
    // Vulkan 后端看不到玻璃 —— 摆个菜单出来只会让人误选到"没有玻璃"的那个，
    // 而且它曾经直接黑屏。所以默认直接走 GL；确实要用 Vulkan 时显式传
    // `--backend vulkan`（那是性能对照用的，不是日常形态）。
    if (backendChoice < 0) backendChoice = 0;
    logf(ANDROID_LOG_INFO, "渲染后端: %s", backendChoice == 1 ? "Vulkan" : "OpenGL ES 3.x");

    // ---- 兼容性自检（--diagnose）----
    //
    // ★ 必须在**所有显示选择/建层之前**短路。这些步骤在没虚拟屏的机器上必然失败，
    //   而那恰恰是最需要自检的场景（"跑不起来，先看环境"）。
    //   只收集事实，不建图层、不接管输入、不改任何显示状态。
    if (diagnoseOnly) {
        // 先解析符号：自检的价值就在"这台机器的 libgui 到底导出哪些符号"，
        // 不 init 的话整张表全是"缺失"，毫无意义。
        {
            std::string initWhy;
            aospglue::init(&initWhy);
        }
        std::string report;
        char dline[512];
        auto addLine = [&](const char* fmt, ...) {
            va_list ap;
            va_start(ap, fmt);
            std::vsnprintf(dline, sizeof dline, fmt, ap);
            va_end(ap);
            report += dline;
            report += '\n';
        };

        report += "===== Aurora 兼容性自检 =====\n";
        addLine("时间          %s", runCapture("date '+%Y-%m-%d %H:%M:%S' 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("SoC           %s", runCapture("getprop ro.soc.model 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("平台          %s", runCapture("getprop ro.board.platform 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("型号          %s", runCapture("getprop ro.product.model 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("厂商          %s", runCapture("getprop ro.product.manufacturer 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("Android       %s / API %s",
                runCapture("getprop ro.build.version.release 2>&1 | tr -d '\\n'", 1u << 12).c_str(),
                runCapture("getprop ro.build.version.sdk 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("GL            %s", runCapture("getprop ro.hardware.egl 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("SELinux       %s", runCapture("getenforce 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("屏幕          %s", runCapture("wm size 2>&1 | tr -d '\\n'", 1u << 12).c_str());
        addLine("刷新率        %s",
                runCapture("dumpsys SurfaceFlinger 2>/dev/null | grep -m1 activeMode | sed 's/^ *//'", 1u << 20).c_str());
        report += "\n----- libgui 符号解析 -----\n";
        report += aospglue::detail::symbolReport();
        report += "\n----- dumpsys SurfaceFlinger --display-id -----\n";
        report += runCapture("dumpsys SurfaceFlinger --display-id 2>&1", 1u << 20);
        report += "\n----- dumpsys display (head 6000) -----\n";
        report += runCapture("dumpsys display 2>&1 | head -c 6000", 1u << 20);

        const char* outPath = std::getenv("AURORA_DIAGNOSE_LOG");
        if (outPath == nullptr || outPath[0] == '\0') outPath = "/data/local/tmp/aurora_diagnose.log";
        if (FILE* f = std::fopen(outPath, "w")) {
            std::fwrite(report.data(), 1, report.size(), f);
            std::fclose(f);
            logf(ANDROID_LOG_INFO, "自检报告已写入 %s（%zu 字节）", outPath, report.size());
        } else {
            logf(ANDROID_LOG_ERROR, "自检报告写入失败: %s", outPath);
        }
        std::fputs(report.c_str(), stdout);
        std::fflush(stdout);
        aospglue::shutdown();
        return 0;
    }

    // ---- 解析目标显示 ----
    std::string displayList;
    std::string displayId;
    // ★★ 物理屏的 DisplayId 必须**现场解析**，绝不能写死。
    //   真实事故：这里原来比较的是开发机的 `Display 1111111111111111111 (HWC display 0)`，
    //   用户把单文件包发给朋友后，朋友机器上是 `Display 2222222222222222222 (HWC display 0)`
    //   ⇒ 字符串不匹配 ⇒ 走回退分支找虚拟屏 ⇒ 朋友机器上没有虚拟屏
    //   ⇒ 直接报「找不到可用的显示」退出 4。
    const std::string displayDump = runCapture("dumpsys SurfaceFlinger --display-id 2>/dev/null");
    const std::string physicalId = aurora::display_pick::pickPhysicalDisplayId(displayDump);
    displayList = displayDump;

    if (!displayIdExplicit.empty()) {
        // 调用方已查到确切的 SF DisplayId（例如验证脚本从 --display-id 输出里取的），直接用。
        // 这条路径最可靠：避开了"逻辑 display id ↔ SF DisplayId"的二次映射。
        displayId = displayIdExplicit;
    } else if (displayIdArg == 0 && !physicalId.empty()) {
        // --display 0 = 物理屏。判据是"这台机器上真的有物理屏"，而不是某个常量。
        displayId = physicalId;
    } else if (displayIdArg >= 0) {
        // 其它取值（含 --display 0 但本机解析不出物理屏）沿用旧语义：找虚拟屏。
        displayId = aurora::display_pick::pickVirtualDisplayId(displayDump);
    } else {
        displayId = aurora::display_pick::pickVirtualDisplayId(displayDump);
    }

    const bool physical = aurora::display_pick::isPhysicalDisplayId(physicalId, displayId);
    if (physical && !yesPhysical) {
        std::printf("\n");
        std::printf("========================================================\n");
        std::printf("  已拒绝运行：目标显示是【物理屏】（DisplayId %s）。\n", displayId.c_str());
        std::printf("\n");
        std::printf("  这是用户的日常主力机。按项目纪律，物理屏实验必须先征得同意，\n");
        std::printf("  并且只做最少次数。\n");
        std::printf("\n");
        std::printf("  确认要做，请显式加 --yes-physical：\n");
        std::printf("    aurora_overlay --display 0 --yes-physical --max-seconds 30\n");
        std::printf("\n");
        std::printf("  只想安全核对（虚拟屏，不碰物理屏）：\n");
        std::printf("    aurora_overlay --max-seconds 30\n");
        std::printf("========================================================\n");
        return 3;
    }

    if (displayId.empty()) {
        logf(ANDROID_LOG_ERROR, "找不到可用的显示。--display-id 原始输出：\n%s", displayList.c_str());
        // ★ 直接跑 ELF（不经单文件 SH）时最常见的困惑：ELF 自己的默认是"虚拟屏"，
        //   而普通手机上**没有**虚拟屏，于是这里报错退出 4 —— 用户会以为程序坏了。
        //   真实反馈（2026-10-13）：朋友的机器上直接执行二进制就是这个报错。
        //   把"下一步该怎么做"直接写出来，别再让人猜。
        if (!physicalId.empty()) {
            std::printf("\n");
            std::printf("========================================================\n");
            std::printf("  本机没有虚拟屏（只找到物理屏 DisplayId %s）。\n", physicalId.c_str());
            std::printf("\n");
            std::printf("  不带 --display 时，本程序默认找**虚拟屏**（测试工具 调试用），\n");
            std::printf("  手机上一般没有，所以这里必然失败 —— 这不是程序坏了。\n");
            std::printf("\n");
            std::printf("  要在物理屏上跑（主力机，请自行确认）：\n");
            std::printf("    aurora_overlay --display 0 --yes-physical --max-seconds 30\n");
            std::printf("\n");
            std::printf("  单文件交付包 sh aurora_overlay.sh 已经内置了这组参数，\n");
            std::printf("  直接跑那个 .sh 即可，不需要手打。\n");
            std::printf("========================================================\n");
        }
        return 4;
    }
    logf(ANDROID_LOG_INFO, "目标显示 DisplayId=%s physical=%d", displayId.c_str(), physical ? 1 : 0);

    // ---- 信号与看门狗 ----
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    if (maxSeconds > 0) {
        std::thread([maxSeconds] {
            const long long deadline = nowMs() + static_cast<long long>(maxSeconds) * 1000;
            while (g_running.load() && nowMs() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            if (g_running.load()) {
                logf(ANDROID_LOG_WARN, "看门狗触发：已达 --max-seconds %d，准备退出", maxSeconds);
                g_running.store(false);
            }
        }).detach();
    }

    // ---- AOSP 胶水 ----
    std::string why;
    if (!aospglue::init(&why)) {
        logf(ANDROID_LOG_ERROR, "aospglue::init 失败: %s", why.c_str());
        reportFailure("平台库初始化失败", why.c_str());
        return 5;
    }



    // 图层尺寸：整屏。因为照搬的界面里两个 ImGui 窗口都可以被用户拖动，
    // 图层必须够大。整屏图层本身不危险 —— 危险的是 touchOcclusionMode 保持默认，
    // 那一项在 attachInputWindow 里被显式设成 ALLOW(2)。
    // layerStack 必须现查（虚拟屏每次重建都会变），除非调用方显式给了。
    const std::string displayName = findDisplayName(displayList, displayId);
    logf(ANDROID_LOG_INFO, "目标显示名 = \"%s\"", displayName.c_str());
    int layerStack = layerStackArg;
    if (layerStack < 0) {
        layerStack = findLayerStack(displayId, displayName);
    }
    if (layerStack < 0) {
        logf(ANDROID_LOG_ERROR,
             "无法确定 layerStack（DisplayId=%s）。用 --layer-stack N 显式指定。",
             displayId.c_str());
        // ★ 兼容性排查：把**原始 dumpsys 输出**一起落盘。
        //   不同厂商（8 Gen 3 / 天玑 上的小米/OPPO/vivo/荣耀…）dumpsys 格式差异很大，
        //   只写"找不到"无从判断是"格式没匹配"还是"真的没有"。这里保留证据。
        {
            std::string dump;
            dump += "===== 无法确定 layerStack =====\n";
            dump += "DisplayId   = " + displayId + "\n";
            dump += "displayName = " + displayName + "\n";
            dump += "\n----- dumpsys SurfaceFlinger --display-id -----\n";
            dump += runCapture("dumpsys SurfaceFlinger --display-id 2>&1", 1u << 20);
            dump += "\n----- dumpsys SurfaceFlinger (head 4000) -----\n";
            dump += runCapture("dumpsys SurfaceFlinger 2>&1 | head -c 4000", 1u << 20);
            dump += "\n----- dumpsys display (head 4000) -----\n";
            dump += runCapture("dumpsys display 2>&1 | head -c 4000", 1u << 20);
            reportFailure("无法确定 layerStack（可用 --layer-stack N 指定）", dump.c_str());
        }
        aospglue::shutdown();
        return 6;
    }
    const int logicalDisplayId = displayIdArg >= 0 ? displayIdArg
        : findLogicalDisplayId(displayName, physical);
    if (doCapture && captureBackend != "screencap" && logicalDisplayId < 0) {
        logf(ANDROID_LOG_ERROR, "找不到WMS逻辑displayId，请指定 --display N");
        {
            std::string dump;
            dump += "===== 找不到 WMS 逻辑 displayId =====\n";
            dump += "displayName = " + displayName + "\n";
            dump += "\n----- dumpsys display (head 6000) -----\n";
            dump += runCapture("dumpsys display 2>&1 | head -c 6000", 1u << 20);
            reportFailure("找不到 WMS 逻辑 displayId（可用 --display N 指定）", dump.c_str());
        }
        aospglue::shutdown();
        return 6;
    }
    auto initialGeometry = aurora::ParseDisplayGeometry(
        runCapture("dumpsys display 2>/dev/null", 4u << 20), logicalDisplayId);
    // ★ 兜底：dumpsys 的 DisplayInfo 格式各厂商不同，严格解析失败时退回 wm size。
    //   wm size 是稳定接口（`wm size` 输出 "Physical size: WxH" / "Override size: WxH"），
    //   虽然不带旋转信息，但比"读不到尺寸就拒绝启动"好得多 —— 拿不到旋转时按 0 处理，
    //   后续每帧还有 geometry 刷新逻辑会纠正。
    if (!initialGeometry.valid()) {
        const std::string wmSize = runCapture("wm size 2>/dev/null", 1u << 16);
        int sw = 0;
        int sh = 0;
        if (aurora::ParseWmSize(wmSize, &sw, &sh)) {
            initialGeometry.width = sw;
            initialGeometry.height = sh;
            initialGeometry.rotation = 0;
            logf(ANDROID_LOG_WARN, "DisplayInfo 解析失败，改用 wm size 兜底：%dx%d", sw, sh);
        }
    }
    if (!initialGeometry.valid()) {
        logf(ANDROID_LOG_ERROR, "无法读取目标逻辑显示的当前尺寸/旋转，拒绝用固定竖屏尺寸启动");
        {
            std::string dump;
            dump += "===== 无法读取逻辑显示几何 =====\n";
            dump += "logicalDisplayId = " + std::to_string(logicalDisplayId) + "\n";
            dump += "\n----- dumpsys display (head 6000) -----\n";
            dump += runCapture("dumpsys display 2>&1 | head -c 6000", 1u << 20);
            reportFailure("无法读取目标逻辑显示的尺寸/旋转", dump.c_str());
        }
        aospglue::shutdown();
        return 6;
    }
    int layerW = initialGeometry.width;
    int layerH = initialGeometry.height;
    std::mutex geometryMutex;
    aurora::DisplayGeometry pendingGeometry = initialGeometry;
    aurora::DisplayGeometry appliedGeometry = initialGeometry;
    logf(ANDROID_LOG_INFO, "图层参数: %dx%d layerStack=%d logicalDisplay=%d capture=%s/%dfps downscale=%d",
         layerW, layerH, layerStack, logicalDisplayId, captureBackend.c_str(), captureFps, downscale);
    // ---- 全屏接管与镜像分辨率是配套的 ----
    //
    // 全屏接管把镜像画面不透明地铺满整屏。若镜像仍是降采样（默认 2×），放大后
    // **整块屏幕**都会糊 —— 那比"只有玻璃糊"更糟。所以这里把降采样自动关掉。
    // 用户显式写了 --downscale 就尊重他（只在启动日志里提醒）。
    //
    // ★ 这里曾经按"像素数是否超过 3.0Mpx"决定要不要保留降采样，想给 2K/平板省带宽。
    //   实测回顾（用户反馈）：平板画面**直接变模糊**，而卡顿**一点没改善**。
    //   结论：降分辨率换流畅在这台平板上不成立 —— 模糊是确定的代价，流畅是假的。
    //   现在无条件 1:1：视觉优先，性能问题另找根因（见 Renderer 里的屏幕刷新率跟随）。
    {
        const char* takeover = std::getenv("AURORA_FULLSCREEN_TAKEOVER");
        const bool takeoverOn = !(takeover != nullptr && takeover[0] == '0' && takeover[1] == '\0');
        if (takeoverOn && downscale > 1 && !downscaleExplicit) {
            logf(ANDROID_LOG_INFO,
                 "全屏接管：镜像降采样 %d → 1（整屏铺满必须 1:1，否则整屏发糊）", downscale);
            downscale = 1;
        } else if (takeoverOn && downscale > 1) {
            logf(ANDROID_LOG_WARN,
                 "全屏接管已开启但 --downscale %d：镜像会被放大到整屏，画面会糊（去掉该参数即自动选）",
                 downscale);
        }
    }

    aospglue::Overlay overlay;
    logf(ANDROID_LOG_INFO, "[Startup] overlay.create begin");
    if (!overlay.create("AuroraOverlay", layerW, layerH, layerStack)) {
        logf(ANDROID_LOG_ERROR, "建图层失败: %s", overlay.error());
        reportFailure("建图层失败", overlay.error());
        aospglue::shutdown();
        return 6;
    }
    logf(ANDROID_LOG_INFO, "[Startup] overlay.create complete layerId=%d", overlay.layerId());

    // ---- 帧率投票：告诉 SurfaceFlinger「这块玻璃要 120Hz」----
    //
    // 为什么必须有它（测试机 / Android 16 实测，不是推测）：
    //   覆盖层是 SF 里 mRoot 的兄弟、**不是 WMS 窗口** ⇒ 系统的刷新率与调频策略
    //   完全不知道我们要 120fps。压在设置界面上时 display 稳在 60Hz、GPU 稳在
    //   222MHz（14 档里的最低档），于是**同一份代码** submit 被驱动背压拖到
    //   14.5ms（≈60fps 上限）；而 display 120Hz + GPU 高频时 submit=3.9ms、
    //   gpu=1.2ms、present=120.6fps —— 完整光学，一个效果都没关。
    //   `ANativeWindow_setFrameRate` 对裸图层不产生 display mode 投票，只有
    //   SurfaceControl 事务里的 setFrameRate 才会进 SF 的 VRI。
    const FrameRateVoteConfig vote = ReadFrameRateVoteConfig();
    if (vote.enabled) {
        if (overlay.applyFrameRateVote(vote.fps, vote.compatibility, vote.strategy,
                                       vote.priority)) {
            logf(ANDROID_LOG_INFO, "帧率投票: 已提交 %.0ffps（compat=%d strategy=%d priority=%d）",
                 vote.fps, vote.compatibility, vote.strategy, vote.priority);
        } else {
            logf(ANDROID_LOG_WARN, "帧率投票失败: %s（继续运行，只是不保证 120Hz）", overlay.error());
        }
    }
    ANativeWindow* anw = overlay.window();
    if (anw == nullptr) {
        logf(ANDROID_LOG_ERROR, "无法取得有效 ANativeWindow：Surface 为空或系统对象布局不兼容（见 Startup 日志）");
        reportFailure("无法取得有效 ANativeWindow（Surface 为空或布局不兼容）", overlay.error());
        overlay.destroy();
        aospglue::shutdown();
        return 7;
    }

    // ---- --probe-egl：把「EGL / 厂商驱动加载」与 Renderer 的其余部分隔离开 ----
    //
    // 背景：真机首跑时 `GlesBackend::createGraphics` 崩在 `eglGetDisplay(EGL_DEFAULT_DISPLAY)`
    // 里，而崩点**不在我们的代码**，在动态链接器：
    //     #00 linker64           solist_add_soinfo+16
    //     #07 libvndksupport.so  android_load_sphal_library
    //     #08 libEGL.so          android::load_system_driver   ← dlopen 厂商 GPU 驱动
    //     #12 libEGL.so          eglGetDisplay+28
    // 同一序列在 probe_m3_ytbl 里是通过的 ⇒ 差异出在「本进程先前做了什么」。
    // 这个模式逐步打点，用来二分定位。
    if (probeEgl) {
        // NDK 不导出 ANDROID_NATIVE_WINDOW_MAGIC，这里按 AOSP 的定义本地算一份：
        //   ANDROID_NATIVE_WINDOW_MAGIC = ANDROID_NATIVE_MAKE_CONSTANT('_','w','n','d')
        //   ANDROID_NATIVE_MAKE_CONSTANT(a,b,c,d) = (a<<24)|(b<<16)|(c<<8)|d
        // 它是 ANativeWindow 的第一个字段（common.magic），用来判定一个指针
        // 到底是不是真的 ANativeWindow。
        constexpr std::uint32_t kNativeWindowMagic = 0x5F776E64u;  // "_wnd"

        logf(ANDROID_LOG_INFO,
             "[egl-probe]  1) overlay 已建，window()=%p", static_cast<void*>(anw));
        const std::uint32_t magic = *reinterpret_cast<const std::uint32_t*>(anw);
        logf(ANDROID_LOG_INFO, "[egl-probe]  2) window->common.magic=0x%08x （期望 0x%08x）%s", magic,
             kNativeWindowMagic, magic == kNativeWindowMagic ? " √" : " × 指针不是 ANativeWindow");
        logf(ANDROID_LOG_INFO, "[egl-probe]  2a) rawLayer()=%p rawSurface()=%p size_t(max_align_t)=%zu",
             overlay.rawLayer(), overlay.rawSurface(), sizeof(std::max_align_t));

        // ★ 2b) A/B 测试 getSurface 的返回约定。
        //
        //   现象：`surface_.get()` 得到 0x7ab36a87a0，而该地址起 48 字节读出来是
        //   一串「像寄存器溢出值」的指针 —— 那是我们**自己的栈**。说明按
        //   `StrongPointer<void>`（64 字节，走 sret）接返回值时，真实结果没落到
        //   我们读的位置。
        //   这里对同一个符号做两种调用约定的对照，看哪个才是真的 Surface*：
        //     (a) StrongPointer<void>(*)(void*)   —— 胶水现在的写法（sret）
        //     (b) void*(*)(void*)                 —— 裸指针（x0）
        //   判据：哪个结果的 offset 0 是 "_wnd"，哪个就对。
        {
            void* h = ::dlopen("libgui.so", RTLD_NOW | RTLD_GLOBAL);
            void* sym = h ? ::dlsym(h, "_ZN7android14SurfaceControl10getSurfaceEv") : nullptr;
            logf(ANDROID_LOG_INFO, "[egl-probe]  2b) dlopen(libgui)=%p dlsym(getSurface)=%p", h, sym);
            if (sym != nullptr && overlay.rawLayer() != nullptr) {
                logf(ANDROID_LOG_INFO, "[egl-probe]  2b0) sizeof(StrongPointer<void>)=%zu",
                     sizeof(aospglue::detail::StrongPointer<void>));
                auto asSp = reinterpret_cast<aospglue::detail::StrongPointer<void> (*)(void*)>(sym);
                const aospglue::detail::StrongPointer<void> r1 = asSp(overlay.rawLayer());
                logf(ANDROID_LOG_INFO,
                     "[egl-probe]  2c) (a) 按 sp<Surface> 直接调 → %p（胶水的 window() 给出的是 %p）",
                     r1.get(), overlay.rawSurface());
                // ★ 2e) 在 A/B 拿到的指针附近找 "_wnd" 魔数 —— 这是「Surface 里
                //        ANativeWindow 子对象到底在哪个偏移」的直接答案。
                if (r1.get() != nullptr) {
                    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(r1.get());
                    int found = -1;
                    for (int off = 0; off <= 2048 && found < 0; off += 8) {
                        if (*reinterpret_cast<const std::uint32_t*>(base + off) ==
                            kNativeWindowMagic) {
                            found = off;
                        }
                    }
                    logf(ANDROID_LOG_INFO,
                         "[egl-probe]  2e) 在 %p 之后 2KB 内找 \"_wnd\" → 偏移 %d %s", r1.get(),
                         found, found < 0 ? "（仍未找到）" : "★ 找到了！");
                    if (found >= 0) {
                        ANativeWindow* w2 = reinterpret_cast<ANativeWindow*>(
                            const_cast<std::uint8_t*>(base) + found);
                        logf(ANDROID_LOG_INFO, "[egl-probe]  2f) 真实 ANativeWindow = %p（偏移 +%d）",
                             static_cast<void*>(w2), found);
                        logf(ANDROID_LOG_INFO, "[egl-probe]  2g) 用它试 EGL ...");
                        EGLDisplay d2 = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                        EGLint a = 0, b = 0;
                        if (eglInitialize(d2, &a, &b)) {
                            const EGLint ca[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                                 EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                                 EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                                 EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
                            EGLConfig cf = nullptr;
                            EGLint nc = 0;
                            eglChooseConfig(d2, ca, &cf, 1, &nc);
                            const EGLint xa[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
                            EGLContext cx = eglCreateContext(d2, cf, EGL_NO_CONTEXT, xa);
                            EGLSurface s2 = eglCreateWindowSurface(
                                d2, cf, reinterpret_cast<EGLNativeWindowType>(w2), nullptr);
                            logf(ANDROID_LOG_INFO,
                                 "[egl-probe]  2h) eglCreateWindowSurface=%p err=0x%x surface=%s",
                                 static_cast<void*>(s2), eglGetError(),
                                 s2 != EGL_NO_SURFACE ? "√ 成功！" : "× 失败");
                            if (s2 != EGL_NO_SURFACE && eglMakeCurrent(d2, s2, s2, cx)) {
                                const char* rr =
                                    reinterpret_cast<const char*>(glGetString(GL_RENDERER));
                                logf(ANDROID_LOG_INFO, "[egl-probe]  2i) ★★ GL_RENDERER=%s",
                                     rr ? rr : "(null)");
                                logf(ANDROID_LOG_INFO,
                                     "[egl-probe] ★★ 结论：ANativeWindow 偏移应为 +%d（原代码用 +16）",
                                     found);
                                return 0;
                            }
                        }
                    }
                }
            }
        }

        // ★ 3) 扫描内存，找出 "_wnd" 魔数的真实偏移。
        //
        //   背景：`window()` 里的 `+16`（照抄 probe_m3_ytbl.cpp:431-433 的
        //   `+ sizeof(std::max_align_t)/2`）在本进程里给出的是**野指针**
        //   （magic=0x7bf12448），直接导致 eglCreateWindowSurface SIGBUS。
        //   Surface 是 C++ 多继承对象，ANativeWindow 子对象的偏移取决于继承布局，
        //   「照抄一个魔数」是脆的 —— 这里改成**按魔数搜索**，不依赖任何硬编码偏移。
        const std::uint8_t* surfaceBase = reinterpret_cast<const std::uint8_t*>(anw) - 16;
        int magicOffset = -1;
        for (int off = 0; off <= 256; off += 8) {
            const std::uint32_t m = *reinterpret_cast<const std::uint32_t*>(surfaceBase + off);
            if (m == kNativeWindowMagic) {
                magicOffset = off;
                break;
            }
        }
        if (magicOffset < 0) {
            // 也试一下负方向（ANativeWindow 可能排在 Surface 基类之前）
            for (int off = -8; off >= -64; off -= 8) {
                const std::uint32_t m = *reinterpret_cast<const std::uint32_t*>(surfaceBase + off);
                if (m == kNativeWindowMagic) {
                    magicOffset = off;
                    break;
                }
            }
        }
        // ★ 近处没有就大范围搜（±8KB），把「偏移错」和「指针本身是野的」区分开
        if (magicOffset < 0) {
            for (int off = -8192; off <= 8192; off += 8) {
                if (off > -320 && off < 320) continue;  // 上面已经搜过
                const std::uint32_t m = *reinterpret_cast<const std::uint32_t*>(surfaceBase + off);
                if (m == kNativeWindowMagic) {
                    magicOffset = off;
                    break;
                }
            }
        }
        logf(ANDROID_LOG_INFO,
             "[egl-probe]  3) surface 基址=%p，\"_wnd\" 魔数偏移 = %d %s",
             static_cast<const void*>(surfaceBase), magicOffset,
             magicOffset < 0 ? "（±8KB 内都没有！）" : "");
        // 直接把开头 48 字节 dump 出来 —— 用来判断这个指针到底指向什么
        {
            char line[160];
            int p = 0;
            for (int i = 0; i < 48 && p < 130; i += 4) {
                const std::uint32_t v =
                    *reinterpret_cast<const std::uint32_t*>(surfaceBase + i);
                p += std::snprintf(line + p, sizeof(line) - p, "%08x ", v);
            }
            line[p] = '\0';
            logf(ANDROID_LOG_INFO, "[egl-probe]  3a) 该地址起 48 字节: %s", line);
        }
        if (magicOffset < 0) return 19;
        ANativeWindow* realWindow =
            reinterpret_cast<ANativeWindow*>(const_cast<std::uint8_t*>(surfaceBase) + magicOffset);
        logf(ANDROID_LOG_INFO, "[egl-probe]  3b) 真实 ANativeWindow = %p（偏移 %+d，原代码用的是 +16）",
             static_cast<void*>(realWindow), magicOffset);

        logf(ANDROID_LOG_INFO, "[egl-probe]  4) 调 eglGetDisplay ...");
        EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        logf(ANDROID_LOG_INFO, "[egl-probe]  5) eglGetDisplay=%p", static_cast<void*>(dpy));
        if (dpy == EGL_NO_DISPLAY) {
            logf(ANDROID_LOG_ERROR, "[egl-probe] eglGetDisplay 失败");
            return 20;
        }
        EGLint mj = 0, mn = 0;
        logf(ANDROID_LOG_INFO, "[egl-probe]  6) 调 eglInitialize ...");
        const EGLBoolean okInit = eglInitialize(dpy, &mj, &mn);
        logf(ANDROID_LOG_INFO, "[egl-probe]  7) eglInitialize=%d 版本=%d.%d err=0x%x",
             static_cast<int>(okInit), mj, mn, eglGetError());
        if (!okInit) return 21;
        const EGLint cfgAttrs[] = {EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                   EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                   EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig cfg = nullptr;
        EGLint ncfg = 0;
        eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &ncfg);
        logf(ANDROID_LOG_INFO, "[egl-probe]  8) eglChooseConfig n=%d cfg=%p", ncfg,
             static_cast<void*>(cfg));
        const EGLint ctxAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttrs);
        logf(ANDROID_LOG_INFO, "[egl-probe]  9) eglCreateContext=%p err=0x%x",
             static_cast<void*>(ctx), eglGetError());
        logf(ANDROID_LOG_INFO, "[egl-probe] 10) 调 eglCreateWindowSurface（用魔数定位到的指针）...");
        EGLSurface es = eglCreateWindowSurface(
            dpy, cfg, reinterpret_cast<EGLNativeWindowType>(realWindow), nullptr);
        logf(ANDROID_LOG_INFO, "[egl-probe] 11) eglCreateWindowSurface=%p err=0x%x",
             static_cast<void*>(es), eglGetError());
        if (es == EGL_NO_SURFACE) return 22;
        const EGLBoolean cur = eglMakeCurrent(dpy, es, es, ctx);
        logf(ANDROID_LOG_INFO, "[egl-probe] 12) eglMakeCurrent=%d err=0x%x", static_cast<int>(cur),
             eglGetError());
        const char* rend = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        logf(ANDROID_LOG_INFO, "[egl-probe] 13) GL_RENDERER=%s", rend ? rend : "(null)");
        logf(ANDROID_LOG_INFO, "[egl-probe] ★ 全链路通过：真实偏移 = %+d", magicOffset);
        return 0;
    }

    // Start with no usable touch area; publish the visible frame's exact region
    // after rendering. Never reserve the historical 800x2160 bounding box.
    logf(ANDROID_LOG_INFO, "[Startup] input.attach begin");
    if (!overlay.attachInputWindow("AuroraOverlay", 1, 1) ||
        !overlay.updateTouchableRegionRects({})) {
        const std::string inputFailure = overlay.error();
        if (!aospglue::detail::inputChainReady()) {
            // 这家厂商的 libbinder/libgui 签名与预期不同，整条输入链路建不起来。
            // 但"画面"和"触摸区"是两件独立的事：没有触摸区，覆盖层照样能把画面
            // 画到屏幕上。直接退出会让这类设备永远跑不起来，所以这里降级。
            logf(ANDROID_LOG_WARN,
                 "输入链路不可用（%s）—— 降级为纯显示：画面正常，但没有精确触摸区",
                 inputFailure.c_str());
            std::fprintf(stderr,
                         "[overlay] 降级：本机缺输入相关符号，只能显示画面，无法接管触摸\n");
        } else {
            logf(ANDROID_LOG_ERROR, "挂精确输入窗口失败: %s", inputFailure.c_str());
            reportFailure("挂精确输入窗口失败", inputFailure.c_str());
            overlay.destroy();
            aospglue::shutdown();
            return 8;
        }
    }
    logf(ANDROID_LOG_INFO, "触摸拦截区由可见窗口圆角并集逐帧更新，空白处穿透");

    {
        std::string check;
        // Validate platform ABI and region replacement before accepting touches.
        const bool ok = aospglue::selfCheck(&check);        // ★ 自检结果是多行长文本，**必须直接写 stdout**，不能只丢给 logcat：
        //   logcat 单条消息上限约 4KB（LOGGER_ENTRY_MAX_PAYLOAD），整块打会被**静默截断**，
        //   结果是「看到前几项全 OK，但程序用退出码 9 拒绝了启动」——查不到是哪一项失败。
        //   这个坑实际发生过一次。
        //   ★ 默认静默时不往屏幕打（用户要求），失败项照旧走 logcat + 落文件。
        if (!g_quiet) {
            std::fputs("===== 自检开始 =====\n", stdout);
            std::fputs(check.c_str(), stdout);
            std::fputs("===== 自检结束 =====\n", stdout);
            std::fflush(stdout);
        }
        logf(ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "自检结果: %s（%s）",
             ok ? "全部通过" : "★ 有失败项", g_quiet ? "详情见 error.log" : "详见 stdout，logcat 会截断");
        if (!ok) {
            // 逐条把失败项用**短行**打到 logcat —— 短行不会被单条长度上限截断。
            size_t p = 0;
            while ((p = check.find("[FAIL]", p)) != std::string::npos) {
                size_t e = check.find('\n', p);
                if (e == std::string::npos) e = check.size();
                logf(ANDROID_LOG_ERROR, "自检失败项 %s", check.substr(p, e - p).c_str());
                p = e;
            }
            reportFailure("平台自检有失败项（详见详情）", check.c_str());
            overlay.destroy();
            aospglue::shutdown();
            return 9;
        }
    }

    // ---- 渲染栈（照搬用户界面的那张牌：LiquidGlassVisible=true） ----
    auto rendererOwner = std::make_unique<aurora::Renderer>();
    auto& renderer = *rendererOwner;
    if (backendChoice == 1) {
        // Vulkan：ImGui 那一层换后端（玻璃光学是 GLES 实现，见本段注释）。
        // 初始化失败时 Renderer 会自动回退到 GLES3 并把它记进 descriptor（见 Renderer::run）。
        renderer.setBackendKind(aurora::BackendKind::Vulkan);
    }
    renderer.setHostMode(aurora::HostMode::RootNative);
    // ★ 帧率跟随屏幕：屏幕只有 60Hz 时按 120fps 出帧，一半的帧永远不会被显示，
    //   代价却是实打实的 —— 平板上实测直接把 GPU 顶满然后掉帧。
    //   这里读一次**屏幕真实刷新率**（fork dumpsys，只读文本，53ms，零 ABI 风险），
    //   只有在它明确低于请求值时才降。用户显式写了 --render-fps 就尊重他。
    //
    //   ★ 为什么不用 libgui 的 getActiveDisplayMode：那条路返回 sp<IBinder>，
    //     本 ELF 静态链接 libc++，析构函数跨 ABI 调用会直接 SIGABRT（已实测崩过）。
    //   ★ 也不用我们自己的 present fps 去反推屏幕刷新率：那是输出不是输入，
    //     会形成"渲染 60 → 判定屏幕 60 → 限制 60 → 永远回不去"的死锁。
    {
        const float panelHz = aospglue::detail::queryPanelRefreshRate();
        if (panelHz > 0.5f) {
            logf(ANDROID_LOG_INFO, "屏幕刷新率: %.1fHz", panelHz);
            if (!renderFpsExplicit && panelHz + 0.5f < static_cast<float>(renderFps)) {
                logf(ANDROID_LOG_INFO,
                     "帧率跟随屏幕：%dfps → %.0ffps（屏幕只有 %.1fHz，多出的帧不会被显示）",
                     renderFps, panelHz, panelHz);
                renderFps = static_cast<int>(panelHz + 0.5f);
            }
        } else {
            logf(ANDROID_LOG_WARN, "屏幕刷新率: 读不到，按 %dfps 出帧", renderFps);
        }
    }
    renderer.setTargetFrameRate(static_cast<float>(renderFps));
    renderer.setLiquidGlassVisible(true);        // ★ 切到 secondary_imgui 那套界面
    renderer.setSafeRegion(0.0f, 0.0f, 0.0f, 0.0f);
    // ★ 触觉反馈：把 Binder 实现注册进界面层（长按激活时"轻震一下"）。
    //   失败**不影响任何其它功能**：界面层拿到不可用就静默跳过（安全降级）。
    //   为什么在这里：aosp_glue 的平台符号此时已解析完，而界面层（aurora_glass_ui）
    //   本身不依赖 binder —— 它只持有一个函数指针。
    {
        const bool hapticOk = aurora::haptic_binder::InstallAsSink();
        logf(ANDROID_LOG_INFO, "触觉反馈：%s（%s）", hapticOk ? "可用" : "不可用",
             aurora::haptic_binder::Why().c_str());
    }
    renderer.surfaceCreated(anw);                // 内部会 InitializeLiquidGlass() + 起渲染线程
    renderer.surfaceChanged(layerW, layerH, 2.0f);

    ImeHost ime;
    // 宿主窗必须建在 display 0：系统输入法只肯绑定 top-focused 显示上的窗口，
    // 而虚拟显示永远不是 top-focused（详见 docs/SOLVING_FIVE_ANDROID16_CHALLENGES.md 难题 5）。
    // 注意这与「玻璃覆盖层画在哪个显示」是两件事，互不冲突。
    ime.attach(&renderer, 0);
    // ★ 但这时必须**说明白**：覆盖层在虚拟屏上时，键盘（如果显示的话）只会出现在
    //   top-focused 的那块屏（物理屏）上，甚至根本不显示 —— 这属于 AOSP 的硬约束，
    //   不是"输入法坏了"。以前没有任何提示，每次虚拟屏验收都会被误判一次
    //   （build/recon/_pull/m.log 里"724 个触点、ime 恒为 0"就是这么来的）。
    if (physical != true) {
        logf(ANDROID_LOG_WARN,
             "覆盖层画在非物理显示（%s）上：系统输入法只能绑定 top-focused 显示，"
             "因此键盘不会出现在这块屏上。要验输入法请用 --display 0 --yes-physical。",
             displayId.c_str());
    }

    // ---- 触摸：把多指 MOTION 逐触点喂给 Renderer ----
    //
    // 这就是「ELF 窗口无法多点触控」的修法所在：
    //   一次 MOTION 报文里带全部 pointerCount 个触点 → 每个触点单独 submit 给
    //   InputRouter（按 pointerId 维护 10 个槽位）→ GestureRouter 从快照里算出
    //   pan / pinch(scale) / rotate → HostInfo → 界面里的「活动触点 / Pan / Pinch / Rotate」
    //   那一栏就会跟着动。若只喂主指针，那一栏永远是 1 个触点。
    std::atomic<long long> touchEvents{0};
    std::atomic<long long> touchPointers{0};
    struct TouchSinkCtx final {
        aurora::Renderer* renderer;
        std::atomic<long long>* events;
        std::atomic<long long>* pointers;
    };
    TouchSinkCtx sinkCtx{&renderer, &touchEvents, &touchPointers};
    // ★ 触摸来源探针（AURORA_TOUCH_TRACE=1）：把每条 MOTION 的动作/坐标打出来。
    //   为什么要它：真机上出现过"没人碰屏幕、窗口尺寸却变了"的现象 ——
    //   只有把**输入通道真实收到的东西**打出来，才能分清是幽灵触摸、
    //   还是我们自己的手势状态机有问题（别靠猜）。
    static const bool kTouchTrace = [] {
        const char* v = ::getenv("AURORA_TOUCH_TRACE");
        return v != nullptr && v[0] == '1';
    }();
    aospglue::TouchFn sink = [](void* user, const aospglue::Touch* touches, int count,
                                int action, int actionIndex) {
        auto* ctx = static_cast<TouchSinkCtx*>(user);
        if (ctx == nullptr || ctx->renderer == nullptr) return;
        if (kTouchTrace && count > 0) {
            logf(ANDROID_LOG_INFO, "[触摸] action=%d idx=%d 触点=%d 首个=(%.0f,%.0f) id=%d",
                 action, actionIndex, count, touches[0].x, touches[0].y, touches[0].pointerId);
        }
        for (int i = 0; i < count; ++i) {
            ctx->renderer->touch(touches[i].action, touches[i].pointerId,
                                 touches[i].x, touches[i].y,
                                 touches[i].pressure, touches[i].size, touches[i].toolType,
                                 static_cast<double>(touches[i].eventTimeNanos) / 1e9);
        }
        if (ctx->events != nullptr) ctx->events->fetch_add(1);
        if (ctx->pointers != nullptr) ctx->pointers->fetch_add(count);
        (void)action; (void)actionIndex;
    };
    std::thread touchThread([&] {
        while (g_running.load()) {
            const int n = aospglue::pollTouches(sink, &sinkCtx);
            if (n < 0) {
                logf(ANDROID_LOG_ERROR, "输入通道断开，停止覆盖层");
                g_exitStatus.store(11);
                g_running.store(false);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });

    // Real display background. The persistent helper captures the WMS display root;
    // eSkipScreenshot removes this overlay from the capture before any shader runs.
    std::thread captureThread;
    std::atomic<long long> captureFrames{0}, captureSkippedLowMem{0}, captureErrors{0}, captureErased{0};
    std::atomic<long long> captureTotalUs{0};
    std::mutex captureErrorMutex;
    std::string captureError;
    // Frame callbacks keep this client alive until the GPU has returned every
    // mirror Image lease. Stop it after Renderer tears down its graphics.
    auto realtime = std::make_shared<aurora::RealtimeCapture>(logicalDisplayId, downscale);
    if (captureBackend == "mirror") realtime->setMirrorOptions(true, static_cast<float>(captureFps));
    if (doCapture) {
        captureThread = std::thread([&] {
            ScreenCapture legacy(displayId, downscale);
            const auto interval = std::chrono::nanoseconds(1000000000LL / captureFps);
            auto nextCapture = std::chrono::steady_clock::now();
            std::vector<std::uint8_t> rgba;
            int w = 0, h = 0;
            std::string why;
            long long captureUs = 0, handoffUs = 0, readbackUs = 0;
            int timedFrames = 0;
            auto nextMemoryCheck = std::chrono::steady_clock::time_point{};
            long long availableKb = -1;
            aurora::DisplayGeometry captureGeometry = initialGeometry;
            while (g_running.load()) {
                std::this_thread::sleep_until(nextCapture);
                if (!g_running.load()) break;
                const auto begin = std::chrono::steady_clock::now();
                if (begin >= nextMemoryCheck) {
                    availableKb = memAvailableKb();
                    nextMemoryCheck = begin + std::chrono::seconds(1);
                }
                if (availableKb >= 0 && availableKb < kMinAvailableKbToCapture) {
                    captureSkippedLowMem.fetch_add(1);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                } else {
                    if (cpuErase) {
                        const auto r = aurora::LiquidGlassRegion();
                        const int x = static_cast<int>(r.left), y = static_cast<int>(r.top);
                        const int ew = std::max(0, static_cast<int>(r.right) - x);
                        const int eh = std::max(0, static_cast<int>(r.bottom) - y);
                        realtime->setEraseRect(x, y, ew, eh);
                        legacy.setEraseRect(x, y, ew, eh);
                    }
                    AHardwareBuffer* buffer = nullptr;
                    const bool ok = (captureBackend == "gpu" || captureBackend == "mirror")
                        ? realtime->grabHardwareBuffer(&buffer, &w, &h, &why)
                        : captureBackend == "cpu"
                            ? realtime->grabOnce(&rgba, &w, &h, &why)
                            : legacy.grabOnce(&rgba, &w, &h, &why);
                    const auto captured = std::chrono::steady_clock::now();
                    if (ok) {
                        const auto timing = realtime->timing();
                        const aurora::DisplayGeometry nextGeometry{
                            static_cast<int>(timing.logicalWidth), static_cast<int>(timing.logicalHeight),
                            static_cast<int>(timing.rotation), timing.geometryGeneration};
                        if (nextGeometry.valid() && nextGeometry != captureGeometry) {
                            // Change viewport before queuing this generation's first Image.
                            // surfaceChanged drops unsubmitted old pixels and clears old shapes.
                            renderer.surfaceChanged(nextGeometry.width, nextGeometry.height, 2.0f);
                            captureGeometry = nextGeometry;
                            std::lock_guard lock(geometryMutex);
                            pendingGeometry = nextGeometry;
                        }
                        if (captureBackend == "mirror" && timing.idle) {
                            renderer.projectionSourceAlive();
                            nextCapture = std::chrono::steady_clock::now();
                            continue;
                        }
                        if (buffer) {
                            const auto sequence = timing.sequence;
                            renderer.projectionHardwareBuffer(buffer,
                                [realtime, sequence] { realtime->releaseFrame(sequence); },
                                timing.sourceTimestampNs);
                            AHardwareBuffer_release(buffer);
                        } else {
                            renderer.projectionFrame(std::move(rgba), w, h);
                        }
                        const auto handedOff = std::chrono::steady_clock::now();
                        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(handedOff - begin).count();
                        captureTotalUs.fetch_add(elapsed);
                        captureUs += std::chrono::duration_cast<std::chrono::microseconds>(captured - begin).count();
                        handoffUs += std::chrono::duration_cast<std::chrono::microseconds>(handedOff - captured).count();
                        if (captureBackend != "screencap") readbackUs += timing.readbackUs;
                        captureFrames.fetch_add(1);
                        captureErased.store(captureBackend == "screencap" ? legacy.erasedFrameCount() : realtime->erasedFrameCount());
                        if (++timedFrames >= 60) {
                            logf(ANDROID_LOG_INFO, "[计时] %s %d帧: 采集+传输=%lldus CPU读回=%lldus 交接=%lldus",
                                 captureBackend.c_str(), timedFrames, captureUs / timedFrames,
                                 readbackUs / timedFrames, handoffUs / timedFrames);
                            captureUs = handoffUs = readbackUs = 0;
                            timedFrames = 0;
                        }
                    } else {
                        if (buffer) AHardwareBuffer_release(buffer);
                        captureErrors.fetch_add(1);
                        { std::lock_guard lock(captureErrorMutex); captureError = why; }
                        logf(ANDROID_LOG_ERROR, "背景采集失败: %s", why.c_str());
                        g_exitStatus.store(12);
                        g_running.store(false);
                        break; // bounded failure; no failing process-spawn loop
                    }
                }
                // Keep an absolute clock. Relative sleeps drift at 120/165 Hz;
                // skip missed slots rather than building a catch-up backlog.
                // A continuous mirror waits for damage in ImageReader. Pace only
                // screenshot requests, including an explicitly reported fallback.
                if (captureBackend == "mirror" && realtime->timing().mirrored) {
                    nextCapture = std::chrono::steady_clock::now();
                    continue;
                }
                nextCapture += interval;
                const auto finished = std::chrono::steady_clock::now();
                if (nextCapture < finished) {
                    const auto missed = (finished - nextCapture) / interval;
                    nextCapture += interval * (missed + 1);
                }
            }
        });
    }

    // ---- 主循环：region 跟随 + 输入法联动 ----
    std::vector<aurora::TouchRect> lastApplied;
    std::vector<aurora::VisibleUiShape> lastShapes;
    long long regionUpdates = 0;
    size_t visibleShapeCount = 0;
    int imeSession = 0;
    bool imeWanted = false;
    long long imeRetryAtMs = 0;          // 拉起失败后的下次重试时刻（不每帧重试）
    long long lastDump = 0;
    const long long startMs = nowMs();   // 主循环起点（--move-demo 的计时基准）
    bool moveDone = false;
    bool frameDumpRequested = false;
    long long lastCaptureCount = 0, lastCaptureMs = startMs;
    // 采样区像素/模糊目标像素的秒增量 —— 性能优化（留白）的判据。
    unsigned long long lastCapturedPixels = 0, lastGatheredPixels = 0, lastCapturedPanels = 0;
    unsigned long long lastGatherRuns = 0, lastPanelPaints = 0;
    unsigned long long lastFullScreenComposes = 0;
    bool fullScreenTakeoverLive = false;
    long long lastSampleMs = startMs;   // ★ 独立时间戳：lastCaptureMs 在本块之前已被刷新过

    // ★★★ 输入法桥接**预热**：窗口已经在屏幕上之后，把 Java 侧冷启动
    //   （写 APK / 建监听 / fork 桥接 / 等 pm install + am start-service + TCP 连回）
    //   在主循环里逐帧非阻塞地做掉，而不是等用户第一次点输入框时同步做。
    //   实测这段要 1.5~2.5 秒（research/ime_result.md 1475ms、
    //   research/ime_physical_run/README.md 2449ms），正是"第一次点击要等好几秒"的来源。
    //   ★ 预热全程**只建窗、不发 SHOW** ⇒ 不会在启动时主动弹键盘。
    if (wantIme) ime.warmUpBegin();

    // ---- --record：界面已经在跑，现在开录（等价于点主题窗口里的「开始录屏」）----
    // ★ 一定要在主循环**之前**起、在收尾里**停**：stop() 走 SIGINT，
    //   screenrecord 收到才会写完 moov —— 直接退出会得到一个打不开的 mp4。
    if (recordWanted) {
        aurora::RecordRequest req;
        req.outputPath = recordPath.empty()
            ? aurora::DefaultRecordPath(static_cast<long long>(::time(nullptr)))
            : recordPath;
        req.secondsLimit = recordSeconds;
        std::string why;
        if (aurora::recorder().start(req, &why)) {
            logf(ANDROID_LOG_INFO, "录屏已开始：%s（%s）", req.outputPath.c_str(),
                 recordSeconds > 0 ? "到点自动停" : "停止时收尾");
        } else {
            logf(ANDROID_LOG_ERROR, "录屏启动失败：%s", why.c_str());
            reportFailure("录屏启动失败", why.c_str());
        }
    }

    while (g_running.load()) {
        if (renderer.graphicsFailed()) {
            logf(ANDROID_LOG_ERROR, "渲染初始化/缓冲区尺寸更新失败，停止覆盖层并清理触摸区");
            g_exitStatus.store(14);
            g_running.store(false);
            break;
        }
        aurora::DisplayGeometry nextGeometry;
        {
            std::lock_guard lock(geometryMutex);
            nextGeometry = pendingGeometry;
        }
        if (nextGeometry != appliedGeometry) {
            layerW = nextGeometry.width;
            layerH = nextGeometry.height;
            if (!overlay.resize(layerW, layerH)) {
                logf(ANDROID_LOG_ERROR, "显示旋转更新失败: %s", overlay.error());
                reportFailure("显示旋转更新失败", overlay.error());
                g_exitStatus.store(13);
                g_running.store(false);
                break;
            }
            appliedGeometry = nextGeometry;
            lastApplied.clear();
            lastShapes.clear();
            logf(ANDROID_LOG_INFO, "[Geometry] logical=%dx%d rotation=%d generation=%u crop/input/viewport updated",
                 layerW, layerH, appliedGeometry.rotation, appliedGeometry.generation);
        }
        // Snapshot belongs to the completed render frame, in physical pixels.
        // Replacing the union removes old positions, gaps and transparent corners.
        const auto shapes = renderer.visibleTouchShapes(layerW, layerH);
        visibleShapeCount = shapes.size();
        const aurora::TouchRegion region = aurora::LiquidGlassRegion(); // diagnostics only
        const auto sameShape = [](const auto& a, const auto& b) {
            return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
                   a.radius == b.radius && a.shape == b.shape && a.roundedCorners == b.roundedCorners &&
                   a.clipLeft == b.clipLeft && a.clipTop == b.clipTop &&
                   a.clipRight == b.clipRight && a.clipBottom == b.clipBottom;
        };
        if (shapes.size() != lastShapes.size() || !std::equal(shapes.begin(), shapes.end(), lastShapes.begin(), sameShape)) {
            const auto exactRegion = aurora::BuildOverlayTouchRegion(shapes, layerW, layerH);
            if (exactRegion != lastApplied) {
            if (!overlay.updateTouchableRegionRects(exactRegion)) {
                logf(ANDROID_LOG_ERROR, "精确触摸区域提交失败，停止覆盖层: %s", overlay.error());
                reportFailure("精确触摸区域提交失败", overlay.error());
                g_exitStatus.store(13);
                g_running.store(false);
                break;
            }
            lastApplied = exactRegion;
            ++regionUpdates;
            }
            lastShapes = shapes;
        }

        if (!frameDumpPath.empty() && !frameDumpRequested && nowMs() - startMs > 3000) {
            renderer.requestFrameDump(frameDumpPath);
            frameDumpRequested = true;
        }

        // ⑤ 输入法：UI 想要文字输入时拉起
        //
        // ★★ 本轮的三个修复都落在这几行里（真机根因，见 docs/ANDROID_IME_BRIDGE.md）：
        //   ① 拉起是**上升沿**触发（`!imeWanted`），绝不每帧 show()、不轮询、不强拉；
        //   ② 键盘被收起（IME 的 HIDDEN 事件 / 系统自己收）⇒ 必须复位 imeWanted，
        //      并让输入框真的失焦，否则 `wants` 恒为 true、上升沿再也不来 ——
        //      表现就是"第一次能弹，之后怎么点都不弹"；
        //   ③ `--no-ime` 把请求吃掉时要**打一条日志**：以前它是完全静默的，
        //      从外面看和"输入法坏了"一模一样（交付的单文件包里默认就带这个参数）。
        const bool wants = renderer.wantsTextInput();
        static bool loggedWantWithoutIme = false;
        if (!wantIme && wants) {
            if (!loggedWantWithoutIme) {
                loggedWantWithoutIme = true;
                logf(ANDROID_LOG_WARN,
                     "UI 想要文字输入，但 --no-ime 生效 → 不拉起输入法（去掉该参数即可）");
            }
        } else if (wants) {
            loggedWantWithoutIme = false;
        }
        if (ime.consumeHiddenEvent()) {
            // 输入法报"已收起" ⇒ 状态回到"没在请求"，并请求 UI 侧清一次输入框焦点。
            imeWanted = false;
            renderer.requestTextInputEnd();
            logf(ANDROID_LOG_INFO, "键盘收起事件已处理：复位请求状态 + 释放输入框焦点");
        }
        if (wantIme && wants && !imeWanted) {
            // ★★ 只有**真的请求成功**才置 imeWanted。
            //   旧代码是先 `imeWanted = true;` 再 `ime.show()`，而 show() 失败是静默的
            //   ⇒ 一次失败就把"上升沿"永久吃掉：输入框不失焦则 `wants` 恒 true，
            //   `!imeWanted` 再也不成立 ⇒ 之后点多少次都不会再拉起键盘
            //   （用户报的"必须重复点击才能触发输入"）。
            if (ime.show(++imeSession)) {
                imeWanted = true;
                logf(ANDROID_LOG_INFO, "UI 请求文字输入 -> 拉起系统输入法（会话 %d）", imeSession);
            } else if (nowMs() >= imeRetryAtMs) {
                // 失败后**隔 1 秒再试**，既不每帧重试（那会每次都白等一次冷启动），
                // 也不无限重试 —— 下一次点击/下一帧满足条件时自然重来。
                imeRetryAtMs = nowMs() + 1000;
                logf(ANDROID_LOG_WARN, "拉起输入法失败：%s（1 秒后可重试）", ime.lastError());
            }
        } else if ((!wants && imeWanted) || (!wantIme && imeWanted)) {
            imeWanted = false;
            ime.hide();
            logf(ANDROID_LOG_INFO, "UI 不再需要输入 -> 收起键盘");
        }
        ime.pump();
        ime.warmUpPoll();          // ★ 非阻塞推进冷启动（预热完成后是空操作）
        if (ime.trace()) {
            logf(ANDROID_LOG_INFO, "[IME-TRACE] wants=%d imeWanted=%d wantIme=%d up=%d warming=%d",
                 wants ? 1 : 0, imeWanted ? 1 : 0, wantIme ? 1 : 0, ime.up() ? 1 : 0,
                 ime.warming() ? 1 : 0);
        }

        // ---- 用户在主题窗口上按了 × ⇒ 退出整个覆盖层 ----
        // ★ 走的是**正常收尾**（和 --max-seconds 到期同一条路）：停采集、收键盘、
        //   拆图层、删临时目录，退出码 0。不做硬退，也不留残留。
        //   这是用户明确要的"点 × 直接关闭程序"（他纠正过一次：不要"收起窗口"）。
        if (aurora::LiquidGlassQuitRequested()) {
            logf(ANDROID_LOG_INFO, "用户在主题窗口按了关闭 -> 退出覆盖层");
            g_running.store(false);
            break;
        }

        // ---- 需求④ 验证：把**图层**移动 (dx,dy)，看 region 是否自动跟随 ----
        //
        // 关键性质：`touchableRegion` 是**图层局部（input）空间**坐标，SF 每次快照都会用
        // 图层几何把它变换到 display 空间 ⇒ **移动图层时 region 自动跟随，不需要重设**。
        // 这条性质在 `research/touch_intercept_result.md:295-304` 真机验证过。
        //
        if ((moveDemoX != 0 || moveDemoY != 0) && !moveDone &&
            nowMs() - startMs > static_cast<long long>(moveDemoAtSeconds) * 1000) {
            moveDone = true;
            const int nx = moveDemoX;
            const int ny = moveDemoY;
            if (overlay.setPosition(nx, ny)) {
                logf(ANDROID_LOG_INFO,
                     "★ 图层移动到 (%d,%d)。region 是图层局部坐标 → 应由 SF 自动跟随，"
                     "无需重设；用 dumpsys 的 Input list 核对 toDisplayTransform。",
                     nx, ny);
            } else {
                logf(ANDROID_LOG_ERROR, "图层移动失败: %s", overlay.error());
            }
        }

        if (doDump && nowMs() - lastDump > 3000) {
            lastDump = nowMs();
            const long long avail = memAvailableKb();
            const long long currentCount = captureFrames.load();
            const long long currentMs = nowMs();
            const double actualFps = 1000.0 * (currentCount - lastCaptureCount) / std::max(1LL, currentMs - lastCaptureMs);
            lastCaptureCount = currentCount; lastCaptureMs = currentMs;
            logf(ANDROID_LOG_INFO, "背景更新 %.1ffps 后端=%s 错误=%lld 平均管线=%lldus",
                 actualFps, captureBackend.c_str(), captureErrors.load(),
                 currentCount ? captureTotalUs.load() / currentCount : 0);
            const auto projection = renderer.projectionDiagnostics();
            const auto perf = renderer.framePerfSnapshot();
            const auto panelPerf = aurora::panel_efficiency::snapshot();
            logf(ANDROID_LOG_INFO,
                 "[Work] gpuSubmit=%.1ffps idleSkipped=%llu lateLatched=%llu panelCombined=%llu resetsSaved=%llu cullCapture=%llu cullPaint=%llu cullShadow=%llu scene=%llu fboChecks=%llu fboFailures=%llu",
                 perf.submittedFramesPerSecond, static_cast<unsigned long long>(perf.idleSkippedFrames),
                 static_cast<unsigned long long>(perf.lateLatchedFrames),
                 static_cast<unsigned long long>(panelPerf.combinedCallbacks), static_cast<unsigned long long>(panelPerf.resetsSaved),
                 static_cast<unsigned long long>(panelPerf.culledCaptures), static_cast<unsigned long long>(panelPerf.culledPaints),
                 static_cast<unsigned long long>(panelPerf.culledShadows), static_cast<unsigned long long>(panelPerf.sceneComposes),
                 static_cast<unsigned long long>(panelPerf.framebufferChecks), static_cast<unsigned long long>(panelPerf.framebufferFailures));
            // 采样区成本：面板数 / 拷贝面积 / 模糊目标面积的秒增量。
            // 留白优化的效果直接看这三个数（越小越快），不必靠猜。
            {
                const unsigned long long capDelta = panelPerf.capturedPixels - lastCapturedPixels;
                const unsigned long long gatherDelta = panelPerf.gatheredPixels - lastGatheredPixels;
                const unsigned long long panelDelta = panelPerf.capturedPanels - lastCapturedPanels;
                const long long windowMs = std::max(1LL, nowMs() - lastSampleMs);
                lastSampleMs = nowMs();
                logf(ANDROID_LOG_INFO,
                     "[Sample] 面板=%llu 采样像素=%llu 模糊像素=%llu 每面板均值=%.0fpx 每秒采样=%.1fMpx 每秒模糊=%.1fMpx gather=%llu paint=%llu",
                     panelDelta, capDelta, gatherDelta,
                     panelDelta > 0 ? static_cast<double>(capDelta) / static_cast<double>(panelDelta) : 0.0,
                     static_cast<double>(capDelta) * 1000.0 / static_cast<double>(windowMs) / 1.0e6,
                     static_cast<double>(gatherDelta) * 1000.0 / static_cast<double>(windowMs) / 1.0e6,
                     static_cast<unsigned long long>(panelPerf.gatherRuns) - lastGatherRuns,
                     static_cast<unsigned long long>(panelPerf.panelPaints) - lastPanelPaints);
                lastGatherRuns = panelPerf.gatherRuns;
                lastPanelPaints = panelPerf.panelPaints;
                lastCapturedPixels = panelPerf.capturedPixels;
                lastGatheredPixels = panelPerf.gatheredPixels;
                lastCapturedPanels = panelPerf.capturedPanels;
            }
            // 低频重投：SF 的投票会被别的图层/显示策略盖掉（例如切前台应用时系统
            // 按新前台重算刷新率）。重投是幂等的，成本只是一次 binder 事务。
            if (vote.enabled) {
                overlay.applyFrameRateVote(vote.fps, vote.compatibility, vote.strategy,
                                           vote.priority);
            }
            // 全屏接管是否真的在每帧生效（全屏铺满次数）。它是"窗口内外时间线
            // 是否已经统一"的唯一硬判据：为 0 说明世界底层不可用、图层退回了
            // 透明覆盖层（那种情况下接缝依旧存在）。
            {
                aurora::GlesSceneCompositor* sc = aurora::ActiveGlesSceneCompositor();
                const unsigned long long full = sc ? sc->fullScreenCount() : 0ull;
                if (full != lastFullScreenComposes) {
                    lastFullScreenComposes = full;
                    fullScreenTakeoverLive = true;
                }
            }
            logf(ANDROID_LOG_INFO,
                 "[Perf] target=%.0f render=%.1f upload=%.1f present=%.1f cpuUi=%.3fms submit=%.3fms swap=%.3fms gpu=%.3fms cache=%llu/%llu dropped=%llu sourceValid=%d sourceImport=%.3fms sourceRender=%.3fms sourcePresent=%.3fms lag=%.2f takeover=%s",
                 perf.targetFramesPerSecond, perf.renderFramesPerSecond, perf.uploadedFramesPerSecond,
                 perf.presentedFramesPerSecond, perf.buildUiMilliseconds, perf.submitMilliseconds,
                 perf.swapMilliseconds, perf.gpuMilliseconds,
                 static_cast<unsigned long long>(perf.importCacheHits),
                 static_cast<unsigned long long>(perf.importCacheMisses),
                 static_cast<unsigned long long>(perf.replacedPendingFrames), perf.sourceTimestampValid ? 1 : 0,
                 perf.sourceToImportMilliseconds, perf.sourceToRenderMilliseconds,
                 perf.sourceToPresentMilliseconds, perf.sourceFrameLag,
                 fullScreenTakeoverLive ? "on" : "off");
            logf(ANDROID_LOG_INFO, "渲染帧=%llu 实际导入=%u 导入失败=%u 背景过期=%d",
                 static_cast<unsigned long long>(projection.renderedFrames),
                 projection.uploadedFrames, projection.uploadFailures, projection.sourceStale ? 1 : 0);
            logf(ANDROID_LOG_INFO,
                 "状态 触摸事件=%lld 采集帧=%lld 抹除帧=%lld 低内存跳过=%lld MemAvailable=%lldKB "
                 "ime=%d 界面=(%.0f,%.0f)-(%.0f,%.0f) 可见形状=%zu 区域条带=%zu 更新=%lld",
                 touchEvents.load(), captureFrames.load(), captureErased.load(),
                 captureSkippedLowMem.load(), avail, ime.up() ? 1 : 0, region.left, region.top,
                 region.right, region.bottom, visibleShapeCount, lastApplied.size(), regionUpdates);
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1000000 / renderFps));
    }

    // ---- 收尾（顺序重要：先收键盘再拆通道，最后才退出渲染） ----
    logf(ANDROID_LOG_INFO, "开始收尾");
    // ★ 先停录屏：SIGINT 让 screenrecord 把 moov 写完，之后才允许进程退出。
    if (aurora::recorder().running()) {
        std::string why;
        const bool ok = aurora::recorder().stop(&why);
        logf(ok ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "录屏已停止：%s（%.1f MB）",
             ok ? aurora::recorder().path().c_str() : why.c_str(),
             static_cast<double>(aurora::recorder().bytesWritten()) / (1024.0 * 1024.0));
    }
    ime.shutdown();
    g_running.store(false);
    if (touchThread.joinable()) touchThread.join();
    if (captureThread.joinable()) captureThread.join();

    renderer.surfaceDestroyed();
    rendererOwner.reset(); // join render thread; finish GPU reads and return Image leases
    realtime->stop();
    overlay.destroy();
    aospglue::shutdown();
    logf(ANDROID_LOG_INFO, "结束：触摸事件=%lld（%lld 个触点） 采集帧=%lld 低内存跳过=%lld%s%s",
         touchEvents.load(), touchPointers.load(), captureFrames.load(),
         captureSkippedLowMem.load(), captureError.empty() ? "" : " 采集错误=",
         captureError.c_str());
    return g_exitStatus.load();
}
