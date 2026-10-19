#pragma once

// 纯 ELF 的触摸防穿透（内核层）。
//
// 为什么要走内核层
// ----------------
// 既有的 APK 方案（docs/TOUCH_PASSTHROUGH.md）靠「全屏透明输入盾窗口 +
// FLAG_NOT_TOUCH_MODAL」在 Java 层拦触摸。纯 ELF 没有窗口系统可用，
// 也拿不到 InputFlinger 的私有接口。设备探测（docs/ROOT_ELF_DEVICE_PROBE.md）
// 给出三个关键事实：
//
//   1. EVIOCGRAB(1) 失败 EBUSY —— InputReader 已持有真实触摸屏，
//      纯 ELF 无法用 grab 抢独占。
//   2. /sys/class/input/eventN/device/inhibited 可写（0↔1）——
//      Android 输入子系统在每个输入设备上暴露的抑制开关。
//   3. root 可创建 uinput 虚拟触摸设备，系统可见。
//
// 由此得到本模块的做法：
//
//     echo 1 > inhibited          // 系统 InputReader 完全看不到真实触摸屏
//     open("/dev/input/eventN")   // 但自己仍能直接读原始事件
//     read -> 自行判断 -> 用 uinput 回灌需要放行的事件
//
// 为什么不会自我回读（这是这类方案最容易踩的坑）
// ----------------------------------------------
// 我们**只读真实触摸屏**、**只写虚拟设备**，两者是不同设备节点。
// 回灌的事件进的是 uinput 设备，不在我们的读取集合里，因此不存在
// 「读到自己的注入事件 -> 再次注入」的无限循环。
// 这是结构上的保证，不依赖任何标记位。
//
// 生命周期安全
// ------------
// inhibited=1 时触摸屏对整个系统失效。如果进程崩溃而没有复位，
// **用户的手机将无法响应触摸**。因此：
//   - 析构、信号处理、atexit 三处都会复位
//   - 复位采用「写 0 后回读确认」的方式，失败会大声报错
//   - 构造时先读出原值，复位时恢复原值而不是硬写 0

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#if defined(_WIN32)
#include <windows.h>   // GetTickCount64，供 nowSec() 使用
// MinGW 的 <fcntl.h> 没有这两个标志。本机自检只需要能编过，
// 真机（Android/glibc）上会走真实定义。
#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#endif

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace aurora {

// 一个触摸事件的原始帧：从 SYN_REPORT 到下一个 SYN_REPORT 之间的所有事件。
struct TouchFrame {
    static constexpr int kMaxEvents = 128;
    static constexpr int kMaxSlots = 16;
    input_event events[kMaxEvents];
    int count = 0;

    // 触点（MT-B 协议）。x/y 保留**原始值**（按 EVIOCGABS 的量程），
    // 命中判断请用归一化的 xNorm()/yNorm()——真实设备上量程与屏幕尺寸
    // 之比不是整数（实测 23040/1080 ≈ 21.33），除不得。
    struct Slot {
        int id = -1;
        int x = 0;
        int y = 0;
        bool down = false;
        float xNorm() const { return rangeX > 0 ? static_cast<float>(x) / rangeX : 0.0f; }
        float yNorm() const { return rangeY > 0 ? static_cast<float>(y) / rangeY : 0.0f; }
        int rangeX = 1;
        int rangeY = 1;
    };
    Slot slots[kMaxSlots];
    int slotCount = 0;

    bool empty() const { return count == 0; }

    // 矩形也用归一化坐标（0..1），这样与机型无关
    bool allInside(float x0, float y0, float x1, float y1) const {
        bool any = false;
        for (int i = 0; i < slotCount; ++i) {
            if (!slots[i].down) continue;
            any = true;
            const float sx = slots[i].xNorm();
            const float sy = slots[i].yNorm();
            if (sx < x0 || sx > x1 || sy < y0 || sy > y1) return false;
        }
        return any;
    }

    bool anyInside(float x0, float y0, float x1, float y1) const {
        for (int i = 0; i < slotCount; ++i) {
            if (!slots[i].down) continue;
            const float sx = slots[i].xNorm();
            const float sy = slots[i].yNorm();
            if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) return true;
        }
        return false;
    }

    // 触点中心的归一化坐标（多指时取平均），用于展示与调试
    bool centroid(float& nx, float& ny) const {
        float sx = 0.0f, sy = 0.0f;
        int n = 0;
        for (int i = 0; i < slotCount; ++i) {
            if (!slots[i].down) continue;
            sx += slots[i].xNorm();
            sy += slots[i].yNorm();
            ++n;
        }
        if (n == 0) return false;
        nx = sx / static_cast<float>(n);
        ny = sy / static_cast<float>(n);
        return true;
    }
};

