#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
对 csa-checker-dev Skill 目录做内容感知的轻量校验（不依赖外部 Python 包）：

1. SKILL.md 存在，且以 YAML frontmatter 起始。
2. frontmatter 的 name / description 字段存在且非空，name 与目录名一致。
3. 所有声明的 references / templates / scripts 文件存在。
4. SKILL.md 正文中引用到的 references/ 与 templates/ 路径，均能在磁盘上找到
   （捕捉"文中提到但忘记创建"的情况）。

退出码 0 表示通过，非 0 表示失败。
"""

from pathlib import Path
import re
import sys


# 期望随 Skill 一起分发的文件（相对 Skill 根目录）。
EXPECTED_FILES = [
    "SKILL.md",
    "references/project-context.md",
    "references/checker-workflow.md",
    "references/coding-style.md",
    "references/ast-visitor-checker.md",
    "references/path-sensitive-checker.md",
    "references/csa-api-cookbook.md",
    "references/registration-build-run.md",
    "references/test-and-readme.md",
    "references/libtooling-prescan-appendix.md",
    "templates/ast_visitor_checker.cpp",
    "templates/path_sensitive_checker.cpp",
    "templates/checkers_td_entry.td",
    "templates/test_case.c",
    "templates/test_cmakelists.txt",
    "templates/rule_readme.md",
    "templates/git_commit_message.md",
    "scripts/check_checker_conventions.py",
    "scripts/validate_skill.py",
]


def fail(message):
    """打印失败原因并返回非 0 退出码。"""
    print(f"[FAIL] {message}")
    return 1


def parse_frontmatter(text):
    """提取 SKILL.md 顶部 --- ... --- 之间的 frontmatter 原文；失败返回 None。"""
    match = re.match(r"^---\n(.*?)\n---\n", text, re.S)
    if match is None:
        return None
    return match.group(1)


def field_nonempty(frontmatter, key):
    """判断 frontmatter 中 key 是否存在且取值非空（支持单行与 > 折叠块）。"""
    pattern = rf"^{key}\s*:\s*(.*)$"
    m = re.search(pattern, frontmatter, re.M)
    if m is None:
        return False
    inline = m.group(1).strip()
    if inline and inline not in (">", "|", ">-", "|-"):
        return True
    # 折叠/块标量：检查后续缩进行是否有非空内容。
    tail = frontmatter[m.end():]
    for line in tail.splitlines():
        if line.strip() == "":
            continue
        if line.startswith((" ", "\t")):
            return True
        break
    return False


def main():
    root = Path(__file__).resolve().parents[1]

    # 1+3. 期望文件存在性。
    missing = [rel for rel in EXPECTED_FILES if not (root / rel).exists()]
    if missing:
        print("[FAIL] 缺失文件：")
        for rel in missing:
            print(f"  - {rel}")
        return 1

    skill_text = (root / "SKILL.md").read_text(encoding="utf-8")

    # 1. frontmatter 起始。
    frontmatter = parse_frontmatter(skill_text)
    if frontmatter is None:
        return fail("SKILL.md 必须以 YAML frontmatter（--- ... ---）开始。")

    # 2. name / description 非空。
    if not field_nonempty(frontmatter, "name"):
        return fail("frontmatter 缺少非空的 name 字段。")
    if not field_nonempty(frontmatter, "description"):
        return fail("frontmatter 缺少非空的 description 字段。")

    name_match = re.search(r"^name\s*:\s*(.+)$", frontmatter, re.M)
    name_value = name_match.group(1).strip()
    if name_value != root.name:
        return fail(f"frontmatter name（{name_value}）应与目录名（{root.name}）一致。")

    # 4. 正文引用到的 references/ 与 templates/ 路径都应存在。
    referenced = set(re.findall(r"(?:references|templates)/[A-Za-z0-9_./-]+", skill_text))
    dangling = sorted(rel for rel in referenced if not (root / rel).exists())
    if dangling:
        print("[FAIL] SKILL.md 引用了不存在的文件：")
        for rel in dangling:
            print(f"  - {rel}")
        return 1

    print("[OK] Skill package validation passed.")
    print(f"     name={name_value}, files={len(EXPECTED_FILES)}, referenced_paths={len(referenced)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
