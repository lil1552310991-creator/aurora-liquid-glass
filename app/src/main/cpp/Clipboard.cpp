#include "Clipboard.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <android/log.h>

#include "AuroraLog.h"

namespace aurora::clipboard {
namespace {

// 跑一条只读 shell 命令并收全部输出。
// ★ 只用于**读取**状态（dumpsys / service call 的查询方法），绝不执行会改状态的东西。
std::string RunCapture(const char* cmd, size_t maxBytes = 64u * 1024u) {
  std::string out;
  FILE* p = ::popen(cmd, "r");
  if (p == nullptr) return out;
  char buf[1024];
  while (out.size() < maxBytes && std::fgets(buf, sizeof buf, p) != nullptr) out += buf;
  ::pclose(p);
  return out;
}

// 从一段文本里摘出"引号包起来的内容"。返回第一个非空、且不是元数据的候选。
//
// dumpsys clipboard 的格式在 Android 10 → 16 之间改过好几次：
//   ClipData { text/plain "hello" }
//   ClipData { text/plain {T:hello} }
//   mPrimaryClip = ClipData { text/plain "hello" }
// 所以这里不做"精确匹配某个模板"，而是找**引号对**，再用几条启发式排除噪音。
bool ExtractQuoted(const std::string& s, std::string* out) {
  static const char* kSkip[] = {
      "text/plain", "text/html", "text/uri-list", "android.intent.action", "ClipData",
      "ClipDescription", "mPrimaryClip", "isSensitive", "null",
  };

  size_t i = 0;
  while (i < s.size()) {
    // 找一对 '…' 或 "…"，两者都可能出现（AOSP 的 ClipData::toString 用双引号，
    // 有些版本用单引号包 {T:...}）。
    const char open = s[i];
    if (open != '"' && open != '\'') {
      ++i;
      continue;
    }
    const size_t begin = i + 1;
    size_t end = begin;
    while (end < s.size() && s[end] != open && s[end] != '\n') ++end;
    if (end >= s.size() || s[end] != open) {
      ++i;
      continue;
    }
    std::string candidate = s.substr(begin, end - begin);
    i = end + 1;
    if (candidate.empty()) continue;

    // 排除明显的 MIME 类型 / 类名 / 布尔串
    bool skip = false;
    for (const char* k : kSkip) {
      if (candidate == k) { skip = true; break; }
    }
    if (skip) continue;
    // 全是空白 / 全是符号的不要
    bool hasPrintable = false;
    for (char c : candidate) {
      if (static_cast<unsigned char>(c) > 0x20 && c != 0x7F) { hasPrintable = true; break; }
    }
    if (!hasPrintable) continue;

    *out = candidate;
    return true;
  }
  return false;
}

// 处理 "ClipData { text/plain {T:hello} }" 这种把类型塞进大括号的写法。
bool ExtractBraceT(const std::string& s, std::string* out) {
  // 找 "{T:" 或者 "T:" 后面直接跟内容
  for (size_t pos = 0; pos + 2 < s.size(); ++pos) {
    if (s[pos] != 'T' || s[pos + 1] != ':') continue;
    size_t begin = pos + 2;
    size_t end = begin;
    while (end < s.size() && s[end] != '}' && s[end] != '"' && s[end] != '\'' &&
           s[end] != '\n' && s[end] != ',') {
      ++end;
    }
    if (end <= begin) continue;
    std::string candidate = s.substr(begin, end - begin);
    // 去掉尾部空白
    while (!candidate.empty() &&
           (candidate.back() == ' ' || candidate.back() == '\t' || candidate.back() == '\r')) {
      candidate.pop_back();
    }
    if (candidate.empty()) continue;
    *out = candidate;
    return true;
  }
  return false;
}

}  // namespace

bool ReadText(std::string* out, std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  if (error != nullptr) error->clear();

  // ---- 主路径：dumpsys clipboard（只读，不会改动用户的剪贴板）----
  //
  // 为什么不用 service call：IClipboard 的 transaction code 每个 Android 版本
  // 都会重排，盲扫 code 有把 setPrimaryClip 打中的风险 —— 那会把用户正在用的
  // 剪贴板覆写掉。dumpsys 是纯查询，最坏情况只是拿不到数据。
  const std::string dump = RunCapture("dumpsys clipboard 2>/dev/null");
  if (!dump.empty()) {
    if (ExtractQuoted(dump, out)) {
      AURORA_LOG(ANDROID_LOG_INFO, "剪贴板：从 dumpsys 取到 %zu 字节", out->size());
      return true;
    }
    if (ExtractBraceT(dump, out)) {
      AURORA_LOG(ANDROID_LOG_INFO, "剪贴板：从 dumpsys 的 {T:...} 取到 %zu 字节", out->size());
      return true;
    }
  }

  // ---- 备用：service call 的**查询**方法 ----
  //
  // 这里只试几个"只读"方法的 code。IClipboard 在多数版本里：
  //   3 = getPrimaryClip(String, String, int, int)
  //   4 = getPrimaryClipAsPackage(...)
  //   5 = getPrimaryClipDescription(...)
  // 参数用 shell 包名 + 空 attributionTag + userId 0。拿不到就当没有。
  if (!out->empty()) return true;

  static const char* kQueries[] = {
      "service call clipboard 3 s16 com.android.shell s16 null i32 0 i32 0 2>/dev/null",
      "service call clipboard 4 s16 com.android.shell s16 null i32 0 i32 0 2>/dev/null",
  };
  for (const char* q : kQueries) {
    const std::string reply = RunCapture(q);
    if (reply.empty()) continue;
    if (ExtractQuoted(reply, out) || ExtractBraceT(reply, out)) {
      AURORA_LOG(ANDROID_LOG_INFO, "剪贴板：从 service call 取到 %zu 字节", out->size());
      return true;
    }
  }

  if (error != nullptr) {
    *error = dump.empty() ? "读不到剪贴板服务" : "剪贴板里没有可用的文本";
  }
  return false;
}

}  // namespace aurora::clipboard
