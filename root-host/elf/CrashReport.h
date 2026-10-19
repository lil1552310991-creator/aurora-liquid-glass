#pragma once

// =============================================================================
// CrashReport.h —— 崩溃取证的**纯逻辑**部分（零 Android 依赖，可离线单测）
//
// ★★ 为什么需要它（真实事故，2026-10-13）：
//   用户把单文件 SH 发给朋友，朋友的机器（另一台设备 / 厂商 ROM 16）上
//   日志停在 `[overlay] 图层参数: ...` 之后就是 `Segmentation fault`，
//   下一行都打不出来。**设备不在手边、没有 gdb、也没法复现**
//   —— 于是每一轮"改一版让他再试"都要等用户转发，代价极高。
//
//   这个头文件提供三件在崩溃现场能安全用到的东西：
//     ① 把整数写进固定缓冲（信号处理器里**不能**用 printf/malloc/string）；
//     ② 解析 /proc/self/maps，回答「出错地址落在哪个 so 里」——
//        这一条就能区分「崩在我们自己 / libgui / 厂商 GPU 驱动 / linker」；
//     ③ 从模块路径里取基名，日志短、可读。
//
//   信号处理器只允许调用**异步信号安全**的函数（write/open/read/sigaction…），
//   所以这里全部是固定缓冲 + 手写循环，没有 std::string、没有分配。
// =============================================================================

#include <cstddef>

namespace aurora {
namespace crash {

/** 追加字符串；缓冲满就丢弃剩余部分（永远保证 NUL 结尾）。 */
inline void AppendStr(char* dst, std::size_t cap, std::size_t* pos, const char* s) {
    if (dst == nullptr || cap == 0 || pos == nullptr || s == nullptr) return;
    while (*s != '\0' && *pos + 1 < cap) dst[(*pos)++] = *s++;
    dst[*pos] = '\0';
}

/** 追加十六进制（带 0x 前缀）。 */
inline void AppendHex(char* dst, std::size_t cap, std::size_t* pos, unsigned long long v) {
    if (dst == nullptr || cap == 0 || pos == nullptr || *pos >= cap) return;
    static const char kDigits[] = "0123456789abcdef";
    char tmp[16];
    int n = 0;
    do {
        tmp[n++] = kDigits[v & 0xFULL];
        v >>= 4;
    } while (v != 0 && n < (int)sizeof tmp);
    AppendStr(dst, cap, pos, "0x");
    while (n > 0 && *pos + 1 < cap) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

/** 追加十进制（带符号）。 */
inline void AppendDec(char* dst, std::size_t cap, std::size_t* pos, long long v) {
    if (dst == nullptr || cap == 0 || pos == nullptr || *pos >= cap) return;
    if (v < 0) {
        AppendStr(dst, cap, pos, "-");
        v = -v;
    }
    char tmp[24];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + (v % 10));
        v /= 10;
    } while (v != 0 && n < (int)sizeof tmp);
    while (n > 0 && *pos + 1 < cap) dst[(*pos)++] = tmp[--n];
    dst[*pos] = '\0';
}

/** 十六进制解析（只认小写/大写十六进制数字），失败返回 false。 */
inline bool ParseHex(const char* s, std::size_t len, unsigned long long* out) {
    if (s == nullptr || out == nullptr || len == 0) return false;
    unsigned long long v = 0;
    for (std::size_t i = 0; i < len; ++i) {
        const char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        v = (v << 4) | d;
    }
    *out = v;
    return true;
}

/** 一次映射命中：地址落在 [start,end) 里。path 是映射名（可能是空串或 [anon]）。 */
struct MapHit {
    unsigned long long start = 0;
    unsigned long long end = 0;
    unsigned long long offset = 0;
    bool executable = false;
    bool writable = false;
    char path[128] = {0};
};

/** 取路径最后一段（`/apex/.../libc.so` → `libc.so`）。没有 '/' 就原样返回。 */
inline void BaseName(const char* path, char* out, std::size_t outCap) {
    if (out == nullptr || outCap == 0) return;
    out[0] = '\0';
    if (path == nullptr) return;
    const char* slash = nullptr;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/') slash = p;
    }
    const char* src = (slash != nullptr) ? slash + 1 : path;
    std::size_t i = 0;
    while (src[i] != '\0' && i + 1 < outCap) {
        out[i] = src[i];
        ++i;
    }
    out[i] = '\0';
}

/**
 * 解析 /proc/self/maps 的**一行**（不含换行符；带 '\r' 也会被剪掉）。
 *
 * maps 的每行形如：
 *   7f8c1a2000-7f8c1a3000 r-xp 00000000 103:03 1234   /apex/.../libc.so
 *   7f8c1a3000-7f8c1a4000 rw-p 00001000 00:00 0        [anon:libc_malloc]
 *
 * ★ 这是**唯一的解析入口**：FindMapping 只是"逐行调用它再判断包含关系"。
 *   这样离线测试覆盖的就是真机上跑的同一段代码（出厂代码 = 被测代码）。
 *   解析失败直接返回 false，绝不因为格式意外而越界读。
 */
