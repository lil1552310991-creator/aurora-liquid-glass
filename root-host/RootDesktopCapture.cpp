// RootDesktopCapture.cpp: Aurora Root desktop frame source.
//
// Structure of this file:
//   1. tokens, policy helpers and report formatting (compile everywhere)
//   2. host self-test for the contract in RootDesktopCapture.h (compile everywhere)
//   3. Android capability probe: binder service, libgui private AIDL proxy
//      symbols, AHardwareBuffer / EGLImage zero-copy entry points
//   4. frame sources: private AIDL (blocked, explicit), public MediaProjection
//      (needs user consent, unavailable to root), diagnostic one-shot (non
//      realtime, never a stream)
//   5. ZeroCopyGlImporter: AHardwareBuffer -> EGLImage -> GL texture, plus a
//      headless end-to-end self-test that verifies the plumbing on the device
//   6. the aurora_root_capture CLI (built only with AURORA_ROOT_CAPTURE_CLI)
//
// Nothing in this file executes a shell, injects into another process, or asks
// SurfaceFlinger for FLAG_SECURE layers. See RootDesktopCapture.h for the policy.

#include "RootDesktopCapture.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(__ANDROID__)
#define AURORA_CAPTURE_ANDROID 1
#else
#define AURORA_CAPTURE_ANDROID 0
#endif

#if AURORA_CAPTURE_ANDROID
#include <dlfcn.h>
#include <link.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <android/hardware_buffer.h>
#endif

#if defined(_WIN32)
#include <process.h>
#endif

