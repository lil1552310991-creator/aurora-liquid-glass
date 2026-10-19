// SurfaceFlinger 输入窗口 —— 触摸拦截（纯 ELF）
//
// 目标（用户要求）
// ----------------
// 「不管那个窗口怎么移动，只要我点击窗口的范围内都会拦截触摸，
//   不会作用到后面的桌面/程序上」
//
// 机制（经 AOSP 源码 + 真机机器码双重核实）
// ------------------------------------------
// InputDispatcher 的命中判定**只看 touchableRegion**（InputDispatcher.cpp:581-607）：
//   displayId 相等 + 非 NOT_VISIBLE + 非 NOT_TOUCHABLE + touchableRegion.contains()
// 遍历顺序是 Z 序 front→back，命中第一个非 spy 窗口即停。所以：
//   - 可触区域必须是**图层局部坐标** Rect(0,0,W,H)
//   - 图层 Z 序必须够高
//   - touchableRegion 为空 = 完全不可触（没有"退回整个 frame"的分支）
// SurfaceFlinger 每次快照会把 region 变换到屏幕坐标，setPosition 触发重算
// —— 所以**移动时拦截区域自动跟随**，不需要手动改 region。
//
// ★★ ABI 关键警告：不要照 AOSP 分支硬编码偏移 ★★
// ------------------------------------------------
// 真机实测（2026-07 构建的 libgui.so，反汇编机器码）与 android16-release
// **不一致**：
//
//   项                         android16-release   真机实测
//   WindowInfoHandle::mInfo            16              56
//   sizeof(WindowInfo)                432             496
//   sizeof(WindowInfoHandle)          448             552
//   touchableRegion 之后的字段        ——            整体 +64
//
// 证据：WindowInfoHandle::readFromParcel @0x1bd844 仅 12 字节
//   5f2403d5 00e00091 47210014  =  bti j; add x0, x0, #0x38; b <plt>   (0x38=56)
// 四处互相印证（readFromParcel / writeToParcel / updateFrom / 默认构造）。
//
// libgui / libui 是**平台私有库**（非 NDK API），QPR 与安全更新允许改内部 ABI。
// 所以下面的偏移是**本机实测值**，并且在使用前会做**运行时校验**：
// 校验不通过就拒绝操作（宁可拦截失效，也不写坏系统内存）。
//
// 已排除的做法：
//   - malloc 假对象：sp<> 会 incStrong/decStrong，RefBase::mRefs 不存在 → 必崩
//   - 手写 std::string 的 SSO 布局：属于实现细节，版本不匹配即内存损坏
//     （改用真实的 std::string::assign）
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unistd.h>

