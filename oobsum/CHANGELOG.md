# CHANGELOG

## v3.0 — 2026-09-21

把"源改成 git 之后才会暴露"的兼容性问题自动化掉。实测在 `fpath` 上踩到。

### 新增

* **`04-setup/scripts/patch_git_source_quirks.py`** —— 新增一个"后改写补丁"环节，
  由脚本 20 自动调用（也可单独跑）。

  **为什么需要它**：opam 有个内置变量 `dev`——**源是 git 检出时为 true，
  是发布压缩包时为 false**。我们为绕开受限网络把大批包的源从压缩包改成了 git，
  这些包的 `dev` 就从 false 变成 true，**激活了上游那条平时走不到、也没人测过的构建分支**。
  这不是改写改错了，是那条分支本来就没被验证过。

  目前收录一条（实测）：`fpath` 上游写的是
  `build: [[ ... "--dev-pkg=true" {dev} ]]`，而它 git tag 里自带的老 topkg
  只认空格分隔的 `--dev-pkg true`，于是报
  `pkg.ml: [ERROR] key --dev-pkg=true: No value specified.`，
  装不上——而 `fpath` 是 infer 的必需依赖。
  改成 `"--dev-pkg" "%{dev}%"`（两个参数，语义等价）即可。

  脚本只改**源已经是 git**的包（压缩包源的 `dev=false`，那条分支根本走不到），
  幂等可反复执行。全仓库扫描 19032 个 opam 文件约 2 秒，
  实际命中只有 `fpath` 和 `notty` 两个。

### 变更

* `20-prepare-opam-repo.sh` 新增第 4 步「修正『源改成 git 后才会暴露』的兼容性问题」，
  步骤数 7 → 8。位置在两个 URL 改写之后、`opam init` 之前。
* `30-install-deps.sh` 的失败处理新增一条识别：日志里出现
  `dev-pkg=true: No value specified` 时，直接打出三行修复命令
  （跑 `patch_git_source_quirks.py` → `opam update default` → 重跑 30），
  并说明老环境为什么需要手动补这一次。

### 文档

* `04-setup/01-restricted-network-build.md`
  - 「常见故障」里 `fpath --dev-pkg` 那条重写：把根因说清楚
    （**是改成 git 源的副作用，不是包本身坏了**），并给出 v3.0 的自动修复与老环境的手动补法。
  - 脚本表里 20 的流程加上 `patch_git_source_quirks.py` 这一环。

---

## v2.9 — 2026-09-20

修一个把 40 分钟整轮跑废的参数错误，并加一道能提前几秒抓住同类问题的闸门。

### 修复

* **`30-install-deps.sh` 用了不存在的 `--dl-jobs`，opam 直接拒绝执行。**
  v2.8 为了压下载并发，给 `opam install` 加了 `--dl-jobs`——但那**不是
  `opam install` 的选项**（只有 `opam init` / `opam update` 这类有），
  opam 2.4 报 `unknown option '--dl-jobs', did you mean '-d'?` 然后退出。
  正确做法是环境变量：`export OPAMDLJOBS=$DLJ`，它对所有 opam 子命令都生效；
  同时 `opam option --global dl-jobs=$DLJ` 写进配置，手工敲 opam 时也是同一个并发度。
* **`_progress.sh` 里的 CA 证书自检，v2.8 的变更说明写了、文件却没改。**
  这次按使用方给的版本真正落盘：检查 `SSL_CERT_FILE` / `CURL_CA_BUNDLE` /
  `REQUESTS_CA_BUNDLE` 三个变量，指向的文件不存在就 unset 并打印原值，
  回退到系统证书。因为它在 `_progress.sh` 里，**10/20/25/30/40/50 六个脚本全都受益**
  （v2.6 那版只有脚本 10 做了这件事，后面几个照样会踩）。
  v2.8 的条目已标注更正。

### 新增

* **`30-install-deps.sh` 装依赖前先 `opam install --dry-run` 试算。**
  几秒钟，能在开跑 40 分钟之前抓到两类问题：
  ① **参数写错**（就是上面那个 `--dl-jobs`，整轮白跑）；② 依赖解不开（缺包、版本冲突）。
  没过就打印错误尾部 20 行并退出，完整日志留在 `$WORK/.deps-dryrun.log`；
  确认是 dry-run 特有的问题可以 `SKIP_DRYRUN=1 ./30-install-deps.sh` 跳过。
* 顺手把全部脚本里的 opam 调用过了一遍，确认其余参数
  （`--bare` / `--disable-sandboxing` / `--no-setup` / `--no-depexts` / `--deps-only` /
  `--dry-run` / `-j` / `pin add --no-action` / `switch create --empty` / `option --global`）
  都是 opam 2.x 的合法选项。

---

## v2.8 — 2026-09-20

装依赖时大面积 `HTTP 403`（而手工 `git clone` 是通的）—— 给出诊断与两种解法。

### 新增

