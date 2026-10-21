#pragma once

// -----------------------------------------------------------------------------
// 统一日志出口（AuroraLog）
//
// 为什么要有这个头：
//   工程里原来有几套并行的打日志方式 —— 有的走 overlay 自己的 logf()，有的直接
//   调 __android_log_print()。做"纯净版"时只静默了前者，后者照旧在跑，于是
//   编译期开关看起来生效了、实际还在打（ELF 里仍能查到 __android_log_write 引用）。
//
//   这里把它们收成一个宏，静默开关只需要管一处。
//
// AURORA_SILENT=1（默认，见 CMakeLists）时：
//   宏展开成 `(void)0`，**连参数都不求值**。
//   这一点很重要：调用点的表达式里有 std::string::c_str()、算术、函数返回值，
//   如果只做成"运行期 if (silent) return"，那些参数仍然会被构造一遍 —— 而
//   这些正是热路径（每帧、每帧多次）里真正花时间的地方。
//
// 需要日志时：cmake -DAURORA_SILENT=0 …
// -----------------------------------------------------------------------------

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#ifndef AURORA_SILENT
#define AURORA_SILENT 1
#endif

#if AURORA_SILENT

// 完全空操作。注意用的是 `(void)0` 而不是空字符串，这样
// `if (cond) AURORA_LOG(...); else ...` 之类的位置也不会被破坏。
#define AURORA_LOG(...) ((void)0)

#else

#if defined(__ANDROID__)
#define AURORA_LOG(prio, ...) __android_log_print((prio), "Aurora", __VA_ARGS__)
#else
#include <cstdio>
#define AURORA_LOG(prio, ...)                 \
  do {                                        \
    (void)(prio);                             \
    std::fprintf(stderr, __VA_ARGS__);        \
    std::fprintf(stderr, "\n");               \
  } while (0)
#endif

#endif
