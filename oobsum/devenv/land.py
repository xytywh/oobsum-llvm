#!/usr/bin/env python3
"""把一个 checker 包按"五处落点"落到目标源码树里（或撤掉）。默认只演示（打印要做的事和 diff），加 --apply 才写。

两个目标：
  --into bxplugin  公司的 BXPlugin（csa-checker-dev 的五处落点）：
                     源码      xir/llvm_ast/StaticAnalyzer/Checkers/        （CMakeLists.txt 的 add_library() 末尾追加）
                     声明      xir/include/clang/StaticAnalyzer/Checkers/Checkers.td  （对应包块的最后）
                     用例      xir/llvm_ast/TestCase/<Name>/                  （TestCase/CMakeLists.txt 最后追加 add_subdirectory）
  --into llvm      本地的 llvm-project-14（预演同样的注册方式，把 checker 编进 clang 本体）：
                     源码      clang/lib/StaticAnalyzer/Checkers/            （add_clang_library() 源文件列表末尾）
                     声明      clang/include/clang/StaticAnalyzer/Checkers/Checkers.td（没有 rb.enhance.* 包就先补上）
                     用例      不落（本地用 cc1-run.sh 直接跑）

包的清单 drop.manifest（与包里的文件放在同一目录，路径相对它）：
    name=OobSummaryProbeChecker          # 规则名（TestCase 子目录名）
    package=core                         # core | ipa → RosaRBEnhanceCore | RosaRBEnhanceIpa
    td=checkers_td_snippet.td            # Checkers.td 片段：从第一行 def 起插入（前面的说明注释不插，Checkers.td 只放英文）
    src=OobSummaryProbeChecker.cpp       # 拷贝并登记进 CMakeLists
    src=OobSummary/ProbeSubdirUnit.cpp
    hdr=OobSummary/ProbeSubdirUnit.h     # 只拷贝
    testcase=TestCase/OobSummaryProbeChecker

用法：
    python3 land.py --drop <包目录> --into bxplugin --root /usr1/BXPlugin            # 演示
    python3 land.py --drop <包目录> --into bxplugin --root /usr1/BXPlugin --apply    # 落地
    python3 land.py --drop <包目录> --into bxplugin --root /usr1/BXPlugin --remove --apply   # 整体撤掉
重复执行是安全的：已经落过的项会跳过。找不到预期的结构（包块结尾、add_library）就停下，不猜。
只依赖 Python 3 标准库。
"""
import argparse
import difflib
import filecmp
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


class Plan(object):
    def __init__(self):
        self.edits = {}      # 路径 → (旧内容, 新内容)
        self.copies = []     # (源, 目的)
        self.deletes = []    # 路径（文件或目录）
        self.notes = []

    def edit(self, path, new):
        old = self.edits[path][0] if path in self.edits else (open(path, encoding='utf-8').read() if os.path.exists(path) else '')
        self.edits[path] = (old, new)

    def current(self, path):
        if path in self.edits:
            return self.edits[path][1]
        return open(path, encoding='utf-8').read() if os.path.exists(path) else ''


def read_manifest(drop):
    m = {'src': [], 'hdr': []}
    for line in open(os.path.join(drop, 'drop.manifest'), encoding='utf-8'):
        line = line.split('#', 1)[0].strip()
        if not line:
            continue
        k, v = [x.strip() for x in line.split('=', 1)]
        if k in ('src', 'hdr'):
            m[k].append(v)
        else:
            m[k] = v
    for k in ('name', 'package', 'td'):
        if k not in m:
            sys.exit('drop.manifest 缺 %s' % k)
    if m['package'] not in PKG:
        sys.exit('package 只能是 core 或 ipa')
    return m


def td_snippet(drop, m):
    lines = open(os.path.join(drop, m['td']), encoding='utf-8').read().splitlines()
    i = next((k for k, l in enumerate(lines) if l.startswith('def ')), None)
    if i is None:
        sys.exit('%s 里没有 def' % m['td'])
    body = '\n'.join(lines[i:]).rstrip() + '\n'
    if re.search(r'[　-〿一-鿿＀-￯]', body):
        sys.exit('%s 的 def 部分含中文：Checkers.td 只放英文' % m['td'])
    return body


