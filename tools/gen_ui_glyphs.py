#!/usr/bin/env python3
"""扫描源码里 UI 实际会画出来的字符，生成 UiGlyphSet.h。

为什么需要它（真机踩到的现象）：
  GlesBackend 建字体图集时只申请了 `GetGlyphRangesChineseSimplifiedCommon()`
  = 基本拉丁 + 半角 + 假名 + 2500 个常用简体汉字。
  **任何不在这个范围内的字符，图集里就没有字形，ImGui 只能画占位符（看起来就是问号）**
  —— 而"不在范围内"跟"生僻"是两件事：`●`(U+25CF) `★`(U+2605) `→`(U+2192)
  这些一点都不生僻，只是**没人申请**（实测字体文件里明明有这些字形）。

本脚本做三件事：
  1. 扫源码里的非 ASCII 字符（只扫我们会画的那些源文件，不碰 imgui 的演示串）；
  2. 用 fontTools 对照内嵌字体，**分开报告**「图集缺 → 加进生成表就能修」
     与「字体本身没有 → 必须换字符或合并符号字体」；
  3. 生成 `UiGlyphSet.h`（去重排序的码点数组），供 GlesBackend 喂给
     ImFontGlyphRangesBuilder。

用法：python tools/gen_ui_glyphs.py [--check]
      --check 只报告不写文件（CI/回归用）
"""
import argparse
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_H = os.path.join(ROOT, "app/src/main/cpp/third_party/liquidglass/font.h")
OUT_H = os.path.join(ROOT, "app/src/main/cpp/UiGlyphSet.h")

# 扫这些目录/后缀：都是"我们会画出来"的字符串来源。
SCAN_DIRS = [
    "app/src/main/cpp",
    "root-host/elf",
]
SKIP_PARTS = (
    "third_party/imgui",      # ImGui 自带的演示/断言文案，不会被画出来
    "third_party/liquidglass/font.h",   # 字体二进制本身
    "UiGlyphSet.h",           # 自己的产物
)
EXTS = (".c", ".cc", ".cpp", ".h", ".hpp", ".inl")


def iter_sources():
    for d in SCAN_DIRS:
        base = os.path.join(ROOT, d)
        for dirpath, _dirnames, filenames in os.walk(base):
            rel = os.path.relpath(dirpath, ROOT).replace("\\", "/")
            if any(p in rel for p in SKIP_PARTS):
                continue
            for fn in filenames:
                if not fn.endswith(EXTS):
                    continue
                path = os.path.join(dirpath, fn)
                if any(p in path.replace("\\", "/") for p in SKIP_PARTS):
                    continue
                yield path


def strip_comments(text):
    """去掉 // 与 /* */ 注释（够用的近似实现：正确处理字符串与转义）。

    ★ 为什么要去：注释里出现的字符**永远不会被画出来**。
      第一版把注释也算进去，于是 `▶ ▼ ✓ ✗` 这类只写在注释里的符号被当成
      "UI 缺字"报了出来 —— 判据必须对准"真的会画的东西"。
    """
    out = []
    i, n = 0, len(text)
    state = None          # None / 'line' / 'block' / 'str' / 'chr' / 'raw'
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state is None:
            if c == "/" and nxt == "/":
                state = "line"; i += 2; continue
            if c == "/" and nxt == "*":
                state = "block"; i += 2; continue
            if c == '"':
                state = "str"; out.append(c); i += 1; continue
            if c == "'":
                state = "chr"; out.append(c); i += 1; continue
            out.append(c); i += 1; continue
        if state == "line":
            if c == "\n":
                state = None; out.append(c)
            i += 1; continue
        if state == "block":
            if c == "*" and nxt == "/":
                state = None; i += 2; continue
            if c == "\n":
                out.append(c)   # 保留换行，行号不变
            i += 1; continue
        if state in ("str", "chr"):
            out.append(c)
            if c == "\\":
                if i + 1 < n:
                    out.append(text[i + 1]); i += 2; continue
            if (state == "str" and c == '"') or (state == "chr" and c == "'"):
                state = None
            i += 1; continue
    return "".join(out)


