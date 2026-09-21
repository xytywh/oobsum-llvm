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
./build-infer.sh --local-opam-switch -y clang     # --local-opam-switch：switch 建在仓库内 _opam/，不污染全局
```

耗时的大头是**从源码编译定制 LLVM/Clang**（官方 `INSTALL.md` 原文警告 "may take a really long time"，
实际数小时）。**强烈建议跳过它**：

```bash
# 先下官方预编译包（276 MB）
curl -L -o infer-bin.tar.xz \
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
> 只有脚本 30 大面积报 `HTTP 403` 时才回头插进来跑一次。

### 原理一句话

受限环境通常只放行两样：**`git clone` GitHub** 和 **GitHub Releases 的下载链接**。
而 opam 默认要从 `opam.ocaml.org` 拿仓库、从各种压缩包地址拿源码。
所以我们做三件事：

1. 用 `git clone` 把 opam 仓库整个搬下来，当本地仓库用；
2. 把仓库里"只能 HTTP 下压缩包"的源**改写成"能 git clone 的源"**；
3. 少数只能从 `raw.githubusercontent.com` 拿的小文件，**用 git 取出来、按官方声明的 sha256
   塞进 opam 的下载缓存** —— opam 查缓存命中就不走网络，**而且校验和依然生效**。

第 3 条是最干净的一招：不动 opam 元数据，不关校验。

### 执行

```bash
cd <本调研包>/04-setup/scripts

export WORK=$HOME/infer-build        # 所有产物都放这里，可随便改
export J=$(nproc)                     # 并行度

./00-check-env.sh                      # 只检查，不改东西
./10-install-opam.sh             # opam 本体 + GMP 头文件 + rsync 替身
export PATH=$WORK/bin:$PATH           # ← 脚本 10 会提示你加这行
./20-prepare-opam-repo.sh                  # clone opam 仓库 + 改写源 + 预填缓存（约 10 分钟）
./30-install-deps.sh          # OCaml 5.4.1+flambda + 152 个依赖（★ 最慢，2 核约 55 分钟）

# 下预编译包借 clang 用（强烈建议，能省数小时）
curl -L -o $WORK/infer-bin.tar.xz \
  https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
tar xf $WORK/infer-bin.tar.xz -C $WORK
export PREBUILT=$WORK/infer-linux-x86_64-v1.3.0

./40-build-infer.sh                     # 构建 infer 本体（2 核约 13 分钟）
./50-install-minioobchecker.sh            # 装上我们写的 checker 并跑 demo
```

### 每个脚本干了什么

| 脚本 | 干什么 | 关键点 |
|---|---|---|
| `00-check-env.sh` | 检查命令、头文件、**网络可达性**，给出走哪条路的结论 | 不改任何东西，可反复跑 |
| **`opam-env.sh`** | **`source` 用，不是执行。**设好 `OPAMROOT` / `PATH` / switch，让你在交互 shell 里手敲的 `opam` 命令作用在对的 root 上 | 见下方「手敲 opam 命令前先 source 它」。任何时候你想自己敲 `opam ...`，先 `. ./opam-env.sh` |
| `10-install-opam.sh` | ① 从 GitHub Releases 下 `opam` 二进制<br>② `gmp.h`（apt 不可用时从 `gmp-mirror/gmp-6.3` 取）<br>③ `rsync` 替身 | GMP 只需要**头文件**；系统若已有 `libgmp.so.10`（GMP 6.x）直接建软链，不用编译整个库 |
| `20-prepare-opam-repo.sh` | clone opam 仓库 → `rewrite_opam_urls.py`（改写 ~6900 个包）→ `rewrite_erratique.py`（改写 ~100 个）→ **`patch_git_source_quirks.py`**（修 git 源特有的构建问题）→ **`opam init`** → `populate_opam_cache.py`（预填缓存）→ `opam update` + 自检 | **`opam init` 必须在预填缓存之前**，见下方「v2.6 修复」；带 `--disable-sandboxing` 避开交互提问 |
| **`25-prefetch-git-sources.sh`** | **串行**把 opam 要 git fetch 的源全部 clone 到本地镜像，再把仓库里的 URL 改写成 `file://` | **只在脚本 30 大面积报 `HTTP 403` 时才需要**。跑完之后 opam 装依赖一次网络都不走 |
| `30-install-deps.sh` | 建 switch、装编译器、pin 仓库自带依赖、装 152 个包 | 自动关掉 `depext`（否则会去调 apt 卡住）；charon 拿不到时自动生成去 Rust 的 lock；**下载并发默认压到 4**（`DLJ=1` 可再降） |
| `40-build-infer.sh` | 复用预编译 clang → `autogen` → `configure` → `make` | 设了 `PREBUILT` 就跳过 LLVM 自举 |
| `45-strip-rust-frontend.sh` | 仅在 charon 装不上时由脚本 40 自动调用 | `--disable-rust-analyzers` **不够**，因为 `integration/dune.in` 无条件依赖 charon 库 |
| `50-install-minioobchecker.sh` | 拷源码 + 打注册补丁 + 重新 make + 跑 demo | — |

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
