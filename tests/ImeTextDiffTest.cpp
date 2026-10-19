// ===========================================================================
//  ImeTextDiff.h 的离线回归
//
//  这一组测试直接钉住用户报的两个现象：
//    · 「只能打字上去」  ⇒ 纯追加必须是 backspaces=0、insert=新增部分
//    · 「而且不能删除」  ⇒ 文本变短必须产生 backspaces>0（旧实现是追加语义，
//                          更短的文本 = 什么都不做 ⇒ 永远删不掉）
//  另外把"按字节退格会把汉字退成半个"这类中文特有的坑也钉住。
// ===========================================================================

#include "ImeTextDiff.h"

#include <cassert>
#include <cstdio>
#include <string>

using aurora::ComputeImeTextDelta;
using aurora::ImeTextDelta;
using aurora::ImeUtf8Length;

static int g_groups = 0;

// ---- ① 正常打字：纯追加，不许产生任何退格 -------------------------------
static void TestAppendOnly() {
    {
        const ImeTextDelta d = ComputeImeTextDelta("", "a");
        assert(!d.identical);
        assert(d.tailOnly);
        assert(d.backspaces == 0);
        assert(d.insert == "a");
    }
    {
        const ImeTextDelta d = ComputeImeTextDelta("ab", "abc");
        assert(d.tailOnly && d.backspaces == 0 && d.insert == "c");
    }
    // 中文一句话：一次回传多个字（输入法上屏一整句）
    {
        const ImeTextDelta d = ComputeImeTextDelta("你好", "你好世界");
        assert(d.tailOnly && d.backspaces == 0 && d.insert == "世界");
    }
    ++g_groups;
}

// ---- ② ★ 退格（用户报的"不能删除"） --------------------------------------
static void TestBackspace() {
    {
        const ImeTextDelta d = ComputeImeTextDelta("abc", "ab");
        assert(!d.identical);
        assert(d.backspaces == 1);      // 删一个字符
        assert(d.insert.empty());
    }
    {
        // 全删：一次性退到底
        const ImeTextDelta d = ComputeImeTextDelta("abc", "");
        assert(d.backspaces == 3 && d.insert.empty());
    }
    {
        // ★ 中文必须按**字符**退：一个汉字 3 字节，退 1 次而不是 3 次
        const ImeTextDelta d = ComputeImeTextDelta("你好吗", "你好");
        assert(d.backspaces == 1);
        assert(d.insert.empty());
    }
    {
        // 带 emoji（4 字节码点）也要按字符数算
        const ImeTextDelta d = ComputeImeTextDelta("a\xF0\x9F\x98\x80 b", "a\xF0\x9F\x98\x80 ");
        assert(d.backspaces == 1 && d.insert.empty());
    }
    ++g_groups;
}

// ---- ③ 末尾替换 / 中间编辑：多退几次再重插，结果必须一致 -------------------
static void TestReplaceAndMiddleEdit() {
    {
        const ImeTextDelta d = ComputeImeTextDelta("abc", "abd");
        assert(d.backspaces == 1 && d.insert == "d");
    }
    {
        // 中间插入（Java 侧光标被移到中间）：退到公共前缀再重插剩下全部
        const ImeTextDelta d = ComputeImeTextDelta("abc", "aXbc");
        assert(d.backspaces == 2 && d.insert == "Xbc");
    }
    {
        // 整段替换
        const ImeTextDelta d = ComputeImeTextDelta("abc", "xyz");
        assert(d.backspaces == 3 && d.insert == "xyz");
    }
    // 应用回放：按算法执行之后必须**逐字节等于**目标文本
    struct Case { const char* from; const char* to; };
    const Case cases[] = {
        {"", "hello"}, {"hello", ""}, {"hello", "hello!"}, {"hello!", "hello"},
        {"a", "ab"}, {"ab", "a"}, {"中文", "中"}, {"中", "中文测试"},
        {"abc", "aXbc"}, {"abc", "xyz"}, {"a\nb", "a\nb\nc"}, {"1.5", "1."},
    };
    for (const Case& c : cases) {
        const ImeTextDelta d = ComputeImeTextDelta(c.from, c.to);
        // 回放：退格（按字符）→ 插入
        std::string replay(c.from);
        for (int i = 0; i < d.backspaces; ++i) {
            assert(!replay.empty());
            // 删一个 UTF-8 字符（从末尾回退到上一个非续字节）
            std::size_t cut = replay.size();
            while (cut > 0 && (static_cast<unsigned char>(replay[cut - 1]) & 0xC0u) == 0x80u) --cut;
            if (cut > 0) --cut;
            replay.erase(cut);
        }
        replay += d.insert;
        assert(replay == c.to);
    }
    ++g_groups;
}

