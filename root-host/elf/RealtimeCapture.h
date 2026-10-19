#pragma once

#include <cstdint>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

struct AHardwareBuffer;

namespace aurora {

// A persistent app_process captures the WMS display subtree. Optional mirror
// mode uses a damage-driven ImageReader instead of requesting a new screenshot
// every frame. The embedded helper needs no APK or MediaProjection approval.
class RealtimeCapture final {
public:
    enum class TimestampOrigin { Unknown, MirrorPresentation, SnapshotCaptureRequest };
    struct Timing {
        std::uint32_t sequence = 0;
        std::uint32_t captureUs = 0;
        std::uint32_t readbackUs = 0;
        std::uint32_t totalUs = 0;
        std::uint64_t sourceTimestampNs = 0;
        std::uint64_t receivedTimestampNs = 0;
        std::uint32_t mirrorEvents = 0;
        std::uint32_t mirrorDropped = 0;
        std::uint32_t duplicateTimestamps = 0;
        // Source logical geometry, separate from downsampled AHB dimensions.
        // Rotation is reported even for 180-degree changes with identical sizes.
        std::uint32_t logicalWidth = 0;
        std::uint32_t logicalHeight = 0;
        std::uint32_t rotation = 0;
        std::uint32_t geometryGeneration = 0;
        TimestampOrigin timestampOrigin = TimestampOrigin::Unknown;
        bool mirrored = false;
        bool idle = false;
    };

    explicit RealtimeCapture(int logicalDisplayId, int downscale = 2);
    ~RealtimeCapture();
    RealtimeCapture(const RealtimeCapture&) = delete;
    RealtimeCapture& operator=(const RealtimeCapture&) = delete;

    bool start(std::string* why = nullptr);
    // Configure before start. Failure to create a mirror falls back to the
    // existing screenshot path and is identified by Timing::mirrored=false.
    void setMirrorOptions(bool enabled, float requestedRefreshRate = 120.0f);
    bool grabOnce(std::vector<std::uint8_t>* rgba, int* outW, int* outH,
                  std::string* why = nullptr);
    // On success caller owns one acquired reference. Mirror frames also hold a
    // producer Image lease: call releaseFrame(timing().sequence) only after the
    // last GPU read completes (or immediately if the frame is never submitted).
    // A successful idle mirror returns a null buffer; no reference/lease exists.
    bool grabHardwareBuffer(AHardwareBuffer** buffer, int* outW, int* outH,
                            std::string* why = nullptr);
    // Thread-safe. It only writes an ACK; capture reads remain on one thread.
    void releaseFrame(std::uint32_t sequence);
    void stop();
    void setEraseRect(int x, int y, int width, int height);
    long long erasedFrameCount() const { return erasedFrames_; }
    Timing timing() const { return timing_; }
    const std::string& logPath() const { return logPath_; }

private:
    bool fail(std::string* why, const std::string& detail);
    bool requestFrame(std::uint8_t mode, std::uint8_t* header, std::string* why);
    int logicalDisplayId_;
    int downscale_;
    int socket_ = -1;
    std::mutex socketWriteMutex_;
    std::atomic<bool> ackFailed_{false};
    bool mirrorEnabled_ = false;
    float mirrorRefreshRate_ = 120.0f;
    int childPid_ = -1;
    int eraseX_ = 0, eraseY_ = 0, eraseW_ = 0, eraseH_ = 0;
    long long erasedFrames_ = 0;
    bool attemptedStart_ = false;
    std::string directory_;
    std::string dexPath_;
    std::string nativePath_;
    std::string logPath_;
    std::string failure_;
    std::vector<std::uint8_t> raw_;
    Timing timing_;
};

} // namespace aurora