* **`04-setup/scripts/25-prefetch-git-sources.sh`** —— 条件步骤，
  只在脚本 30 报一堆 `Could not synchronize ... "git fetch -q" ... HTTP 403` 时才需要。
  做法：**我们自己串行 clone**，opam 就不用联网了。
  1. 先用三个探针仓库（`janestreet/ppx_base` / `ocaml/num` / `dbuenzli/mtime`）
     确认 git 到 github 到底通不通 —— 一个都不通就直接退出并给排查命令，不做无用功。
  2. 从 infer 的 lock 文件解析出要拉哪些 git 源（含版本对应的 opam 文件路径）。
  3. **串行** clone 到 `$WORK/git-mirror/`，带重试与退避，逐个打
     `[ 12/114] ppx_base.v0.17.0  janestreet/ppx_base#v0.17.0 ... ✓ (1s)`；
     `--branch <tag>` 拿不到时自动退化成全量 clone + checkout（ref 是 commit sha 的情况）。
  4. 把 opam 仓库里对应的 `src:` 改写成 `git+file:///<本地镜像>#<ref>`，再 `opam update`。
  5. 拉不到的仓库单独列到 `$WORK/.prefetch-failed.txt`，并说明剩下只可能是
     "代理按仓库白名单放行"，那种情况只能加白名单或改用官方预编译 infer。
  已在本地实测跑通（4 个包：clone → 改写 → 校验镜像 commit 正确）。

### 变更

* **`30-install-deps.sh` 下载并发默认压到 4**（`--dl-jobs`，可用 `DLJ=1` 再降）。
  opam 默认按 CPU 核数并发 git fetch —— 实测环境有 47 核，
  等于同时 47 个 fetch 打到 github，被代理限流成 403 的概率很高。
* `30-install-deps.sh` 失败时**不再只是退出**：统计日志里 `Could not synchronize` 的条数，
  若 > 0 则明确告诉你"这是取源失败、不是编译失败"，并给三步排查：
  ① 手工 clone 一个验证连通性 → ② 跑 `25-prefetch-git-sources.sh` → ③ `DLJ=1` 再试。
  完整日志留在 `$WORK/.deps-install.log`。
* `30-install-deps.sh` 开头会检测 `$WORK/git-mirror` 是否存在，
  存在就提示"基本不会走网络"，不存在就预告 403 时该怎么办。
* ~~并入使用方对 `_progress.sh` 的改进~~ —— **这条当时写了但没真正落到文件里，
  v2.9 才补上。** 见 v2.9 的说明。

### 文档

* `04-setup/01-restricted-network-build.md`
  - 脚本表新增 `25-prefetch-git-sources.sh` 一行，并标明它是**条件步骤**。
  - 「常见故障」新增两行：`HTTP 403` 的三种成因与按序排查法、
    `FileNotFoundError: infer.opam.locked` 的归因（v2.7 已修）。
  - 脚本清单处加注：正常情况跳过 25，只有 30 报 403 时才回头插进来跑。

---

## v2.7 — 2026-09-20

三处实测踩出来的修复，外加一份把散落步骤串起来的端到端手册。

### 修复

* **`04-setup/scripts/20-prepare-opam-repo.sh`：没 clone infer 源码就去读它的 lock 文件。**
  `populate_opam_cache.py` 要读 `<infer>/opam/infer.opam.locked` 才知道该预填哪些包，
  而 infer 源码原本是**脚本 30** 才 clone 的 —— 顺序又错了一次，
  实测直接 `FileNotFoundError` 崩在第 5 步。
  - 把 clone infer 提到脚本 20（很快，而且让 20 自此自洽、不依赖 30）。
  - `populate_opam_cache.py` 改为读 `LOCK` 环境变量，**拿不到时不再崩**：
    退化成只预填编译器相关的包，并打印清楚为什么、影响是什么。
* **`06-practice/diagnose-analyze.sh`：把别的进程的 OOM 记成 infer 的（假警报）。**
  旧版用 `out of memory` 做关键字，实测把 `Kill process 15713 (codeagent)`
  这五行完全无关的记录报成了 analyze 的死因。
  现在只匹配进程名含 `infer`/`clang`/`InferAnalyze`/`InferClang` 的行；
  其它进程的 OOM 单独提一句"和本次无关，忽略"，不下结论。
* **`06-practice/diagnose-analyze.sh`：没装 sqlite3 时把成功的运行误判成失败。**
  旧版只看 `specs` 条数（要 sqlite3），拿不到就走"失败"分支，
  于是一次完全正常的 analyze 被判成"没写出任何摘要"。
  现在判定有三级回退：`specs` 条数 → `results.db` + `report.json` 存在且日志干净
  → 都没有才算失败；真拿不准时输出"无法判断"而不是"失败"。

### 变更

* **`diagnose-analyze.sh` 从"失败诊断"扩成"跑完就该跑的检查"**：
  - 开头先给**结论**（成功 / 部分成功 / 失败 / 无法判断）与下一步该看哪节。
  - `--jobs` 建议**按实际内存和核数算**（~2 GB/并发，留一半余量），
    不再是写死的 4 —— 实测环境从 8 GB 到 251 GB 都有，写死毫无意义。
  - 新增第 6 节「分析成功后该看什么」：直接打出告警类型分布、
    `Skipped large procedure` 计数，并给出跨过程深度分布（T0-3）的现成脚本。
  - 摘要覆盖率一栏加说明：达不到 100% 是正常的（按需分析）。

