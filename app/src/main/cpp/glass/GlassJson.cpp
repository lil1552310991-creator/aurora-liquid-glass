#include "GlassJson.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace aurora {
namespace glass {
namespace {

// 缺失字段/越界下标的统一返回值（引用必须长期有效）
const JsonValue& NullJson() {
  static const JsonValue null;
  return null;
}

}  // namespace

const std::string JsonValue::kEmpty;

JsonValue JsonValue::Bool(bool value) {
  JsonValue out;
  out.type_ = Type::Bool;
  out.bool_ = value;
  return out;
}

JsonValue JsonValue::Number(double value) {
  JsonValue out;
  out.type_ = Type::Number;
  out.number_ = value;
  return out;
}

JsonValue JsonValue::Str(const std::string& value) {
  JsonValue out;
  out.type_ = Type::String;
  out.string_ = value;
  return out;
}

JsonValue JsonValue::Array() {
  JsonValue out;
  out.type_ = Type::Array;
  return out;
}

JsonValue JsonValue::Object() {
  JsonValue out;
  out.type_ = Type::Object;
  return out;
}

bool JsonValue::AsBool(bool fallback) const {
  if (type_ == Type::Bool) return bool_;
  if (type_ == Type::Number) return number_ != 0.0;
  return fallback;
}

double JsonValue::AsNumber(double fallback) const {
  if (type_ == Type::Number) return number_;
  if (type_ == Type::Bool) return bool_ ? 1.0 : 0.0;
  return fallback;
}

float JsonValue::AsFloat(float fallback) const {
  const double value = AsNumber(static_cast<double>(fallback));
  return std::isfinite(value) ? static_cast<float>(value) : fallback;
}

int JsonValue::AsInt(int fallback) const {
  if (type_ != Type::Number) return fallback;
  const double rounded = std::floor(number_ + 0.5);
  if (!std::isfinite(rounded)) return fallback;
  return static_cast<int>(rounded);
}

const std::string& JsonValue::AsString() const {
  return type_ == Type::String ? string_ : kEmpty;
}

std::size_t JsonValue::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  return 0;
}

const JsonValue& JsonValue::At(std::size_t index) const {
  if (type_ != Type::Array || index >= array_.size()) return NullJson();
  return array_[index];
}

void JsonValue::Push(JsonValue value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  array_.push_back(std::move(value));
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
  if (type_ != Type::Object) return NullJson();
  auto it = object_.find(key);
  return it == object_.end() ? NullJson() : it->second;
}

bool JsonValue::Has(const std::string& key) const {
  return type_ == Type::Object && object_.find(key) != object_.end();
}

void JsonValue::Set(const std::string& key, JsonValue value) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  object_[key] = std::move(value);
}

// -----------------------------------------------------------------------------
// 序列化
// -----------------------------------------------------------------------------
std::string JsonEscape(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char ch : text) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(ch) & 0xFFu);
          out += buffer;
        } else {
          out += ch;
        }
        break;
    }
  }
  return out;
}

namespace {

std::string NumberToText(double value) {
  if (!std::isfinite(value)) return "0";
  if (value == std::floor(value) && std::fabs(value) < 1.0e15) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return std::string(buffer);
  }
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "%.9g", value);
  return std::string(buffer);
}

}  // namespace

