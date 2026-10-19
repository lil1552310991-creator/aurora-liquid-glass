#include "RealtimeCapture.h"

#include "AuroraCaptureDex.h"
#include "AuroraCaptureNative.h"
#include "CapturePipeline.h"
#include <android/hardware_buffer.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

extern char** environ;

namespace aurora {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::uint32_t kMagic = 0x41555243u;
constexpr int kTimeoutMs = 5000;
constexpr std::size_t kMaxFrameBytes = std::size_t(4096) * 4096 * 4;

std::uint32_t little32(const std::uint8_t* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

std::uint64_t little64(const std::uint8_t* p) {
    return std::uint64_t(little32(p)) | (std::uint64_t(little32(p + 4)) << 32);
}

bool sendExact(int fd, const std::uint8_t* bytes, std::size_t size) {
    while (size != 0) {
        // Requests and five-byte GPU ACKs are small. Never stall the render
        // thread behind a wedged helper; EAGAIN becomes a capture failure.
        const ssize_t count = ::send(fd, bytes, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        bytes += count;
        size -= std::size_t(count);
    }
    return true;
}

bool readExact(int fd, void* data, std::size_t size, Clock::time_point deadline) {
    auto* out = static_cast<std::uint8_t*>(data);
    while (size != 0) {
        const auto remain = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remain <= 0) { errno = ETIMEDOUT; return false; }
        pollfd pfd{fd, POLLIN, 0};
        int result = ::poll(&pfd, 1, int(remain));
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) { if (result == 0) errno = ETIMEDOUT; return false; }
        const ssize_t count = ::recv(fd, out, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (count == 0) errno = EPIPE; return false; }
        out += count;
        size -= std::size_t(count);
    }
    return true;
}

std::string logTail(const std::string& path) {
    if (path.empty()) return {};
    FILE* file = ::fopen(path.c_str(), "re");
    if (file == nullptr) return {};
    ::fseek(file, 0, SEEK_END);
    long size = ::ftell(file);
    if (size > 4096) ::fseek(file, size - 4096, SEEK_SET);
    else ::rewind(file);
    char bytes[4097]{};
    std::size_t count = ::fread(bytes, 1, sizeof(bytes) - 1, file);
    ::fclose(file);
    return std::string(bytes, count);
}

bool extractReadOnly(const std::string& path, const unsigned char* bytes, std::size_t size) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::size_t offset = 0;
    while (offset < size) {
        ssize_t count = ::write(fd, bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { ::close(fd); return false; }
        offset += std::size_t(count);
    }
    const bool ok = ::fchmod(fd, 0444) == 0;
    ::close(fd);
    return ok;
}
} // namespace

RealtimeCapture::RealtimeCapture(int logicalDisplayId, int downscale)
    : logicalDisplayId_(logicalDisplayId), downscale_(std::clamp(downscale, 1, 8)) {}

RealtimeCapture::~RealtimeCapture() { stop(); }

void RealtimeCapture::setMirrorOptions(bool enabled, float requestedRefreshRate) {
    if (attemptedStart_) return;
    mirrorEnabled_ = enabled;
    mirrorRefreshRate_ = std::isfinite(requestedRefreshRate)
        ? std::clamp(requestedRefreshRate, 1.0f, 240.0f) : 120.0f;
}

bool RealtimeCapture::fail(std::string* why, const std::string& detail) {
    failure_ = detail;
    const std::string diagnostic = logTail(logPath_);
    if (!diagnostic.empty()) failure_ += "\n" + diagnostic;
    // A mirror Image remains leased while the renderer GPU reads it. The main
    // owner drains/destroys the renderer before stopping this capture object;
    // keep the helper alive here so a receive failure cannot recycle those
    // ImageReader allocations underneath an in-flight GPU read.
    if (!mirrorEnabled_ || socket_ < 0) stop();
    if (why != nullptr) *why = failure_;
    return false;
}