### 新增

* **`06-practice/04-end-to-end-runbook.md`** —— 回答"这套流程有没有文档指导"。
  把原本散在 `01`/`02`/`03` 三篇里的步骤串成一条可以照着敲的流程：
  ① 体检 cdb → ② 切子集 → ③ capture → ④ **能力探针（不能跳）** →
  ⑤ analyze → ⑥ diagnose → ⑦ 读四个真正有用的数 → ⑧ 放开全量。
  每步给命令、预期输出、不对劲时看哪里；末尾附一页速查。
  其中第 ⑦ 步的**跨过程深度分布直接对应 T0-3**，
  给出了"深度 ≥2 占比 >30% / ≤1 占比 >85%"两种结果分别该怎么决策。

---

## v2.6 — 2026-09-20

修一个会让受限环境构建**静默失败**的顺序 bug，并给所有长任务加上进度输出。

### 修复（重要）

* **`04-setup/scripts/20-prepare-opam-repo.sh` 的步骤顺序错了，导致 `opam init` 被整个跳过。**
  旧版顺序是 clone → 改写 → **预填下载缓存** → `[ -d "$OPAMROOT" ] || opam init`。
  预填那一步会建出 `$OPAMROOT/download-cache/`，于是 `[ -d "$OPAMROOT" ]` 成立，
  **`opam init` 被跳过**；紧接着的 `opam update` 对着一个没有 `config` 的半成品 root 报
  `exists, but does not appear to be a valid opam root`。
  更糟的是末尾的 `完成。OPAMROOT=...` 照样打印——因为 `opam update ... | tail -2`
  这个管道让 `set -e` 失效——所以**看起来像成功了**。
  - 新顺序：clone → 改写 → **`opam init`** → 预填缓存 → `opam update` → **自检**。
  - 对已经踩进坑里的目录做**自动修复**：把已预填的 `download-cache` 挪走、
    `opam init` 完再挪回来，不用重新下载。
  - 末尾新增自检：opam root 是否有效、仓库能看到多少个包、缓存预填了多少个文件；
    任何一项不过就 **exit 1**，不再假装成功。
* **两个改写脚本无法分辨「重复执行」和「正则失效」。**
  旧版都只打一行 `rewrote url blocks in 0 / 19125`，0 到底是好是坏看不出来。
  现在会先打一张**源分布直方图**（github 压缩包 / releases / erratique / 其它主机各多少），
  跑完再自检：
  - 剩余 github 压缩包源 ≤ 50 → ✓（含「之前已改写过」这种情况）
  - 一个都没改写但还剩一堆 → ✗ 并 `exit 3`，同时提示把直方图贴出来
* **并入使用方在真实环境的修复**：`10-install-opam.sh` 清理失效的
  `SSL_CERT_FILE` / `CURL_CA_BUNDLE`（环境里残留 PyInstaller 的临时 CA 路径
  `/tmp/_MEIxxxx/certifi/cacert.pem`，curl 报 `error setting certificate file`，
  报错长得像网络问题，很难定位）。优先换系统 CA 包，找不到就 unset。

### 新增

* **`06-practice/diagnose-analyze.sh`** —— `infer analyze` 在全量工程上失败时先跑它，
  再决定要不要自己编译 infer。六节输出：环境（磁盘/内存/核数）、capture 结果还在不在、
  已写出多少摘要（崩在前段还是后段）、日志里致命错误的分类计数与第一个异常栈、
  `infer-out` 里的线索文件、dmesg 里的 OOM 证据；最后按 ①~⑥ 给出对号入座的下一步。
  **要点：analyze 报错基本不需要从源码编译 infer 才能定位**——
  官方预编译包已经带完整 OCaml 异常栈；`Internal error` / `Assert_failure`
  这类即使自己编译也修不好（除非改 OCaml 源码），正确做法是 `--keep-going` 跳过并统计影响面。
* `04-setup/scripts/_progress.sh` —— 统一的步骤标题与计时：
  每步打 `[n/N] 标题 (已用 3m12s)`，结束打上一步耗时，收尾打总耗时；
  另提供 `heartbeat_start/stop`，长时间无输出的命令每 30 秒打一行"还活着"。
* 脚本 10 / 20 / 30 / 40 / 50 全部接入；`git clone` 加 `--progress`；
  `curl` 加 `--progress-bar`。
* `rewrite_opam_urls.py`：每 2000 个文件打一次进度（含已改写数、用时、ETA）。
  实测 19032 个文件只要 **2.4 秒**——之前那"10 分钟"几乎全是 `git clone` 的时间，
  现在从输出上能一眼看出来卡在哪。
* `populate_opam_cache.py`：每 10 个文件打一次进度；末尾对 unresolved 给出解释
  （绝大多数是 opam 里已失效的历史引用，不影响后续）。
* 脚本 30 / 40 / 50 开头加**前置检查**：opam root 无效或 infer 源码不存在时
  直接报错退出，不再在半路以各种奇怪的方式失败；并预告本步大概要跑多久。

