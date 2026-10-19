#pragma once

// ===========================================================================
//  输入法回填文本 → 「退格 N 次 + 插入一段」的换算（纯逻辑）
// ===========================================================================
//  为什么必须有这一层：Java 侧回传的是**整段文本**（EditText 的内容快照），
//  而我们把文字送进 ImGui 输入框的手段只有两种：`AddInputCharactersUTF8`（追加）
//  和 `Backspace` 键。二者都不是"整段设置"。
//
//  ★ 上一版直接 `pendingText_ += 整段` ⇒ 用户看到的是：
//      · 打字能上去，但**越打越重复**（敲 a → "a"；敲 b → 追加整段 "ab" ⇒ "aab"）；
//      · **删不掉** —— 退格后 Java 回传的是空串/更短的串，追加语义下"什么都不做"
//        （用户原话："只能打字上去，而且不能删除，体验怪怪的"）。
//
//  这里把"上一次的文本 → 这一次的文本"换算成 **公共前缀之后的退格数 + 待插入文本**：
//    1. 求公共前缀 p（字节）；
//    2. 退格数 = previous[p..] 的 **UTF-8 码点**个数（按字节退会把一个汉字退成半个）；
//    3. 插入 = next[p..]。
//  对任何差异都成立（中间插入/整段替换也只是多退几次再重插），
//  而且纯追加时 backspaces=0 —— 正常打字走的是最短路径。
// ===========================================================================

#include <cstddef>
#include <string>

namespace aurora {

// UTF-8 码点个数（退格键一次删一个"字符"，不是删一个字节）
inline int ImeUtf8Length(const std::string& text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xC0u) != 0x80u) ++count;      // 跳过续字节（10xxxxxx）
    }
    return count;
}

/**
 * UTF-8 字节偏移 → UTF-16 码元偏移。
 *
 *  ★ 为什么必须换算：系统输入法（Java）回传的选区是 **UTF-16** 偏移，而
 *    ImGui 的编辑状态（`STB_TexteditState::cursor`）用的是 **ImWchar** 计数
 *    （本工程 ImWchar 是 16 位 ⇒ 与 UTF-16 一致）。两个口径混用会让光标
 *    在中英文混排时错位（一个汉字 UTF-8 占 3 字节、UTF-16 占 1 个码元；
 *    emoji UTF-8 占 4 字节、UTF-16 占 2 个码元 = 一个代理对）。
 */
inline int ImeUtf16IndexFromUtf8(const std::string& text, int utf8Offset) {
    if (utf8Offset <= 0) return 0;
    int wcharIndex = 0;
    int i = 0;
    const int limit = static_cast<int>(text.size());
    while (i < limit && i < utf8Offset) {
        const unsigned char b = static_cast<unsigned char>(text[i]);
        int size = (b < 0x80u) ? 1 : ((b & 0xE0u) == 0xC0u) ? 2 : ((b & 0xF0u) == 0xE0u) ? 3 : 4;
        i += size;
        wcharIndex += (size == 4) ? 2 : 1;      // 4 字节 UTF-8 = 代理对 = 2 个 UTF-16 码元
    }
    return wcharIndex;
}

struct ImeTextDelta {
    bool identical = false;   // 与上一次完全相同 ⇒ 什么都不做（去抖后的重复上报）
    bool tailOnly = false;    // 纯末尾追加 ⇒ 最快的路径（不需要退格）
    int backspaces = 0;       // 先按 Backspace 的次数（UTF-8 码点）
    std::string insert;       // 随后插入的文本（UTF-8）
};

inline ImeTextDelta ComputeImeTextDelta(const std::string& previous, const std::string& next) {
    ImeTextDelta out;
    if (previous == next) {
        out.identical = true;
        return out;
    }
    // 公共前缀（按字节比较是安全的：UTF-8 的自同步性质保证"字节相同 ⇒ 字符相同"）
    std::size_t prefix = 0;
    while (prefix < previous.size() && prefix < next.size() && previous[prefix] == next[prefix]) {
        ++prefix;
    }
    out.tailOnly = (prefix == previous.size());
    out.backspaces = ImeUtf8Length(previous.substr(prefix));
    out.insert = next.substr(prefix);
    return out;
}

}  // namespace aurora
