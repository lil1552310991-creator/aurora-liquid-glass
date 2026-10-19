#pragma once

// 纯 ELF 拉起系统输入法。
//
// 背景
// ----
// 纯 ELF 没有 Activity、没有 Window、没有 InputConnection，因此**无法直接
// 请求系统输入法**。能够拉起输入法的唯一东西是「一个获得窗口焦点的 Android
// 窗口」。
//
// 做法（参考 ssyclr/AndroidSurfaceImgui-Enhanced-Input-method）
// -------------------------------------------------------------
// 把一个小辅助 APK 内嵌进 ELF，运行时释放并安装，然后借助它的
// **透明 1×1 悬浮窗**取得输入法焦点：
//
//   1. 释放 APK 到 /data/adb/aurora-ime-XXXXXX/（目录 0700、APK 0400）
//      每次用新目录，避免覆盖正在被 ART 映射的代码
//   2. root app_process 加载 APK 里的 Main 类
//   3. Main 把 APK 安装成临时应用，再启动里面的 AuroraImeService
//   4. Service 创建 1×1 透明窗；待命时不抢焦点，需要输入时请求焦点并
//      showSoftInput
//   5. 本类与 Service 之间走 127.0.0.1 TCP，令牌每次运行随机
//
// 为什么是 1×1 而不是全屏：全屏透明窗会挡住下方应用的所有触摸。
//
// 与输入盾的关系
// --------------
// 本模块只负责「拉起输入法并同步文本」。触摸归属由 InputShield 决定：
// 面板内的触摸被吞掉（因此不会点到下方应用），键盘区域的触摸交给 Android。
// 两者配合才构成完整的防穿透 + 可输入。

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace aurora {

// 由 build-embedded-apk.ps1 生成
#include "AuroraImeApk.h"

/** 输入会话的快照。偏移一律是 UTF-8 字节偏移（C++ 侧口径）。 */
struct ImeState {
    int session = 0;
    std::string text;
    int start = 0;
    int end = 0;
    bool keyboardVisible = false;
};

/** 输入法弹层在屏幕上的矩形（物理像素；坐标原点 = 屏幕左上角）。 */
struct ImeInsetRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    bool valid() const { return right > left && bottom > top; }
};

/** IME 事件回调。事件在 poll() 里同步派发。 */
class ImeCallbacks {
public:
    virtual ~ImeCallbacks() = default;
    /** Service 回传了完整编辑结果（**整段文本**，不是增量）。 */
    virtual void onEdit(int session, const std::string& text, int start, int end) {
        (void)session; (void)text; (void)start; (void)end;
    }
    /** 键盘可见性变化。 */
    virtual void onKeyboard(int session, bool visible) {
        (void)session; (void)visible;
    }
    /** 用户主动收起键盘。 */
    virtual void onHidden(int session) { (void)session; }
    /** ★ 输入法弹层的几何（来自 Java 侧的 WindowInsets.Type.ime()）。
     *  宿主用它把键盘那一块**从自己的触摸拦截区里挖掉**，并做"窗口避让"，
     *  否则键盘会被我们这层玻璃压住、按键也点不动。 */
    virtual void onImeInsets(int session, const ImeInsetRect& rect) {
        (void)session; (void)rect;
    }
    /** 剪贴板读取结果。 */
    virtual void onClipboard(const std::string& text) { (void)text; }
};

class InputBridge final {
public:
    InputBridge() = default;
    ~InputBridge() { close(); }

    InputBridge(const InputBridge&) = delete;
    InputBridge& operator=(const InputBridge&) = delete;

    void setCallbacks(ImeCallbacks* cb) { callbacks_ = cb; }
    void setTempRoot(const char* dir) { tempRoot_ = dir ? dir : "/data/adb"; }

    /**
     * 让 1×1 输入窗建在指定显示上（0 = 默认显示/物理屏）。
     *
     * 必须与渲染覆盖层所在的显示一致，否则 IMMS 的
     * `hasInputMethodClientFocus` 会因 displayId 不匹配而拒绝绑定
     * （WMS:8223-8245）。测试虚拟屏是 20。
     */
    void setDisplay(int display) { display_ = display; }
    const char* lastError() const { return error_; }
    bool ready() const { return sock_ >= 0; }
    const std::string& tempDir() const { return tempDir_; }

