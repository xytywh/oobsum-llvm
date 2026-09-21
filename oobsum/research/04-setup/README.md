# 环境搭建

对应报告 **§20**。三条路，按你要做的事选。

> **受限网络环境（opam.ocaml.org 不可达、但能 git clone GitHub）请直接看
> [`01-restricted-network-build.md`](01-restricted-network-build.md)** —— 那里有编号脚本 `scripts/00`…`scripts/50`，
> 按顺序执行即可，并且解答了「`./build-infer.sh clang` 卡在 `initializing opam...`」这个问题。

---

## 路线 1：官方预编译二进制（**强烈建议先走这条**）

跑通报告 §13–§17 的**全部实验**只需要这条路线，20 分钟内能搞定。
官方 `INSTALL.md` 自己也这么建议："compiling infer's dependencies is time-consuming"。

```bash
curl -L -o infer.tar.xz \
  https://github.com/facebook/infer/releases/download/v1.3.0/infer-linux-x86_64-v1.3.0.tar.xz
tar xf infer.tar.xz
export PATH=$PWD/infer-linux-x86_64-v1.3.0/bin:$PATH
infer --version           # → Infer version v1.3.0
```

体积：下载 276 MB，解压后约 1.3 GB（其中 `clang/install` 占 935 MB —— 它自带一套定制 Clang 21.1.6）。

跑一下：

```bash
cd ../05-experiments && ./run-all.sh
```

---

## 路线 2：从源码构建（只有要改 Infer 时才需要，比如跑 MiniOobChecker）

### 系统依赖（`INSTALL.md` 原文 + 官方 Dockerfile）

```bash
apt-get install --yes --no-install-recommends \
  autoconf automake bubblewrap bzip2 cmake curl g++ gcc git libc6-dev libgmp-dev libmpfr-dev \
  libsqlite3-dev sqlite3 make opam openjdk-11-jdk-headless patch patchelf pkg-config \
  python3 python3-distutils unzip xz-utils zlib1g-dev
```

另需 `opam >= 2.0.0`；OCaml 版本由 `build-infer.sh` 指定为 **5.4.1+flambda**。
`charon = 0.1.177` 只有 Rust 分析需要。

### 标准流程

```bash
git clone https://github.com/facebook/infer.git
cd infer
./build-infer.sh clang                       # 只构建 C/C++/ObjC 分析器
# 或 ./build-infer.sh --local-opam-switch clang   （switch 建在仓库内 _opam/，不污染全局）
sudo make install                            # 或 export PATH=$PWD/infer/bin:$PATH
infer --version
```

`build-infer.sh` 依次做：`opam init` → 建 switch → pin 仓库自带的
`dependencies/{charon,ppx_show,pyml,camlzip}` → `opam install --deps-only opam/infer.opam.locked`
→ `facebook-clang-plugins/clang/setup.sh`（**从源码编译整套定制 LLVM/Clang，这一步最耗时**）→ `make`。

> **走受限路线（`scripts/10…50`）的话，opam root 不在 `~/.opam`，而在 `$WORK/opamroot`。**
> 想自己手敲 `opam ...` 命令时，先 `. scripts/opam-env.sh`，否则会报
> `Opam has not been initialised` —— 那时**不要**去跑 `opam init`。
> 详见 `01-restricted-network-build.md` 的「手敲 opam 命令前先 source 它」。

### 资源预估（实测，2 vCPU / 7.8 GB）

| 阶段 | 耗时 | 产物 |
|---|---:|---:|
| opam init + 仓库准备 | ~5 min | ~1 GB |
| OCaml 5.4.1+flambda 自举 | ~14 min | — |
| 152 个 opam 依赖 | ~40 min | `$OPAMROOT` 3.8 GB |
| `configure` + `make -j2 infer` | ~13 min | `_build` 1.3 GB，二进制 125 MB |
| **合计（复用预编译 clang，跳过 LLVM 自举）** | **≈ 72 min** | **≈ 6 GB** |
| 若从源码编译 LLVM/Clang | 官方警告 "may take a really long time" | 另需数 GB |

**建议**：磁盘预留 ≥ 20 GB，内存 ≥ 8 GB，CPU 越多越好。
跳过 LLVM 自举的做法见 `../03-MiniOobChecker/README.md` 的"加速提示"。

### 先确认一件事

从源码构建强依赖 opam 生态（实测 152 个包）以及 `gitlab.inria.fr`（menhir、charon 的依赖）。
**如果你们内网有 opam 镜像，先确认它可用 —— 这往往比"要不要用 Infer"更先决。**
报告 §20.7 记录了在一个只允许 `git clone` + GitHub Releases 的受限环境里，
我们用哪 9 条变通把构建跑通的（其中"用 git blob 填 opam download-cache"那条最通用）。

---

## 路线 3：Docker

`Dockerfile-upstream` 是仓库 `docker/master/Dockerfile` 的原文，未做任何改动
（两阶段构建：`compilator` 编译，`executor` 只拷产物）。

```bash
docker build -t infer-from-source -f Dockerfile-upstream .
docker run --rm infer-from-source infer --version
docker run --rm -v "$PWD/testcode:/testcode" infer-from-source \
  bash -c "cd /testcode && infer run --bufferoverrun-only -- gcc -c example.c"
```

要在 Docker 里验证 MiniOobChecker，在 `compilator` 阶段的 `git clone` 之后插入：

```dockerfile
COPY 03-MiniOobChecker/src/MiniOob*.ml  /infer/infer/src/labs/
COPY 03-MiniOobChecker/src/MiniOob*.mli /infer/infer/src/labs/
COPY 03-MiniOobChecker/patch/0001-register-minioob-checker.patch /infer/
RUN cd /infer && git apply 0001-register-minioob-checker.patch
```
