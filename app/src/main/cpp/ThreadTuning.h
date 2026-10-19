#pragma once

// ===========================================================================
//  调度策略（场景自适应，两级：通用 → 设备画像）
//
//  ★ 设计前提（来自用户的实际使用场景）：
//    这个覆盖层不是手机唯一在跑的东西 —— 用户会在**游戏**里开它、会**刷视频**时
//    开它、也会让它在桌面上挂着。所以"把所有资源都给自己"是**错的**：
//      · 骁龙 8 Elite 只有 2 个 prime 核（cpu6-7），相机/游戏/系统 UI 同样在抢；
//        把渲染线程**硬绑**上去，等于把调度器的手绑起来，谁都没好处。
//      · 固定要高性能档（uclamp 80% + nice -16）在视频/静止场景是纯浪费电量。
//
//  ⇒ 策略：**默认不绑核**，让 EAS 自己决定；按场景动态升降优先级；
//     检测到"别人也在要高刷/系统已经繁忙"时主动让路。
//
//  两条铁律：
//    ① 绑核必须是**显式选择**（AURORA_SCHED_PIN=1），不是默认行为；
//    ② 任何时刻都不能把自己抬到"饿死别人"的程度（不用 SCHED_FIFO 做默认）。
//
//  ★ 纯逻辑部分（SummarizeTopology / BusyPercentDelta / ComputeProfile）零系统调用，
//    可离线回归 —— 它们才是"策略是否正确"的真正判据。
// ===========================================================================

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined(__ANDROID__) || defined(__linux__)
#include <dirent.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#define AURORA_TUNING_POSIX 1
#else
#define AURORA_TUNING_POSIX 0
#endif

namespace aurora::tuning {

// ---------------------------------------------------------------------------
//  纯逻辑 · 一：把"每核最大频率"归纳成簇结构
// ---------------------------------------------------------------------------
struct CpuCluster {
    int firstCore = 0;
    int coreCount = 0;
    long maxKhz = 0;
};

struct CpuTopology {
    std::vector<long> coreMaxKhz;
    std::vector<CpuCluster> clusters;
    int primeCluster = -1;
    long maxKhz = 0;
    long minKhz = 0;

    bool valid() const { return !clusters.empty(); }
    int coreCount() const {
        int n = 0;
        for (const CpuCluster& c : clusters) n += c.coreCount;
        return n;
    }
    int primeFirstCore() const {
        return primeCluster >= 0 ? clusters[primeCluster].firstCore : -1;
    }
    int primeCoreCount() const {
        return primeCluster >= 0 ? clusters[primeCluster].coreCount : 0;
    }
    std::vector<int> primeCores() const {
        std::vector<int> out;
        if (primeCluster < 0) return out;
        const CpuCluster& c = clusters[primeCluster];
        for (int i = 0; i < c.coreCount; ++i) out.push_back(c.firstCore + i);
        return out;
    }
};

inline CpuTopology SummarizeTopology(const std::vector<long>& coreMaxKhz) {
    CpuTopology t;
    t.coreMaxKhz = coreMaxKhz;
    for (long f : coreMaxKhz) {
        if (f <= 0) continue;
        if (t.minKhz == 0 || f < t.minKhz) t.minKhz = f;
        if (f > t.maxKhz) t.maxKhz = f;
    }
    for (std::size_t i = 0; i < coreMaxKhz.size(); ++i) {
        const long f = coreMaxKhz[i];
        if (f <= 0) continue;
        if (!t.clusters.empty() && t.clusters.back().maxKhz == f &&
            t.clusters.back().firstCore + t.clusters.back().coreCount == static_cast<int>(i)) {
            t.clusters.back().coreCount++;
        } else {
            CpuCluster c;
            c.firstCore = static_cast<int>(i);
            c.coreCount = 1;
            c.maxKhz = f;
            t.clusters.push_back(c);
        }
    }
    for (std::size_t i = 0; i < t.clusters.size(); ++i) {
        if (t.primeCluster < 0 || t.clusters[i].maxKhz > t.clusters[t.primeCluster].maxKhz) {
            t.primeCluster = static_cast<int>(i);
        }
    }
    return t;
}

// ---------------------------------------------------------------------------
//  纯逻辑 · 二：系统繁忙度（从两次 /proc/stat 采样算差值）
// ---------------------------------------------------------------------------
struct CpuTimes {
    unsigned long long idle = 0;
    unsigned long long total = 0;
    bool valid() const { return total > 0; }
};

// 返回 0..100。跨 CPU 热插拔/计数器回绕时返回 -1（调用方保留上一次的值）。
inline int BusyPercentDelta(const CpuTimes& a, const CpuTimes& b) {
    if (!a.valid() || !b.valid()) return -1;
    if (b.total <= a.total || b.idle < a.idle) return -1;
    const unsigned long long dTotal = b.total - a.total;
    const unsigned long long dIdle = b.idle - a.idle;
    if (dIdle > dTotal) return -1;
    const unsigned long long busy = dTotal - dIdle;
    return static_cast<int>(busy * 100ull / dTotal);
}

// ---------------------------------------------------------------------------
//  纯逻辑 · 三：场景 → 调度档位
// ---------------------------------------------------------------------------
struct SceneContext {
    bool glassActive = false;     // 玻璃面板当前可见 / 在画
    bool interacting = false;     // 最近 ~300ms 内有触摸、动画或背景在动
    float targetFps = 120.0f;     // 我们的目标帧率
    int systemBusyPercent = 0;    // 全系统 CPU 占用（0..100，-1=未知）
    // 还有别人在要高刷（前台很可能是游戏/相机这类同样吃大核的东西）。
    bool otherHighRefreshClient = false;
};

struct TuningProfile {
    int nice = 0;                 // 相对本进程默认（0=不争）
    int uclampPercent = 0;        // 0=不提性能档
    bool pinToPrime = false;      // ★ 默认 false：不跟系统调度器抢大核
    const char* reason = "";
};

// 让路优先于争抢：只要系统繁忙或别人也要高刷，我们就退回中性档。
inline TuningProfile ComputeProfile(const SceneContext& c) {
    TuningProfile p;

    if (!c.glassActive) {
        p.nice = 0;
        p.uclampPercent = 0;
        p.reason = "玻璃未显示：完全让路";
        return p;
    }
    if (c.otherHighRefreshClient) {
        p.nice = 0;
        p.uclampPercent = 0;
        p.reason = "其它高刷前台在场（多半是游戏/相机）：让路";
        return p;
    }
    if (c.systemBusyPercent >= 70) {
        p.nice = 0;
        p.uclampPercent = 0;
        p.reason = "系统繁忙：让路";
        return p;
    }
    // 系统不忙、玻璃也在画 —— 这时争一点点是安全的（空闲的核不用白不用）。
    const bool highRefresh = c.targetFps >= 100.0f;
    if (!c.interacting) {
        p.nice = -4;
        p.uclampPercent = 20;
        p.reason = "玻璃可见但静止：轻量优先";
        return p;
    }
    if (highRefresh && c.systemBusyPercent < 40) {
        p.nice = -10;
        p.uclampPercent = 50;
        p.reason = "交互中 + 高刷 + 系统空闲：适度优先";
        return p;
    }
    p.nice = -6;
    p.uclampPercent = 30;
    p.reason = "交互中：适度优先";
    return p;
}

#if AURORA_TUNING_POSIX

// ---------------------------------------------------------------------------
//  采集
// ---------------------------------------------------------------------------
inline std::vector<long> ReadCoreMaxKhz() {
    std::vector<long> out;
    for (int core = 0; core < 32; ++core) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", core);
        FILE* f = std::fopen(path, "re");
        if (f == nullptr) break;
        long khz = 0;
        if (std::fscanf(f, "%ld", &khz) != 1) khz = 0;
        std::fclose(f);
        out.push_back(khz);
    }
    return out;
}

