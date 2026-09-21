# 受限环境下从源码构建 Infer —— 照着执行就行

> 回答的问题：**"我执行 `./build-infer.sh clang`，显示 `initializing opam...` 就没动静了，是不是有问题？"**
> 以及 **"报告 §20.7 我没太看懂，能不能写简单点、把每步命令列出来？"**

---

## 先回答：卡在 `initializing opam...` 是怎么回事

`build-infer.sh` 第 234 行：

```bash
echo "initializing opam... " >&2
...
setup_opam        # → opam init --reinit --bare --no-setup
```

**它不是死了，是在等 `opam init`。** 而 `opam init` 会做两件在受限环境下容易卡住的事：

1. **从 `opam.ocaml.org` 下载默认仓库索引**。你的代理如果没覆盖这个域名，curl 会静默重试到超时，
   期间**一个字都不打印**，看起来就像挂了。
2. **检查沙箱（bubblewrap）**。容器里通常没有 `bwrap`，opam 会**弹出交互式提问**
   "Sandboxing is not working... disable it?"，而 `build-infer.sh` 把它的输出重定向了，
   你看不到提示，它就一直等你敲回车。

### 三条诊断命令（先跑这个，别猜）

```bash
# a) opam 装了吗、什么版本
opam --version

# b) opam.ocaml.org 能不能通（这一条决定走哪条路线）
curl -sS -o /dev/null -w 'opam.ocaml.org: %{http_code}\n' --max-time 25 \
     https://opam.ocaml.org/index.tar.gz

# c) 绕开 build-infer.sh，直接手跑，看它到底停在哪一句
opam init --reinit --bare --no-setup --disable-sandboxing
```

> **只有还没开始走受限路线时才这么敲。**一旦跑过 `scripts/20-prepare-opam-repo.sh`，
> 你的 opam root 就在 `$WORK/opamroot` 而不是 `~/.opam` 了，这时再裸敲 `opam init`
> 会建出第二个 root —— 见下面「手敲 opam 命令前先 source 它」。

或者直接跑本目录的检查脚本，它会把上面这些连同别的一起测完并给出结论：

```bash
./scripts/00-check-env.sh
```

### 两种结果，两条路

| `curl opam.ocaml.org` 的结果 | 走哪条 |
|---|---|
| **200** | **标准路线**（下一节）。之前卡住多半只是 `opam init` 在等沙箱确认，加 `--disable-sandboxing` 就好 |
| **超时 / 403** | **受限路线**（再下一节）。本目录的 `scripts/` 就是为这种情况准备的 |

---

## 标准路线（`opam.ocaml.org` 可达）

```bash
# 1. 先手动把 opam 初始化掉，避开交互提问
opam init --reinit --bare --no-setup --disable-sandboxing -y

# 2. 再跑官方脚本
git clone https://github.com/facebook/infer.git
cd infer
git checkout 3d38229b04b649024fee065621f6a4c838f6d029   # 调研基线，见「为什么要钉在调研基线 commit」
./build-infer.sh --local-opam-switch -y clang     # --local-opam-switch：switch 建在仓库内 _opam/，不污染全局
```

耗时的大头是**从源码编译定制 LLVM/Clang**（官方 `INSTALL.md` 原文警告 "may take a really long time"，
实际数小时）。**强烈建议跳过它**：

```bash
# 先下官方预编译包（276 MB）
# 报 curl: (77) error setting certificate file 的话，先 unset SSL_CERT_FILE（只影响当前窗口）
curl -fL -o infer-bin.tar.xz \
  https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
tar xf infer-bin.tar.xz

# 把里面编译好的 clang 借给源码树用
cd infer/facebook-clang-plugins/clang
ln -sfn "$PWD/../../../infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install" install
SHASUM="sha256sum" ./setup.sh -r          # 记录版本哈希
SHASUM="sha256sum" ./setup.sh -c && echo "clang 已就位"

# 然后只构建 OCaml 部分
cd ../../..
eval $(opam env)
./autogen.sh && ./configure --disable-rust-analyzers
make -j$(nproc) BUILD_MODE=default infer
./infer/bin/infer --version
```

---

## 受限路线（`opam.ocaml.org` 不可达，但能 `git clone` GitHub）

**这正是你的情况**（你说代理能 clone GitHub 仓库）。
本目录 `scripts/` 下是编号脚本，**按顺序执行即可**，每一步都能单独重跑。

