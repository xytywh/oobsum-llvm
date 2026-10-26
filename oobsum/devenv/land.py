#!/usr/bin/env python3
"""把一个 checker 按"五处落点"落到目标源码树里（或撤掉）。默认只演示（打印要做的事和 diff），加 --apply 才写。

两种来源（--drop 指向哪个）：
  * 包目录（含 drop.manifest）：文件都在这个目录里（例：oobsum/probes/i0-integration）
  * 仓库内清单（*.manifest）：文件就在本仓库的 llvm-project 源码树里（例：oobsum/landing/OobSummaryChecker.manifest）。
    这是 v4.0 起的主路径：代码在分支上开发，公司 clone / pull 后用它把当前版本同步进 BXPlugin。

两个目标（--into）：
  bxplugin  公司的 BXPlugin（csa-checker-dev 的五处落点）：
              源码  xir/llvm_ast/StaticAnalyzer/Checkers/          （CMakeLists.txt 的 add_library() 末尾追加）
              声明  xir/include/clang/StaticAnalyzer/Checkers/Checkers.td（对应包块的最后）
              用例  xir/llvm_ast/TestCase/<Name>/                  （TestCase/CMakeLists.txt 最后追加 add_subdirectory）
  llvm      本地 llvm-project（只在"独立的 llvm-project-14 + 包目录"时用来预演；本仓库里 checker 已经在树内）

包目录的 drop.manifest：
    name=OobSummaryProbeChecker   package=core   td=checkers_td_snippet.td
    src=OobSummaryProbeChecker.cpp   src=OobSummary/ProbeSubdirUnit.cpp   hdr=OobSummary/ProbeSubdirUnit.h
    testcase=TestCase/OobSummaryProbeChecker
仓库内清单（路径相对仓库根；src / hdr 可用通配符，** 表示任意层子目录）：
    name=OobSummaryChecker   package=ipa
    td_from=clang/include/clang/StaticAnalyzer/Checkers/Checkers.td   td_def=OobSummaryChecker
    src_root=clang/lib/StaticAnalyzer/Checkers
    src=OobSummaryChecker.cpp   src=OobSummary/**/*.cpp   hdr=OobSummary/**/*.h
    testcase=oobsum/testcase/OobSummaryChecker

同步语义（仓库内清单落进 BXPlugin 时）：
  * 子目录（如 OobSummary/）由清单"拥有"：目标里多出来的文件删掉，CMakeLists 里多出来的登记去掉；
  * Checkers.td 里已有同名 def 但内容不同 → 整段替换；用例目录内容不同 → 整个替换。
  重复执行安全：已经一致的项跳过。找不到预期结构（包块结尾、add_library）就停下，不猜。

另有 --check-tree：检查仓库内清单与 clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt、Checkers.td 是否一致（加了源文件
却忘了登记时报错）。CLAUDE.md 要求每次提交前跑。

用法：
    python3 land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin            # 演示
    python3 land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --apply    # 落地 / 同步
    python3 land.py --drop oobsum/landing/OobSummaryChecker.manifest --into bxplugin --root /usr1/BXPlugin --remove --apply
    python3 land.py --drop oobsum/landing/OobSummaryChecker.manifest --check-tree
只依赖 Python 3 标准库。
"""
import argparse
import difflib
import filecmp
import fnmatch
import os
import re
import shutil
import sys

LAYOUT = {
    'bxplugin': {'checkers': 'xir/llvm_ast/StaticAnalyzer/Checkers',
                 'td': 'xir/include/clang/StaticAnalyzer/Checkers/Checkers.td',
                 'testcase': 'xir/llvm_ast/TestCase'},
    'llvm': {'checkers': 'clang/lib/StaticAnalyzer/Checkers',
             'td': 'clang/include/clang/StaticAnalyzer/Checkers/Checkers.td',
             'testcase': None},
}
PKG = {'core': ('RosaRBEnhanceCore', 'rb.enhance.core'), 'ipa': ('RosaRBEnhanceIpa', 'rb.enhance.ipa')}
# 上游 llvm-project 里没有 rb.enhance.*：本地预演时补上（只在 --into llvm 时用；公司的 Checkers.td 本来就有）
LLVM_PACKAGES = '''
//===----------------------------------------------------------------------===//
// rb.enhance.* packages: local rehearsal of the BXPlugin/xir layout (not upstream).
//===----------------------------------------------------------------------===//

def RosaRB : Package<"rb">;
def RosaRBEnhance : Package<"enhance">, ParentPackage<RosaRB>;
def RosaRBEnhanceCore : Package<"core">, ParentPackage<RosaRBEnhance>;
def RosaRBEnhanceIpa : Package<"ipa">, ParentPackage<RosaRBEnhance>;

let ParentPackage = RosaRBEnhanceCore in {

} // end "rb.enhance.core"

let ParentPackage = RosaRBEnhanceIpa in {

} // end "rb.enhance.ipa"
'''
CJK = re.compile(r'[　-〿一-鿿＀-￯]')


