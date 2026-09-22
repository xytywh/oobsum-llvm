# 在真实代码库上跑一遍：端到端操作手册

> 你手里有一份 `compile_commands.json`，想把 Infer 在自己的业务代码上从头跑到尾。
> 本文把散在 `01`/`02`/`03` 三篇里的步骤串成**一条可以照着敲的流程**，
> 每一步给命令、给预期输出、给"不对劲时看哪里"。
>
> 前四步之后你会拿到几个数字，它们直接决定
> `08-summary2-design/` 里方案 C 的取舍 —— 见最后一节。

---

## 0. 总览

```
① 体检 cdb          → 路径能不能解析？分母是多少？
② 切子集            → 先拿一个目录试水，别一上来全量
③ capture           → 交叉编译工程走 capture-cross-toolchain.sh
④ 能力探针          → ★ 确认目标宏改写没把分析本身改坏
⑤ analyze           → 只开一个 checker，按内存定 --jobs
⑥ diagnose          → 判断成功/失败，失败时定位
⑦ 读结果            → 告警分布、没分析的函数数、★ 跨过程深度分布
⑧ 全量              → 子集数据可接受，再放开
```

**④ 不能跳。** 交叉编译工程要靠 `-U__LP64__ -D__ILP32__=1` 这类人为改写目标宏，
等于让 infer 在一个"半真半假"的目标模型下工作。不先确认基线能力，
后面所有漏报数字都没法解释。

---

## ① 体检 compile_commands.json

```bash
cd <放 compile_commands.json 的目录>
python3 - <<'PY'
import json, os
db = json.load(open("compile_commands.json"))
bad_dir, bad_file, dirs = 0, 0, set()
for c in db:
    d = c["directory"]; dirs.add(d)
    if not os.path.isdir(d):
        bad_dir += 1; continue
    if not os.path.isfile(os.path.normpath(os.path.join(d, c.get("file","")))):
        bad_file += 1
srcs = set(os.path.normpath(c["file"]) for c in db if c.get("file"))
print(f"记录数 {len(db)}，唯一源文件 {len(srcs)}，不同 directory {len(dirs)} 个")
print(f"directory 不存在: {bad_dir}    源文件解析不到: {bad_file}")
print("样例 directory:", sorted(dirs)[:3])
PY
```

**预期**：后两个数都是 0。
**记下「唯一源文件数」** —— 它是后面算 capture 成功率的**正确分母**，不是记录数。

不为 0 怎么办、为什么分母是唯一源文件数：见 `01-whole-program-analysis.md` 开头两节。

---

## ② 切子集（强烈建议）

全量 cdb 动辄上万条，第一次跑先只留一个目录：

```bash
python3 - <<'PY'
import json
SUB = "/usr1/518C10/HERT_BBU/hert_bbu/rosarb/"   # 改成你要试的目录前缀
db = json.load(open("compile_commands.json"))
sub = [c for c in db if c.get("file","").startswith(SUB)]
json.dump(sub, open("cc_subset.json","w"), indent=1)
print(f"{len(db)} → {len(sub)} 条")
PY
```

几十到几百条最合适：跑得快，问题也暴露得出来。

---

## ③ capture

**普通工程**：

```bash
infer capture --compilation-database cc_subset.json --results-dir result/infer-out
```

**交叉编译工程**（目标不是 x86-64 host）：

```bash
cd 06-practice
./capture-cross-toolchain.sh /path/to/cc_subset.json result
```

脚本会自己打覆盖率摘要。**要看的是这三行**：

```
cdb 唯一源文件数:        77   ← 正确分母
capture.db 源文件数:     77
成功 capture（按唯一源文件）: 77 / 77 (100.0%)
```

成功率低于 ~90%，先别往下走，去看 `03-cross-toolchain-capture-fix.md`。
日志里的 `error:` 行数**不是**失败数——`--keep-going` 下 clang 的 error recovery
仍会产出 AST，文件照样落库。

---

## ④ ★ 能力探针（交叉编译工程必做）

```bash
cd ../07-capability-boundary
./run-boundary.sh tp default
```

**预期：`合计 18 条告警，涉及 18 / 22 个文件`**，漏的 4 个固定是
`p05_struct` / `p12_global_noinit` / `p13_fnptr` / `p20_union`
（就是那三条机制性缺口，见 `01-capability-boundary-fp-fn.md` §3）。

- **对得上** → 目标宏改写没影响分析本身，后面的全量结果可以正常解读。
- **对不上** → 多半是 `__SIZEOF_POINTER__` 那条遗留风险在起作用，
  全量结果的漏报要打折看，别当成"代码很干净"。

---

## ⑤ analyze

```bash
# --jobs 按内存定：bufferoverrun 约 2 GB/并发，建议留一半内存余量
#   8 GB   → --jobs 2
#   64 GB  → --jobs 16
#   251 GB → --jobs 32（再高就 CPU 受限了）
infer analyze --results-dir result/infer-out \
  --keep-going \
  --bufferoverrun-only \
  --jobs 16 \
  --progress-bar-style plain 2>&1 | tee analyze.log
```

四个参数都有理由：