> **`25-prefetch-git-sources.sh` 是条件步骤**：正常情况跳过它，
> 只有脚本 30 大面积报 `HTTP 403`、而你手工 `git clone` 又是通的时候，才回头插进来跑一次。
>
> 嫌麻烦就直接 `./run-all.sh`（下面「执行（A）」），它把顺序和环境变量都包好了。

### 原理一句话

受限环境通常只放行两样：**`git clone` GitHub** 和 **GitHub Releases 的下载链接**。
而 opam 默认要从 `opam.ocaml.org` 拿仓库、从各种压缩包地址拿源码。
所以我们做三件事：

1. 用 `git clone` 把 opam 仓库整个搬下来，当本地仓库用；
2. 把仓库里"只能 HTTP 下压缩包"的源**改写成"能 git clone 的源"**；
3. 少数只能从 `raw.githubusercontent.com` 拿的小文件，**用 git 取出来、按官方声明的 sha256
   塞进 opam 的下载缓存** —— opam 查缓存命中就不走网络，**而且校验和依然生效**。

第 3 条是最干净的一招：不动 opam 元数据，不关校验。

### 执行（A：一把梭，推荐）

干净窗口、从零开始，只需要两条命令：

```bash
cd <本调研包>/04-setup/scripts
./run-all.sh
```

它按 00 → 10 → 20 →（25 条件）→ 30 → 35 → 40 → 50 的顺序跑完，
`WORK` / `J` / `DLJ` / `PATH` / `OPAMROOT` / `PREBUILT` 全部自己管，不用手动 export 任何东西；
CA 证书环境变量的自检也在开头做掉了。跑起来是这样的：

1. **先打印执行计划**：哪几步会跑（`[1/7]`…`[7/7]`）、哪几步跳过、为什么跳过、每步大概多久；
2. **每一步打 `[第几步/共几步]` 标题**，并告诉你这一步单独执行的命令和日志文件位置；
3. **任何一步失败立即停下**，告诉你是哪一步失败、退出码和用时，给出三件事：
   单独重跑这一步的命令、修好后从这一步接着跑的命令、排障表位置。
   30 撞上 `Could not synchronize`、35 下不了预编译包时，还会给针对性的下一步；
4. **结束（或失败）时打一张汇总表**：每步 ✓ 成功 / ✗ 失败 / — 跳过 / · 未执行，以及耗时。

每一步的完整输出都存在 `$WORK/logs/run-all-<时间戳>/<脚本名>.log`。

任何一步挂了，修完之后从那一步接着跑即可 —— 每个子脚本都能重跑、会跳过已完成的部分：

```bash
FROM=30 ./run-all.sh        # 步号取 00/10/20/25/30/35/40/50
```

可调的环境变量（都有默认值）：

| 变量 | 默认 | 说明 |
|---|---|---|
| `WORK` | `$HOME/infer-build` | 所有产物的落地目录 |
| `J` | `$(nproc)` | 编译并行度。内存 < 8 GB 时调小，如 `J=2` |
| `DLJ` | `4` | opam 下载并发。**核多的机器别用默认核数**，几十个 `git fetch` 同时打到代理容易被限流成 403 |
| `FROM` | `00` | 从哪一步开始 |
| `PREFETCH` | 空 | 设为 1 才跑条件步骤 25（见下） |
| `SKIP_CHECKER` | 空 | 设为 1 则只构建 infer 本体，不装 MiniOobChecker |
| `INFER_REF` | 调研基线 `3d38229b` | 换别的 infer commit（sha / tag / `main`）。**一般不要动**，见下方「为什么要钉在调研基线」 |

比如一台 48 核、内存紧张的机器：

```bash
J=8 DLJ=2 ./run-all.sh
```

### 执行（B：手动一步步来）

想看清每一步、或者只跑其中几步，就按下面这个顺序，**每一行都可以直接复制**：

