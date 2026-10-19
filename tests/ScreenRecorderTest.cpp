// ScreenRecorder 的纯逻辑回归：参数拼装 + 默认文件名。
// 真正的 fork/exec 在设备上验（PC 上没有 screenrecord，也没意义）。
#include "ScreenRecorder.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurora;

static bool HasPair(const std::vector<std::string>& a, const char* k, const char* v) {
    for (size_t i = 0; i + 1 < a.size(); ++i)
        if (a[i] == k && a[i + 1] == v) return true;
    return false;
}

int main() {
    // 1) 基本形态：先选项、路径在最后（screenrecord 要求最后一个参数是文件）
    {
        RecordRequest r;
        r.outputPath = "/sdcard/Download/a.mp4";
        r.secondsLimit = 120;
        const auto a = BuildScreenRecordArgs(r);
        assert(!a.empty());
        assert(a.back() == "/sdcard/Download/a.mp4");        // 路径必须在最后
        assert(HasPair(a, "--bit-rate", "12000000"));        // 默认 12 Mbps
        assert(HasPair(a, "--time-limit", "120"));
        assert(a.size() == 5);
    }
    // 2) 不限时（0）→ 不传 --time-limit（screenrecord 里 0 表示不限，传了反而绕）
    {
        RecordRequest r;
        r.outputPath = "/data/local/tmp/b.mp4";
        r.secondsLimit = 0;
        const auto a = BuildScreenRecordArgs(r);
        for (const auto& s : a) assert(s != "--time-limit");
        assert(a.back() == "/data/local/tmp/b.mp4");
    }
    // 3) 指定分辨率 + 自定义码率
    {
        RecordRequest r;
        r.outputPath = "/sdcard/Download/c.mp4";
        r.bitRate = 6000000;
        r.width = 720;
        r.height = 1600;
        const auto a = BuildScreenRecordArgs(r);
        assert(HasPair(a, "--bit-rate", "6000000"));
        assert(HasPair(a, "--size", "720x1600"));
        assert(a.back() == "/sdcard/Download/c.mp4");
    }
    // 4) 只给宽或只给高 ⇒ 不传 --size（避免生成 "720x0" 这种非法参数）
    {
        RecordRequest r;
        r.outputPath = "/sdcard/Download/d.mp4";
        r.width = 720;
        r.height = 0;
        const auto a = BuildScreenRecordArgs(r);
        for (const auto& s : a) assert(s != "--size");
    }
    // 5) 非法码率（<=0）回落到 12 Mbps，绝不把 0 传给编码器
    {
        RecordRequest r;
        r.outputPath = "/sdcard/Download/e.mp4";
        r.bitRate = 0;
        const auto a = BuildScreenRecordArgs(r);
        assert(HasPair(a, "--bit-rate", "12000000"));
    }
    // 6) 默认路径：目录 + 前缀 + 扩展名；同一秒内两次调用结果一致（可复现）
    {
        const long long t = 1760000000LL;   // 2025-10-09 08:53:20 UTC
        const std::string p = DefaultRecordPath(t);
        assert(p.rfind("/sdcard/Download/aurora_rec_", 0) == 0);
        assert(p.size() > 40);
        assert(p.compare(p.size() - 4, 4, ".mp4") == 0);
        assert(p == DefaultRecordPath(t));
        // 时间戳不同 ⇒ 文件名不同（永不覆盖上一次录像）
        assert(p != DefaultRecordPath(t + 1));
        // 自定义目录也要生效
        assert(DefaultRecordPath(t, "/data/local/tmp").rfind("/data/local/tmp/", 0) == 0);
    }
    // 7) 时间换算抽样：整点、跨日、闰年边界都不能错位到别的天
    {
        const std::string a = DefaultRecordPath(0LL);            // 1970-01-01 00:00:00
        assert(a.find("19700101_000000") != std::string::npos);
        const std::string b = DefaultRecordPath(86399LL);        // 1970-01-01 23:59:59
        assert(b.find("19700101_235959") != std::string::npos);
        const std::string c = DefaultRecordPath(86400LL);        // 1970-01-02 00:00:00
        assert(c.find("19700102_000000") != std::string::npos);
        const std::string d = DefaultRecordPath(951782400LL);    // 2000-02-29 00:00:00（闰日）
        assert(d.find("20000229_000000") != std::string::npos);
    }
    std::puts("ScreenRecorderTest: 7 regression groups passed");
}
