// aosp_glue.h —— 纯 ELF 宿主用的 SurfaceFlinger / binder / 输入通道胶水层
// ===========================================================================
// 这个文件**不是**重新设计的，而是把本项目已真机验证过的代码整块搬过来：
//
//   来源（D:\纯c图形基座）                                       搬运的段落
//   ---------------------------------------------------------  -----------------------------
//   build/recon/probe_touch_intercept.cpp:112-119               StrongPointer
//   build/recon/probe_touch_intercept.cpp:140-192               String8W / MetaW / Client
//                                                               （含 createSurface 必须重试）
//   build/recon/probe_touch_intercept.cpp:206-229               Transaction 符号与 txNew/txApply
//   build/recon/probe_touch_intercept.cpp:234-256               BindSyms
//   build/recon/probe_touch_intercept.cpp:258-320               Blob / String16 工具 /
//                                                               getServiceByHandle0
//   build/recon/probe_touch_intercept.cpp:322-431               createInputChannel
//                                                               （含 24 字节魔数定位）
//   build/recon/probe_touch_intercept.cpp:443-474               通道轮询（★ 本文件把 16 字节
//                                                               FINISHED 修正为 24 字节）
//   build/recon/probe_touch_intercept.cpp:478-507               removeInputChannel（默认禁用）
//   build/recon/probe_touch_intercept.cpp:583-659               全部 dlsym 字符串
//   build/recon/probe_touch_intercept.cpp:847-867               WindowInfo 字段设置序列
//   build/recon/probe_touch_intercept.cpp:888-919               事务提交序列
//   build/recon/probe_m3_ytbl.cpp:199-233                       StrongPointer / Client（同一形态）
//   build/recon/probe_m3_ytbl.cpp:431-433                       getSurface -> ANativeWindow* 取法
//   app/src/main/cpp/TouchInterceptor.h:55-386                  WindowInfoHandle 整块
//                                                               （真机偏移 + 三重 ABI 校验）
//   research/aosp/src/native/include/input/InputTransport.h:64-225   报文布局（逐字校对）
//   research/aosp/src/native/include/input/Input.h:500-556     PointerCoords bitset 稀疏语义
//   research/aosp/src/core/libutils/include/utils/BitSet.h:186,213,283
//                                                              BitSet64 位序
//   research/aosp/src/native/libs/input/InputConsumer.cpp:781-787
//                                                              24 字节 FINISHED 的构造
//
// ===========================================================================
// 已坐实、直接当前提用的事实（来自 research/touch_intercept_result.md）
// ===========================================================================
//   A. createSurface 第 1 次必返回厂商的「假 SurfaceControl」（mHandle==nullptr），
//      判据是 getHandle() != nullptr，**必须重试**（第 2 次成功）。拿空壳提交事务时
//      apply() 仍返回 0（假成功）。
//   B. **保持 uid 0**，不要降权到 1026 —— 否则图层掉进 Offscreen Hierarchy，
//      永远进不了输入系统。
//   C. `RefBase::incStrong(IBinder*)` 不能直接调（IBinder : public virtual RefBase，
//      本机 RefBase 在 +144）。必须**转移** readStrongBinder 已持有的那个引用
//      → setTokenOwned()。
//   D. `defaultServiceManager()->getService()` 在本机对所有服务返回 null
//      → 必须用 IPCThreadState::transact(handle=0, code=1, ...) 直连 servicemanager。
//   E. Android 16 的 Parcel 对象头是 24 字节魔数结构
//      （FD=0x66642A85 / binder=0x73682A85）—— **搜魔数定位，不要数偏移**。
//   F. ★ FINISHED 回复必须是 24 字节（Header 8 + Body::Finished 16）。
//      16 字节版本在 Android 16 上非法：InputTransport.cpp:113-117 会
//      ALOGE("Received message of incorrect size ...") 并返回 BAD_VALUE，
//      于是窗口 5 秒后被判 not responsive。
//   G. ★ touchOcclusionMode 必须 = ALLOW(2)。默认值 BLOCK_UNTRUSTED 会让全屏图层的
//      frame 把后面 app 的触摸判成不可信遮挡后**整条丢弃**（真机上表现为「整屏滑动不了」）。
//   H. touchableRegion 是**图层局部/input 空间**坐标。SF 每次快照变换到 display 空间，
//      所以 setPosition 之后 region **自动跟随**。但注意：SF 手里那份 WindowInfo 是
//      **反序列化出来的副本**，只改我们这边的 region 而不再提交一次事务，SF 看不到 —— 见
//      updateTouchableRegionRect() 的说明。
//
// ===========================================================================
// 与仓库旧注释的一处重要出入（代码照抄，注释修正）
// ===========================================================================
//   probe_touch_intercept.cpp:786 与 docs/TOUCH_INTERCEPTION_SF.md:515 把
//       surf.get() + sizeof(std::max_align_t)/2
//   注释成「= +8」。**这个注释是错的**：本工具链（NDK 27 / aarch64）上
//   sizeof(std::max_align_t) == 32，所以表达式值是 **+16**。
//   而 +16 恰好与 AOSP 源码的布局分析一致：`Surface : ANativeObjectBase<ANativeWindow, Surface, RefBase>`，
//   其中 RefBase 是多态基类（被选为 primary base，占偏移 0，vptr + 一个指针 = 16 字节），
//   非多态的 ANativeWindow 被排到 +16（本机已用同构类做 ABI 复现，clang 汇编里是 add x0, x0, #16）。
//   ⇒ 按项目铁律：**照抄代码表达式**（它是真机验证过的），不要照抄注释里的数字。
//
// ===========================================================================
// 安全纪律（本模块不上屏、不改触摸屏）
// ===========================================================================
//   * 不碰 /sys/class/input/*/device/inhibited（历史事故，已废弃为主路线）。
//   * 图层落在调用方指定的 layerStack 上。**做可触拦截实验请用虚拟屏的 layerStack**，
//     不要用物理屏（layerStack=0）—— 那会真的吃掉用户屏幕上的触摸。
//   * 不 dlclose（RefBase 对象仍活着时卸载 libgui 会崩）。
//   * 不 close(fd)（InputChannel 的 fd 是 Parcel 给的，fdsan 会 abort）。
//
// ===========================================================================
// 未在真机核实的部分（明确标注，别当已验证用）
// ===========================================================================
//   [未核实-1] `setTrustedOverlay` 的 mangled 名（bool 重载）。本机用 NDK clang 生成汇编
//              反推过形状（见文件末注释），但**没有在设备 libgui 上 dlsym 过**。
//              默认不使用；要用请先看 selfCheck 的符号清单。
//   Region 替换：设备 libgui 汇编和默认构造已独立确认 info()+248；
//   启动时再次核验运行中 addTouchableRegion 的 BTI/ADD/B thunk，未知 OTA 拒绝。
//   Region 方法从 libgui 依赖闭包解析；不再使用错误的 info()+184。
//   [未核实-3] `AXIS_PRESSURE=2` / `AXIS_SIZE=3` / `FINGER=1` 三个常量值来自 NDK 公开头
//              `android/input.h`（本机 sysroot 里逐行核过），不是从设备机器码读的。
//              PointerCoords 的 bitset 稀疏取值语义来自 AOSP 源码，**未在真机上用真实多指
//              报文比对过**（本机报文只有单指 312 字节的记录）。
//
// ===========================================================================
#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <string>
#include "../../app/src/main/cpp/OverlayTouchRegion.h"
#include "AndroidStrongReference.h"
#include "NativeWindowProbe.h"

// ---------------------------------------------------------------------------
// 平台开关
// ---------------------------------------------------------------------------
// AOSPGLUE_PLATFORM=1：完整胶水层（dlopen libgui/libbinder、SurfaceFlinger 事务、
//                    InputChannel、WindowInfoHandle）—— 只能在 Android 上编。
// AOSPGLUE_PLATFORM=0：只编**纯解析核心**（报文布局常量 / Touch / parseInputMessage /
//                    buildFinished）。这部分不依赖任何 Android 符号，
//                    所以可以在 PC 上编译并跑单元测试（见 aosp_glue_selftest.cpp）。
// 默认按 __ANDROID__ 自动判定；也可以 -DAOSPGLUE_WITH_PLATFORM=1 强制打开。
#if defined(__ANDROID__) || defined(AOSPGLUE_WITH_PLATFORM)
#define AOSPGLUE_PLATFORM 1
#else
#define AOSPGLUE_PLATFORM 0
#endif

#if AOSPGLUE_PLATFORM
#include <cerrno>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#endif

struct ANativeWindow;

