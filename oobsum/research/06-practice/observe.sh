#!/usr/bin/env bash
# 把一次 infer 运行的所有可观测点一次性 dump 出来。
#
#   ./observe.sh [infer-out 目录]      默认 ./infer-out
#
# 输出同时打屏并写入 observation-report.txt。对照 02-observability-handbook.md 阅读。
set -u
OUT="${1:-infer-out}"
REPORT="observation-report.txt"
command -v infer >/dev/null || { echo "找不到 infer，请先 export PATH=<infer>/bin:\$PATH"; exit 1; }
[ -d "$OUT" ] || { echo "找不到目录 $OUT"; exit 1; }

exec > >(tee "$REPORT") 2>&1
sec(){ printf '\n════════ %s ════════\n' "$1"; }

sec "0. 本次运行的参数快照"
python3 - "$OUT" <<'PY' 2>/dev/null || echo "(无 .infer_runstate.json)"
import json,sys,os
p=os.path.join(sys.argv[1],".infer_runstate.json")
d=json.load(open(p))
for k,v in list(d.items())[:20]: print(f"  {k}: {str(v)[:110]}")
PY

sec "1. capture 阶段：捕获了哪些源文件"
python3 - "$OUT" <<'PY'
import sqlite3,sys,os
c=sqlite3.connect(os.path.join(sys.argv[1],"capture.db"))
rows=list(c.execute("select source_file, freshly_captured from source_files order by source_file"))
print(f"  共 {len(rows)} 个源文件（source_file 的 R 前缀 = 相对 project root）")
for f,fresh in rows[:40]: print(f"    {'[新]' if fresh else '[旧]'} {f}")
if len(rows)>40: print(f"    … 其余 {len(rows)-40} 个略")
PY

sec "2. capture 阶段：捕获了哪些过程（按 CFG 大小排序，找超大函数）"
python3 - "$OUT" <<'PY'
import sqlite3,sys,os
c=sqlite3.connect(os.path.join(sys.argv[1],"capture.db"))
rows=list(c.execute("select proc_uid, length(cfg), length(callees) from procedures order by length(cfg) desc"))
print(f"  共 {len(rows)} 个过程")
print(f"    {'过程':<40} {'cfg字节':>9} {'callees字节':>11}")
for u,cfg,cal in rows[:25]: print(f"    {u[:40]:<40} {cfg or 0:>9} {cal or 0:>11}")
if len(rows)>25: print(f"    … 其余 {len(rows)-25} 个略")
PY

sec "3. analyze 阶段：★ 摘要（人可读形式）"
echo "  命令: infer debug --procedures --procedures-name --procedures-summary --procedures-summary-skip-empty --select all"
echo "  注意：不是 --dump-json-summaries（那个只导出 Pulse，BufferOverrun 摘要不在里面）"
echo
infer debug -o "$OUT" --procedures --procedures-name --procedures-summary \
      --procedures-summary-skip-empty --select all 2>/dev/null | head -120

sec "4. analyze 阶段：跨过程依赖边（增量失效的基础）"
infer debug -o "$OUT" --procedures --procedures-name --procedures-summary \
      --procedures-summary-skip-empty --select all 2>/dev/null \
  | grep -E "^(int|void|char|unsigned|long|float|double|static|struct|_Bool).*\(|^summary_loads|^type environments" \
  | paste - - - 2>/dev/null | head -40

sec "5. analyze 阶段：每个过程产出了哪些分析的摘要"
python3 - "$OUT" <<'PY'
import sqlite3,sys,os
c=sqlite3.connect(os.path.join(sys.argv[1],"results.db"))
cols=[r[1] for r in c.execute("PRAGMA table_info(specs)")][4:]
rows=list(c.execute("select proc_uid,%s from specs"%",".join(f'"{x}"' for x in cols)))
print(f"  specs 表 {len(rows)} 行；payload 列共 {len(cols)} 个")
for uid,*vals in rows[:25]:
    have=[c2 for c2,v in zip(cols,vals) if v is not None]
    print(f"    {uid[:44]:<44} {have}")
if len(rows)>25: print(f"    … 其余 {len(rows)-25} 行略")
PY

sec "6. analyze 阶段：摘要体积排行（找状态膨胀最严重的过程）"
python3 - "$OUT" <<'PY'
import sqlite3,sys,os
c=sqlite3.connect(os.path.join(sys.argv[1],"results.db"))
q="""select proc_uid,
        coalesce(length(BufferOverrunChecker),0) bo,
        coalesce(length(Pulse),0) pulse,
        length(report_summary) rep
     from specs order by bo+pulse desc limit 15"""
print(f"    {'过程':<40} {'BO摘要':>8} {'Pulse摘要':>10} {'报告':>7}")
for u,bo,pu,rep in c.execute(q): print(f"    {u[:40]:<40} {bo:>8} {pu:>10} {rep:>7}")
print("\n  序列化格式自检（应为 8495a6be = OCaml Marshal 魔数）：")
for u,h in c.execute("select proc_uid, hex(substr(report_summary,1,4)) from specs limit 3"):
    print(f"    {u[:40]:<40} {h}")
PY

sec "7. 统计计数器（判断分析行为最快的一张表）"
if [ -f "$OUT/stats/stats.txt" ]; then
  cat "$OUT/stats/stats.txt"
  echo
  echo "  ↑ 上真实代码库后最该盯的三个："
  echo "     ondemand_procs_analyzed  = 实际分析了几个过程（覆盖率）"
  echo "     pulse_disjuncts_dropped  > 0 表示发生了欠近似（= 漏报风险）"
  echo "     pulse_interrupted_loops  > 0 表示有循环因超 widen 阈值被中断"
else
  echo "  (无 stats/stats.txt)"
fi

sec "8. 报告：按规则统计"
python3 - "$OUT" <<'PY'
import json,collections,sys,os
p=os.path.join(sys.argv[1],"report.json")
try: d=json.load(open(p))
except Exception as e: print("  无 report.json:",e); raise SystemExit
print(f"  共 {len(d)} 条告警")
for t,n in collections.Counter(x["bug_type"] for x in d).most_common(): print(f"    {t:<28} {n}")
if d:
    x=d[0]
    print(f"\n  第一条的完整字段：")
    for k in ("bug_type","file","line","procedure","qualifier","bug_trace_length","bug_trace_max_depth","key","hash"):
        print(f"    {k:<20} {str(x.get(k))[:90]}")
    print(f"\n  它的跨过程路径（bug_trace，看根因在哪个文件）：")
    for s in x.get("bug_trace",[]):
        print(f"    L{s['level']} {s['filename']}:{s['line_number']}  {s['description'][:70]}")
PY

sec "9. infer-out 目录清单"
du -ah "$OUT" 2>/dev/null | sort -k2 | sed "s|$OUT|.|" | head -30

printf '\n完成。完整输出已写入 %s\n' "$REPORT"
