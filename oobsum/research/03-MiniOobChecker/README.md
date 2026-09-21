# MiniOobChecker —— 基于函数摘要的最小越界检查器

对应报告 **§19**。在 `facebook/infer` commit `3d38229b04b649024fee065621f6a4c838f6d029` 上
**编译通过并实测跑通**，自建二进制 `infer --version` → `Infer version v1.3.0-3d38229b`。

## 它是干什么的

它存在的唯一目的，是**用最小代价证明一件事**：在 Infer 里复用它的跨过程框架、
只写自己的 Domain 和 Transfer Function，是可行的，成本是几百行。

`read` 被独立分析一次并生成摘要；`foo` 分析到 `read(...)` 时**读取摘要**，
而不是 inline 它的函数体 —— 处理 `Call` 的代码里没有任何一行加载被调者的 `Procdesc`
或遍历其 CFG，唯一的跨过程操作是框架注入的 `analyze_dependency`。

摘要内容是一组安全条件 `0 <= idx < extent(base)`，其中 `idx` 可以是常量或"形参 #k + 常量"，
`base` 可以是已知长度的局部数组或形参 #k。这是报告 §12.1 里 Infer 真实的
`ArrayAccessCondition` + `ConditionSet.{subst,for_summary}` 的教学级最小实现。

## 文件

```
src/MiniOobDomain.mli / .ml     抽象域：Idx（常量 / 形参+常量 / Top）、Base（局部数组 / 形参）、
                                Obligation（安全条件）、summary；共 442 行含注释
src/MiniOobChecker.mli / .ml    TransferFunctions + checker 入口
patch/0001-register-minioob-checker.patch
                                9 个文件共 37 行注册改动
demo/demo.c                     §19.5 的两层用例
demo/chain3.c                   §19.6 的四层条件传播用例
demo/deep.c                     §19.7 的 20 层深链 + 阴性对照
```

## 安装

**最省事**：走 `04-setup/scripts/`（`./run-all.sh` 或逐步到 `50-install-minioobchecker.sh`），
它会把 infer 钉在调研基线、核对版本、打补丁、增量构建、跑 demo，全部自动。

手动安装：

```bash
cd <infer>
git checkout 3d38229b04b649024fee065621f6a4c838f6d029      # ★ 先切到调研基线
cp /path/to/src/MiniOob*.ml /path/to/src/MiniOob*.mli infer/src/labs/
git apply --check /path/to/patch/0001-register-minioob-checker.patch && \
git apply         /path/to/patch/0001-register-minioob-checker.patch
```

> **为什么一定要先切到基线**：补丁是 `git diff` 生成的**上下文补丁**，靠被改文件周边的几行定位。
> 在基线上它一定能打上；在更新的 commit 上能不能打上，取决于上游在这之间有没有改动这 9 个文件。
> 2026-09-21 实测：main 比基线多 2 个 commit，补丁涉及的 9 个文件一个没动，所以能直接打上 ——
> 但这只是"恰好"，不是保证。

### 在其他 commit 上安装

想在更新的 infer 上用它（比如验证它在最新 main 上是否还成立）：

```bash
# 1) 先看补丁涉及的文件在基线之后被上游改过哪些
git diff --stat 3d38229b04b649024fee065621f6a4c838f6d029 HEAD -- \
    $(grep '^+++ b/' /path/to/patch/0001-register-minioob-checker.patch | sed 's|^+++ b/||')

# 2) 输出为空 → 直接 git apply；有输出 → 试三方合并
git apply --3way /path/to/patch/0001-register-minioob-checker.patch
```

`--3way` 在上游只改了补丁**周边**（不相邻）的行时能自动合并；
改到**紧挨着插入点**的行就会冲突，这时需要手工移植。好在这 37 行改动的模式非常单一
—— **每个文件都是往一个枚举或列表里加一项 `MiniOob` / `mini_oob`**，照着相邻的
`LithoRequiredProps` / `litho_required_props` 那一项抄一行即可：

| 文件 | 加什么 |
|---|---|
| `backend/Payloads.ml` / `.mli` | 记录字段 `mini_oob`，以及 `all_fields` / `empty` / `freeze` / 加载处各一项（共 6 处） |
| `backend/registerCheckers.ml` | 登记 `{checker= MiniOob; callbacks= [(interprocedural Payloads.Fields.mini_oob MiniOobChecker.checker, Clang)]}` |
| `base/Checker.ml` / `.mli` | 枚举成员 `MiniOob` 及其 `config`（名字、说明、支持的语言、默认关闭） |
| `base/IssueType.ml` / `.mli` | issue 类型 `mini_oob`（报告里显示为 `MINI_OOB`） |
| `base/PayloadId.ml` / `.mli` | payload 编号 `MiniOob`（`specs` 表的新列由它自动生成） |

