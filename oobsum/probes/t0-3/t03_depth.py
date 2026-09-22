#!/usr/bin/env python3
"""T0-3：真实越界的跨过程深度分布（同时产出 T0-2 的"单函数内可判定"比例）。

两个数据源，用哪个都行，两个都有最好（互相校验）：

  ① Infer 的告警（快，自动；但只能看到 Infer 看得到的那部分）
        python3 t03_depth.py infer <infer-out>/report.json [--csv out.csv]

  ② 历史 DTS 单（慢，人工；但是"真实越界"的无偏样本）
        python3 t03_depth.py dts-template > dts.csv      # 生成模板，逐行填
        python3 t03_depth.py dts dts.csv                 # 汇总

两种深度，必须分开数（v3.5 起；旧版 03-文档里那段脚本把它们混成了一个 max(level)）：

  down 深度  = 从"报出告警的函数"往下，要穿过几层调用才到真正的数组访问。
               f(){ int a[10]; g(a, 11); }  g(p,i){ return p[i]; }      → down = 1
               这是 requires 式摘要（安全条件逐层上传 + 代换）要解决的深度。
  origin 深度 = 下标 / 长度的**值**是在几层之下算出来的。
               f(){ int a[10]; return a[idx()]; }  idx(){ return 15; }  → down = 0, origin = 1
               这要靠返回值 / 出参摘要（01-architecture.md ExtentModel #20、PostCondition），
               是另一类能力。

  depth = max(down, origin) 是"至少要看穿几层"；决策主要看 down，origin 单独列出。

v3.6 起项目已立项（目标 2–17 层），本脚本不再用于判断"做不做"，
改为从告警 / DTS 单里挑出 ≥ 3 层的真实样例，作为 Summary 2.0 的验收语料。

只依赖 Python 3 标准库。
"""
import argparse
import collections
import csv
import json
import sys

# ---------------------------------------------------------------------------
# 公共：分布与判读
# ---------------------------------------------------------------------------


def histogram(title, values, width=40):
    c = collections.Counter(values)
    n = len(values)
    print(f"\n{title}（共 {n} 条）")
    if n == 0:
        print("  （无数据）")
        return
    cum = 0
    for k in sorted(c):
        cum += c[k]
        bar = "█" * max(1, round(c[k] / n * width))
        print(f"  {k:>3} 层  {c[k]:>6}  {c[k] / n * 100:5.1f}%   累计 {cum / n * 100:5.1f}%   {bar}")


def verdict(depths, label):
    n = len(depths)
    if n == 0:
        return
    ge2 = sum(1 for d in depths if d >= 2) / n
    le1 = sum(1 for d in depths if d <= 1) / n
    ge3 = sum(1 for d in depths if d >= 3) / n
    print(f"\n判读（{label}，n={n}）：")
    print(f"  depth ≥ 2：{ge2 * 100:5.1f}%     depth ≥ 3：{ge3 * 100:5.1f}%     depth ≤ 1：{le1 * 100:5.1f}%")
    # v3.6 起项目已立项（目标 2–17 层），这里只给分布，不再据此判断"做不做"
    print("  （v3.6 起不再据此决定方案；用途是挑 ≥3 层的样例做验收语料 —— 见 --csv 明细里 down ≥ 3 的行）")
    if n < 30:
        print(f"  ！样本只有 {n} 条，少于 30，结论只能当方向参考。")


# ---------------------------------------------------------------------------
# ① Infer report.json
# ---------------------------------------------------------------------------


def is_access(t):
    return (t.get("description") or "").startswith("Array access")


def analyze_issue(x):
    """返回 (down, origin, files, access_site)。"""
    trace = x.get("bug_trace") or []
    if not trace:
        return 0, 0, 1, (x.get("file"), x.get("line"))
    acc = [t for t in trace if is_access(t)]
    access = acc[-1] if acc else trace[-1]
    down = int(access.get("level", 0) or 0)
    # origin：值推导链条上（非 Parameter、非 Array access、非分节标题）出现过的最深层级。
    #   Parameter 行是"沿调用链往下传"的记录，层级 ≤ down，不算值来源。
    origin = 0
    for t in trace:
        d = t.get("description") or ""
        if is_access(t) or d.startswith("Parameter") or d.startswith("<"):
            continue
        lvl = int(t.get("level", 0) or 0)
        if d == "Call":
            continue          # Call 行本身在调用者那层，不代表值来源
        origin = max(origin, lvl)
    files = len({t.get("filename") for t in trace if t.get("filename")}) or 1
    return down, origin, files, (access.get("filename"), access.get("line_number"))


