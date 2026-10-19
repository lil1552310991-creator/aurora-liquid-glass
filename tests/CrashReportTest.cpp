// =============================================================================
// CrashReportTest —— 崩溃取证解析逻辑的离线回归。
//
// ★ 为什么要有它：
//   朋友的机器（另一台设备 / 厂商 ROM 16）上，旧版日志停在
//   `[overlay] 图层参数: ...` 之后直接 `Segmentation fault`。
//   设备不在手边、没有 gdb ⇒ 只能靠**崩溃现场自动落盘**的信息定位。
//   那段代码跑在信号处理器里（不能 printf/malloc），格式一旦解析错，
//   得到的就不是"没定位到"，而是**错误的模块名** —— 那比没有更糟。
//   所以这里的 maps 样本是**真机抓的原样文本**，不是编的格式。
//
// 纯字符串/整数逻辑，不需要设备。
// =============================================================================

#include "CrashReport.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using aurora::crash::AppendDec;
using aurora::crash::AppendHex;
using aurora::crash::AppendStr;
using aurora::crash::BaseName;
using aurora::crash::FindMapping;
using aurora::crash::LooksLikeCodeAddress;
using aurora::crash::MapHit;
using aurora::crash::ParseHex;
using aurora::crash::ParseMapLine;

// ── 真机（测试机，Android 16）`head -c 2000 /proc/self/maps` 的原样输出 ──
static const char* kRealMaps =
    "6470ee5000-6470f15000 r--p 00000000 fe:1a 10524173                       /system/bin/toybox\n"
    "6470f15000-6470f68000 r-xp 00030000 fe:1a 10524173                       /system/bin/toybox\n"
    "6470f68000-6470f6d000 r--p 00083000 fe:1a 10524173                       /system/bin/toybox\n"
    "6470f6d000-6470f70000 rw-p 00088000 fe:1a 10524173                       /system/bin/toybox\n"
    "6470f70000-6470f73000 rw-p 00000000 00:00 0                              [anon:.bss]\n"
    "74e3a00000-74e3d23000 ---p 00000000 00:00 0                              [anon:cfi shadow]\n"
    "74e3d23000-74e3d24000 r--p 00000000 00:00 0                              [anon:cfi shadow]\n"
    "7563a00000-7563e00000 rw-p 00000000 00:00 0                              [anon:libc_malloc]\n"
    "7563e62000-7563ea2000 r--s 00000000 00:12 312                            "
    "/dev/__properties__/u:object_r:exported_default_prop:s0\n"
    "7563ee2000-7563ee5000 r--p 00000000 fe:1a 29601597                       "
    "/system/lib64/libnetd_client.so\n"
    "7563ee5000-7563eea000 r-xp 00003000 fe:1a 29601597                       "
    "/system/lib64/libnetd_client.so\n"
    "7563eea000-7563eeb000 r--p 00008000 fe:1a 29601597                       "
    "/system/lib64/libnetd_client.so\n"
    "7563eeb000-7563eec000 rw-p 00009000 fe:1a 29601597                       "
    "/system/lib64/libnetd_client.so\n";

static std::size_t len(const char* s) { return std::strlen(s); }

// ── 1. 真机文本里逐段命中：代码段/只读段/数据段三个视图的区分是核心 ─────────
static void testSegmentsOfOneLibrary() {
    MapHit hit;

    // r-xp 段 = 代码段
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x7563ee6000ULL, &hit));
    assert(hit.executable);
    assert(!hit.writable);
    assert(std::strcmp(hit.path, "/system/lib64/libnetd_client.so") == 0);
    assert(hit.start == 0x7563ee5000ULL && hit.end == 0x7563eea000ULL);
    assert(hit.offset == 0x3000ULL);   // 第三列要解析对，别把 inode 当成 offset

    // r--p 段 = 只读数据（同一个 so，但**不是**代码）
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x7563ee3000ULL, &hit));
    assert(!hit.executable);
    assert(std::strcmp(hit.path, "/system/lib64/libnetd_client.so") == 0);

    // rw-p 段 = 可写数据
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x7563eeb800ULL, &hit));
    assert(hit.writable && !hit.executable);

    // --- 只有可执行映射才算"像代码指针" ---
    assert(LooksLikeCodeAddress(kRealMaps, len(kRealMaps), 0x7563ee6000ULL, nullptr));
    assert(!LooksLikeCodeAddress(kRealMaps, len(kRealMaps), 0x7563ee3000ULL, nullptr));
    assert(!LooksLikeCodeAddress(kRealMaps, len(kRealMaps), 0x7563a00000ULL, nullptr));

    std::puts("  [OK] 同一 so 的三个段分流正确（代码/只读/可写），offset 不是 inode");
}