namespace aospglue {

// 置 1 打开诊断输出（默认关，避免污染宿主 stdout/stderr）
#ifndef AOSPGLUE_VERBOSE
#define AOSPGLUE_VERBOSE 0
#endif

#if AOSPGLUE_PLATFORM

namespace detail {

// Startup calls cross private system ABIs. Keep their boundaries visible even
// when verbose per-frame diagnostics are disabled, including an abrupt fault.
// The hook lets aurora_overlay.cpp remember the last startup step for the crash
// report (a segfault here means the next line never gets printed at all).
inline void (*startupTraceHook)(const char*) = nullptr;

// ★★ 默认**不出声**：正常跑起来不该往屏幕上打任何东西（用户要求）。
//   只是不打印而已 —— hook **照旧**记录最后一步，崩溃取证一个字都不少。
//   诊断时由 aurora_overlay.cpp 用 --verbose / AURORA_VERBOSE=1（或 --dump）打开。
inline bool startupTraceSilent = true;

inline void startupTrace(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (!startupTraceSilent) {
        std::fputs("[overlay] [Startup] ", stdout);
        std::fputs(buf, stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
    if (startupTraceHook != nullptr) startupTraceHook(buf);
}

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
inline void vlog(const char* fmt, ...) {
#if AOSPGLUE_VERBOSE
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stderr);
    fputc('\n', stderr);
#else
    (void)fmt;
#endif
}

inline std::string fmtStr(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return std::string(buf);
}

inline int64_t nowNs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

// ---------------------------------------------------------------------------
// StrongPointer —— 整块复制自 probe_touch_intercept.cpp:112-119
// （与 probe_m3_ytbl.cpp:199-203 同一形态；这个「模板形态」是踩过坑的，
//   不要改成 std::array 或裸指针：AArch64 下 >16 字节的复合返回类型走内存传递，
//   与 non-trivial 的 sp<T> 调用约定一致，这是它能当 sret 接收槽用的原因。）
// ---------------------------------------------------------------------------
template <typename T>
struct StrongPointer {
    union {
        T* pointer;
        char padding[sizeof(std::max_align_t) * 2];
    };
    T* get() const { return pointer; }
};

// ---------------------------------------------------------------------------
// libgui 符号 —— dlsym 字符串整块复制自 probe_touch_intercept.cpp:583-612
// ---------------------------------------------------------------------------
struct GuiSyms {
    void* libgui = nullptr;
    void* libbinder = nullptr;
    void* libutils = nullptr;
    void* libui = nullptr;

    // -- SurfaceComposerClient / SurfaceControl --
    void (*string8Ctor)(void*, const char*) = nullptr;
    void (*metaCtor)(void*) = nullptr;
    StrongPointer<void> (*createSurface)(void*, void*, uint32_t, uint32_t, int32_t, uint32_t,
                                         void*, void*, uint32_t*) = nullptr;
    int32_t (*createSurfaceChecked)(void*, void*, uint32_t, uint32_t, int32_t, void*, int32_t,
                                    void*, void*, void*) = nullptr;
    int32_t (*validate)(const void*) = nullptr;
    int32_t (*getLayerId)(const void*) = nullptr;
    StrongPointer<void> (*getHandle)(const void*) = nullptr;
    StrongPointer<void> (*getSurface)(void*) = nullptr;
    void (*updateDefaultBufferSize)(void*, uint32_t, uint32_t) = nullptr;
    StrongPointer<void> (*clientGetDefault)() = nullptr;
    int32_t (*clientInitCheck)(const void*) = nullptr;

    // -- SurfaceComposerClient::Transaction --
    void (*txCtor)(void*) = nullptr;
    void (*txClear)(void*) = nullptr;
    void* (*txShow)(void*, const void*) = nullptr;
    void* (*txHide)(void*, const void*) = nullptr;
    void* (*txSetPosition)(void*, const void*, float, float) = nullptr;
    void* (*txSetCrop)(void*, const void*, const void*) = nullptr;
    void* (*txSetLayer)(void*, const void*, int32_t) = nullptr;
    void* (*txSetLayerStack)(void*, const void*, uint32_t) = nullptr;
    void* (*txSetAlpha)(void*, const void*, float) = nullptr;
    // AArch64 invisible-reference parameter: x2 points to a real owning
    // 8-byte sp<WindowInfoHandle> slot. The callee moves it and zeros the slot.
    // Pass its address explicitly so ownership and post-call cleanup are real.
    void* (*txSetInputWindowInfo)(void*, const void*, void*) = nullptr;
    // [未核实-1] bool 重载。形状为本机 clang 汇编反推，未在设备上 dlsym 过。
    void* (*txSetTrustedOverlay)(void*, const void*, bool) = nullptr;
    // AArch64 invisible-reference parameter: x2 points to a real owning
    // 8-byte sp<WindowInfoHandle> slot. The callee moves it and zeros the slot.
    // Pass its address explicitly so ownership and post-call cleanup are real.
    // -- 帧率投票（可选符号：缺失只是不能投票，不影响建层）--
    //
    //   为什么需要它：我们的图层是 SF 里 mRoot 的兄弟，不是 WMS 窗口。
    //   `ANativeWindow_setFrameRate` 只作用于应用自己的 buffer queue，对裸图层
    //   **不产生 display mode 投票**。测试机 / Android 16 实测：
    //     · 覆盖层压在设置界面上时 display 稳在 60Hz、GPU 稳在 222MHz（最低档）
    //       ⇒ 玻璃无论多快都只有 60fps，submit 被驱动背压拖到 14ms；
    //     · 同样代码在 display 120Hz + GPU 高频时：submit=3.9ms、gpu=1.2ms、
    //       present=120.6fps（完整光学，一个效果都没关）。
    //   所以"稳定 120fps"的前提是让 SF 知道这块玻璃要 120Hz。
    //
    //   Transaction::setFrameRate(const sp<SurfaceControl>&, float, int8_t, int8_t)
    //   Transaction::setFrameRateSelectionPriority(const sp<SurfaceControl>&, int32_t)
    void* (*txSetFrameRate)(void*, const void*, float, int8_t, int8_t) = nullptr;
    void* (*txSetFrameRateSelectionPriority)(void*, const void*, int32_t) = nullptr;
    int32_t (*txApply)(void*, bool, bool) = nullptr;

    // -- binder / libutils --
    void* (*ipcThreadStateSelf)() = nullptr;
    int32_t (*ipcTransact)(void*, int32_t, uint32_t, const void*, void*, uint32_t) = nullptr;
    void (*parcelCtor)(void*) = nullptr;
    int32_t (*writeInterfaceToken)(void*, const void*) = nullptr;
    int32_t (*writeString16)(void*, const void*) = nullptr;
    int32_t (*readInt32)(const void*, int32_t*) = nullptr;
    void (*setDataPosition)(const void*, size_t) = nullptr;
    const uint8_t* (*parcelData)(const void*) = nullptr;
    size_t (*parcelDataSize)(const void*) = nullptr;
    int32_t (*readString16)(const void*, void*) = nullptr;
    int (*readFileDescriptor)(const void*) = nullptr;
    int32_t (*readStrongBinder)(const void*, void*) = nullptr;
    int32_t (*writeStrongBinder)(void*, const void*) = nullptr;
    int32_t (*bpBinderHandle)(const void*) = nullptr;
    void (*string16Ctor)(void*, const char*, size_t) = nullptr;
    void (*string16DefaultCtor)(void*) = nullptr;
    size_t (*string16Size)(const void*) = nullptr;
    // WindowInfoHandle : public RefBase（**非虚**继承）—— 直接对它 incStrong 是正确的。
    // ★ incStrong 是 **const 成员函数**，mangled 名带 `K`；写成 _ZN... 会永远是 null。
    void (*refBaseIncStrong)(void*, const void*) = nullptr;
    void (*stringAssign)(void*, const char*) = nullptr;

    bool resolved = false;
    bool ok = false;
    std::string why;
    // 输入/触摸链路是否完整。false 表示厂商改过 libbinder/libgui 的相关签名，
    // 此时降级为"纯显示"：画面照常，但没有精确触摸区。
    bool inputChainReady = false;
};

inline GuiSyms& syms() {
    static GuiSyms s;
    return s;
}

// 屏幕当前**实际**刷新率（Hz）。读不到返回 0，调用方按"未知"处理。
//
// ★ 刻意用外部命令读文本，而不是 libgui 的 getActiveDisplayMode：
//   后者收发 sp<IBinder>，本 ELF 静态链接 libc++，跨 ABI 析构会 SIGABRT
//   （已在本机实测崩过：libc++abi: terminating + SIGABRT）。
//   这里只读一段文本，不构造任何系统类型的 C++ 对象，没有 ABI 面。
// ★ 也不拿自己的出帧率冒充屏幕刷新率：那是输出不是输入，会自锁（曾锁死 60fps）。
// 耗时约 50ms，只应在启动时调用一次。
inline float queryPanelRefreshRate() {
    const char* cmd = "dumpsys SurfaceFlinger 2>/dev/null | grep -m1 activeMode";
    FILE* pipe = popen(cmd, "r");
    if (pipe == nullptr) return 0.0f;
    char line[512] = {};
    float hz = 0.0f;
    if (fgets(line, sizeof line, pipe) != nullptr) {
        const char* p = strstr(line, "vsyncRate=");
        if (p != nullptr) {
            hz = strtof(p + 10, nullptr);
        }
    }
    pclose(pipe);
    // 合理区间保护：读到垃圾值不要拿去当帧率用。
    if (!(hz >= 20.0f && hz <= 240.0f)) return 0.0f;
    return hz;
}

// 输入/触摸链路是否可用。false = 厂商改过 libbinder/libgui 的相关签名，
// 调用方应降级为"纯显示"，而不是把整个覆盖层判为启动失败。
inline bool inputChainReady() {
    return syms().inputChainReady;
}


inline void* Rlib(void* h, const char* n, bool loud) {
    void* p = h ? dlsym(h, n) : nullptr;
    if (!p && loud) vlog("  [缺] %s", n);
    return p;
}

// 符号解析。**整块照抄 probe_touch_intercept.cpp:573-659 的 dlsym 字符串**。
inline bool resolveAll(std::string* why) {
    GuiSyms& s = syms();
    if (s.resolved) {
        if (!s.ok && why) *why = s.why;
        return s.ok;
    }
    s.resolved = true;

    // 顺序照抄：libgui -> libutils -> libbinder -> libui，全部 RTLD_GLOBAL
    s.libgui = dlopen("/system/lib64/libgui.so", RTLD_LAZY | RTLD_GLOBAL);
    s.libutils = dlopen("/system/lib64/libutils.so", RTLD_LAZY | RTLD_GLOBAL);
    s.libbinder = dlopen("/system/lib64/libbinder.so", RTLD_LAZY | RTLD_GLOBAL);
    s.libui = dlopen("/system/lib64/libui.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!s.libgui || !s.libbinder || !s.libutils) {
        s.why = fmtStr("dlopen 失败: libgui=%p libbinder=%p libutils=%p（errno=%d %s）",
                       s.libgui, s.libbinder, s.libutils, errno, strerror(errno));
        if (why) *why = s.why;
        return false;
    }

    auto R = [&](const char* n, bool loud = true) -> void* {
        void* p = dlsym(s.libgui, n);
        if (!p && loud) vlog("  [缺] %s", n);
        return p;
    };

    s.string8Ctor = (void (*)(void*, const char*))R("_ZN7android7String8C2EPKc");
    s.metaCtor = (void (*)(void*))R("_ZN7android3gui13LayerMetadataC2Ev");
    s.createSurface = (decltype(s.createSurface))R(
        "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj");
    s.createSurfaceChecked = (decltype(s.createSurfaceChecked))R(
        "_ZN7android21SurfaceComposerClient20createSurfaceCheckedERKNS_7String8EjjiPNS_2spINS_14SurfaceControlEEEiRKNS4_INS_7IBinderEEENS_3gui13LayerMetadataEPj",
        false);
    s.validate = (decltype(s.validate))R("_ZNK7android14SurfaceControl8validateEv", false);
    s.getLayerId = (decltype(s.getLayerId))R("_ZNK7android14SurfaceControl10getLayerIdEv", false);
    s.getHandle = (decltype(s.getHandle))R("_ZNK7android14SurfaceControl9getHandleEv", false);
    s.getSurface =
        (decltype(s.getSurface))R("_ZN7android14SurfaceControl10getSurfaceEv", false);
    s.updateDefaultBufferSize = (decltype(s.updateDefaultBufferSize))R(
        "_ZN7android14SurfaceControl23updateDefaultBufferSizeEjj", false);
    s.clientGetDefault = (decltype(s.clientGetDefault))R("_ZN7android21SurfaceComposerClient10getDefaultEv");
    s.clientInitCheck = (decltype(s.clientInitCheck))R("_ZNK7android21SurfaceComposerClient9initCheckEv");

    s.txCtor = (void (*)(void*))R("_ZN7android21SurfaceComposerClient11TransactionC2Ev");
    s.txClear = (void (*)(void*))R("_ZN7android21SurfaceComposerClient11Transaction5clearEv");
    s.txShow = (decltype(s.txShow))R(
        "_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE");
    s.txHide = (decltype(s.txHide))R(
        "_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE");
    s.txSetPosition = (decltype(s.txSetPosition))R(
        "_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff");
    s.txSetCrop = (decltype(s.txSetCrop))R(
        "_ZN7android21SurfaceComposerClient11Transaction7setCropERKNS_2spINS_14SurfaceControlEEERKNS_4RectE");
    s.txSetLayer = (decltype(s.txSetLayer))R(
        "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi");
    s.txSetLayerStack = (decltype(s.txSetLayerStack))R(
        "_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE");
    s.txSetAlpha = (decltype(s.txSetAlpha))R(
        "_ZN7android21SurfaceComposerClient11Transaction8setAlphaERKNS_2spINS_14SurfaceControlEEEf");
    s.txSetInputWindowInfo = (decltype(s.txSetInputWindowInfo))R(
        "_ZN7android21SurfaceComposerClient11Transaction18setInputWindowInfoERKNS_2spINS_14SurfaceControlEEENS2_INS_3gui16WindowInfoHandleEEE");
    s.txSetTrustedOverlay = (decltype(s.txSetTrustedOverlay))R(
        "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb",
        false);
    // 帧率投票：设备（Android 16 / libgui.so）里这两个符号都在，grep 已核实：
    //   _ZN7android21SurfaceComposerClient11Transaction12setFrameRateERKNS…
    //   _ZN7android21SurfaceComposerClient11Transaction29setFrameRateSelectionPriorityERKNS…
    s.txSetFrameRate = (decltype(s.txSetFrameRate))R(
        "_ZN7android21SurfaceComposerClient11Transaction12setFrameRateERKNS_2spINS_14SurfaceControlEEEfaa",
        false);
    s.txSetFrameRateSelectionPriority = (decltype(s.txSetFrameRateSelectionPriority))R(
        "_ZN7android21SurfaceComposerClient11Transaction29setFrameRateSelectionPriorityERKNS_2spINS_14SurfaceControlEEEi",
        false);
    s.txApply = (decltype(s.txApply))R(
        "_ZN7android21SurfaceComposerClient11Transaction5applyEbb");

    s.ipcThreadStateSelf =
        (decltype(s.ipcThreadStateSelf))Rlib(s.libbinder, "_ZN7android14IPCThreadState4selfEv", true);
    s.ipcTransact = (decltype(s.ipcTransact))Rlib(
        s.libbinder, "_ZN7android14IPCThreadState8transactEijRKNS_6ParcelEPS1_j", true);
    s.parcelCtor = (decltype(s.parcelCtor))Rlib(s.libbinder, "_ZN7android6ParcelC1Ev", true);
    s.writeInterfaceToken = (decltype(s.writeInterfaceToken))Rlib(
        s.libbinder, "_ZN7android6Parcel19writeInterfaceTokenERKNS_8String16E", true);
    s.writeString16 = (decltype(s.writeString16))Rlib(
        s.libbinder, "_ZN7android6Parcel13writeString16ERKNS_8String16E", true);
    s.readInt32 = (decltype(s.readInt32))Rlib(s.libbinder, "_ZNK7android6Parcel9readInt32EPi", true);
    s.setDataPosition =
        (decltype(s.setDataPosition))Rlib(s.libbinder, "_ZNK7android6Parcel15setDataPositionEm", true);
    s.parcelData = (decltype(s.parcelData))Rlib(s.libbinder, "_ZNK7android6Parcel4dataEv", true);
    s.parcelDataSize =
        (decltype(s.parcelDataSize))Rlib(s.libbinder, "_ZNK7android6Parcel8dataSizeEv", true);
    s.readString16 = (decltype(s.readString16))Rlib(
        s.libbinder, "_ZNK7android6Parcel12readString16EPNS_8String16E", false);
    s.readFileDescriptor = (decltype(s.readFileDescriptor))Rlib(
        s.libbinder, "_ZNK7android6Parcel18readFileDescriptorEv", true);
    s.readStrongBinder = (decltype(s.readStrongBinder))Rlib(
        s.libbinder, "_ZNK7android6Parcel16readStrongBinderEPNS_2spINS_7IBinderEEE", true);
    s.writeStrongBinder = (decltype(s.writeStrongBinder))Rlib(
        s.libbinder, "_ZN7android6Parcel17writeStrongBinderERKNS_2spINS_7IBinderEEE", false);
    s.bpBinderHandle =
        (decltype(s.bpBinderHandle))Rlib(s.libbinder, "_ZNK7android8BpBinder12binderHandleEv", false);
    s.string16Ctor = (decltype(s.string16Ctor))Rlib(s.libutils, "_ZN7android8String16C2EPKcm", true);
    s.string16DefaultCtor =
        (decltype(s.string16DefaultCtor))Rlib(s.libutils, "_ZN7android8String16C2Ev", false);
    s.string16Size = (decltype(s.string16Size))Rlib(s.libutils, "_ZNK7android8String164sizeEv", false);
    s.refBaseIncStrong = (decltype(s.refBaseIncStrong))Rlib(
        s.libutils, "_ZNK7android7RefBase9incStrongEPKv", false);
    if (!s.refBaseIncStrong)
        s.refBaseIncStrong =
            (decltype(s.refBaseIncStrong))dlsym(RTLD_DEFAULT, "_ZNK7android7RefBase9incStrongEPKv");
    // std::string::assign 在 libc++_shared.so 里，**不在 libutils**（真机实测查不到）。
    s.stringAssign = (void (*)(void*, const char*))dlsym(
        RTLD_DEFAULT,
        "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");
    if (!s.stringAssign)
        s.stringAssign = (void (*)(void*, const char*))Rlib(
            s.libutils,
            "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc", false);

    // 必需符号：缺了**连图层都建不出来**，所以这里失败就得退出。
    //
    // ★ 输入/触摸那条链路（IPCThreadState、Parcel、String16、setInputWindowInfo）
    //   故意**不列入必需**：它们只决定"能不能精确接管触摸"，与"能不能把画面
    //   画到屏幕上"是两件事。把整条链路都算必需的话，任何一家厂商稍微改过
    //   libbinder/libgui 的签名，整个覆盖层就直接起不来 —— 明明画面是可以显示的。
    //   现在缺这些只会降级成"纯显示、无精确触摸区"，并在日志里说清楚。
    const char* miss = nullptr;
    if (!s.string8Ctor) miss = "libgui: String8::String8(const char*)";
    else if (!s.metaCtor) miss = "libgui: gui::LayerMetadata::LayerMetadata()";
    else if (!s.createSurface) miss = "libgui: SurfaceComposerClient::createSurface";
    else if (!s.clientGetDefault) miss = "libgui: SurfaceComposerClient::getDefault()";
    else if (!s.txCtor) miss = "libgui: Transaction::Transaction()";
    else if (!s.txClear) miss = "libgui: Transaction::clear()";
    else if (!s.txShow) miss = "libgui: Transaction::show";
    else if (!s.txHide) miss = "libgui: Transaction::hide";
    else if (!s.txSetPosition) miss = "libgui: Transaction::setPosition";
    else if (!s.txSetCrop) miss = "libgui: Transaction::setCrop";
    else if (!s.txSetAlpha) miss = "libgui: Transaction::setAlpha";
    else if (!s.txApply) miss = "libgui: Transaction::apply";
    if (miss) {
        s.why = fmtStr("缺少必需符号: %s", miss);
        if (why) *why = s.why;
        return false;
    }
    // 输入链路是否完整，单独记一条 —— 调用方据此决定"要不要挂触摸区"。
    s.inputChainReady = s.txSetInputWindowInfo != nullptr && s.ipcThreadStateSelf != nullptr &&
                        s.ipcTransact != nullptr && s.parcelCtor != nullptr &&
                        s.writeInterfaceToken != nullptr && s.readStrongBinder != nullptr &&
                        s.readFileDescriptor != nullptr && s.string16Ctor != nullptr;
    s.ok = true;
    return true;
}

// ---------------------------------------------------------------------------
// SurfaceComposerClient 壳
// 整块复制自 probe_touch_intercept.cpp:140-192（含 4 次重试），
// 只是把 P(...) 换成了 vlog(...)。
// ---------------------------------------------------------------------------
struct String8W {
    // 缓冲区大小照抄（曾因 String8W 偏移踩坑，不要动）
    alignas(std::max_align_t) char d[64];
    explicit String8W(const char* s) { syms().string8Ctor(d, s); }
    void* p() { return d; }
};

struct MetaW {
    alignas(std::max_align_t) char d[1024];
    MetaW() {
        if (syms().metaCtor) syms().metaCtor(d);
    }
    void* p() { return d; }
};

struct Client {
    StrongPointer<void> owner{};
    bool valid = false;
    int32_t status = -19;
    Client() {
        // The constructor leaves mStatus=NO_INIT. The framework's sp factory
        // invokes onFirstRef(), connects to SF, and supplies a genuine owner.
        // Retain that owner for the process lifetime, like DefaultComposerClient.
        startupTrace("client.getDefault begin");
        if (!syms().clientGetDefault || !syms().clientInitCheck) return;
        owner = syms().clientGetDefault();
        startupTrace("client.getDefault returned=%p", owner.get());
        if (!owner.get()) return;
        status = syms().clientInitCheck(owner.get());
        valid = status == 0;
        startupTrace("client.initCheck=%d", status);
    }
    // ★ createSurface 必须重试。真机实测：本进程里**第一次** createSurface 家族调用必定
    //   失败，libgui 会「造一个假 SurfaceControl 防异常」（OPLUS 定制，日志：
    //   `createSurfaceChecked: Create fake SurfaceControl to prevent exception`），
    //   判据是 getHandle() != nullptr，第二次就成功。拿空壳提交事务时 SF 打
    //   `applyTransactions Handle 0x0 is not valid` 当空操作，而 apply() 仍返回 0（假成功）。
    StrongPointer<void> create(const char* n, uint32_t w, uint32_t h, uint32_t flags) {
        String8W s8(n);
        void* ph = nullptr;
        StrongPointer<void> out;
        out.pointer = nullptr;
        for (int attempt = 1; attempt <= 4 && !out.pointer; ++attempt) {
            // Each by-value metadata argument is consumed by the callee. Never
            // reuse its destroyed/moved storage for a later attempt.
            MetaW m;
            uint32_t hint = 0;
            StrongPointer<void> s;
            s.pointer = nullptr;
            startupTrace("surface.create attempt=%d api=%s", attempt,
                         syms().createSurfaceChecked ? "checked" : "legacy");
            if (syms().createSurfaceChecked) {
                const int32_t rc = syms().createSurfaceChecked(owner.get(), s8.p(), w, h, 1,
                                                              &s.pointer, flags, &ph, m.p(), &hint);
                startupTrace("surface.create rc=%d pointer=%p", rc, s.get());
            } else {
                s = syms().createSurface(owner.get(), s8.p(), w, h, 1, flags, &ph, m.p(), &hint);
                startupTrace("surface.create returned=%p", s.get());
            }
            bool real = false;
            if (s.get() && syms().getHandle) {
                startupTrace("surface.getHandle begin");
                StrongPointer<void> hh = syms().getHandle(s.get());
                real = hh.get() != nullptr;
                startupTrace("surface.getHandle valid=%d", real ? 1 : 0);
            } else {
                real = s.get() != nullptr;
            }
            vlog("  第 %d 次 createSurface -> sp=%p handle=%s layerId=%d %s", attempt, s.get(),
                 real ? "有效" : "**空壳**",
                 (real && syms().getLayerId) ? syms().getLayerId(s.get()) : -1,
                 real ? "" : "(OPLUS fake SurfaceControl)");
            if (real) out = s;
        }
        return out;
    }
};

// ---------------------------------------------------------------------------
// Transaction —— 整块复制自 probe_touch_intercept.cpp:222-229
// ---------------------------------------------------------------------------
constexpr size_t kTxBytes = 1024;  // Transaction 实测至少用到 +0x160 = 352 字节

inline void* txNew() {
    void* m = calloc(1, kTxBytes);
    if (m) syms().txCtor(m);
    return m;
}
inline int32_t txApply(void* tx, bool sync) {
    return syms().txApply(tx, sync, /*oneWay=*/false);
}

// ---------------------------------------------------------------------------
// String16 工具 —— 整块复制自 probe_touch_intercept.cpp:258-276
// ---------------------------------------------------------------------------
struct Blob {
    alignas(std::max_align_t) char d[256];
};

inline void makeString16(Blob* b, const char* s) {
    memset(b->d, 0, sizeof(b->d));
    syms().string16Ctor(b->d, s, strlen(s));
}

// ---------------------------------------------------------------------------
// 输入通道 —— 整块复制自 probe_touch_intercept.cpp:279-431
// ---------------------------------------------------------------------------
struct InputChannelResult {
    bool ok = false;
    int fd = -1;
    void* token = nullptr;
};

// inputflinger 的 binder 与 handle（退出时 removeInputChannel 要用）
struct InputFlingerRef {
    void* binder = nullptr;
    int32_t handle = -1;
};
inline InputFlingerRef& inputflingerRef() {
    static InputFlingerRef r;
    return r;
}

// 经 IPCThreadState 直连 servicemanager(handle 0) 拿服务 binder。
// **不能用** defaultServiceManager()->getService()：本机对所有服务都返回 null。
inline void* getServiceByHandle0(const char* name) {
    GuiSyms& s = syms();
    void* its = s.ipcThreadStateSelf ? s.ipcThreadStateSelf() : nullptr;
    if (!its || !s.ipcTransact) return nullptr;
    for (uint32_t code = 1; code <= 3; ++code) {
        Blob d, r;
        s.parcelCtor(d.d);
        s.parcelCtor(r.d);
        Blob tok, nm;
        makeString16(&tok, "android.os.IServiceManager");
        makeString16(&nm, name);
        s.writeInterfaceToken(d.d, tok.d);
        s.writeString16(d.d, nm.d);
        errno = 0;
        int32_t tr = s.ipcTransact(its, 0, code, d.d, r.d, 0);
        size_t rsz = s.parcelDataSize(r.d);
        vlog("  [handle0] code=%u transact -> %d (errno=%d) replySize=%zu", code, tr, errno, rsz);
        if (rsz < 8) continue;
        s.setDataPosition(r.d, 0);
        int32_t exc = -999;
        s.readInt32(r.d, &exc);
        if (exc != 0) continue;
        s.setDataPosition(r.d, 4);
        Blob spb;
        memset(spb.d, 0, sizeof(spb.d));
        int32_t brs = s.readStrongBinder(r.d, spb.d);
        void* b = *(void**)spb.d;
        vlog("  [handle0] readStrongBinder -> status=%d binder=%p", brs, b);
        if (b) return b;
    }
    return nullptr;
}

// IInputFlinger::createInputChannel —— 整块复制自 probe_touch_intercept.cpp:322-431
// 只把 P(...) 换成 vlog(...)，逻辑一字未改（含魔数定位与「token sp 故意不析构」）。
inline InputChannelResult createInputChannel(const char* chanName, std::string* why) {
    GuiSyms& s = syms();
    InputChannelResult out;
    void* its = s.ipcThreadStateSelf ? s.ipcThreadStateSelf() : nullptr;
    if (!its || !s.ipcTransact) {
        if (why) *why = "IPCThreadState 不可用（libbinder 符号缺失）";
        return out;
    }

    void* inputflinger = getServiceByHandle0("inputflinger");
    if (!inputflinger) {
        if (why) *why = "未拿到 inputflinger binder（需要 uid 0 或 2000，且 servicemanager 可达）";
        return out;
    }
    int32_t h = s.bpBinderHandle ? s.bpBinderHandle(inputflinger) : -1;
    inputflingerRef().binder = inputflinger;
    inputflingerRef().handle = h;
    vlog("  inputflinger binder = %p  BpBinder::binderHandle() = %d", inputflinger, h);
    if (h < 0) {
        if (why) *why = "inputflinger 的 BpBinder::binderHandle() 取不到";
        return out;
    }

    Blob data, reply;
    s.parcelCtor(data.d);
    s.parcelCtor(reply.d);
    Blob iface, chan;
    makeString16(&iface, "android.os.IInputFlinger");
    makeString16(&chan, chanName);
    int32_t w1 = s.writeInterfaceToken(data.d, iface.d);
    int32_t w2 = s.writeString16(data.d, chan.d);
    vlog("  writeInterfaceToken -> %d   writeString16(\"%s\") -> %d", w1, chanName, w2);

    errno = 0;
    // createInputChannel = code 1（由设备自己的 libinput.so 反汇编确认）
    int32_t rc = s.ipcTransact(its, h, 1, data.d, reply.d, 0);
    size_t rsz = s.parcelDataSize(reply.d);
    const uint8_t* rdata = s.parcelData(reply.d);
    vlog("  IPCThreadState::transact(handle=%d, code=1) -> %d (errno=%d)  replySize=%zu", h, rc,
         errno, rsz);
    if (rsz < 16) {
        if (why) *why = fmtStr("createInputChannel 回复太短（%zu 字节，rc=%d errno=%d）", rsz, rc,
                               errno);
        return out;
    }

    s.setDataPosition(reply.d, 0);
    int32_t exc = -1;
    s.readInt32(reply.d, &exc);
    vlog("  binder::Status 异常码 = %d (0=EX_NONE)", exc);
    if (exc != 0) {
        if (why) *why = fmtStr("createInputChannel 抛异常，binder::Status=%d", exc);
        return out;
    }

    // ★ 对象头是魔数结构（差 4 字节就返回 -2147483647）：
    //     FD            magic = 0x66642A85，值在 +8，cookie 在 +16
    //     强 binder     magic = 0x73682A85
    //   直接搜魔数定位，**不要数偏移**。
    const uint32_t kFdMagic = 0x66642A85u;
    const uint32_t kBinderMagic = 0x73682A85u;
    int fdOff = -1, bOff = -1;
    for (int off = 4; off + 24 <= (int)rsz; off += 4) {
        uint32_t mg = 0;
        memcpy(&mg, rdata + off, 4);
        if (mg == kFdMagic && fdOff < 0) fdOff = off;
        if (mg == kBinderMagic && bOff < 0) bOff = off;
    }
    vlog("  按对象魔数定位：FD 对象 @0x%x   binder 对象 @0x%x", fdOff, bOff);

    if (fdOff >= 0) {
        s.setDataPosition(reply.d, (size_t)fdOff);
        errno = 0;
        int f = s.readFileDescriptor(reply.d);
        vlog("  Parcel::readFileDescriptor() -> %d (errno=%d %s)", f, errno, strerror(errno));
        out.fd = f;
    }

    // ★ 这个 sp<IBinder> 缓冲区**故意永不析构**：它持有的那个强引用会被「转移」给
    //   WindowInfo（setTokenOwned），WindowInfo 析构时正好 decStrong 一次。
    static Blob s_tokenSp;
    memset(s_tokenSp.d, 0, sizeof(s_tokenSp.d));  // 全 0 == sp() 默认构造
    if (bOff >= 0) s.setDataPosition(reply.d, (size_t)bOff);
    int32_t brs = s.readStrongBinder(reply.d, s_tokenSp.d);
    out.token = *(void**)s_tokenSp.d;
    vlog("  Parcel::readStrongBinder() -> status=%d token=%p", brs, out.token);
    if (out.token && s.bpBinderHandle)
        vlog("         token 的 BpBinder::binderHandle() = %d", s.bpBinderHandle(out.token));

    // fd 体检（InputChannel 指纹：SOCK_SEQPACKET + O_NONBLOCK）
    if (out.fd >= 0) {
        int fl = fcntl(out.fd, F_GETFL);
        int sotype = 0;
        socklen_t sl = sizeof(sotype);
        getsockopt(out.fd, SOL_SOCKET, SO_TYPE, &sotype, &sl);
        vlog("  fd=%d  F_GETFL=0x%x (O_NONBLOCK=%d)  SO_TYPE=%d (5=SEQPACKET)", out.fd, fl,
             (fl & O_NONBLOCK) ? 1 : 0, sotype);
    }

    out.ok = (out.fd >= 0 && out.token != nullptr);
    if (!out.ok && why)
        *why = fmtStr("输入通道不完整：fd=%d token=%p（魔数定位 fd@0x%x binder@0x%x）", out.fd,
                      out.token, fdOff, bOff);
    return out;
}

// IInputFlinger::removeInputChannel（code=2）—— 抄自 probe_touch_intercept.cpp:478-507。
//
// ★★ 默认**不要调用** ★★
// 真机实测：按这个写法在退出阶段调用会 native_crash
// （gaia:crashbox: errorType=native_crash）。不加它也没问题：进程退出时 SF 会回收图层，
// InputDispatcher 会把通道标记为 `Channel is unrecoverably broken and will be disposed!`。
// 保留函数只为后续查证（可能要先用 dup 处理 fd，或注意 token 的引用计数状态）。
inline void removeInputChannel(void* token, std::string* why) {
    GuiSyms& s = syms();
    InputFlingerRef& ref = inputflingerRef();
    if (!ref.binder || token == nullptr) {
        if (why) *why = "removeInputChannel: 跳过（没有 inputflinger 或 token）";
        return;
    }
    void* its = s.ipcThreadStateSelf ? s.ipcThreadStateSelf() : nullptr;
    if (!its || !s.ipcTransact || !s.writeStrongBinder) {
        if (why) *why = "removeInputChannel: 跳过（binder 符号不可用）";
        return;
    }
    Blob d, r;
    s.parcelCtor(d.d);
    s.parcelCtor(r.d);
    Blob iface;
    makeString16(&iface, "android.os.IInputFlinger");
    s.writeInterfaceToken(d.d, iface.d);
    struct SpSlot {
        void* p;
    };
    SpSlot tok{token};  // sp<IBinder> 的二进制形态就是一个指针
    int32_t w = s.writeStrongBinder(d.d, &tok);
    errno = 0;
    int32_t rc = s.ipcTransact(its, ref.handle, 2, d.d, r.d, 0);
    vlog("  removeInputChannel(token) -> writeStrongBinder=%d transact(code=2)=%d errno=%d", w, rc,
          errno);
    if (why) *why = fmtStr("removeInputChannel rc=%d（★ 真机实测此调用会 native_crash）", rc);
}

}  // namespace detail

// ===========================================================================
// WindowInfoHandle —— **整块复制** app/src/main/cpp/TouchInterceptor.h:55-386
// ===========================================================================
// 只做了三件不改语义的事：
//   1. 命名空间 aurora -> aospglue（原文件是 aurora::WindowInfoHandle）
//   2. P()/fprintf 日志换成 detail::vlog()
//   3. 新增三个只读访问器（touchOcclusionMode / regionRect / regionRectCount）与
//      Region 的「替换语义」符号（Region::set / clear / getArray），供
//      updateTouchableRegionRect() 用。原文件的写字段逻辑一字未改。
//
// ★★ ABI 关键警告：不要照 AOSP 分支硬编码偏移 ★★
// 真机实测（2026-07 构建的 libgui.so，反汇编机器码）与 android16-release **不一致**：
//     项                        android16-release   真机实测
//     WindowInfoHandle::mInfo          16               56
//     sizeof(WindowInfo)              432              496
//     sizeof(WindowInfoHandle)        448              552
//     Region 与之后的字段            ——             整体 +64
// 证据：WindowInfoHandle::readFromParcel @0x1bd844 仅 12 字节
//   5f2403d5 00e00091 47210014 = bti j; add x0, x0, #0x38; b <plt>   (0x38=56)
// 四处互相印证（readFromParcel / writeToParcel / updateFrom / 默认构造）。
// 下面用的是**本机实测值**，并且使用前做运行时 ABI 三重校验；校验不过就拒绝操作
// （宁可拦截失效，也不写坏系统内存）。
//
// 已排除的做法：
//   - malloc 假对象：sp<> 会 incStrong/decStrong，RefBase::mRefs 不存在 → 必崩
//   - 手写 std::string 的 SSO 布局（改用真实的 std::string::assign）
namespace winfo {

// InputConfig 位值（AIDL 公开契约，跨版本稳定）
constexpr uint32_t kCfgDefault = 0;
constexpr uint32_t kCfgNoInputChannel = 1u << 0;
constexpr uint32_t kCfgNotVisible = 1u << 1;
constexpr uint32_t kCfgNotFocusable = 1u << 2;
constexpr uint32_t kCfgNotTouchable = 1u << 3;
constexpr uint32_t kCfgTrustedOverlay = 1u << 8;
constexpr uint32_t kCfgSpy = 1u << 14;             // ★ 千万不要设
constexpr uint32_t kCfgInterceptsStylus = 1u << 15;  // ★ 同上

// Z 序（参考实现 EndToEndNativeInputTest.cpp 用 INT32_MAX - 10）
constexpr int32_t kLayerBase = 0x7FFFFFFF - 10;

// touchOcclusionMode：ALLOW = 2（见文件头 G 条）
constexpr int32_t kTouchOcclusionAllow = 2;

// 分配字节数：真机实测 552，留一倍余量容忍 OTA 后变大。**仅用于分配**，不参与字段寻址。
constexpr size_t kAllocBytes = 1024;

// ---- 本机实测偏移（相对 WindowInfoHandle 起点）----
// 简单标量字段经过真机校验；Region 与后续字段比 AOSP 多 64 字节。
constexpr size_t kInfoOffset = 56;

// WindowInfo 内部字段偏移（真机值）
constexpr size_t kOffToken = 8;
constexpr size_t kOffWindowToken = 16;
constexpr size_t kOffId = 24;
constexpr size_t kOffName = 32;
constexpr size_t kOffDispTimeout = 56;
constexpr size_t kOffFrame = 64;
constexpr size_t kOffContentSize = 80;
constexpr size_t kOffSurfaceInset = 88;
constexpr size_t kOffGlobalScale = 92;
constexpr size_t kOffAlpha = 96;
constexpr size_t kOffTouchableRegion = 248;
// OEM Transform 在 Region 之前多 64 字节；Region 本体仍是 104 字节。
constexpr size_t kOffTouchOcclusion = 352;
constexpr size_t kOffOwnerPid = 356;
constexpr size_t kOffOwnerUid = 360;
constexpr size_t kOffPackageName = 368;
constexpr size_t kOffInputConfig = 392;
constexpr size_t kOffDisplayId = 396;
constexpr size_t kOffAppInfo = 400;
constexpr size_t kOffReplaceRegion = 448;
constexpr size_t kOffLayoutType = 472;
constexpr size_t kOffLayoutFlags = 476;
constexpr size_t kMinStructBytes = 496;  // sizeof(WindowInfo) 真机值

}  // namespace winfo

// Android 的 Rect（4×int32）—— NDK 公开的稳定类型（android/rect.h 的 ARect），可安全手写
struct ARect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    ARect() = default;
    ARect(int32_t l, int32_t t, int32_t r, int32_t b) : left(l), top(t), right(r), bottom(b) {}
};

class WindowInfoHandle final {
public:
    WindowInfoHandle() = default;
    ~WindowInfoHandle() { destroy(); }
    WindowInfoHandle(const WindowInfoHandle&) = delete;
    WindowInfoHandle& operator=(const WindowInfoHandle&) = delete;