```bash
cd <本调研包>/04-setup/scripts

# ── 0. 环境变量（一个窗口设一次）────────────────────────────────
export WORK=$HOME/infer-build         # 所有产物都放这里，可随便改
export J=$(nproc)                     # 编译并行度；内存 < 8 GB 就调小
export DLJ=4                          # opam 下载并发；核多的机器别调大

# ── 1. 体检（只看不改，有 ✗ 是正常的）──────────────────────────
./00-check-env.sh                     # 第 0 节会告诉你 CA 证书环境变量是否失效

# ── 2. 装 opam 本体 + GMP 头文件 + rsync 替身 ──────────────────
./10-install-opam.sh
. ./opam-env.sh                       # ★ 设好 PATH 和 OPAMROOT，并清掉失效的 CA 变量
                                      #   新开窗口都要重跑这行

# ── 3. 准备 opam 仓库 + clone infer 并切到调研基线 ─────────────
./20-prepare-opam-repo.sh             # 约 10 分钟

# ── 4. 装 OCaml 和全部依赖（★ 最慢：2 核约 55 分钟，8 核约 20 分钟）─
./30-install-deps.sh
#   ↑ 如果这一步大面积报 "Could not synchronize ... HTTP 403"，
#     先跑条件步骤 25 把源预取到本地，再回来重跑 30：
#         ./25-prefetch-git-sources.sh
#         ./30-install-deps.sh
#     还不行就再压并发： DLJ=1 ./30-install-deps.sh

# ── 5. 取官方预编译包，借它编译好的 clang（省掉数小时 LLVM 自举）──
./35-fetch-prebuilt.sh                # 下载 + 校验 + 解压；40 会自动找到，不用 export PREBUILT

# ── 6. 构建 infer 本体（2 核约 13 分钟）────────────────────────
./40-build-infer.sh

# ── 7. 装上本包里的 MiniOobChecker 并跑 demo ───────────────────
./50-install-minioobchecker.sh        # 会先核对 infer 源码是否在调研基线上

# ── 8. 用起来 ─────────────────────────────────────────────────
export PATH=$WORK/infer/infer/bin:$PATH
infer --version                       # 预期 Infer version v1.3.0-3d38229b
```

> **v3.2 及以前第 5 步是一条手敲的 `curl`**，会被 shell 里残留的失效 CA 变量打断：
>
> ```
> curl: (77) error setting certificate file: /tmp/_MEI7vvHlN/certifi/cacert.pem
> ```
>
> 根因是 `SSL_CERT_FILE` 指向了某个 PyInstaller 打包工具的临时目录，工具退出后目录被清掉了。
> 临时解法就是 `unset SSL_CERT_FILE` —— **只影响当前窗口**，别的窗口若需要这个变量不受影响。
> v3.3 起这件事自动化了：`35-fetch-prebuilt.sh`（以及 10–50 所有脚本和 `run-all.sh`）
> 启动时都会清掉**指向不存在文件**的 CA 变量；`. ./opam-env.sh` 则在你的交互窗口里做同样的事。
> 指向**存在**的文件（比如公司代理的根证书）一律原样保留。

> **`25-prefetch-git-sources.sh` 是条件步骤，默认不跑。**
> 它要串行 clone 一两百个仓库，本身很慢，只有在第 4 步大面积 403
> **且你手工 `git clone` 同一个仓库是通的**时才有价值。
> 走 A 路线时用 `PREFETCH=1 FROM=25 ./run-all.sh`。

> **每开一个新窗口，都要重新跑 `. ./opam-env.sh`。**
> 这些是环境变量，不会自己留在新 shell 里。

### 为什么要钉在调研基线 commit

本包的全部实验数据、以及 `03-MiniOobChecker/patch/0001-register-minioob-checker.patch`，
都是在 `facebook/infer` 的 **`3d38229b04b649024fee065621f6a4c838f6d029`**（main，2026-09-10）上产出和验证的。
v3.2 及以前，脚本 20 clone 的是**当时最新的 main**，没有切到这个 commit。

**这是个真实隐患（感谢使用方指出）。** 补丁是 `git diff` 生成的上下文补丁，
依赖被改文件的周边几行保持不变。今天能打上，只能说明基线到今天之间
上游恰好没动这几个文件；infer main 每周都有提交，过几个月很可能就打不上了。

2026-09-21 实测的漂移情况【实测数据】：

| 项目 | 基线 `3d38229b` → main `63568044` |
|---|---|
| 相差 commit 数 | 2 |
| 改动范围 | `infer/src/pulse/`（taint 匹配、TreeBorrows）、`infer/src/IR/Specialization.ml`、Rust 测试用例，共 15 个文件 |
| 补丁涉及的 9 个文件 | **0 个被改** —— 所以使用方在最新 main 上打补丁成功了 |
| `opam/infer.opam.locked` | 未改 —— 依赖版本不受影响 |
| BufferOverrun / `labs/` | 未改 —— 报告里的 BufferOverrun 结论在这个 main 上同样成立 |