class InputShield final {
public:
    InputShield() = default;
    ~InputShield() { close(); }

    InputShield(const InputShield&) = delete;
    InputShield& operator=(const InputShield&) = delete;

    // ---- 配置 ----

    // 触摸屏事件节点与坐标缩放。
    // 设备实测：ABS_MT_POSITION_X 0..23040 / Y 0..50688，屏幕 1080x2376，
    // 即 16 倍缩放（23040/1080 = 21.33，50688/2376 = 21.33 —— 见下）
    //
    // 注意：这里不硬编码缩放，而是打开设备后用 EVIOCGABS 读真实量程，
    // 再按屏幕尺寸算出缩放，避免换机型就错。
    void setDevice(const char* eventPath) {
        devicePath_ = eventPath ? eventPath : "";
    }
    void setScreenSize(int w, int h) {
        screenW_ = w;
        screenH_ = h;
    }
    // 回灌设备名（会出现在 /proc/bus/input/devices 里，便于真机排查）
    void setVirtualName(const char* name) {
        virtualName_ = name ? name : "aurora-touch";
    }

    // 抑制的最长持续时间（秒）。**这是一道安全阀，不要关掉。**
    //
    // 起因：2026-10-13 一次真机实验把抑制持续了 12 秒，用户正好在那时想
    // 滑动屏幕，结果整个触摸屏无响应 —— 在用户看来就是"手机划不动了"。
    // 抑制是设备级的：置位期间触摸屏对整个系统失效，而且实测**连本进程
    // 自己也读不到事件**（那次测试 12 秒收到 0 帧）。
    //
    // 所以抑制窗口必须尽可能短，并且必须有一个不依赖调用方自觉的兜底。
    // 超时后 pollFrame() 会自行复位并在下一帧返回负值，调用方据此收尾。
    void setMaxHoldSeconds(double s) {
        maxHoldSec_ = (s > 0.0) ? s : 0.0;   // <=0 表示不限制（不建议）
    }
    bool expired() const { return expired_; }
    double heldSeconds() const {
        return inhibited_ ? (nowSec() - openedAtSec_) : 0.0;
    }

    // ---- 状态 ----
    bool opened() const { return fd_ >= 0; }
    bool inhibited() const { return inhibited_; }
    int rangeX() const { return rangeX_; }
    int rangeY() const { return rangeY_; }
    // 用于诊断：量程与屏幕尺寸之比（真实设备上不是整数，不要用它做坐标换算）
    int scaleHint() const { return scaleX_; }
    const char* lastError() const { return error_; }

    // ---- 生命周期 ----

    static double nowSec() {
        // 刻意不用 std::chrono：在 MinGW 上引入 <chrono> 会连带实例化
        // libstdc++ 的 basic_string / leap_second 迭代器模板，而该工具链在
        // 这些模板上是坏的（本项目已多次踩到），会让本机自检编不过。
        // 两端各用一个最小的单调时钟即可。
#if defined(_WIN32)
        return (double)GetTickCount64() * 1e-3;
#else
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
    }

    // 安全阀检查。返回 true 表示已因超时而复位（调用方应立即收尾）。
    //
    // 每次 pollFrame() 都会调用它。这样即使调用方忘了计时、主循环卡住
    // 或上层逻辑写错，抑制也不会超过 maxHoldSec_ —— 用户的手机不会
    // 无限期失去触摸。
    bool checkDeadline() {
        if (!inhibited_ || maxHoldSec_ <= 0.0) return false;
        if (nowSec() - openedAtSec_ < maxHoldSec_) return false;
        std::fprintf(stderr,
                     "[InputShield] 已达最长抑制时间 %.1f 秒，自动复位触摸屏\n",
                     maxHoldSec_);
        close();            // 走带「写后回读确认」的复位路径
        expired_ = true;
        return true;
    }

