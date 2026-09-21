# 用 compile_commands.json 做全量跨文件分析

> 回答的问题：**"我有一个全量业务代码的 compile_commands.json，怎么做全量分析？
> 逐个文件分析的话跨文件信息不就丢了吗？"**

## 一句话答案

**你的直觉是对的，但 Infer 的 capture 和 analyze 是两个独立阶段，只有 capture 是按文件走的。**

* `infer capture` —— 按 `compile_commands.json` **逐条编译命令**翻译每个文件（每个文件用它自己的
  `-I` / `-D` / `-std`），结果**全部累积进同一个 `infer-out/capture.db`**。
* `infer analyze` —— 从这个**统一的** `capture.db` 出发做**全量分析**。
  它不知道也不关心"文件"边界：分析到 `Call` 就按需去 `capture.db` 里找被调过程的 CFG，
  不管那个过程来自哪个文件。跨文件信息在这一步**完全保留**。

所以正确姿势是：**capture 按文件，analyze 一次全量**。
真正会丢信息的是"每个文件一个 `infer-out`"，那才是逐文件分析。下面有实测对照。

---

## 开跑之前：先体检 compile_commands.json

**90% 的"跑不起来"都出在 `directory` 字段上。** 它必须是一个**当前机器上真实存在的绝对
目录**——infer 对每条记录会先 `chdir` 进 `directory`，再在那里执行编译命令，
所以命令里的 `-I../include`、`../src/x.c` 这类相对路径全都是相对它解析的。

典型翻车场景：`compile_commands.json` 是在构建机上生成的，`directory` 指向
`/build/ci/xxx/obj`；拿到本机后那个目录不存在 → infer 直接：

```
WARNING: Failed to resolve file .../build/../src/access.c with "No such file or directory"
External Error: Error running compilation for 'build/../src/access.c':
chdir(.../build): No such file or directory
```

注意 `Starting translating 3 files` 照样会打印——**它只表示"读到了 3 条记录"，
不代表翻译成功**。所以先跑这段体检，别等 infer 报栈：

```bash
python3 - <<'PY'
import json, os
db = json.load(open("compile_commands.json"))
bad_dir, bad_file, dirs = 0, 0, set()
for c in db:
    d = c["directory"]; dirs.add(d)
    if not os.path.isdir(d):
        bad_dir += 1; continue
    f = c.get("file", "")
    if not os.path.isfile(os.path.normpath(os.path.join(d, f))):
        bad_file += 1
print(f"记录数 {len(db)}，不同 directory {len(dirs)} 个")
print(f"directory 不存在: {bad_dir}    源文件解析不到: {bad_file}")
print("样例 directory:", sorted(dirs)[:3])
PY
```

两个数都为 0 才往下走。不为 0 有三种修法：

| 情况 | 修法 |
|---|---|
| 构建目录能重建 | 在本机把那个路径建出来（`mkdir -p`），或干脆重跑一次 CMake 生成新的 cdb |
| 路径前缀整体搬家了 | 批量改写 `directory`（和 `file`，如果它也是绝对路径）的前缀 |
| 只是目录空了但路径在 | `mkdir -p` 即可——infer 只需要能 `chdir` 进去，不需要里面有 `.o` |

批量改写前缀：

```bash
python3 - <<'PY'
import json
OLD, NEW = "/build/ci/xxx", "/usr1/mywork"      # 改成你的
db = json.load(open("compile_commands.json"))
for c in db:
    for k in ("directory", "file", "output"):
        if k in c and isinstance(c[k], str):
            c[k] = c[k].replace(OLD, NEW)
json.dump(db, open("compile_commands.json", "w"), indent=2)
print("改写完成", len(db), "条")
PY
```

### 还有一个陷阱：cdb 记录数 ≠ 源文件数

体检脚本里的"记录数"是 cdb 的**条目数**，不是**唯一源文件数**。
同一个 `.c` / `.cpp` 常常以多组 flag 出现在 cdb 里（不同 build target、不同 `-D`），
而 **infer capture 按源文件去重落库**，`source_files` 表只落一行。

**后果**：如果你用 cdb 记录数当分母算 capture 成功率，会把"重复条目"误判成"失败"。

```bash
# 正确分母
python3 -c "import json,os;c=json.load(open('compile_commands.json'));print(len(set(os.path.normpath(e['file']) for e in c)))"
# 实际落库
sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM source_files;"
```

【实测数据·真实工程】使用方在一个 10640 条记录的 RTOS 工程上踩过这个坑：
按记录数算是"96.2% 成功、408 个失败"，按唯一源文件数算是
**10232 / 10232 = 100% 落库**——那 408 条差值全是重复条目。
完整过程见 `03-cross-toolchain-capture-fix.md` §7。

> **本包的 demo 工程**：`06-practice/gen-compile-commands.sh` 已经把上面这些都做了——
> 改写 `directory` 为你解压后的真实路径、`mkdir -p build/`、再自检一遍。
> **解压后第一件事就是跑它**，跑完它会把下一步命令打印给你。