void JsonValue::DumpTo(std::string& out, int indent, int depth) const {
  const std::string pad(indent > 0 ? static_cast<std::size_t>(indent * depth) : 0, ' ');
  const std::string padInner(indent > 0 ? static_cast<std::size_t>(indent * (depth + 1)) : 0, ' ');
  const char* newline = indent > 0 ? "\n" : "";
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_ ? "true" : "false";
      break;
    case Type::Number:
      out += NumberToText(number_);
      break;
    case Type::String:
      out += "\"" + JsonEscape(string_) + "\"";
      break;
    case Type::Array: {
      if (array_.empty()) {
        out += "[]";
        break;
      }
      out += "[";
      out += newline;
      for (std::size_t i = 0; i < array_.size(); ++i) {
        out += padInner;
        array_[i].DumpTo(out, indent, depth + 1);
        if (i + 1 < array_.size()) out += ",";
        out += newline;
      }
      out += pad + "]";
      break;
    }
    case Type::Object: {
      if (object_.empty()) {
        out += "{}";
        break;
      }
      out += "{";
      out += newline;
      std::size_t index = 0;
      for (const auto& item : object_) {
        out += padInner + "\"" + JsonEscape(item.first) + "\":" + (indent > 0 ? " " : "");
        item.second.DumpTo(out, indent, depth + 1);
        if (++index < object_.size()) out += ",";
        out += newline;
      }
      out += pad + "}";
      break;
    }
  }
}

std::string JsonValue::Dump(int indent) const {
  std::string out;
  DumpTo(out, indent, 0);
  return out;
}

// -----------------------------------------------------------------------------
// 解析
// -----------------------------------------------------------------------------
namespace {

class Parser {
 public:
  Parser(const std::string& text) : text_(text) {}

  bool Parse(JsonValue* out, std::string* error) {
    SkipWhitespace();
    if (!ParseValue(out)) {
      if (error != nullptr) *error = ErrorText();
      return false;
    }
    SkipWhitespace();
    if (pos_ != text_.size()) {
      Fail("末尾有多余内容");
      if (error != nullptr) *error = ErrorText();
      return false;
    }
    return true;
  }

 private:
  std::string ErrorText() const {
    int line = 1;
    int column = 1;
    for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    char buffer[256];
    std::snprintf(buffer, sizeof(buffer), "第 %d 行第 %d 列: %s", line, column, message_.c_str());
    return std::string(buffer);
  }

  void Fail(const char* message) {
    if (message_.empty()) message_ = message;
  }

