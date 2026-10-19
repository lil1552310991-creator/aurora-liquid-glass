// ===========================================================================
//  触觉反馈的平台层接线（纯 C++，零 Android 依赖）
//
//  为什么单独一个文件：界面层（aurora_glass_ui）里要能在"激活那一帧"直接调用
//  `aurora::HapticTap(...)`，但**界面层不该被 binder 依赖污染**（它同时被
//  Android APK 目标复用）。所以这里只保存一个函数指针，真正的 Binder 实现在
//  宿主 ELF 侧注册进来（root-host/elf/HapticBinder.h）。
//  没有注册 = 不可用 ⇒ HapticTap 直接返回 false，界面层什么都不用管。
// ===========================================================================

#include "Haptics.h"

namespace aurora {
namespace {

HapticSink g_sink = nullptr;

}  // namespace

void SetHapticSink(HapticSink sink) {
    g_sink = sink;
}

bool HapticAvailable() {
    return g_sink != nullptr;
}

bool HapticTap(const HapticParams& params) {
    if (g_sink == nullptr) return false;
    return g_sink(params);
}

}  // namespace aurora