namespace aurora::root::capture {

// ---------------------------------------------------------------------------
// 1. Tokens and report formatting
// ---------------------------------------------------------------------------

const char* capturePathToken(CapturePath path) {
    switch (path) {
        case CapturePath::PrivateSurfaceFlingerAidl:
            return "private-sf-aidl";
        case CapturePath::PublicMediaProjection:
            return "public-mediaproj";
        case CapturePath::DiagnosticOneShot:
            return "diagnostic-oneshot";
    }
    return "unknown";
}

const char* capturePathDescription(CapturePath path) {
    switch (path) {
        case CapturePath::PrivateSurfaceFlingerAidl:
            return "root private SurfaceFlinger AIDL (captureDisplayById + "
                   "ScreenCaptureResults -> GraphicBuffer -> AHardwareBuffer, zero copy)";
        case CapturePath::PublicMediaProjection:
            return "public Android MediaProjection path (app identity + explicit user "
                   "consent token; not reachable from a root ELF)";
        case CapturePath::DiagnosticOneShot:
            return "diagnostic one-shot /system/bin/screencap helper (PNG, non realtime, "
                   "never used as a streaming source)";
    }
    return "unknown";
}

const char* availabilityToken(Availability availability) {
    switch (availability) {
        case Availability::Ready:
            return "ready";
        case Availability::Blocked:
            return "blocked";
        case Availability::Unsupported:
            return "unsupported";
        case Availability::Unknown:
            return "unknown";
    }
    return "unknown";
}

const char* probeOutcomeToken(ProbeOutcome outcome) {
    switch (outcome) {
        case ProbeOutcome::Pass:
            return "pass";
        case ProbeOutcome::Fail:
            return "fail";
        case ProbeOutcome::Info:
            return "info";
    }
    return "info";
}

const char* leaseStatusToken(LeaseStatus status) {
    switch (status) {
        case LeaseStatus::Acquired:
            return "acquired";
        case LeaseStatus::Timeout:
            return "timeout";
        case LeaseStatus::NotSupported:
            return "not-supported";
        case LeaseStatus::Refused:
            return "refused";
        case LeaseStatus::Error:
            return "error";
    }
    return "error";
}

int ProbeReport::passCount() const {
    return static_cast<int>(std::count_if(checks.begin(), checks.end(),
                                          [](const ProbeCheck& check) {
                                              return check.outcome == ProbeOutcome::Pass;
                                          }));
}

int ProbeReport::failCount() const {
    return static_cast<int>(std::count_if(checks.begin(), checks.end(),
                                          [](const ProbeCheck& check) {
                                              return check.outcome == ProbeOutcome::Fail;
                                          }));
}

void addCheck(ProbeReport& report, ProbeOutcome outcome, std::string id, std::string detail) {
    report.checks.push_back(ProbeCheck{std::move(id), outcome, std::move(detail)});
}

std::string formatProbeReport(const ProbeReport& report) {
    std::string out;
    out += "aurora-root-capture probe report\n";
    out += "policy: capture_secure_content=never inject_other_process=never "
           "fabricate_result=never\n";
    for (const ProbeCheck& check : report.checks) {
        out += "probe[";
        out += probeOutcomeToken(check.outcome);
        out += "] ";
        out += check.id;
        out += ": ";
        out += check.detail;
        out += "\n";
    }
    out += "probe.summary pass=" + std::to_string(report.passCount()) +
           " fail=" + std::to_string(report.failCount()) +
           " total=" + std::to_string(report.checks.size()) + "\n";
    out += std::string("verdict.private-aidl=") + availabilityToken(report.privateAidl) + "\n";
    if (!report.privateAidlBlockedReason.empty()) {
        out += "verdict.private-aidl.reason=" + report.privateAidlBlockedReason + "\n";
    }
    out += std::string("verdict.public-mediaprojection=") +
           availabilityToken(report.publicMediaProjection) + "\n";
    out += std::string("verdict.zero-copy-consumer=") +
           availabilityToken(report.zeroCopyConsumer) + "\n";
    out += std::string("verdict.diagnostic-fallback=") +
           availabilityToken(report.diagnosticFallback) + "\n";
    if (!report.requiredInterfaces.empty()) {
        out += "required-interfaces (for the private zero-copy producer):\n";
        for (const std::string& item : report.requiredInterfaces) {
            out += "  - " + item + "\n";
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// FrameLease
// ---------------------------------------------------------------------------

FrameLease::~FrameLease() { reset(); }

FrameLease::FrameLease(FrameLease&& other) noexcept
    : descriptor_(other.descriptor_), release_(other.release_) {
    other.descriptor_ = FrameDescriptor{};
    other.release_ = nullptr;
}

FrameLease& FrameLease::operator=(FrameLease&& other) noexcept {
    if (this != &other) {
        reset();
        descriptor_ = other.descriptor_;
        release_ = other.release_;
        other.descriptor_ = FrameDescriptor{};
        other.release_ = nullptr;
    }
    return *this;
}

bool FrameLease::valid() const { return descriptor_.hardwareBuffer != nullptr; }

const FrameDescriptor& FrameLease::descriptor() const { return descriptor_; }

void FrameLease::reset() {
    if (release_ != nullptr) {
        release_(descriptor_.hardwareBuffer);
    }
    descriptor_ = FrameDescriptor{};
    release_ = nullptr;
}

void FrameLease::adopt(FrameDescriptor descriptor, void (*release)(void*)) {
    reset();
    descriptor_ = descriptor;
    release_ = release;
}

// ---------------------------------------------------------------------------
// 2. Required interfaces + verdict helpers (compile everywhere)
// ---------------------------------------------------------------------------

namespace {

// The exact artifacts that are missing today. This list is the deliverable of
// the blocked branch: it is what the next implementation round must provide.
std::vector<std::string> requiredPrivateInterfaces() {
    return {
        "ROM-matched AIDL: android.gui.ISurfaceComposer transaction order + interface "
        "version for this build fingerprint (captureDisplayById, captureDisplay, "
        "captureLayers)",
        "ROM-matched AIDL parcelables: android.gui.CaptureArgs, DisplayCaptureArgs, "
        "LayerCaptureArgs, ScreenCaptureResults (binder-parcel field layout + version)",
        "android::sp<T> / android::IBinder / android::Parcel client ABI or the AIDL C++ "
        "generated headers for the android.gui package at this ROM version (today only "
        "mangled symbols exist in /system/lib64/libgui.so; no layout information)",
        "android.gui.IScreenCaptureListener + BnScreenCaptureListener implementation to "
        "receive ScreenCaptureResults, or the synchronous ScreenCaptureResults return of "
        "captureDisplayById",
        "GraphicBuffer -> AHardwareBuffer handoff for the captured frame "
        "(GraphicBuffer::getAHardwareBuffer / AHardwareBuffer_fromHardwareBuffer) so the "
        "renderer can eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID) without a copy",
        "display id enumeration for captureDisplayById (getPhysicalDisplayIds / "
        "getPhysicalDisplayToken) - the id is an opaque SurfaceFlinger handle, not a DRM "
        "connector index",
        "SELinux allowance for the su/ksu domain to call SurfaceFlinger's binder (checked "
        "at runtime by this probe before any transaction is attempted)",
        "binder service instance name to use (this ROM registers both 'SurfaceFlingerAIDL' "
        "with android.gui.ISurfaceComposer and 'SurfaceFlinger' with "
        "android.ui.ISurfaceComposer)",
    };
}

constexpr bool kPrivateGuiAbiVendored = false;

constexpr const char* kPrivateAidlBlockedReason =
    "private SurfaceFlinger AIDL capture is not implemented: this build has no "
    "ROM-matched android.gui AIDL declarations (CaptureArgs/ScreenCaptureResults/parcel "
    "layouts), only the platform's mangled symbols. See required-interfaces.";

}  // namespace

// ---------------------------------------------------------------------------
// 3. Host / Android capability probe
// ---------------------------------------------------------------------------

#if AURORA_CAPTURE_ANDROID

namespace {

// Small RAII wrapper around dlopen. It is the only way this binary reaches the
// platform's private libraries: no link-time dependency on libgui.so exists.
class DynLib {
public:
    explicit DynLib(const char* name) { handle_ = ::dlopen(name, RTLD_NOW | RTLD_LOCAL); }
    ~DynLib() {
        if (handle_ != nullptr) {
            ::dlclose(handle_);
        }
    }
    DynLib(const DynLib&) = delete;
    DynLib& operator=(const DynLib&) = delete;

    bool loaded() const { return handle_ != nullptr; }
    void* handle() const { return handle_; }
    std::string error() const {
        const char* message = ::dlerror();
        return message != nullptr ? std::string(message) : std::string("dlopen failed");
    }

    void* symbol(const char* name) const {
        return handle_ != nullptr ? ::dlsym(handle_, name) : nullptr;
    }

private:
    void* handle_ = nullptr;
};

std::string readTextFileTrimmed(const char* path) {
    FILE* file = ::fopen(path, "re");
    if (file == nullptr) {
        return {};
    }
    char buffer[256] = {};
    const std::size_t count = ::fread(buffer, 1, sizeof(buffer) - 1, file);
    ::fclose(file);
    std::string text(buffer, count);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

// Reads a NUL terminated string only when the whole window provably lives inside
// a readable PT_LOAD segment of a loaded module. Reading the platform's
// `descriptor` data symbol must never be able to fault.
struct ReadableWindow {
    const char* address = nullptr;
    std::size_t length = 0;
    bool ok = false;
};

int findReadableSegment(dl_phdr_info* info, std::size_t, void* data) {
    ReadableWindow* window = static_cast<ReadableWindow*>(data);
    for (int index = 0; index < info->dlpi_phnum; ++index) {
        const ElfW(Phdr)& header = info->dlpi_phdr[index];
        if (header.p_type != PT_LOAD || (header.p_flags & PF_R) == 0) {
            continue;
        }
        const auto start = static_cast<ElfW(Addr)>(info->dlpi_addr + header.p_vaddr);
        const auto end = start + header.p_memsz;
        const auto address = reinterpret_cast<ElfW(Addr)>(window->address);
        if (address >= start && address + window->length <= end) {
            window->ok = true;
            return 1;
        }
    }
    return 0;
}

bool safeReadString(const void* address, std::size_t maxLength, std::string& out) {
    if (address == nullptr || maxLength == 0) {
        return false;
    }
    ReadableWindow window{static_cast<const char*>(address), maxLength, false};
    ::dl_iterate_phdr(findReadableSegment, &window);
    if (!window.ok) {
        return false;
    }
    const char* text = static_cast<const char*>(address);
    const std::size_t length = ::strnlen(text, maxLength);
    if (length == 0 || length == maxLength) {
        return false;
    }
    out.assign(text, length);
    return true;
}

// Dereferences a `const char* descriptor` style data symbol and reports what is
// really stored there at runtime.
std::string readDescriptorSymbol(const DynLib& library, const char* symbol) {
    void* slot = library.symbol(symbol);
    if (slot == nullptr) {
        return "(symbol missing)";
    }
    const void* value = *reinterpret_cast<const void* const*>(slot);
    if (value == nullptr) {
        return "(runtime value null: symbol is zero-initialised and never set)";
    }
    std::string text;
    if (!safeReadString(value, 64, text)) {
        return "(value points outside every readable module segment)";
    }
    const bool looksLikeDescriptor =
        text.rfind("android.", 0) == 0 || text.rfind("SurfaceFlinger", 0) == 0 ||
        text.rfind("android::", 0) == 0;
    if (!looksLikeDescriptor) {
        return "(unexpected content, not an interface descriptor)";
    }
    return text;
}

struct SymbolExpectation {
    const char* symbol;
    const char* label;
};

// Every symbol below was observed in /system/lib64/libgui.so of the target ROM.
// Presence is necessary but NOT sufficient: the parcel layouts are still needed.
constexpr SymbolExpectation kSurfaceFlingerAidlSymbols[] = {
    {"_ZN7android3gui16ISurfaceComposer10descriptorE",
     "android::gui::ISurfaceComposer::descriptor"},
    {"_ZN7android3gui16ISurfaceComposer11asInterfaceERKNS_2spINS_7IBinderEEE",
     "android::gui::ISurfaceComposer::asInterface"},
    {"_ZN7android3gui17BpSurfaceComposer18captureDisplayByIdElRKNS0_11CaptureArgsERKNS_2spINS0_22IScreenCaptureListenerEEE",
     "android::gui::BpSurfaceComposer::captureDisplayById(int64_t, const CaptureArgs&, "
     "const sp<IScreenCaptureListener>&)"},
    {"_ZN7android3gui17BpSurfaceComposer14captureDisplayERKNS0_18DisplayCaptureArgsERKNS_2spINS0_22IScreenCaptureListenerEEE",
     "android::gui::BpSurfaceComposer::captureDisplay(const DisplayCaptureArgs&, ...)"},
    {"_ZN7android3gui17BpSurfaceComposer13captureLayersERKNS0_16LayerCaptureArgsERKNS_2spINS0_22IScreenCaptureListenerEEE",
     "android::gui::BpSurfaceComposer::captureLayers(const LayerCaptureArgs&, ...)"},
    {"_ZN7android3gui11CaptureArgs14readFromParcelEPKNS_6ParcelE",
     "android::gui::CaptureArgs::readFromParcel"},
    {"_ZN7android3gui18DisplayCaptureArgs14readFromParcelEPKNS_6ParcelE",
     "android::gui::DisplayCaptureArgs::readFromParcel"},
    {"_ZN7android3gui16LayerCaptureArgs14readFromParcelEPKNS_6ParcelE",
     "android::gui::LayerCaptureArgs::readFromParcel"},
    {"_ZN7android3gui20ScreenCaptureResults14readFromParcelEPKNS_6ParcelE",
     "android::gui::ScreenCaptureResults::readFromParcel"},
    {"_ZN7android3gui22IScreenCaptureListener10descriptorE",
     "android::gui::IScreenCaptureListener::descriptor"},
    {"_ZN7android3gui23BnScreenCaptureListener10onTransactEjRKNS_6ParcelEPS2_j",
     "android::gui::BnScreenCaptureListener::onTransact"},
    {"_ZN7android16ScreenshotClient14captureDisplayENS_9DisplayIdERKNS_3gui11CaptureArgsERKNS_2spINS2_22IScreenCaptureListenerEEE",
     "android::ScreenshotClient::captureDisplay(DisplayId, const CaptureArgs&, ...)"},
    {"_ZN7android16ScreenshotClient13captureLayersERKNS_3gui16LayerCaptureArgsERKNS_2spINS1_22IScreenCaptureListenerEEEb",
     "android::ScreenshotClient::captureLayers(const LayerCaptureArgs&, ..., bool)"},
};

// NDK/platform binder declarations. AServiceManager_getService is not declared
// in the NDK headers of this release, so the signature is declared here; it was
// verified against the device's libbinder_ndk.so
// (one argument, returns AIBinder*, see the disassembly evidence in the report).
struct AIBinder;
using GetServiceFn = AIBinder* (*)(const char* instance);
using IsAliveFn = bool (*)(const AIBinder* binder);
using DecStrongFn = void (*)(AIBinder* binder);

struct BinderNdk {
    DynLib library{"libbinder_ndk.so"};
    GetServiceFn getService = nullptr;
    IsAliveFn isAlive = nullptr;
    DecStrongFn decStrong = nullptr;

    bool resolve() {
        if (!library.loaded()) {
            return false;
        }
        getService = reinterpret_cast<GetServiceFn>(library.symbol("AServiceManager_getService"));
        isAlive = reinterpret_cast<IsAliveFn>(library.symbol("AIBinder_isAlive"));
        decStrong = reinterpret_cast<DecStrongFn>(library.symbol("AIBinder_decStrong"));
        return getService != nullptr && isAlive != nullptr;
    }

    // Returns "" on success, otherwise a diagnostic.
    std::string probeInstance(const char* instance, std::string& detail) {
        if (getService == nullptr) {
            return "libbinder_ndk service manager entry points unavailable";
        }
        AIBinder* binder = getService(instance);
        if (binder == nullptr) {
            detail = std::string("instance=") + instance + " not registered";
            return detail;
        }
        const bool alive = isAlive(binder);
        detail = std::string("instance=") + instance + " found alive=" + (alive ? "1" : "0");
        if (decStrong != nullptr) {
            decStrong(binder);
        }
        if (!alive) {
            return detail;
        }
        return {};
    }
};

struct AhbApi {
    DynLib library{"libandroid.so"};
    int (*allocate)(const AHardwareBuffer_Desc*, AHardwareBuffer**) = nullptr;
    void (*release)(AHardwareBuffer*) = nullptr;
    void (*describe)(const AHardwareBuffer*, AHardwareBuffer_Desc*) = nullptr;
    int (*lock)(AHardwareBuffer*, uint64_t, int32_t, const void*, void**) = nullptr;
    int (*unlock)(AHardwareBuffer*, int32_t*) = nullptr;

    bool resolve() {
        if (!library.loaded()) {
            return false;
        }
        allocate = reinterpret_cast<decltype(allocate)>(library.symbol("AHardwareBuffer_allocate"));
        release = reinterpret_cast<decltype(release)>(library.symbol("AHardwareBuffer_release"));
        describe = reinterpret_cast<decltype(describe)>(library.symbol("AHardwareBuffer_describe"));
        lock = reinterpret_cast<decltype(lock)>(library.symbol("AHardwareBuffer_lock"));
        unlock = reinterpret_cast<decltype(unlock)>(library.symbol("AHardwareBuffer_unlock"));
        return allocate != nullptr && release != nullptr && describe != nullptr &&
               lock != nullptr && unlock != nullptr;
    }
};

struct EglApi {
    DynLib egl{"libEGL.so"};
    DynLib gles{"libGLESv3.so"};
    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture = nullptr;

    bool resolve() {
        if (!egl.loaded()) {
            return false;
        }
        createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            ::eglGetProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            ::eglGetProcAddress("eglDestroyImageKHR"));
        if (gles.loaded()) {
            imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
                egl.symbol("glEGLImageTargetTexture2DOES"));
            if (imageTargetTexture == nullptr) {
                imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
                    gles.symbol("glEGLImageTargetTexture2DOES"));
            }
        }
        return createImage != nullptr && imageTargetTexture != nullptr;
    }
};

std::string eglExtensionSummary() {
    EGLDisplay display = ::eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display == EGL_NO_DISPLAY) {
        return "eglGetDisplay failed";
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (::eglInitialize(display, &major, &minor) != EGL_TRUE) {
        return "eglInitialize failed";
    }
    const char* extensions = ::eglQueryString(display, EGL_EXTENSIONS);
    const std::string text = extensions != nullptr ? std::string(extensions) : std::string();
    const auto has = [&text](const char* name) {
        return text.find(name) != std::string::npos;
    };
    std::string summary = "EGL " + std::to_string(major) + "." + std::to_string(minor);
    summary += std::string(" EGL_ANDROID_image_native_buffer=") +
               (has("EGL_ANDROID_image_native_buffer") ? "1" : "0");
    summary += std::string(" EGL_KHR_image_base=") +
               (has("EGL_KHR_image_base") ? "1" : "0");
    summary += std::string(" EGL_ANDROID_get_native_client_buffer=") +
               (has("EGL_ANDROID_get_native_client_buffer") ? "1" : "0");
    ::eglTerminate(display);
    return summary;
}

std::string currentSelinuxContext() {
    const std::string context = readTextFileTrimmed("/proc/self/attr/current");
    return context.empty() ? std::string("unavailable") : context;
}

std::string selinuxMode() {
    const std::string enforce = readTextFileTrimmed("/sys/fs/selinux/enforce");
    if (enforce == "1") {
        return "enforcing";
    }
    if (enforce == "0") {
        return "permissive";
    }
    return "unknown";
}

std::string displayInventory() {
    // A SurfaceFlinger display id is an opaque handle, not a DRM connector index,
    // so the authoritative source is getPhysicalDisplayIds over the same AIDL that
    // is still missing. DRM connector status is reported for context only.
    std::string text;
    const char* connectors[] = {"/sys/class/drm/card0-DSI-1/status",
                                "/sys/class/drm/card0-DP-1/status",
                                "/sys/class/drm/card0-DSI-2/status"};
    for (const char* path : connectors) {
        const std::string status = readTextFileTrimmed(path);
        if (status.empty()) {
            continue;
        }
        if (!text.empty()) {
            text += " ";
        }
        text += std::string(path) + "=" + status;
    }
    if (text.empty()) {
        text = "no DRM connector status readable";
    }
    text += "; SurfaceFlinger display ids require the (still blocked) AIDL "
            "getPhysicalDisplayIds";
    return text;
}

ProbeReport probeAndroid(bool runLiveZeroCopy);

}  // namespace

#endif  // AURORA_CAPTURE_ANDROID

// ---------------------------------------------------------------------------
// 4. Frame sources
// ---------------------------------------------------------------------------

namespace {

class PrivateAidlSource final : public FrameSource {
public:
    const char* name() const override { return "surfaceflinger-aidl-captureDisplayById"; }
    CapturePath path() const override { return CapturePath::PrivateSurfaceFlingerAidl; }
    bool realtime() const override { return true; }
    bool zeroCopy() const override { return true; }
    bool requiresUserConsent() const override { return false; }

    Availability availability(std::string& reason) const override {
        const ProbeReport report = probeCapabilities();
        reason = report.privateAidlBlockedReason.empty() ? kPrivateAidlBlockedReason
                                                        : report.privateAidlBlockedReason;
        return report.privateAidl;
    }

    bool open(const SourceConfig& config, std::string& error) override {
        if (config.requestSecureContent) {
            error = "policy: FLAG_SECURE content is never requested; the request was refused";
            return false;
        }
        availability(error);
        return false;
    }

    LeaseStatus acquire(FrameLease& lease, std::string& error) override {
        (void)lease;
        error = kPrivateAidlBlockedReason;
        return LeaseStatus::NotSupported;
    }

    void close() override {}
};

class MediaProjectionSource final : public FrameSource {
public:
    const char* name() const override { return "public-mediaprojection"; }
    CapturePath path() const override { return CapturePath::PublicMediaProjection; }
    bool realtime() const override { return true; }
    bool zeroCopy() const override { return false; }
    bool requiresUserConsent() const override { return true; }

    Availability availability(std::string& reason) const override {
        reason =
            "public MediaProjection needs an APK identity and an explicit user consent "
            "token from the system dialog; a root ELF cannot obtain or forge one. Drive "
            "this path through the APK (aurora_root_bootstrap overlay --showcase glass "
            "--capture) instead.";
        return Availability::Unsupported;
    }

    bool open(const SourceConfig& config, std::string& error) override {
        if (config.requestSecureContent) {
            error = "policy: FLAG_SECURE content is never requested; the request was refused";
            return false;
        }
        availability(error);
        return false;
    }

    LeaseStatus acquire(FrameLease& lease, std::string& error) override {
        (void)lease;
        error =
            "MediaProjection is an APK-side, user-authorized flow; the root ELF does not "
            "own a projection token";
        return LeaseStatus::NotSupported;
    }

    void close() override {}
};

#if AURORA_CAPTURE_ANDROID

constexpr const char* kScreencapBinary = "/system/bin/screencap";
constexpr const char* kDefaultDiagnosticPath = "/data/local/tmp/aurora_diagnostic_frame.png";

bool fileExists(const char* path) {
    struct stat info = {};
    return ::stat(path, &info) == 0;
}

int runProcessWait(const std::vector<std::string>& argv) {
    std::vector<char*> raw;
    raw.reserve(argv.size() + 1);
    for (const std::string& part : argv) {
        raw.push_back(const_cast<char*>(part.c_str()));
    }
    raw.push_back(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        return -1;
    }
    if (child == 0) {
        ::execv(raw[0], raw.data());
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128;
}

#endif

class DiagnosticOneShotSource final : public FrameSource {
public:
    const char* name() const override { return "screencap-oneshot-diagnostic"; }
    CapturePath path() const override { return CapturePath::DiagnosticOneShot; }
    bool realtime() const override { return false; }
    bool zeroCopy() const override { return false; }
    bool requiresUserConsent() const override { return false; }

    Availability availability(std::string& reason) const override {
#if AURORA_CAPTURE_ANDROID
        if (!fileExists(kScreencapBinary)) {
            reason = std::string(kScreencapBinary) + " is not present on this ROM";
            return Availability::Unsupported;
        }
        reason =
            "single frame through /system/bin/screencap; PNG encode per call, so it is a "
            "diagnostic fallback only and refuses every realtime request";
        return Availability::Ready;
#else
        reason = "no Android platform: the diagnostic helper is only available on device";
        return Availability::Unsupported;
#endif
    }

    bool open(const SourceConfig& config, std::string& error) override {
        if (config.requestSecureContent) {
            error = "policy: FLAG_SECURE content is never requested; the request was refused";
            return false;
        }
        if (config.realtime) {
            error =
                "diagnostic one-shot source refuses realtime streaming: one PNG encode per "
                "frame is not a frame source. Implement the private AIDL producer or use the "
                "APK MediaProjection path instead.";
            return false;
        }
#if !AURORA_CAPTURE_ANDROID
        error = "diagnostic capture is only implemented on Android";
        return false;
#else
        if (!fileExists(kScreencapBinary)) {
            error = std::string(kScreencapBinary) + " is not present on this ROM";
            return false;
        }
        outputPath_ = config.outputPath.empty() ? kDefaultDiagnosticPath : config.outputPath;
        // The path is passed to execv as a single argv element, never to a shell.
        realtimeRequested_ = false;
        opened_ = true;
        return true;
#endif
    }

    LeaseStatus acquire(FrameLease& lease, std::string& error) override {
#if !AURORA_CAPTURE_ANDROID
        (void)lease;
        error = "diagnostic capture is only implemented on Android";
        return LeaseStatus::NotSupported;
#else
        if (!opened_) {
            error = "source is not open";
            return LeaseStatus::Error;
        }
        if (realtimeRequested_) {
            error = "refused: the diagnostic source never serves a realtime stream";
            return LeaseStatus::Refused;
        }
        const std::vector<std::string> argv{kScreencapBinary, "-p", outputPath_};
        const int code = runProcessWait(argv);
        if (code != 0) {
            error = std::string(kScreencapBinary) + " exited with code " + std::to_string(code);
            return code == -1 ? LeaseStatus::Error : LeaseStatus::Refused;
        }
        FrameDescriptor descriptor;
        descriptor.displayId = 0;
        descriptor.zeroCopy = false;
        descriptor.secure = false;
        descriptor.timestampNanos = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        // No AHardwareBuffer: the frame is a PNG file on disk. Consumers must not
        // treat this as a zero-copy frame.
        lease.adopt(descriptor, nullptr);
        return LeaseStatus::Acquired;
#endif
    }

    void close() override { opened_ = false; }

    const std::string& outputPath() const { return outputPath_; }

private:
    std::string outputPath_;
    bool opened_ = false;
    bool realtimeRequested_ = false;
};

}  // namespace

std::unique_ptr<FrameSource> makePrivateAidlSource() {
    return std::make_unique<PrivateAidlSource>();
}

std::unique_ptr<FrameSource> makeMediaProjectionSource() {
    return std::make_unique<MediaProjectionSource>();
}

std::unique_ptr<FrameSource> makeDiagnosticOneShotSource() {
    return std::make_unique<DiagnosticOneShotSource>();
}

std::unique_ptr<FrameSource> selectRealtimeSource(const ProbeReport& report,
                                                  std::string& reason) {
    if (report.privateAidl == Availability::Ready && kPrivateGuiAbiVendored) {
        return makePrivateAidlSource();
    }
    reason = report.privateAidlBlockedReason.empty() ? kPrivateAidlBlockedReason
                                                    : report.privateAidlBlockedReason;
    return nullptr;
}

// ---------------------------------------------------------------------------
// 5. Zero-copy consumer
// ---------------------------------------------------------------------------

#if AURORA_CAPTURE_ANDROID

struct ZeroCopyGlImporter::Impl {
    bool entryPointsTried = false;
    bool entryPointsReady = false;
    std::string entryPointDetail;

    bool contextTried = false;
    bool contextReady = false;
    bool ownsContext = false;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    EGLConfig config = nullptr;

    PFNEGLCREATEIMAGEKHRPROC createImage = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC imageTargetTexture = nullptr;

    int (*ahbAllocate)(const AHardwareBuffer_Desc*, AHardwareBuffer**) = nullptr;
    void (*ahbRelease)(AHardwareBuffer*) = nullptr;
    void (*ahbDescribe)(const AHardwareBuffer*, AHardwareBuffer_Desc*) = nullptr;
    int (*ahbLock)(AHardwareBuffer*, uint64_t, int32_t, const void*, void**) = nullptr;
    int (*ahbUnlock)(AHardwareBuffer*, int32_t*) = nullptr;

    DynLib eglLibrary{"libEGL.so"};
    DynLib glesLibrary{"libGLESv3.so"};
    DynLib androidLibrary{"libandroid.so"};

    GLuint ownedTexture = 0;
    GLuint ownedFramebuffer = 0;

    bool resolveEntryPoints(std::string& detail) {
        if (entryPointsTried) {
            detail = entryPointDetail;
            return entryPointsReady;
        }
        entryPointsTried = true;

        std::string problems;
        if (!eglLibrary.loaded()) {
            problems += "libEGL.so: " + eglLibrary.error() + "; ";
        }
        if (!androidLibrary.loaded()) {
            problems += "libandroid.so: " + androidLibrary.error() + "; ";
        }
        createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
            ::eglGetProcAddress("eglCreateImageKHR"));
        destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
            ::eglGetProcAddress("eglDestroyImageKHR"));
        imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglLibrary.loaded() ? eglLibrary.symbol("glEGLImageTargetTexture2DOES") : nullptr);
        if (imageTargetTexture == nullptr && glesLibrary.loaded()) {
            imageTargetTexture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
                glesLibrary.symbol("glEGLImageTargetTexture2DOES"));
        }
        ahbAllocate = reinterpret_cast<decltype(ahbAllocate)>(
            androidLibrary.loaded() ? androidLibrary.symbol("AHardwareBuffer_allocate")
                                    : nullptr);
        ahbRelease = reinterpret_cast<decltype(ahbRelease)>(
            androidLibrary.loaded() ? androidLibrary.symbol("AHardwareBuffer_release")
                                    : nullptr);
        ahbDescribe = reinterpret_cast<decltype(ahbDescribe)>(
            androidLibrary.loaded() ? androidLibrary.symbol("AHardwareBuffer_describe")
                                    : nullptr);
        ahbLock = reinterpret_cast<decltype(ahbLock)>(
            androidLibrary.loaded() ? androidLibrary.symbol("AHardwareBuffer_lock") : nullptr);
        ahbUnlock = reinterpret_cast<decltype(ahbUnlock)>(
            androidLibrary.loaded() ? androidLibrary.symbol("AHardwareBuffer_unlock") : nullptr);

