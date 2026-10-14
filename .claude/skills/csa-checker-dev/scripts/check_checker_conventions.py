#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
check_checker_conventions.py — 对一条 CSA checker 的全部产物做约定 lint。

对应 csa-checker-dev SKILL.md §9 收尾自检中"可机械核对"的部分。设计动机：
五处落点中漏掉 Checkers.td / 源码 CMakeLists 会编译或运行报错、自然被纠正；
但父级 TestCase/CMakeLists.txt 的 add_subdirectory 与测试文件头四块漏掉后
完全静默（不报编译错），LLM 首轮生成的遵守率即最终遵守率——本脚本把这类
静默项变成确定性的 FAIL，制造反馈回路。

结论分两级：
  [FAIL] 违反硬性约定，计入退出码（非 0），必须修复；
  [WARN] 启发式提示 / 允许有正当理由的用法（如 sink 场景的 generateErrorNode、
         规则确需区分隐/显式 cast 的 IgnoreParenImpCasts），需人工确认。

用法：
  python3 check_checker_conventions.py --repo /usr1/BXPlugin --checker IntegerNarrowingThenWideningCastChecker
  python3 check_checker_conventions.py --repo /usr1/BXPlugin --checker IntegerNarrowingThenWideningCast \\
      --build-dir /usr1/BXPlugin/cmake-build-release-release   # 可选：联动核验 compile_commands.json

说明：
  - --checker 带/不带 Checker 后缀均可，脚本自动归一化；
  - --repo 指向 BXPlugin 根目录（也容忍直接指向 xir）；
  - 仅依赖 Python 标准库。
"""

import argparse
import json
import re
import sys
from pathlib import Path

NON_ASCII = re.compile(r"[^\x00-\x7F]")
MAX_COL = 120


# ---------------------------------------------------------------------------
# 结果收集
# ---------------------------------------------------------------------------
class Report:
    """按 section 收集 PASS / WARN / FAIL 三类结论并统一输出。"""

    def __init__(self):
        self.sections = []  # [(标题, [(级别, 文案)])]

    def section(self, title):
        self.sections.append((title, []))

    def _add(self, level, msg):
        if not self.sections:
            self.section("(未分组)")
        self.sections[-1][1].append((level, msg))

    def ok(self, msg):
        self._add("PASS", msg)

    def warn(self, msg):
        self._add("WARN", msg)

    def fail(self, msg):
        self._add("FAIL", msg)

    def dump(self):
        """打印全部结论，返回 (pass数, warn数, fail数)。"""
        counts = {"PASS": 0, "WARN": 0, "FAIL": 0}
        for title, items in self.sections:
            print("== {} ==".format(title))
            for level, msg in items:
                counts[level] += 1
                print("[{}] {}".format(level, msg))
            print()
        print("== 汇总 ==")
        print("PASS {pass} / WARN {warn} / FAIL {fail}".format(
            **{"pass": counts["PASS"], "warn": counts["WARN"], "fail": counts["FAIL"]}))
        if counts["FAIL"]:
            print("结论：存在 FAIL，按 SKILL.md §9 不得宣布完成，修复后重跑。")
        elif counts["WARN"]:
            print("结论：无 FAIL；WARN 项需人工确认理由（在 README/注释中可追溯）。")
        else:
            print("结论：机械核对全部通过；判断项（范式/包路由/四象限取舍/README 一致性）仍需自查。")
        return counts


# ---------------------------------------------------------------------------
# 轻量 C/C++ 词法切分（足以支撑"注释中文允许、字符串必须英文"的判定）
# ---------------------------------------------------------------------------
def tokenize_c(code):
    """把源码切成 (kind, text, start_line) 片段；kind ∈ {code, comment, string, char}。"""
    out = []
    i, n, line = 0, len(code), 1
    while i < n:
        c = code[i]
        start_line = line
        if c == "/" and i + 1 < n and code[i + 1] == "/":
            j = code.find("\n", i)
            j = n if j < 0 else j
            out.append(("comment", code[i:j], start_line))
            i = j
            continue
        if c == "/" and i + 1 < n and code[i + 1] == "*":
            j = code.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += code.count("\n", i, j)
            out.append(("comment", code[i:j], start_line))
            i = j
            continue
        if c == '"' or c == "'":
            quote, j = c, i + 1
            while j < n:
                if code[j] == "\\":
                    j += 2
                    continue
                if code[j] == quote:
                    j += 1
                    break
                if code[j] == "\n":  # 容错：未闭合字面量，按行截断
                    break
                j += 1
            out.append(("string" if quote == '"' else "char", code[i:j], start_line))
            i = j
            continue
        # 普通代码段：吃到下一个注释/字面量起点
        j = i
        while j < n:
            cj = code[j]
            if cj == "\n":
                line += 1
            if cj == '"' or cj == "'":
                break
            if cj == "/" and j + 1 < n and code[j + 1] in "/*":
                break
            j += 1
        out.append(("code", code[i:j], start_line))
        i = j if j > i else i + 1
    return out


def blank_non_code(code):
    """把注释与字面量内容替换为空格（保留换行），用于按行做语法启发式。"""
    pieces = []
    for kind, text, _ in tokenize_c(code):
        if kind == "code":
            pieces.append(text)
        else:
            pieces.append(re.sub(r"[^\n]", " ", text))
    return "".join(pieces)


def find_in_code(code, pattern):
    """在 code 类 token 中找 pattern，返回 [(行号, 命中串)]。"""
    hits = []
    for kind, text, start_line in tokenize_c(code):
        if kind != "code":
            continue
        for m in re.finditer(pattern, text):
            hits.append((start_line + text.count("\n", 0, m.start()), m.group(0)))
    return hits


def leading_comment_block(code):
    """提取文件起始处连续注释（/* */ 与 // 均可，允许其间空白）的合并文本。"""
    parts = []
    for kind, text, _ in tokenize_c(code):
        if kind == "comment":
            parts.append(text)
        elif kind == "code" and text.strip() == "":
            continue
        else:
            break
    return "\n".join(parts)


