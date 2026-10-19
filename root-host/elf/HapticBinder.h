#pragma once

// ===========================================================================
//  震动：Binder 直连系统 vibrator_manager（宿主侧实现）
// ===========================================================================
//  为什么是 Binder，而不是 fork 一个命令：
//   · 本机没有 `/sys/class/timed_output/vibrator/enable`（真机实测不存在），
//     `cmd vibrator` 也不存在（`cmd: Can't find service: vibrator`）；
//   · fork `app_process`（Java 侧 Vibrator API）启动就要 200–400ms，
//     震动会明显滞后于视觉反馈，手感是"两下"而不是"一下"；
//   · Binder **oneway** 事务只往 /dev/binder 写一小段数据（微秒级、不等回复）
//     ⇒ 在渲染线程里直接调用也不会阻塞，符合"不得阻塞渲染线程"的要求。
//
//  用的是哪一层接口（**不是猜的**）：
//   系统服务 `vibrator_manager` = android.os.IVibratorManagerService。
//   事务号 = AIDL 里的声明顺序（AOSP android16-release 的
//   core/java/android/os/IVibratorManagerService.aidl）：
//     1 getVibratorIds            5 registerVibratorStateListener   9 cancelVibrate
//     2 getCapabilities           6 unregisterVibratorStateListener 10 performHapticFeedback
//     3 getVibratorInfo           7 setAlwaysOnEffect               11 performHapticFeedbackForInputDevice
//     4 isVibrating               8 vibrate                         12 startVendorVibrationSession
//   选 **10 = performHapticFeedback(uid, deviceId, opPkg, constant, reason, flags, privFlags)**：
//   它是系统自己的"触觉反馈"入口 —— 常量由厂商调校（测试机 上 23 号 =
//   Prebaked=TICK(STRONG)），并且**自动尊重用户的触感设置**。Parcel 也最简单
//   （4 个 int + 2 个 string），不需要手搓 VibrationEffect/VibrationAttributes
//   那套版本敏感的 parcelable 布局。
//
//  ★★ 不盲信事务号（本项目在"写死偏移"上吃过两次大亏）：
//    Install() 先用**双向**事务 1（getVibratorIds）做一次**写回读校验**：
//      · 写 interface token + 读回复；
//      · 期望 `异常码=0` 且后面跟一个 `1..8` 的数组长度与合法 id；
//      · 任一条不满足（含 UNKNOWN_TRANSACTION）⇒ 整体停用并写明原因，
//        绝不"硬发"一个可能打到别的方法上的事务。
//    （真机实测：事务 1 回 `00000000 00000001 00000000` = 异常 0 / 长度 1 / id 0；
//      事务 99 回 `Error: ... Not a data message`，说明错号是**可检测**的。）
//
//  ★ 权限：服务侧 `performHapticFeedback` 要 VIBRATE 权限；本进程是 root（uid 0），
//    ActivityManager 的权限检查对 uid 0 直接放行（真机实测调用成功，
//    dumpsys 历史里能看到 `aurora_overlay (uid=0, deviceId=0)`）。
//    非 root 运行时不会崩：事务失败即返回 false，界面侧安全降级。
// ===========================================================================

#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "aosp_glue.h"   // 复用它的 dlopen 句柄、binder 符号与 servicemanager 直连
#include "Haptics.h"