        if (createImage == nullptr) {
            problems += "eglCreateImageKHR missing; ";
        }
        if (imageTargetTexture == nullptr) {
            problems += "glEGLImageTargetTexture2DOES missing; ";
        }
        if (ahbAllocate == nullptr || ahbRelease == nullptr || ahbDescribe == nullptr ||
            ahbLock == nullptr || ahbUnlock == nullptr) {
            problems += "libandroid AHardwareBuffer entry points incomplete; ";
        }

        entryPointsReady = problems.empty();
        if (entryPointsReady) {
            entryPointDetail = "eglCreateImageKHR + glEGLImageTargetTexture2DOES + "
                               "AHardwareBuffer_{allocate,describe,lock,unlock,release} resolved";
        } else {
            entryPointDetail = problems;
        }
        detail = entryPointDetail;
        return entryPointsReady;
    }

    bool ensureContext(std::string& detail) {
        if (::eglGetCurrentContext() != EGL_NO_CONTEXT) {
            detail = "using the caller's current GL context";
            contextReady = true;
            ownsContext = false;
            return true;
        }
        if (contextTried && contextReady) {
            detail = "reusing the importer's headless context";
            return true;
        }
        contextTried = true;

        display = ::eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY) {
            detail = "eglGetDisplay(EGL_DEFAULT_DISPLAY) failed";
            return false;
        }
        EGLint major = 0;
        EGLint minor = 0;
        if (::eglInitialize(display, &major, &minor) != EGL_TRUE) {
            detail = "eglInitialize failed";
            return false;
        }
        ::eglBindAPI(EGL_OPENGL_ES_API);
        const EGLint configAttributes[] = {EGL_SURFACE_TYPE,
                                           EGL_PBUFFER_BIT,
                                           EGL_RENDERABLE_TYPE,
                                           EGL_OPENGL_ES2_BIT,
                                           EGL_RED_SIZE,
                                           8,
                                           EGL_GREEN_SIZE,
                                           8,
                                           EGL_BLUE_SIZE,
                                           8,
                                           EGL_ALPHA_SIZE,
                                           8,
                                           EGL_NONE};
        EGLint configCount = 0;
        if (::eglChooseConfig(display, configAttributes, &config, 1, &configCount) != EGL_TRUE ||
            configCount < 1) {
            detail = "eglChooseConfig found no pbuffer config";
            return false;
        }
        const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surface = ::eglCreatePbufferSurface(display, config, surfaceAttributes);
        if (surface == EGL_NO_SURFACE) {
            detail = "eglCreatePbufferSurface failed";
            return false;
        }
        const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = ::eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
        if (context == EGL_NO_CONTEXT) {
            detail = "eglCreateContext(ES3) failed";
            return false;
        }
        if (::eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            detail = "eglMakeCurrent failed";
            return false;
        }
        ownsContext = true;
        contextReady = true;
        detail = "headless EGL " + std::to_string(major) + "." + std::to_string(minor) +
                 " pbuffer context created (ES3)";
        return true;
    }

    // The display the EGLImage was created on: the headless one we own, or the
    // caller's current display when a GL context was already current.
    EGLDisplay activeDisplay() const {
        return display != EGL_NO_DISPLAY ? display : ::eglGetCurrentDisplay();
    }

    EGLImageKHR createImageFrom(AHardwareBuffer* buffer) {
        const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        return createImage(activeDisplay(), EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                           static_cast<EGLClientBuffer>(buffer), attributes);
    }
};