# ---------------------------------------------------------------------------
# 各项检查
# ---------------------------------------------------------------------------
def check_layout(xir, checker, rep):
    """五处落点：源码 / 源码 CMakeLists / Checkers.td / 用例目录+其 CMakeLists / 父级 add_subdirectory。"""
    rep.section("[落地] 五处落点（{}）".format(checker))
    src_dir = xir / "llvm_ast" / "StaticAnalyzer" / "Checkers"
    tc_root = xir / "llvm_ast" / "TestCase"
    td_path = xir / "include" / "clang" / "StaticAnalyzer" / "Checkers" / "Checkers.td"

    cpp = src_dir / "{}.cpp".format(checker)
    if cpp.is_file():
        rep.ok("① checker 源码存在：{}".format(cpp))
    else:
        rep.fail("① 未找到 checker 源码：{}".format(cpp))
        cpp = None

    src_cmake = src_dir / "CMakeLists.txt"
    if src_cmake.is_file():
        if "{}.cpp".format(checker) in src_cmake.read_text(encoding="utf-8", errors="replace"):
            rep.ok("② 源码 CMakeLists 已登记 {}.cpp".format(checker))
        else:
            rep.fail("② {} 未登记 {}.cpp（应追加到 add_library() 列表最后）".format(src_cmake, checker))
    else:
        rep.fail("② 未找到源码 CMakeLists：{}".format(src_cmake))

    if td_path.is_file():
        rep.ok("③ Checkers.td 存在：{}".format(td_path))
    else:
        rep.fail("③ 未找到 Checkers.td：{}".format(td_path))
        td_path = None

    tc_dir = tc_root / checker
    tests = []
    if tc_dir.is_dir():
        rep.ok("④ 测试目录存在：{}".format(tc_dir))
        tc_cmake = tc_dir / "CMakeLists.txt"
        if tc_cmake.is_file():
            cm = tc_cmake.read_text(encoding="utf-8", errors="replace")
            rep.ok("④ 测试目录自身 CMakeLists.txt 存在")
            if "add_library" not in cm and "file(GLOB" not in cm.replace(" ", ""):
                rep.warn("④ {} 中未见 add_library/file(GLOB)，确认其确能把用例编入构建".format(tc_cmake))
            if "CMAKE_EXPORT_COMPILE_COMMANDS" not in cm:
                rep.warn("④ {} 未设置 CMAKE_EXPORT_COMPILE_COMMANDS ON（模板默认有）".format(tc_cmake))
        else:
            rep.fail("④ 测试目录缺少自身的 CMakeLists.txt：{}".format(tc_cmake))
        tests = sorted([p for p in tc_dir.glob("*.c")] + [p for p in tc_dir.glob("*.cpp")]
                       + [p for p in (tc_dir / "src").glob("*.c") if (tc_dir / "src").is_dir()]
                       + [p for p in (tc_dir / "src").glob("*.cpp") if (tc_dir / "src").is_dir()])
        if tests:
            rep.ok("④ 找到用例源文件 {} 个：{}".format(len(tests), ", ".join(p.name for p in tests)))
        else:
            rep.fail("④ 测试目录下未找到任何用例源文件（顶层 TC_*.c[pp] 或 src/*.c[pp]）")
    else:
        rep.fail("④ 未找到测试目录：{}（每规则一子目录，大驼峰）".format(tc_dir))

    parent_cmake = tc_root / "CMakeLists.txt"
    if parent_cmake.is_file():
        text = parent_cmake.read_text(encoding="utf-8", errors="replace")
        if re.search(r'add_subdirectory\(\s*"?' + re.escape(checker) + r'"?\s*\)', text):
            rep.ok("⑤ 父级 TestCase/CMakeLists.txt 已追加 add_subdirectory({})".format(checker))
        else:
            rep.fail("⑤ 父级 {} 缺少 add_subdirectory({})——此项不报编译错，最易静默遗漏".format(parent_cmake, checker))
    else:
        rep.fail("⑤ 未找到父级 TestCase/CMakeLists.txt：{}".format(parent_cmake))

    return cpp, td_path, tests


