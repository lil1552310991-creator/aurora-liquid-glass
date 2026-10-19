#pragma once

// ===========================================================================
// 录屏开关（内置在 ELF 里）
// ===========================================================================
// 为什么是 fork/exec 系统自带的 screenrecord，而不是自己写编码器：
//
//   ★ 悬浮窗**录不到**的原因不是"没关 eSkipScreenshot"，而是两条采集路径的范围不同：
//     手机自带录屏（MediaProjection / 镜像）从 **显示根图层** 往下遍历子树，
//     而我们的图层建在全局 mRoot 下、是它的**兄弟节点** ⇒ 不在那棵树里。
//     我们自己的 mirror 采集后端是同一个盲区（见 research/zorder_acceptance/base_dump.png）。
//   ★ `screenrecord` 走的是 SurfaceFlinger 的**合成**路径（按 layerStack 抓），
//     和 `screencap` 同一条 ⇒ **拍得到悬浮窗**（真机抽帧验证过，不是推断）。
//
// 所以最省事也最可靠的做法：ELF 本来就是 root，直接起系统那个工具，
// 开关只负责「起 / 停 / 收尾时保证 mp4 写完整」。不自己实现编码器，
// 也就不需要把 MediaCodec/SF capture 那一整套搬进来。
//
// 本头文件里只有纯逻辑（参数拼装、默认文件名），**可在 PC 上离线单测**；
// 真正 fork/exec 的部分在 ScreenRecorder.cpp（Android 目标）。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace aurora {

struct RecordRequest {
    std::string outputPath;      // 目标 mp4（绝对路径）
    int secondsLimit = 0;        // 0 = 不限时（停止时靠 SIGINT 收尾）
    int bitRate = 12000000;      // 12 Mbps
    int width = 0;               // 0 = 用系统默认（屏幕原始分辨率）
    int height = 0;
};

// screenrecord 的参数表。★ 顺序有讲究：先选项后路径（最后一个参数必须是文件）。
inline std::vector<std::string> BuildScreenRecordArgs(const RecordRequest& r) {
    std::vector<std::string> args;
    args.emplace_back("--bit-rate");
    args.emplace_back(std::to_string(r.bitRate > 0 ? r.bitRate : 12000000));
    // --time-limit 0 在 screenrecord 里表示"不限"，直接不传更清楚。
    if (r.secondsLimit > 0) {
        args.emplace_back("--time-limit");
        args.emplace_back(std::to_string(r.secondsLimit));
    }
    if (r.width > 0 && r.height > 0) {
        args.emplace_back("--size");
        args.emplace_back(std::to_string(r.width) + "x" + std::to_string(r.height));
    }
    args.push_back(r.outputPath);
    return args;
}

// 默认落盘路径：/sdcard/Download/aurora_rec_YYYYmmdd_HHMMSS.mp4
// 用时间戳是为了**永不覆盖**上一次的录像（用户可能连续录几段）。
inline std::string DefaultRecordPath(long long unixSeconds, const char* dir = "/sdcard/Download") {
    // 只做日历换算，不依赖本地时区数据库：按 UTC 算，文件名只用于区分先后。
    const long long days = unixSeconds / 86400;
    long long rem = unixSeconds % 86400;
    if (rem < 0) rem += 86400;
    const int hh = static_cast<int>(rem / 3600);
    const int mm = static_cast<int>((rem % 3600) / 60);
    const int ss = static_cast<int>(rem % 60);
    // Howard Hinnant 的 civil_from_days
    long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long long doe = static_cast<unsigned long long>(z - era * 146097);
    const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long long mp = (5 * doy + 2) / 153;
    const unsigned long long d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned long long m = mp < 10 ? mp + 3 : mp - 9;
    const long long year = y + (m <= 2 ? 1 : 0);
    char buf[256];
    std::snprintf(buf, sizeof buf, "%s/aurora_rec_%04lld%02llu%02llu_%02d%02d%02d.mp4",
                  dir, year, m, d, hh, mm, ss);
    return std::string(buf);
}

// 录制器。**不是线程安全的**：只在渲染线程（UI 按钮）与主线程（收尾）用，
// 两边都在同一时刻最多一个动作；跨线程只读 running()/elapsedSeconds() 是可接受的。
class ScreenRecorder {
public:
    ~ScreenRecorder();

    // 起 screenrecord。失败时 why 里给原因（找不到工具 / fork 失败 / 路径不可写…）。
    bool start(const RecordRequest& request, std::string* why = nullptr);
    // 停止：先 SIGINT（screenrecord 收到会**写完 moov 再退出**），超时才 SIGTERM。
    // 这一步必须被调用（退出收尾里也要调），否则 mp4 没有索引、播放器打不开。
    bool stop(std::string* why = nullptr);

    bool running() const { return pid_ > 0; }
    int elapsedSeconds() const;
    const std::string& path() const { return path_; }
    const std::string& error() const { return error_; }
    int lastExitStatus() const { return lastStatus_; }
    long long bytesWritten() const;   // 文件大小（录制中也能看到增长）

private:
    int pid_ = -1;
    int lastStatus_ = -1;
    long long startedMs_ = 0;
    std::string path_;
    std::string error_;
};

// 全局单例：UI（LiquidGlassRuntime.cpp）与主流程（aurora_overlay.cpp）共用同一个。
ScreenRecorder& recorder();

}  // namespace aurora