### 文档

* `04-setup/01-restricted-network-build.md`
  - 脚本表里的步骤顺序更新，标明 **`opam init` 必须在预填缓存之前**。
  - 「常见故障」新增两行：`does not appear to be a valid opam root` 的完整归因与对策、
    `rewrote url blocks in 0 / N` 该怎么解读。
  - 耗时表注明脚本 20 的 10 分钟几乎全是 clone。

---

## v2.5 — 2026-09-20

**全部目录与文件名改为英文。只动文件名和文件里的路径引用，文档正文一字未改。**

### 重命名

根目录：`Infer调研包-v2` → **`infer-research-kit`**（去掉版本后缀，版本号只在
CHANGELOG 与压缩包文件名里体现，避免再出现"包 v1 里装着报告 v2"那种对不上号）。

| 旧 | 新 |
|---|---|
| `01-报告/` | `01-report/` |
| `02-前置知识/` | `02-prerequisites/` |
| `04-环境搭建/` | `04-setup/` |
| `05-复现实验/` | `05-experiments/` |
| `06-实战/` | `06-practice/` |
| `07-能力边界/` | `07-capability-boundary/` |
| `08-Summary2.0设计/` | `08-summary2-design/` |
| 复现实验的六个子目录<br>`00-基础摘要/` `01-十三类场景/` `02-调用深度/`<br>`03-路径爆炸/` `04-全局数组/` `05-增量分析/` | `00-basic-summary/` `01-thirteen-scenarios/` `02-call-depth/`<br>`03-path-explosion/` `04-global-arrays/` `05-incremental/` |
| `Infer跨过程分析与函数摘要机制深度调研.md` | `infer-interprocedural-summary-research.md` |
| `报告网页版（离线副本）.html` | `report-web-offline.html` |
| `01-读懂Infer源码需要的OCaml.md` | `01-ocaml-for-reading-infer.md` |
| `02-抽象解释入门（给懂数据流分析的人）.md` | `02-abstract-interpretation-primer.md` |
| `03-分离逻辑与Bi-Abduction（给懂CSA的人）.md` | `03-separation-logic-and-biabduction.md` |
| `04-术语对照表（CSA-vs-Infer）.md` | `04-glossary-csa-vs-infer.md` |
| `01-受限环境逐步构建.md` | `01-restricted-network-build.md` |
| `Dockerfile-官方原文` | `Dockerfile-upstream` |
| `00-检查环境.sh` | `00-check-env.sh` |
| `10-安装opam和依赖工具.sh` | `10-install-opam.sh` |
| `20-准备opam仓库.sh` | `20-prepare-opam-repo.sh` |
| `30-安装ocaml和infer依赖.sh` | `30-install-deps.sh` |
| `40-构建infer.sh` | `40-build-infer.sh` |
| `45-摘掉rust前端.sh` | `45-strip-rust-frontend.sh` |
| `50-装上MiniOobChecker.sh` | `50-install-minioobchecker.sh` |
| `01-全量工程分析（compile_commands.json）.md` | `01-whole-program-analysis.md` |
| `02-可观测点手册（infer-out 全解剖）.md` | `02-observability-handbook.md` |
| `03-跨编译工具链capture失败定位与修复（aarch64_be-ilp32）.md` | `03-cross-toolchain-capture-fix.md` |
| `01-Infer能力边界与误报漏报全表.md` | `01-capability-boundary-fp-fn.md` |
| `01-总体设计.md` | `01-architecture.md` |
| `02-工作量估算与里程碑.md` | `02-effort-and-milestones.md` |
| `03-待验证假设与实测清单.md` | `03-assumptions-and-test-plan.md` |

`observe.sh` 生成的产物也一并改名：`观测报告.txt` → `observation-report.txt`。

### 同步更新

* 全包 **23 个文件、218 处路径引用**已自动改写并逐条核对。
* 文档里那几个省略写法（`03-assumptions-and-test-plan.md`、`02-effort-and-milestones.md`、`07-capability-boundary/01-capability-boundary-fp-fn.md`）
  一并展开成完整英文文件名。
* CHANGELOG 里描述历史重命名的条目保留原意，但不再引用已不存在的中文名。
* `README.md` 的目录结构块重排（英文名长度变了，原来的对齐全乱了），
  并补上 `Dockerfile-upstream`、`05-experiments/` 六个子目录、
  `06-practice/gen-compile-commands.sh` 等之前没列出的条目。

### ⚠️ 提交时注意

这是一次**大规模重命名**（35 个条目 + 根目录，144 个文件全部换了路径）。
**用 `git add -A` 提交**，让 git 的 rename detection 生效；
否则会显示成"删除 144 个文件 + 新增 144 个文件"，diff 完全没法看。

### 验证

* 全包路径已无任何非 ASCII 字符（`find | grep -P '[^\x00-\x7F]'` 空）。
* 从干净解压跑通：`06-practice/gen-compile-commands.sh` → `capture` → `analyze`，
  仍得到 `src/main.c:5 BUFFER_OVERRUN_L1 Offset: 17 Size: 10 by call to mid_fetch`。