---

## 命令

### 最短路径

```bash
cd <你的工程根目录>          # compile_commands.json 里的 directory 字段要能解析到
infer run --compilation-database compile_commands.json
```

`infer run` = `infer capture` + `infer analyze` + `infer report`。

### 分开跑（推荐，便于观察中间产物、便于增量）

```bash
infer capture  --compilation-database compile_commands.json
infer analyze  --bufferoverrun-only                      # 或 --pulse，或默认全开
infer report                                             # 重新出报告，不重新分析
```

### 常用变体

```bash
# 只分析 compile_commands.json 的一个子集（先拿一个模块试水，强烈建议）
infer capture --compilation-database-escaped subset.json

# 并行（大工程必开）
infer analyze --jobs 32 --scheduler callgraph

# 挑 checker
infer analyze --bufferoverrun-only              # 只跑越界（BufferOverrun）
infer analyze --pulse-only                      # 只跑 Pulse
infer analyze                                    # 默认启用的全部 checker
infer analyze --mini-oob                        # 我们自己写的（需自建二进制）

# 换低漏报档位（把"符号条件只传不报"关掉）
infer analyze --bufferoverrun-only --bo-debug 4 \
  --enable-issue-type BUFFER_OVERRUN_L4 \
  --enable-issue-type BUFFER_OVERRUN_L5 \
  --enable-issue-type BUFFER_OVERRUN_U5
```

### 如果 `compile_commands.json` 里是 `command` 字符串而不是 `arguments` 数组

Infer 两种都吃。`--compilation-database` 与 `--compilation-database-escaped` 的区别是
**参数里的引号/转义是否已经按 shell 规则转义过**：

* `--compilation-database`：`arguments` 是数组，或 `command` 是**未经额外转义**的命令行 → 用这个。
* `--compilation-database-escaped`：`command` 字符串里含 `\"` 这类**已转义**内容（Buck / 某些构建系统导出的）→ 用这个。

拿不准就先用 `--compilation-database`，capture 阶段报"translating 0 files"或大量失败再换另一个。

---

## 实测：全量 vs 逐文件（这组数据就是在回答你的疑问）

`06-practice/demo-project/` 是一个刻意设计的三文件工程，**越界只有把三个文件合起来才看得见**：

```c
/* include/buf.h */
#define BUF_CAP 10
struct Buf { int len; int data[BUF_CAP]; };

/* src/access.c  —— 叶子，这里看不出任何问题 */
int buf_get(struct Buf *b, int i) { return b->data[i]; }

/* src/middle.c  —— 下标偏移 IDX_BIAS，由它自己的 -DIDX_BIAS=12 传入 */
int mid_fetch(struct Buf *b, int k) { return buf_get(b, k + IDX_BIAS); }

/* src/main.c    —— 只有这里才知道 b 只有 10 个元素、下标是 5 */
int main(void) { struct Buf b; return mid_fetch(&b, 5); }
```

`compile_commands.json` 里三条命令**各带不同的 `-I` / `-D`**（`middle.c` 独有 `-DIDX_BIAS=12`）。

### 全量分析

```bash
infer capture --compilation-database compile_commands.json
infer analyze --bufferoverrun-only
```

**实测输出：**

```
src/main.c:5: error: Buffer Overrun L1(BUFFER_OVERRUN_L1)
  Offset: 17 Size: 10 by call to `mid_fetch`.
  3. int main(void) {
  4.     struct Buf b;
  5.     return mid_fetch(&b, 5);
                ^
Found 1 issue
```

`Offset: 17` = `5`（来自 main.c）`+ 12`（来自 **middle.c 自己的 `-DIDX_BIAS=12`**）。
`Size: 10` 来自 buf.h 的 `BUF_CAP`。**三个文件、三套编译参数的信息在分析阶段被正确合并了。**

### 逐文件分析（每个文件一个 infer-out）

```
access.c 单独分析 -> 0 条报告
middle.c 单独分析 -> 0 条报告
main.c   单独分析 -> 0 条报告
```

**全丢。** 这正是你担心的情况——但它只会在你刻意给每个文件建独立 `infer-out` 时发生。

### 跨文件摘要是怎么串起来的（真实摘要）

```
int buf_get(Buf* b, int i)
BufferOverrunChecker: Safety conditions:
                      { i < 10 at src/access.c:3:44 }
summary_loads=                                  ← 叶子，不依赖别人

int mid_fetch(Buf* b, int k)
BufferOverrunChecker: Safety conditions:
                      { k + 12 < 10 at src/access.c:3:44 },      ← 12 来自 middle.c 的 -D
                      { (k + 12):signed32 at src/middle.c:6:46 }
summary_loads= buf_get                          ← ★ 跨文件依赖边
type environments from sources= include/buf.h   ← ★ 头文件的类型环境依赖也记了

int main()
BufferOverrunChecker: Safety conditions: { }    ← 全部判定完毕 → 转为报告
summary_loads= mid_fetch                        ← ★ 跨文件依赖边
ERRORS: Buffer Overrun L1
```