**为什么必须在脚本 20 就切，而不是等到 50 打补丁前才切**：
20 之后的每一步都读 infer 的 `opam/infer.opam.locked` —— 20 按它预填缓存，25 按它预取源，
30 按它装依赖。等到 50 才切，就成了"按新 lock 装的依赖 + 旧源码"，轻则编译失败，
重则编过了但行为跟报告对不上、还很难查出原因。

v3.3 的做法：

- **`_baseline.sh`** 统一定义基线 commit，20 / 30 / 50 / `run-all.sh` 都读它；
- **脚本 20** clone 之后立刻 `checkout` 到基线。已有 clone 时：
  - 已在基线 → ✓；
  - 不在基线、工作区干净 → 自动切过去（没有任何本地改动会丢）；
  - 不在基线、工作区有改动（跑过 45 或 50 留下的）→ **不替你动**，停下来给三个选项：
    切回基线（`stash` 收起改动，不丢）/ 维持现状（`INFER_REF=HEAD`）/ 推倒重来；
- **脚本 30** 不再兜底 clone（兜底 clone 出来的是最新 main，会跟 20 读的 lock 不一致）；
- **脚本 50** 打补丁前先核对版本；不在基线时列出**补丁涉及的文件里哪些在基线之后被上游改过**；
  先 `git apply --check` 试打，失败退到 `--3way` 三方合并，还不行就回滚干净并给出回到基线的命令；
  补丁已经打过则跳过（旧版重跑会在 `git apply` 直接报错）。

**你当前的环境**如果是 v3.2 及以前建的（infer 在最新 main、已打过补丁），想回到基线：

```bash
git -C $WORK/infer stash -u                                          # 收起 45/50 留下的改动
git -C $WORK/infer checkout 3d38229b04b649024fee065621f6a4c838f6d029
cd <本调研包>/04-setup/scripts && FROM=20 ./run-all.sh               # 按基线的 lock 重走一遍
```

不回也可以：按上面的实测，现在的 main 跟基线在补丁文件、lock、BufferOverrun 上都没有差异。
但**以后再从零搭一套时，就会自动落在基线上了。**

想刻意用别的 commit（比如验证补丁在最新 main 上还能不能打）：

```bash
INFER_REF=main ./run-all.sh
```

### 每个脚本干了什么

| 脚本 | 干什么 | 关键点 |
|---|---|---|
| **`run-all.sh`** | **一把梭**：先打执行计划，再按顺序跑 00→10→20→(25)→30→35→40→50，失败立即停下并告诉你怎么单独重跑那一步，最后打汇总表 | `FROM=<步号>` 断点续跑；`PREFETCH=1` 才跑 25；`SKIP_CHECKER=1` 只建 infer 本体；日志在 `$WORK/logs/` |
| `00-check-env.sh` | 检查 **CA 证书环境变量**、命令、头文件、**网络可达性**，给出走哪条路的结论 | 不改任何东西，可反复跑。失效的 CA 变量会让它的网络探测全部误判为不可达，所以 v3.3 起第 0 节先查这个 |
| **`opam-env.sh`** | **`source` 用，不是执行。**设好 `OPAMROOT` / `PATH` / switch，清掉失效的 CA 变量 | 任何时候想手敲 `opam ...` 或 `curl ...`，先 `. ./opam-env.sh`。只影响当前窗口 |
| `_baseline.sh` | 定义调研基线 commit，被 20 / 30 / 50 / `run-all.sh` source | 改 `INFER_REF` 环境变量即可换 commit，不用改这个文件 |
| `10-install-opam.sh` | ① 从 GitHub Releases 下 `opam` 二进制<br>② `gmp.h`（apt 不可用时从 `gmp-mirror/gmp-6.3` 取）<br>③ `rsync` 替身 | GMP 只需要**头文件**；系统若已有 `libgmp.so.10`（GMP 6.x）直接建软链，不用编译整个库 |
| `20-prepare-opam-repo.sh` | clone opam 仓库 → `rewrite_opam_urls.py`（改写 ~6900 个包）→ `rewrite_erratique.py`（改写 ~100 个）→ **`patch_git_source_quirks.py`**（修 git 源特有的构建问题）→ **`opam init`** → **clone infer 并切到调研基线** → `populate_opam_cache.py`（预填缓存）→ `opam update` + 自检 | **`opam init` 必须在预填缓存之前**，见下方「v2.6 修复」；带 `--disable-sandboxing` 避开交互提问；**infer 必须在这一步就切到基线**，因为后面都读它的 lock |
| **`25-prefetch-git-sources.sh`** | **串行**把 opam 要 git fetch 的源全部 clone 到本地镜像，再把仓库里的 URL 改写成 `file://` | **只在脚本 30 大面积报 `HTTP 403` 时才需要**。报「0 个要拉」时会说清楚是哪种情况 |
| `30-install-deps.sh` | 建 switch、装编译器、pin 仓库自带依赖、装 152 个包 | 自动关掉 `depext`（否则会去调 apt 卡住）；charon 拿不到时自动生成去 Rust 的 lock；**下载并发默认压到 4**（`DLJ=1` 可再降） |
| **`35-fetch-prebuilt.sh`** | 下载官方预编译包（先下到 `.part`、完整后改名）→ `xz -t` 校验 → 解压 | 已就位则跳过；下载失败时按 curl 退出码（77/60/22/7/28/35）给出针对性原因；下不了可 `PREBUILT_TARBALL=<本地路径>` 指向别处拷来的包 |
| `40-build-infer.sh` | 复用预编译 clang → `autogen` → `configure` → `make` | 自动找到 35 解压的预编译包，不用 `export PREBUILT` |
| `45-strip-rust-frontend.sh` | 仅在 charon 装不上时由脚本 40 自动调用 | `--disable-rust-analyzers` **不够**，因为 `integration/dune.in` 无条件依赖 charon 库 |
| `50-install-minioobchecker.sh` | 核对 infer 版本 → 拷源码 + 打注册补丁 → 增量 make → 跑 demo | 不在基线时列出上游改过的补丁文件；`--check` → `--3way` → 回滚三级兜底；可重跑 |