| 参数 | 为什么 |
|---|---|
| `--keep-going` | 单个过程崩了不中断整轮 |
| `--bufferoverrun-only` | **别全开**。全开要算 17 个 payload，慢好几倍，而且 Pulse 对数组越界一条都不报 |
| `--jobs N` | 内存峰值大致与并发成正比，这是 analyze 最常见的死法 |
| `--progress-bar-style plain` | 长任务可见进度，不然完全是黑盒 |
| `\| tee analyze.log` | **一定要留日志**，否则出事了没法定位 |

---

## ⑥ diagnose

```bash
cd ../06-practice
./diagnose-analyze.sh <infer-out 路径> <analyze.log 路径>
```

第一节直接给结论（成功 / 部分成功 / 失败 / 无法判断），
成功就跳到它的第 6 节，失败按第 7 节对号入座。

> 机器上装一下 `sqlite3`（`yum install -y sqlite`）判断会更准——
> 没有它就数不了摘要条数，只能靠 `report.json` 推断。

---

## ⑦ 读结果：四个真正有用的数

### 1）告警总数与类型分布

```bash
python3 -c "
import json, collections
r = json.load(open('result/infer-out/report.json'))
c = collections.Counter(x['bug_type'] for x in r)
print('总计', len(r), '条')
[print(f'  {v:6d}  {k}') for k, v in c.most_common()]
"
```

### 2）有多少函数「根本没看」

```bash
grep -c "Skipped large procedure" result/infer-out/logs
grep   "Skipped large procedure" result/infer-out/logs | sort -u | head -20
```

**这个数不为 0，说明你那份"干净"里有一部分是"没看"。**
`--bo-max-cfg-size` 默认 15000，超了整个过程跳过，而且**只写日志、不进 report.json**。
几千行的状态机函数在工业代码里很常见。

### 3）摘要覆盖率

```bash
sqlite3 result/infer-out/capture.db "SELECT COUNT(*) FROM procedures;"
sqlite3 result/infer-out/results.db "SELECT COUNT(*) FROM specs;"
```

达不到 100% 正常：按需分析，没人调用又不自己出告警的过程可能根本没算过。

### 4）跨过程深度分布（v3.6：不再决定方案 C 做不做）

项目已立项、目标定为 2–17 层的多层场景，这一项不再是"要不要做"的判据。它仍然有两个用处：
**看 Infer 在你们代码上看到的深链长什么样**，以及**从告警里挑 ≥ 3 层的样例当 Summary 2.0 的验收语料**。

```bash
KIT=/usr1/BXPlugin/xir/FunctionSummary/infer-research-kit
python3 $KIT/08-summary2-design/probes/t0-3/t03_depth.py infer result/infer-out/report.json --csv depth.csv
# 挑 ≥ 3 层的：depth.csv 里 down 列 ≥ 3 的行，access_site 列就是真正的数组访问点
```

> v3.5 之前这里是一段取 `max(level)` 的内联脚本，它会把"下标值来自被调函数返回值"误算成跨层；
> 脚本版把 **down**（访问点在几层之下）和 **origin**（值在几层之下算出）分开数，见 `08-summary2-design/03-…` T0-3。

---|---|
| 深度 ≥ 2 占 > 30% | 假设 A4 成立，方案 C 的核心前提成立，按 `08-summary2-design/02-effort-and-milestones.md` 的 M0–M5 走 |
| 深度 ≤ 1 占 > 85% | **A4 不成立**。改走那份文档 §5 的便宜选项：只做 ExtentModel + 把 inline 深度从 2 提到 4–5，**15–20 人日拿约 70% 收益** |

---

## ⑧ 放开到全量

子集的四个数都可接受，再把 `cc_subset.json` 换成完整的 cdb 重跑 ③⑤⑥⑦。

全量要额外留意：

* **磁盘**：`infer-out` 会长到几个 GB。77 个文件就 16 MB，按比例估一下。
* **耗时**：留够时间，用 `--progress-bar-style plain` 看进度。
* **内存**：并发照样按 ~2 GB/并发 估。
* **增量**：以后每次改动不用全量重来，见报告 §17 与 `02-observability-handbook.md`。
  但记住一条实测教训：**CI 里要断言的是"顶层告警是否重现"，不是"失效了几条"**——
  失效条数不是稳定可断言的指标。

---

## 一页速查

```bash
# ① 体检
python3 -c "import json,os;c=json.load(open('compile_commands.json'));print(len(c),'条 /',len(set(os.path.normpath(e['file']) for e in c)),'个唯一源文件')"

# ③ capture（交叉编译）
cd 06-practice && ./capture-cross-toolchain.sh /path/to/cc.json result

# ④ 探针        预期 18/22
cd ../07-capability-boundary && ./run-boundary.sh tp default

# ⑤ analyze
infer analyze --results-dir result/infer-out --keep-going --bufferoverrun-only \
  --jobs 16 --progress-bar-style plain 2>&1 | tee analyze.log

# ⑥ diagnose
cd ../06-practice && ./diagnose-analyze.sh result/infer-out analyze.log

# ⑦ 四个数（脚本里第 6 节会直接给前两个）
grep -c "Skipped large procedure" result/infer-out/logs
./observe.sh result/infer-out        # 全部可观测点 dump 到 observation-report.txt
```