def iter_string_literals(text):
    """产出文本里所有字符串字面量的内容（含转义原样，不影响非 ASCII 判定）。"""
    text = strip_comments(text)
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
        yield m.group(1)


def collect_codepoints():
    """只收集**字符串字面量**里的非 ASCII 码点 —— 那才是会被画出来的东西。"""
    found = {}
    for path in iter_sources():
        try:
            text = open(path, encoding="utf-8", errors="ignore").read()
        except OSError:
            continue
        for lit in iter_string_literals(text):
            for ch in lit:
                cp = ord(ch)
                if cp < 0x80:
                    continue
                found.setdefault(cp, set()).add(os.path.relpath(path, ROOT).replace("\\", "/"))
    return found


def load_font_cmap():
    """从 font.h 里把 TTF 解出来，返回它的 cmap（码点集合）。"""
    src = open(FONT_H, encoding="utf-8", errors="ignore").read()
    m = re.search(r"font_v\[(\d+)/4\]", src)
    if not m:
        return None
    n = int(m.group(1))
    body = src[src.index("{"):]
    vals = re.findall(r"0x([0-9a-fA-F]{8})", body)[:n]
    data = b"".join(struct.pack("<I", int(v, 16)) for v in vals)
    tmp = os.path.join(ROOT, "build", "font_check_embedded.ttf")
    os.makedirs(os.path.dirname(tmp), exist_ok=True)
    open(tmp, "wb").write(data)
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        print("警告：没有 fontTools，无法核对字体覆盖率（只生成表）", file=sys.stderr)
        return None
    font = TTFont(tmp, lazy=True)
    return set(font.getBestCmap().keys())


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--check", action="store_true", help="只报告，不写文件")
    args = ap.parse_args()

    found = collect_codepoints()
    cmap = load_font_cmap()

    have = sorted(cp for cp in found if cmap is None or cp in cmap)
    missing = sorted(cp for cp in found if cmap is not None and cp not in cmap)

    print(f"源码里出现的非 ASCII 字符：{len(found)} 个")
    print(f"  字体里有（会被烘焙）：{len(have)} 个")
    print(f"  ★ 字体里没有（必须换字符或合并符号字体）：{len(missing)} 个")
    for cp in missing:
        who = sorted(found[cp])[:2]
        print(f"    U+{cp:04X} {chr(cp)!r}  出现在 {', '.join(who)}")

    if args.check:
        return 0 if not missing else 1

    lines = [
        "// 本文件由 tools/gen_ui_glyphs.py 生成，请勿手改。",
        "//",
        "// 内容：UI 源码里**实际会画出来**的非 ASCII 字符码点。",
        "// 为什么要它：ImGui 的字体图集只烘焙你申请的码点范围，",
        "// 没申请的字符即使字体文件里有字形，画出来也是占位符（问号）。",
        "// 真机现象：`●` `★` `→` 这类一点都不生僻的符号显示成问号 ——",
        "// 因为它们不在 GetGlyphRangesChineseSimplifiedCommon() 的范围内。",
        "#pragma once",
        "",
        "namespace aurora {",
        "",
        f"inline constexpr int kUiGlyphCount = {len(have)};",
        "",
        "// 去重、升序。GlesBackend 用它 + 常用汉字区 + 符号区一起构建图集。",
        "inline constexpr unsigned int kUiGlyphs[] = {",
    ]
    row = []
    for cp in have:
        row.append(f"0x{cp:04X}")
        if len(row) == 12:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines += ["};", "", "}  // namespace aurora", ""]

    if not args.check:
        with open(OUT_H, "w", encoding="utf-8", newline="\n") as f:
            f.write("\n".join(lines))
        print(f"已写入 {os.path.relpath(OUT_H, ROOT)}（{len(have)} 个码点）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