// ---- ④ 重复上报：必须被判成 identical（去抖后的重复不能重复插入） --------
static void TestIdentical() {
    assert(ComputeImeTextDelta("abc", "abc").identical);
    assert(ComputeImeTextDelta("", "").identical);
    assert(ComputeImeTextDelta("你好", "你好").identical);
    const ImeTextDelta d = ComputeImeTextDelta("abc", "abc");
    assert(d.backspaces == 0 && d.insert.empty());
    ++g_groups;
}

// ---- ⑤ 码点计数本身 ------------------------------------------------
static void TestUtf8Length() {
    assert(ImeUtf8Length("") == 0);
    assert(ImeUtf8Length("abc") == 3);
    assert(ImeUtf8Length("你好") == 2);                       // 6 字节 / 2 字符
    assert(ImeUtf8Length("a你b") == 3);
    assert(ImeUtf8Length("\xF0\x9F\x98\x80") == 1);           // emoji 4 字节
    assert(ImeUtf8Length("e\xCC\x81") == 2);                  // 组合音标：两个码点
    ++g_groups;
}

// ---- ⑥ ★ UTF-8 偏移 → UTF-16 码元偏移（光标位置换算） ------------------
//      错一位 = 光标插在错的地方 = 用户看到的"莫名其妙多一个字/字跑到前面去"
static void TestUtf16IndexConversion() {
    using aurora::ImeUtf16IndexFromUtf8;
    const std::string ascii = "hello";
    assert(ImeUtf16IndexFromUtf8(ascii, 0) == 0);
    assert(ImeUtf16IndexFromUtf8(ascii, 3) == 3);
    assert(ImeUtf16IndexFromUtf8(ascii, 5) == 5);
    assert(ImeUtf16IndexFromUtf8(ascii, 99) == 5);            // 越界夹到末尾

    const std::string cn = "你好a";                           // 3+3+1 字节，1+1+1 码元
    assert(ImeUtf16IndexFromUtf8(cn, 0) == 0);
    assert(ImeUtf16IndexFromUtf8(cn, 3) == 1);                // 一个汉字之后
    assert(ImeUtf16IndexFromUtf8(cn, 6) == 2);
    assert(ImeUtf16IndexFromUtf8(cn, 7) == 3);

    const std::string emoji = "a\xF0\x9F\x98\x80" "b";        // a + emoji(4B) + b
    assert(ImeUtf16IndexFromUtf8(emoji, 1) == 1);
    assert(ImeUtf16IndexFromUtf8(emoji, 5) == 3);             // emoji 占 2 个 UTF-16 码元
    assert(ImeUtf16IndexFromUtf8(emoji, 6) == 4);

    // 与"码点数"的区别：emoji 一个码点 = 两个 UTF-16 码元
    assert(ImeUtf8Length(emoji) == 3);
    assert(ImeUtf16IndexFromUtf8(emoji, static_cast<int>(emoji.size())) == 4);
    ++g_groups;
}

int main() {
    TestAppendOnly();
    TestBackspace();
    TestReplaceAndMiddleEdit();
    TestIdentical();
    TestUtf8Length();
    TestUtf16IndexConversion();
    std::printf("ImeTextDiffTest: %d regression groups passed\n", g_groups);
    return 0;
}
