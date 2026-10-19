#ifndef AURORA_ROOT_HOST_ROOT_DESKTOP_CAPTURE_H
#define AURORA_ROOT_HOST_ROOT_DESKTOP_CAPTURE_H

// Aurora Root desktop frame source: capability probe, provider contract and
// zero-copy consumer plumbing.
//
// WHAT THIS MODULE IS
// -------------------
// A root-only desktop frame source for the Aurora native host. It is split into
// two independent halves so a blocked half can never fake the other one:
//
//   1. A capability probe that reports, with device evidence, whether the
//      private SurfaceFlinger AIDL capture path is actually usable on this ROM.
//   2. A frame source contract (FrameSource) plus sources:
//        - PrivateAidlSource      : SurfaceFlinger AIDL captureDisplayById /
//                                   ScreenCaptureResults -> GraphicBuffer ->
//                                   AHardwareBuffer, zero copy.
//        - PublicMediaProjection  : the public, user-authorized Android path.
//                                   It needs an APK identity + consent token and
//                                   is therefore NOT available to a root ELF.
//        - DiagnosticOneShot      : /system/bin/screencap single frame. This is a
//                                   diagnostic fallback only; it is explicitly
//                                   non-realtime and never the streaming path.
//   3. A zero-copy consumer (ZeroCopyGlImporter) that turns an AHardwareBuffer
//      into an EGLImage / GL texture without a copy. This half depends only on
//      public Android NDK APIs, so it can be verified on device even while the
//      producer half is blocked by a missing private ABI.
//
// WHAT THIS MODULE REFUSES TO DO (hard policy, not a configuration option)
// -----------------------------------------------------------------------
//   - It never captures FLAG_SECURE content and never asks SurfaceFlinger for
//     secure layers. Content marked FLAG_SECURE is reported as secure and left
//     blank; the policy is not overridable from the command line.
//   - It never injects into, hooks or reads the memory of another process. The
//     only inputs are the binder capture transaction (when available) and the
//     platform's own screencap helper (diagnostic only).
//   - It never replaces the public MediaProjection authorization path. Root does
//     not grant MediaProjection consent, so that path stays an APK-side,
//     user-confirmed flow.
//   - It never fabricates a completed capture. When the private ABI is missing
//     the probe reports Blocked with the exact missing interfaces.
//
// The pure parts of this header (tokens, report formatting, policy and source
// selection) compile on a desktop host, so the contract is testable without a
// device. Every device-specific probe lives in RootDesktopCapture.cpp.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aurora::root::capture {

// ---------------------------------------------------------------------------
// Hard policy
// ---------------------------------------------------------------------------

struct CapturePolicy {
    // FLAG_SECURE layers are never decoded, never exported and never uploaded to
    // the Aurora renderer. There is no API to change this.
    static constexpr bool kCaptureSecureContent = false;
    // The frame source talks to SurfaceFlinger's capture API only; it never
    // injects into, hooks or reads another app's process.
    static constexpr bool kInjectIntoOtherProcesses = false;
    // A missing capability is reported as missing, never as a partial success.
    static constexpr bool kFabricateCaptureResults = false;
};

// How a frame source obtains desktop pixels. The three values are deliberately
// distinct: root private AIDL, the public authorized path, and a diagnostic
// one-shot are not interchangeable.
enum class CapturePath {
    PrivateSurfaceFlingerAidl,
    PublicMediaProjection,
    DiagnosticOneShot,
};

enum class Availability {
    Ready,
    Blocked,      // exists in principle but a required piece is missing
    Unsupported,  // never available on this platform/API level
    Unknown,
};

enum class ProbeOutcome { Pass, Fail, Info };

enum class LeaseStatus {
    Acquired,
    Timeout,
    NotSupported,  // the source cannot produce frames at all
    Refused,       // the request violates the contract (e.g. realtime diagnostic)
    Error,
};

const char* capturePathToken(CapturePath path);
const char* capturePathDescription(CapturePath path);
const char* availabilityToken(Availability availability);
const char* probeOutcomeToken(ProbeOutcome outcome);
const char* leaseStatusToken(LeaseStatus status);

// ---------------------------------------------------------------------------
// Capability probe
// ---------------------------------------------------------------------------

struct ProbeCheck {
    std::string id;
    ProbeOutcome outcome = ProbeOutcome::Info;
    std::string detail;
};

struct ProbeReport {
    std::vector<ProbeCheck> checks;

    Availability privateAidl = Availability::Unknown;
    Availability publicMediaProjection = Availability::Unknown;
    Availability zeroCopyConsumer = Availability::Unknown;
    Availability diagnosticFallback = Availability::Unknown;

    // Non-empty whenever privateAidl is not Ready.
    std::string privateAidlBlockedReason;

    // Machine readable list of the interfaces/artifacts that must be supplied
    // before the private path can be implemented. This is the answer to "what is
    // the next interface we need"; it is printed by `probe` and `sources`.
    std::vector<std::string> requiredInterfaces;

    int passCount() const;
    int failCount() const;
};

void addCheck(ProbeReport& report, ProbeOutcome outcome, std::string id, std::string detail);

// Renders the report as stable, greppable lines:
//   probe[pass] root-identity: uid=0 selinux=u:r:ksu:s0
//   verdict.private-aidl=blocked reason=...
std::string formatProbeReport(const ProbeReport& report);

// Runs every capability check. On a desktop host build the device checks report
// Unsupported instead of pretending to have run.
ProbeReport probeCapabilities();

// Same as probeCapabilities(), but the caller decides whether the live
// AHardwareBuffer -> EGLImage -> GL texture round trip runs. The live check
// creates a headless EGL context, so a caller that already owns a GL context may
// prefer to skip it.
ProbeReport probeCapabilities(bool runLiveZeroCopy);