### 各阶段耗时与磁盘（2 vCPU / 7.8 GB 实测）

| 阶段 | 耗时 | 磁盘 |
|---|---:|---:|
| opam 仓库准备（脚本 20） | ~10 min（**几乎全是 clone 的时间**；两个改写脚本各 2~3 秒） | ~1 GB |
| OCaml 编译器自举 | ~14 min | — |
| 152 个依赖（脚本 30） | ~40 min | `$OPAMROOT` 3.8 GB |
| 构建 infer（脚本 40） | ~13 min | `_build` 1.3 GB，二进制 125 MB |
| **合计（不含 LLVM 自举）** | **≈ 75 min** | **≈ 6 GB** |

**磁盘建议预留 ≥ 20 GB，内存 ≥ 8 GB。核数越多越好——依赖安装是可并行的大头。**

---

## 手敲 opam 命令前先 source 它

这一条单独拎出来讲，因为**实测踩过，而且踩下去之后的错误提示会把人引向完全错误的方向**。

本套脚本把 opam root 建在 **`$WORK/opamroot`**（默认 `~/infer-build/opamroot`），
不是 opam 默认的 `~/.opam`。脚本内部每个都有 `export OPAMROOT="${OPAMROOT:-$WORK/opamroot}"`，
所以**跑脚本没问题**。出问题的是你自己开一个 shell 手敲命令的时候：

```console
$ opam update default
[ERROR] Opam has not been initialised, please run `opam init'
```

这句话是**误导性的**——opam 早就初始化过了，只是你这个 shell 里 `OPAMROOT` 是空的，
它去看了 `~/.opam`，那儿确实什么都没有。如果照字面去跑 `opam init`：

```console
$ opam init
[ERROR] Missing dependencies -- the following commands are required for opam to operate:
  - bwrap: Sandboxing tool bwrap was not found. You should install 'bubblewrap'.