def cmake_add(text, srcs, into):
    """在 add_library / add_clang_library 的源文件列表末尾追加（保持相邻行的缩进），已有的跳过。"""
    lines = text.splitlines(keepends=True)
    head = 'add_clang_library(' if into == 'llvm' else 'add_library('
    start = next((i for i, l in enumerate(lines) if l.lstrip().startswith(head)), None)
    if start is None:
        sys.exit('CMakeLists.txt 里找不到 %s——结构与预期不同，请手工登记：%s' % (head, ', '.join(srcs)))
    depth, end = 0, None
    for i in range(start, len(lines)):
        depth += lines[i].count('(') - lines[i].count(')')
        if depth <= 0:
            end = i
            break
    if end is None:
        sys.exit('CMakeLists.txt 的 %s 没有闭合' % head)
    block = lines[start:end + 1]
    have = set(re.findall(r'[\w./+-]+\.(?:cpp|cc|c)\b', ''.join(block)))
    todo = [s for s in srcs if s not in have]
    if not todo:
        return text, []
    # 最后一个源文件行之后插入（LLVM 的列表后面还有 LINK_LIBS / DEPENDS，不能插在闭括号前）
    last = max(i for i in range(start, end + 1) if re.search(r'\.(?:cpp|cc|c)\b', lines[i]))
    m = re.match(r'(\s*)', lines[last])
    indent = m.group(1) if lines[last].strip() and not lines[last].lstrip().startswith(head) else '    '
    if lines[last].rstrip().endswith(')') and last == end:
        # 形如 "    Last.cpp)"：把闭括号挪到新行后面
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


def plan_land(args, m, plan):
    root, drop, lay = args.root, args.drop, LAYOUT[args.into]
    ck = os.path.join(root, lay['checkers'])
    if not os.path.isdir(ck):
        sys.exit('找不到 %s：--root 指的是 %s 的根目录吗？' % (ck, 'BXPlugin' if args.into == 'bxplugin' else 'llvm-project'))
    # ① 源码与头文件
    for f in m['src'] + m['hdr']:
        dst = os.path.join(ck, f)
        if os.path.exists(dst) and filecmp.cmp(os.path.join(drop, f), dst, shallow=False):
            plan.notes.append('源码已是最新：%s' % f)
        else:
            plan.copies.append((os.path.join(drop, f), dst))
    # ② 源码 CMakeLists
    cm = os.path.join(ck, 'CMakeLists.txt')
    new, added = cmake_add(plan.current(cm), m['src'], args.into)
    if added:
        plan.edit(cm, new)
    else:
        plan.notes.append('CMakeLists.txt 已登记')
    # ③ Checkers.td
    td = os.path.join(root, lay['td'])
    text = plan.current(td)
    pkgdef, pkgname = PKG[m['package']]
    if args.into == 'llvm' and 'RosaRBEnhanceCore' not in text:
        text = text.rstrip('\n') + '\n' + LLVM_PACKAGES
    snip = td_snippet(drop, m)
    first_def = snip.splitlines()[0]
    if first_def in text:
        plan.notes.append('Checkers.td 已有 %s' % first_def.split(':')[0])
    else:
        mk = list(td_end_marker(pkgname).finditer(text))
        if len(mk) != 1:
            sys.exit('Checkers.td 里找到 %d 处 `} // end "%s"`，应当正好 1 处——请手工把片段追加到 %s 块的最后' % (len(mk), pkgname, pkgdef))
        pos = mk[0].start()
        before = text[:pos].rstrip('\n') + '\n\n'
        text = before + snip + '\n' + text[pos:]
    if text != open(td, encoding='utf-8').read():
        plan.edit(td, text)
    # ④⑤ 用例目录与登记
    if lay['testcase'] and m.get('testcase'):
        tcroot = os.path.join(root, lay['testcase'])
        dst = os.path.join(tcroot, m['name'])
        src = os.path.join(drop, m['testcase'])
        if os.path.isdir(dst):
            plan.notes.append('用例目录已存在（不覆盖；要更新先 --remove）：%s' % dst)
        else:
            plan.copies.append((src, dst))
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
    for f in m['src'] + m['hdr']:
        if os.path.exists(os.path.join(ck, f)):
            plan.deletes.append(os.path.join(ck, f))
    for d in sorted({os.path.dirname(f) for f in m['src'] + m['hdr'] if os.path.dirname(f)}, reverse=True):
        plan.notes.append('若 %s 已空则删除' % os.path.join(ck, d))
    cm = os.path.join(ck, 'CMakeLists.txt')
    new, gone = cmake_remove(plan.current(cm), m['src'])
    if gone:
        plan.edit(cm, new)
    td = os.path.join(root, lay['td'])
    text = plan.current(td)
    snip = td_snippet(drop=args.drop, m=m)
    if snip in text:
        text = text.replace('\n' + snip + '\n', '\n').replace(snip, '')
        plan.edit(td, text)
    if lay['testcase'] and m.get('testcase'):
        tcroot = os.path.join(root, lay['testcase'])
        if os.path.isdir(os.path.join(tcroot, m['name'])):
            plan.deletes.append(os.path.join(tcroot, m['name']))
        tcm = os.path.join(tcroot, 'CMakeLists.txt')
        cur = plan.current(tcm)
        new = re.sub(r'^\s*add_subdirectory\(%s\)\s*\n?' % re.escape(m['name']), '', cur, flags=re.M)
        if new != cur:
            plan.edit(tcm, new)