脚本 `50-install-minioobchecker.sh` 已经把这套流程自动化：
`--check` → `--3way` → 失败则回滚干净并告诉你是哪几个文件被上游改过。

**不需要新建或修改任何 dune 文件** —— `infer/src/labs/` 已经是库 `Labs`，
而 `infer/src/backend/dune` 已经 `-open Labs` 并把 `Labs` 列入 `libraries`。
`specs` 表的新列 `MiniOob` 由 `PayloadId.database_fields` 自动生成（列数 21 → 22）。
把代码放进 `labs/` 而不是新建库目录，是这个方案比常规做法少一半改动的原因。

## 构建

```bash
eval $(opam env)
./autogen.sh
./configure --disable-rust-analyzers --disable-java-analyzers --disable-erlang-analyzers \
            --disable-hack-analyzers --disable-python-analyzers --disable-swift-analyzers
make -j$(nproc) BUILD_MODE=default infer
./infer/bin/infer --version
```

**加速提示**（把构建时间从数小时压到分钟级）：不要从源码编译 LLVM/Clang，
直接复用官方预编译包里已经编译好的那套：

```bash
cd <infer>/facebook-clang-plugins/clang
ln -sfn <解压出来的>/infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install install
SHASUM="sha256sum" ./setup.sh -r      # -r = --only-record-install，写 installed.version
SHASUM="sha256sum" ./setup.sh -c && echo "clang 被认为已安装"
```

**一个坑**：`--disable-rust-analyzers` **不足以**让 dune 跳过 Rust 代码 ——
`infer/src/integration/dune.in` 无条件依赖 `charon` 库。
如果你们的机器能访问 gitlab.inria.fr，`opam install charon` 即可，不用管这条；
如果不能，就得额外把 Rust 前端从 dune 构建里摘掉（我们调研时就是这么绕过去的，
但那是一次性 hack，故意没有放进这个包）。

## 运行与预期输出

```bash
cd demo
<infer>/infer/bin/infer run --mini-oob -o out -- gcc -c demo.c
```

```
[MiniOOB] Analyze read
[MiniOOB]   open condition 0 <= arg2 < extent(arg0) at line 1, column 41
[MiniOOB] Generate Summary read: 0 <= arg2 < extent(arg0) at line 1, column 41
[MiniOOB] Analyze foo
[MiniOOB] Load Summary read (1 open condition(s))
[MiniOOB] Apply Summary read
[MiniOOB]   discharged (VIOLATED): 0 <= 20 < extent(a[0..10)) at line 1, column 41
[MiniOOB] Generate Summary foo: (no open safety conditions)

demo.c:5: error: Mini Oob(MINI_OOB)
  Out-of-bounds access inside `read`: 0 <= 20 < extent(a[0..10)) at line 1, column 41.
```

注意 `Analyze read` **严格早于** `Analyze foo` 且只出现一次 —— 这就是自底向上按需分析。

`chain3.c` 会看到条件沿调用链被逐层代换：
`0 <= arg1 < extent(arg0)` → `arg1+5` → `arg1+7` → 最后在 `entry` 判定为 `0 <= 27 < extent(a[0..10))`。

`deep.c` 是 20 层 + 阴性对照：偏移被符号累加 19 次，0.31 秒，1 个真阳性 0 个假阳性。

## 验证摘要确实落盘了

```bash
# 官方工具，可读
infer debug -o out --procedures --procedures-name --procedures-summary \
            --procedures-summary-skip-empty --select all

# 直接看 SQLite 里的 blob（开头 8495a6be 是 OCaml Marshal 的魔数）
python3 - <<'EOF'
import sqlite3; c=sqlite3.connect('out/results.db')
for uid,b in c.execute('select proc_uid, MiniOob from specs'):
    print(uid, len(b) if b else 0, b[:4].hex() if b else '')
EOF
```

## 已知局限（教学级实现，刻意保持最小）

不处理：循环（`widen` 就是 `join`，循环变量一变即 Top —— 会漏报但不会误报）、`malloc`、
多级指针、结构体字段、全局变量、非线性下标、函数指针；`Idx` 只有三态，没有区间 `[lo,hi]`。

把它补成生产级，需要的正是 Infer 真实用的那套：`Itv` + `Bound`（含符号与非线性项）+
`ArrayBlk` + 阈值 widening。**这段"从几百行到生产级"的距离，就是报告 §27 要量化的自研成本。**
