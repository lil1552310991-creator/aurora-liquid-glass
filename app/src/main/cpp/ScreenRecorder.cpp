// 录屏开关的实现（POSIX）。设计理由见 ScreenRecorder.h 顶部。
//
// ★ 只做三件事：起 screenrecord、停它（SIGINT 收尾）、报告状态。
//   编码、封装、mux 全交给系统那个工具 —— 我们不重复实现它，
//   也就不会引入 MediaCodec/SF capture 那一整套。

#include "ScreenRecorder.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace aurora {
namespace {

constexpr const char* kScreenRecord = "/system/bin/screenrecord";

long long nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// 等 pid 退出，最多 waitMs 毫秒。返回 true = 已经退出。
bool waitForExit(int pid, int waitMs, int* status) {
    const long long deadline = nowMs() + waitMs;
    for (;;) {
        int st = 0;
        const pid_t r = ::waitpid(pid, &st, WNOHANG);
        if (r == pid) {
            if (status) *status = st;
            return true;
        }
        if (r < 0 && errno != EINTR) {
            if (status) *status = -1;
            return true;   // 已经被回收/不存在，当作已退出
        }
        if (nowMs() >= deadline) return false;
        ::usleep(50 * 1000);
    }
}

// ★ 只允许异步信号安全的调用：子进程里除了 dup2/execl/_exit 什么都不做。
//   （父进程是多线程的，fork 之后碰任何锁都可能死锁 —— 这条必须守住。）
[[noreturn]] void execScreenRecord(const std::vector<std::string>& args) {
    // 别让 screenrecord 的输出混进我们的 stdout/stderr（默认静默是硬要求）。
    const int devnull = ::open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        ::dup2(devnull, STDOUT_FILENO);
        ::dup2(devnull, STDERR_FILENO);
        if (devnull > STDERR_FILENO) ::close(devnull);
    }
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>("screenrecord"));
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    ::execv(kScreenRecord, argv.data());
    _exit(127);
}

}  // namespace

ScreenRecorder::~ScreenRecorder() {
    // 析构时还活着就收尾 —— 否则 mp4 没有 moov，白录。
    if (pid_ > 0) stop();
}

ScreenRecorder& recorder() {
    static ScreenRecorder instance;
    return instance;
}

bool ScreenRecorder::start(const RecordRequest& request, std::string* why) {
    const auto fail = [&](const char* msg) {
        error_ = msg;
        if (why) *why = msg;
        return false;
    };
    if (pid_ > 0) {
        error_ = "已经在录了";
        if (why) *why = error_;
        return false;
    }
    if (request.outputPath.empty()) return fail("录屏路径为空");
    if (::access(kScreenRecord, X_OK) != 0) {
        error_ = std::string("找不到可执行的 ") + kScreenRecord;
        if (why) *why = error_;
        return false;
    }

    RecordRequest req = request;
    // 安全上限：即使没人来停，也不会无限录下去（0 = 不限时 ⇒ 给 1 小时兜底）。
    if (req.secondsLimit <= 0) req.secondsLimit = 3600;
    const std::vector<std::string> args = BuildScreenRecordArgs(req);

    const pid_t pid = ::fork();
    if (pid < 0) {
        error_ = std::string("fork 失败: ") + std::strerror(errno);
        if (why) *why = error_;
        return false;
    }
    if (pid == 0) execScreenRecord(args);   // 不返回

    pid_ = static_cast<int>(pid);
    path_ = req.outputPath;
    startedMs_ = nowMs();
    lastStatus_ = -1;
    error_.clear();
    return true;
}

bool ScreenRecorder::stop(std::string* why) {
    if (pid_ <= 0) return true;
    const int pid = pid_;

    // ★ SIGINT 是 screenrecord 的"正常停止"信号：它会写完索引再退出。
    //   用 SIGKILL 会得到一个没有 moov 的 mp4（播放器打不开），所以顺序不能反。
    ::kill(pid, SIGINT);
    int status = 0;
    if (!waitForExit(pid, 5000, &status)) {
        ::kill(pid, SIGTERM);
        if (!waitForExit(pid, 2000, &status)) {
            // 只能放弃了：**不 SIGKILL**（宁可留个坏文件，也不要留个跑不停的录屏进程）。
            error_ = "screenrecord 没有响应 SIGINT/SIGTERM，未确认收尾";
            if (why) *why = error_;
            pid_ = -1;
            return false;
        }
    }
    lastStatus_ = status;
    pid_ = -1;
    return true;
}

int ScreenRecorder::elapsedSeconds() const {
    if (startedMs_ == 0) return 0;
    return static_cast<int>((nowMs() - startedMs_) / 1000);
}

long long ScreenRecorder::bytesWritten() const {
    if (path_.empty()) return 0;
    struct stat st{};
    if (::stat(path_.c_str(), &st) != 0) return 0;
    return static_cast<long long>(st.st_size);
}

}  // namespace aurora