inline bool ParseMapLine(const char* line, std::size_t len, MapHit* out) {
    if (line == nullptr || out == nullptr || len == 0) return false;
    // ★ 先把输出**整个**清掉：缺列时必须落成默认值，绝不能残留上一次解析的值
    //   （单测抓到过：offset 残留 0x3000，报告里就会把模块内偏移指错）。
    *out = MapHit{};

    // 跳到下一个空格之后再吃掉空格：即"跳过一列"。信号处理器里只做指针运算。
    struct Skip {
        static std::size_t Token(const char* m, std::size_t lineEnd, std::size_t p) {
            while (p < lineEnd && m[p] != ' ') ++p;
            while (p < lineEnd && m[p] == ' ') ++p;
            return p;
        }
    };

    std::size_t lineEnd = 0;
    while (lineEnd < len && line[lineEnd] != '\n' && line[lineEnd] != '\r') ++lineEnd;
    if (lineEnd == 0) return false;

    // 第 1 列 "start-end"
    std::size_t p = 0;
    while (p < lineEnd && line[p] == ' ') ++p;
    std::size_t dash = p;
    while (dash < lineEnd && line[dash] != '-') ++dash;
    unsigned long long start = 0, end = 0;
    if (dash >= lineEnd || dash == p) return false;
    if (!ParseHex(line + p, dash - p, &start)) return false;
    std::size_t q = dash + 1;
    std::size_t sp = q;
    while (sp < lineEnd && line[sp] != ' ') ++sp;
    if (sp == q || !ParseHex(line + q, sp - q, &end)) return false;

    out->start = start;
    out->end = end;
    // 第 2 列权限 "rwxp"：**必须有**。没有权限列的行是畸形行，
    // 放它过去就等于拿一行垃圾去给地址归属 —— 那比"没定位到"更糟。
    const std::size_t perm = Skip::Token(line, lineEnd, sp);
    if (perm + 3 > lineEnd) return false;
    const bool permOk = (line[perm] == 'r' || line[perm] == '-') &&
                        (line[perm + 1] == 'w' || line[perm + 1] == '-') &&
                        (line[perm + 2] == 'x' || line[perm + 2] == '-');
    if (!permOk) return false;
    out->writable = line[perm + 1] == 'w';
    out->executable = line[perm + 2] == 'x';
    // 第 3 列 offset（**不是** dev/inode —— 把 fe:1a 当偏移会解析失败）。
    // offset/dev/inode/路径都可能缺失（内核匿名映射就是空路径），缺了就取默认值。
    const std::size_t off = Skip::Token(line, lineEnd, perm);
    const std::size_t offEnd = (off < lineEnd) ? Skip::Token(line, lineEnd, off) : off;
    std::size_t offStop = offEnd;
    while (offStop > off && line[offStop - 1] == ' ') --offStop;
    unsigned long long offVal = 0;
    if (offStop > off && ParseHex(line + off, offStop - off, &offVal)) out->offset = offVal;
    // 第 4/5 列 dev + inode，之后整段是路径（行尾空白剪掉）
    std::size_t pathStart = Skip::Token(line, lineEnd, offEnd);   // 跳过 dev
    pathStart = Skip::Token(line, lineEnd, pathStart);            // 跳过 inode
    std::size_t pathEnd = lineEnd;
    while (pathEnd > pathStart && (line[pathEnd - 1] == ' ' || line[pathEnd - 1] == '\r')) {
        --pathEnd;
    }
    std::size_t n = 0;
    while (pathStart + n < pathEnd && n + 1 < sizeof out->path) {
        out->path[n] = line[pathStart + n];
        ++n;
    }
    out->path[n] = '\0';
    return true;
}

/**
 * 在 /proc/self/maps 的文本里找出 addr 落在哪个映射（半开区间 [start,end)）。
 *
 * ★ 注意：真机上的 maps 是 **560~665 KB**（system_server 实测 665564 字节），
 *   进程里千万不要"整个读进固定缓冲再解析"—— 会被截断，而截断之后
 *   "栈区间未知"就会让栈扫描按常数越界读、把信号处理器自己再崩一次
 *   （这个坑真机上真实踩到过：崩溃报告后半段全丢）。
 *   现场要的是**流式**逐行解析（见 aurora_overlay.cpp 的 scanMapsForFault）。
 */
inline bool FindMapping(const char* maps, std::size_t len, unsigned long long addr, MapHit* out) {
    if (maps == nullptr || out == nullptr) return false;
    std::size_t i = 0;
    while (i < len) {
        std::size_t lineEnd = i;
        while (lineEnd < len && maps[lineEnd] != '\n') ++lineEnd;
        MapHit hit;
        if (ParseMapLine(maps + i, lineEnd - i, &hit) && hit.start <= addr && addr < hit.end) {
            *out = hit;
            return true;
        }
        i = lineEnd + 1;
    }
    return false;
}

/** 地址落在可执行映射里才可能是**代码指针**（用来筛栈上的可疑字）。 */
inline bool LooksLikeCodeAddress(const char* maps, std::size_t len, unsigned long long addr,
                                 MapHit* out) {
    MapHit hit;
    if (!FindMapping(maps, len, addr, &hit)) return false;
    if (!hit.executable) return false;
    if (out != nullptr) *out = hit;
    return true;
}

}  // namespace crash
}  // namespace aurora