class Plan(object):
    def __init__(self):
        self.edits = {}      # 路径 → (旧内容, 新内容)
        self.copies = []     # (源, 目的)
        self.deletes = []    # 路径（文件或目录）
        self.notes = []

    def edit(self, path, new):
        old = self.edits[path][0] if path in self.edits else (open(path, encoding='utf-8').read() if os.path.exists(path) else '')
        if new != old:
            self.edits[path] = (old, new)
        elif path in self.edits:
            del self.edits[path]

    def current(self, path):
        if path in self.edits:
            return self.edits[path][1]
        return open(path, encoding='utf-8').read() if os.path.exists(path) else ''


# ---------------------------------------------------------------- 读清单
def find_repo_root(start):
    d = os.path.abspath(start)
    while d != os.path.dirname(d):
        # 完整检出有 clang/ 与 llvm/；公司那边稀疏检出时只有 clang/ 的几个文件与 oobsum/
        if os.path.isdir(os.path.join(d, 'clang')) and (os.path.isdir(os.path.join(d, 'llvm')) or os.path.isdir(os.path.join(d, 'oobsum'))):
            return d
        d = os.path.dirname(d)
    sys.exit('从 %s 往上找不到仓库根目录（应有 clang/，以及 llvm/ 或 oobsum/）' % start)


def expand(base, patterns):
    """通配符展开（** = 任意层子目录），结果是相对 base 的路径，排好序。"""
    out = []
    allfiles = None
    for p in patterns:
        if any(c in p for c in '*?['):
            if allfiles is None:
                allfiles = []
                for root, dirs, files in os.walk(base):
                    dirs[:] = [x for x in dirs if x != '__pycache__']
                    for f in files:
                        allfiles.append(os.path.relpath(os.path.join(root, f), base))
            rx = re.compile('^' + re.escape(p).replace(r'\*\*/', '(?:.*/)?').replace(r'\*', '[^/]*').replace(r'\?', '[^/]') + '$')
            hits = sorted(f for f in allfiles if rx.match(f))
            out += [h for h in hits if h not in out]
        elif p not in out:
            out.append(p)
    return out