ZeroCopyGlImporter::ZeroCopyGlImporter() : impl_(std::make_unique<Impl>()) {}
ZeroCopyGlImporter::~ZeroCopyGlImporter() { shutdown(); }

bool ZeroCopyGlImporter::initialize(std::string& detail) {
    if (impl_ == nullptr) {
        detail = "importer is not available on this platform";
        return false;
    }
    return impl_->resolveEntryPoints(detail);
}

bool ZeroCopyGlImporter::initialized() const {
    return impl_ != nullptr && impl_->entryPointsReady;
}

bool ZeroCopyGlImporter::import(const FrameDescriptor& frame, FrameImport& out) {
    out = FrameImport{};
    if (impl_ == nullptr) {
        out.ok = false;
        out.detail = "zero-copy import is only implemented on Android";
        return false;
    }
    if (frame.secure) {
        out.ok = false;
        out.detail = "refused: the frame contained FLAG_SECURE layers";
        return false;
    }
    if (frame.hardwareBuffer == nullptr) {
        out.ok = false;
        out.detail = "no AHardwareBuffer on this frame (diagnostic PNG frames cannot be "
                     "imported zero copy)";
        return false;
    }
    std::string detail;
    if (!impl_->resolveEntryPoints(detail)) {
        out.detail = detail;
        return false;
    }
    if (!impl_->ensureContext(detail)) {
        out.detail = detail;
        return false;
    }

    auto* buffer = static_cast<AHardwareBuffer*>(frame.hardwareBuffer);
    EGLImageKHR image = impl_->createImageFrom(buffer);
    if (image == EGL_NO_IMAGE_KHR) {
        out.detail = "eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID) failed, eglGetError=" +
                     std::to_string(::eglGetError());
        return false;
    }
    if (impl_->ownedTexture == 0) {
        ::glGenTextures(1, &impl_->ownedTexture);
    }
    ::glBindTexture(GL_TEXTURE_2D, impl_->ownedTexture);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    impl_->imageTargetTexture(GL_TEXTURE_2D, image);
    const GLenum error = ::glGetError();
    impl_->destroyImage(impl_->activeDisplay(), image);
    if (error != GL_NO_ERROR) {
        out.detail = "glEGLImageTargetTexture2DOES reported GL error " + std::to_string(error);
        return false;
    }
    out.ok = true;
    out.textureId = impl_->ownedTexture;
    out.zeroCopy = true;
    out.glError = 0;
    out.detail = std::to_string(frame.width) + "x" + std::to_string(frame.height) +
                 " AHardwareBuffer imported by reference (no pixel copy)";
    return true;
}

