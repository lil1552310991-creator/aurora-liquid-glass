#pragma once

// ===========================================================================
//  触觉反馈（长按激活的瞬间"轻震一下"）—— 纯逻辑层
// ===========================================================================
//  用户要求（照抄要点）：
//   · 只在**长按成功激活**的那一瞬间震一次；
//   · 长按等待阶段不震、拖动缩放中不逐帧震、取消长按不震；
//   · 轻微、短促、清脆，像苹果控制中心进入交互模式那种确认感；
//   · 震动不可用时**安全降级**，不得阻塞渲染线程、不得影响交互；
//   · 复用项目现有的系统调用封装，不引入重量级依赖。
//
//  分层（本文件零 Android 依赖，所以能进离线回归）：
//    · 本文件      ：参数 + "只在状态转换上放行一次"的门 + 平台层注册接口；
//    · Haptics.cpp ：把调用转给已注册的平台实现（没注册就是不可用，直接返回 false）；
//    · 宿主 ELF    ：root-host/elf/HapticBinder.h —— 用 Binder 直连系统
//                    vibrator_manager 的 performHapticFeedback（见那边的说明）。
//
//  ★ 为什么不做成"自己 fork 一个进程去震"：那样每次震动要 ~50–100ms 才落地，
//    手感会明显滞后；Binder 直连是一次 oneway 事务（微秒级、不等待回复），
//    在渲染线程里调用也不会阻塞（详见 HapticBinder.h 的注释）。
// ===========================================================================

#include <cstdint>

namespace aurora {

// ---- 参数（一处配置）-------------------------------------------------------
struct HapticParams {
    // 23 = HapticFeedbackConstants.GESTURE_THRESHOLD_ACTIVATE
    //      （"用户的动作刚越过激活阈值"）—— 与"长按达阈值进入缩放模式"语义完全对应。
    // 真机实测（测试机 / 厂商 ROM 16 / Android 16，读 dumpsys vibrator_manager 的
    // 振动历史）：该常量被映射成 `Prebaked=TICK(STRONG)`，短促清脆、力度明显强于
    // CONTEXT_CLICK/CLOCK_TICK 那种轻 tick（用户反馈"可以再重一点"就是照着它调的）。
    // 想更重可以试 25（DRAG_START → HEAVY_CLICK(STRONG)）：AURORA_HAPTIC_CONSTANT=25
    int32_t constant = 23;
    // 0 = **尊重用户的触觉设置**（关掉"触感反馈"就不震）。
    // 刻意不用 FLAG_IGNORE_GLOBAL_SETTING：进入缩放不该无视用户的系统设置。
    int32_t flags = 0;
    int32_t privFlags = 0;
    // 会出现在 dumpsys 的振动历史里 ⇒ 真机验收时能一眼看出"是哪一次交互震的"
    const char* reason = "resize_activate";
};

// ---- 纯逻辑门：只在"未激活 → 激活"这一个转换上放行一次 --------------------
//  每帧喂当前激活状态即可，不需要任何计时器：
//    · 等待阶段（还没到 330ms）active=false ⇒ 不震；
//    · 激活那一帧 active 由 false 变 true ⇒ 放行一次；
//    · 之后一直按住（拖动）active 恒为 true ⇒ 不会重复震；
//    · 松手/取消 ⇒ active=false，闩锁复位，下次长按还能再震一次。
struct HapticGate {
    bool latched = false;

    bool Step(bool active) {
        if (active && !latched) {
            latched = true;
            return true;
        }
        if (!active) latched = false;
        return false;
    }
};

// ---- 平台层（宿主启动时注册一次）------------------------------------------
//  返回 true = 已经交给系统（不代表一定能感觉到：用户可能关了触感）。
using HapticSink = bool (*)(const HapticParams& params);

void SetHapticSink(HapticSink sink);
bool HapticAvailable();
bool HapticTap(const HapticParams& params);

}  // namespace aurora