def check_cpp(cpp, checker, rep):
    """源码约定：注册头、注册函数后缀、报告 API 与 cast 用法告警、字符串英文、Tab/列宽、单语句大括号。"""
    rep.section("[源码] {}.cpp".format(checker))
    text = cpp.read_text(encoding="utf-8", errors="replace")

    if re.search(r'#include\s*"clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration\.h"', text):
        rep.ok('包含 "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"')
    else:
        rep.fail('缺少 #include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"')

    for fn in ("register{}".format(checker), "shouldRegister{}".format(checker)):
        if re.search(r"\b" + re.escape(fn) + r"\s*\(", text):
            rep.ok("注册函数 {}() 存在（带 Checker 后缀）".format(fn))
        else:
            rep.fail("未找到注册函数 {}()（函数名须与 td def 名一致、带 Checker 后缀）".format(fn))

    hits = find_in_code(text, r"\bgenerateErrorNode\b")
    if hits:
        rep.warn("使用了 generateErrorNode（{}）——仅当确需 sink 中止路径才允许，"
                 "否则改用 generateNonFatalErrorNode，避免同路径多 issue 被截断"
                 .format(", ".join("L{}".format(l) for l, _ in hits)))
    else:
        rep.ok("未使用 generateErrorNode（默认 generateNonFatalErrorNode 路线）")

    hits = find_in_code(text, r"\bIgnoreParenImpCasts\b")
    if hits:
        rep.warn("使用了 IgnoreParenImpCasts（{}）——判类型剥括号默认应用 IgnoreParenCasts，"
                 "仅当规则确需区分隐/显式 cast 才允许".format(", ".join("L{}".format(l) for l, _ in hits)))
    else:
        rep.ok("未使用 IgnoreParenImpCasts（判类型默认 IgnoreParenCasts 路线）")

    bad_literals = [(ln, t) for k, t, ln in tokenize_c(text)
                    if k in ("string", "char") and NON_ASCII.search(t)]
    if bad_literals:
        for ln, t in bad_literals[:5]:
            rep.fail("字符串/字符字面量含非 ASCII（诊断文案须英文）：L{} {}".format(
                ln, t if len(t) <= 60 else t[:57] + "..."))
        if len(bad_literals) > 5:
            rep.fail("……另有 {} 处非 ASCII 字面量".format(len(bad_literals) - 5))
    else:
        rep.ok("全部字符串字面量为 ASCII（中文注释不在此限）")

    _check_format(text, cpp.name, rep)