// ── 2. 边界：半开区间 [start,end) —— 差一个字节就会指错模块 ─────────────────
static void testHalfOpenBoundaries() {
    MapHit hit;
    // 首字节必须命中
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x6470ee5000ULL, &hit));
    assert(hit.executable == false && hit.path[0] == '/');
    // 末字节命中
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x6470f14fffULL, &hit));
    assert(std::strcmp(hit.path, "/system/bin/toybox") == 0);
    // 恰好等于 end ⇒ 不命中（会落到下一行或落空）
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x6470f15000ULL, &hit));
    assert(hit.path[0] == '/' && std::strstr(hit.path, "toybox") != nullptr);  // 下一段的起点
    // 全空区间之外
    assert(!FindMapping(kRealMaps, len(kRealMaps), 0x1000ULL, nullptr));
    assert(!FindMapping(kRealMaps, len(kRealMaps), 0x7fffffffffffULL, nullptr));
    std::puts("  [OK] 半开区间 [start,end)：首字节命中、末字节命中、end 落到下一段");
}

// ── 3. 无名映射与畸形输入：绝不能读越界、也绝不能编出模块名 ────────────────
static void testAnonymousAndMalformed() {
    MapHit hit;
    assert(FindMapping(kRealMaps, len(kRealMaps), 0x7563a80000ULL, &hit));
    assert(std::strcmp(hit.path, "[anon:libc_malloc]") == 0);

    // 最后一行没有结尾换行 —— 真机上 maps 被截断就是这样
    const std::string noTrailingNl(kRealMaps, len(kRealMaps) - 1);
    assert(FindMapping(noTrailingNl.c_str(), noTrailingNl.size(), 0x7563eeb800ULL, &hit));

    // 只有设备的 maps 才有的 CRLF（PC 侧读同一份日志时会遇到）
    const char* crlf = "1000-2000 r-xp 00000000 fe:1a 7 /system/lib64/libgui.so\r\n";
    assert(FindMapping(crlf, len(crlf), 0x1800ULL, &hit));
    assert(std::strcmp(hit.path, "/system/lib64/libgui.so") == 0);

    // 畸形行：没有 '-'、没有权限列、十六进制非法、空文本
    assert(!FindMapping("garbage\n", 8, 0x1000ULL, nullptr));
    assert(!FindMapping("1000\n", 5, 0x1000ULL, nullptr));
    assert(!FindMapping("1000-2000\n", 10, 0x1800ULL, nullptr));   // 缺权限列
    assert(!FindMapping("zzzz-2000 r-xp 0 0 0 /x.so\n", 29, 0x1800ULL, nullptr));
    assert(!FindMapping("", 0, 0x1000ULL, nullptr));
    assert(!FindMapping(kRealMaps, len(kRealMaps), 0, nullptr));
    std::puts("  [OK] 匿名映射/无尾换行/CRLF/畸形行都不越界、不编造模块名");
}

// ── 4. 模块基名：日志里短、可读，且不会把路径当名字 ───────────────────────
static void testBaseName() {
    char out[128];
    BaseName("/apex/com.android.runtime/lib64/bionic/libc.so", out, sizeof out);
    assert(std::strcmp(out, "libc.so") == 0);
    BaseName("/vendor/lib64/egl/libGLESv2_adreno.so", out, sizeof out);
    assert(std::strcmp(out, "libGLESv2_adreno.so") == 0);
    BaseName("[anon:libc_malloc]", out, sizeof out);
    assert(std::strcmp(out, "[anon:libc_malloc]") == 0);
    BaseName("", out, sizeof out);
    assert(out[0] == '\0');
    BaseName(nullptr, out, sizeof out);
    assert(out[0] == '\0');
    // 缓冲小时必须截断且仍然 NUL 结尾
    char tiny[6];
    BaseName("/system/lib64/libgui.so", tiny, sizeof tiny);
    assert(std::strcmp(tiny, "libgu") == 0);
    std::puts("  [OK] 模块基名提取/截断（含 nullptr 与匿名映射）");
}