// ---------------------------------------------------------------------------
// Frames
// ---------------------------------------------------------------------------

// Describes one captured frame. `hardwareBuffer` is an AHardwareBuffer* owned by
// the FrameLease that produced it; it stays valid until the lease is reset. A
// zero-copy source fills exactly this and never copies pixels.
struct FrameDescriptor {
    void* hardwareBuffer = nullptr;  // AHardwareBuffer*, owned by the lease
    int32_t width = 0;
    int32_t height = 0;
    int32_t layers = 1;
    int32_t format = 0;  // AHARDWAREBUFFER_FORMAT_*
    uint64_t usage = 0;  // AHARDWAREBUFFER_USAGE_*
    int32_t stride = 0;
    int64_t displayId = 0;
    uint64_t timestampNanos = 0;
    int32_t transform = 0;
    // True when the captured content contained FLAG_SECURE layers. Such a frame
    // must never be presented; the consumer refuses it (see import()).
    bool secure = false;
    bool zeroCopy = false;
};

// RAII owner of a FrameDescriptor. Releases the underlying AHardwareBuffer.
class FrameLease {
public:
    FrameLease() = default;
    ~FrameLease();
    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;
    FrameLease(FrameLease&& other) noexcept;
    FrameLease& operator=(FrameLease&& other) noexcept;

    bool valid() const;
    const FrameDescriptor& descriptor() const;
    void reset();

    // Used by frame sources. `release` may be null when nothing must be freed.
    void adopt(FrameDescriptor descriptor, void (*release)(void*));

private:
    FrameDescriptor descriptor_{};
    void (*release_)(void*) = nullptr;
};

struct SourceConfig {
    // 0 means "the primary display"; the probe prints the ids this ROM reports.
    int64_t displayId = 0;
    bool realtime = true;
    int32_t maxWidth = 0;
    int32_t maxHeight = 0;
    // Rejected unless false: the policy forbids requesting secure content.
    bool requestSecureContent = false;
    // Used only by diagnostic sources that must write their single frame
    // somewhere. Realtime sources ignore it.
    std::string outputPath;
};

// The provider contract every frame source implements.
class FrameSource {
public:
    virtual ~FrameSource() = default;

    virtual const char* name() const = 0;
    virtual CapturePath path() const = 0;
    // True only for a source that can produce a continuous low-latency stream.
    virtual bool realtime() const = 0;
    virtual bool zeroCopy() const = 0;
    // True when the platform requires an explicit user authorization step that a
    // root ELF cannot perform by itself.
    virtual bool requiresUserConsent() const = 0;

    virtual Availability availability(std::string& reason) const = 0;

    // Prepares the source. Returns false and fills `error` on failure. A source
    // that is Blocked/Unsupported must return false here; it must not degrade
    // silently into a different path.
    virtual bool open(const SourceConfig& config, std::string& error) = 0;

    // Acquires one frame into `lease`. A non-realtime source returns Refused when
    // config.realtime was set, so a diagnostic helper can never be mistaken for
    // a streaming source.
    virtual LeaseStatus acquire(FrameLease& lease, std::string& error) = 0;

    virtual void close() = 0;
};

std::unique_ptr<FrameSource> makePrivateAidlSource();
std::unique_ptr<FrameSource> makeMediaProjectionSource();
std::unique_ptr<FrameSource> makeDiagnosticOneShotSource();

// Picks the best source for the request. It returns the private AIDL source when
// the probe says Ready, otherwise it returns null and fills `reason` with the
// blocked/unsupported explanation. The diagnostic source is NEVER returned here:
// a caller that wants it must ask for it explicitly.
std::unique_ptr<FrameSource> selectRealtimeSource(const ProbeReport& report, std::string& reason);

// ---------------------------------------------------------------------------
// Zero-copy consumer: AHardwareBuffer -> EGLImage -> GL texture
// ---------------------------------------------------------------------------

struct FrameImport {
    bool ok = false;
    uint32_t textureId = 0;
    int32_t glError = 0;
    bool zeroCopy = true;
    std::string detail;
};

// Imports AHardwareBuffer backed frames into the calling thread's current GL
// context (or into a headless pbuffer context created by initialize()). The
// import path is eglCreateImageKHR(EGL_NATIVE_BUFFER_ANDROID) +
// glEGLImageTargetTexture2DOES, which is a reference, not a copy: on the same
// Adreno/Mali GPU that composed the frame, no pixel data is moved.
class ZeroCopyGlImporter {
public:
    ZeroCopyGlImporter();
    ~ZeroCopyGlImporter();
    ZeroCopyGlImporter(const ZeroCopyGlImporter&) = delete;
    ZeroCopyGlImporter& operator=(const ZeroCopyGlImporter&) = delete;

    // Resolves the platform entry points. Safe to call repeatedly.
    bool initialize(std::string& detail);
    bool initialized() const;

    // Binds the frame's AHardwareBuffer as a 2D texture. Refuses secure frames.
    // `out.textureId` is owned by this importer and reused on the next import.
    bool import(const FrameDescriptor& frame, FrameImport& out);

    // Runs the headless end-to-end check: allocates an AHardwareBuffer, writes a
    // known pattern through the CPU mapping, imports it as an EGLImage/texture and
    // reads it back with glReadPixels. This proves the zero-copy plumbing on the
    // device without needing a working capture producer.
    bool runSelfTest(std::string& detail);

    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// Host testable helpers
// ---------------------------------------------------------------------------

// Pure logic self-test for the contract in this header. Runs on a desktop host
// and on device. Returns 0 when every assertion holds.
int runSelfTest(std::string& output);

std::string usageText();

}  // namespace aurora::root::capture

#endif  // AURORA_ROOT_HOST_ROOT_DESKTOP_CAPTURE_H
