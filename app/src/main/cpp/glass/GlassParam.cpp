#include "GlassParam.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace aurora {
namespace glass {
namespace {

bool Finite(float v) { return std::isfinite(v); }

float Clamp(float v, float lo, float hi) {
  if (!Finite(v)) return lo;
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

int ClampInt(int v, int lo, int hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

std::string FormatFloat(float v, const char* format) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), format, static_cast<double>(v));
  return std::string(buffer);
}

}  // namespace

const char* ParamKindName(ParamKind kind) {
  switch (kind) {
    case ParamKind::Float: return "float";
    case ParamKind::Int: return "int";
    case ParamKind::Bool: return "bool";
    case ParamKind::Enum: return "enum";
    case ParamKind::Color: return "color";
  }
  return "?";
}

const char* ParamCostName(ParamCost cost) {
  switch (cost) {
    case ParamCost::Uniform: return "uniform";
    case ParamCost::EffectChain: return "effect-chain";
    case ParamCost::Pipeline: return "pipeline";
  }
  return "?";
}

const char* ChangeOriginName(ChangeOrigin origin) {
  switch (origin) {
    case ChangeOrigin::Ui: return "ui";
    case ChangeOrigin::Component: return "component";
    case ChangeOrigin::Preset: return "preset";
    case ChangeOrigin::Reset: return "reset";
    case ChangeOrigin::Test: return "test";
  }
  return "?";
}

float ParamValue::Scalar() const {
  switch (kind) {
    case ParamKind::Float: return f;
    case ParamKind::Int:
    case ParamKind::Enum: return static_cast<float>(i);
    case ParamKind::Bool: return b ? 1.0f : 0.0f;
    case ParamKind::Color: return rgba[0];
  }
  return 0.0f;
}

bool ParamValue::Equals(const ParamValue& other, float epsilon) const {
  if (kind != other.kind) return false;
  switch (kind) {
    case ParamKind::Float:
      return std::fabs(f - other.f) <= epsilon;
    case ParamKind::Int:
    case ParamKind::Enum:
      return i == other.i;
    case ParamKind::Bool:
      return b == other.b;
    case ParamKind::Color:
      for (int c = 0; c < 4; ++c) {
        if (std::fabs(rgba[c] - other.rgba[c]) > epsilon) return false;
      }
      return true;
  }
  return false;
}

std::string ParamValue::ToText() const {
  switch (kind) {
    case ParamKind::Float: return FormatFloat(f, "%.4f");
    case ParamKind::Int:
    case ParamKind::Enum: return std::to_string(i);
    case ParamKind::Bool: return b ? "true" : "false";
    case ParamKind::Color: {
      char buffer[96];
      std::snprintf(buffer, sizeof(buffer), "%.3f,%.3f,%.3f,%.3f",
                    static_cast<double>(rgba[0]), static_cast<double>(rgba[1]),
                    static_cast<double>(rgba[2]), static_cast<double>(rgba[3]));
      return std::string(buffer);
    }
  }
  return "";
}

// -----------------------------------------------------------------------------
// ParamSlot
// -----------------------------------------------------------------------------
ParamValue ParamSlot::Read() const {
  ParamValue out;
  out.kind = desc.kind;
  if (ref == nullptr) return out;
  switch (desc.kind) {
    case ParamKind::Float:
      out.f = *static_cast<const float*>(ref);
      break;
    case ParamKind::Int:
    case ParamKind::Enum:
      out.i = *static_cast<const int*>(ref);
      break;
    case ParamKind::Bool:
      out.b = *static_cast<const bool*>(ref);
      break;
    case ParamKind::Color: {
      const float* src = static_cast<const float*>(ref);
      for (int c = 0; c < 4; ++c) out.rgba[c] = src[c];
      break;
    }
  }
  return out;
}

void ParamSlot::Write(const ParamValue& value) {
  if (ref == nullptr) return;
  switch (desc.kind) {
    case ParamKind::Float:
      *static_cast<float*>(ref) = value.f;
      break;
    case ParamKind::Int:
    case ParamKind::Enum:
      *static_cast<int*>(ref) = value.i;
      break;
    case ParamKind::Bool:
      *static_cast<bool*>(ref) = value.b;
      break;
    case ParamKind::Color: {
      float* dst = static_cast<float*>(ref);
      for (int c = 0; c < 4; ++c) dst[c] = value.rgba[c];
      break;
    }
  }
}