bool RealtimeCapture::start(std::string* why) {
    if (socket_ >= 0) return true;
    if (attemptedStart_) {
        if (why != nullptr) *why = failure_.empty() ? "persistent capture stopped" : failure_;
        return false;
    }
    attemptedStart_ = true;
    char directory[] = "/data/local/tmp/aurora-capture-XXXXXX";
    if (::mkdtemp(directory) == nullptr) return fail(why, "capture mkdtemp: " + std::string(::strerror(errno)));
    directory_ = directory;
    dexPath_ = directory_ + "/classes.dex";
    nativePath_ = directory_ + "/libaurora_capture_send.so";
    logPath_ = directory_ + "/capture.log";
    // Android 14+ only accepts a read-only dynamically loaded dex.
    if (!extractReadOnly(dexPath_, kAuroraCaptureDex, kAuroraCaptureDexSize) ||
        !extractReadOnly(nativePath_, kAuroraCaptureNative, kAuroraCaptureNativeSize))
        return fail(why, "capture embedded dex/JNI extraction failed");

    int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) return fail(why, "capture socket failed");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string name = "aurora_capture_" + std::to_string(::getpid()) + "_" + std::to_string(Clock::now().time_since_epoch().count());
    std::memcpy(address.sun_path + 1, name.data(), name.size());
    const socklen_t addressSize = offsetof(sockaddr_un, sun_path) + 1 + name.size();
    if (::bind(server, reinterpret_cast<sockaddr*>(&address), addressSize) != 0 || ::listen(server, 1) != 0) {
        ::close(server);
        return fail(why, "capture UDS bind/listen failed");
    }

    std::vector<std::string> environment;
    for (char** entry = environ; *entry != nullptr; ++entry)
        if (std::strncmp(*entry, "CLASSPATH=", 10) != 0) environment.emplace_back(*entry);
    environment.push_back("CLASSPATH=" + dexPath_);
    std::vector<char*> envPointers;
    for (auto& entry : environment) envPointers.push_back(entry.data());
    envPointers.push_back(nullptr);
    const std::string display = std::to_string(logicalDisplayId_);
    const std::string scale = std::to_string(downscale_);
    const std::string backend = mirrorEnabled_ ? "mirror" : "snapshot";
    const std::string refresh = std::to_string(mirrorRefreshRate_);
    const char* argv[] = {"/system/bin/app_process", "/system/bin", "--nice-name=aurora_capture",
                         "com.aurora.capture.CaptureHelper", name.c_str(), display.c_str(), scale.c_str(),
                         nativePath_.c_str(), backend.c_str(), refresh.c_str(), nullptr};
    posix_spawn_file_actions_t actions;
    ::posix_spawn_file_actions_init(&actions);
    ::posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    ::posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    ::posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, logPath_.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    pid_t child = -1;
    const int status = ::posix_spawn(&child, "/system/bin/app_process", &actions, nullptr,
                                    const_cast<char* const*>(argv), envPointers.data());
    ::posix_spawn_file_actions_destroy(&actions);
    if (status != 0) { ::close(server); return fail(why, "capture app_process spawn: " + std::string(::strerror(status))); }
    childPid_ = child;
    const auto deadline = Clock::now() + std::chrono::milliseconds(kTimeoutMs);
    while (Clock::now() < deadline) {
        pollfd pfd{server, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 100);
        if (ready > 0 && (pfd.revents & POLLIN)) {
            socket_ = ::accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
            if (socket_ >= 0) break;
        }
        int childStatus = 0;
        if (::waitpid(child, &childStatus, WNOHANG) == child) {
            childPid_ = -1;
            break;
        }
    }
    ::close(server);
    if (socket_ < 0) return fail(why, "persistent capture helper did not connect");
    timeval timeout{0, 500000};
    ::setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    timeval receiveTimeout{5, 0};
    ::setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
    ucred peer{};
    socklen_t peerSize = sizeof(peer);
    if (::getsockopt(socket_, SOL_SOCKET, SO_PEERCRED, &peer, &peerSize) != 0 ||
        peer.pid != childPid_ || peer.uid != ::getuid())
        return fail(why, "capture helper peer credentials do not match spawned process");
    return true;
}