    /**
     * 释放 APK、引导 Java 桥接、建立连接。
     *
     * 返回 false 时不会留下运行中的子进程（已做的都会回收）。
     *
     * ★★ 这个同步版本现在只用于"必须立刻拿到桥接"的少数场合；
     *    正常路径请用 beginOpen() + openPoll()（见下），因为**只有第 4 步慢**：
     *      extractApk/startServer/spawnBridge  合计通常 < 100ms（只是写文件 + 建监听 + fork）
     *      acceptWithToken                     1.5~2.5s（等 Java 侧 pm install + am start-service + 连回）
     *    把后面那 1.5~2.5s 塞进"用户第一次点输入框"的调用栈里，正是用户报的
     *    "点一下要等好几秒"的根因。
     */
    bool open() {
        if (sock_ >= 0) return true;
        if (!beginOpen()) return false;
        // 同步等待（保留给确实需要阻塞的调用者）
        for (;;) {
            const int r = openPoll();
            if (r > 0) return true;
            if (r < 0) return false;
            ::usleep(2000);
        }
    }

    /**
     * 冷启动的**前半段**：只做快的那三步（写 APK / 建监听 / fork 桥接进程）。
     * 不等待 Java 侧连回来，因此通常在 100ms 内返回。
     *
     * 返回 false 表示连前半段都失败（此时已经清理干净）。
     */
    bool beginOpen() {
        if (sock_ >= 0) return true;
        if (opening_) return true;             // 已经在路上
        if (!extractApk()) return false;
        if (!startServer()) { cleanupTemp(); return false; }
        if (!spawnBridge()) { closeServer(); cleanupTemp(); return false; }
        opening_ = true;
        openStartMs_ = monoMs();
        return true;
    }

