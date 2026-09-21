#!/usr/bin/env bash
# 一键复现报告 §13–§17 的全部实验。
#
# 用法：
#   export PATH=/path/to/infer-linux-x86_64-v1.3.0/bin:$PATH
#   ./run-all.sh
#
# 每组实验会打印【预期】与【实测】，便于对照报告里的数据表。
set -u
cd "$(dirname "$0")"

command -v infer >/dev/null || { echo "找不到 infer，请先 export PATH=<infer>/bin:\$PATH"; exit 1; }
command -v gcc   >/dev/null || { echo "找不到 gcc"; exit 1; }
echo "使用的 infer: $(command -v infer)  ($(infer --version | head -1))"

rule(){ printf '\n\033[1m── %s ──\033[0m\n' "$1"; }
expect(){ printf '  【预期】%s\n' "$1"; }

########################################################################
rule "00-A 基础实验：三层调用，越界归因到最外层  (报告 §13.1)"
cd 00-basic-summary
expect "example.c:7 报 BUFFER_OVERRUN_L1  Offset: 20 Size: 10 by call to \`wrapper\`"
rm -rf out-example
infer run --bufferoverrun --pulse -o out-example -- gcc -c example.c 2>&1 | grep -E "error:|Offset|Found [0-9]+ issue"

rule "00-B ★ 把摘要打印出来  (报告 §13.2)"
expect "read_value 的 Safety conditions = buf.offset + i < buf.length"
infer debug -o out-example --procedures --procedures-name --procedures-summary \
      --procedures-summary-skip-empty --select all 2>/dev/null \
  | grep -E "^int |Safety conditions|^ +\{ .*length|summary_loads" | head -20

rule "00-C ★ 三层形参→实参代换  (报告 §13.3，全报告最核心的一张表)"
expect "leaf: base.offset + idx | mid: arr.offset + k + 1 | top: p.offset + 2.n + 1 | caller: 空 + 报 L1"
rm -rf out-sub
infer run --bufferoverrun-only -o out-sub -- gcc -c sub.c 2>&1 | grep -E "error:|Offset"
infer debug -o out-sub --procedures --procedures-name --procedures-summary \
      --procedures-summary-skip-empty --select all 2>/dev/null \
  | grep -E "^int |length|signed32|summary_loads" | head -24
cd ..

########################################################################
rule "01 十三类复杂场景矩阵  (报告 §14)"
cd 01-thirteen-scenarios
printf "  %-16s %-26s %s\n" "用例" "默认配置" "最高灵敏度(--bo-debug 4 +L4/L5)"
HI="--enable-issue-type BUFFER_OVERRUN_L4 --enable-issue-type BUFFER_OVERRUN_L5 --enable-issue-type BUFFER_OVERRUN_U5 --bo-debug 4"
for f in c*.c; do
  b=${f%.c}
  rm -rf o_$b oh_$b
  infer run --bufferoverrun --pulse            -o o_$b  -- gcc -c "$f" >/dev/null 2>&1
  infer run --bufferoverrun-only $HI           -o oh_$b -- gcc -c "$f" >/dev/null 2>&1
  d=$(python3 -c "import json;print(','.join(x['bug_type'] for x in json.load(open('o_$b/report.json'))) or 'NONE')" 2>/dev/null || echo NONE)
  h=$(python3 -c "import json;print(','.join(x['bug_type'] for x in json.load(open('oh_$b/report.json'))) or 'NONE')" 2>/dev/null || echo NONE)
  printf "  %-16s %-26s %s\n" "$b" "$d" "$h"
done
expect "c08_global 两列都是 NONE（结构性漏报）；c03/c13_cfg 默认 NONE、高灵敏度出 L4；其余默认即检出"
cd ..