namespace aurora {

namespace winfo {

// InputConfig 位值（AIDL 公开契约，跨版本稳定）
constexpr uint32_t kCfgDefault           = 0;
constexpr uint32_t kCfgNoInputChannel    = 1u << 0;
constexpr uint32_t kCfgNotVisible        = 1u << 1;
constexpr uint32_t kCfgNotFocusable      = 1u << 2;
constexpr uint32_t kCfgNotTouchable      = 1u << 3;
constexpr uint32_t kCfgTrustedOverlay    = 1u << 8;
constexpr uint32_t kCfgSpy               = 1u << 14;  // ★ 千万不要设
constexpr uint32_t kCfgInterceptsStylus  = 1u << 15;  // ★ 同上

// Z 序（参考实现 EndToEndNativeInputTest.cpp 用 INT32_MAX - 10）
constexpr int32_t kLayerBase = 0x7FFFFFFF - 10;

// 分配字节数：真机实测 552，留一倍余量容忍 OTA 后变大。
// **仅用于分配**，不参与字段寻址。
constexpr size_t kAllocBytes = 1024;

// ---- 本机实测偏移（相对 WindowInfoHandle 起点）----
// 全部 = android16-release 的值 + 下面这个增量（touchableRegion 之前不变）
constexpr size_t kInfoOffset = 56;

// WindowInfo 内部字段偏移（真机值）
constexpr size_t kOffToken           = 8;
constexpr size_t kOffWindowToken     = 16;
constexpr size_t kOffId              = 24;
constexpr size_t kOffName            = 32;
constexpr size_t kOffDispTimeout     = 56;
constexpr size_t kOffFrame           = 64;
constexpr size_t kOffContentSize     = 80;
constexpr size_t kOffSurfaceInset    = 88;
constexpr size_t kOffGlobalScale     = 92;
constexpr size_t kOffAlpha           = 96;
constexpr size_t kOffTouchableRegion = 184;
// touchableRegion 之后整体 +64（真机 touchableRegion 占 168 而非 104）
constexpr size_t kOffTouchOcclusion  = 352;
constexpr size_t kOffOwnerPid        = 356;
constexpr size_t kOffOwnerUid        = 360;
constexpr size_t kOffPackageName     = 368;
constexpr size_t kOffInputConfig     = 392;
constexpr size_t kOffDisplayId       = 396;
constexpr size_t kOffAppInfo         = 400;
constexpr size_t kOffReplaceRegion   = 448;
constexpr size_t kOffLayoutType      = 472;
constexpr size_t kOffLayoutFlags     = 476;
constexpr size_t kMinStructBytes     = 496;   // sizeof(WindowInfo) 真机值

}  // namespace winfo

// Android 的 Rect（4×int32）—— NDK 公开的稳定类型，可安全手写
struct ARect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    ARect() = default;
    ARect(int32_t l, int32_t t, int32_t r, int32_t b) : left(l), top(t), right(r), bottom(b) {}
};