bool ZeroCopyGlImporter::runSelfTest(std::string& detail) {
    if (impl_ == nullptr) {
        detail = "zero-copy self-test is only implemented on Android";
        return false;
    }
    std::string step;
    if (!impl_->resolveEntryPoints(step)) {
        detail = step;
        return false;
    }

    constexpr int32_t kSize = 64;
    const uint32_t usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                           AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
                           AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN;
    AHardwareBuffer_Desc description = {};
    description.width = kSize;
    description.height = kSize;
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage = usage;
    AHardwareBuffer* buffer = nullptr;
    const int allocateResult = impl_->ahbAllocate(&description, &buffer);
    if (allocateResult != 0 || buffer == nullptr) {
        detail = "AHardwareBuffer_allocate failed: " + std::to_string(allocateResult);
        return false;
    }

    bool ok = false;
    EGLImageKHR image = EGL_NO_IMAGE_KHR;
    GLuint framebuffer = 0;
    do {
        if (!impl_->ensureContext(step)) {
            detail = step;
            break;
        }
        image = impl_->createImageFrom(buffer);
        if (image == EGL_NO_IMAGE_KHR) {
            detail = "eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID) failed, eglGetError=" +
                     std::to_string(::eglGetError());
            break;
        }
        if (impl_->ownedTexture == 0) {
            ::glGenTextures(1, &impl_->ownedTexture);
        }
        ::glBindTexture(GL_TEXTURE_2D, impl_->ownedTexture);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        impl_->imageTargetTexture(GL_TEXTURE_2D, image);
        if (::glGetError() != GL_NO_ERROR) {
            detail = "glEGLImageTargetTexture2DOES failed";
            break;
        }
        ::glGenFramebuffers(1, &framebuffer);
        ::glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        ::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                 impl_->ownedTexture, 0);
        const GLenum status = ::glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            detail = "framebuffer incomplete: 0x" + std::to_string(status);
            break;
        }

        // Write the pattern through the CPU AFTER the EGLImage exists. If the
        // import had copied pixels, the readback below would not see this data.
        AHardwareBuffer_Desc actual = {};
        impl_->ahbDescribe(buffer, &actual);
        void* pixels = nullptr;
        const int lockResult =
            impl_->ahbLock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, -1, nullptr, &pixels);
        if (lockResult != 0 || pixels == nullptr) {
            detail = "AHardwareBuffer_lock(CPU_WRITE) failed: " + std::to_string(lockResult);
            break;
        }
        for (int32_t row = 0; row < kSize; ++row) {
            auto* line = reinterpret_cast<uint8_t*>(pixels) +
                         static_cast<std::size_t>(row) * actual.stride * 4;
            for (int32_t column = 0; column < kSize; ++column) {
                line[column * 4 + 0] = static_cast<uint8_t>(column * 4);
                line[column * 4 + 1] = static_cast<uint8_t>(row * 4);
                line[column * 4 + 2] = 0x40;
                line[column * 4 + 3] = 0xff;
            }
        }
        int32_t fence = -1;
        const int unlockResult = impl_->ahbUnlock(buffer, &fence);
        if (unlockResult != 0) {
            detail = "AHardwareBuffer_unlock failed: " + std::to_string(unlockResult);
            break;
        }

        std::vector<uint8_t> readback(static_cast<std::size_t>(kSize) * kSize * 4, 0);
        ::glFinish();
        ::glReadPixels(0, 0, kSize, kSize, GL_RGBA, GL_UNSIGNED_BYTE, readback.data());
        const GLenum readError = ::glGetError();
        if (readError != GL_NO_ERROR) {
            detail = "glReadPixels failed: 0x" + std::to_string(readError);
            break;
        }
        // GL reads bottom-up; compare one row from each end.
        bool matched = true;
        for (int32_t column = 0; column < kSize; ++column) {
            const std::size_t index = static_cast<std::size_t>(column) * 4;
            const uint8_t expectedR = static_cast<uint8_t>(column * 4);
            if (readback[index + 0] != expectedR || readback[index + 3] != 0xff) {
                matched = false;
                break;
            }
        }
        if (!matched) {
            detail = "zero-copy content mismatch: CPU writes to the AHardwareBuffer are not "
                     "visible through the imported texture";
            break;
        }
        detail = "AHardwareBuffer " + std::to_string(actual.width) + "x" +
                 std::to_string(actual.height) + " stride=" + std::to_string(actual.stride) +
                 " imported as EGLImage + GL texture; CPU writes made after the import are "
                 "visible in glReadPixels (shared allocation, no copy)";
        ok = true;
    } while (false);

    if (framebuffer != 0) {
        ::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ::glDeleteFramebuffers(1, &framebuffer);
    }
    if (image != EGL_NO_IMAGE_KHR) {
        impl_->destroyImage(impl_->activeDisplay(), image);
    }
    impl_->ahbRelease(buffer);
    return ok;
}