inline std::string ReadSocName() {
    const char* candidates[] = {
        "/sys/firmware/devicetree/base/model",
        "/sys/devices/soc0/machine",
    };
    for (const char* path : candidates) {
        FILE* f = std::fopen(path, "re");
        if (f == nullptr) continue;
        char buf[256] = {};
        const std::size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
        std::fclose(f);
        if (n == 0) continue;
        std::size_t len = n;
        while (len > 0 && (buf[len - 1] == '\0' || buf[len - 1] == '\n' ||
                           buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
            --len;
        }
        if (len == 0) continue;
        return std::string(buf, len);
    }
    return {};
}

// /proc/stat 第一行的 cpu 汇总：user nice system idle iowait irq softirq steal ...
inline CpuTimes ReadCpuTimes() {
    CpuTimes t;
    FILE* f = std::fopen("/proc/stat", "re");
    if (f == nullptr) return t;
    char line[512];
    if (std::fgets(line, sizeof(line), f) != nullptr && std::strncmp(line, "cpu ", 4) == 0) {
        unsigned long long v[10] = {};
        const int n = std::sscanf(line + 4, "%llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                                  &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7],
                                  &v[8], &v[9]);
        for (int i = 0; i < n; ++i) t.total += v[i];
        t.idle = v[3] + v[4];   // idle + iowait
    }
    std::fclose(f);
    return t;
}

// ---------------------------------------------------------------------------
//  系统调用
// ---------------------------------------------------------------------------
inline void NameCurrentThread(const char* name) {
    if (name != nullptr) pthread_setname_np(pthread_self(), name);
}

inline bool RaiseThreadPriority(int nice) {
    return ::setpriority(PRIO_PROCESS, 0, nice) == 0;
}

inline bool SetUclampViaSyscall(int utilMin, int utilMax) {
    struct SchedAttr {
        std::uint32_t size;
        std::uint32_t sched_policy;
        std::uint64_t sched_flags;
        std::int32_t sched_nice;
        std::uint32_t sched_priority;
        std::uint64_t sched_runtime;
        std::uint64_t sched_deadline;
        std::uint64_t sched_period;
        std::uint32_t sched_util_min;
        std::uint32_t sched_util_max;
    };
    constexpr std::uint64_t kKeepPolicy = 0x08;
    constexpr std::uint64_t kKeepParams = 0x10;
    constexpr std::uint64_t kClampMin = 0x20;
    constexpr std::uint64_t kClampMax = 0x40;
    SchedAttr attr{};
    attr.size = sizeof(SchedAttr);
    attr.sched_flags = kKeepPolicy | kKeepParams | kClampMin | kClampMax;
    attr.sched_util_min = static_cast<std::uint32_t>(utilMin < 0 ? 0 : utilMin);
    attr.sched_util_max = static_cast<std::uint32_t>(utilMax > 1024 ? 1024 : utilMax);
    return ::syscall(SYS_sched_setattr, 0, &attr, 0u) == 0;
}

// 真机上 /proc/<tid>/uclamp.min 是**不可写**的，所以 syscall 那条才是实际路径。
inline bool RaiseUclampMin(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    const int value = percent * 1024 / 100;
    const long tid = static_cast<long>(::syscall(SYS_gettid));
    char path[128];
    std::snprintf(path, sizeof(path), "/proc/self/task/%ld/uclamp.min", tid);
    if (FILE* f = std::fopen(path, "we"); f != nullptr) {
        const int written = std::fprintf(f, "%d", value);
        std::fclose(f);
        if (written > 0) return true;
    }
    return SetUclampViaSyscall(value, 1024);
}

// 放开亲和性：让 EAS 自己决定把线程放哪。恢复默认时用这个。
inline bool AllowAllCores() {
    cpu_set_t set;
    CPU_ZERO(&set);
    const int n = static_cast<int>(::sysconf(_SC_NPROCESSORS_ONLN));
    for (int i = 0; i < n && i < CPU_SETSIZE; ++i) CPU_SET(i, &set);
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
}

inline bool BindCurrentThread(const std::vector<int>& cores) {
    if (cores.empty()) return false;
    cpu_set_t set;
    CPU_ZERO(&set);
    for (int c : cores) {
        if (c >= 0 && c < CPU_SETSIZE) CPU_SET(c, &set);
    }
    return ::sched_setaffinity(0, sizeof(set), &set) == 0;
}

// ---------------------------------------------------------------------------
//  高层：初始化 + 周期性按场景调整
// ---------------------------------------------------------------------------
struct TuningResult {
    std::string soc;
    std::string topology;
    std::string actions;
    std::string skipped;
    CpuTopology topo;
};

// 只做"一次性"的事：命名 + 把优先级**归零**（从干净状态开始，之后交给场景策略）。
// ★ 不绑核、不提 uclamp —— 那两件事必须由场景决定（见 ComputeProfile）。
inline TuningResult Initialize(const char* threadName) {
    TuningResult r;
    r.soc = ReadSocName();
    r.topo = SummarizeTopology(ReadCoreMaxKhz());
    if (r.topo.valid()) {
        char buf[256];
        int off = 0;
        for (const CpuCluster& c : r.topo.clusters) {
            off += std::snprintf(buf + off, sizeof(buf) - static_cast<std::size_t>(off),
                                 "%s%dx%ld", off ? "+" : "", c.coreCount, c.maxKhz);
            if (off >= static_cast<int>(sizeof(buf)) - 32) break;
        }
        r.topology = buf;
    }
    NameCurrentThread(threadName);
    r.actions = "name";
    AllowAllCores();               // 显式放开，避免继承到受限的亲和性掩码
    r.actions += ",affinity=all";
    RaiseThreadPriority(0);        // 起点中性
    r.actions += ",nice0";
    RaiseUclampMin(0);
    r.actions += ",uclamp0";
    return r;
}

struct ProfileDecision {
    TuningProfile profile;
    bool changed = false;
    int appliedNice = 0;
};

// 幂等：每秒调一次即可。只在档位真的变化时才动系统调用（零稳态开销）。
inline ProfileDecision ApplySceneProfile(const SceneContext& ctx, TuningProfile* current,
                                         bool allowPin) {
    ProfileDecision d;
    d.profile = ComputeProfile(ctx);
    if (!allowPin) d.profile.pinToPrime = false;

    if (current != nullptr &&
        current->nice == d.profile.nice &&
        current->uclampPercent == d.profile.uclampPercent &&
        current->pinToPrime == d.profile.pinToPrime) {
        d.profile.reason = current->reason;
        return d;   // 没变化：一次系统调用都不发
    }

    d.changed = true;
    RaiseThreadPriority(d.profile.nice);
    d.appliedNice = d.profile.nice;
    RaiseUclampMin(d.profile.uclampPercent);
    if (d.profile.pinToPrime && ctx.glassActive) {
        const CpuTopology topo = SummarizeTopology(ReadCoreMaxKhz());
        if (topo.valid() && topo.clusters.size() > 1) BindCurrentThread(topo.primeCores());
    } else {
        AllowAllCores();   // ★ 默认路径：把核还给调度器
    }
    if (current != nullptr) *current = d.profile;
    return d;
}

#endif  // AURORA_TUNING_POSIX

}  // namespace aurora::tuning