// ---------------------------------------------------------------------------
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

        s.libgui = dlopen("/system/lib64/libgui.so", RTLD_LAZY | RTLD_GLOBAL);
        void* lu = dlopen("/system/lib64/libutils.so", RTLD_LAZY | RTLD_GLOBAL);
        dlopen("/system/lib64/libbinder.so", RTLD_LAZY | RTLD_GLOBAL);
        dlopen("/system/lib64/libui.so", RTLD_LAZY | RTLD_GLOBAL);
        if (!s.libgui) return (s.ok = false);

        auto g = [&](const char* n) { return dlsym(s.libgui, n); };
        auto u = [&](const char* n) { return lu ? dlsym(lu, n) : nullptr; };

        s.ctor  = (void (*)(void*))g("_ZN7android3gui16WindowInfoHandleC2Ev");
        s.dtor  = (void (*)(void*))g("_ZN7android3gui16WindowInfoHandleD1Ev");
        s.addTouchableRegion = (void (*)(void*, const ARect*))
            g("_ZN7android3gui10WindowInfo18addTouchableRegionERKNS_4RectE");

        // 真实 operator new / delete：RefBase::decStrong 归零时会 `delete this`，
        // 所以对象**必须**由真实 operator new 分配，否则那个 delete 会崩。
        s.opNew    = (void* (*)(size_t))dlsym(RTLD_DEFAULT, "_Znwm");
        if (!s.opNew) s.opNew = (void* (*)(size_t))u("_Znwm");
        s.opDelete = (void (*)(void*))dlsym(RTLD_DEFAULT, "_ZdlPv");
        s.incStrong = (void (*)(void*, const void*))u("_ZNK7android7RefBase9incStrongEPKv");
        // ★ 注意 `K`：incStrong 是 **const 成员函数**，mangled 名是
        //   `_ZNK7android7RefBase9incStrongEPKv`。写成 `_ZN...` 会 dlsym 返回 null，
        //   于是 setToken() 静默失败（真机实测：原写法一直是 null）。
        // std::string::assign 属于 libc++（libc++_shared.so），**不在 libutils 里**。
        // 先查全局符号表，再退回 libutils。（真机实测：libutils 里查不到。）
        s.assignStr = (void (*)(void*, const char*))dlsym(
            RTLD_DEFAULT,
            "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");
        if (!s.assignStr)
            s.assignStr = (void (*)(void*, const char*))u(
                "_ZNSt3__112basic_stringIcNS_11char_traitsIcEENS_9allocatorIcEEE6assignEPKc");

        s.ok = s.ctor && s.dtor && s.opNew;
        return s.ok;
    }

    static const char* missingSymbol() {
        auto& s = syms();
        if (!s.libgui) return "dlopen libgui.so 失败";
        if (!s.ctor)  return "缺 WindowInfoHandle 构造函数";
        if (!s.dtor)  return "缺 WindowInfoHandle 析构函数";
        if (!s.opNew) return "缺 operator new（无法安全分配 RefBase）";
        if (!s.addTouchableRegion) return "缺 addTouchableRegion";
        if (!s.incStrong) return "缺 RefBase::incStrong（token 无法安全赋值）";
        return nullptr;
    }

    // 创建。用真实 operator new 分配 + 真实构造函数。
    bool create() {
        if (mem_) return true;
        if (!resolve()) return false;
        mem_ = syms().opNew(winfo::kAllocBytes);
        if (!mem_) return false;
        std::memset(mem_, 0, winfo::kAllocBytes);
        syms().ctor(mem_);
        // ---- 运行时 ABI 校验 ----
        // 校验 mInfo 偏移对不对：WindowInfo 的 vptr 应指向代码段（高位地址），
        // 且 ObjectSize 之后若干字节应被构造函数写过（非全 0）。
        // 校验不通过就拒绝使用 —— 宁可拦截失效，也不写坏系统内存。
        if (!validateLayout()) {
            std::fprintf(stderr,
                "[TouchInterceptor] ★ ABI 校验失败：本机 libgui 的 WindowInfo 布局"
                "与本文件记录的偏移不一致。已拒绝操作（不会写坏内存）。\n"
                "  需要在本机重新实测偏移，见 docs/TOUCH_INTERCEPTION_SF.md 第七节。\n");
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
    void setTouchableRegion(int32_t w, int32_t h) {
        if (!info() || !syms().addTouchableRegion) return;
        ARect r(0, 0, w, h);
        syms().addTouchableRegion(info(), &r);
    }

    // inputConfig 直接按偏移写，**不调用 setInputConfig**。
    //
    // 原因（真机实测）：调用 `WindowInfo::setInputConfig(ftl::Flags<InputConfig>, bool)`
    // 会 SIGSEGV。`ftl::Flags<E>` 与 libutils 的 `sp<T>` 一样属于
    // "non-trivial for the purposes of calls" —— 按 AArch64 ABI 应经内存传递，
    // 而我按 `uint32_t` 声明就成了寄存器传递，调用约定错位。
    //
    // 而 `ftl::Flags<E>` 的底层就是一个 4 字节整数（AOSP 自己用
    // `static_assert(sizeof(inputConfig) == 4u)` 钉死），所以按偏移写
    // 一个 uint32 是**语义等价且安全**的。
    //
    // 默认值 0 = DEFAULT = visible + touchable + focusable，正是我们要的。
    void setInputConfigBits(uint32_t bits) {
        writeU32(winfo::kOffInputConfig, bits);
    }
    uint32_t inputConfigBits() const { return readU32(winfo::kOffInputConfig); }

    // token 是 sp<IBinder>（偏移 8）。**必须做引用计数配对**：
    // WindowInfo 析构会对它 decStrong，只写指针不加引用 = 引用计数错乱 → 崩。
    //
    // ★★ 2026-10 重要更正：本函数直接对 `IBinder*` 调 `RefBase::incStrong` 是**错的** ★★
    //
    // 依据（两条都是设备自己的机器码 / AOSP 头文件，不是推测）：
    //   1. `research/aosp/src/native/libs/binder/include/binder/IBinder.h:52`
    //        class ... IBinder : public virtual RefBase
    //      —— RefBase 是 IBinder 的**虚基类**，子对象不在偏移 0。
    //   2. 设备 libbinder.so `BpRefBase::BpRefBase(const sp<IBinder>&)` 的机器码：
    //        ldr  x8, [x0]            ; x8 = this 的 vptr
    //        ldur x8, [x8, #-0x18]    ; vtable[-3] = 虚基类偏移
    //        add  x0, x0, x8          ; x0 = RefBase 子对象
    //        bl   RefBase::incStrong
    //      —— 编译器必须做这个 `add` 才能拿到 RefBase*。
    //
    // 直接调用会把 `BpBinder+8` 当成 `mRefs` 去加计数：既不增加真正的强引用计数，
    // 又踩坏 BpBinder 自己的字段；随后 WindowInfo 析构的 decStrong 会把一个
    // 从未加过的计数减到 0 → 提前 delete → use-after-free。
    //
    // 因此**推荐用 setTokenOwned()**（见下）：转移一个已经持有的强引用，
    // 语义上完全等价于 sp<> 的拷贝构造 + 移动赋值，零 ABI 风险。
    // 保留本函数只为兼容旧调用点，并在此显式警告。
    bool setToken(void* binder) {
        if (!info() || !binder) return false;
        if (!syms().incStrong) return false;
        std::fprintf(stderr,
            "[TouchInterceptor] 警告：setToken() 直接对 IBinder* 调 RefBase::incStrong。\n"
            "  IBinder : public virtual RefBase（RefBase 在非 0 偏移），这样调用是错的。\n"
            "  请改用 setTokenOwned()（转移一个已持有的强引用）。\n");
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

    void setName(const char* v)        { if (v) assignString(winfo::kOffName, v); }
    void setPackageName(const char* v) { if (v) assignString(winfo::kOffPackageName, v); }
    void setAppInfoName(const char* v) { if (v) assignString(winfo::kOffAppInfo + 16, v); }
    void setAppInfoTokenRaw(void* b)   { writePtr(winfo::kOffAppInfo + 8, b); }
    void setAppInfoTimeoutMs(int64_t v){ writeI64(winfo::kOffAppInfo + 40, v); }

    void setId(int32_t v)                 { writeI32(winfo::kOffId, v); }
    void setDispatchingTimeoutNs(int64_t v){ writeI64(winfo::kOffDispTimeout, v); }
    void setGlobalScaleFactor(float v)    { writeF32(winfo::kOffGlobalScale, v); }
    void setAlpha(float v)                { writeF32(winfo::kOffAlpha, v); }
    void setSurfaceInset(int32_t v)       { writeI32(winfo::kOffSurfaceInset, v); }
    void setOwnerPid(int32_t v)           { writeI32(winfo::kOffOwnerPid, v); }
    void setOwnerUid(int32_t v)           { writeI32(winfo::kOffOwnerUid, v); }
    void setDisplayId(int32_t v)          { writeI32(winfo::kOffDisplayId, v); }
    void setLayoutParamsType(int32_t v)   { writeI32(winfo::kOffLayoutType, v); }
    void setLayoutParamsFlags(uint32_t v) { writeU32(winfo::kOffLayoutFlags, v); }
    void setReplaceRegionWithCrop(bool v) { writeU8(winfo::kOffReplaceRegion, v ? 1 : 0); }
    void setTouchOcclusionMode(int32_t v) { writeI32(winfo::kOffTouchOcclusion, v); }
    void setContentSize(int32_t w, int32_t h) {
        writeI32(winfo::kOffContentSize, w);
        writeI32(winfo::kOffContentSize + 4, h);
    }

    std::string describe() const {
        char buf[512];
        if (!info()) return "<未创建>";
        std::snprintf(buf, sizeof buf,
            "id=%d gsf=%.2f alpha=%.2f disp=%lld pid=%d uid=%d cfg=0x%x displayId=%d region@%zu",
            readI32(winfo::kOffId), (double)readF32(winfo::kOffGlobalScale),
            (double)readF32(winfo::kOffAlpha), (long long)readI64(winfo::kOffDispTimeout),
            readI32(winfo::kOffOwnerPid), readI32(winfo::kOffOwnerUid),
            readU32(winfo::kOffInputConfig), readI32(winfo::kOffDisplayId),
            winfo::kOffTouchableRegion);
        return buf;
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
        void (*assignStr)(void*, const char*) = nullptr;
    };
    static Syms& syms() { static Syms s; return s; }

    // 运行时 ABI 校验。
    //
    // 判据：WindowInfo 的 vptr（在 info() 起点）必须是一个**代码地址**。
    // 本机 libgui 映射在 0x7x 段（见探针输出），而对象在 0xb4... 堆段。
    // 如果 mInfo 偏移算错，读到的会是 sp<> 指针或 0 —— 都不是代码地址。
    bool validateLayout() const {
        uintptr_t vptr = 0;
        std::memcpy(&vptr, info(), 8);
        // AArch64 Android 上用户态代码地址通常是 0x6f...~0x7f... 或 0x5x...，
        // 判据放宽为：非 0、8 字节对齐、且落在常见映射区间。
        if (vptr == 0 || (vptr & 7) != 0) return false;
        if (vptr < 0x100000000ULL || vptr > 0x0000FFFFFFFFFFFFULL) return false;
        // 交叉校验：构造函数应把 dispatchingTimeout 初始化成 5 秒（5000000000 ns）。
        // 这是一个**与布局强相关**的已知默认值，能独立确认偏移正确。
        const int64_t disp = readI64(winfo::kOffDispTimeout);
        if (disp != 5000000000LL) {
            std::fprintf(stderr, "[TouchInterceptor] dispatchingTimeout 读回 %lld（期望 5000000000）\n",
                         (long long)disp);
            return false;
        }
        // 再校验一个：globalScaleFactor 默认 1.0f
        const float gsf = readF32(winfo::kOffGlobalScale);
        if (!(gsf > 0.99f && gsf < 1.01f)) {
            std::fprintf(stderr, "[TouchInterceptor] globalScaleFactor 读回 %.4f（期望 1.0）\n", (double)gsf);
            return false;
        }
        return true;
    }

    // 给 WindowInfo 里的 std::string 赋值。
    // **不复刻 std::string 内部布局** —— 调真实的 std::string::assign。
    // 拿不到符号就跳过（name 只影响 dumpsys，空串时返回 "<invalid>"，无害）。
    void assignString(size_t off, const char* value) {
        auto& s = syms();
        if (!s.assignStr) return;
        s.assignStr((unsigned char*)info() + off, value);
    }

    void writePtr(size_t off, void* v)   { std::memcpy((unsigned char*)info() + off, &v, 8); }
    void writeI32(size_t off, int32_t v) { std::memcpy((unsigned char*)info() + off, &v, 4); }
    void writeU32(size_t off, uint32_t v){ std::memcpy((unsigned char*)info() + off, &v, 4); }
    void writeI64(size_t off, int64_t v) { std::memcpy((unsigned char*)info() + off, &v, 8); }
    void writeF32(size_t off, float v)   { std::memcpy((unsigned char*)info() + off, &v, 4); }
    void writeU8(size_t off, uint8_t v)  { *((unsigned char*)info() + off) = v; }

    int32_t  readI32(size_t off) const { int32_t v=0;  std::memcpy(&v, (const unsigned char*)info()+off, 4); return v; }
    uint32_t readU32(size_t off) const { uint32_t v=0; std::memcpy(&v, (const unsigned char*)info()+off, 4); return v; }
    int64_t  readI64(size_t off) const { int64_t v=0;  std::memcpy(&v, (const unsigned char*)info()+off, 8); return v; }
    float    readF32(size_t off) const { float v=0;    std::memcpy(&v, (const unsigned char*)info()+off, 4); return v; }

    void* mem_ = nullptr;
    bool tokenSet_ = false;
};

}  // namespace aurora