def read_manifest(path):
    """返回统一的描述：name、package、files[(相对 Checkers/ 的路径, 源绝对路径)]、srcs、td_snippet、testcase（源目录）、mode。"""
    path = os.path.abspath(path)
    if os.path.isdir(path):
        drop, mf = path, os.path.join(path, 'drop.manifest')
        mode = 'drop'
    else:
        drop, mf = os.path.dirname(path), path
        mode = 'tree'
    raw = {'src': [], 'hdr': []}
    for line in open(mf, encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        k, v = [x.strip() for x in line.split('=', 1)]
        if k in ('src', 'hdr'):
            raw[k].append(v)
        else:
            raw[k] = v
    for k in ('name', 'package'):
        if k not in raw:
            sys.exit('%s 缺 %s' % (mf, k))
    if raw['package'] not in PKG:
        sys.exit('package 只能是 core 或 ipa')
    m = {'name': raw['name'], 'package': raw['package'], 'mode': mode, 'manifest': mf}
    if mode == 'drop':
        base = drop
        m['td_snippet'] = td_snippet_from_file(os.path.join(drop, raw['td']))
        m['testcase'] = os.path.join(drop, raw['testcase']) if raw.get('testcase') else None
    else:
        root = find_repo_root(drop)
        m['repo'] = root
        base = os.path.join(root, raw['src_root'])
        m['td_from'] = os.path.join(root, raw['td_from'])
        m['td_def'] = raw['td_def']
        m['td_snippet'] = td_def_block(open(m['td_from'], encoding='utf-8').read(), raw['td_def'])
        if m['td_snippet'] is None:
            sys.exit('%s 里找不到 def %s' % (raw['td_from'], raw['td_def']))
        m['td_snippet'] = m['td_snippet'].rstrip() + '\n'
        m['testcase'] = os.path.join(root, raw['testcase']) if raw.get('testcase') else None
    srcs = expand(base, raw['src'])
    hdrs = expand(base, raw['hdr'])
    for f in srcs + hdrs:
        if not os.path.isfile(os.path.join(base, f)):
            sys.exit('清单里的文件不存在：%s' % os.path.join(base, f))
    m['srcs'] = [f for f in srcs if f.endswith(('.cpp', '.cc', '.c'))]
    m['files'] = [(f, os.path.join(base, f)) for f in srcs + [h for h in hdrs if h not in srcs]]
    # 由清单"拥有"的子目录（同步时清理多余文件）；顶层单个文件不算
    m['owned_dirs'] = sorted({f.split('/', 1)[0] for f, _ in m['files'] if '/' in f}) if mode == 'tree' else []
    if CJK.search(m['td_snippet']):
        sys.exit('%s 的 Checkers.td 条目含中文：Checkers.td 只放英文' % m['name'])
    return m


def td_snippet_from_file(path):
    lines = open(path, encoding='utf-8').read().splitlines()
    i = next((k for k, l in enumerate(lines) if l.startswith('def ')), None)
    if i is None:
        sys.exit('%s 里没有 def' % path)
    return '\n'.join(lines[i:]).rstrip() + '\n'


DEF_RX = r'^def %s\s*:.*?;[ \t]*$'


def td_def_block(text, name):
    """def <name> : ... ; 整段（到第一个以 ; 结尾的行）。"""
    mt = re.search(DEF_RX % re.escape(name), text, re.S | re.M)
    return mt.group(0) if mt else None


def def_name(snippet):
    return re.match(r'def\s+(\w+)', snippet).group(1)


# ---------------------------------------------------------------- CMakeLists
def cmake_block(lines, into):
    head = 'add_clang_library(' if into == 'llvm' else 'add_library('
    start = next((i for i, l in enumerate(lines) if l.lstrip().startswith(head)), None)
    if start is None:
        return None, None, head
    depth = 0
    for i in range(start, len(lines)):
        depth += lines[i].count('(') - lines[i].count(')')
        if depth <= 0:
            return start, i, head
    return start, None, head


def cmake_entries(text, into):
    lines = text.splitlines(keepends=True)
    start, end, head = cmake_block(lines, into)
    if start is None or end is None:
        return None
    return re.findall(r'[\w./+-]+\.(?:cpp|cc|c)\b', ''.join(lines[start:end + 1]))


def cmake_include(text):
    """bxplugin 的 Checkers/CMakeLists.txt：源码里 #include "OobSummary/core/…" 要能从 Checkers/ 找到——
    target_include_directories(<add_library 的目标> …) 里补上 ${CMAKE_CURRENT_SOURCE_DIR}；没有这一行就在 add_library 之后加一行（SPEC-0114）。
    返回 (新文本, 改了没有)。"""
    want = '${CMAKE_CURRENT_SOURCE_DIR}'
    m = re.search(r'add_library\(\s*([\w.+-]+)', text)
    if m is None:
        return text, False
    target = m.group(1)
    inc = re.search(r'target_include_directories\(\s*%s\b[^)]*\)' % re.escape(target), text)
    if inc:
        if want in inc.group(0):
            return text, False
        call = inc.group(0)[:-1].rstrip() + ' ' + want + ')'
        return text[:inc.start()] + call + text[inc.end():], True
    lines = text.splitlines(keepends=True)
    start, end, _ = cmake_block(lines, 'bxplugin')
    if end is None:
        return text, False
    lines.insert(end + 1, 'target_include_directories(%s PRIVATE %s)\n' % (target, want))
    return ''.join(lines), True


def cmake_add(text, srcs, into):
    """在 add_library / add_clang_library 的源文件列表末尾追加（保持相邻行的缩进），已有的跳过。"""
    lines = text.splitlines(keepends=True)
    start, end, head = cmake_block(lines, into)
    if start is None:
        sys.exit('CMakeLists.txt 里找不到 %s——结构与预期不同，请手工登记：%s' % (head, ', '.join(srcs)))
    if end is None:
        sys.exit('CMakeLists.txt 的 %s 没有闭合' % head)
    have = set(re.findall(r'[\w./+-]+\.(?:cpp|cc|c)\b', ''.join(lines[start:end + 1])))
    todo = [s for s in srcs if s not in have]
    if not todo:
        return text, []
    # 最后一个源文件行之后插入（LLVM 的列表后面还有 LINK_LIBS / DEPENDS，不能插在闭括号前）
    last = max(i for i in range(start, end + 1) if re.search(r'\.(?:cpp|cc|c)\b', lines[i]))
    mt = re.match(r'(\s*)', lines[last])
    indent = mt.group(1) if lines[last].strip() and not lines[last].lstrip().startswith(head) else '    '
    if lines[last].rstrip().endswith(')') and last == end:
        lines[last] = lines[last].rstrip()[:-1].rstrip() + '\n'
        new = [indent + s + '\n' for s in todo]
        new[-1] = new[-1].rstrip('\n') + ')\n'
    else:
        new = [indent + s + '\n' for s in todo]
    lines[last + 1:last + 1] = new
    return ''.join(lines), todo


def cmake_remove(text, srcs):
    out, gone = [], []
    for l in text.splitlines(keepends=True):
        s = l.strip().rstrip(')').strip()
        if s in srcs:
            gone.append(s)
            if l.rstrip().endswith(')') and out:
                out[-1] = out[-1].rstrip('\n') + ')\n'
            continue
        out.append(l)
    return ''.join(out), gone


def td_end_marker(pkg_name):
    return re.compile(r'^\}\s*//\s*end\s*"?%s"?\s*$' % re.escape(pkg_name), re.M)


def dir_differs(a, b):
    cmp = filecmp.dircmp(a, b)
    if cmp.left_only or cmp.right_only or cmp.diff_files or cmp.funny_files:
        return True
    _, mismatch, errors = filecmp.cmpfiles(a, b, cmp.common_files, shallow=False)
    if mismatch or errors:
        return True
    return any(dir_differs(os.path.join(a, d), os.path.join(b, d)) for d in cmp.common_dirs)


# ---------------------------------------------------------------- 计划
def plan_land(args, m, plan):
    root, lay = args.root, LAYOUT[args.into]
    ck = os.path.join(root, lay['checkers'])
    if not os.path.isdir(ck):
        sys.exit('找不到 %s：--root 指的是 %s 的根目录吗？' % (ck, 'BXPlugin' if args.into == 'bxplugin' else 'llvm-project'))
    # ① 源码与头文件
    want = {f for f, _ in m['files']}
    for f, src in m['files']:
        dst = os.path.join(ck, f)
        if os.path.exists(dst) and filecmp.cmp(src, dst, shallow=False):
            continue
        plan.copies.append((src, dst))
    unchanged = len(m['files']) - len(plan.copies)
    if unchanged:
        plan.notes.append('源码 %d 个文件已是最新' % unchanged)
    stale = []
    for d in m['owned_dirs']:
        for rootdir, dirs, files in os.walk(os.path.join(ck, d)):
            for f in files:
                rel = os.path.relpath(os.path.join(rootdir, f), ck)
                if rel not in want:
                    stale.append(rel)
                    plan.deletes.append(os.path.join(ck, rel))
    # ② 源码 CMakeLists：追加缺的；拥有的子目录里多出来的登记去掉
    cm = os.path.join(ck, 'CMakeLists.txt')
    new, added = cmake_add(plan.current(cm), m['srcs'], args.into)
    entries = cmake_entries(new, args.into) or []
    extra = [e for e in entries if e.split('/', 1)[0] in m['owned_dirs'] and '/' in e and e not in m['srcs']]
    if extra:
        new, _ = cmake_remove(new, extra)
    included = False
    if args.into == 'bxplugin' and m['owned_dirs']:
        new, included = cmake_include(new)  # 子目录里的头文件按 "OobSummary/…" 包含（SPEC-0114）
    plan.edit(cm, new)
    if not added and not extra and not included:
        plan.notes.append('CMakeLists.txt 已登记')
    # ③ Checkers.td：没有就插到包块最后；有但内容不同就整段替换
    td = os.path.join(root, lay['td'])
    text = plan.current(td)
    pkgdef, pkgname = PKG[m['package']]
    if args.into == 'llvm' and 'RosaRBEnhanceCore' not in text:
        text = text.rstrip('\n') + '\n' + LLVM_PACKAGES
    snip, name = m['td_snippet'], def_name(m['td_snippet'])
    old = td_def_block(text, name)
    if old is not None:
        if old.rstrip() == snip.rstrip():
            plan.notes.append('Checkers.td 的 def %s 已是最新' % name)
        else:
            text = text.replace(old, snip.rstrip(), 1)
    else:
        mk = list(td_end_marker(pkgname).finditer(text))
        if len(mk) != 1:
            sys.exit('Checkers.td 里找到 %d 处 `} // end "%s"`，应当正好 1 处——请手工把条目追加到 %s 块的最后' % (len(mk), pkgname, pkgdef))
        pos = mk[0].start()
        text = text[:pos].rstrip('\n') + '\n\n' + snip + '\n' + text[pos:]
    plan.edit(td, text)
    # ④⑤ 用例目录与登记
    if lay['testcase'] and m.get('testcase'):
        tcroot = os.path.join(root, lay['testcase'])
        dst = os.path.join(tcroot, m['name'])
        if os.path.isdir(dst):
            if dir_differs(m['testcase'], dst):
                plan.deletes.append(dst)
                plan.copies.append((m['testcase'], dst))
            else:
                plan.notes.append('用例目录已是最新')
        else:
            plan.copies.append((m['testcase'], dst))
        tcm = os.path.join(tcroot, 'CMakeLists.txt')
        cur = plan.current(tcm)
        line = 'add_subdirectory(%s)' % m['name']
        if re.search(r'^\s*%s\s*$' % re.escape(line), cur, re.M):
            plan.notes.append('TestCase/CMakeLists.txt 已登记')
        else:
            plan.edit(tcm, cur.rstrip('\n') + '\n' + line + '\n')


def plan_remove(args, m, plan):
    root, lay = args.root, LAYOUT[args.into]
    ck = os.path.join(root, lay['checkers'])
    for f, _ in m['files']:
        if os.path.exists(os.path.join(ck, f)):
            plan.deletes.append(os.path.join(ck, f))
    for d in m['owned_dirs']:
        if os.path.isdir(os.path.join(ck, d)):
            plan.deletes.append(os.path.join(ck, d))
    cm = os.path.join(ck, 'CMakeLists.txt')
    cur = plan.current(cm)
    entries = cmake_entries(cur, args.into) or []
    mine = set(m['srcs']) | {e for e in entries if '/' in e and e.split('/', 1)[0] in m['owned_dirs']}
    new, gone = cmake_remove(cur, mine)
    if gone:
        plan.edit(cm, new)
    td = os.path.join(root, lay['td'])
    text = plan.current(td)
    old = td_def_block(text, def_name(m['td_snippet']))
    if old is not None:
        # 落地时插入的是 "\n\n<def>\n"，撤掉时原样拿走，空行恢复成原来的样子
        if '\n\n' + old + '\n' in text:
            text = text.replace('\n\n' + old + '\n', '\n', 1)
        else:
            text = re.sub(r'\n?' + re.escape(old) + r'\n', '\n', text, count=1)
        plan.edit(td, text)
    if lay['testcase'] and m.get('testcase'):
        tcroot = os.path.join(root, lay['testcase'])
        if os.path.isdir(os.path.join(tcroot, m['name'])):
            plan.deletes.append(os.path.join(tcroot, m['name']))
        tcm = os.path.join(tcroot, 'CMakeLists.txt')
        cur = plan.current(tcm)
        new = re.sub(r'^\s*add_subdirectory\(%s\)\s*\n?' % re.escape(m['name']), '', cur, flags=re.M)
        plan.edit(tcm, new)


def check_tree(m):
    """仓库内清单 ↔ llvm 树里的登记是否一致。"""
    if m['mode'] != 'tree':
        sys.exit('--check-tree 只用于仓库内清单（*.manifest）')
    probs = []
    cm = os.path.join(m['repo'], LAYOUT['llvm']['checkers'], 'CMakeLists.txt')
    if not os.path.exists(cm):
        print('== %s：没有 clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt（稀疏检出时把它加进 sparse-checkout），跳过登记检查' % m['name'])
        return True
    entries = cmake_entries(open(cm, encoding='utf-8').read(), 'llvm') or []
    mine = [e for e in entries if e in m['srcs'] or ('/' in e and e.split('/', 1)[0] in m['owned_dirs'])]
    for s in m['srcs']:
        if s not in entries:
            probs.append('源文件没登记进 clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt：%s' % s)
    for e in mine:
        if e not in m['srcs']:
            probs.append('CMakeLists.txt 登记了清单里没有（或已删除）的文件：%s' % e)
    print('== %s：清单 %d 个源文件、%d 个头文件；CMakeLists 登记 %d 个；Checkers.td 有 def %s' % (
        m['name'], len(m['srcs']), len(m['files']) - len(m['srcs']), len(mine), m['td_def']))
    for p in probs:
        print('  ✗ ' + p)
    if not probs:
        print('  一致')
    return not probs


# ---------------------------------------------------------------- 输出与执行
def show(plan, root):
    rel = lambda p: os.path.relpath(p, root)
    for d in plan.deletes:
        print('删除  %s' % rel(d))
    for s, d in plan.copies:
        print('拷贝  %s → %s' % (os.path.basename(s.rstrip('/')), rel(d)))
    for p, (old, new) in plan.edits.items():
        print('修改  %s' % rel(p))
        for l in difflib.unified_diff(old.splitlines(), new.splitlines(), 'a/' + rel(p), 'b/' + rel(p), n=2, lineterm=''):
            print('      ' + l)
    for n in plan.notes:
        print('跳过  %s' % n)


def apply(plan, args, m):
    for d in plan.deletes:
        if os.path.isdir(d):
            shutil.rmtree(d)
        elif os.path.exists(d):
            os.remove(d)
    for s, d in plan.copies:
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if os.path.isdir(s):
            shutil.copytree(s, d, ignore=shutil.ignore_patterns('__pycache__'))
        else:
            shutil.copyfile(s, d)
    # 删空了的子目录也去掉
    ck = os.path.join(args.root, LAYOUT[args.into]['checkers'])
    for d in sorted({os.path.dirname(f) for f, _ in m['files'] if os.path.dirname(f)} | set(m['owned_dirs']), key=len, reverse=True):
        p = os.path.join(ck, d)
        for rootdir, dirs, files in os.walk(p, topdown=False):
            if not os.listdir(rootdir):
                os.rmdir(rootdir)
    for p, (_, new) in plan.edits.items():
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write(new)


def main():
    ap = argparse.ArgumentParser(description='按五处落点落地 / 同步 / 撤掉一个 checker')
    ap.add_argument('--drop', required=True, help='包目录（含 drop.manifest）或仓库内清单（*.manifest）')
    ap.add_argument('--into', choices=sorted(LAYOUT))
    ap.add_argument('--root', help='BXPlugin 根目录 或 llvm-project 根目录')
    ap.add_argument('--remove', action='store_true', help='撤掉（删除拷贝的文件、去掉登记）')
    ap.add_argument('--apply', action='store_true', help='真的写（默认只演示）')
    ap.add_argument('--check-tree', action='store_true', help='只检查仓库内清单与 llvm 树里的登记是否一致')
    args = ap.parse_args()
    m = read_manifest(args.drop)
    if args.check_tree:
        sys.exit(0 if check_tree(m) else 1)
    if not args.into or not args.root:
        ap.error('落地需要 --into 与 --root')
    args.root = os.path.abspath(os.path.expanduser(args.root))
    plan = Plan()
    (plan_remove if args.remove else plan_land)(args, m, plan)
    print('== %s %s（%s，%d 个文件）%s %s' % ('撤掉' if args.remove else '落地', m['name'], PKG[m['package']][1],
                                        len(m['files']), '←' if args.remove else '→', args.root))
    show(plan, args.root)
    if not (plan.copies or plan.deletes or plan.edits):
        print('== 无事可做（已是最新）')
        return
    if args.apply:
        apply(plan, args, m)
        print('== 已写入。接着：重新编译（BXPlugin：make -j16；llvm：ninja clang），然后跑用例')
    else:
        print('== 以上是演示，没有写任何文件；确认无误后加 --apply')


if __name__ == '__main__':
    main()
