// =============================================================================
//  参数持久化 / 导入导出 / 迁移 / 原子保存 的离线回归（不需要手机）
//
//  覆盖任务书 §8 的全部条目：
//    1. 保存当前组件配置   2. 保存整窗口参数   3. 保存全部参数
//    4. 恢复默认值         5. 撤销重做（另见 GlassHistoryTest）
//    6. 导出配置           7. 导入配置         8. 多个预设
//    以及：参数名称变更、旧版本兼容、不合法数值、配置缺失、原子保存
// =============================================================================
#include "glass/GlassPreset.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace aurora::glass;

namespace {

int g_checks = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    std::printf("  断言失败（第 %d 项）：%s\n", g_checks, what);
    std::fflush(stdout);
    assert(condition);
  }
}

struct Tune {
  float blurDp = 2.0f;
  float lensHeightDp = 12.0f;
  float lensAmountDp = 24.0f;
  float dispersion = 0.0f;
  int edgeMode = 1;
  bool shadowOn = true;
  bool spectralOn = false;
  float accent[4] = {0.0f, 0.478f, 1.0f, 1.0f};
};

// 窗口目标：v1 迁移用例走的是它（组件演示删除后，只剩窗口相关的目标）。
Tune g_window;
Tune g_button1;
Tune g_button2;

void Register(Registry& registry, const char* id, const char* name, Tune* tune, int order) {
  TargetDesc desc;
  desc.id = id;
  desc.name = name;
  desc.type = "PillButton";
  desc.window = "secondary";
  desc.parent = "secondary_window";
  desc.order = order;
  TargetBuilder b(registry, desc);
  if (!b.created()) return;
  b.Group("模糊");
  b.Float("blurDp", "模糊半径", "blur", &tune->blurDp, 0.0f, 10.0f, "dp", "ytbl 上限 10dp");
  b.Group("折射");
  b.Float("lensHeightDp", "折射高度", "lensHeight", &tune->lensHeightDp, 0.0f, 40.0f, "dp", "");
  b.Float("lensAmountDp", "折射强度", "lensAmount", &tune->lensAmountDp, 0.0f, 60.0f, "dp", "");
  b.Bool("spectralOn", "启用色散", "spectral", &tune->spectralOn, "");
  b.Float("dispersion", "色散强度", "dispersion", &tune->dispersion, 0.0f, 2.0f, "×", "");
  b.Group("光影");
  b.Enum("edgeMode", "边光模式", "edgeMode", &tune->edgeMode, 0, 4,
         "关闭\0Gradient\0Ambient\0Plain\0Contour\0", "");
  b.Bool("shadowOn", "外阴影", "shadowOn", &tune->shadowOn, "");
  b.Color("accent", "着色", "accent", tune->accent, "");
}

// 一台"能跑起来的"最小现场：注册表 + 两个目标 + 预设仓库
struct Fixture {
  Registry registry;
  PresetStore store{registry};
  Fixture() {
    Register(registry, "secondary_window", "整窗口", &g_window, 0);
    Register(registry, "secondary_window.pill_button_1", "按钮 1", &g_button1, 10);
    Register(registry, "secondary_window.pill_button_2", "按钮 2", &g_button2, 20);
  }
};

std::string TempPath(const char* name) {
  std::string dir;
#if defined(_WIN32)
  const char* temp = std::getenv("TEMP");
  dir = temp != nullptr ? temp : ".";
#else
  dir = "/tmp";
#endif
  return dir + "/" + name;
}

}  // namespace

