#pragma once

// =============================================================================
// 从 `dumpsys SurfaceFlinger --display-id` 的输出里解析显示。
//
// ★★ 为什么必须解析、绝不能写死：
//   每台机器的 SF DisplayId 都不一样，连同一型号的不同机器都可能不同。
//   真实事故（2026-10-13）：`--display 0` 的分支里写死了开发机的
//   1111111111111111111，用户把单文件包发给朋友后，朋友的机器是
//   2222222222222222222 ⇒ 匹配失败 ⇒ 回退去找虚拟屏 ⇒ 朋友机器上没有虚拟屏
//   ⇒ 直接报「找不到可用的显示」退出 4。
//
// 输出行样例：
//   Display 1111111111111111111 (HWC display 0): port=147 pnpId=QCM displayName=""
//   Display 11529215047573656498 (Virtual display): displayName="AgentVirtualDisplay"
//   Display <display-id> (Virtual display): displayName="ScreenRecorder"
//
// 本头文件是**纯字符串逻辑、零 Android 依赖**，所以能进离线回归套件
// （tests/DisplayPickTest.cpp）。解析函数一律只吃 dump 文本，不自己跑命令。
// =============================================================================

#include <string>

namespace aurora {
namespace display_pick {

/** 行里带 "HWC display" 的就是物理屏。返回第一台的 DisplayId；没有则空串。 */
inline std::string pickPhysicalDisplayId(const std::string& dump) {
    size_t pos = 0;
    while ((pos = dump.find("Display ", pos)) != std::string::npos) {
        const size_t idStart = pos + 8;
        const size_t idEnd = dump.find_first_of(" \n", idStart);
        if (idEnd == std::string::npos) break;
        const std::string id = dump.substr(idStart, idEnd - idStart);
        const size_t lineEnd = dump.find('\n', idEnd);
        const std::string line =
            dump.substr(pos, (lineEnd == std::string::npos ? dump.size() : lineEnd) - pos);
        const bool isPhysical = line.find("HWC display") != std::string::npos;
        const bool allDigits =
            !id.empty() && id.find_first_not_of("0123456789") == std::string::npos;
        if (allDigits && isPhysical) return id;
        pos = idEnd;
    }
    return std::string();
}

/** 挑一台**非**物理屏（虚拟屏）的 DisplayId。多台时取最后一台。 */
inline std::string pickVirtualDisplayId(const std::string& dump) {
    std::string best;
    size_t pos = 0;
    while ((pos = dump.find("Display ", pos)) != std::string::npos) {
        const size_t idStart = pos + 8;
        const size_t idEnd = dump.find_first_of(" \n", idStart);
        if (idEnd == std::string::npos) break;
        const std::string id = dump.substr(idStart, idEnd - idStart);
        const size_t lineEnd = dump.find('\n', idEnd);
        const std::string line =
            dump.substr(pos, (lineEnd == std::string::npos ? dump.size() : lineEnd) - pos);
        const bool isPhysical = line.find("HWC display") != std::string::npos;
        const bool allDigits =
            !id.empty() && id.find_first_not_of("0123456789") == std::string::npos;
        if (allDigits && !isPhysical) best = id;   // 多台时取最后一台
        pos = idEnd;
    }
    return best;
}

/**
 * 取某台显示的 displayName。物理屏是空串（`displayName=""`），虚拟屏通常是
 * "AgentVirtualDisplay"。这个名字是 `DisplayId → layerStack` 退路查找的钥匙。
 */
inline std::string findDisplayName(const std::string& dump, const std::string& displayId) {
    if (dump.empty() || displayId.empty()) return std::string();
    const size_t pos = dump.find("Display " + displayId);
    if (pos == std::string::npos) return std::string();
    size_t end = dump.find('\n', pos);
    if (end == std::string::npos) end = dump.size();
    const std::string line = dump.substr(pos, end - pos);
    const size_t k = line.find("displayName=\"");
    if (k == std::string::npos) return std::string();
    const size_t s = k + 13;
    const size_t e = line.find('"', s);
    if (e == std::string::npos) return std::string();
    return line.substr(s, e - s);
}

/** 这台 DisplayId 是不是物理屏（拿 pickPhysicalDisplayId 的结果比）。 */
inline bool isPhysicalDisplayId(const std::string& physicalId, const std::string& displayId) {
    return !physicalId.empty() && physicalId == displayId;
}

}  // namespace display_pick
}  // namespace aurora