void ZeroCopyGlImporter::shutdown() {
    if (impl_ == nullptr) {
        return;
    }
    // GL objects can only be released while a context is current.
    const bool hasContext = ::eglGetCurrentContext() != EGL_NO_CONTEXT;
    if (hasContext && impl_->ownedFramebuffer != 0) {
        ::glBindFramebuffer(GL_FRAMEBUFFER, 0);
        ::glDeleteFramebuffers(1, &impl_->ownedFramebuffer);
    }
    impl_->ownedFramebuffer = 0;
    if (hasContext && impl_->ownedTexture != 0) {
        ::glDeleteTextures(1, &impl_->ownedTexture);
    }
    impl_->ownedTexture = 0;
    if (impl_->ownsContext && impl_->display != EGL_NO_DISPLAY) {
        ::eglMakeCurrent(impl_->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (impl_->context != EGL_NO_CONTEXT) {
            ::eglDestroyContext(impl_->display, impl_->context);
            impl_->context = EGL_NO_CONTEXT;
        }
        if (impl_->surface != EGL_NO_SURFACE) {
            ::eglDestroySurface(impl_->display, impl_->surface);
            impl_->surface = EGL_NO_SURFACE;
        }
        ::eglTerminate(impl_->display);
        impl_->display = EGL_NO_DISPLAY;
        impl_->ownsContext = false;
        impl_->contextReady = false;
    }
}

#else  // !AURORA_CAPTURE_ANDROID

struct ZeroCopyGlImporter::Impl {};

ZeroCopyGlImporter::ZeroCopyGlImporter() : impl_(std::make_unique<Impl>()) {}
ZeroCopyGlImporter::~ZeroCopyGlImporter() = default;

bool ZeroCopyGlImporter::initialize(std::string& detail) {
    detail = "zero-copy AHardwareBuffer/EGLImage import requires an Android device";
    return false;
}

bool ZeroCopyGlImporter::initialized() const { return false; }

bool ZeroCopyGlImporter::import(const FrameDescriptor& frame, FrameImport& out) {
    out = FrameImport{};
    if (frame.secure) {
        out.detail = "refused: the frame contained FLAG_SECURE layers";
        return false;
    }
    out.detail = "zero-copy import is only implemented on Android";
    return false;
}

bool ZeroCopyGlImporter::runSelfTest(std::string& detail) {
    detail = "zero-copy self-test requires an Android device";
    return false;
}

void ZeroCopyGlImporter::shutdown() {}

#endif  // AURORA_CAPTURE_ANDROID

// ---------------------------------------------------------------------------
// probeCapabilities
// ---------------------------------------------------------------------------

#if AURORA_CAPTURE_ANDROID

namespace {

ProbeReport probeAndroid(bool runLiveZeroCopy) {
    ProbeReport report;
    report.requiredInterfaces = requiredPrivateInterfaces();

    // --- root identity -----------------------------------------------------
    const uid_t uid = ::getuid();
    const gid_t gid = ::getgid();
    {
        const std::string context = currentSelinuxContext();
        const bool root = uid == 0;
        addCheck(report, root ? ProbeOutcome::Pass : ProbeOutcome::Fail, "root-identity",
                 "uid=" + std::to_string(uid) + " gid=" + std::to_string(gid) +
                     " selinux_context=" + context +
                     (root ? "" : " (root is required for the private path)"));
    }
    addCheck(report, ProbeOutcome::Info, "selinux-mode", "mode=" + selinuxMode());

    // --- binder reachability ----------------------------------------------
    {
        std::string target = "/dev/binder";
        char link[256] = {};
        const ssize_t length = ::readlink("/dev/binder", link, sizeof(link) - 1);
        if (length > 0) {
            target = std::string("/dev/binder -> ") + std::string(link, static_cast<std::size_t>(length));
        }
        const bool accessible = ::access("/dev/binder", R_OK | W_OK) == 0;
        addCheck(report, accessible ? ProbeOutcome::Pass : ProbeOutcome::Fail, "binder-device",
                 target + (accessible ? " rw=1" : " rw=0 (binder is not reachable)"));
    }

    // --- SurfaceFlinger AIDL service --------------------------------------
    BinderNdk binder;
    if (!binder.resolve()) {
        addCheck(report, ProbeOutcome::Fail, "libbinder-ndk",
                 "libbinder_ndk.so service manager entry points unavailable: " +
                     binder.library.error());
    } else {
        addCheck(report, ProbeOutcome::Pass, "libbinder-ndk",
                 "AServiceManager_getService + AIBinder_isAlive resolved from libbinder_ndk.so");
        std::string detail;
        std::string failure = binder.probeInstance("SurfaceFlingerAIDL", detail);
        addCheck(report, failure.empty() ? ProbeOutcome::Pass : ProbeOutcome::Fail,
                 "surfaceflinger-aidl-service",
                 failure.empty() ? detail + " descriptor=android.gui.ISurfaceComposer"
                                 : failure);
        std::string legacyDetail;
        const std::string legacyFailure = binder.probeInstance("SurfaceFlinger", legacyDetail);
        addCheck(report, ProbeOutcome::Info, "surfaceflinger-legacy-service",
                 legacyFailure.empty() ? legacyDetail + " descriptor=android.ui.ISurfaceComposer"
                                       : legacyDetail);
    }

    // --- libgui private AIDL proxy symbols --------------------------------
    DynLib guiLibrary{"libgui.so"};
    if (!guiLibrary.loaded()) {
        addCheck(report, ProbeOutcome::Fail, "libgui-private-abi",
                 "dlopen(libgui.so) failed: " + guiLibrary.error());
    } else {
        std::vector<std::string> missing;
        for (const SymbolExpectation& expectation : kSurfaceFlingerAidlSymbols) {
            if (guiLibrary.symbol(expectation.symbol) == nullptr) {
                missing.emplace_back(expectation.label);
            }
        }
        const std::size_t total = sizeof(kSurfaceFlingerAidlSymbols) /
                                  sizeof(kSurfaceFlingerAidlSymbols[0]);
        if (missing.empty()) {
            addCheck(report, ProbeOutcome::Pass, "libgui-private-abi",
                     "all " + std::to_string(total) +
                         " AIDL capture proxy symbols exported by /system/lib64/libgui.so");
        } else {
            std::string detail = std::to_string(missing.size()) + "/" + std::to_string(total) +
                                 " symbols missing: ";
            for (std::size_t index = 0; index < missing.size(); ++index) {
                if (index != 0) {
                    detail += ", ";
                }
                detail += missing[index];
            }
            addCheck(report, ProbeOutcome::Fail, "libgui-private-abi", detail);
        }
        addCheck(report, ProbeOutcome::Info, "libgui-descriptor-symbol",
                 "ISurfaceComposer::descriptor at runtime = " +
                     readDescriptorSymbol(
                         guiLibrary, "_ZN7android3gui16ISurfaceComposer10descriptorE"));
    }

    // --- AHardwareBuffer ---------------------------------------------------
    AhbApi ahb;
    const bool ahbReady = ahb.resolve();
    addCheck(report, ahbReady ? ProbeOutcome::Pass : ProbeOutcome::Fail, "ahardwarebuffer-api",
             ahbReady ? "AHardwareBuffer_{allocate,release,describe,lock,unlock} resolved from "
                        "libandroid.so"
                      : "libandroid.so AHardwareBuffer entry points incomplete: " +
                            ahb.library.error());

    // --- EGLImage zero-copy entry points ----------------------------------
    EglApi egl;
    const bool eglReady = egl.resolve();
    const std::string extensionSummary = eglExtensionSummary();
    addCheck(report, eglReady ? ProbeOutcome::Pass : ProbeOutcome::Fail,
             "egl-image-native-buffer",
             (eglReady ? "eglCreateImageKHR + glEGLImageTargetTexture2DOES resolved; "
                       : "missing EGLImage entry points; ") +
                 extensionSummary);

    // --- public MediaProjection (must stay distinguishable) ---------------
    addCheck(report, ProbeOutcome::Info, "public-mediaprojection",
             "requires an APK identity + a user-confirmed consent token; a root ELF cannot "
             "obtain one, so this stays the public authorized path only");
    addCheck(report, ProbeOutcome::Info, "flag-secure-policy",
             "FLAG_SECURE layers are never requested, never captured and never imported");
    addCheck(report, ProbeOutcome::Info, "no-process-injection",
             "no ptrace/hook/LD_PRELOAD of another process is used anywhere in this binary");

    // --- diagnostic fallback ----------------------------------------------
    const bool screencapPresent = fileExists(kScreencapBinary);
    addCheck(report, screencapPresent ? ProbeOutcome::Pass : ProbeOutcome::Fail,
             "diagnostic-screencap",
             std::string(kScreencapBinary) +
                 (screencapPresent
                      ? " present (single frame, PNG encode, non realtime, diagnostic only)"
                      : " missing"));

    // --- display inventory -------------------------------------------------
    addCheck(report, ProbeOutcome::Info, "display-inventory", displayInventory());

    // --- zero-copy consumer live check ------------------------------------
    if (runLiveZeroCopy) {
        ZeroCopyGlImporter importer;
        std::string detail;
        const bool ok = importer.runSelfTest(detail);
        addCheck(report, ok ? ProbeOutcome::Pass : ProbeOutcome::Fail, "zerocopy-live-import",
                 detail);
        report.zeroCopyConsumer = ok ? Availability::Ready : Availability::Blocked;
    } else {
        addCheck(report, ProbeOutcome::Info, "zerocopy-live-import",
                 "skipped (--no-live); entry point presence already checked");
        report.zeroCopyConsumer = (eglReady && ahbReady) ? Availability::Ready
                                                        : Availability::Blocked;
    }

    // --- verdicts ----------------------------------------------------------
    const bool isRoot = uid == 0;
    const bool symbolsPresent = std::none_of(
        report.checks.begin(), report.checks.end(), [](const ProbeCheck& check) {
            return check.id == "libgui-private-abi" && check.outcome == ProbeOutcome::Fail;
        });
    const bool servicePresent = std::none_of(
        report.checks.begin(), report.checks.end(), [](const ProbeCheck& check) {
            return check.id == "surfaceflinger-aidl-service" &&
                   check.outcome == ProbeOutcome::Fail;
        });

    report.privateAidl = Availability::Blocked;
    std::string blockedReason;
    if (!kPrivateGuiAbiVendored) {
        blockedReason =
            "no ROM-matched android.gui AIDL declarations are compiled into this binary "
            "(CaptureArgs / DisplayCaptureArgs / LayerCaptureArgs / ScreenCaptureResults "
            "parcel layouts and the IBinder/Parcel client ABI). The platform symbols exist, "
            "but a mangled symbol is not a usable ABI.";
    }
    if (!isRoot) {
        blockedReason += " The process does not run as uid 0.";
    }
    if (!servicePresent) {
        blockedReason += " SurfaceFlingerAIDL is not reachable through the service manager.";
    }
    if (!symbolsPresent) {
        blockedReason += " Some libgui capture proxy symbols are missing on this ROM.";
    }
    blockedReason += " Verified on this device: service=" +
                     std::string(servicePresent ? "reachable" : "unreachable") + ", proxy symbols=" +
                     std::string(symbolsPresent ? "complete" : "incomplete") + ".";
    report.privateAidlBlockedReason = blockedReason;

    report.publicMediaProjection = Availability::Unsupported;
    report.diagnosticFallback = screencapPresent ? Availability::Ready : Availability::Unsupported;
    return report;
}

}  // namespace

ProbeReport probeCapabilities() { return probeAndroid(true); }

ProbeReport probeCapabilities(bool runLiveZeroCopy) { return probeAndroid(runLiveZeroCopy); }

#else  // !AURORA_CAPTURE_ANDROID

ProbeReport probeCapabilities() {
    ProbeReport report;
    report.requiredInterfaces = requiredPrivateInterfaces();
    addCheck(report, ProbeOutcome::Info, "host-build",
             "desktop host build: device probes (binder, libgui AIDL proxy, "
             "AHardwareBuffer/EGLImage, screencap) are only executed on the Android target");
    addCheck(report, ProbeOutcome::Info, "flag-secure-policy",
             "FLAG_SECURE layers are never requested, never captured and never imported");
    report.privateAidl = Availability::Unsupported;
    report.privateAidlBlockedReason =
        "host build: the SurfaceFlinger AIDL probe only runs on Android";
    report.publicMediaProjection = Availability::Unsupported;
    report.zeroCopyConsumer = Availability::Unsupported;
    report.diagnosticFallback = Availability::Unsupported;
    return report;
}

ProbeReport probeCapabilities(bool runLiveZeroCopy) {
    (void)runLiveZeroCopy;
    return probeCapabilities();
}

#endif  // AURORA_CAPTURE_ANDROID

// ---------------------------------------------------------------------------
// 2b. Host testable self-test
// ---------------------------------------------------------------------------

int runSelfTest(std::string& output) {
    int failures = 0;
    const auto check = [&output, &failures](bool ok, const std::string& name) {
        output += ok ? "ok   " : "FAIL ";
        output += name;
        output += "\n";
        if (!ok) {
            ++failures;
        }
    };

    check(std::string(capturePathToken(CapturePath::PrivateSurfaceFlingerAidl)) ==
              "private-sf-aidl",
          "token.private-aidl");
    check(std::string(capturePathToken(CapturePath::PublicMediaProjection)) ==
              "public-mediaproj",
          "token.public-mediaprojection");
    check(std::string(capturePathToken(CapturePath::DiagnosticOneShot)) ==
              "diagnostic-oneshot",
          "token.diagnostic-oneshot");
    check(std::string(availabilityToken(Availability::Blocked)) == "blocked",
          "token.availability.blocked");
    check(std::string(leaseStatusToken(LeaseStatus::Refused)) == "refused",
          "token.lease.refused");

    check(!CapturePolicy::kCaptureSecureContent, "policy.never-capture-flag-secure");
    check(!CapturePolicy::kInjectIntoOtherProcesses, "policy.never-inject-other-process");
    check(!CapturePolicy::kFabricateCaptureResults, "policy.never-fabricate-results");

    // A lease must release exactly once and must not leak the callback.
    static int releases = 0;
    releases = 0;
    {
        FrameLease lease;
        check(!lease.valid(), "lease.default-invalid");
        FrameDescriptor descriptor;
        descriptor.width = 8;
        descriptor.height = 4;
        descriptor.zeroCopy = true;
        descriptor.hardwareBuffer = reinterpret_cast<void*>(0x10);
        lease.adopt(descriptor, [](void*) { ++releases; });
        check(lease.valid() && lease.descriptor().width == 8, "lease.adopt");
        FrameLease moved = std::move(lease);
        check(moved.valid() && !lease.valid(), "lease.move");
    }
    check(releases == 1, "lease.release-once");

    // Report formatting is the CLI contract.
    ProbeReport report;
    addCheck(report, ProbeOutcome::Pass, "unit-check", "detail");
    const std::string text = formatProbeReport(report);
    check(text.find("probe[pass] unit-check: detail") != std::string::npos,
          "report.check-line");
    check(text.find("verdict.private-aidl=unknown") != std::string::npos,
          "report.verdict-line");
    check(report.passCount() == 1 && report.failCount() == 0, "report.summary-counts");

    // A blocked report never yields a realtime source.
    std::string reason;
    std::unique_ptr<FrameSource> source = selectRealtimeSource(report, reason);
    check(source == nullptr && !reason.empty(),
          "select.realtime-source-refused-when-blocked");

    // The diagnostic source is not realtime and refuses realtime requests.
    std::unique_ptr<FrameSource> diagnostic = makeDiagnosticOneShotSource();
    check(diagnostic != nullptr && !diagnostic->realtime(), "diagnostic.not-realtime");
    check(diagnostic->path() == CapturePath::DiagnosticOneShot, "diagnostic.path-token");
    SourceConfig streaming;
    streaming.realtime = true;
    std::string error;
    check(!diagnostic->open(streaming, error) && !error.empty(),
          "diagnostic.refuses-realtime-open");

    // Policy: a secure-content request is refused on every source.
    SourceConfig secure;
    secure.realtime = false;
    secure.requestSecureContent = true;
    std::string secureError;
    check(!diagnostic->open(secure, secureError) && !secureError.empty(),
          "diagnostic.refuses-secure-request");
    std::string privateError;
    check(!makePrivateAidlSource()->open(secure, privateError), "private.refuses-secure-request");
    std::string projectionError;
    check(!makeMediaProjectionSource()->open(secure, projectionError),
          "projection.refuses-secure-request");

    // The MediaProjection path must stay a consent-requiring, separate path.
    std::unique_ptr<FrameSource> projection = makeMediaProjectionSource();
    check(projection->requiresUserConsent(), "projection.requires-consent");
    check(projection->path() == CapturePath::PublicMediaProjection, "projection.path-token");
    std::string projectionReason;
    check(projection->availability(projectionReason) != Availability::Ready,
          "projection.not-ready-from-root");

    // The zero-copy consumer refuses secure frames even when it is not available.
    ZeroCopyGlImporter importer;
    FrameDescriptor secureFrame;
    secureFrame.secure = true;
    FrameImport import;
    check(!importer.import(secureFrame, import) &&
              import.detail.find("FLAG_SECURE") != std::string::npos,
          "importer.refuses-secure-frame");

    output += failures == 0 ? "selftest: PASS\n"
                            : "selftest: FAIL failures=" + std::to_string(failures) + "\n";
    return failures == 0 ? 0 : 1;
}

std::string usageText() {
    return std::string(
        "Aurora Root desktop capture (SurfaceFlinger AIDL capability probe + frame source)\n"
        "\n"
        "Usage:\n"
        "  aurora_root_capture probe [--no-live]\n"
        "  aurora_root_capture sources\n"
        "  aurora_root_capture selftest\n"
        "  aurora_root_capture zerocopy-selftest\n"
        "  aurora_root_capture diagnostic-capture [--out PATH]\n"
        "  aurora_root_capture frames --realtime [--display ID]\n"
        "  aurora_root_capture help\n"
        "\n"
        "Commands:\n"
        "  probe              probe the device capabilities and print the verdicts\n"
        "                     (--no-live skips the live AHardwareBuffer/EGLImage import)\n"
        "  sources            list every frame source and its availability\n"
        "  selftest           assert the contract in RootDesktopCapture.h (device or host)\n"
        "  zerocopy-selftest  run the headless AHardwareBuffer -> EGLImage -> GL texture\n"
        "                     import round trip on this device\n"
        "  diagnostic-capture run /system/bin/screencap exactly once (non realtime)\n"
        "  frames             request a frame stream; refuses when no realtime source exists\n"
        "\n"
        "Policy enforced by this binary:\n"
        "  * FLAG_SECURE content is never requested, captured or imported.\n"
        "  * No process injection, hooking or LD_PRELOAD of another process is used.\n"
        "  * The public MediaProjection path stays an APK-side, user-authorized flow; root\n"
        "    does not obtain a projection token and this binary never fakes one.\n"
        "  * A missing capability is reported as blocked, never as a partial success.\n"
        "  * /system/bin/screencap is a diagnostic single-frame helper and is never used as\n"
        "    the realtime frame source (a PNG encode per frame is not a frame source).\n"
        "\n"
        "Exit codes:\n"
        "  0  the requested probe/command succeeded\n"
        "  1  a self-test assertion failed\n"
        "  2  usage error\n"
        "  3  the requested capture path is blocked/unsupported (reported, never faked)\n"
        "  4  a required platform helper could not be executed\n");
}

}  // namespace aurora::root::capture