########################################################################
rule "02 调用深度：2 / 20 / 200 层  (报告 §15)"
cd 02-call-depth
printf "  %-8s %-10s %-12s %s\n" "深度" "耗时(s)" "specs行数" "报告数"
for f in chain2 chain20 chain200; do
  n=${f#chain}; rm -rf o_$f
  s=$(date +%s.%N); infer run --bufferoverrun-only -o o_$f -- gcc -c $f.c >/dev/null 2>&1; e=$(date +%s.%N)
  rows=$(python3 -c "import sqlite3;print(sqlite3.connect('o_$f/results.db').execute('select count(*) from specs').fetchone()[0])" 2>/dev/null||echo NA)
  iss=$(python3 -c "import json;print(len(json.load(open('o_$f/report.json'))))" 2>/dev/null||echo 0)
  printf "  %-8s %-10s %-12s %s\n" "$n" "$(python3 -c "print(f'{$e-$s:.2f}')")" "$rows" "$iss"
done
expect "耗时几乎不随深度增长；specs 行数 = 过程数 = 深度+1；每个深度都报出 1 条 L1"
grep ondemand_procs_analyzed o_chain200/stats/stats.txt 2>/dev/null | sed 's/^/  【实测】/'
cd ..

########################################################################
rule "03-A 每层多分支：理论 4^20 条路径  (报告 §16)"
cd 03-path-explosion
for f in d3_b2 d20_b4; do
  rm -rf bo_$f pl_$f
  s=$(date +%s.%N); infer run --bufferoverrun-only -o bo_$f -- gcc -c $f.c >/dev/null 2>&1; e=$(date +%s.%N)
  tbo=$(python3 -c "print(f'{$e-$s:.2f}')")
  s=$(date +%s.%N); infer run --pulse-only        -o pl_$f -- gcc -c $f.c >/dev/null 2>&1; e=$(date +%s.%N)
  tpl=$(python3 -c "print(f'{$e-$s:.2f}')")
  ibo=$(python3 -c "import json;print(len(json.load(open('bo_$f/report.json'))))" 2>/dev/null||echo 0)
  ipl=$(python3 -c "import json;print(len(json.load(open('pl_$f/report.json'))))" 2>/dev/null||echo 0)
  printf "  %-8s BufferOverrun %ss (%s 条报告)   Pulse %ss (%s 条报告)\n" "$f" "$tbo" "$ibo" "$tpl" "$ipl"
done
expect "两者都在亚秒级；BufferOverrun 检出，Pulse 报 0 条（Pulse 没有数组越界 issue type）"

rule "03-B Pulse 析取上限：主动丢状态  (报告 §16)"
for md in 5 20 200; do
  rm -rf o_$md
  infer run --pulse-only --pulse-max-disjuncts $md -o o_$md -- gcc -c disj.c >/dev/null 2>&1
  printf "  --pulse-max-disjuncts=%-4s %s\n" "$md" "$(grep -E 'pulse_disjuncts_dropped|pulse_summaries_count:' o_$md/stats/stats.txt | tr '\n' ' ')"
done
rm -rf o_bo && infer run --bufferoverrun-only -o o_bo -- gcc -c disj.c 2>&1 | grep -E "error:|Offset"
expect "丢弃数随上限增大而增大（欠近似=漏报）；非析取的 BufferOverrun 一次 join 得 Offset: [0, 4095]"
cd ..

########################################################################
rule "04 全局数组漏报的根因对照  (报告 §14.3)"
cd 04-global-arrays
rm -rf o_g2 && infer run --bufferoverrun-only -o o_g2 -- gcc -c g2.c 2>&1 | grep -E "error:|Offset|Found [0-9]+ issue"
expect "只有 g_init / g_st_init（带初始化器）被报出；g_noinit（试探性定义）完全漏报"
echo "  【为什么】Infer 获知全局数组 size 的唯一通道是 __infer_globals_initializer_* 的摘要："
infer debug -o o_g2 --procedures --procedures-name --select all 2>/dev/null | grep "__infer_globals_initializer" | sed 's/^/    /'
cd ..

########################################################################
rule "05 增量分析：改一个底层函数，谁被失效重算  (报告 §17)"
cd 05-incremental
cp leaf.c leaf.c.bak
restore(){ cp -f leaf.c.bak leaf.c; }
trap restore EXIT

# 注意：下面两个场景各自从一份全新的 infer-out 开始。
# 这不是洁癖 —— 实测发现失效判定依赖 capture 历史：若上一次 capture 已把某过程标记为
# "unchanged"，之后该过程真的改了，changed 标志有可能没被更新，导致 Invalidated 0 of 8。
# 详见本目录 README 的"已知坑"。CI 里请以场景 B 的命令序列为准。

echo "  ── 场景 A：源码完全没变，验证摘要是否跨进程复用"
restore; rm -rf infer-out
infer capture -- gcc -c leaf.c mid.c top.c >/dev/null 2>&1
infer analyze --bufferoverrun-only >/dev/null 2>&1
grep ondemand_procs_analyzed infer-out/stats/stats.txt | sed 's/^/    基线：/'
infer capture --incremental-analysis -- gcc -c leaf.c mid.c top.c >/dev/null 2>&1
infer analyze --bufferoverrun-only --incremental-analysis --changed-files-index all.idx 2>&1 \
  | grep -iE "Invalidated|Computed" | sed 's/^/    /'
grep ondemand_procs_analyzed infer-out/stats/stats.txt | sed 's/^/    /'
expect "Invalidated 0 of 8 / Computed 0 / ondemand_procs_analyzed: 0 —— 摘要跨进程被真正复用"

echo
echo "  ── 场景 B：只改 leaf.c 里的 leaf_get（CI 里应当照抄的命令序列）"
restore; rm -rf infer-out
infer capture -- gcc -c leaf.c mid.c top.c >/dev/null 2>&1
infer analyze --bufferoverrun-only >/dev/null 2>&1
cat > leaf.c <<'EOF'
int leaf_get(int *p, int i) { return p[i + 100]; }
int leaf_untouched(int *p) { return p[0]; }
EOF
infer capture --incremental-analysis -- gcc -c leaf.c >/dev/null 2>&1
infer analyze --bufferoverrun-only --incremental-analysis --changed-files-index all.idx 2>&1 \
  | grep -iE "Invalidated|Computed|error:|Found [0-9]+ issue" | sed 's/^/    /'
grep ondemand_procs_analyzed infer-out/stats/stats.txt | sed 's/^/    /'
python3 - <<'PYEOF' 2>/dev/null | sed 's/^/    /'
import sqlite3
c=sqlite3.connect('infer-out/results.db')
print("重算后 specs 里的过程：", sorted(r[0] for r in c.execute('select proc_uid from specs')))
PYEOF
expect "6~7 of 8 被失效并重算（确切数字取决于 capture 历史，见 README 已知坑），"
expect "关键是：top.c 重新报出 2 条 L1，且与改动无关的 *_indep 过程被复用而非重算"
restore; rm -f leaf.c.bak; trap - EXIT
cd ..

printf '\n\033[1m全部实验结束。\033[0m 对照报告 §13–§17 的数据表核对上面的【实测】输出。\n'