    static bool resolve() {
        auto& s = syms();
        if (s.ready) return s.ok;
        s.ready = true;

        // libgui/libutils 已由 detail::resolveAll() dlopen（RTLD_GLOBAL）；这里再取一次句柄。
        s.libgui = dlopen("/system/lib64/libgui.so", RTLD_LAZY | RTLD_GLOBAL);
        void* lu = dlopen("/system/lib64/libutils.so", RTLD_LAZY | RTLD_GLOBAL);
        dlopen("/system/lib64/libui.so", RTLD_LAZY | RTLD_GLOBAL);
        dlopen("/system/lib64/libbinder.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!s.libgui) return (s.ok = false);

        auto g = [&](const char* n) { return dlsym(s.libgui, n); };
        auto u = [&](const char* n) { return lu ? dlsym(lu, n) : nullptr; };

        s.ctor = (void (*)(void*))g("_ZN7android3gui16WindowInfoHandleC2Ev");
        s.dtor = (void (*)(void*))g("_ZN7android3gui16WindowInfoHandleD1Ev");
        s.addTouchableRegion = (void (*)(void*, const ARect*))
            g("_ZN7android3gui10WindowInfo18addTouchableRegionERKNS_4RectE");

        // 真实 operator new / delete：RefBase::decStrong 归零时会 `delete this`，
        // 所以对象**必须**由真实 operator new 分配，否则那个 delete 会崩。
        s.opNew = (void* (*)(size_t))dlsym(RTLD_DEFAULT, "_Znwm");
        if (!s.opNew) s.opNew = (void* (*)(size_t))u("_Znwm");
        s.opDelete = (void (*)(void*))dlsym(RTLD_DEFAULT, "_ZdlPv");
        s.incStrong = (void (*)(void*, const void*))u("_ZNK7android7RefBase9incStrongEPKv");
        s.decStrong = (void (*)(void*, const void*))u("_ZNK7android7RefBase9decStrongEPKv");
        // ★ 注意 `K`：incStrong 是 **const 成员函数**，mangled 名是
        //   `_ZNK7android7RefBase9incStrongEPKv`。写成 `_ZN...` 会 dlsym 返回 null，
        //   于是 setToken() 静默失败（真机实测：原写法一直是 null）。
        // std::string::assign 属于 libc++（libc++_shared.so），**不在 libutils 里**。
        s.assignStr = (void (*)(void*, const char*))dlsym(
            RTLD_DEFAULT,
            "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");
        if (!s.assignStr)
            s.assignStr = (void (*)(void*, const char*))u(
                "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");

        // Device libgui thunk: BTI; ADD x0,x0,#0xf8; B Region::orSelf@plt.
        // Its default constructor independently calls Region::C1 at info+0xf8.
        // The old info+184 addressed an OEM Transform float (1.0), not Region.
        // Verify the running library before any Region member call, and resolve
        // through libgui's dependency closure (libui), never libinput's copy.
        uint32_t regionThunk[3] = {};
        if (s.addTouchableRegion) {
            std::memcpy(regionThunk, reinterpret_cast<const void*>(s.addTouchableRegion),
                        sizeof regionThunk);
            s.regionAbiVerified = aurora::DecodeWindowInfoRegionOffset(
                regionThunk, 3, &s.regionOffset);
        }
        if (s.regionAbiVerified) {
            s.regionSet = (void (*)(void*, const ARect*))
                g("_ZN7android6Region3setERKNS_4RectE");
            s.regionClear = (void (*)(void*))g("_ZN7android6Region5clearEv");
            s.regionGetArray = (const void* (*)(const void*, size_t*))
                g("_ZNK7android6Region8getArrayEPm");
        }

        s.ok = s.ctor && s.dtor && s.opNew && s.incStrong && s.decStrong;
        return s.ok;
    }

    static const char* missingSymbol() {
        auto& s = syms();
        if (!s.libgui) return "dlopen libgui.so 失败";
        if (!s.ctor) return "缺 WindowInfoHandle 构造函数";
        if (!s.dtor) return "缺 WindowInfoHandle 析构函数";
        if (!s.opNew) return "缺 operator new（无法安全分配 RefBase）";
        if (!s.addTouchableRegion) return "缺 addTouchableRegion";
        if (!s.incStrong) return "缺 RefBase::incStrong（无法持有 WindowInfo）";
        if (!s.decStrong) return "缺 RefBase::decStrong（无法平衡输入事务参数）";
        return nullptr;
    }

    // 创建。用真实 operator new 分配 + 真实构造函数。
    bool create() {
        if (mem_) return true;
        if (!resolve()) return false;
        // ★★ 先确定布局位移。Region 偏移是**设备实测值**（OverlayTouchRegion.h 的解码器
        //    从 addTouchableRegion 的第一条 ADD 里读出来），Region 及其之后的字段
        //    全部跟着它走。解不出来 = 不知道本机布局 ⇒ **拒绝创建**，
        //    绝不按开发机的偏移硬写（换台机器就是往错的地方写，会写坏 sp<> 等字段）。
        if (!syms().regionAbiVerified) {
            detail::startupTrace(
                "layout: Region 偏移未解出（%s）→ 拒绝创建 WindowInfoHandle",
                regionReplaceInfo().c_str());
            return false;
        }
        layoutShift_ = aurora::WindowInfoLayoutShift(syms().regionOffset);
        layoutShiftKnown_ = true;
        detail::startupTrace("layout: Region 偏移=%zu（AOSP %zu）位移=%+lld",
                             syms().regionOffset, aurora::kAospWindowInfoRegionOffset,
                             static_cast<long long>(layoutShift_));
        mem_ = syms().opNew(winfo::kAllocBytes);
        if (!mem_) return false;
        std::memset(mem_, 0, winfo::kAllocBytes);
        syms().ctor(mem_);
        // ---- 运行时 ABI 校验 ----
        // 校验 mInfo 偏移对不对：WindowInfo 的 vptr 应指向代码段（高位地址），
        // 且 ObjectSize 之后若干字节应被构造函数写过（非全 0）。
        // 校验不通过就拒绝使用 —— 宁可拦截失效，也不写坏系统内存。
        if (!validateLayout()) {
            detail::vlog(
                "[aospglue] ★ ABI 校验失败：本机 libgui 的 WindowInfo 布局与本文件记录的偏移"
                "不一致。已拒绝操作（不会写坏内存）。");
            detail::startupTrace("layout: 前缀 ABI 校验失败（dispatchingTimeout@56 / scale@92）→ 拒绝");
            destroy();
            return false;
        }
        // ★ 位移后的字段也要**写回读**验一次：证明"位移"这个推导对这台机器成立。
        if (!verifyShiftedFields()) {
            detail::startupTrace("layout: 位移后字段写回读失败（位移=%+lld）→ 拒绝",
                                 static_cast<long long>(layoutShift_));
            destroy();
            return false;
        }
        return true;
    }

    void destroy() {
        if (!mem_) return;
        // 用真实析构。之后**不 free** —— 对象由 RefBase 的引用计数管理，
        // decStrong 归零时它会自己 `delete this`。
        // 若引用计数没归零，宁可泄漏也不 double free。
        //
        // ★★ 调用本函数前请确认：这个 handle 的 token 强引用**没有被转移**给别人
        //    （见 Overlay::rebuildForRegion 的注释）。一个 token 只能被一个 handle 拥有，
        //    否则会有两次 decStrong 打在一个引用上 → 提前释放 → use-after-free。
        if (syms().dtor) syms().dtor(mem_);
        mem_ = nullptr;
    }

    void* raw() const { return mem_; }
    bool valid() const { return mem_ != nullptr; }
    void* info() const {
        return mem_ ? (void*)((unsigned char*)mem_ + winfo::kInfoOffset) : nullptr;
    }

    // ---- 字段设置（优先用真实方法）----

    // 可触区域（图层局部坐标）。用真实方法，内部正确处理 Region。
    // ★ 注意语义：WindowInfo::addTouchableRegion 内部是 `touchableRegion.orSelf(region)`
    //   （并集，**不是替换**）。所以它只适合初次设置（此时 region 为空）；
    //   反复调用会累积成历史所有矩形的并集，最后退化成全屏拦截。
    //   更新区域请用 setTouchableRegionReplace()。
    void setTouchableRegion(int32_t w, int32_t h) {
        if (!info() || !syms().addTouchableRegion) return;
        ARect r(0, 0, w, h);
        syms().addTouchableRegion(info(), &r);
    }

    // Read-only bytes for the diagnostic mode. Region's exact object offset
    // is already verified from addTouchableRegion's machine code at runtime.
    const unsigned char* infoBytes() const { return reinterpret_cast<const unsigned char*>(info()); }
    bool probeAddTouchableRegion(int32_t l, int32_t t, int32_t r, int32_t b) {
        if (!info() || !syms().addTouchableRegion) return false;
        ARect rect(l, t, r, b);
        syms().addTouchableRegion(info(), &rect);
        return true;
    }

    // ★ 真·替换语义：把 touchableRegion 整个换成 Rect(l,t,r,b)（图层局部坐标，可带偏移）。
    //   优先 Region::set(Rect)（语义 = clear + 加入该矩形）；
    //   退而求其次 clear() + addTouchableRegion()；
    //   两者都拿不到返回 false（调用方必须处理，别静默退化成并集）。
    bool setTouchableRegionReplace(int32_t l, int32_t t, int32_t r, int32_t b) {
        if (!info()) return false;
        ARect rect(l, t, r, b);
        if (!syms().regionAbiVerified) return false;
        unsigned char* region = (unsigned char*)info() + syms().regionOffset;
        if (syms().regionSet) {
            syms().regionSet(region, &rect);
            return true;
        }
        if (syms().regionClear && syms().addTouchableRegion) {
            syms().regionClear(region);
            syms().addTouchableRegion(info(), &rect);
            return true;
        }
        return false;
    }

    // Region members are available only after the runtime ABI guard passes.
    static bool hasRegionSetSymbol() { return resolve() && syms().regionSet != nullptr; }
    static bool hasRegionClearSymbol() { return resolve() && syms().regionClear != nullptr; }
    static bool hasRegionGetArraySymbol() {
        return resolve() && syms().regionGetArray != nullptr;
    }
    static std::string regionReplaceInfo() {
        resolve();
        auto& s = syms();
        return detail::fmtStr("Region ABI=%s offset=%zu set=%s clear=%s getArray=%s",
            s.regionAbiVerified ? "verified" : "rejected", s.regionOffset,
            s.regionSet ? "yes" : "no", s.regionClear ? "yes" : "no",
            s.regionGetArray ? "yes" : "no");
    }

    // Real replacement on the existing, correctly constructed Region. The
    // token/ref-counted WindowInfoHandle stays the same throughout a drag.
    bool replaceTouchableRegionRects(const std::vector<aurora::TouchRect>& rects) {
        if (!info() || !syms().regionAbiVerified || !syms().regionClear ||
            !syms().regionGetArray || !syms().addTouchableRegion) {
            // ★ 把"为什么拒绝"打出来：朋友那台机器上就是在这里返回 false，
            //   随后走进失败清理路径崩掉，现场只剩下"崩在清理里"，原因全丢了。
            detail::startupTrace("region replace 拒绝：%s", regionReplaceInfo().c_str());
            return false;
        }
        syms().regionClear((unsigned char*)info() + syms().regionOffset);
        for (const auto& r : rects) {
            if (r.left >= r.right || r.top >= r.bottom) return false;
            const ARect actual(r.left, r.top, r.right, r.bottom);
            syms().addTouchableRegion(info(), &actual);
        }
        const int32_t* actual = nullptr;
        const size_t count = readTouchableRegion(&actual);
        if (!actual || count > 16384) return false;
        std::vector<aurora::TouchRect> readback;
        for (size_t i = 0; i < count; ++i) {
            aurora::TouchRect r{actual[i*4],actual[i*4+1],actual[i*4+2],actual[i*4+3]};
            // Region::clear represents emptiness as one empty rectangle.
            if (r.left < r.right && r.top < r.bottom) readback.push_back(r);
        }
        return aurora::UnionTouchRects(readback) == aurora::UnionTouchRects(rects);
    }

    // 读回 touchableRegion 的全部矩形。返回矩形个数，rects 里每个是 4 个 int32。
    // 拿不到 getArray 符号时返回 0（调用方据此判断「无法校验」）。
    size_t readTouchableRegion(const int32_t** rects) const {
        if (rects) *rects = nullptr;
        if (!info() || !syms().regionAbiVerified || !syms().regionGetArray) return 0;
        size_t count = 0;
        const int32_t* a =
            (const int32_t*)syms().regionGetArray((const unsigned char*)info() + syms().regionOffset,
                                                  &count);
        if (rects) *rects = a;
        return count;
    }

    // inputConfig 直接按偏移写，**不调用 setInputConfig**。
    //
    // 原因（真机实测）：调用 `WindowInfo::setInputConfig(ftl::Flags<InputConfig>, bool)`
    // 会 SIGSEGV。`ftl::Flags<E>` 与 libutils 的 `sp<T>` 一样属于
    // "non-trivial for the purposes of calls" —— 按 AArch64 ABI 应经内存传递，
    // 而按 `uint32_t` 声明就成了寄存器传递，调用约定错位。
    // 而 `ftl::Flags<E>` 的底层就是一个 4 字节整数（AOSP 自己用
    // `static_assert(sizeof(inputConfig) == 4u)` 钉死），所以按偏移写一个 uint32 是
    // **语义等价且安全**的。
    // 默认值 0 = DEFAULT = visible + touchable + focusable，正是我们要的。
    void setInputConfigBits(uint32_t bits) { writeU32(winfo::kOffInputConfig, bits); }
    uint32_t inputConfigBits() const { return readU32(winfo::kOffInputConfig); }

    // token 是 sp<IBinder>（偏移 8）。**必须做引用计数配对**：
    // WindowInfo 析构会对它 decStrong，只写指针不加引用 = 引用计数错乱 → 崩。
    //
    // ★★ 不要直接对 `IBinder*` 调 `RefBase::incStrong` ★★
    // 依据（两条都是设备自己的机器码 / AOSP 头文件，不是推测）：
    //   1. research/aosp/src/native/libs/binder/include/binder/IBinder.h:52
    //        class ... IBinder : public virtual RefBase   ← 虚基类，RefBase 不在偏移 0
    //   2. 设备 libbinder.so `BpRefBase::BpRefBase(const sp<IBinder>&)` 的机器码：
    //        ldr  x8, [x0] ; ldur x8, [x8, #-0x18] ; add x0, x0, x8 ; bl RefBase::incStrong
    //      —— 编译器必须做这个 `add` 才能拿到 RefBase*（本机实测偏移 = 144）。
    // 直接调用会把 `BpBinder+8` 当成 `mRefs` 去加计数：既不增加真正的强引用计数，
    // 又踩坏 BpBinder 自己的字段；随后 WindowInfo 析构的 decStrong 会把一个从未加过的
    // 计数减到 0 → 提前 delete → use-after-free。
    bool setToken(void* binder) {
        if (!info() || !binder) return false;
        if (!syms().incStrong) return false;
        detail::vlog(
            "[aospglue] 警告：setToken() 直接对 IBinder* 调 RefBase::incStrong 是错的；"
            "请改用 setTokenOwned()。");
        syms().incStrong(binder, this);
        writePtr(winfo::kOffToken, binder);
        tokenSet_ = true;
        return true;
    }

    // ★ 推荐用法：**转移**一个已经存在的强引用（不额外 incStrong）。
    //
    // 典型来源是 `Parcel::readStrongBinder(sp<IBinder>*)` —— 它写回来的 sp<>
    // 已经持有一个强引用。把这个引用交给 WindowInfo 之后：
    //   +1 = readStrongBinder 给的（并且**那个 sp<> 不能再析构**）
    //   -1 = WindowInfo 析构时对 token 的 decStrong
    // 收支平衡，且不依赖任何虚基类偏移计算。
    void setTokenOwned(void* binder) {
        if (!info() || !binder) return;
        writePtr(winfo::kOffToken, binder);
        tokenSet_ = true;
    }
    bool tokenSet() const { return tokenSet_; }
    void* tokenRaw() const { return info() ? readPtr(winfo::kOffToken) : nullptr; }

    void setName(const char* v) { if (v) assignString(winfo::kOffName, v); }
    void setPackageName(const char* v) { if (v) assignString(winfo::kOffPackageName, v); }
    void setAppInfoName(const char* v) { if (v) assignString(winfo::kOffAppInfo + 16, v); }
    void setAppInfoTokenRaw(void* b) { writePtr(winfo::kOffAppInfo + 8, b); }
    void setAppInfoTimeoutMs(int64_t v) { writeI64(winfo::kOffAppInfo + 40, v); }

    void setId(int32_t v) { writeI32(winfo::kOffId, v); }
    void setDispatchingTimeoutNs(int64_t v) { writeI64(winfo::kOffDispTimeout, v); }
    void setGlobalScaleFactor(float v) { writeF32(winfo::kOffGlobalScale, v); }
    void setAlpha(float v) { writeF32(winfo::kOffAlpha, v); }
    void setSurfaceInset(int32_t v) { writeI32(winfo::kOffSurfaceInset, v); }
    void setOwnerPid(int32_t v) { writeI32(winfo::kOffOwnerPid, v); }
    void setOwnerUid(int32_t v) { writeI32(winfo::kOffOwnerUid, v); }
    void setDisplayId(int32_t v) { writeI32(winfo::kOffDisplayId, v); }
    void setLayoutParamsType(int32_t v) { writeI32(winfo::kOffLayoutType, v); }
    void setLayoutParamsFlags(uint32_t v) { writeU32(winfo::kOffLayoutFlags, v); }
    void setReplaceRegionWithCrop(bool v) { writeU8(winfo::kOffReplaceRegion, v ? 1 : 0); }
    // ★★ touchOcclusionMode 必须 = ALLOW(2) ★★
    // 默认 BLOCK_UNTRUSTED 会让全屏图层的 frame 把后面 app 的触摸判成不可信遮挡后整条丢弃
    // （真机上表现为「整屏滑动不了」）。写回读校验见 selfCheck()。
    void setTouchOcclusionMode(int32_t v) { writeI32(winfo::kOffTouchOcclusion, v); }
    int32_t touchOcclusionMode() const { return readI32(winfo::kOffTouchOcclusion); }
    void setContentSize(int32_t w, int32_t h) {
        writeI32(winfo::kOffContentSize, w);
        writeI32(winfo::kOffContentSize + 4, h);
    }

    // ABI 三件套的读回（selfCheck 用）
    int64_t dispatchingTimeoutNs() const { return readI64(winfo::kOffDispTimeout); }
    float globalScaleFactor() const { return readF32(winfo::kOffGlobalScale); }
    int32_t ownerPid() const { return readI32(winfo::kOffOwnerPid); }
    int32_t ownerUid() const { return readI32(winfo::kOffOwnerUid); }
    int32_t id() const { return readI32(winfo::kOffId); }

    // 三重 ABI 校验是否通过（create() 里已跑过一次；selfCheck 里再报一次）
    bool layoutValid() const { return info() != nullptr && validateLayout(); }

    // 给 WindowInfoHandle 自己加一个强引用（我们自己持有的那一份）。
    // apply() 之后 layer_state_t 会被 clear()，那时它会 decStrong 一次，
    // 所以我们要自己拿一份，保证 handle 在 apply() 之后仍然活着。
    bool addRef(const void* id) {
        if (!mem_ || !syms().incStrong) return false;
        // WindowInfoHandle : public RefBase（**非虚**继承，RefBase 在偏移 0）→ 直接调用正确
        syms().incStrong(mem_, id);
        return true;
    }

    void releaseRef(const void* id) {
        if (mem_ && syms().decStrong) syms().decStrong(mem_, id);
    }

    std::string describe() const {
        if (!info()) return "<未创建>";
        return detail::fmtStr(
            "id=%d gsf=%.2f alpha=%.2f disp=%lld pid=%d uid=%d cfg=0x%x displayId=%d "
            "occlusion=%d region@%zu shift=%+lld",
            readI32(winfo::kOffId), (double)readF32(winfo::kOffGlobalScale),
            (double)readF32(winfo::kOffAlpha), (long long)readI64(winfo::kOffDispTimeout),
            readI32(winfo::kOffOwnerPid), readI32(winfo::kOffOwnerUid),
            readU32(winfo::kOffInputConfig), readI32(winfo::kOffDisplayId),
            readI32(winfo::kOffTouchOcclusion), syms().regionOffset,
            static_cast<long long>(layoutShift_));
    }

private:
    struct Syms {
        bool ready = false, ok = false;
        void* libgui = nullptr;
        void (*ctor)(void*) = nullptr;
        void (*dtor)(void*) = nullptr;
        void (*addTouchableRegion)(void*, const ARect*) = nullptr;
        void* (*opNew)(size_t) = nullptr;
        void (*opDelete)(void*) = nullptr;
        void (*incStrong)(void*, const void*) = nullptr;
        void (*decStrong)(void*, const void*) = nullptr;
        void (*assignStr)(void*, const char*) = nullptr;
        // Region members are enabled only after verifying the device thunk.
        bool regionAbiVerified = false;
        size_t regionOffset = 0;
        void (*regionSet)(void*, const ARect*) = nullptr;
        void (*regionClear)(void*) = nullptr;
        const void* (*regionGetArray)(const void*, size_t*) = nullptr;
    };
    static Syms& syms() {
        static Syms s;
        return s;
    }

    // 运行时 ABI 校验。判据与 TouchInterceptor.h:339-361 完全一致：
    //   1. WindowInfo 的 vptr 必须是代码地址（非 0、8 字节对齐、落在高位映射区）
    //   2. dispatchingTimeout 默认值必须是 5 秒（5000000000 ns）—— 与布局强相关的已知默认值
    //   3. globalScaleFactor 默认值必须是 1.0f
    // 三条都过才算布局对；否则拒绝操作。
    bool validateLayout() const {
        if (!info()) return false;
        uintptr_t vptr = 0;
        std::memcpy(&vptr, info(), 8);
        if (vptr == 0 || (vptr & 7) != 0) return false;
        if (vptr < 0x100000000ULL || vptr > 0x0000FFFFFFFFFFFFULL) return false;
        const int64_t disp = readI64(winfo::kOffDispTimeout);
        if (disp != 5000000000LL) {
            detail::vlog("[aospglue] dispatchingTimeout 读回 %lld（期望 5000000000）",
                         (long long)disp);
            return false;
        }
        const float gsf = readF32(winfo::kOffGlobalScale);
        if (!(gsf > 0.99f && gsf < 1.01f)) {
            detail::vlog("[aospglue] globalScaleFactor 读回 %.4f（期望 1.0）", (double)gsf);
            return false;
        }
        return true;
    }

    // 给 WindowInfo 里的 std::string 赋值。
    // **不复刻 std::string 内部布局** —— 调真实的 std::string::assign。
    // 拿不到符号就跳过（name 只影响 dumpsys，空串时返回 "<invalid>"，无害）。
    void assignString(size_t off, const char* value) {
        if (!syms().assignStr) return;
        syms().assignStr((unsigned char*)info() + eff(off), value);
    }

    // ★★ WindowInfo 的字段访问**一律经过 eff()**：Region 及其之后的字段会因 OEM
    //    插入而整体后移（本机 +64，纯 AOSP 布局为 0），位移由设备自己的
    //    addTouchableRegion 指令现场解出。写死偏移 = 换台机器就往错的地方写。
    size_t eff(size_t off) const {
        return aurora::ShiftWindowInfoOffset(off, layoutShift_);
    }

    void writePtr(size_t off, void* v) { std::memcpy((unsigned char*)info() + eff(off), &v, 8); }
    void writeI32(size_t off, int32_t v) { std::memcpy((unsigned char*)info() + eff(off), &v, 4); }
    void writeU32(size_t off, uint32_t v) { std::memcpy((unsigned char*)info() + eff(off), &v, 4); }
    void writeI64(size_t off, int64_t v) { std::memcpy((unsigned char*)info() + eff(off), &v, 8); }
    void writeF32(size_t off, float v) { std::memcpy((unsigned char*)info() + eff(off), &v, 4); }
    void writeU8(size_t off, uint8_t v) { *((unsigned char*)info() + eff(off)) = v; }

    int32_t readI32(size_t off) const {
        int32_t v = 0;
        std::memcpy(&v, (const unsigned char*)info() + eff(off), 4);
        return v;
    }
    uint32_t readU32(size_t off) const {
        uint32_t v = 0;
        std::memcpy(&v, (const unsigned char*)info() + eff(off), 4);
        return v;
    }
    int64_t readI64(size_t off) const {
        int64_t v = 0;
        std::memcpy(&v, (const unsigned char*)info() + eff(off), 8);
        return v;
    }
    float readF32(size_t off) const {
        float v = 0;
        std::memcpy(&v, (const unsigned char*)info() + eff(off), 4);
        return v;
    }
    void* readPtr(size_t off) const {
        void* v = nullptr;
        std::memcpy(&v, (const unsigned char*)info() + eff(off), 8);
        return v;
    }

    // ★ 位移后的字段**写回读校验**：既证明位移对，也保证后面的写入不会写错地方。
    //   在 create() 里建完对象后跑一次；任何一项读不回来就拒绝使用这个 handle
    //   （宁可拦截失效，也不写坏系统内存 —— 朋友机器上那次崩溃就是写错了地方）。
    bool verifyShiftedFields() {
        if (!info()) return false;
        const uint32_t saved = readU32(winfo::kOffInputConfig);
        writeU32(winfo::kOffInputConfig, 0xA5A50000u);
        const bool ok = readU32(winfo::kOffInputConfig) == 0xA5A50000u;
        writeU32(winfo::kOffInputConfig, saved);
        return ok && readU32(winfo::kOffInputConfig) == saved;
    }

    void* mem_ = nullptr;
    bool tokenSet_ = false;
    // Region 及其之后字段的整体位移（设备实测 − AOSP 184）。未解出时为 0，
    // 但那时 regionAbiVerified 为假，create() 会直接拒绝，不会走到写入。
    ptrdiff_t layoutShift_ = 0;
    bool layoutShiftKnown_ = false;
};

#endif  // AOSPGLUE_PLATFORM —— 以上是平台相关部分（libgui/binder/WindowInfoHandle）

// ===========================================================================
// 触摸报文（InputMessage）—— 纯解析核心，PC 上也能编译
// ===========================================================================
// 布局逐字校对 research/aosp/src/native/include/input/InputTransport.h:64-225
// （android16-release）。**不要按「轴号直索引」读坐标**：PointerCoords 是
// bitset + 稀疏数组（Input.h:500-556），未置位的轴返回 0。
//
//   Header { Type type; uint32_t seq; }                                   @0  (8B)
//   Body::Motion {
//     int32_t  eventId                                                     @8
//     uint32_t pointerCount                                                @12
//     nsecs_t  eventTime            (aligned 8)                            @16
//     int32_t  deviceId                                                    @24
//     int32_t  source                                                      @28
//     int32_t  displayId                                                   @32
//     uint8_t  hmac[32]                                                    @36
//     int32_t  action                                                      @68
//     int32_t  actionButton                                                @72
//     int32_t  flags                                                       @76
//     int32_t  metaState                                                   @80
//     int32_t  buttonState                                                 @84
//     MotionClassification classification (uint8) + uint8_t empty2[3]      @88
//     int32_t  edgeFlags                                                   @92
//     nsecs_t  downTime             (aligned 8)                            @96
//     float dsdx,dtdx,dtdy,dsdy,tx,ty        （窗口变换）                   @104
//     float xPrecision,yPrecision,xCursorPosition,yCursorPosition          @128
//     float dsdxRaw,dtdxRaw,dtdyRaw,dsdyRaw,txRaw,tyRaw （raw 变换）        @144
//     struct Pointer pointers[]  (aligned 8)                               @168
//   }
//   struct Pointer { PointerProperties properties; PointerCoords coords; } = 144B
//     properties: int32 id @0, int32 toolType @4
//     coords:     uint64 bits @8, float values[30] @16, bool isResampled @136, empty[7]
//
// ⇒ 1 指 MOTION = 168 + 144 = **312 字节**（与真机实测的 312 完全一致，
//    这也是本布局表最强的交叉验证）。
constexpr uint32_t kMsgTypeKey = 0;
constexpr uint32_t kMsgTypeMotion = 1;
constexpr uint32_t kMsgTypeFinished = 2;

constexpr size_t kOffMsgType = 0;
constexpr size_t kOffMsgSeq = 4;
constexpr size_t kOffMotionPointerCount = 12;
constexpr size_t kOffMotionAction = 68;
constexpr size_t kOffMotionEventTime = 16;
constexpr size_t kOffMotionDeviceId = 24;
constexpr size_t kOffMotionSource = 28;
constexpr size_t kOffMotionDisplayId = 32;
constexpr size_t kOffMotionDsdx = 104;
constexpr size_t kOffMotionDtdx = 108;
constexpr size_t kOffMotionDtdy = 112;
constexpr size_t kOffMotionDsdy = 116;
constexpr size_t kOffMotionTx = 120;
constexpr size_t kOffMotionTy = 124;
constexpr size_t kOffMotionPointers = 168;

constexpr size_t kPointerBytes = 144;
constexpr size_t kOffPointerId = 0;
constexpr size_t kOffPointerToolType = 4;
constexpr size_t kOffPointerCoords = 8;
constexpr size_t kOffPointerCoordsBits = 0;
constexpr size_t kOffPointerCoordsValues = 8;

constexpr size_t kMaxAxes = 30;      // Input.h:501  enum { MAX_AXES = 30 }
constexpr size_t kMaxPointers = 16;  // Input.h:196  static constexpr size_t MAX_POINTERS = 16

constexpr size_t kMotionFixedBytes = kOffMotionPointers;                            // 168
constexpr size_t kMessageMaxBytes = kMotionFixedBytes + kPointerBytes * kMaxPointers;  // 2472

// ★ FINISHED 回复必须是 24 字节：Header(8) + Body::Finished(16)
//   Body::Finished { bool handled @0; uint8_t empty[7]; nsecs_t consumeTime @8; } = 16B
//   （InputTransport.h:169-175；构造方式见 InputConsumer.cpp:781-787）
constexpr size_t kFinishedBytes = 24;

// action 常量（NDK 公开头 android/input.h，本机 sysroot 逐行核过）
constexpr int32_t kActionDown = 0;
constexpr int32_t kActionUp = 1;
constexpr int32_t kActionMove = 2;
constexpr int32_t kActionCancel = 3;
constexpr int32_t kActionPointerDown = 5;
constexpr int32_t kActionPointerUp = 6;
constexpr int32_t kActionMask = 0xff;
constexpr int32_t kActionPointerIndexShift = 8;
constexpr int32_t kActionPointerIndexMask = 0xff00;

// 轴号（NDK android/input.h:425/440/453/464）
constexpr int32_t kAxisX = 0;
constexpr int32_t kAxisY = 1;
constexpr int32_t kAxisPressure = 2;
constexpr int32_t kAxisSize = 3;

// toolType（NDK android/input.h:874  AMOTION_EVENT_TOOL_TYPE_FINGER = 1）
constexpr int32_t kToolTypeFinger = 1;

struct Touch {
    int action;       // 0=DOWN 1=UP 2=MOVE 3=CANCEL 5=POINTER_DOWN 6=POINTER_UP（掩码后的值）
    int actionIndex;  // action 的高 8 位（哪个 pointer index 是 action 目标）
    int pointerId;
    float x, y;  // ★ 已按报文里的 transform 变换到图层局部坐标
    float pressure, size;
    int toolType;
    long long eventTimeNanos;
};

using TouchFn = void (*)(void* user, const Touch* touches, int count, int action, int actionIndex);

namespace detail {

// --- 小端读取（aarch64 上就是 memcpy，保持可移植写法）---
inline uint32_t rdU32(const uint8_t* p) {
    uint32_t v = 0;
    memcpy(&v, p, 4);
    return v;
}
inline int32_t rdI32(const uint8_t* p) {
    int32_t v = 0;
    memcpy(&v, p, 4);
    return v;
}
inline int64_t rdI64(const uint8_t* p) {
    int64_t v = 0;
    memcpy(&v, p, 8);
    return v;
}
inline float rdF32(const uint8_t* p) {
    float v = 0;
    memcpy(&v, p, 4);
    return v;
}
inline uint64_t rdU64(const uint8_t* p) {
    uint64_t v = 0;
    memcpy(&v, p, 8);
    return v;
}

// --- PointerCoords 的 bitset 稀疏取值 ---
// 逐字对应 AOSP：
//   utils/BitSet.h:186  BitSet64::valueForBit(n)  = 0x8000000000000000ULL >> n
//   utils/BitSet.h:213  BitSet64::hasBit(v, n)    = v & valueForBit(n)
//   utils/BitSet.h:283  BitSet64::getIndexOfBit(v, n)
//                          = popcount(v & ~(0xffffffffffffffffULL >> n))
//   Input.cpp           PointerCoords::getAxisValue(axis)
//                          = hasBit ? values[getIndexOfBit(bits, axis)] : 0
// ★ 注意位序是「MSB 优先」：AXIS_X(=0) 对应最高位。(#[未核实-3])
inline bool axisIsSet(uint64_t bits, int32_t axis) {
    return (bits & (0x8000000000000000ULL >> axis)) != 0;
}
inline int32_t axisIndex(uint64_t bits, int32_t axis) {
    return (int32_t)__builtin_popcountll(bits & ~(0xffffffffffffffffULL >> axis));
}
inline float axisValue(const uint8_t* coords, int32_t axis) {
    if (axis < 0 || axis > 63) return 0.0f;
    const uint64_t bits = rdU64(coords + kOffPointerCoordsBits);
    if (!axisIsSet(bits, axis)) return 0.0f;
    const int32_t index = axisIndex(bits, axis);
    if (index < 0 || (size_t)index >= kMaxAxes) return 0.0f;
    return rdF32(coords + kOffPointerCoordsValues + (size_t)index * 4);
}

// 构造 24 字节 FINISHED 报文（**纯函数**，可单测）。
// ★★ 16 字节版本在 Android 16 上非法：InputMessage::isValid()（InputTransport.cpp:113-117）
//    会 ALOGE("Received message of incorrect size ...") 并返回 BAD_VALUE，
//    ⇒ 该回复被丢弃 ⇒ 窗口 5 秒后被判 not responsive，后续触摸直接被丢。
//
// 布局逐字来自 input/InputTransport.h:169-175：
//   Header             { Type type; uint32_t seq; }                        @0  (8B)
//   Body::Finished     { bool handled; uint8_t empty[7]; nsecs_t consumeTime; } @8 (16B)
//   ⇒ sizeof = 24。构造方式照抄 InputConsumer.cpp:781-787（sendUnchainedFinishedSignal）。
inline void buildFinished(uint32_t seq, uint8_t out[kFinishedBytes], int64_t consumeTimeNs) {
    memset(out, 0, kFinishedBytes);
    const uint32_t type = kMsgTypeFinished;
    memcpy(out + kOffMsgType, &type, 4);
    memcpy(out + kOffMsgSeq, &seq, 4);
    out[8] = 1;  // body.finished.handled = true（handled=true 才不会被转给后面的窗口）
    memcpy(out + 16, &consumeTimeNs, 8);  // body.finished.consumeTime（只喂延迟统计）
}

// 解析一条 MOTION 报文，把 pointerCount 个触点**全部**填进 out[0..pc-1]。
// 长度必须**正好**等于 168 + 144*pointerCount —— 与 AOSP 的判据一致
// （InputTransport.cpp:113-117 InputMessage::isValid：`size() != actualSize` 即非法），
// 而 SOCK_SEQPACKET 保证一次 read 正好一条报文。
inline bool parseMotionMessage(const uint8_t* buf, size_t n, Touch* out, int maxOut, int* outCount,
                               int* outAction, int* outActionIndex) {
    if (n < kMotionFixedBytes) return false;
    const uint32_t pc = rdU32(buf + kOffMotionPointerCount);
    if (pc == 0 || pc > kMaxPointers) return false;
    const size_t need = kMotionFixedBytes + kPointerBytes * (size_t)pc;
    if (n != need) return false;
    if (out == nullptr || maxOut < (int)pc) return false;

    const int32_t rawAction = rdI32(buf + kOffMotionAction);
    const int32_t masked = rawAction & kActionMask;
    const int32_t idx = (rawAction & kActionPointerIndexMask) >> kActionPointerIndexShift;
    const int64_t eventTime = rdI64(buf + kOffMotionEventTime);

    // 坐标变换（ui::Transform）：x' = dsdx*x + dtdx*y + tx ; y' = dtdy*x + dsdy*y + ty
    const float dsdx = rdF32(buf + kOffMotionDsdx);
    const float dtdx = rdF32(buf + kOffMotionDtdx);
    const float dtdy = rdF32(buf + kOffMotionDtdy);
    const float dsdy = rdF32(buf + kOffMotionDsdy);
    const float tx = rdF32(buf + kOffMotionTx);
    const float ty = rdF32(buf + kOffMotionTy);

    for (uint32_t i = 0; i < pc; ++i) {
        // ★ 触点数组**不在固定偏移**：按 pointerCount 与 Pointer=144B 步进
        const uint8_t* p = buf + kOffMotionPointers + kPointerBytes * (size_t)i;
        const uint8_t* coords = p + kOffPointerCoords;
        // ★ PointerCoords 是 bitset + 稀疏数组：未置位的轴返回 0，不能按轴号直索引
        const float x = axisValue(coords, kAxisX);
        const float y = axisValue(coords, kAxisY);
        Touch& t = out[i];
        t.action = masked;
        t.actionIndex = idx;
        t.pointerId = rdI32(p + kOffPointerId);
        t.x = dsdx * x + dtdx * y + tx;
        t.y = dtdy * x + dsdy * y + ty;
        t.pressure = axisValue(coords, kAxisPressure);  // 未置位 = 0
        t.size = axisValue(coords, kAxisSize);          // 未置位 = 0
        t.toolType = rdI32(p + kOffPointerToolType);
        t.eventTimeNanos = (long long)eventTime;
    }
    *outCount = (int)pc;
    *outAction = masked;
    *outActionIndex = idx;
    return true;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// parseInputMessage —— 公开的纯解析入口（不读 fd、不写 fd、无副作用）
// ---------------------------------------------------------------------------
// 把一段 InputMessage 字节解析成触点，便于用合成报文做单元测试
// （PC 上也能编、能跑，见 root-host/elf/aosp_glue_selftest.cpp）。
//
//   buf/len     : 一段完整的 InputMessage（含 8 字节 Header）
//   out/maxOut  : 触点输出数组与容量
//   outCount    : 解出的触点个数（非 MOTION 报文时为 0）
//   outAction / outActionIndex : action 与其高 8 位的 pointer index
//
// 返回 false = 报文非法：
//     * len < 8 或 buf == nullptr
//     * MOTION 的长度 != 168 + 144*pointerCount（含「差 1 字节」这种截断）
//     * pointerCount == 0 或 > 16
//     * out 容量不足
// 非 MOTION 报文（KEY/FINISHED/FOCUS/...）返回 **true** 且 *outCount = 0
// —— 由调用方决定要不要回 FINISHED（pollTouches 的选择是「一律回」，见其注释）。
inline bool parseInputMessage(const uint8_t* buf, size_t len, Touch* out, int maxOut, int* outCount,
                              int* outAction, int* outActionIndex) {
    if (outCount) *outCount = 0;
    if (outAction) *outAction = -1;
    if (outActionIndex) *outActionIndex = -1;
    if (buf == nullptr || len < 8) return false;
    const uint32_t type = detail::rdU32(buf + kOffMsgType);
    if (type != kMsgTypeMotion) return true;
    return detail::parseMotionMessage(buf, len, out, maxOut, outCount, outAction, outActionIndex);
}

#if AOSPGLUE_PLATFORM

namespace detail {

// 发送 24 字节 FINISHED。不 close(fd)（fdsan 会 abort）；写失败只记日志，不抛。
inline bool writeFinished(int fd, uint32_t seq) {
    uint8_t out[kFinishedBytes];
    buildFinished(seq, out, nowNs());
    errno = 0;
    ssize_t w = ::write(fd, out, sizeof out);
    if (w != (ssize_t)sizeof out) {
        vlog("  FINISHED 写失败：seq=%u write=%zd errno=%d %s", seq, w, errno, strerror(errno));
        return false;
    }
    return true;
}

}  // namespace detail

// ===========================================================================
// Overlay
// ===========================================================================
class Overlay {
public:
    // ★★ 必须显式初始化这两个 union 成员 —— 这里踩过一个极难归因的坑。
    //
    //   `StrongPointer` 是**没有构造函数的裸 union**（为了让 `sizeof` > 16 从而走
    //   AArch64 的 sret 间接返回，对齐 `sp<T>` 的调用约定，所以刻意保持 POD）。
    //   代价是：它作为**类成员**被默认初始化时，内容是**未定义的**。
    //
    //   原来写的是 `Overlay() = default;`，而 `surface_.pointer = nullptr;` 只在
    //   destroy() 里做 ⇒ 刚构造出来的 Overlay 里 surface_.pointer 是垃圾。
    //   于是 window() 的这一句：
    //       if (surface_.get() == nullptr) surface_ = detail::syms().getSurface(...);
    //   因为垃圾值**恰好非 null** 而**整句跳过**，永远返回「垃圾 + 16」。
    //
    //   真机表现（浪费了整轮排查）：eglCreateWindowSurface 拿到野指针 → SIGBUS，
    //   而崩溃栈**整条落在动态链接器里**：
    //       #00 linker64          solist_add_soinfo+16
    //       #07 libvndksupport.so android_load_sphal_library
    //       #08 libEGL.so         android::load_system_driver
    //   看起来完全像「厂商 GPU 驱动的链接器问题」，与真实原因（一个未初始化的成员）
    //   毫无关系。锁定它的关键证据：对同一符号**直接调用**得 0xb400007698a94200
    //   （合法堆指针），而 window() 返回 0x7750f677a0（栈地址）；前者 +16 命中
    //   `"_wnd"` 魔数，且 EGL 全链路成功（GL_RENDERER = Adreno (TM) 830）。
    //
    //   教训：**凡是给 `StrongPointer` 当成员用的地方，都必须显式置空。**
    Overlay() {
        layer_.pointer = nullptr;
        surface_.pointer = nullptr;
    }
    ~Overlay() { destroy(); }
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;

    // 建图层（含 createSurface 必须重试：判据 getHandle() != nullptr，最多 4 次）。
    //
    // bufferLayer=true （默认，对应锁定签名 create(name,w,h,layerStack)）：
    //     建 w×h 的 buffer 图层（flags=0），给 EGL / 渲染后端用。
    //     —— 这条路在真机验证过（probe_m3_ytbl.cpp:427-451，3000 帧 95.3fps）。
    // bufferLayer=false：
    //     建 eFXSurfaceEffect 图层（buf 尺寸必须 0×0，flags=0x00020000），
    //     不渲染任何像素，只做触摸拦截。
    //     —— 这条路在真机端到端验证过（probe_touch_intercept.cpp「无 buffer 也进窗口列表」）。
    //
    // 两条路的输入窗口注册门槛都能过：
    //   LayerSnapshot::canReceiveInput()（LayerSnapshot.cpp:267-269）
    //     = !isHiddenByPolicy() && (!hasBufferOrSidebandStream() || color.a > 0.0f)
    //   ⇒ buffer 图层只要 alpha > 0 就行（我们设 1.0）；effect 图层走 !hasBuffer 分支，恒真。
    bool create(const char* name, int w, int h, int layerStack, bool bufferLayer = true) {
        error_.clear();
        if (!detail::resolveAll(&error_)) return false;
        if (name == nullptr || name[0] == '\0') {
            error_ = "create: name 不能为空（空名字的窗口在输入系统里无法归因）";
            return false;
        }
        if (layerStack <= 0) {
            // 0 = 物理屏。这里不禁止（宿主可能就是要在物理屏上跑），但必须显式知道代价：
            // 物理屏上的可触窗口会真的吃掉用户屏幕上的触摸。
            detail::vlog("  [aospglue] 注意：layerStack=%d（0 = 物理屏，会真的拦截用户触摸）",
                         layerStack);
        }

        static detail::Client client;
        if (!client.valid) {
            error_ = detail::fmtStr("SurfaceComposerClient 连接失败：initCheck=%d", client.status);
            return false;
        }

        // ★★ 图层类型必须是 eFXSurfaceEffect（不是 buffer 图层）★★
        // 依据 AOSP 自己的端到端输入测试
        // （research/aosp/.../libs__gui__tests__EndToEndNativeInputTest.cpp:144-171）：
        //     scc->createSurface(String8(name), 0, 0, PIXEL_FORMAT_RGBA_8888,
        //                        ISurfaceComposerClient::eFXSurfaceEffect);
        // 枚举值来自 gitiles 抓的 ISurfaceComposerClient.aidl（android16-release）：
        //     eFXSurfaceBufferQueue = 0x00000000
        //     eFXSurfaceEffect      = 0x00020000   ← 用这个
        const uint32_t kSurfaceTypeEffect = 0x00020000u;

        // ★★★ eSkipScreenshot = 0x00000040 —— 让 screencap **不要抓到我们自己的图层**
        //
        // 这是防自反馈的**正道**，取代「CPU 侧抹除 + 环填充」那条补丁路：
        //   抓屏抓到的画面里包含我们自己刚画上去的玻璃 ⇒ 拿去当 backdrop 折射
        //   ⇒ 正反馈无限镜像。CPU 抹除能止住反馈，但**抹掉的是真桌面**
        //   （用环填充糊上一团涂抹），玻璃折射出来就是"扭曲拉伸"的假背景
        //   —— 用户真机实测报告原话：「捕捉到的背景完全扭曲拉伸了」。
        //   设了这个 flag，screencap 直接把我们排除在外 ⇒ 抓到的是**玻璃背后
        //   真实的桌面** ⇒ 既无自反馈，背景也是真的。
        //
        // 值来源（都是本机抓的 AOSP 源码，android16-qpr1-release）：
        //   libs/gui/aidl/android/gui/ISurfaceComposerClient.aidl:31
        //       const int eSkipScreenshot = 0x00000040;
        //   （同一文件 :29 eHidden = 0x00000004，与 SurfaceControl.java:740 的
        //     HIDDEN = 0x4 一致，说明这套值与 Java 侧是同一套）
        //   映射关系见 services/surfaceflinger/FrontEnd/RequestedLayerState.cpp:83-88
        //       if (args.flags & ISurfaceComposerClient::eSkipScreenshot)
        //           flags |= layer_state_t::eLayerSkipScreenshot;   // = 0x40
        //
        // ★ 会不会影响我们已经验收的**触摸拦截**？**不会**，已逐层核实
        //   （research/aosp/ANDROID16_INPUT_INTERCEPT_REPORT.md §2.5）：
        //   - 它唯一的去向是 `LayerFilter::toInternalDisplay`（截屏/录屏/镜像过滤）。
        //     `git grep toInternalDisplay` 全部 9 处使用点里**没有任何一处进入 inputInfo**。
        //   - input 侧的可见性判据是
        //     `LayerSnapshotBuilder.cpp:270-280`
        //         visibleForInput = hasInputInfo() ? canReceiveInput() : isVisible;
        //     我们的图层**有 inputInfo**（设了 InputWindowInfo）⇒ 走 canReceiveInput()，
        //     `isVisible` 那条路根本不被读。
        const uint32_t kSkipScreenshot = 0x00000040u;

        // ★ 2026-10-13 用户真机报告（原话）：
        //     「手机的系统录屏和截屏都截不到这个窗口」
        //   真机 A/B 实测确认根因就在这个 flag（同一屏、同一位置采样）：
        //       基线（无悬浮窗） (594,1200)=17 2 3   (500,2000)=16 8 33
        //       带 flag           (594,1200)=17 2 3   (500,2000)=16 8 33  ← 完全相同
        //       不带 flag         (594,1200)=15 14 14 (500,2000)=15 14 16 ← 出现了
        //   即：被 skip-screenshot 排除的图层，在**所有** capture 消费者眼里
        //   （系统截屏 / 系统录屏 / screencap / screenrecord / 投屏）都不存在。
        //   它是防自反馈的手段，但代价正是用户要的功能 —— 截不到、录不到。
        //
        // ★ 现在的取舍：**默认可被截屏/录屏拍到**（用户明确要的就是这个）。
        //   真机实测该模式下画面正常：玻璃仍透出背后内容，没有失控递归色块。
        //   若某台设备上出现自反馈（玻璃里套玻璃、逐帧放大），
        //   设环境变量 AURORA_SKIP_SCREENSHOT=1 切回防自反馈模式
        //   —— 代价是系统截屏/录屏又会拍不到本窗口。
        const bool skipScreenshot = (::getenv("AURORA_SKIP_SCREENSHOT") != nullptr);
        if (skipScreenshot) {
            detail::vlog("  ★ AURORA_SKIP_SCREENSHOT 已设：本次带上 eSkipScreenshot"
                         "（防自反馈，但系统截屏/录屏将拍不到本窗口）");
        }

        const uint32_t flags = (bufferLayer ? 0u : kSurfaceTypeEffect) |
                               (skipScreenshot ? kSkipScreenshot : 0u);
        const uint32_t bw = bufferLayer ? (uint32_t)w : 0u;
        const uint32_t bh = bufferLayer ? (uint32_t)h : 0u;

        layer_ = client.create(name, bw, bh, flags);
        if (!layer_.get()) {
            error_ = "图层创建失败（4 次 createSurface 都没有拿到真句柄）";
            return false;
        }
        // 再确认一次：拿空壳提交事务时 SF 当空操作、apply() 还返回 0（假成功）
        if (!hasRealHandle()) {
            error_ = "createSurface 返回的是空壳（getHandle()==nullptr）—— 拒绝继续";
            layer_.pointer = nullptr;
            return false;
        }
        name_ = name;
        w_ = w;
        h_ = h;
        layerStack_ = layerStack;
        layerId_ = detail::syms().getLayerId ? detail::syms().getLayerId(layer_.get()) : -1;
        detail::vlog("  图层 \"%s\" SurfaceControl=%p layerId=%d", name, layer_.get(), layerId_);
        return true;
    }

    // Find the real ANativeWindow header in the held Surface object. Kernel
    // reads make a bad/incompatible pointer a reported failure, not SIGSEGV.
    ANativeWindow* window() const {
        if (!layer_.get() || !detail::syms().getSurface) return nullptr;
        if (surface_.get() == nullptr) {
            detail::startupTrace("surface.getSurface begin");
            surface_ = detail::syms().getSurface(layer_.get());
            detail::startupTrace("surface.getSurface returned=%p", surface_.get());
        }
        if (surface_.get() == nullptr) return nullptr;
        if (windowOffset_ < 0) {
            std::array<std::byte, 256> bytes{};
            uintptr_t address = reinterpret_cast<uintptr_t>(surface_.get());
#if defined(__aarch64__)
            address &= 0x00ffffffffffffffULL; // Kernel address lookup strips the heap's top-byte tag.
#endif
            struct iovec local{bytes.data(), bytes.size()};
            struct iovec remote{reinterpret_cast<void*>(address), bytes.size()};
            ssize_t count = process_vm_readv(getpid(), &local, 1, &remote, 1, 0);
            if (count <= 0) {
                const int fd = ::open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
                if (fd >= 0) {
                    count = ::pread(fd, bytes.data(), bytes.size(), static_cast<off_t>(address));
                    ::close(fd);
                }
            }
            if (count > 0) windowOffset_ = aurora::FindNativeWindowOffset(
                std::span<const std::byte>(bytes.data(), static_cast<size_t>(count)));
            detail::startupTrace("nativeWindow.header bytes=%zd offset=%d", count, windowOffset_);
        }
        if (windowOffset_ < 0) return nullptr;
        return reinterpret_cast<ANativeWindow*>(reinterpret_cast<uintptr_t>(surface_.get()) + windowOffset_);
    }

    // 诊断用访问器（供 --probe-egl 做返回约定的 A/B 测试）。
    // 调用前请先调一次 window() 以确保 surface_ 已填充。
    void* rawLayer() const { return layer_.get(); }
    void* rawSurface() const { return surface_.get(); }

    // 输入窗口：IInputFlinger.createInputChannel → fd + token → WindowInfoHandle。
    // 字段序列照抄 probe_touch_intercept.cpp:847-867，只加了 touchOcclusionMode=ALLOW(2)
    // （见文件头 G 条；原探针没设这一项，本项目真机上因此出过「整屏滑动不了」）。
    bool attachInputWindow(const char* name, int regionW, int regionH) {
        error_.clear();
        if (!layer_.get()) {
            error_ = "attachInputWindow: 还没有建图层";
            return false;
        }
        if (wi_ != nullptr) {
            // 一个 token 只能被一个 handle 拥有：重复 attach 会让两个 handle 抢同一个强引用，
            // 后析构的那个 decStrong 会打到已经归零的计数上 → 提前释放。
            // 要换 region 请用 updateTouchableRegionRect()。
            error_ = "attachInputWindow: 已经 attach 过了（用 updateTouchableRegionRect 改区域）";
            return false;
        }
        const char* chanName = (name && name[0]) ? name : name_.c_str();
        if (chanName == nullptr || chanName[0] == '\0') {
            error_ = "attachInputWindow: 通道名不能为空";
            return false;
        }
        if (regionW <= 0 || regionH <= 0) {
            error_ = detail::fmtStr("attachInputWindow: 区域尺寸非法 %dx%d", regionW, regionH);
            return false;
        }

        // ---- 拿输入通道（必须在任何降权之前：inputflinger 只放行 uid 0 / 2000）----
        std::string chanWhy;
        detail::InputChannelResult ic = detail::createInputChannel(chanName, &chanWhy);
        if (!ic.ok) {
            error_ = "输入通道获取失败：" + chanWhy;
            return false;
        }
        // ★ fd 一律不 close（fdsan 会 abort）
        fd_ = ic.fd;
        token_ = ic.token;

        // ---- 构造 WindowInfoHandle（含运行时 ABI 三重校验）----
        wi_ = new WindowInfoHandle();  // 故意不 delete：RefBase 管生命周期（见 destroy() 注释）
        if (!wi_->create()) {
            error_ = "WindowInfoHandle::create() 失败（ABI 校验不过）—— 拒绝继续";
            wi_ = nullptr;
            return false;
        }

        wi_->setTokenOwned(ic.token);  // ← 转移 readStrongBinder 的强引用（不要 incStrong）
        wi_->setTouchableRegion(regionW, regionH);  // ← 图层局部坐标 Rect(0,0,W,H)
        wi_->setId(4242);
        wi_->setName(chanName);
        wi_->setPackageName("android");  // 照抄探针（只影响 dumpsys）
        wi_->setOwnerPid(getpid());
        wi_->setOwnerUid(getuid());  // ★ 保持 uid 0，不要降权到 1026
        wi_->setGlobalScaleFactor(1.0f);
        wi_->setAlpha(1.0f);
        wi_->setDispatchingTimeoutNs(5000000000LL);
        wi_->setContentSize(w_ > 0 ? w_ : regionW, h_ > 0 ? h_ : regionH);
        wi_->setSurfaceInset(0);
        wi_->setReplaceRegionWithCrop(false);
        // ★★ 必须 ALLOW(2) ★★
        wi_->setTouchOcclusionMode(winfo::kTouchOcclusionAllow);
        // 0 = DEFAULT = visible + touchable + focusable（照抄探针 full 模式）
        wi_->setInputConfigBits(winfo::kCfgDefault);

        regionL_ = 0;
        regionT_ = 0;
        regionR_ = regionW;
        regionB_ = regionH;
        regionRects_ = {{0,0,regionW,regionH}};

        // 我们自己拿一份强引用：apply() 内部会 clear() 掉 layer_state_t，那时它会 decStrong
        if (!wi_->addRef(&infoSp_)) {
            error_ = "缺 RefBase::incStrong —— apply() 之后 handle 会失效，拒绝继续";
            return false;
        }
        infoSp_.pointer = wi_->raw();

        if (!applyGeometryAndInput(true)) {
            return false;
        }
        active() = this;
        attached_ = true;
        return true;
    }

    // 只改 region（同一笔事务里带几何），用于面板移动 / 缩放后重设。
    bool updateTouchableRegion(int regionW, int regionH) {
        return updateTouchableRegionRect(0, 0, regionW, regionH);
    }

    // Replace the exact union of the visible UI shapes in layer-local pixels.
    // Empty input means no touch interception. The same WindowInfo/token is
    // retained; neither Region memory nor binder reference counts are copied.
    bool updateTouchableRegionRects(const std::vector<aurora::TouchRect>& requested, bool forceCommit = false) {
        error_.clear();
        if (!wi_ || !attached_) {
            error_ = "updateTouchableRegionRects: input window is not attached";
            return false;
        }
        for (const auto& r : requested) {
            if (r.left >= r.right || r.top >= r.bottom) {
                error_ = "updateTouchableRegionRects: invalid half-open rectangle";
                failOpenTouchInput();
                return false;
            }
        }
        const auto rects = aurora::UnionTouchRects(requested);
        if (rects == regionRects_ && !forceCommit) return true;
        if (!wi_->replaceTouchableRegionRects(rects)) {
            error_ = "updateTouchableRegionRects: Region ABI/symbol/readback verification failed";
            failOpenTouchInput();
            return false;
        }
        // SF sees a serialized copy, so modifying the local Region alone is
        // insufficient. Re-submit input info on every changed shape snapshot.
        wi_->setInputConfigBits(winfo::kCfgDefault);
        if (!applyGeometryAndInput(true)) {
            failOpenTouchInput();
            return false;
        }
        regionRects_ = rects;
        if (rects.empty()) regionL_ = regionT_ = regionR_ = regionB_ = 0;
        else {
            regionL_ = rects[0].left; regionT_ = rects[0].top;
            regionR_ = rects[0].right; regionB_ = rects[0].bottom;
            for (const auto& r : rects) {
                regionL_ = std::min(regionL_,r.left); regionT_ = std::min(regionT_,r.top);
                regionR_ = std::max(regionR_,r.right); regionB_ = std::max(regionB_,r.bottom);
            }
        }
        return true;
    }

    bool updateTouchableRegionRect(int left, int top, int right, int bottom) {
        return updateTouchableRegionRects({{left,top,right,bottom}});
    }

    // Clear the old orientation's input union in the same crop/content-size transaction.
    // The host replaces it with the next completed frame's precise visible shapes.
    bool resize(int width, int height) {
        if (width <= 0 || height <= 0 || !attached_ || !wi_) {
            error_ = "resize: invalid dimensions or missing input window";
            return false;
        }
        // getSurface() creates a BLAST child with updateDestinationFrame=true.
        // Resizing only its producer buffer leaves that child's destination frame
        // at the creation size and stretches the entire UI after a rotation.
        if (!detail::syms().updateDefaultBufferSize) {
            error_ = "resize: missing SurfaceControl::updateDefaultBufferSize";
            failOpenTouchInput();
            return false;
        }
        detail::syms().updateDefaultBufferSize(layer_.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        w_ = width;
        h_ = height;
        wi_->setContentSize(width, height);
        return updateTouchableRegionRects({}, true);
    }

    // Stop system-level interception if a dynamic update cannot be verified.
    // The caller must terminate the overlay if this submission also fails.
    bool disableTouchInput() {
        if (!wi_ || !attached_) return true;
        wi_->setInputConfigBits(winfo::kCfgNotTouchable);
        return applyGeometryAndInput(true);
    }

    // 移动图层。真机验证：**不需要**重设 region，SF 会把图层局部 region 变换到 display 空间，
    // 所以拦截区域自动跟随（probe_touch_intercept.cpp 阶段 B 实测）。
    bool setPosition(int x, int y) {
        error_.clear();
        if (!layer_.get()) {
            error_ = "setPosition: 还没有建图层";
            return false;
        }
        x_ = x;
        y_ = y;
        // 只发几何（不带 setInputWindowInfo），与探针的移动事务一致
        return applyGeometryAndInput(false);
    }

    bool setTrustedOverlay(bool on) {
        // [未核实-1] 该重载的 mangled 名未在设备上 dlsym 过。默认不使用。
        if (!detail::syms().txSetTrustedOverlay) {
            error_ = "setTrustedOverlay: 本机 libgui 没有该符号（未核实项，可忽略）";
            return false;
        }
        trustedOverlay_ = on;
        return applyGeometryAndInput(false);
    }

    // 销毁：hide + setAlpha(0)，析构时自动清理。
    void destroy() {
        if (layer_.get() && detail::syms().txHide && detail::syms().txSetAlpha) {
            void* tx = transaction();
            if (tx) {
                detail::syms().txHide(tx, &layer_);
                detail::syms().txSetAlpha(tx, &layer_, 0.0f);
                detail::txApply(tx, true);
            }
        }
        // 注意：这里**故意不析构 wi_**（也不 decStrong）。
        // 理由（照抄探针的取舍，probe_touch_intercept.cpp:840「故意不 delete：RefBase 管生命周期」）：
        // WindowInfoHandle 的析构会对 token 做一次 decStrong。我们的引用计数收支是
        //   +1 = readStrongBinder 给的（那个 sp 缓冲区永不析构）
        //   -1 = WindowInfo 析构
        // 理论上平衡，但一旦某个进程中同一个 token 被写进过两个 handle（例如未来加了
        // 「重建 WindowInfo」的路径），析构就会多减一次。进程退出时这些内存本来就归还内核，
        // 所以选择「宁可泄漏 1KB，不要 use-after-free」。
        // 需要严格回收的宿主可以自己按上面的收支对 wi_->raw() 调 RefBase::decStrong。
        wi_ = nullptr;
        attached_ = false;
        if (active() == this) active() = nullptr;
        layer_.pointer = nullptr;
        surface_.pointer = nullptr;
        fd_ = -1;
        token_ = nullptr;
    }

    bool ok() const { return layer_.get() != nullptr; }
    const char* error() const { return error_.c_str(); }
    int layerId() const { return layerId_; }

    // ---- 帧率投票：把「这块玻璃要 120Hz」告诉 SurfaceFlinger ----
    //
    // 幂等、可重复调用：SF 的 VRI 每次取的是图层状态里的投票值，被别的图层或
    // 显示策略覆盖后再投一次即可恢复 ⇒ 调用点做成「启动时一次 + 低频重试」。
    //
    // compatibility / strategy 是 int8_t 枚举，各版本取值有 ±1 差别，所以
    // **不做假设**：调用方从环境变量传，设备上逐个试。
    //   compatibility: 0=Default 1=ExactOrMultiple(≈Java FIXED_SOURCE) 2=Exact
    //   strategy:      0=Default 1=OnlyIfSeamless              2=Always
    bool applyFrameRateVote(float fps, int compatibility, int strategy, int priority) {
        if (!layer_.get()) {
            error_ = "帧率投票：还没有图层";
            return false;
        }
        detail::GuiSyms& s = detail::syms();
        if (!s.txSetFrameRate) {
            error_ = "帧率投票：缺 Transaction::setFrameRate 符号";
            return false;
        }
        void* tx = transaction();
        if (!tx) {
            error_ = "帧率投票：Transaction 分配失败";
            return false;
        }
        s.txSetFrameRate(tx, &layer_, fps, (int8_t)compatibility, (int8_t)strategy);
        if (s.txSetFrameRateSelectionPriority) {
            s.txSetFrameRateSelectionPriority(tx, &layer_, (int32_t)priority);
        }
        const int32_t st = detail::txApply(tx, /*sync=*/true);
        if (st != 0) {
            error_ = detail::fmtStr("帧率投票：apply 返回 %d", st);
            return false;
        }
        frameRateVote_ = fps;
        return true;
    }
    float frameRateVote() const { return frameRateVote_; }

    // --- 以下为 selfCheck / 诊断用的内部状态（不参与锁定接口形状，但只读暴露）---
    int inputFd() const { return fd_; }
    void* token() const { return token_; }
    WindowInfoHandle* windowInfo() const { return wi_; }
    bool attached() const { return attached_; }
    const std::vector<aurora::TouchRect>& touchableRegionRects() const { return regionRects_; }
    bool hasRealHandle() const {
        if (!layer_.get() || !detail::syms().getHandle) return layer_.get() != nullptr;
        return detail::syms().getHandle(layer_.get()).get() != nullptr;
    }
    void regionRect(int32_t* l, int32_t* t, int32_t* r, int32_t* b) const {
        *l = regionL_;
        *t = regionT_;
        *r = regionR_;
        *b = regionB_;
    }
    static Overlay*& active() {
        static Overlay* p = nullptr;
        return p;
    }

private:
    void* transaction() {
        if (!transaction_) transaction_ = detail::txNew();
        else detail::syms().txClear(transaction_);
        return transaction_;
    }

    void failOpenTouchInput() {
        const auto originalError = error_;
        // ★ 先记录原因再清理：清理路径自己也可能失败、甚至崩（朋友的机器上就崩在这里），
        //   不先落一行的话，现场只剩「崩在清理里」，最初是哪个检查没过就永远查不到了。
        detail::startupTrace("failOpenTouchInput: %s", originalError.c_str());
        const bool disabled = disableTouchInput();
        error_ = originalError + (disabled ? "; touch interception disabled" :
            "; could not disable interception: terminate the overlay");
    }

    // 把几何（+ 可选地 input window）一起提交。
    // 事务顺序照抄 probe_touch_intercept.cpp:893-918（真机验证过的顺序）。
    bool applyGeometryAndInput(bool withInput) {
        if (!layer_.get()) {
            error_ = "apply: 没有图层";
            return false;
        }
        detail::GuiSyms& s = detail::syms();
        void* tx = transaction();
        if (!tx) {
            error_ = "Transaction 分配失败";
            return false;
        }
        s.txShow(tx, &layer_);
        if (s.txSetLayerStack) s.txSetLayerStack(tx, &layer_, (uint32_t)layerStack_);
        if (s.txSetLayer) s.txSetLayer(tx, &layer_, winfo::kLayerBase);
        s.txSetPosition(tx, &layer_, (float)x_, (float)y_);
        ARect crop(0, 0, w_ > 0 ? w_ : std::max(1, regionR_), h_ > 0 ? h_ : std::max(1, regionB_));
        s.txSetCrop(tx, &layer_, &crop);  // ★ setCrop 必须合法非空，否则事务有问题
        s.txSetAlpha(tx, &layer_, 1.0f);
        if (trustedOverlay_ && s.txSetTrustedOverlay) {
            s.txSetTrustedOverlay(tx, &layer_, true);
        }
        if (withInput) {
            if (!s.txSetInputWindowInfo) {
                error_ = "缺 setInputWindowInfo 符号";
                return false;
            }
            if (!infoSp_.get()) {
                error_ = "内部错误：infoSp 为空";
                return false;
            }
            // Keep infoSp_'s independent +1. The actual parameter needs a
            // separate +1 before the real sp move transfers it to layer_state.
            // Without it, apply's clear deletes our only owner after frame one.
            if (!detail::WithOwnedStrongSlot(infoSp_.get(),
                    [&](void** slot) { return wi_->addRef(slot); },
                    [&](void** slot) { wi_->releaseRef(slot); },
                    [&](void** slot) { s.txSetInputWindowInfo(tx, &layer_, slot); })) {
                error_ = "Cannot acquire input-info parameter reference";
                return false;
            }
        }
        const int32_t st = detail::txApply(tx, /*sync=*/true);
        // ★ 注意：apply() 返回 0 **不代表**事务真的生效（拿到空壳时 SF 会打
        //   「applyTransactions Handle 0x0 is not valid」并把事务当空操作，apply 仍是 0）。
        //   所以真正的判据是 create() 时的 getHandle() != nullptr。
        if (st != 0) {
            error_ = detail::fmtStr("Transaction::apply 返回 %d（非 0）", st);
            return false;
        }
        return true;
    }

    // Transaction's destructor is not exported. Reuse one instance instead
    // of leaking a freshly constructed 1KB instance for every drag frame.
    void* transaction_ = nullptr;
    detail::StrongPointer<void> layer_;
    mutable detail::StrongPointer<void> surface_;
    mutable int windowOffset_ = -1;
    float frameRateVote_ = 0.0f;
    WindowInfoHandle* wi_ = nullptr;
    detail::StrongPointer<void> infoSp_{};
    void* token_ = nullptr;
    int fd_ = -1;
    std::string name_;
    std::string error_;
    int w_ = 0, h_ = 0, x_ = 0, y_ = 0;
    int layerStack_ = 0;
    int layerId_ = -1;
    int32_t regionL_ = 0, regionT_ = 0, regionR_ = 0, regionB_ = 0;
    std::vector<aurora::TouchRect> regionRects_;
    bool attached_ = false;
    bool trustedOverlay_ = false;
};

// ===========================================================================
// 模块级接口
// ===========================================================================
inline bool init(std::string* why) {
    if (why) why->clear();
    return detail::resolveAll(why);
}

// ★ 故意**不 dlclose**：RefBase 对象（SurfaceControl / Surface / WindowInfoHandle）
//   仍活着时卸载 libgui 会崩。这里只清模块状态。
inline void shutdown() {
    if (Overlay::active()) {
        detail::vlog("[aospglue] shutdown(): 还有 Overlay 活着，请先 Overlay::destroy()");
    }
    // libgui/libbinder/libutils 的句柄保留（不 dlclose），见上。
}

// 非阻塞轮询输入通道；一次 MOTION 的所有触点一次交给 fn；★ 自动回 24 字节 FINISHED。
// 返回本次处理的 MOTION 条数；-1 = 连接断开（或 fd 出错）。
//
// 说明：
//   * fd 是 SOCK_SEQPACKET | O_NONBLOCK：一次 read() 正好一条报文，read 不会阻塞。
//   * **不要 close(fd)**（fdsan 会 abort）。本函数也不 close。
//   * 每收到一条报文都回 FINISHED（包含 FOCUS/TOUCH_MODE/CAPTURE/DRAG ——
//     InputDispatcher 对这几类也建 waitQueue 条目等回复，不回会在 5 秒后判 ANR）。
//   * KEY 报文不回调 fn（本模块只做触摸）。
inline int pollTouches(TouchFn fn, void* user) {
    Overlay* ov = Overlay::active();
    if (ov == nullptr || ov->inputFd() < 0) return 0;
    const int fd = ov->inputFd();
    int motions = 0;
    alignas(8) uint8_t buf[kMessageMaxBytes];

    for (;;) {
        errno = 0;
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // 没有更多报文了
            detail::vlog("  pollTouches: read 失败 errno=%d %s", errno, strerror(errno));
            return -1;
        }
        if (n == 0) return -1;  // EOF：对端关闭（通道坏了）

        const size_t len = (size_t)n;
        if (len < 8) continue;
        const uint32_t type = detail::rdU32(buf + kOffMsgType);
        const uint32_t seq = detail::rdU32(buf + kOffMsgSeq);
        detail::vlog("      >>> [INPUT] %zd 字节 type=%u seq=%u", n, type, seq);

        if (type == kMsgTypeMotion) {
            Touch touches[kMaxPointers];
            memset(touches, 0, sizeof touches);
            int count = 0, action = 0, actionIndex = 0;
            if (detail::parseMotionMessage(buf, len, touches, kMaxPointers, &count, &action, &actionIndex)) {
                ++motions;
                if (fn) fn(user, touches, count, action, actionIndex);
            } else {
                // 报文不自洽（长度/pointerCount 不对）。照抄 probe_touch_intercept.cpp:357
                // 的最小长度检查思路，但**不**影响保活：下面照样回 FINISHED。
                detail::vlog("  MOTION 报文解析失败（len=%zu）—— 跳过触点解析", len);
            }
        }
        // ★ 每条报文都要回 FINISHED，否则 5 秒后窗口被判 not responsive。
        detail::writeFinished(fd, seq);
    }
    return motions;
}

// 自检：把「符号解析 / 图层真句柄 / token / WindowInfo 三重校验 / touchOcclusionMode 写回读 /
// region 替换语义 / **拦截区等于窗口框** / 报文解析」逐条写进 *why。全部通过返回 true。
//
// expectRegionW/H：调用方期望的拦截区尺寸（= 界面窗口的包络框，**不是图层尺寸**）。
//   传 0 表示跳过这一条（例如调用方还没 attachInputWindow）。
inline bool selfCheck(std::string* why, int32_t expectRegionW = 0, int32_t expectRegionH = 0) {
    std::string out;
    bool allOk = true;
    auto item = [&](bool ok, const char* name, const std::string& detail) {
        out += ok ? "  [OK]   " : "  [FAIL] ";
        out += name;
        if (!detail.empty()) {
            out += " —— ";
            out += detail;
        }
        out += "\n";
        if (!ok) allOk = false;
    };
    // SKIP：**已知未核实**或**可选**的能力。打印出来便于按需开启，但**不计入 allOk** ——
    // 否则一个可选能力缺失就会让整个程序拒绝启动，把「能力缺失」误报成「程序不可用」。
    auto itemSkip = [&](const char* name, const std::string& detail) {
        out += "  [SKIP] ";
        out += name;
        if (!detail.empty()) {
            out += " —— ";
            out += detail;
        }
        out += "\n";
    };

    // 1. 符号解析
    std::string why1;
    const bool symOk = detail::resolveAll(&why1);
    detail::GuiSyms& s = detail::syms();
    item(symOk, "符号解析（libgui/libbinder/libutils + 必需符号）", symOk ? "" : why1);
    item(s.txSetInputWindowInfo != nullptr, "Transaction::setInputWindowInfo",
         s.txSetInputWindowInfo ? "" : "缺（这一项缺了整条输入链路都做不了）");
    item(WindowInfoHandle::resolve(), "WindowInfoHandle 符号（ctor/dtor/addTouchableRegion/incStrong）",
         WindowInfoHandle::missingSymbol() ? WindowInfoHandle::missingSymbol() : "");
    if (WindowInfoHandle::hasRegionClearSymbol() && WindowInfoHandle::hasRegionGetArraySymbol()) {
        item(true, "Region replacement ABI and symbols", WindowInfoHandle::regionReplaceInfo());
    } else {
        item(false, "Region replacement ABI and symbols", WindowInfoHandle::regionReplaceInfo());
    }

    Overlay* ov = Overlay::active();
    if (ov == nullptr) {
        item(false, "Overlay 状态", "进程里还没有 attach 成功的 Overlay（先 create + attachInputWindow）");
        if (why) *why = out;
        return allOk;
    }

    // 2. 图层是真句柄（不是厂商的假 SurfaceControl）
    const bool real = ov->hasRealHandle();
    item(real, "图层是真句柄（SurfaceControl::getHandle() != nullptr）",
         detail::fmtStr("layerId=%d%s", ov->layerId(), real ? "" : "（空壳！事务会被 SF 丢弃）"));

    // 3. token 是否设置
    WindowInfoHandle* wi = ov->windowInfo();
    const bool tok = (wi != nullptr) && wi->tokenSet() && wi->tokenRaw() != nullptr;
    item(tok, "WindowInfoHandle::token 已设置（setTokenOwned 转移引用）",
         detail::fmtStr("token=%p", wi ? wi->tokenRaw() : nullptr));

    if (wi == nullptr || !wi->valid()) {
        item(false, "WindowInfoHandle 有效", "不可用");
        if (why) *why = out;
        return allOk;
    }

    // 4. WindowInfo 三重校验（读回默认值 + vptr）
    const bool layout = wi->layoutValid();
    item(layout, "WindowInfo 三重 ABI 校验",
         detail::fmtStr("dispatchingTimeout=%lldns（期望 5000000000） gsf=%.3f（期望 1.0） pid=%d uid=%d",
                        (long long)wi->dispatchingTimeoutNs(), (double)wi->globalScaleFactor(),
                        wi->ownerPid(), wi->ownerUid()));

    // 5. touchOcclusionMode 写回读 == ALLOW(2)
    //    ★ 这一条不过就**不要上屏**：默认 BLOCK_UNTRUSTED 会把后面 app 的触摸整条丢弃。
    {
        wi->setTouchOcclusionMode(winfo::kTouchOcclusionAllow);
        const int32_t mode = wi->touchOcclusionMode();
        item(mode == winfo::kTouchOcclusionAllow, "touchOcclusionMode 写回读 == ALLOW(2)",
             detail::fmtStr("读回 %d%s", mode,
                            mode == winfo::kTouchOcclusionAllow
                                ? ""
                                : "（★ 全屏图层的 frame 会把后面 app 的触摸判为不可信遮挡后丢弃）"));
    }

    // 6. region 替换语义自检：连续设两个不同矩形，读回必须只有第二个
    //    （防「悄悄变成并集」—— 真机上表现为莫名其妙的整屏拦截）
    {
        const auto savedRegionRects = ov->touchableRegionRects();
        // Unknown device ABI/symbols are rejected by resolve().
        const bool regionSyms =
            WindowInfoHandle::hasRegionSetSymbol() || WindowInfoHandle::hasRegionClearSymbol();
        if (!regionSyms) {
            item(false, "region replacement self-check",
                 "Runtime ABI or Region symbols were rejected; dynamic input cannot start");
        } else {
            bool ok = false;
            std::string d;
            if (!wi->setTouchableRegionReplace(0, 0, 100, 100)) {
                d = "Region replacement symbol unavailable after runtime ABI guard";
            } else if (!wi->setTouchableRegionReplace(200, 200, 300, 300)) {
                d = "第二次替换调用失败";
            } else {
                const int32_t* rects = nullptr;
                const size_t n = wi->readTouchableRegion(&rects);
                if (n == 0 || rects == nullptr) {
                    d = "拿不到 Region::getArray，无法回读";
                } else if (n != 1 || rects[0] != 200 || rects[1] != 200 || rects[2] != 300 ||
                           rects[3] != 300) {
                    d = detail::fmtStr(
                        "★ 替换语义不成立：读回 %zu 个矩形，第一个 (%d,%d,%d,%d)"
                        "（期望唯一矩形 200,200,300,300；多矩形 = 并集累积）",
                        n, rects[0], rects[1], rects[2], rects[3]);
                } else {
                    ok = true;
                    d = "第二次 (200,200,300,300) 读回仍只有一个矩形";
                }
            }
            item(ok, "region 替换语义自检（(0,0,100,100) → (200,200,300,300)，读回必须只有后者）",
                 d);
            // 还原：把真实 region 写回去
            if (!wi->replaceTouchableRegionRects(savedRegionRects)) {
                item(false, "region 自检后还原", "还原失败，region 可能停在自检值上");
            }
            // These temporary probes are never submitted to SF. Restoring
            // the exact saved union leaves both local and SF state unchanged.
        }
    }

    // 7. ★ 拦截区必须等于**窗口框**，不是图层尺寸
    //
    //    这是对一次真实 bug 的回归防护：attachInputWindow 曾经用图层尺寸
    //    （1080x2376）当 region，结果是**整屏拦截** —— 用户看到的就是
    //    「什么都点不动」。用户要求③是「窗口范围内拦截」，所以这里必须钉死。
    //
    //    期望值由调用方传进来（= 界面窗口的包络框），因为只有它知道界面多大。
    //    传 0 则跳过。
    {
        if (expectRegionW <= 0 || expectRegionH <= 0) {
            itemSkip("touchableRegion == 窗口框（非图层尺寸）",
                     "调用方未传期望尺寸，跳过");
        } else {
            const int32_t* rects = nullptr;
            const size_t n = wi->readTouchableRegion(&rects);
            if (n == 0 || rects == nullptr) {
                itemSkip("touchableRegion == 窗口框（非图层尺寸）",
                         "Region::getArray is unavailable after runtime ABI verification");
            } else if (n != 1) {
                item(false, "touchableRegion == 窗口框（非图层尺寸）",
                     detail::fmtStr("★ 期望唯一矩形，实际 %zu 个 —— 多矩形说明被并集累积了", n));
            } else {
                const int32_t w = rects[2] - rects[0];
                const int32_t h = rects[3] - rects[1];
                const bool ok = (w == expectRegionW && h == expectRegionH);
                item(ok, "touchableRegion == 窗口框（非图层尺寸）",
                     detail::fmtStr("读回 (%d,%d,%d,%d) 即 %dx%d；期望 %dx%d%s", rects[0], rects[1],
                                    rects[2], rects[3], w, h, expectRegionW, expectRegionH,
                                    ok ? "" : "（★ 若等于图层尺寸就是整屏拦截，用户会「什么都点不动」）"));
            }
        }
    }

    // 8. 报文布局自检：合成一条 1 指 MOTION（312 字节），验证解析
    //    含：pointerCount / transform / bitset 稀疏取值（X,Y,PRESSURE,SIZE）/ toolType
    {
        uint8_t msg[kMotionFixedBytes + kPointerBytes];
        memset(msg, 0, sizeof msg);
        const uint32_t t = kMsgTypeMotion;
        memcpy(msg + kOffMsgType, &t, 4);
        const uint32_t pc = 1;
        memcpy(msg + kOffMotionPointerCount, &pc, 4);
        const int32_t action = kActionDown;
        memcpy(msg + kOffMotionAction, &action, 4);
        const int64_t et = 123456789LL;
        memcpy(msg + kOffMotionEventTime, &et, 8);
        const float dsdx = 2.0f, dtdx = 0.0f, dtdy = 0.0f, dsdy = 3.0f, tx = 10.0f, ty = 20.0f;
        memcpy(msg + kOffMotionDsdx, &dsdx, 4);
        memcpy(msg + kOffMotionDtdx, &dtdx, 4);
        memcpy(msg + kOffMotionDtdy, &dtdy, 4);
        memcpy(msg + kOffMotionDsdy, &dsdy, 4);
        memcpy(msg + kOffMotionTx, &tx, 4);
        memcpy(msg + kOffMotionTy, &ty, 4);
        uint8_t* p = msg + kOffMotionPointers;
        const int32_t pid = 7;
        memcpy(p + kOffPointerId, &pid, 4);
        const int32_t tool = kToolTypeFinger;
        memcpy(p + kOffPointerToolType, &tool, 4);
        uint8_t* coords = p + kOffPointerCoords;
        // bits: AXIS_X(0)=MSB, AXIS_Y(1), AXIS_PRESSURE(2), AXIS_SIZE(3)
        uint64_t bits = (0x8000000000000000ULL >> kAxisX) |
                        (0x8000000000000000ULL >> kAxisY) |
                        (0x8000000000000000ULL >> kAxisPressure) |
                        (0x8000000000000000ULL >> kAxisSize);
        memcpy(coords + kOffPointerCoordsBits, &bits, 8);
        const float xs = 5.0f, ys = 6.0f, press = 0.75f, sz = 0.5f;
        memcpy(coords + kOffPointerCoordsValues + 0 * 4, &xs, 4);      // index 0 = X
        memcpy(coords + kOffPointerCoordsValues + 1 * 4, &ys, 4);      // index 1 = Y
        memcpy(coords + kOffPointerCoordsValues + 2 * 4, &press, 4);   // index 2 = PRESSURE
        memcpy(coords + kOffPointerCoordsValues + 3 * 4, &sz, 4);      // index 3 = SIZE

        Touch touches[kMaxPointers];
        memset(touches, 0, sizeof touches);
        int count = 0, act = -1, actIdx = -1;
        const bool parsed =
            detail::parseMotionMessage(msg, sizeof msg, touches, kMaxPointers, &count, &act, &actIdx);
        const bool valuesOk = parsed && count == 1 && touches[0].pointerId == 7 &&
                              touches[0].toolType == kToolTypeFinger &&
                              touches[0].x == dsdx * xs + tx && touches[0].y == dsdy * ys + ty &&
                              touches[0].pressure == press && touches[0].size == sz &&
                              touches[0].eventTimeNanos == (long long)et && act == kActionDown &&
                              actIdx == 0;
        item(valuesOk, "报文布局自检（合成 312 字节 1 指 MOTION：transform + bitset 稀疏取值）",
             detail::fmtStr("count=%d x=%.2f y=%.2f pressure=%.2f size=%.2f tool=%d", count,
                            count ? (double)touches[0].x : 0.0, count ? (double)touches[0].y : 0.0,
                            count ? (double)touches[0].pressure : 0.0,
                            count ? (double)touches[0].size : 0.0,
                            count ? touches[0].toolType : -1));
        // ★ 原来的断言写的是 `sizeof(Touch) == kPointerBytes` —— 拿**我们自己的 C++ struct**
        //   （Touch，实测 40 字节）去比 **AOSP 的 Pointer**（144 字节），两者毫无关系，
        //   于是恒为假，把整个自检拖成 allOk=false，宿主机因此拒绝启动。
        //   断言要校验的其实就是 168 + 144 == 312。
        item(kMotionFixedBytes + kPointerBytes == 312,
             "1 指报文长度 = kMotionFixedBytes(168) + kPointerBytes(144) == 312（真机实测）",
             detail::fmtStr("fixed=%zu pointer=%zu 1 指=%zu（Touch=%zu，另一回事）",
                            kMotionFixedBytes, kPointerBytes, kMotionFixedBytes + kPointerBytes,
                            sizeof(Touch)));
    }

    // 7b. ★★ 双指报文自检 —— 这是「ELF 窗口多点触控」这一项的核心证据。
    //     它一次证明两件事：
    //       (a) 第二个 Pointer 确实位于 第一个 +144 处（结构体布局推断正确）；
    //       (b) 解析器会把**全部**触点交出来，**不会只交第一个**。
    //     合成报文的长度也必须是真机实测的 312 + 144 = 456。
    {
        uint8_t msg2f[kMotionFixedBytes + kPointerBytes * 2];
        memset(msg2f, 0, sizeof msg2f);
        const uint32_t t = kMsgTypeMotion;
        memcpy(msg2f + kOffMsgType, &t, 4);
        const uint32_t pc = 2;  // ← 两个触点
        memcpy(msg2f + kOffMotionPointerCount, &pc, 4);
        // POINTER_DOWN，index=1
        // ★ AOSP 的编码是 `action = (actionIndex << 8) | actionCode`
        //   （Input.h: ACTION_POINTER_INDEX_SHIFT=8 / ACTION_POINTER_INDEX_MASK=0xff00）。
        //   原来写成了 `(code << 8) | index` —— 顺序反了，于是解码出来是 action=1 / idx=5。
        //   解析器忠实解码了这个（错的）输入，说明解析器是对的，是测试数据错了。
        const int32_t act2 = (1 << kActionPointerIndexShift) | kActionPointerDown;
        memcpy(msg2f + kOffMotionAction, &act2, 4);
        const float dsdx = 1.0f, dtdx = 0.0f, dtdy = 0.0f, dsdy = 1.0f;
        const float tx2 = 11.0f, ty2 = 22.0f;
        memcpy(msg2f + kOffMotionDsdx, &dsdx, 4);
        memcpy(msg2f + kOffMotionDtdx, &dtdx, 4);
        memcpy(msg2f + kOffMotionDtdy, &dtdy, 4);
        memcpy(msg2f + kOffMotionDsdy, &dsdy, 4);
        memcpy(msg2f + kOffMotionTx, &tx2, 4);
        memcpy(msg2f + kOffMotionTy, &ty2, 4);

        auto fillPointer = [](uint8_t* p, int32_t id, float x, float y) {
            memcpy(p + kOffPointerId, &id, 4);
            const int32_t tool = kToolTypeFinger;
            memcpy(p + kOffPointerToolType, &tool, 4);
            uint8_t* coords = p + kOffPointerCoords;
            const uint64_t bits =
                (0x8000000000000000ULL >> kAxisX) | (0x8000000000000000ULL >> kAxisY);
            memcpy(coords + kOffPointerCoordsBits, &bits, 8);
            memcpy(coords + kOffPointerCoordsValues + 0 * 4, &x, 4);  // index 0 = X
            memcpy(coords + kOffPointerCoordsValues + 1 * 4, &y, 4);  // index 1 = Y
        };
        fillPointer(msg2f + kOffMotionPointers, 41, 3.0f, 4.0f);
        fillPointer(msg2f + kOffMotionPointers + kPointerBytes, 42, 30.0f, 40.0f);  // ← +144

        Touch tt[kMaxPointers];
        memset(tt, 0, sizeof tt);
        int c3 = 0, a3 = -1, i3 = -1;
        const bool p3 = detail::parseMotionMessage(msg2f, sizeof msg2f, tt, kMaxPointers, &c3, &a3,
                                                   &i3);
        const bool twoOk = p3 && c3 == 2 && tt[0].pointerId == 41 && tt[0].x == 3.0f + tx2 &&
                           tt[0].y == 4.0f + ty2 && tt[1].pointerId == 42 &&
                           tt[1].x == 30.0f + tx2 && tt[1].y == 40.0f + ty2 &&
                           a3 == kActionPointerDown && i3 == 1;
        item(twoOk, "★★ 双指报文自检（pointerCount=2，第二个 Pointer 在 +144）— 多点触控核心证据",
             detail::fmtStr("count=%d id0=%d (%.1f,%.1f) id1=%d (%.1f,%.1f) action=%d idx=%d",
                            c3, c3 > 0 ? tt[0].pointerId : -1, c3 > 0 ? (double)tt[0].x : 0.0,
                            c3 > 0 ? (double)tt[0].y : 0.0, c3 > 1 ? tt[1].pointerId : -1,
                            c3 > 1 ? (double)tt[1].x : 0.0, c3 > 1 ? (double)tt[1].y : 0.0, a3, i3));

        // 7c. 稀疏轴：只置 AXIS_Y 时，X 必须为 0（未置位的轴按 0 取值），Y 正确。
        uint8_t msg1f[kMotionFixedBytes + kPointerBytes];
        memset(msg1f, 0, sizeof msg1f);
        memcpy(msg1f, msg2f, sizeof msg1f);
        const uint32_t pc1 = 1;
        memcpy(msg1f + kOffMotionPointerCount, &pc1, 4);
        {
            uint8_t* p = msg1f + kOffMotionPointers;
            uint8_t* coords = p + kOffPointerCoords;
            const uint64_t onlyY = (0x8000000000000000ULL >> kAxisY);
            memcpy(coords + kOffPointerCoordsBits, &onlyY, 8);
            const float yy = 77.0f;
            memcpy(coords + kOffPointerCoordsValues + 0 * 4, &yy, 4);  // 只有 Y，占 index 0
        }
        Touch t1f[kMaxPointers];
        memset(t1f, 0, sizeof t1f);
        int c4 = 0, a4 = -1, i4 = -1;
        const bool p4 =
            detail::parseMotionMessage(msg1f, sizeof msg1f, t1f, kMaxPointers, &c4, &a4, &i4);
        (void)a4;
        (void)i4;
        // ★ 期望值必须把 transform 算进去：未置位的轴取 0，但 x_local = dsdx*0 + dtdx*y + tx = tx。
        //   原来期望 x==0 是错的（忘了 tx），解析器返回 11 才是对的。
        item(p4 && c4 == 1 && t1f[0].x == tx2 && t1f[0].y == 77.0f + ty2,
             "稀疏轴自检（只置 AXIS_Y：X 未置位取 0，再过 transform）",
             detail::fmtStr("count=%d x=%.2f y=%.2f（期望 x=tx=%.2f, y=77+ty=%.2f）", c4,
                            c4 ? (double)t1f[0].x : 0.0, c4 ? (double)t1f[0].y : 0.0, (double)tx2,
                            (double)(77.0f + ty2)));
    }

    if (why) *why = out;
    return allOk;
}

}  // namespace aospglue

// ★ 必须补这个 #endif：上面 1426 行的 `#if AOSPGLUE_PLATFORM` 原本没有配对，
//   编译器会在 EOF 报 "unterminated conditional directive"。
//   注意 `#if AOSPGLUE_PLATFORM` 为真时下面的代码才参与编译 —— 这正是我们想要的
//   （宿主是 Android ELF，需要 Overlay / pollTouches / selfCheck）。
#endif  // AOSPGLUE_PLATFORM