```

于是很自然地以为"要装 bubblewrap"。**不是的。**
`00-check-env.sh` 第一屏就标过这台机器没有 bubblewrap，而本套脚本的 `opam init`
一直带着 `--disable-sandboxing`，**从头到尾都不需要它**。
更麻烦的是，那个裸 `opam init` 一旦跑完，会在 `~/.opam` 建出**第二个 opam root**，
之后两边看到的包、switch 完全不同，排查会非常难受。

**正确做法**——任何时候想手敲 `opam ...`，先 source：

```bash
cd 04-setup/scripts
. ./opam-env.sh        # 设好 OPAMROOT / PATH，switch 建好了也一并切过去
opam update default    # 现在才是对的 root
```

`opam-env.sh` 还会顺手检查 `~/.opam` 在不在——在的话提醒你删掉。

> 如果你已经让那个裸 `opam init` 跑完了：`rm -rf ~/.opam` 即可，
> 里面没有任何有用的东西；你真正的 root 在 `$WORK/opamroot`，没被动过。

---

## 常见故障与对策（都是实测踩过的）

| 现象 | 原因 | 对策 |
|---|---|---|
| `initializing opam...` 后无输出 | `opam init` 在下 opam.ocaml.org 或等沙箱确认 | 先手跑 `opam init --bare --no-setup --disable-sandboxing -y` |
| **手敲 `opam 任何命令` 报 `[ERROR] Opam has not been initialised, please run 'opam init'`** | **不是 opam 坏了，是找错 root 了。** 本套脚本把 opam root 建在 `$WORK/opamroot`（默认 `~/infer-build/opamroot`），每个脚本内部都 `export OPAMROOT=...`；而你在一个干净的交互 shell 里手敲时 `OPAMROOT` 是空的，opam 就去看默认的 `~/.opam` —— 那个从来没被初始化过 | **先 source 环境再敲**：<br>`. 04-setup/scripts/opam-env.sh`<br>`opam update default`<br>（v3.1 新增的 `opam-env.sh`，一并设好 `OPAMROOT`/`PATH`/switch）<br>**千万别真的去跑 `opam init`** —— 见下一行 |
| **`[ERROR] Missing dependencies ... bwrap: Sandboxing tool bwrap was not found. You should install 'bubblewrap'.`** | **这是上一行的连锁反应，不是独立故障。** 看到 "has not been initialised" 后顺手跑了裸的 `opam init`，它在 `~/.opam` 建**第二个** root，且没带 `--disable-sandboxing`，于是撞上缺 bubblewrap。`00-check-env.sh` 第一屏就标过 `✗ bubblewrap` | **不需要装 bubblewrap**：脚本里的 `opam init` 一直带 `--disable-sandboxing`。正确动作是 Ctrl-C 掉那个 init，改成 `. 04-setup/scripts/opam-env.sh`。如果 `~/.opam` 已经建出来了，`rm -rf ~/.opam` 删掉（里面没有有用的东西，你真正的 root 在 `$WORK/opamroot`，不受影响） |
| **`$OPAMROOT exists, but does not appear to be a valid opam root`** | **v2.5 及以前脚本 20 的顺序 bug**：预填缓存先建出了 `$OPAMROOT/download-cache`，于是 `[ -d "$OPAMROOT" ] \|\| opam init` 判断成立，**`opam init` 被整个跳过**，后面的 `opam update` 对着半成品 root 报错。而末尾的 `完成` 照样打印（`opam update ... \| tail` 让 `set -e` 失效），看着像成功 | v2.6 已修：顺序改成 init 在预填之前，并对已经踩坑的目录自动修复。**老环境直接重跑新版 20 即可**，它会把已预填的 `download-cache` 挪开、init 完再挪回来 |
| **`curl: (77) error setting certificate file: /tmp/_MEIxxxx/certifi/cacert.pem`** | shell 里残留的 `SSL_CERT_FILE`（或 `CURL_CA_BUNDLE`）指向某个 PyInstaller 打包工具的临时目录，工具退出后目录被清掉了。**报错长得像网络问题，其实跟网络无关** | 当前窗口 `unset SSL_CERT_FILE` 即可，**只影响当前窗口**，其他窗口若需要这个变量不受影响。v3.3 起：10–50 所有脚本、`run-all.sh` 启动时自动清理；`. ./opam-env.sh` 在交互窗口里做同样的事；`00-check-env.sh` 第 0 节会先把它查出来。只清**指向不存在文件**的变量，指向存在文件的（如公司代理根证书）一律保留 |
| **脚本 20 报 `已有的 infer 源码在 xxx，不是目标 commit 3d38229b…，而且工作区有本地改动`** | v3.2 及以前建的环境：infer clone 在最新 main 上，且跑过 45（摘 Rust 前端）或 50（打补丁），工作区有改动。v3.3 的 20 要切到调研基线，但**不替你动有改动的工作区** | 按它给的三选一：切回基线 `git -C $WORK/infer stash -u && git -C $WORK/infer checkout 3d38229b04b649024fee065621f6a4c838f6d029` 然后 `FROM=20 ./run-all.sh`；或维持现状 `INFER_REF=HEAD`；或 `rm -rf $WORK/infer` 重来。见上方「为什么要钉在调研基线 commit」 |
| **脚本 50 报 `补丁打不上，--3way 也合并不了`** | infer 源码不在调研基线上，上游在基线之后改了补丁涉及的文件。50 会先列出**哪些补丁文件被上游改过** | 回到基线重建（命令脚本会打出来）。工作区已自动回滚，不会留下半截合并。想留在新 commit 就按 `03-MiniOobChecker/README.md`「在其他 commit 上安装」手工移植 |
| `rewrote url blocks in 0 / 19125` | 多半是**重复执行**——第一次已经改写完了 | v2.6 起脚本会自己分辨：「之前已改写过」打 ✓，「正则没匹配上」才打 ✗ 并退出 |
| **脚本 30 大面积报 `Could not synchronize ... "git fetch -q" exited with code 128 "RPC failed; HTTP 403"`，但手工 `git clone` 同一个仓库是通的** | 三种可能：① opam 按 CPU 核数**并发** git fetch（47 核就是 47 个并发），被代理限流成 403；② opam 调 git 时的环境/配置跟你交互 shell 不一样，没走到代理；③ 代理按仓库白名单放行 | 按顺序：先 `DLJ=1 ./30-install-deps.sh`（排除 ①）；不行就 `./25-prefetch-git-sources.sh` 把源串行拉到本地再跑 30（同时解决 ①②）；还不行就是 ③，看 `$WORK/.prefetch-failed.txt` 里的仓库清单找网管加白名单 |
| `FileNotFoundError: .../infer.opam.locked` | v2.6 及以前：脚本 20 读 lock 文件时 infer 源码还没 clone（那是脚本 30 干的） | v2.7 已修：20 自己 clone infer；`populate_opam_cache.py` 拿不到 lock 也不再崩 |
| `The following system packages will first need to be installed: libgmp-dev` 然后停住 | opam 的 depext 去调 apt | `opam option --global depext=false`，或 `opam install --no-depexts` |
| `"rsync": command not found` | opam 拷本地 pin 源要用 rsync | 用脚本 10 装的替身 |
| `Missing dependency: camlzip = 1.12 / charon / unknown package` | 这些包在上游仓库里没有或已下架，Infer 用的是仓库内自带版本 | 脚本 30 里的 `opam pin add` 系列；charon 拿不到就走去 Rust 的 lock |
| `Library "charon" not found` | `--disable-rust-analyzers` 不影响 dune 的依赖声明 | 跑 `45-strip-rust-frontend.sh` |
| `fpath ... --dev-pkg=true: No value specified` | **把源改成 git 的副作用**：opam 内置变量 `dev` 从 false 变成 true，激活了上游 `"--dev-pkg=true" {dev}` 那条平时走不到的分支，而包自带的老 topkg 只认空格分隔的 `--dev-pkg true` | **v3.0 起脚本 20 自动修**（`patch_git_source_quirks.py`）。老环境手动补一次：<br>`python3 scripts/patch_git_source_quirks.py $WORK/opam-repository`<br>`. scripts/opam-env.sh && opam update default`<br>（第二行的 source 不能省，否则报 "Opam has not been initialised"） |
| `menhir` / `visitors` / `unionFind` / `store` 拿不到 | 它们在 `gitlab.inria.fr` / `gitlab.com` | menhir 改用 GitHub 镜像 `LexiFi/menhir` 的 `20231231` 标签（满足 `>= 20220210`）；后三个是 charon 的依赖，走去 Rust 路线 |
| `Assertion failed` in `opamRepository.ml` | 给 `file://` 的 extra-source 去掉了 checksum | **不要删 checksum**；改用"预填下载缓存"的办法（脚本 20 第 4 步） |
| 编译到一半 OOM | 内存不够 | 降低 `J`，如 `export J=2` |

---

## 只想验证 MiniOobChecker、不想折腾全套？

MiniOobChecker **必须**从源码构建（它是编进二进制的），没有捷径。
但你可以把范围缩到最小：

1. 找一台**网络正常**的机器（能访问 opam.ocaml.org 和 gitlab.inria.fr）走标准路线；
2. 复用预编译 clang 跳过 LLVM 自举；
3. 全程约 70 分钟。

如果只是想**看摘要机制、跑实验**，那完全不需要自己构建 —— 官方预编译二进制就够了，
本包 `05-experiments/` 的全部实验都是用它跑的。