def cmd_infer(args):
    try:
        report = json.load(open(args.report, encoding="utf-8"))
    except FileNotFoundError:
        print(f"✗ 找不到 {args.report}", file=sys.stderr)
        return 2
    oob = [x for x in report if str(x.get("bug_type", "")).startswith("BUFFER_OVERRUN")]
    print(f"report.json 共 {len(report)} 条告警，其中越界类 BUFFER_OVERRUN_* {len(oob)} 条")
    if not oob:
        print("  没有越界告警。确认 analyze 时带了 --bufferoverrun（或 --bufferoverrun-only），"
              "以及分析的文件集合够大。", file=sys.stderr)
        return 1

    grades = collections.Counter(x["bug_type"] for x in oob)
    print("  按等级：" + "  ".join(f"{k}={v}" for k, v in sorted(grades.items())))

    rows = []
    for x in oob:
        down, origin, files, site = analyze_issue(x)
        rows.append({
            "bug_type": x.get("bug_type"),
            "file": x.get("file"),
            "line": x.get("line"),
            "procedure": x.get("procedure"),
            "down": down,
            "origin": origin,
            "depth": max(down, origin),
            "files_in_trace": files,
            "cross_file": files > 1,
            "access_site": f"{site[0]}:{site[1]}",
            "qualifier": (x.get("qualifier") or "").replace("\n", " "),
        })

    # 口径 1：每条告警一个样本（同一访问点被不同调用者报出，算多次）
    histogram("口径 1 · 每条告警：down 深度", [r["down"] for r in rows])
    histogram("口径 1 · 每条告警：depth = max(down, origin)", [r["depth"] for r in rows])
    nret = sum(1 for r in rows if r["origin"] > r["down"])
    print(f"\n  其中 {nret} 条（{nret / len(rows) * 100:.1f}%）的下标/长度来自比访问点更深的被调函数"
          "（返回值 / 出参）—— 这部分需要 PostCondition 摘要，不是 requires 上传能解决的。")
    xf = sum(1 for r in rows if r["cross_file"])
    print(f"  跨文件的 {xf} 条（{xf / len(rows) * 100:.1f}%）—— 单 TU 内 inline 看不到，只有全量分析 / CTU / 摘要能看到。")

    # 口径 2：每个真实访问点一个样本（取该访问点所有告警里最深的一条）
    by_site = {}
    for r in rows:
        s = r["access_site"]
        if s not in by_site or r["depth"] > by_site[s]["depth"]:
            by_site[s] = r
    histogram("口径 2 · 每个访问点（去重后取最深）：depth", [r["depth"] for r in by_site.values()])

    # L1/L2 单列：默认配置下这两级几乎没有误报（07-capability-boundary：默认档精确率 100%）
    hi = [r["depth"] for r in by_site.values() if r["bug_type"] in ("BUFFER_OVERRUN_L1", "BUFFER_OVERRUN_L2")]
    histogram("口径 2 · 只看 L1/L2（最可信的两级）：depth", hi)

    verdict([r["depth"] for r in by_site.values()], "口径 2 全部")
    verdict(hi, "口径 2 仅 L1/L2")

    if args.csv:
        with open(args.csv, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=list(rows[0].keys()))
            w.writeheader()
            w.writerows(rows)
        print(f"\n逐条明细已写入 {args.csv}（可以抽查几条，把误报剔掉后再判读）")

    print("\n注意：这是 **Infer 看得到的** 越界的深度分布，不是全部真实越界的深度分布。"
          "\n      Infer 在 struct 字段数组（局部/全局基址）、全局无初始化器、函数指针上有结构性漏报"
          "\n      （07-capability-boundary §3），这些样本在这里是缺席的。用 ② DTS 单交叉校验。")
    return 0