* `07-capability-boundary/run-boundary.sh tp default` 仍为 18/22。
* 所有 `.sh` 通过 `bash -n`；全包交叉引用自检无失效链接。

---

## v2.4 — 2026-09-20

同步使用方对跨编译工具链两份文件的修订，并把其中的**真实全量数据**回填进相关文档。

### 变更（来自使用方，已按其提供内容整体替换）

* `06-practice/03-cross-toolchain-capture-fix.md`
  - 「一句话答案」补上第三层原因：cdb 的 `-isystem` / `-I` 指向已卸载的
    `bisheng_embedded_cpu-305.1.0.b003`。
  - **方案 A 参数组修正**：去掉 `--clang-libcxx-include-to-override-regex`，
    改用 `--Xclang "-I<infer>/include/c++/v1"` 显式注入 libc++。
    实测在 infer v1.3.0 上，该选项与 `--clang-isystem-to-override-regex`
    同时设置时**没有独立工作**——所有匹配同一 regex 的 `-I`/`-isystem`
    都被替换成了 `clang/21/include`，`c++/v1` 根本没出现在 `clang -###` 输出里。
  - 验证一节补上 C++ 文件（`lacl_relation.cpp`）的实测结果。
  - 遗留风险补充：`__SIZEOF_POINTER__=8` 会让 libc++ 内部的
    `reinterpret_cast<uintptr_t>` 产生硬 error，靠 `-ferror-limit 19` 容忍；
    超限会触发 `too many errors emitted`。
  - **新增第七节「全量执行结果（2026-09-20）」**：cdb 10640 条 /
    10232 唯一源文件 / capture.db 10232 源文件 / 328602 过程 /
    **按唯一源文件 100% 成功**；并逐条解释了 6638 条 `cast from pointer to
    smaller type`、129 条 `too many errors emitted`、136 条 `Frontend error`
    为什么都没有阻止文件落库。
  - 第六节补上"cdb 记录数 ≠ 唯一源文件数"的正确分母算法。
* `06-practice/capture-cross-toolchain.sh`
  - 参数组同步（去掉 libcxx override，改 `--Xclang -I`）。
  - 新增 `--progress-bar-style plain` + `tee`，长任务实时可见进度。
  - `RC=${PIPESTATUS[0]}` 取真实退出码。
  - 覆盖率统计改用**唯一源文件数**做分母，并单列"cdb 重复条目（非失败）"。
  - 错误统计拆成「行数」与「涉及源文件数」两类，并注明
    `too many errors emitted` 与 `Frontend error` 都不等于文件缺失。

### 回填（本包补充）

* `07-capability-boundary/01-capability-boundary-fp-fn.md` §7「本文档没有回答的」——
  "真实业务规模的数字"一条标注**部分补上**：capture 阶段已有真实数字
  （100% 落库、328602 过程），**报告 §31 的第一风险「Infer 能不能吃下你们的代码」已过关**；
  但 analyze 阶段仍空白，且因目标宏被人为改写，**下一步必须先跑能力探针**
  （预期 18/22）确认基线没被改坏。
* `08-summary2-design/03-assumptions-and-test-plan.md` —— **T1-1 标记为已完成**，
  填入实测表格、结论与两条后续动作；"我拿到数据之后会改什么"对照表同步更新。
* `08-summary2-design/01-architecture.md` —— 假设 **A5 从"已验证 cdb 可解析"
  升级为"已跑通 capture"**。
* `06-practice/01-whole-program-analysis.md` —— 新增
  「还有一个陷阱：cdb 记录数 ≠ 源文件数」一节。这是个通用坑（不限于交叉编译）：
  用记录数做分母会把重复条目误判成失败，真实案例里差出 408 个"假失败"。

---

## v2.3 — 2026-09-17

把报告 §25/§28 的「方案 C」从**架构建议**细化成**可以开工的设计**，
并给出工作量估算与一份按「能不能推翻设计」排序的实测清单。

### 新增

**`08-summary2-design/`（全新目录）**

* `01-architecture.md`
  - 组件图与逐组件设计：ExtentModel / AccessPath / BoundExpr / SummaryGen /
    SummaryIR / SummaryApplier / SummaryDB / SummaryManager / OobSummaryChecker，
    含 C++ 数据结构伪代码与关键算法。
  - **借鉴清单 12 条**，每条标注 Infer 出处与报告章节；
    **反面借鉴 6 条**（R1–R6），每条对应一处实测到的 Infer 缺陷。
  - **假设清单 A1–A6**，每条标注「若不成立会怎样」与对应的测法编号。
  - 「明确不做的」6 条，写死第一版边界（不做 SCC 不动点、不做指向集合分析、
    不做 C++、不做路径敏感摘要、不追求精度不低于现有规则、不替换 CTU）。