    // 打开真实触摸屏并抑制系统输入。
    // 返回 false 时不会留下任何副作用（已做的都会回滚）。
    bool open() {
        if (fd_ >= 0) return true;
        if (devicePath_.empty()) {
            fail("未设置设备路径");
            return false;
        }

        // 1. 先读量程，算出坐标缩放
        fd_ = ::open(devicePath_.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            fail(std::string("open ") + devicePath_ + " 失败: " + std::strerror(errno));
            return false;
        }
        if (!readAbsRanges()) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        // 2. 抑制系统输入（这一步之后，触摸屏对系统不可见）
        inhibitPath_ = deriveInhibitPath(devicePath_);
        if (inhibitPath_.empty()) {
            fail("无法从设备路径推导 inhibited 路径");
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        if (!readInhibit(originalInhibit_)) {
            // 读不到就按 0 处理，但不阻断：某些 ROM 该节点只写不可读
            originalInhibit_ = 0;
        }
        if (!writeInhibit(1)) {
            fail(std::string("写 ") + inhibitPath_ + " 失败: " + std::strerror(errno));
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        inhibited_ = true;
        openedAtSec_ = nowSec();
        expired_ = false;
        if (maxHoldSec_ > 0.0) {
            std::fprintf(stderr,
                         "[InputShield] 已抑制触摸屏，最长 %.1f 秒后自动复位\n",
                         maxHoldSec_);
        }

        // 3. 创建回灌用的虚拟触摸设备
        if (!createVirtualDevice()) {
            // 回灌不可用 -> 放行能力失效 -> 必须立刻复位抑制
            writeInhibit(originalInhibit_);
            inhibited_ = false;
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        return true;
    }

    // 复位抑制并关闭。幂等，可重复调用。
    void close() {
        if (vfd_ >= 0) {
            ::close(vfd_);
            vfd_ = -1;
        }
        if (inhibited_ && !inhibitPath_.empty()) {
            if (!writeInhibit(originalInhibit_)) {
                // 这是最严重的失败：用户的触摸屏仍处于失效状态
                std::fprintf(stderr,
                             "[InputShield] 严重: 无法复位 %s，触摸屏可能仍不可用！"
                             "请手动执行: echo 0 > %s\n",
                             inhibitPath_.c_str(), inhibitPath_.c_str());
            }
            inhibited_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    // ---- 事件循环 ----

    // 读一帧触摸事件。返回：
    //   >0  读到一帧，写入 *out
    //    0  暂无数据
    //   <0  出错
    int pollFrame(TouchFrame* out, int timeoutMs = 0) {
        // 安全阀优先于一切：超时了就复位，并让调用方知道该收尾了。
        if (checkDeadline()) {
            if (out) { out->count = 0; out->slotCount = 0; }
            return -2;      // -2 = 超时复位（区别于 -1 的读取错误）
        }
        if (fd_ < 0 || !out) return -1;
        out->count = 0;
        out->slotCount = 0;

        // 先把内核缓冲里已有的读完，组帧到 SYN_REPORT
        for (;;) {
            pollfd pfd{fd_, POLLIN, 0};
            const int pr = ::poll(&pfd, 1, out->count == 0 ? timeoutMs : 0);
            if (pr < 0) {
                if (errno == EINTR) continue;
                return -1;
            }
            if (pr == 0) return out->count > 0 ? 1 : 0;
            if (!(pfd.revents & POLLIN)) return out->count > 0 ? 1 : 0;

            input_event ev{};
            const ssize_t n = ::read(fd_, &ev, sizeof(ev));
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return out->count > 0 ? 1 : 0;
                if (errno == EINTR) continue;
                return -1;
            }
            if (n != static_cast<ssize_t>(sizeof(ev))) continue;

            if (out->count < TouchFrame::kMaxEvents) {
                out->events[out->count++] = ev;
            }
            if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
                parseSlots(out);
                return 1;
            }
        }
    }

    // 把一帧原样回灌给系统（放行）。
    bool inject(const TouchFrame& f) {
        if (vfd_ < 0 || f.count == 0) return false;
        size_t total = sizeof(input_event) * static_cast<size_t>(f.count);
        const uint8_t* p = reinterpret_cast<const uint8_t*>(f.events);
        while (total > 0) {
            const ssize_t n = ::write(vfd_, p, total);
            if (n < 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += n;
            total -= static_cast<size_t>(n);
        }
        return true;
    }

private:
    void fail(const std::string& msg) {
        std::snprintf(error_, sizeof(error_), "%s", msg.c_str());
        std::fprintf(stderr, "[InputShield] %s\n", error_);
    }

    // 从 /dev/input/eventN 推导 /sys/class/input/eventN/device/inhibited
    static std::string deriveInhibitPath(const std::string& dev) {
        const size_t slash = dev.find_last_of('/');
        if (slash == std::string::npos) return {};
        const std::string node = dev.substr(slash + 1);   // event7
        if (node.rfind("event", 0) != 0) return {};
        return "/sys/class/input/" + node + "/device/inhibited";
    }

    bool readInhibit(int& value) {
        const int f = ::open(inhibitPath_.c_str(), O_RDONLY | O_CLOEXEC);
        if (f < 0) return false;
        char buf[16] = {0};
        const ssize_t n = ::read(f, buf, sizeof(buf) - 1);
        ::close(f);
        if (n <= 0) return false;
        value = (buf[0] == '1') ? 1 : 0;
        return true;
    }

    bool writeInhibit(int value) {
        const int f = ::open(inhibitPath_.c_str(), O_WRONLY | O_CLOEXEC);
        if (f < 0) return false;
        const char c = value ? '1' : '0';
        const ssize_t n = ::write(f, &c, 1);
        ::close(f);
        if (n != 1) return false;
        // 回读确认：写成功不等于生效
        int back = -1;
        if (readInhibit(back)) return back == value;
        return true;   // 节点不可读时只能信任写入结果
    }

    // 用 EVIOCGABS 读量程，按屏幕尺寸算缩放
    bool readAbsRanges() {
        auto absMax = [&](int code, int fallback) -> int {
            input_absinfo info{};
            if (::ioctl(fd_, EVIOCGABS(code), &info) < 0) return fallback;
            return info.maximum > 0 ? info.maximum : fallback;
        };
        rangeX_ = absMax(ABS_MT_POSITION_X, screenW_);
        rangeY_ = absMax(ABS_MT_POSITION_Y, screenH_);
        // 这个比值只用于打印诊断；命中判断一律走归一化坐标，不做整数除
        scaleX_ = rangeX_ / (screenW_ > 0 ? screenW_ : 1);
        return true;
    }

    bool createVirtualDevice() {
        vfd_ = ::open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (vfd_ < 0) {
            fail(std::string("open /dev/uinput 失败: ") + std::strerror(errno));
            return false;
        }

        // 声明为直接输入设备（触摸屏）
        ::ioctl(vfd_, UI_SET_EVBIT, EV_SYN);
        ::ioctl(vfd_, UI_SET_EVBIT, EV_KEY);
        ::ioctl(vfd_, UI_SET_EVBIT, EV_ABS);
        ::ioctl(vfd_, UI_SET_KEYBIT, BTN_TOUCH);
        ::ioctl(vfd_, UI_SET_PROPBIT, INPUT_PROP_DIRECT);
        ::ioctl(vfd_, UI_SET_ABSBIT, ABS_MT_SLOT);
        ::ioctl(vfd_, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
        ::ioctl(vfd_, UI_SET_ABSBIT, ABS_MT_POSITION_X);
        ::ioctl(vfd_, UI_SET_ABSBIT, ABS_MT_POSITION_Y);

        uinput_user_dev dev{};
        std::snprintf(dev.name, UINPUT_MAX_NAME_SIZE, "%s", virtualName_.c_str());
        dev.id.bustype = BUS_VIRTUAL;
        dev.id.vendor = 0x4155;    // 'AU'
        dev.id.product = 0x0001;
        dev.id.version = 1;

        // 用真实设备量程，保证回灌坐标语义一致
        input_absinfo info{};
        int maxX = rangeX_, maxY = rangeY_;
        if (::ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_X), &info) == 0 && info.maximum > 0) {
            maxX = info.maximum;
        }
        if (::ioctl(fd_, EVIOCGABS(ABS_MT_POSITION_Y), &info) == 0 && info.maximum > 0) {
            maxY = info.maximum;
        }
        dev.absmin[ABS_MT_POSITION_X] = 0;
        dev.absmax[ABS_MT_POSITION_X] = maxX;
        dev.absmin[ABS_MT_POSITION_Y] = 0;
        dev.absmax[ABS_MT_POSITION_Y] = maxY;
        dev.absmin[ABS_MT_SLOT] = 0;
        dev.absmax[ABS_MT_SLOT] = 9;
        // tracking id 用 -1 表示抬起
        dev.absmin[ABS_MT_TRACKING_ID] = -1;
        dev.absmax[ABS_MT_TRACKING_ID] = 65535;

        if (::write(vfd_, &dev, sizeof(dev)) != static_cast<ssize_t>(sizeof(dev))) {
            fail("写 uinput_user_dev 失败");
            ::close(vfd_);
            vfd_ = -1;
            return false;
        }
        if (::ioctl(vfd_, UI_DEV_CREATE) < 0) {
            fail(std::string("UI_DEV_CREATE 失败: ") + std::strerror(errno));
            ::close(vfd_);
            vfd_ = -1;
            return false;
        }
        return true;
    }

    // 从帧里解析 MT-B 触点
    void parseSlots(TouchFrame* f) const {
        int slot = 0;
        TouchFrame::Slot local[TouchFrame::kMaxSlots];
        for (int i = 0; i < TouchFrame::kMaxSlots; ++i) {
            local[i].down = false;
            local[i].rangeX = rangeX_;
            local[i].rangeY = rangeY_;
        }
        for (int i = 0; i < f->count; ++i) {
            const input_event& e = f->events[i];
            if (e.type != EV_ABS) continue;
            if (e.code == ABS_MT_SLOT) {
                slot = e.value;
                if (slot < 0 || slot >= TouchFrame::kMaxSlots) slot = 0;
            } else if (e.code == ABS_MT_TRACKING_ID) {
                local[slot].id = e.value;
                local[slot].down = (e.value >= 0);
            } else if (e.code == ABS_MT_POSITION_X) {
                local[slot].x = e.value;
            } else if (e.code == ABS_MT_POSITION_Y) {
                local[slot].y = e.value;
            }
        }
        int n = 0;
        for (int i = 0; i < TouchFrame::kMaxSlots; ++i) {
            if (local[i].down) f->slots[n++] = local[i];
        }
        f->slotCount = n;
    }

    std::string devicePath_;
    std::string inhibitPath_;
    std::string virtualName_ = "aurora-touch";
    int screenW_ = 1080;
    int screenH_ = 2376;
    int fd_ = -1;
    int vfd_ = -1;
    int rangeX_ = 1;
    int rangeY_ = 1;
    int scaleX_ = 1;
    int originalInhibit_ = 0;
    bool inhibited_ = false;
    // 安全阀：默认 20 秒。超时后 pollFrame() 自行复位并返回负值。
    double maxHoldSec_ = 20.0;
    double openedAtSec_ = 0.0;
    bool expired_ = false;
    char error_[256]{};
};

// 进程退出/收到信号时的兜底复位。
//
// inhibited=1 而没有复位会让用户的手机无法响应触摸，所以除了析构之外，
// 还必须有信号与 atexit 两条兜底路径（崩溃或被 kill 时析构不会跑）。
class InputShieldGuard final {
public:
    static void Arm(InputShield* shield) {
        instance() = shield;
        static bool hooked = false;
        if (hooked) return;
        hooked = true;
        std::atexit(&InputShieldGuard::OnExit);
        // 用 std::signal 而不是 sigaction：前者在所有目标上都可用，
        // 后者的 struct sigaction 在部分工具链（如 MinGW）里不完整。
        // 本进程只需要「收到信号就复位」这一个行为，不需要精细控制。
        //
        // 信号清单按可用性裁剪：MinGW 没有 SIGHUP / SIGQUIT，
        // 强行引用会编译失败，而本机自检需要能编译这个头文件。
        const int signals[] = {
            SIGINT,
            SIGTERM,
#ifdef SIGHUP
            SIGHUP,
#endif
#ifdef SIGQUIT
            SIGQUIT,
#endif
            SIGSEGV,
            SIGABRT,
#ifdef SIGBUS
            SIGBUS,
#endif
        };
        for (int sig : signals) {
            std::signal(sig, &InputShieldGuard::OnSignal);
        }
    }

private:
    static InputShield*& instance() {
        static InputShield* p = nullptr;
        return p;
    }
    static void OnExit() {
        if (instance()) instance()->close();
    }
    static void OnSignal(int sig) {
        if (instance()) instance()->close();
        // 复位后按默认行为重新抛出，保留原有的退出码语义
        ::signal(sig, SIG_DFL);
        ::raise(sig);
    }
};

}  // namespace aurora