bool Sanitize(const ParamDesc& desc, ParamValue& value) {
  bool clean = true;
  switch (desc.kind) {
    case ParamKind::Float: {
      if (!Finite(value.f)) {
        value.f = desc.minF;
        clean = false;
      }
      const float clamped = Clamp(value.f, desc.minF, desc.maxF);
      if (clamped != value.f) clean = false;
      value.f = clamped;
      break;
    }
    case ParamKind::Int:
    case ParamKind::Enum: {
      const int clamped = ClampInt(value.i, desc.minI, desc.maxI);
      if (clamped != value.i) clean = false;
      value.i = clamped;
      break;
    }
    case ParamKind::Bool:
      break;
    case ParamKind::Color: {
      for (int c = 0; c < 4; ++c) {
        if (!Finite(value.rgba[c])) {
          value.rgba[c] = 0.0f;
          clean = false;
        }
        const float clamped = Clamp(value.rgba[c], 0.0f, 1.0f);
        if (clamped != value.rgba[c]) clean = false;
        value.rgba[c] = clamped;
      }
      break;
    }
  }
  value.kind = desc.kind;
  return clean;
}

float Normalize(const ParamDesc& desc, const ParamValue& value) {
  switch (desc.kind) {
    case ParamKind::Float: {
      const float span = desc.maxF - desc.minF;
      if (!(span > 0.0f)) return 0.0f;
      return Clamp((value.f - desc.minF) / span, 0.0f, 1.0f);
    }
    case ParamKind::Int:
    case ParamKind::Enum: {
      const int span = desc.maxI - desc.minI;
      if (span <= 0) return 0.0f;
      return Clamp(static_cast<float>(value.i - desc.minI) / static_cast<float>(span),
                   0.0f, 1.0f);
    }
    case ParamKind::Bool:
      return value.b ? 1.0f : 0.0f;
    case ParamKind::Color:
      return Clamp(value.rgba[3], 0.0f, 1.0f);   // 颜色用 alpha 当"强度"
  }
  return 0.0f;
}

ParamValue Denormalize(const ParamDesc& desc, float t) {
  ParamValue out;
  out.kind = desc.kind;
  const float clamped = Clamp(t, 0.0f, 1.0f);
  switch (desc.kind) {
    case ParamKind::Float:
      out.f = desc.minF + (desc.maxF - desc.minF) * clamped;
      break;
    case ParamKind::Int:
    case ParamKind::Enum: {
      const float span = static_cast<float>(desc.maxI - desc.minI);
      out.i = desc.minI + static_cast<int>(std::lround(span * clamped));
      break;
    }
    case ParamKind::Bool:
      out.b = clamped >= 0.5f;
      break;
    case ParamKind::Color:
      out.rgba[3] = clamped;
      break;
  }
  return out;
}

std::string RangeText(const ParamDesc& desc) {
  switch (desc.kind) {
    case ParamKind::Float: {
      std::string text = FormatFloat(desc.minF, "%.3g") + " ~ " + FormatFloat(desc.maxF, "%.3g");
      if (!desc.unit.empty()) text += " " + desc.unit;
      return text;
    }
    case ParamKind::Int:
    case ParamKind::Enum:
      return std::to_string(desc.minI) + " ~ " + std::to_string(desc.maxI);
    case ParamKind::Bool:
      return "开 / 关";
    case ParamKind::Color:
      return "RGBA 0 ~ 1";
  }
  return "";
}

// -----------------------------------------------------------------------------
// Target
// -----------------------------------------------------------------------------
const ParamSlot* Target::FindParam(const std::string& id) const {
  for (const ParamSlot& slot : params) {
    if (slot.desc.id == id) return &slot;
  }
  return nullptr;
}

ParamSlot* Target::FindParam(const std::string& id) {
  for (ParamSlot& slot : params) {
    if (slot.desc.id == id) return &slot;
  }
  return nullptr;
}

float Target::GetF(const std::string& id, float fallback) const {
  const ParamSlot* slot = FindParam(id);
  return slot != nullptr ? slot->Read().f : fallback;
}

int Target::GetI(const std::string& id, int fallback) const {
  const ParamSlot* slot = FindParam(id);
  return slot != nullptr ? slot->Read().i : fallback;
}

bool Target::GetB(const std::string& id, bool fallback) const {
  const ParamSlot* slot = FindParam(id);
  return slot != nullptr ? slot->Read().b : fallback;
}

// -----------------------------------------------------------------------------
// Snapshot
// -----------------------------------------------------------------------------
const SnapshotEntry* Snapshot::Find(const std::string& target, const std::string& param) const {
  for (const SnapshotEntry& entry : entries) {
    if (entry.target == target && entry.param == param) return &entry;
  }
  return nullptr;
}

}  // namespace glass
}  // namespace aurora