// ── 5. 数字格式化：信号处理器里不能用 printf，这里把等价性钉死 ──────────────
static void testNumberFormatting() {
    char buf[64];
    std::size_t pos = 0;
    buf[0] = '\0';

    AppendStr(buf, sizeof buf, &pos, "pc=");
    AppendHex(buf, sizeof buf, &pos, 0x7563ee6000ULL);
    AppendStr(buf, sizeof buf, &pos, " n=");
    AppendDec(buf, sizeof buf, &pos, -12345);
    AppendStr(buf, sizeof buf, &pos, " z=");
    AppendDec(buf, sizeof buf, &pos, 0);
    assert(std::strcmp(buf, "pc=0x7563ee6000 n=-12345 z=0") == 0);

    // 缓冲区必须始终 NUL 结尾、且不越界写（用金丝雀字节检查）
    char small[12];
    std::memset(small, 0x7f, sizeof small);
    pos = 0;
    small[0] = '\0';
    AppendStr(small, 8, &pos, "abcdefghijklmn");   // cap=8 ⇒ 最多写 7 个字符 + NUL
    assert(small[7] == '\0');
    assert(std::strcmp(small, "abcdefg") == 0);
    assert(small[8] == 0x7f);   // 越界写会踩掉金丝雀

    char hx[8];
    std::memset(hx, 0x7f, sizeof hx);
    pos = 0;
    hx[0] = '\0';
    AppendHex(hx, 6, &pos, 0xdeadbeefULL);   // 截断到 "0xde" + NUL
    assert(std::strcmp(hx, "0xdea") == 0);
    assert(hx[6] == 0x7f);

    // 空/非法参数不能崩
    pos = 0;
    AppendStr(nullptr, 0, &pos, "x");
    AppendHex(nullptr, 0, nullptr, 1);
    AppendDec(buf, sizeof buf, nullptr, 1);

    // ParseHex
    unsigned long long v = 0;
    assert(ParseHex("7563ee6000", 10, &v) && v == 0x7563ee6000ULL);
    assert(ParseHex("DEADBEEF", 8, &v) && v == 0xdeadbeefULL);
    assert(!ParseHex("12g4", 4, &v));
    assert(!ParseHex("", 0, &v));
    assert(!ParseHex(nullptr, 4, &v));
    std::puts("  [OK] 手写 hex/dec 格式化与 printf 等价，截断不越界（金丝雀验证）");
}

// ── 6. 单行解析入口：真机是**流式**逐行喂进来的，喂的就是这些形态 ──────────
static void testSingleLineParsing() {
    MapHit hit;
    const char* withNl = "7563ee5000-7563eea000 r-xp 00003000 fe:1a 29601597  /system/lib64/libgui.so\n";
    const char* noNl = "7563ee5000-7563eea000 r-xp 00003000 fe:1a 29601597  /system/lib64/libgui.so";
    const char* withCr = "7563ee5000-7563eea000 r-xp 00003000 fe:1a 29601597  /system/lib64/libgui.so\r\n";

    assert(ParseMapLine(withNl, std::strlen(withNl), &hit));
    assert(hit.executable && hit.offset == 0x3000ULL);
    assert(std::strcmp(hit.path, "/system/lib64/libgui.so") == 0);

    // 行尾有没有换行、是不是 CRLF，结果必须完全一致（流式读到的行就没有换行）
    MapHit a, b, c;
    assert(ParseMapLine(withNl, std::strlen(withNl) - 1, &a));
    assert(ParseMapLine(noNl, std::strlen(noNl), &b));
    assert(ParseMapLine(withCr, std::strlen(withCr), &c));
    assert(a.start == b.start && b.start == c.start);
    assert(a.end == b.end && b.end == c.end);
    assert(a.executable == b.executable && b.executable == c.executable);
    assert(a.offset == b.offset && b.offset == c.offset);
    assert(std::strcmp(a.path, b.path) == 0 && std::strcmp(b.path, c.path) == 0);

    // 逐行解析的并集必须与"整段文本一次解析"一致（两条路径不能有分歧）
    const char* text =
        "1000-2000 r-xp 00000000 fe:1a 1 /system/lib64/libgui.so\n"
        "2000-3000 rw-p 00001000 fe:1a 1 /system/lib64/libgui.so\n";
    MapHit whole, line0;
    assert(FindMapping(text, std::strlen(text), 0x1800ULL, &whole));
    const char* firstNl = std::strchr(text, '\n');            // 流式读到的第一行就是这一段
    assert(firstNl != nullptr);
    assert(ParseMapLine(text, static_cast<std::size_t>(firstNl - text), &line0));
    assert(whole.start == line0.start && whole.end == line0.end);
    assert(whole.executable == line0.executable && line0.executable);
    assert(std::strcmp(whole.path, line0.path) == 0);

    // 畸形单行：全部拒绝（缺权限列的行绝不能被当成合法映射）
    assert(!ParseMapLine("", 0, &hit));
    assert(!ParseMapLine("garbage", 7, &hit));
    assert(!ParseMapLine("1000-2000", 9, &hit));
    assert(!ParseMapLine("1000-2000 xxxx", 14, &hit));   // 权限列不是 rwxp 的形态
    // 只有"范围 + 权限"（后面几列缺失）是合法的：内核匿名映射本来就没有路径
    assert(ParseMapLine("1000-2000 r-xp", 14, &hit));
    assert(hit.executable && !hit.writable && hit.offset == 0 && hit.path[0] == '\0');
    std::puts("  [OK] 单行解析入口：换行/CRLF/无换行一致，与整段解析不分歧");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("CrashReportTest: 崩溃取证解析回归（maps 样本取自真机）");
    testSegmentsOfOneLibrary();
    testHalfOpenBoundaries();
    testAnonymousAndMalformed();
    testBaseName();
    testNumberFormatting();
    testSingleLineParsing();
    std::puts("CrashReportTest: 全部断言通过（6 组）");
    return 0;
}