def _check_format(text, name, rep):
    """Tab / 列宽 / 单语句控制流大括号（后者为启发式 WARN）。"""
    lines = text.splitlines()
    tab_lines = [i + 1 for i, l in enumerate(lines) if "\t" in l]
    if tab_lines:
        rep.fail("{}: 含 Tab（UseTab: Never），行 {}{}".format(
            name, ", ".join(map(str, tab_lines[:8])), " ..." if len(tab_lines) > 8 else ""))
    else:
        rep.ok("{}: 无 Tab".format(name))

    blanked_lines = blank_non_code(text).splitlines()
    long_lines = [i + 1 for i, l in enumerate(lines)
                  if len(l) > MAX_COL and i < len(blanked_lines) and blanked_lines[i].strip()]
    if long_lines:
        rep.warn("{}: {} 个含代码的行超过 {} 列（行 {}{}）——请用项目 .clang-format 执行 clang-format -i（纯注释行如头块运行命令不计）".format(
            name, len(long_lines), MAX_COL,
            ", ".join(map(str, long_lines[:8])), " ..." if len(long_lines) > 8 else ""))
    else:
        rep.ok("{}: 含代码的行列宽 ≤ {}".format(name, MAX_COL))

    blanked = blanked_lines
    suspects = []
    for i, raw in enumerate(blanked):
        line = raw.strip()
        cond = re.match(r"^(if|for|while)\s*\(.*\)\s*$", line)
        bare_else = (line == "else")
        if not cond and not bare_else:
            continue
        if cond and line.count("(") != line.count(")"):
            continue  # 条件跨行，启发式放过
        nxt = ""
        for j in range(i + 1, len(blanked)):
            if blanked[j].strip():
                nxt = blanked[j].strip()
                break
        if nxt and not nxt.startswith("{") and not nxt.startswith("}") \
                and not re.match(r"^(if|else)\b", nxt):
            suspects.append(i + 1)
    if suspects:
        rep.warn("{}: 疑似单语句控制流未带 {{}}（启发式，可能误报）：行 {}".format(
            name, ", ".join(map(str, suspects[:8]))))
    else:
        rep.ok("{}: 未发现单语句控制流缺 {{}}（启发式）".format(name))