* `02-effort-and-milestones.md`
  - 分模块估算：约 9400–13400 行代码，**98–151 人日（5–7.5 人月）**，置信区间 ±40%。
  - 分 M0–M5 六个里程碑，各自的人日与验收标准；
    **建议第一个决策点是 M1 结束（33–48 人日）**。
  - 「由 Claude 写代码」的分层估算：能大幅加速的 / 加速有限的 / 帮不上的，
    合并后 M0+M1 约 14–22 人日、全量 48–80 人日（节省约 45–55%），
    并列出「要我写代码需要你提供的 5 样东西」。
  - 风险表，以及**一个便宜一个数量级的备选**（只做 ExtentModel +
    把 inline 深度从 2 提到 4–5，15–20 人日拿约 70% 收益）。
* `03-assumptions-and-test-plan.md`
  - **Tier 0（半天～一天，能推翻设计）**：T0-1 CSA 的 extent 能力基线、
    T0-2 现有告警的「单函数内可判定」比例、T0-3 真实越界的调用深度分布、
    T0-4 unsupported 的根因分布。每项给出可直接用的代码/命令与解读规则。
  - Tier 1（影响参数与优先级）5 项，Tier 2（细化设计用）5 项。
  - 「我拿到数据之后会改什么」对照表——每项实测明确对应会修改的文档与结论。

### 变更

* **报告 §25 新增指引块，并修正 §25.2 的一处架构结论**：
  摘要生成不应以 CSA 的 ExplodedGraph 为**主要**事实源。
  理由：CSA 放弃探索的每一处（循环预算、路径预算、inline 深度到顶）都会没有事实可采，
  于是没有摘要，于是又回到 `unsupported`——**当前最痛的失败模式会被原封不动继承**。
  修正后的结构是「独立 AI 为主（全函数性是硬约束）、CSA 路径信息为辅（精度增强）」。
  该修正本身也挂了证伪条件（T0-4）。
* 报告新增**附录 E — 方案 C 的落地设计**，§0.0 的附录清单更新为五个。
* `README.md` 目录结构、「按你的目的选一条路」表更新；
  推荐结论下加了一段**前提未验证**的提醒（「调用深度是主要瓶颈」来自团队自述，
  尚未被数据验证，T0-3/T0-4 是验证它的）。

---

## v2.2 — 2026-09-17

回应"Infer 的能力边界到底是什么"这个选型问题，并收录使用方的跨编译工具链修复成果。
**本版包含一处对既有结论的更正。**

### 新增

**`07-capability-boundary/`（全新目录）—— 选型权衡用**

* `01-capability-boundary-fp-fn.md`
  用 **47 个文件的正交四象限语料**（22 个真实越界 / 15 个绝对安全 / 10 个易误报 /
  13 个专项探针）实测出的：
  - **混淆矩阵**：默认档 TP 18 / FN 4 / FP 0 / TN 25（精确率 100%，召回 81.8%）；
    高灵敏档 TP 19 / FN 3 / FP 13 / TN 12（精确率 59.4%，召回 86.4%）。
    **结论：高灵敏档换 1 个真阳性赔 13 个文件的噪声，不值得开。**
  - **漏报逐条归因，并明确区分「机制性」与「参数性」** —— 这是本文档的核心产出。
    机制性三条（struct 字段数组 / 全局数组无初始化器 / C 函数指针），
    每条给出源码根因、已验证"调参无效"、以及修复工作量估计
    （10–30 行 / 20–50 行 / 人月级）。
    参数性两条（符号条件只传不报 / 超大函数被跳过），给出正确的应对方式。
  - **参数与代价对照表**（11 个旋钮），并给出一条分界线：
    影响**精度**的旋钮不该动，影响**覆盖**的旋钮值得调。
  - Pulse 对照：同样 22 个真实越界，`--pulse-only` **检出 0 个数组越界**。
  - 对 CSA Summary 2.0 的五条直接启示，与一份诚实的"本文没回答"清单。
* `run-boundary.sh` —— 一键复现上述全部数字（已实测跑通）。
* `corpus/tp tn fp probe` —— 60 个语料文件，可固化进 CI 做能力回归。

**`06-practice/` 增补 —— 交叉编译工程**

* `03-cross-toolchain-capture-fix.md`
  由使用方在自己的 RTOS 工程上实地定位后写成，收录进包。
  核心机制结论对任何交叉编译工程都成立：**infer 会用自带 clang 替换 cdb 里的编译器，
  且不继承 `--target` / `--sysroot`**。含三套修复方案与取舍。
  本包补充了第六节「capture 通了以后：先测能力基线，再解读结果」。
* `capture-cross-toolchain.sh` —— 上面那份文档【方案 A】的落地脚本。
  本包补充了 capture 覆盖率统计（直接查 `capture.db` 的 `source_files` 条数，
  而不是只数日志里的 error）。

### 更正

* **报告 §13 场景 #6「结构体字段数组：支持」的结论过于乐观，本版更正。**
  初版只测了结构体**指针形参**（`b->data[11]`）这一种写法。
  13 个变体的专项探针显示：基址是**形参**或 **`malloc`** → 检出；
  基址是**局部 / `static` 局部 / 全局 struct（即使有初始化器）/ struct 数组元素 /
  嵌套 struct** → **全部漏报，且 `--bo-field-depth-limit`、
  `--bo-sound-unknown-sets-join`、`--bo-debug 4` 三个旋钮都救不回来**。
  把 `.` 换成 `->` 也没用——区别不在访问语法，在基址是不是形参或堆分配。
  【源码确认】根因是 `bufferOverrunUtils.ml:56-70` 的 `decl_local_loc`
  对 `Typ.Tstruct` **原样返回内存、不递归进字段**；
  【实测数据】此时摘要里 `Safety conditions: { }`，一条条件都没生成。
  报告表格已加场景 #6b，并在表后加了整段更正说明。