bool RealtimeCapture::requestFrame(std::uint8_t mode, std::uint8_t* header, std::string* why) {
    if (!start(why)) return false;
    if (ackFailed_.load()) return fail(why, "capture GPU lease acknowledgement failed");
    const auto deadline = Clock::now() + std::chrono::milliseconds(kTimeoutMs);
    bool sent;
    {
        std::lock_guard<std::mutex> lock(socketWriteMutex_);
        sent = socket_ >= 0 && sendExact(socket_, &mode, 1);
    }
    if (!sent) return fail(why, "capture request: " + std::string(::strerror(errno)));
    if (!readExact(socket_, header, 32, deadline)) return fail(why, "capture header: " + std::string(::strerror(errno)));
    const auto width = little32(header + 8), height = little32(header + 12);
    const auto returnedMode = little32(header + 4);
    const bool snapshotResponse = (mode == 2 || mode == 3)
        && (returnedMode == 2 || returnedMode == 5 || returnedMode == 7);
    const bool geometryResponse = (mode == 3 && returnedMode == 6) || (mode == 1 && returnedMode == 8);
    if (little32(header) != kMagic || (returnedMode != mode && !snapshotResponse && !geometryResponse) || width == 0 || height == 0 ||
        width > 4096 || height > 4096) return fail(why, "capture invalid frame header");
    timing_ = Timing{};
    timing_.sequence = little32(header + 20);
    timing_.captureUs = little32(header + 24);
    timing_.readbackUs = little32(header + 28);
    if (returnedMode == 3 || returnedMode == 5 || returnedMode == 6 || returnedMode == 7 || returnedMode == 8) {
        std::uint8_t extension[32];
        if (!readExact(socket_, extension, sizeof(extension), deadline))
            return fail(why, "capture mirror header extension: " + std::string(::strerror(errno)));
        timing_.mirrored = returnedMode == 3 || returnedMode == 6;
        timing_.timestampOrigin = timing_.mirrored ? TimestampOrigin::MirrorPresentation : TimestampOrigin::SnapshotCaptureRequest;
        timing_.sourceTimestampNs = little64(extension);
        timing_.receivedTimestampNs = little64(extension + 8);
        timing_.mirrorEvents = little32(extension + 16);
        timing_.mirrorDropped = little32(extension + 20);
        timing_.duplicateTimestamps = little32(extension + 24);
        const auto flags = little32(extension + 28);
        if ((flags & ~1u) != 0) return fail(why, "capture invalid mirror flags");
        timing_.idle = (flags & 1u) != 0;
    }
    if (returnedMode == 6 || returnedMode == 7 || returnedMode == 8) {
        std::uint8_t geometry[16];
        if (!readExact(socket_, geometry, sizeof(geometry), deadline))
            return fail(why, "capture geometry header extension: " + std::string(::strerror(errno)));
        timing_.logicalWidth = little32(geometry);
        timing_.logicalHeight = little32(geometry + 4);
        timing_.rotation = little32(geometry + 8);
        timing_.geometryGeneration = little32(geometry + 12);
        if (timing_.logicalWidth == 0 || timing_.logicalHeight == 0 ||
            timing_.logicalWidth > 32768 || timing_.logicalHeight > 32768 ||
            timing_.rotation > 3 || timing_.geometryGeneration == 0)
            return fail(why, "capture invalid logical display geometry");
    }
    return true;
}

bool RealtimeCapture::grabOnce(std::vector<std::uint8_t>* rgba, int* outW, int* outH, std::string* why) {
    if (rgba == nullptr || outW == nullptr || outH == nullptr) return fail(why, "capture output is null");
    const auto begin = Clock::now();
    const auto deadline = begin + std::chrono::milliseconds(kTimeoutMs);
    std::uint8_t header[32];
    if (!requestFrame(1, header, why)) return false;
    const std::uint32_t width = little32(header + 8), height = little32(header + 12);
    const std::uint32_t bytes = little32(header + 16);
    if (little32(header) != kMagic || (little32(header + 4) != 1 && little32(header + 4) != 8) || width == 0 || height == 0 ||
        width > 4096 || height > 4096 || bytes > kMaxFrameBytes || std::size_t(width) * height * 4 != bytes)
        return fail(why, "capture invalid RGBA frame header");
    raw_.resize(bytes);
    if (!readExact(socket_, raw_.data(), bytes, deadline)) return fail(why, "capture pixels: " + std::string(::strerror(errno)));
    CapturePostParams params;
    params.downscale = 1; // SurfaceFlinger already scaled on the GPU.
    params.eraseX = eraseX_ / downscale_; params.eraseY = eraseY_ / downscale_;
    params.eraseW = eraseW_ / downscale_; params.eraseH = eraseH_ / downscale_;
    if (!PostProcessCaptureFrame(raw_.data(), int(width), int(height), params, rgba, outW, outH))
        return fail(why, "capture postprocess failed");
    if (eraseW_ > 0 && eraseH_ > 0) ++erasedFrames_;
    timing_.sequence = little32(header + 20);
    timing_.captureUs = little32(header + 24);
    timing_.readbackUs = little32(header + 28);
    timing_.totalUs = std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
    return true;
}