def check_td(td_path, checker, base, rep):
    """Checkers.td：def 名带后缀、Checker<\"短名\"> 不带后缀、条目纯英文、包路由就近推断。"""
    rep.section("[td] Checkers.td")
    text = td_path.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"\bdef\s+" + re.escape(checker) + r"\b.*?;", text, re.S)
    if not m:
        rep.fail("未找到 def {}（def 名须带 Checker 后缀，与 C++ 类名一致）".format(checker))
        if re.search(r"\bdef\s+" + re.escape(base) + r"\b", text):
            rep.fail("检测到 def {}（缺 Checker 后缀）——def 名与运行短名写反了".format(base))
        return
    rep.ok("找到 def {}".format(checker))
    block = m.group(0)

    if 'Checker<"{}">'.format(base) in block:
        rep.ok('运行短名正确：Checker<"{}">（不带 Checker 后缀）'.format(base))
    elif 'Checker<"{}">'.format(checker) in block:
        rep.fail('Checker<"{}"> 误带 Checker 后缀，应为 Checker<"{}">'.format(checker, base))
    else:
        rep.fail('def 块中未找到 Checker<"{}">'.format(base))

    if NON_ASCII.search(block):
        rep.fail("td 条目含非 ASCII 字符（HelpText 等须英文）")
    else:
        rep.ok("td 条目为纯英文")

    pkgs = re.findall(r"let\s+ParentPackage\s*=\s*(\w+)", text[: m.start()])
    if pkgs:
        rep.ok("按就近 let ParentPackage 推断：位于 {} 包（core/ipa 路由是否正确属判断项，自查）".format(pkgs[-1]))
    else:
        rep.warn("def 之前未见 let ParentPackage —— 确认条目放进了 RosaRBEnhanceCore/RosaRBEnhanceIpa 包块内")


def check_test_header(path, base, rep):
    """测试文件头四块：①作者/日期/描述 ② bxen 运行命令 ③ DTS issue ④ Cases 列表。"""
    text = path.read_text(encoding="utf-8", errors="replace")
    head = leading_comment_block(text)
    if not head.strip():
        rep.fail("{}: 缺少文件头注释（四块全无）——应从 templates/test_case.c 起步".format(path.name))
        _check_format(text, path.name, rep)
        return

    # 块①：作者 / 日期 / 描述
    m = re.search(r"Author\s*:\s*(.+)", head)
    if not m or not m.group(1).strip():
        rep.fail("{}: 头块缺 Author（git user.name，取不到回退 g00586360）".format(path.name))
    elif "<" in m.group(1):
        rep.fail("{}: Author 仍是模板占位「{}」，未替换".format(path.name, m.group(1).strip()))
    else:
        rep.ok("{}: Author = {}".format(path.name, m.group(1).strip()))
    m = re.search(r"Date\s*:\s*(.+)", head)
    if not m or not m.group(1).strip():
        rep.fail("{}: 头块缺 Date".format(path.name))
    elif not re.search(r"\d{4}-\d{2}-\d{2}", m.group(1)):
        rep.warn("{}: Date「{}」不是 YYYY-MM-DD".format(path.name, m.group(1).strip()))
    else:
        rep.ok("{}: Date = {}".format(path.name, m.group(1).strip()))
    if re.search(r"Desc\s*:\s*\S", head):
        rep.ok("{}: Desc 已填写".format(path.name))
    else:
        rep.fail("{}: 头块缺 Desc".format(path.name))

    # 块②：bxen 运行命令
    if "bxen" in head:
        rep.ok("{}: 头块含 bxen 运行命令".format(path.name))
        if "-analyzer-max-loop 3" in head:
            rep.ok("{}: 运行命令含 -analyzer-max-loop 3".format(path.name))
        else:
            rep.fail("{}: 运行命令缺 -analyzer-max-loop 3（core/ipa 统一 3）".format(path.name))
        if "-cc_json=" not in head:
            rep.warn("{}: 运行命令未见 -cc_json=<构建目录>".format(path.name))
        cm = re.search(r"-checker=([\w.]+)", head)
        if cm:
            short = cm.group(1).split(".")[-1]
            if short.endswith("Checker"):
                rep.fail("{}: -checker={} 误用带后缀名，应为运行短名（Checker<\"...\"> 字符串）"
                         .format(path.name, cm.group(1)))
            elif short != base:
                rep.warn("{}: -checker 短名「{}」与本规则「{}」不一致，确认是否笔误"
                         .format(path.name, short, base))
            else:
                rep.ok("{}: -checker 短名正确（{}）".format(path.name, cm.group(1)))
            if not re.match(r"rb\.enhance\.(core|ipa)\.", cm.group(1)):
                rep.warn("{}: -checker 未带 rb.enhance.core/ipa 包前缀".format(path.name))
        else:
            rep.fail("{}: 运行命令缺 -checker=".format(path.name))
    else:
        rep.fail("{}: 头块缺 bxen 运行命令".format(path.name))
    if "-Xanalyzer" in head or "clang --analyze" in head:
        rep.fail("{}: 出现 clang --analyze / -Xanalyzer 形式——一律用 bxen".format(path.name))

    # 块③：issue 链接
    if re.search(r"Issue\s*:.*DTS\w+", head):
        rep.ok("{}: Issue 行含 DTS 单号/占位".format(path.name))
    else:
        rep.fail("{}: 头块缺 Issue 行（占位写 DTSXXXX / https:xxxx 也必须有）".format(path.name))

    # 块④：case 列表与四象限
    if re.search(r"Cases\s*:", head):
        rep.ok("{}: 头块含 Cases 列表".format(path.name))
        missing = [q for q in ("TP", "TN", "FP", "FN") if not re.search(r"\b{}\d*\b".format(q), head)]
        if missing:
            rep.warn("{}: Cases 未覆盖象限 {}（默认四象限齐全；确有理由须在 README 说明）"
                     .format(path.name, "/".join(missing)))
        else:
            rep.ok("{}: 四象限 TP/TN/FP/FN 均已列出".format(path.name))
    else:
        rep.fail("{}: 头块缺 Cases 列表".format(path.name))

    _check_format(text, path.name, rep)