  void SkipWhitespace() {
    while (pos_ < text_.size()) {
      const char ch = text_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool ParseValue(JsonValue* out) {
    SkipWhitespace();
    if (pos_ >= text_.size()) {
      Fail("内容意外结束");
      return false;
    }
    switch (text_[pos_]) {
      case '{': return ParseObject(out);
      case '[': return ParseArray(out);
      case '"': {
        std::string value;
        if (!ParseString(&value)) return false;
        *out = JsonValue::Str(value);
        return true;
      }
      case 't':
        if (text_.compare(pos_, 4, "true") == 0) {
          pos_ += 4;
          *out = JsonValue::Bool(true);
          return true;
        }
        Fail("无效字面量");
        return false;
      case 'f':
        if (text_.compare(pos_, 5, "false") == 0) {
          pos_ += 5;
          *out = JsonValue::Bool(false);
          return true;
        }
        Fail("无效字面量");
        return false;
      case 'n':
        if (text_.compare(pos_, 4, "null") == 0) {
          pos_ += 4;
          *out = JsonValue();
          return true;
        }
        Fail("无效字面量");
        return false;
      default: return ParseNumber(out);
    }
  }

  bool ParseObject(JsonValue* out) {
    ++pos_;   // '{'
    *out = JsonValue::Object();
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == '}') {
      ++pos_;
      return true;
    }
    while (true) {
      SkipWhitespace();
      std::string key;
      if (pos_ >= text_.size() || text_[pos_] != '"') {
        Fail("对象的键必须是字符串");
        return false;
      }
      if (!ParseString(&key)) return false;
      SkipWhitespace();
      if (pos_ >= text_.size() || text_[pos_] != ':') {
        Fail("键之后缺少冒号");
        return false;
      }
      ++pos_;
      JsonValue value;
      if (!ParseValue(&value)) return false;
      out->Set(key, std::move(value));
      SkipWhitespace();
      if (pos_ >= text_.size()) {
        Fail("对象没有闭合");
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == '}') {
        ++pos_;
        return true;
      }
      Fail("对象里出现意外字符");
      return false;
    }
  }

  bool ParseArray(JsonValue* out) {
    ++pos_;   // '['
    *out = JsonValue::Array();
    SkipWhitespace();
    if (pos_ < text_.size() && text_[pos_] == ']') {
      ++pos_;
      return true;
    }
    while (true) {
      JsonValue value;
      if (!ParseValue(&value)) return false;
      out->Push(std::move(value));
      SkipWhitespace();
      if (pos_ >= text_.size()) {
        Fail("数组没有闭合");
        return false;
      }
      if (text_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (text_[pos_] == ']') {
        ++pos_;
        return true;
      }
      Fail("数组里出现意外字符");
      return false;
    }
  }

  bool ParseString(std::string* out) {
    ++pos_;   // '"'
    out->clear();
    while (pos_ < text_.size()) {
      const char ch = text_[pos_++];
      if (ch == '"') return true;
      if (ch != '\\') {
        *out += ch;
        continue;
      }
      if (pos_ >= text_.size()) {
        Fail("转义序列不完整");
        return false;
      }
      const char esc = text_[pos_++];
      switch (esc) {
        case '"': *out += '"'; break;
        case '\\': *out += '\\'; break;
        case '/': *out += '/'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'n': *out += '\n'; break;
        case 'r': *out += '\r'; break;
        case 't': *out += '\t'; break;
        case 'u': {
          if (pos_ + 4 > text_.size()) {
            Fail("\\u 转义不完整");
            return false;
          }
          unsigned code = 0;
          for (int i = 0; i < 4; ++i) {
            const char digit = text_[pos_ + static_cast<std::size_t>(i)];
            code <<= 4;
            if (digit >= '0' && digit <= '9') code |= static_cast<unsigned>(digit - '0');
            else if (digit >= 'a' && digit <= 'f') code |= static_cast<unsigned>(digit - 'a' + 10);
            else if (digit >= 'A' && digit <= 'F') code |= static_cast<unsigned>(digit - 'A' + 10);
            else {
              Fail("\\u 后面不是十六进制");
              return false;
            }
          }
          pos_ += 4;
          // 只处理 BMP（参数配置里的中文名字都在这里）
          if (code < 0x80u) {
            *out += static_cast<char>(code);
          } else if (code < 0x800u) {
            *out += static_cast<char>(0xC0u | (code >> 6));
            *out += static_cast<char>(0x80u | (code & 0x3Fu));
          } else {
            *out += static_cast<char>(0xE0u | (code >> 12));
            *out += static_cast<char>(0x80u | ((code >> 6) & 0x3Fu));
            *out += static_cast<char>(0x80u | (code & 0x3Fu));
          }
          break;
        }
        default:
          Fail("未知转义字符");
          return false;
      }
    }
    Fail("字符串没有闭合");
    return false;
  }

  bool ParseNumber(JsonValue* out) {
    const std::size_t start = pos_;
    if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
    bool digits = false;
    while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
      ++pos_;
      digits = true;
    }
    if (pos_ < text_.size() && text_[pos_] == '.') {
      ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
        digits = true;
      }
    }
    if (!digits) {
      Fail("不是合法数字");
      return false;
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) ++pos_;
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
    }
    const std::string token = text_.substr(start, pos_ - start);
    *out = JsonValue::Number(std::strtod(token.c_str(), nullptr));
    return true;
  }

  const std::string& text_;
  std::size_t pos_ = 0;
  std::string message_;
};

}  // namespace

JsonValue JsonValue::Parse(const std::string& text, std::string* error) {
  JsonValue out;
  Parser parser(text);
  std::string localError;
  if (!parser.Parse(&out, &localError)) {
    if (error != nullptr) *error = localError;
    return JsonValue();
  }
  if (error != nullptr) error->clear();
  return out;
}

}  // namespace glass
}  // namespace aurora