### 变更

* 报告新增**附录 D — 能力边界与误报漏报**（混淆矩阵、机制/参数归因表、
  对 §27 架构建议的修正、一条方法论教训），§0.0 的附录清单同步更新为四个。
* 附录 D 把 §27 的 P0 优先级往前推了一格：**"把哪些基址能拿到 extent 做全"
  排在 SummaryDB 和 AbstractRange 之前**——因为 4 个漏报里有 3 个出在
  "某类基址拿不到 size"，而不是算法不够强。
* §25「反面借鉴」清单新增第三条：extent 必须是内存模型的一等属性，
  不能绑在初始化器过程的摘要上，也不能在声明处对聚合类型止步。
* `README.md` 目录结构、"按你的目的选一条路"表、"最关键的结论"同步更新。

---

## v2.1 — 2026-09-17

修一个 v2 的实打实的 bug，外加把它背后的通用坑写进文档。

### 修复

* **`06-practice/gen-compile-commands.sh` 没有创建 `build/` 目录。**
  `compile_commands.json` 的 `directory` 字段被改写成了 `<工程>/build`，
  但那个目录不存在，导致 `infer capture` 报
  `chdir(.../build): No such file or directory` 并打出一长串 OCaml 栈。
  现在脚本会 `mkdir -p "$root/build"`，改写后按 `directory` + `file` 逐条**自检**
  源文件是否解析得到，失败就直接退出（不再让 infer 去报栈），
  成功则把下一步命令打印出来。
* 两个 demo 工程补了 `build/.gitkeep`，这样即使不跑脚本，`build/` 也随包一起存在。

### 新增

* `06-practice/01-whole-program-analysis.md` 新增开篇一节
  **「开跑之前：先体检 compile_commands.json」**。这不只是 demo 的问题——
  真实工程里 `compile_commands.json` 常常是在**构建机**上生成的，
  `directory` 指向本机不存在的构建目录，拿过来就是同样的报错。
  该节给出：可直接粘的体检脚本、三种情况的修法对照表、批量改写路径前缀的脚本，
  以及一条容易误导人的提示 —— `Starting translating N files` 只表示
  **读到了 N 条记录**，不代表翻译成功。

### 变更

* `README.md` 的「快速开始」把 `gen-compile-commands.sh` 标成必跑步骤，并写明跳过会看到什么报错。

---

## v2 — 2026-09-16

本轮回应四个问题：全量工程怎么分析、报告里的概念看不懂、受限环境构建卡住、
以及"关键技术点是不是都覆盖了 / 报告还要怎么补"。

### 新增

**`06-practice/`（全新目录）—— 上自己代码库要用的东西**

* `01-whole-program-analysis.md`
  回答"逐文件分析跨文件信息不就丢了吗"。结论：**capture 按文件，analyze 一次全量**。
  含全量 vs 逐文件的对照实测（三文件工程，全量报 1 条 L1，逐文件三个各 0 条），
  跨文件摘要链的真实输出，以及上真实代码库前的 6 条建议（切子集、量化 capture 成功率、
  并行、增量、reportdiff、磁盘估算）。
* `02-observability-handbook.md`
  capture.db / results.db 的真实 schema、可直接跑的 SQL、把 IR 导出成可读 SIL 文本的方法、
  逐节点抽象状态 HTML 的提取脚本、stats 计数器逐条解读、report.json 字段表，
  以及"观测点 → 报告结论 → 对 CSA Summary 2.0 的启示"对应表。
* `observe.sh` —— 一键把一次运行的所有可观测点 dump 成 `observation-report.txt`（已实测跑通）。
* `sql/queries.sql` —— 可直接 `sqlite3 infer-out/results.db < queries.sql`。
* `demo-project/`、`demo-project-plain/` —— 多文件工程 + `compile_commands.json`
  （每个文件带不同的 `-I` / `-D`，越界只有跨三个文件才看得见）。
* `gen-compile-commands.sh` —— 修正 `compile_commands.json` 里必须是绝对路径的 `directory` 字段。

**`02-prerequisites/`（全新目录）—— 面向懂 CSA、懂数据流分析但没读过 OCaml 的读者**

* `01-ocaml-for-reading-infer.md` —— 先解决"`Location.t` 里的 `.t` 是什么"；
  `.ml`/`.mli`、`val` 签名、ADT 与模式匹配、`option`/`result`、functor、`[@@deriving]`，
  以及"不需要掌握的"。附半天练手路径。
* `02-abstract-interpretation-primer.md` —— 用"你已经会的数据流分析"作锚点讲
  抽象域 / join / 不动点 / widening / narrowing，配报告里抓到的**真实 widening 轨迹**
  （`i: 0 → [0,1] → [0,9] → [0,10] → 不动点`）与阈值来源源码。