def check_compile_commands(build_dir, tests, rep):
    """可选：核验 compile_commands.json 是否包含用例 TU（add_subdirectory 漏登记的运行期表现）。"""
    rep.section("[构建] compile_commands.json（--build-dir）")
    cc = Path(build_dir) / "compile_commands.json"
    if not cc.is_file():
        rep.fail("未找到 {}（确认构建目录、且 CMAKE_EXPORT_COMPILE_COMMANDS=ON）".format(cc))
        return
    try:
        entries = json.loads(cc.read_text(encoding="utf-8", errors="replace"))
    except ValueError as exc:
        rep.fail("解析 {} 失败：{}".format(cc, exc))
        return
    files = {Path(e.get("file", "")).name for e in entries}
    for t in tests:
        if t.name in files:
            rep.ok("{} 在 compile_commands.json 中".format(t.name))
        else:
            rep.fail("{} 不在 compile_commands.json 中——通常是父级 add_subdirectory 漏登记或未重新 cmake/make".format(t.name))


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="CSA checker 产物约定 lint（csa-checker-dev §9 机械核对部分）")
    parser.add_argument("--repo", required=True, help="BXPlugin 根目录（或 xir 目录）")
    parser.add_argument("--checker", required=True, help="规则名，带/不带 Checker 后缀均可")
    parser.add_argument("--build-dir", default=None, help="可选：构建目录，联动核验 compile_commands.json")
    args = parser.parse_args()

    name = args.checker.strip()
    if name.endswith("Checker") and len(name) > len("Checker"):
        checker, base = name, name[: -len("Checker")]
    else:
        checker, base = name + "Checker", name

    repo = Path(args.repo).resolve()
    if (repo / "xir").is_dir():
        xir = repo / "xir"
    elif (repo / "llvm_ast").is_dir():
        xir = repo
    else:
        print("[FAIL] --repo 下未找到 xir/ 或 llvm_ast/：{}".format(repo))
        return 2

    print("checker = {}（运行短名 {}）  xir = {}\n".format(checker, base, xir))
    rep = Report()
    cpp, td_path, tests = check_layout(xir, checker, rep)
    if cpp:
        check_cpp(cpp, checker, rep)
    if td_path:
        check_td(td_path, checker, base, rep)
    if tests:
        rep.section("[测试] 文件头四块与格式")
        for t in tests:
            check_test_header(t, base, rep)
    if args.build_dir:
        check_compile_commands(args.build_dir, tests, rep)

    counts = rep.dump()
    return 1 if counts["FAIL"] else 0


if __name__ == "__main__":
    sys.exit(main())
