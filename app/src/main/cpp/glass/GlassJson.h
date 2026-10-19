// =============================================================================
//  极简 JSON（纯逻辑，零依赖）
//
//  为什么自己写而不是引入第三方：本项目是单 ELF 交付，多一个依赖就多一份
//  打包/许可证/体积负担；而参数配置需要的 JSON 子集很小（对象/数组/字符串/
//  数字/布尔/null）。这里只实现这个子集，但**解析器对非法输入必须给出确切
//  错误位置** —— 导入别人的配置文件时，"JSON 坏了"和"字段不认识"是两种完全
//  不同的处理（前者拒绝，后者跳过 + 报告）。
// =============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

namespace aurora {
namespace glass {

class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  static JsonValue Bool(bool value);
  static JsonValue Number(double value);
  static JsonValue Str(const std::string& value);
  static JsonValue Array();
  static JsonValue Object();

  Type type() const { return type_; }
  bool IsNull() const { return type_ == Type::Null; }

  bool AsBool(bool fallback = false) const;
  double AsNumber(double fallback = 0.0) const;
  float AsFloat(float fallback = 0.0f) const;
  int AsInt(int fallback = 0) const;
  const std::string& AsString() const;

  // 数组/对象
  std::size_t size() const;
  const JsonValue& At(std::size_t index) const;      // 数组
  void Push(JsonValue value);
  const JsonValue& operator[](const std::string& key) const;   // 对象，缺失返回 Null
  bool Has(const std::string& key) const;
  void Set(const std::string& key, JsonValue value);
  const std::map<std::string, JsonValue>& fields() const { return object_; }
  const std::vector<JsonValue>& items() const { return array_; }

  std::string Dump(int indent = 2) const;

  // 解析失败返回 Null 并填 error（含行号列号）
  static JsonValue Parse(const std::string& text, std::string* error);

 private:
  void DumpTo(std::string& out, int indent, int depth) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double number_ = 0.0;
  std::string string_;
  std::vector<JsonValue> array_;
  std::map<std::string, JsonValue> object_;
  static const std::string kEmpty;
};

std::string JsonEscape(const std::string& text);

}  // namespace glass
}  // namespace aurora