    /**
     * 推进冷启动的**后半段**（等 Java 连回 + 等 READY）。**完全非阻塞**，
     * 适合放在主循环里每帧调一次。
     *
     *   返回  1 = 已就绪（sock_ 可用、gotReady_ 已置位）
     *   返回  0 = 还在进行中（继续每帧调用）
     *   返回 -1 = 失败（已清理；调用者可以稍后重试 beginOpen）
     */
    int openPoll() {
        if (sock_ >= 0 && gotReady_) return 1;
        if (!opening_) return -1;

        if (sock_ < 0) {
            // —— 阶段 A：等 Java 侧 TCP 连回来 ——
            pollfd pfd{listenFd_, POLLIN, 0};
            const int pr = ::poll(&pfd, 1, 0);          // ★ 0 = 不阻塞
            if (pr < 0) {
                finishOpenFail(std::string("poll 失败: ") + std::strerror(errno));
                return -1;
            }
            if (pr == 0) {
                if (monoMs() - openStartMs_ > kOpenTimeoutMs) {
                    finishOpenFail("等待桥接连接超时（30s）");
                    return -1;
                }
                return 0;                                // 继续等
            }
            sock_ = ::accept(listenFd_, nullptr, nullptr);
            if (sock_ < 0) {
                finishOpenFail(std::string("accept 失败: ") + std::strerror(errno));
                return -1;
            }
            int one = 1;
            ::setsockopt(sock_, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
            if (!sendLine("HELLO " + token_)) {
                finishOpenFail("发送 HELLO 失败");
                return -1;
            }
        }

        // —— 阶段 B：等 READY ——
        //   ★ 一次 pump(0) 处理当前可读到的全部命令；READY 与紧随其后的消息
        //     可能不在同一次可读窗口里到达，所以必须允许跨多次调用累积。
        {
            const int r = pump(0);                       // ★ 非阻塞
            if (r < 0) {
                finishOpenFail("桥接连接在 READY 之前断开");
                return -1;
            }
        }
        if (gotReady_) {
            opening_ = false;
            return 1;
        }
        if (monoMs() - openStartMs_ > kOpenTimeoutMs) {
            finishOpenFail("桥接未返回 READY");
            return -1;
        }
        return 0;
    }

    bool opening() const { return opening_; }

    /** 关闭连接、终止子进程、删除临时目录。幂等。 */
    void close() {
        if (sock_ >= 0) {
            sendLine("QUIT");
            ::close(sock_);
            sock_ = -1;
        }
        closeServer();
        shutdownChild();
        cleanupTemp();
        opening_ = false;
        gotReady_ = false;
    }

    // ---- 协议 ----

    /**
     * 只同步文本与选区（会创建 1×1 窗），不请求键盘。
     *
     * 与 show() 分开是刻意的：真机排查时需要在「窗口已建好但键盘未弹出」
     * 的状态下先核对窗口落在哪个显示，再决定要不要真的拉键盘。
     */
    bool sync(const ImeState& st) {
        if (sock_ < 0) return false;
        char head[128];
        std::snprintf(head, sizeof head, "SYNC %d %d %d 0 %d ",
                      st.session, st.start, st.end,
                      static_cast<int>(st.text.size()) + 64);
        if (!sendRaw(head)) return false;
        if (!sendRaw(base64Encode(st.text))) return false;
        return sendRaw("\n");
    }

    /** 请求显示键盘（需先 sync）。 */
    bool showKeyboard(int session) {
        char showBuf[64];
        std::snprintf(showBuf, sizeof showBuf, "SHOW %d\n", session);
        return sendRaw(showBuf);
    }

    /** 同步当前文本与选区，然后显示键盘。 */
    bool show(const ImeState& st) {
        if (!sync(st)) return false;
        return showKeyboard(st.session);
    }

    bool hide() { return sendLine("HIDE"); }

    bool requestClipboard() { return sendLine("GETCLIP"); }

    bool setClipboard(const std::string& text) {
        return sendLine("SETCLIP " + base64Encode(text));
    }

    /**
     * 处理一次 IO。timeoutMs 为 0 表示非阻塞。
     * 返回处理的命令条数，-1 表示连接已断开。
     */
    int pump(int timeoutMs = 0) {
        if (sock_ < 0) return -1;
        pollfd pfd{sock_, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr < 0) return (errno == EINTR) ? 0 : -1;
        if (pr == 0) return 0;

        int handled = 0;
        char buf[4096];
        for (;;) {
            const ssize_t n = ::recv(sock_, buf, sizeof(buf) - 1, MSG_DONTWAIT);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                if (errno == EINTR) continue;
                return -1;
            }
            if (n == 0) return -1;   // 对端关闭
            buf[n] = '\0';
            rxBuf_.append(buf, static_cast<size_t>(n));
            size_t nl;
            while ((nl = rxBuf_.find('\n')) != std::string::npos) {
                const std::string line = rxBuf_.substr(0, nl);
                rxBuf_.erase(0, nl + 1);
                if (!line.empty()) {
                    handleLine(line);
                    ++handled;
                }
            }
        }
        return handled;
    }

private:
    // ---- 释放内嵌 APK ----