* `03-separation-logic-and-biabduction.md` —— `p |-> v`、分离合取、Frame Rule、
  溯因的完整走查，配真实 Pulse 摘要输出（`Inferred pre` / `MustBeValid`），
  以及"BufferOverrun 用的是同一思想的轻量版，这个对你们更实用"。
* `04-glossary-csa-vs-infer.md` —— 程序表示 / 分析状态 / 框架 / 跨过程 / 摘要 / 运行产物
  六张对照表，左边是你熟悉的说法，右边是 Infer 的说法与源码位置。

**`04-setup/` 增补 —— 受限网络环境**

* `01-restricted-network-build.md` —— 先诊断"`./build-infer.sh clang` 卡在 `initializing opam...`"
  （两个原因：下载 opam.ocaml.org 无输出；或 opam 在等沙箱确认的交互提问），
  给三条诊断命令，再按"标准路线 / 受限路线"分叉。附耗时与磁盘实测表、11 条常见故障对策。
* `scripts/00-check-env.sh` —— 只检查不改动，测 7 个域名可达性并给出走哪条路的结论。
* `scripts/10-install-opam.sh` —— opam 二进制、GMP 头文件、rsync 替身。
* `scripts/20-prepare-opam-repo.sh` —— clone opam 仓库 + 改写源 + **按 sha256 预填下载缓存**
  （不动元数据、不关校验的干净做法）。
* `scripts/30-install-deps.sh` —— OCaml 5.4.1+flambda + 152 个依赖，
  自动关 depext、自动处理 charon 不可用的情况。
* `scripts/40-build-infer.sh`、`45-strip-rust-frontend.sh`、`50-install-minioobchecker.sh`。
* `scripts/*.py` —— 三个改写/缓存脚本 + rsync 替身。

### 变更

* **报告改名**：去掉文件名里的版本后缀（当时是中文名带 `-v2`，现已统一为
  `infer-interprocedural-summary-research.md`）。
  之前"包 v1 里装着报告 v2"对不上号，现在报告版本跟随包版本，文件名不再带版本后缀。
* **报告增补三个附录**（正文与章节编号不变，附录追加在 §33 之后）：
  * 附录 A — 执行流程与可观测点速查（含"四个最该知道的观测入口"与观测点→借鉴点对应表）
  * 附录 B — 全量工程分析实测
  * 附录 C — **技术点覆盖自查（诚实版）**：分三类列出「已充分展开」「提到但未充分展开」
    「必须用真实代码库才能回答」。明确指出若要真的把 Infer 用起来，
    Specialization、告警 hash/reportdiff、Clang 前端翻译覆盖率这三项需要补做。
* 报告新增 §0.0 指向上述附录与 `02-prerequisites/`。
* **报告网页版同步更新**：新增第 15 节「全量工程与可观测点」（含全量 vs 逐文件对照表、
  真实跨文件摘要链、四个最该知道的观测入口、观测点→借鉴点对应表），
  并补齐 `<!doctype>` / `<head>` / `<body>` 使其成为可离线双击打开的完整文档。
* **目录重新编号**（按阅读顺序）：`02-` → `03-MiniOobChecker`；复现实验目录 `03-` → `05-`；
  新增 `02-prerequisites`、`06-practice`。所有交叉引用已同步更新并自检通过。
  （当时这些目录还是中文名，v2.5 统一改成了英文，见本文件顶部。）
* `README.md` 重写：新增"按你的目的选一条路"表与"快速开始"可复制命令块。
* `04-setup/README.md` 顶部加指向受限环境指南的提示。

### 修复

* `05-experiments/run-all.sh` 第 05 组（增量分析）重写为**两个各自从干净 `infer-out` 开始的场景**。
  原写法受 capture 历史干扰，会出现 `Invalidated 0 of 8`。
* 同组的【预期】不再断言确切的失效条数。实测同一份改动会得到 `6 of 8` 或 `7 of 8`，
  取决于 capture 历史 —— **"失效条数"不是稳定可断言的指标**。
  报告 §17.2 已加补充说明，`05-experiments/README.md` 的"已知坑"第 4 条也写明：
  CI 里要断言的是"顶层报告是否重新出现"，不是失效了几个。

### 已知局限（本版仍未解决，需要真实代码库）

* Infer 对贵团队代码的 **capture 成功率**（内部 LLVM 14 fork 的扩展语法 vs Infer 自带 Clang 21.1.6）。
* 大型工程（万级文件）的端到端耗时与内存。
* 高灵敏度配置（`--bo-debug 4` + L4/L5）在真实代码上的误报率。
* MiniOobChecker 不处理 struct 字段数组（`b->data[i]`）、循环、malloc、多级指针
  —— 教学级实现的刻意取舍，见 `03-MiniOobChecker/README.md`。

---

## v1 — 2026-09-11

首版。报告（源码追踪 + 实测数据 + 可编译运行的 MiniOobChecker）、
13 类场景复现实验、环境搭建说明、报告网页版。