// ---------------------------------------------------------------------------
// 6. CLI
// ---------------------------------------------------------------------------

#if defined(AURORA_ROOT_CAPTURE_CLI)

#include <iostream>

namespace {

constexpr int kExitSuccess = 0;
constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitBlocked = 3;
constexpr int kExitEnvironment = 4;

using aurora::root::capture::Availability;
using aurora::root::capture::CapturePath;
using aurora::root::capture::FrameSource;
using aurora::root::capture::LeaseStatus;
using aurora::root::capture::ProbeReport;
using aurora::root::capture::SourceConfig;

void note(const std::string& line) { std::cout << line << '\n'; }
void fail(const std::string& line) { std::cerr << "Aurora capture: error: " << line << '\n'; }

int commandProbe(bool live) {
    const ProbeReport report = aurora::root::capture::probeCapabilities(live);
    std::cout << aurora::root::capture::formatProbeReport(report);
    return kExitSuccess;
}

int commandSources() {
    const ProbeReport report = aurora::root::capture::probeCapabilities();
    const std::unique_ptr<FrameSource> sources[] = {
        aurora::root::capture::makePrivateAidlSource(),
        aurora::root::capture::makeMediaProjectionSource(),
        aurora::root::capture::makeDiagnosticOneShotSource(),
    };
    for (const std::unique_ptr<FrameSource>& source : sources) {
        std::string reason;
        const Availability availability = source->availability(reason);
        note(std::string("source=") + source->name() +
             " path=" + aurora::root::capture::capturePathToken(source->path()) +
             " availability=" + aurora::root::capture::availabilityToken(availability) +
             " realtime=" + (source->realtime() ? "1" : "0") +
             " zero_copy=" + (source->zeroCopy() ? "1" : "0") +
             " user_consent=" + (source->requiresUserConsent() ? "1" : "0"));
        note("  description: " +
             std::string(aurora::root::capture::capturePathDescription(source->path())));
        note("  reason: " + reason);
    }
    note("selected_realtime_source=" +
         std::string(report.privateAidl == Availability::Ready ? "private-sf-aidl" : "none"));
    note("selection_reason=" + report.privateAidlBlockedReason);
    return report.privateAidl == Availability::Ready ? kExitSuccess : kExitBlocked;
}

int commandZeroCopySelfTest() {
    aurora::root::capture::ZeroCopyGlImporter importer;
    std::string detail;
    if (!importer.initialize(detail)) {
        note("zerocopy_initialize=failed");
        note("detail=" + detail);
        return kExitBlocked;
    }
    note("zerocopy_initialize=ok");
    note("detail=" + detail);
    std::string testDetail;
    const bool ok = importer.runSelfTest(testDetail);
    note(std::string("zerocopy_live_import=") + (ok ? "ok" : "failed"));
    note("detail=" + testDetail);
    return ok ? kExitSuccess : kExitFailure;
}

int commandDiagnosticCapture(const std::string& outputPath, bool realtimeRequested) {
    std::unique_ptr<FrameSource> source = aurora::root::capture::makeDiagnosticOneShotSource();
    std::string reason;
    const Availability availability = source->availability(reason);
    if (availability != Availability::Ready) {
        fail("diagnostic capture is unavailable: " + reason);
        return kExitBlocked;
    }
    SourceConfig config;
    config.realtime = realtimeRequested;
    config.outputPath = outputPath;
    std::string error;
    if (!source->open(config, error)) {
        fail(error);
        return realtimeRequested ? kExitBlocked : kExitEnvironment;
    }
    aurora::root::capture::FrameLease lease;
    const LeaseStatus status = source->acquire(lease, error);
    if (status != LeaseStatus::Acquired) {
        fail(std::string("diagnostic capture failed (") +
             aurora::root::capture::leaseStatusToken(status) + "): " + error);
        return kExitEnvironment;
    }
    note("diagnostic_frame=written");
    note("path=" + (outputPath.empty() ? std::string("/data/local/tmp/aurora_diagnostic_frame.png")
                                       : outputPath));
    note("realtime=0 zero_copy=0 note=diagnostic single frame only; not a streaming source");
    source->close();
    return kExitSuccess;
}

int commandFrames(bool realtime, int64_t displayId) {
    const ProbeReport report = aurora::root::capture::probeCapabilities();
    std::string reason;
    if (realtime) {
        std::unique_ptr<FrameSource> source =
            aurora::root::capture::selectRealtimeSource(report, reason);
        if (source == nullptr) {
            fail("no realtime frame source is available: " + reason);
            note("hint: run 'aurora_root_capture probe' for the capability evidence and the "
                 "list of required interfaces; the diagnostic helper is one-shot only and is "
                 "deliberately not offered here");
            return kExitBlocked;
        }
        SourceConfig config;
        config.displayId = displayId;
        config.realtime = true;
        std::string error;
        if (!source->open(config, error)) {
            fail(error);
            return kExitBlocked;
        }
        note("realtime_source=" + std::string(source->name()));
        source->close();
        return kExitSuccess;
    }
    fail("frames currently requires --realtime; the diagnostic one-shot is a separate command "
         "so it can never be mistaken for a stream");
    return kExitUsage;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
    for (int index = 1; index < argc; ++index) {
        args.emplace_back(argv[index]);
    }
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::cout << aurora::root::capture::usageText();
        return args.empty() ? kExitUsage : kExitSuccess;
    }