namespace aurora {
namespace haptic_binder {

namespace {

using aospglue::detail::Blob;
using aospglue::detail::makeString16;
using aospglue::detail::syms;

// AOSP android16-release IVibratorManagerService.aidl 的声明顺序 ⇒ 事务号
constexpr uint32_t kCodeGetVibratorIds = 1;
constexpr uint32_t kCodePerformHapticFeedback = 10;
constexpr uint32_t kFlagOneWay = 0x1;   // IBinder::FLAG_ONEWAY

const char* const kServiceName = "vibrator_manager";
const char* const kDescriptor = "android.os.IVibratorManagerService";
const char* const kOpPackage = "aurora_overlay";

bool g_ready = false;
bool g_checked = false;
int32_t g_serviceHandle = -1;
int32_t g_deviceId = 0;
std::string g_why = "尚未初始化";
int32_t (*g_writeInt32)(void*, int32_t) = nullptr;

// 一条事务的公共前半段：写 interface token。
bool beginCall(Blob* data) {
    auto& s = syms();
    s.parcelCtor(data->d);
    Blob token;
    makeString16(&token, kDescriptor);
    return s.writeInterfaceToken != nullptr && s.writeInterfaceToken(data->d, token.d) == 0;
}

}  // namespace

// 取服务 + 校验 AIDL 布局。启动时调用一次；任何一步失败都只是"不可用"。
inline bool Install() {
    if (g_checked) return g_ready;
    g_checked = true;

    auto& s = syms();
    if (!s.ok) {
        g_why = "aosp_glue 符号未解析";
        return false;
    }
    void* its = s.ipcThreadStateSelf ? s.ipcThreadStateSelf() : nullptr;
    if (its == nullptr || s.ipcTransact == nullptr) {
        g_why = "libbinder: IPCThreadState 不可用";
        return false;
    }
    // writeInt32 没在 aosp_glue 的符号表里，这里自己补一个（同 libbinder）。
    g_writeInt32 = reinterpret_cast<int32_t (*)(void*, int32_t)>(
        aospglue::detail::Rlib(s.libbinder, "_ZN7android6Parcel10writeInt32Ei", false));
    if (g_writeInt32 == nullptr) {
        g_why = "libbinder: Parcel::writeInt32 缺失";
        return false;
    }

    // ---- 1) 拿 vibrator_manager 的 binder ----
    //  ★ 不能用 defaultServiceManager()->getService()：本机对所有服务都返回 null
    //    （aosp_glue.h:573 记过这条），所以走它现成的 handle-0 直连封装。
    void* svc = aospglue::detail::getServiceByHandle0(kServiceName);
    if (svc == nullptr) {
        g_why = "拿不到 vibrator_manager 服务（需要 root）";
        return false;
    }
    const int32_t handle = s.bpBinderHandle ? s.bpBinderHandle(svc) : -1;
    if (handle < 0) {
        g_why = "vibrator_manager 的 BpBinder::binderHandle() 取不到";
        return false;
    }

    // ---- 2) 写回读校验：事务 1 = getVibratorIds() → int[] ----
    Blob data, reply;
    if (!beginCall(&data)) {
        g_why = "Parcel::writeInterfaceToken 失败";
        return false;
    }
    s.parcelCtor(reply.d);
    errno = 0;
    const int32_t tr = s.ipcTransact(its, handle, kCodeGetVibratorIds, data.d, reply.d, 0);
    if (tr != 0) {
        g_why = aospglue::detail::fmtStr("事务 1 失败（tr=%d errno=%d）—— 布局不符，停用",
                                        tr, errno);
        return false;
    }
    s.setDataPosition(reply.d, 0);
    int32_t exception = -1;
    s.readInt32(reply.d, &exception);
    s.setDataPosition(reply.d, 4);
    int32_t count = -1;
    s.readInt32(reply.d, &count);
    if (exception != 0 || count <= 0 || count > 8) {
        g_why = aospglue::detail::fmtStr("事务 1 回复不自洽（异常=%d 数量=%d）—— 布局不符，停用",
                                        exception, count);
        return false;
    }
    int32_t firstId = 0;
    s.readInt32(reply.d, &firstId);
    if (firstId < 0 || firstId > 64) {
        g_why = aospglue::detail::fmtStr("事务 1 回的 vibrator id 非法（%d）—— 停用", firstId);
        return false;
    }

    g_serviceHandle = handle;
    g_deviceId = firstId;
    g_ready = true;
    g_why = "ok";
    return true;
}

inline bool Available() { return g_ready; }
inline int32_t DeviceId() { return g_deviceId; }
inline const std::string& Why() { return g_why; }

// 真正发一次触觉反馈。**oneway**：写完即返回，不等系统回复，不会卡住渲染线程。
inline bool Tap(const HapticParams& params) {
    if (!g_ready) return false;
    auto& s = syms();
    void* its = s.ipcThreadStateSelf ? s.ipcThreadStateSelf() : nullptr;
    if (its == nullptr || s.ipcTransact == nullptr || g_writeInt32 == nullptr) return false;

    Blob data;
    if (!beginCall(&data)) return false;
    g_writeInt32(data.d, static_cast<int32_t>(::getuid()));   // uid：归属用（root=0）
    g_writeInt32(data.d, g_deviceId);                          // deviceId
    Blob pkg;
    makeString16(&pkg, kOpPackage);
    s.writeString16(data.d, pkg.d);                            // opPkg
    g_writeInt32(data.d, params.constant);                     // constant
    Blob reason;
    makeString16(&reason, params.reason != nullptr ? params.reason : "resize_activate");
    s.writeString16(data.d, reason.d);                         // reason
    g_writeInt32(data.d, params.flags);                        // flags
    g_writeInt32(data.d, params.privFlags);                    // privFlags

    errno = 0;
    const int32_t tr = s.ipcTransact(its, g_serviceHandle, kCodePerformHapticFeedback,
                                     data.d, nullptr, kFlagOneWay);
    return tr == 0;
}

// 注册进界面层（aurora::HapticTap 会转到这里）。
inline bool InstallAsSink() {
    aurora::SetHapticSink(&Tap);
    return Install();
}

}  // namespace haptic_binder
}  // namespace aurora