def show(plan, root):
    rel = lambda p: os.path.relpath(p, root)
    for s, d in plan.copies:
        print('拷贝  %s → %s' % (os.path.basename(s.rstrip('/')), rel(d)))
    for d in plan.deletes:
        print('删除  %s' % rel(d))
    for p, (old, new) in plan.edits.items():
        print('修改  %s' % rel(p))
        for l in difflib.unified_diff(old.splitlines(), new.splitlines(), 'a/' + rel(p), 'b/' + rel(p), n=2, lineterm=''):
            print('      ' + l)
    for n in plan.notes:
        print('跳过  %s' % n)


def apply(plan, args, m):
    for s, d in plan.copies:
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if os.path.isdir(s):
            shutil.copytree(s, d)
        else:
            shutil.copyfile(s, d)
    for d in plan.deletes:
        if os.path.isdir(d):
            shutil.rmtree(d)
        elif os.path.exists(d):
            os.remove(d)
    if args.remove:
        ck = os.path.join(args.root, LAYOUT[args.into]['checkers'])
        for d in sorted({os.path.dirname(f) for f in m['src'] + m['hdr'] if os.path.dirname(f)}, reverse=True):
            p = os.path.join(ck, d)
            if os.path.isdir(p) and not os.listdir(p):
                os.rmdir(p)
    for p, (_, new) in plan.edits.items():
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write(new)


def main():
    ap = argparse.ArgumentParser(description='按五处落点落地 / 撤掉一个 checker 包')
    ap.add_argument('--drop', required=True, help='包目录（含 drop.manifest）')
    ap.add_argument('--into', required=True, choices=sorted(LAYOUT))
    ap.add_argument('--root', required=True, help='BXPlugin 根目录 或 llvm-project 根目录')
    ap.add_argument('--remove', action='store_true', help='撤掉（删除拷贝的文件、去掉登记）')
    ap.add_argument('--apply', action='store_true', help='真的写（默认只演示）')
    args = ap.parse_args()
    args.drop = os.path.abspath(args.drop)
    args.root = os.path.abspath(os.path.expanduser(args.root))
    m = read_manifest(args.drop)
    plan = Plan()
    (plan_remove if args.remove else plan_land)(args, m, plan)
    print('== %s %s（%s）%s %s' % ('撤掉' if args.remove else '落地', m['name'], PKG[m['package']][1],
                                 '←' if args.remove else '→', args.root))
    show(plan, args.root)
    if not (plan.copies or plan.deletes or plan.edits):
        print('== 无事可做')
        return
    if args.apply:
        apply(plan, args, m)
        print('== 已写入。接着：重新编译（BXPlugin：make -j16；llvm：ninja clang），然后跑用例')
    else:
        print('== 以上是演示，没有写任何文件；确认无误后加 --apply')


if __name__ == '__main__':
    main()