bool RealtimeCapture::grabHardwareBuffer(AHardwareBuffer** buffer, int* outW, int* outH, std::string* why) {
    if (buffer == nullptr || outW == nullptr || outH == nullptr) return fail(why, "capture output is null");
    *buffer = nullptr;
    const auto begin = Clock::now();
    std::uint8_t header[32];
    if (!requestFrame(mirrorEnabled_ ? 3 : 2, header, why)) return false;
    if (little32(header + 16) != 0) return fail(why, "capture GPU header contains pixel payload");
    if (timing_.idle) {
        *outW = int(little32(header + 8)); *outH = int(little32(header + 12));
        timing_.totalUs = std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
        return true;
    }
    pollfd pending{socket_, POLLIN, 0};
    int ready;
    do { ready = ::poll(&pending, 1, kTimeoutMs); } while (ready < 0 && errno == EINTR);
    if (ready <= 0) return fail(why, "capture GPU handle timeout");
    AHardwareBuffer* captured = nullptr;
    const int status = AHardwareBuffer_recvHandleFromUnixSocket(socket_, &captured);
    if (status != 0 || captured == nullptr) return fail(why, "capture GPU handle receive status=" + std::to_string(status));
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(captured, &description);
    if (description.width != little32(header + 8) || description.height != little32(header + 12) ||
        description.layers != 1 || description.format != AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM ||
        (description.usage & AHARDWAREBUFFER_USAGE_PROTECTED_CONTENT)) {
        AHardwareBuffer_release(captured);
        return fail(why, "capture GPU descriptor mismatch/unsupported format");
    }
    *buffer = captured;
    *outW = int(description.width); *outH = int(description.height);
    timing_.totalUs = std::uint32_t(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count());
    return true;
}

void RealtimeCapture::releaseFrame(std::uint32_t sequence) {
    if (!mirrorEnabled_ || sequence == 0) return;
    const std::uint8_t ack[] = {4, std::uint8_t(sequence), std::uint8_t(sequence >> 8),
                               std::uint8_t(sequence >> 16), std::uint8_t(sequence >> 24)};
    std::lock_guard<std::mutex> lock(socketWriteMutex_);
    if (socket_ >= 0 && !sendExact(socket_, ack, sizeof(ack))) ackFailed_.store(true);
}

void RealtimeCapture::setEraseRect(int x, int y, int width, int height) {
    eraseX_ = x; eraseY_ = y; eraseW_ = width; eraseH_ = height;
}

void RealtimeCapture::stop() {
    {
        std::lock_guard<std::mutex> lock(socketWriteMutex_);
        if (socket_ >= 0) {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
            socket_ = -1;
        }
    }
    if (childPid_ > 0) {
        const pid_t child = childPid_;
        // EOF lets Java remove its mirror and release the virtual display before
        // exit. Give that cleanup a short grace period before the SIGTERM fallback.
        const auto gracefulDeadline = Clock::now() + std::chrono::milliseconds(250);
        do {
            int status = 0;
            const pid_t result = ::waitpid(child, &status, WNOHANG);
            if (result == child || (result < 0 && errno == ECHILD)) { childPid_ = -1; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (Clock::now() < gracefulDeadline);
        if (childPid_ > 0) ::kill(child, SIGTERM);
        const auto deadline = Clock::now() + std::chrono::milliseconds(1500);
        while (childPid_ > 0) {
            int status = 0;
            const pid_t result = ::waitpid(child, &status, WNOHANG);
            if (result == child || (result < 0 && errno == ECHILD)) { childPid_ = -1; break; }
            if (Clock::now() >= deadline) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (childPid_ <= 0) {
        if (!dexPath_.empty()) ::unlink(dexPath_.c_str());
        if (!nativePath_.empty()) ::unlink(nativePath_.c_str());
        if (!logPath_.empty()) ::unlink(logPath_.c_str());
        if (!directory_.empty()) ::rmdir(directory_.c_str());
    }
    // A helper which ignores SIGTERM keeps its files for diagnosis. Never kill
    // unrelated app_process processes or issue SIGKILL on the user's phone.
    raw_.clear();
}

} // namespace aurora