    const std::string command = args[0];
    std::string outputPath;
    bool realtime = false;
    bool live = true;
    int64_t displayId = 0;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string& arg = args[index];
        if (arg == "--no-live") {
            live = false;
        } else if (arg == "--realtime") {
            realtime = true;
        } else if (arg == "--out") {
            if (index + 1 >= args.size()) {
                fail("--out requires a path");
                return kExitUsage;
            }
            outputPath = args[++index];
        } else if (arg == "--display") {
            if (index + 1 >= args.size()) {
                fail("--display requires an id");
                return kExitUsage;
            }
            displayId = std::strtoll(args[++index].c_str(), nullptr, 10);
        } else {
            fail("unknown option: " + arg);
            std::cerr << aurora::root::capture::usageText();
            return kExitUsage;
        }
    }
    (void)live;

    if (command == "probe") {
        return commandProbe(live);
    }
    if (command == "sources") {
        return commandSources();
    }
    if (command == "selftest") {
        std::string text;
        const int result = aurora::root::capture::runSelfTest(text);
        std::cout << text;
        return result == 0 ? kExitSuccess : kExitFailure;
    }
    if (command == "zerocopy-selftest") {
        return commandZeroCopySelfTest();
    }
    if (command == "diagnostic-capture") {
        return commandDiagnosticCapture(outputPath, realtime);
    }
    if (command == "frames") {
        return commandFrames(realtime, displayId);
    }

    fail("unknown command: " + command);
    std::cerr << aurora::root::capture::usageText();
    return kExitUsage;
}

#endif  // AURORA_ROOT_CAPTURE_CLI