注意 `mid_fetch` 的条件里，**源码位置仍然是 `src/access.c:3:44`** —— 即 `b->data[i]` 那次真正的
数组访问。这就是为什么最终报告能精确归因到根因所在的文件，而不是只报在调用点。

---

## MiniOobChecker 在全量工程上同样可用

用自建二进制（`03-MiniOobChecker/README.md` 里构建出来的那个）：

```bash
<自建infer>/infer/bin/infer capture --compilation-database compile_commands.json
<自建infer>/infer/bin/infer analyze --mini-oob
```

`06-practice/demo-project-plain/` 是同构但用裸数组（不用 struct 字段）的版本，实测：

```
[MiniOOB] Analyze leaf_get
[MiniOOB]   open condition 0 <= arg1 < extent(arg0) at line 2, column 38
[MiniOOB] Analyze mid_fetch
[MiniOOB] Load Summary leaf_get (1 open condition(s))
[MiniOOB]   still open, propagating: 0 <= arg1+12 < extent(arg0) at line 2, column 38
[MiniOOB] Analyze main
[MiniOOB] Load Summary mid_fetch (1 open condition(s))
[MiniOOB]   discharged (VIOLATED): 0 <= 17 < extent(a[0..10)) at line 2, column 38
src/main.c:2: error: Mini Oob(MINI_OOB)
```

**跨三个文件、`+12` 来自另一个文件的宏 —— 全量分析下 MiniOob 一样工作。**

> **诚实说明**：MiniOob 在原版 demo-project（`b->data[i]`，struct 字段数组）上**不报**，
> 实测输出是 `Generate Summary buf_get: (no open safety conditions)`。
> 因为它只认 `Load {e = Lindex (Lvar/Var, idx)}`，不认 `Lfield` 套 `Lindex`。
> 这是它作为教学级实现的已知局限（见 `03-MiniOobChecker/README.md`），不是全量分析的问题。

---

## 上你们真实代码库前的建议

1. **先切一个子集试水。** 从 `compile_commands.json` 里挑一个模块（几十到几百个文件）单独导出一份
   再 capture，确认翻译成功率与耗时，再上全量。
   ```bash
   python3 -c "
   import json
   d=json.load(open('compile_commands.json'))
   sub=[c for c in d if '/module_a/' in c['file']]
   json.dump(sub, open('cc-module-a.json','w')); print(len(sub),'files')"
   infer capture --compilation-database cc-module-a.json
   ```

2. **确认 capture 成功率**（capture 失败的文件在分析阶段等于不存在，是最隐蔽的漏报来源）：
   ```bash
   python3 -c "
   import sqlite3,json
   n_cc=len(json.load(open('compile_commands.json')))
   n_db=sqlite3.connect('infer-out/capture.db').execute('select count(*) from source_files').fetchone()[0]
   print(f'compile_commands 里 {n_cc} 条，capture.db 里 {n_db} 个源文件')"
   grep -ic "error" infer-out/logs | head -1
   ```
   **翻译失败通常是编译器扩展语法**（你们用的是 Huawei 内部 LLVM 14 fork，Infer 自带的是
   定制 Clang 21.1.6，两边对内部扩展的支持不一定一致）—— 这是本次调研判定
   "不建议把生产越界主链路迁移到 Infer" 的工程理由之一，上真实代码时务必先量化这个成功率。

3. **并行**：`--jobs $(nproc) --scheduler callgraph`。`callgraph` 调度器按过程而非按文件派活，
   官方文档原文 "Usually faster than file"。

4. **增量**：见 `05-experiments/README.md` 的"已知坑"。核心三条——
   默认 `infer analyze` 会先清空摘要；`--incremental-analysis` 必须配 `--changed-files-index`；
   这个 index 同时决定分析范围，**必须覆盖需要出结论的全部文件**，不能只写变更文件。

5. **版本间比对**（你们做 CI 应该会需要）：
   ```bash
   infer reportdiff --report-previous old/report.json --report-current new/report.json
   # 产物在 infer-out/differential/{introduced,fixed,preexisting}.json
   ```
   `report.json` 每条告警自带 `hash` 与 `key` 字段可用于去重，含义见
   `06-practice/02-observability-handbook.md`。

6. **磁盘**：`capture.db` 存了每个过程的 CFG，`results.db` 存摘要。真实工程上这两个库会到 GB 级，
   评估存储时按"过程数 × 平均 CFG/摘要大小"估算（本包 demo 里 3 个过程 ≈ 160 KB + 224 KB，
   其中绝大部分是 SQLite 页开销，不要按这个比例外推）。