# ---------------------------------------------------------------------------
# ② DTS 单（人工）
# ---------------------------------------------------------------------------

DTS_FIELDS = [
    ("dts", "DTS 单号"),
    ("access_func", "越界访问所在函数"),
    ("decl_func", "数组声明/分配所在函数"),
    ("index_func", "越界下标值最终算出来的函数"),
    ("down", "从'同时知道数组多大和下标多大'的那个函数往下，到越界访问点跨几层调用（同一函数=0）"),
    ("origin", "下标或长度的值是在更深的被调函数里算出来的（返回值/出参），又深几层（没有=0）"),
    ("cross_file", "是否跨文件 Y/N"),
    ("t02_class", "T0-2 分类 A/B/C/D（A 单函数可判定 / B 跨1层 / C 跨≥2层 / D 需全局或堆的跨TU信息）"),
    ("base_kind", "基址形态：local/static/global/heap/param/field/fam/extern/other"),
    ("loop", "访问是否在循环里 Y/N"),
    ("fnptr", "链条上是否有函数指针调用 Y/N"),
    ("note", "备注"),
]


def cmd_dts_template(_args):
    w = csv.writer(sys.stdout)
    w.writerow([k for k, _ in DTS_FIELDS])
    w.writerow(["# " + d for _, d in DTS_FIELDS])
    w.writerow(["DTS2026xxxxxxx", "Foo_Parse", "Foo_Recv", "Foo_Recv", "2", "0", "Y", "C",
                "field", "N", "N", "示例行，填完删掉；#开头的行会被忽略"])
    return 0


def cmd_dts(args):
    rows = []
    with open(args.csv, encoding="utf-8-sig") as fh:
        for r in csv.DictReader(fh):
            if (r.get("dts") or "").startswith("#") or not (r.get("dts") or "").strip():
                continue
            if r.get("dts", "").startswith("DTS2026xxxxxxx"):
                continue
            rows.append(r)
    if not rows:
        print("✗ 没有有效行（# 开头和示例行会被忽略）", file=sys.stderr)
        return 1

    def num(v):
        try:
            return int(str(v).strip())
        except ValueError:
            return None

    bad = [r["dts"] for r in rows if num(r.get("down")) is None]
    if bad:
        print(f"！{len(bad)} 行的 down 不是整数，已跳过：{', '.join(bad[:10])}")
    rows = [r for r in rows if num(r.get("down")) is not None]
    for r in rows:
        r["_down"] = num(r["down"])
        r["_origin"] = num(r.get("origin")) or 0
        r["_depth"] = max(r["_down"], r["_origin"])

    print(f"DTS 样本 {len(rows)} 条")
    histogram("down 深度", [r["_down"] for r in rows])
    histogram("depth = max(down, origin)", [r["_depth"] for r in rows])
    verdict([r["_depth"] for r in rows], "DTS")

    for col, title in (("t02_class", "T0-2 分类"), ("base_kind", "基址形态"),
                       ("cross_file", "跨文件"), ("loop", "在循环里"), ("fnptr", "函数指针")):
        c = collections.Counter((r.get(col) or "?").strip() or "?" for r in rows)
        print(f"\n{title}：" + "  ".join(f"{k}={v}（{v / len(rows) * 100:.0f}%）" for k, v in c.most_common()))

    deep = [r["dts"] for r in rows if r["_depth"] >= 3]
    print(f"\n≥ 3 层的样例 {len(deep)} 条（可直接作为验收语料）：{', '.join(deep[:20])}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("infer", help="从 infer 的 report.json 统计")
    p.add_argument("report")
    p.add_argument("--csv", help="逐条明细输出到这个 CSV")
    p.set_defaults(fn=cmd_infer)
    p = sub.add_parser("dts-template", help="打印 DTS 人工统计模板")
    p.set_defaults(fn=cmd_dts_template)
    p = sub.add_parser("dts", help="汇总填好的 DTS 统计表")
    p.add_argument("csv")
    p.set_defaults(fn=cmd_dts)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