int main() {
  std::printf("GlassPresetTest: 参数持久化 / 导入导出 / 迁移回归\n");

  // ---- 组 1：捕获 / 应用 / 默认值 ----------------------------------------
  {
    Fixture fx;
    Check(fx.store.Count() == 1, "初始就有一个「默认」预设");
    // ★ 「默认」是**隐式**的：它的值 = 注册表现场捕获的默认值。
    //   这样"恢复默认"永远等于"把每个参数写回注册时的值"，且与
    //   PresetStore 什么时候被创建无关（构造顺序 bug 在这里被结构性排除）。
    Check(fx.store.IsImplicitDefault(0), "默认预设是隐式的（不需要存在文件里）");
    Check(fx.store.DiffFromPreset("默认") == 0, "初始状态与默认值零差异");
    Check(fx.registry.CaptureDefaults().size() > 0, "注册表能提供默认值快照");

    fx.registry.SetF("secondary_window.pill_button_1", "blurDp", 7.5f);
    fx.registry.SetF("secondary_window.pill_button_2", "lensAmountDp", 41.0f);
    Check(fx.store.DiffFromPreset("默认") == 2, "与默认预设的差异被计数");

    Check(fx.store.CaptureAs("我的配方"), "保存当前配置为预设");
    Check(fx.store.Count() == 2, "预设数量增加");

    // 4. 恢复默认值
    fx.store.ApplyPreset("默认");
    Check(std::fabs(g_button1.blurDp - 2.0f) < 1e-6f, "预设切换把按钮 1 恢复默认");
    Check(std::fabs(g_button2.lensAmountDp - 24.0f) < 1e-6f, "预设切换把按钮 2 恢复默认");

    fx.store.ApplyPreset("我的配方");
    Check(std::fabs(g_button1.blurDp - 7.5f) < 1e-6f, "切回自定义预设恢复保存的值");
    Check(std::fabs(g_button2.lensAmountDp - 41.0f) < 1e-6f, "整窗口多组件一起恢复");
    Check(fx.store.ActiveName() == "我的配方", "当前预设名被记录");
  }

  // ---- 组 2：导出 / 导入 round-trip --------------------------------------
  {
    Fixture fx;
    fx.registry.SetF("secondary_window.pill_button_1", "blurDp", 6.25f);
    fx.registry.SetI("secondary_window.pill_button_1", "edgeMode", 4);
    fx.registry.SetB("secondary_window.pill_button_1", "spectralOn", true);
    fx.registry.SetColor("secondary_window.pill_button_1", "accent", 0.25f, 0.5f, 0.75f, 1.0f);
    const std::string text = fx.store.ExportText();

    /* 导出必须是合法 JSON，且带格式标记与版本号 */
    std::string error;
    const JsonValue root = JsonValue::Parse(text, &error);
    Check(error.empty(), "导出的文本是合法 JSON");
    Check(root["format"].AsString() == "aurora.glass.params", "导出带格式标记");
    Check(root["version"].AsInt() == kPresetFormatVersion, "导出带版本号");

    // 换一个"全新进程"（新 registry + 新 store）导入
    g_button1 = Tune();
    Fixture fresh;
    ImportReport report;
    Check(fresh.store.ImportText(text, &report), "导入成功");
    Check(report.ok(), "导入没有错误");
    fresh.store.ApplyPreset(fresh.store.ActiveName());
    Check(std::fabs(g_button1.blurDp - 6.25f) < 1e-6f, "浮点值 round-trip");
    Check(g_button1.edgeMode == 4, "枚举值 round-trip");
    Check(g_button1.spectralOn, "布尔值 round-trip");
    Check(std::fabs(g_button1.accent[0] - 0.25f) < 1e-6f &&
          std::fabs(g_button1.accent[3] - 1.0f) < 1e-6f, "颜色 RGBA round-trip");
  }

  // ---- 组 3：不合法数值 / 配置缺失 ---------------------------------------
  {
    Fixture fx;
    // 手工构造一份"被人改坏了"的配置：blur 超范围、颜色通道越界、少了 kind、
    // 多了一个不存在的目标与一个不存在的参数
    const char* broken = R"({
      "format": "aurora.glass.params", "version": 2, "active": "坏配置",
      "presets": { "坏配置": { "targets": {
        "secondary_window.pill_button_1": {
          "blurDp":   { "kind": "float", "f": 999.0 },
          "accent":   { "kind": "color", "c": [2.0, -1.0, 0.5, 5.0] },
          "lensHeightDp": { "f": 20.0 },
          "不存在的参数": { "kind": "float", "f": 1.0 }
        },
        "secondary_window.不存在的目标": { "blurDp": { "kind": "float", "f": 1.0 } }
      } } }
    })";
    ImportReport report;
    Check(fx.store.ImportText(broken, &report), "坏配置仍然可以导入（部分应用）");
    Check(report.unmatched == 2, "两个当前注册表里没有的条目被统计出来");
    Check(report.notes.size() >= 2, "每个跳过都给出原因");
    std::vector<std::string> skipped;
    std::vector<std::string> adjusted;
    fx.store.ApplyPreset("坏配置", &skipped, &adjusted);
    Check(skipped.size() == 2, "应用时未知目标/参数被真正跳过并报告");
    Check(adjusted.size() == 2, "被夹紧的值单独报告（不是失败，是修正）");
    Check(std::fabs(g_button1.blurDp - 10.0f) < 1e-6f, "超范围数值被夹到 10dp");
    Check(g_button1.accent[0] == 1.0f && g_button1.accent[1] == 0.0f &&
          g_button1.accent[3] == 1.0f, "越界颜色通道被夹进 0..1");
    Check(std::fabs(g_button1.lensHeightDp - 20.0f) < 1e-6f,
          "缺少 kind 的条目按 descriptor 的类型还原");

    // JSON 本身坏了 ⇒ 必须整体拒绝，而不是半个配置悄悄生效
    const float before = g_button1.blurDp;
    ImportReport bad;
    Check(!fx.store.ImportText("{ \"version\": 2, ", &bad), "非法 JSON 被拒绝");
    Check(!bad.error.empty(), "拒绝时给出解析错误位置");
    Check(std::fabs(g_button1.blurDp - before) < 1e-6f, "被拒绝的导入没有改动任何值");
  }

  // ---- 组 4：v1 迁移（结构 + 字段改名）-----------------------------------
  {
    Fixture fx;
    // v1：按组件分组的一层平铺，颜色写成 #RRGGBBAA，字段名是旧的
    const char* v1 = R"({
      "version": 1,
      "groups": {
        "window": { "blur": 3.5, "spectral": 0.8, "shadowOn": false, "accent": "#4080C0FF" },
        "不认识的组": { "blur": 1.0 }
      }
    })";
    ImportReport report;
    Check(fx.store.ImportText(v1, &report), "v1 配置可以导入");
    Check(report.sourceVersion == 1, "识别出源版本是 1");
    Check(report.migratedFrom == 1, "记录了迁移来源");
    Check(report.skipped == 1, "没有对应目标的旧组被跳过");
    bool sawRename = false;
    for (const std::string& note : report.notes) {
      if (note.find("字段改名") != std::string::npos) sawRename = true;
    }
    Check(sawRename, "字段改名被记录在报告里");
    fx.store.ApplyPreset(fx.store.ActiveName());
    Check(std::fabs(g_window.blurDp - 3.5f) < 1e-6f, "v1 的 blur 写进了 v2 的 blurDp");
    Check(std::fabs(g_window.dispersion - 0.8f) < 1e-6f,
          "v1 的 spectral 迁移成了 v2 的 dispersion");
    Check(!g_window.shadowOn, "v1 的布尔值迁移正确");
    Check(std::fabs(g_window.accent[0] - 0.251f) < 0.01f &&
          std::fabs(g_window.accent[2] - 0.753f) < 0.01f, "v1 的 #RRGGBBAA 迁移成 RGBA");
  }

  // ---- 组 5：原子保存 / 读回 / 损坏文件 -----------------------------------
  {
    const std::string path = TempPath("aurora_glass_preset_test.json");
    RemoveFile(path);
    {
      Fixture fx;
      fx.store.SetPath(path);
      fx.registry.SetF("secondary_window.pill_button_2", "lensHeightDp", 33.0f);
      fx.store.CaptureAs("落盘测试");
      std::string error;
      Check(fx.store.Save(&error), "保存成功");
      Check(error.empty(), "保存没有报错");
      Check(FileExists(path), "文件真的写出来了");
      Check(!fx.store.dirty(), "保存后不再是脏的");
      Check(!FileExists(path + ".tmp"), "临时文件已改名，没有残留");
    }
    {
      // 「重启」：新进程读回
      g_button2 = Tune();
      Fixture fx;
      fx.store.SetPath(path);
      std::string error;
      Check(fx.store.Load(&error), "重启后读回成功");
      Check(fx.store.Count() >= 2, "预设数量被保留");
      fx.store.ApplyPreset("落盘测试");
      Check(std::fabs(g_button2.lensHeightDp - 33.0f) < 1e-6f, "保存后重启可以恢复（验收项 8）");
    }
    {
      // 文件被写坏：必须报错，且**不能**把现场改坏
      const std::string text = "{ oops";
      std::string error;
      Check(SaveTextAtomic(path + ".corrupt", text, &error), "写一份坏文件");
      Check(FileExists(path + ".corrupt"), "坏文件存在");
      Fixture fx;
      fx.store.SetPath(path + ".corrupt");
      const float before = g_button2.lensHeightDp;
      Check(!fx.store.Load(&error), "读到坏文件时失败");
      Check(!error.empty(), "失败原因非空");
      Check(std::fabs(g_button2.lensHeightDp - before) < 1e-6f, "失败没有污染当前参数");
      RemoveFile(path + ".corrupt");
    }
    RemoveFile(path);
  }

  // ---- 组 6：多预设管理 ---------------------------------------------------
  {
    Fixture fx;
    fx.registry.SetF("secondary_window.pill_button_1", "blurDp", 1.0f);
    fx.store.CaptureAs("预设A");
    fx.registry.SetF("secondary_window.pill_button_1", "blurDp", 5.0f);
    fx.store.CaptureAs("预设B");
    Check(fx.store.Count() == 3, "三个预设（默认 + A + B）");
    Check(fx.store.IndexOf("预设B") == 2, "按名字定位");
    Check(!fx.store.CaptureAs(""), "空名字被拒绝");
    Check(fx.store.Rename(2, "预设C"), "重命名成功");
    Check(fx.store.ActiveName() == "预设C", "重命名同步了激活名");
    Check(!fx.store.Rename(1, "预设C"), "与别的预设重名被拒绝");
    Check(fx.store.Rename(1, "预设A"), "改成自己的名字是无操作、不算冲突");
    Check(fx.store.Remove(0), "删除预设");
    Check(fx.store.Count() == 2, "删除生效");
    Check(!fx.store.Remove(0) || fx.store.Count() == 1, "至少保留一个预设");
  }

  // ---- 组 7：JSON 基础设施 ------------------------------------------------
  {
    std::string error;
    Check(JsonValue::Parse("", &error).IsNull() && !error.empty(), "空文本不是合法 JSON");
    error.clear();
    Check(JsonValue::Parse("[1,2,]", &error).IsNull(), "尾随逗号被拒绝");
    Check(error.find("行") != std::string::npos, "错误信息带行列位置");
    error.clear();
    const JsonValue ok = JsonValue::Parse(R"({"a":[1,2.5,-3e2,true,false,null,"x\ny"]})", &error);
    Check(error.empty(), "合法 JSON 解析成功");
    Check(ok["a"].size() == 7, "数组长度正确");
    Check(ok["a"].At(1).AsFloat() == 2.5f, "小数解析正确");
    Check(ok["a"].At(2).AsFloat() == -300.0f, "科学计数法解析正确");
    Check(ok["a"].At(6).AsString() == "x\ny", "转义解析正确");
    const std::string dumped = ok.Dump(2);
    error.clear();
    Check(!JsonValue::Parse(dumped, &error).IsNull() && error.empty(), "序列化结果可再解析");
    Check(JsonValue::Str("含\"引号\"与\\反斜杠\n换行").Dump(0).find("\\\"") != std::string::npos,
          "序列化会转义引号");
  }

  std::printf("GlassPresetTest: %d 项断言通过（7 组）\n", g_checks);
  return 0;
}