    static std::string randomSuffix() {
        // 每次运行用新目录，避免覆盖正在被 ART 映射的 APK
        unsigned char rnd[6] = {0};
        const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            ssize_t got = ::read(fd, rnd, sizeof rnd);
            (void)got;
            ::close(fd);
        } else {
            const unsigned seed = static_cast<unsigned>(::getpid() ^ ::time(nullptr));
            for (int i = 0; i < 6; ++i) rnd[i] = static_cast<unsigned char>((seed >> (i * 4)) & 0xFF);
        }
        char out[16];
        std::snprintf(out, sizeof out, "%02x%02x%02x%02x%02x%02x",
                      rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5]);
        return out;
    }

    bool extractApk() {
        tempDir_ = tempRoot_ + "/aurora-ime-" + randomSuffix();
        if (::mkdir(tempDir_.c_str(), 0700) != 0) {
            fail(std::string("mkdir ") + tempDir_ + " 失败: " + std::strerror(errno));
            return false;
        }
        apkPath_ = tempDir_ + "/aurora_ime.apk";

        const int fd = ::open(apkPath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0400);
        if (fd < 0) {
            fail(std::string("创建 ") + apkPath_ + " 失败: " + std::strerror(errno));
            return false;
        }
        const unsigned char* p = kAuroraImeApk;
        size_t left = kAuroraImeApkSize;
        while (left > 0) {
            const ssize_t n = ::write(fd, p, left);
            if (n <= 0) {
                if (errno == EINTR) continue;
                fail("写 APK 失败");
                ::close(fd);
                return false;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        ::close(fd);
        return true;
    }

    void cleanupTemp() {
        if (!apkPath_.empty()) {
            ::unlink(apkPath_.c_str());
            apkPath_.clear();
        }
        if (!tempDir_.empty()) {
            ::rmdir(tempDir_.c_str());
            tempDir_.clear();
        }
    }

    // ---- 回环 TCP ----

    bool startServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listenFd_ < 0) {
            fail(std::string("socket 失败: ") + std::strerror(errno));
            return false;
        }
        int one = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = ::htonl(INADDR_LOOPBACK);   // 只监听回环
        addr.sin_port = 0;                                  // 让内核选端口
        if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            fail(std::string("bind 失败: ") + std::strerror(errno));
            return false;
        }
        if (::listen(listenFd_, 1) != 0) {
            fail(std::string("listen 失败: ") + std::strerror(errno));
            return false;
        }
        socklen_t len = sizeof addr;
        if (::getsockname(listenFd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
            fail("getsockname 失败");
            return false;
        }
        port_ = ::ntohs(addr.sin_port);
        token_ = randomSuffix() + randomSuffix();
        return true;
    }

    void closeServer() {
        if (listenFd_ >= 0) {
            ::close(listenFd_);
            listenFd_ = -1;
        }
    }

    // ★ 原来的同步版 acceptWithToken()（内部 poll 30000 + 150×pump(200)）已被
    //   beginOpen()/openPoll() 取代并删除：它会把 1.5~2.5s 的 Java 冷启动
    //   同步压进"用户第一次点输入框"的调用栈（用户报的"点一下等几秒"）。

    // ---- app_process 引导 ----

    bool spawnBridge() {
        char portBuf[16];
        std::snprintf(portBuf, sizeof portBuf, "%d", port_);
        char displayBuf[16];
        std::snprintf(displayBuf, sizeof displayBuf, "%d", display_);

        // app_process 的调用约定：app_process [JVM 选项] <父目录> <主类> [参数...]
        // 主类从 -Djava.class.path 指定的 APK 里加载。
        const std::string cpOpt = "-Djava.class.path=" + apkPath_;
        const char* argv[] = {
            "/system/bin/app_process",
            cpOpt.c_str(),
            "/system/bin",
            "com.aurora.imebridge.Main",
            "--port", portBuf,
            "--token", token_.c_str(),
            "--apk", apkPath_.c_str(),
            "--display", displayBuf,
            nullptr,
        };

        // 给子进程一根 stdin 管道：我们持有写端，close() 时关掉它，
        // Java 侧读到 EOF 就会执行 pm uninstall 再退出。
        //
        // 必须带 O_CLOEXEC：否则子进程自己也会继承写端的副本，父进程关掉自己那份
        // 之后管道仍然「有写者」，EOF 永远不到，pm uninstall 就永远不跑。
        int lifePipe[2] = {-1, -1};
        if (::pipe2(lifePipe, O_CLOEXEC) != 0) {
            fail(std::string("pipe 失败: ") + std::strerror(errno));
            return false;
        }

        const int devNull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_adddup2(&fa, lifePipe[0], STDIN_FILENO);
        if (devNull >= 0) {
            posix_spawn_file_actions_adddup2(&fa, devNull, STDOUT_FILENO);
            // stderr 保留：Java 侧把日志写到 stderr，便于排查
        }
        const int rc = ::posix_spawn(&child_, "/system/bin/app_process", &fa, nullptr,
                                     const_cast<char**>(argv), environ);
        posix_spawn_file_actions_destroy(&fa);
        if (devNull >= 0) ::close(devNull);
        ::close(lifePipe[0]);
        if (rc != 0) {
            ::close(lifePipe[1]);
            fail(std::string("posix_spawn app_process 失败: ") + std::strerror(rc));
            return false;
        }
        childStdin_ = lifePipe[1];
        return true;
    }

    /** 等子进程自己退出（它要先跑完 pm uninstall），超时返回 false。 */
    bool reapBridge(int timeoutMs) {
        if (child_ <= 0) return true;
        const int steps = timeoutMs / 50;
        for (int i = 0; i < steps; ++i) {
            int status = 0;
            const pid_t r = ::waitpid(child_, &status, WNOHANG);
            if (r == child_ || (r < 0 && errno == ECHILD)) {
                child_ = -1;
                return true;
            }
            ::usleep(50000);
        }
        return false;
    }

    /**
     * 优雅地送走 Java 桥接：关掉它的 stdin（EOF 触发 pm uninstall），
     * 等它自己退出；超时才 SIGTERM。成功与失败路径都必须走这里，
     * 否则辅助包会残留在设备上。
     */
    void shutdownChild() {
        if (childStdin_ >= 0) {
            ::close(childStdin_);
            childStdin_ = -1;
        }
        reapBridge(15000);
        killBridge();
    }

    void killBridge() {
        if (child_ > 0) {
            ::kill(child_, SIGTERM);
            int status = 0;
            ::waitpid(child_, &status, 0);
            child_ = -1;
        }
        if (childStdin_ >= 0) {
            ::close(childStdin_);
            childStdin_ = -1;
        }
    }

    // ---- 协议行处理 ----

    void handleLine(const std::string& line) {
        if (line == "READY") {
            gotReady_ = true;
            return;
        }
        // 用空格切出前几个字段，剩下的原样保留（文本是 Base64，不含空格）
        auto field = [&](int idx, std::string& out) -> bool {
            size_t pos = 0;
            for (int i = 0; i < idx; ++i) {
                pos = line.find(' ', pos);
                if (pos == std::string::npos) return false;
                ++pos;
            }
            size_t end = line.find(' ', pos);
            out = line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            return true;
        };

        std::string cmd;
        if (!field(0, cmd)) return;

        if (cmd == "EDIT") {
            std::string s, a, b, txt;
            if (!field(1, s) || !field(2, a) || !field(3, b)) return;
            const size_t p = line.find(' ', line.find(' ', line.find(' ', line.find(' ') + 1) + 1) + 1);
            txt = (p == std::string::npos) ? "" : line.substr(p + 1);
            if (callbacks_) {
                callbacks_->onEdit(std::atoi(s.c_str()), base64Decode(txt),
                                   std::atoi(a.c_str()), std::atoi(b.c_str()));
            }
        } else if (cmd == "IME") {
            std::string s, vis;
            if (!field(1, s) || !field(2, vis)) return;
            if (callbacks_) callbacks_->onKeyboard(std::atoi(s.c_str()), vis == "1");
            // ★ 键盘几何（可选字段：老版本 Java 只发 3 个 0，新版本发 4 个真实像素）。
            //   缺字段 / 全 0 ⇒ 当作"未知"，宿主退回到"不挖洞"的老行为 ——
            //   这样新旧两侧可以各自升级，不会因为一边没更新就崩或者行为倒退。
            std::string l, t, r, b;
            ImeInsetRect rect;
            if (field(3, l) && field(4, t) && field(5, r) && field(6, b)) {
                rect.left = std::atoi(l.c_str());
                rect.top = std::atoi(t.c_str());
                rect.right = std::atoi(r.c_str());
                rect.bottom = std::atoi(b.c_str());
            }
            if (callbacks_) callbacks_->onImeInsets(std::atoi(s.c_str()), rect);
        } else if (cmd == "HIDDEN") {
            std::string s;
            if (!field(1, s)) return;
            if (callbacks_) callbacks_->onHidden(std::atoi(s.c_str()));
        } else if (cmd == "CLIP") {
            const size_t p = line.find(' ', line.find(' ') + 1);
            if (callbacks_) {
                callbacks_->onClipboard(p == std::string::npos ? "" : base64Decode(line.substr(p + 1)));
            }
        } else {
            // 其余是诊断类消息（WINDOW / SHOWREQUEST / SHOWVISIBLE / SHOWFAIL ...）。
            // 真机排查时它们是最直接的证据，原样打到 stderr。
            std::fprintf(stderr, "[InputBridge] %s\n", line.c_str());
        }
    }

    // ---- 工具 ----

    bool sendRaw(const char* s) {
        if (sock_ < 0) return false;
        size_t left = std::strlen(s);
        const char* p = s;
        while (left > 0) {
            const ssize_t n = ::send(sock_, p, left, MSG_NOSIGNAL);
            if (n <= 0) {
                if (errno == EINTR) continue;
                return false;
            }
            p += n;
            left -= static_cast<size_t>(n);
        }
        return true;
    }
    bool sendRaw(const std::string& s) { return sendRaw(s.c_str()); }
    bool sendLine(const std::string& s) { return sendRaw(s + "\n"); }

    static std::string base64Encode(const std::string& in) {
        static const char* kTbl =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((in.size() + 2) / 3) * 4);
        size_t i = 0;
        while (i + 2 < in.size()) {
            const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                               (static_cast<unsigned char>(in[i + 1]) << 8) |
                               static_cast<unsigned char>(in[i + 2]);
            out += kTbl[(v >> 18) & 63];
            out += kTbl[(v >> 12) & 63];
            out += kTbl[(v >> 6) & 63];
            out += kTbl[v & 63];
            i += 3;
        }
        if (i + 1 == in.size()) {
            const unsigned v = static_cast<unsigned char>(in[i]) << 16;
            out += kTbl[(v >> 18) & 63];
            out += kTbl[(v >> 12) & 63];
            out += '=';
            out += '=';
        } else if (i + 2 == in.size()) {
            const unsigned v = (static_cast<unsigned char>(in[i]) << 16) |
                               (static_cast<unsigned char>(in[i + 1]) << 8);
            out += kTbl[(v >> 18) & 63];
            out += kTbl[(v >> 12) & 63];
            out += kTbl[(v >> 6) & 63];
            out += '=';
        }
        return out;
    }

    static std::string base64Decode(const std::string& in) {
        auto val = [](char c) -> int {
            if (c >= 'A' && c <= 'Z') return c - 'A';
            if (c >= 'a' && c <= 'z') return c - 'a' + 26;
            if (c >= '0' && c <= '9') return c - '0' + 52;
            if (c == '+') return 62;
            if (c == '/') return 63;
            return -1;
        };
        std::string out;
        int buf = 0, bits = 0;
        for (char c : in) {
            if (c == '=') break;
            const int v = val(c);
            if (v < 0) continue;
            buf = (buf << 6) | v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                out += static_cast<char>((buf >> bits) & 0xFF);
            }
        }
        return out;
    }

    void fail(const std::string& msg) {
        std::snprintf(error_, sizeof error_, "%s", msg.c_str());
        std::fprintf(stderr, "[InputBridge] %s\n", error_);
    }

    /** 单调时钟毫秒（只用于"等了多久"，与墙上时间无关）。 */
    static long long monoMs() {
        timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
    }

    /** openPoll 的失败收尾：**必须把已经做的东西全部回收**（否则 sock_/子进程会泄漏，
     *  ready() 还会假 true），然后允许调用者稍后重试 beginOpen()。 */
    void finishOpenFail(const std::string& why) {
        fail(why);
        if (sock_ >= 0) { ::close(sock_); sock_ = -1; }
        closeServer();
        shutdownChild();
        cleanupTemp();
        opening_ = false;
        gotReady_ = false;
    }

    std::string tempRoot_ = "/data/adb";
    std::string tempDir_;
    std::string apkPath_;
    std::string token_;
    std::string rxBuf_;
    int listenFd_ = -1;
    int sock_ = -1;
    int port_ = 0;
    int display_ = 0;
    pid_t child_ = -1;
    int childStdin_ = -1;
    bool gotReady_ = false;
    bool opening_ = false;          // 冷启动后半段进行中（beginOpen .. openPoll 成功）
    long long openStartMs_ = 0;     // 后半段起点，用于 30s 超时
    static constexpr long long kOpenTimeoutMs = 30000;
    ImeCallbacks* callbacks_ = nullptr;
    char error_[256]{};
};

}  // namespace aurora
