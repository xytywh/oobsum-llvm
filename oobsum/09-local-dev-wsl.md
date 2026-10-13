# 本地开发环境：Windows + WSL2 + 上游 llvm-project 14.0.0

> 调研包 v3.15 · 2026-10-13 · 对应 `07-dev-plan.md` 的开发方式调整：**公司电脑不能做开发**，代码在自己的 Windows 电脑（WSL2）里写、编、测，
> 再带到公司电脑上编进 BXPlugin、用 bxen 跑业务代码。脚本都在 `devenv/`，**每一步都在沙箱里实际跑过**（Ubuntu 24.04、2 核 7 GB）。
>
> 证据标签：【实测数据】沙箱实跑；【文档】官方页面；【推断】按实测外推。

---

## 0. 先说结论：接下来按这个顺序做

| # | 在哪 | 做什么 | 大约用时 | 做完的标志 |
|---|---|---|---|---|
| 1 | Windows | 装 WSL2 + **Ubuntu 22.04**，配 `.wslconfig`（§2） | 30 分钟（含重启） | `wsl -l -v` 显示 `Ubuntu-22.04  VERSION 2` |
| 2 | WSL | 把调研包放到 `~/` 下，跑 `00-check-host.sh`、`10-install-deps.sh`（§3） | 15 分钟 | 没有"注意"或都看懂了 |
| 3 | WSL | **`LLVM_KIND=apt ./40-smoke.sh`**（§3） | 2 分钟 | `PASS 6 / FAIL 0`——**这时就能开始写 checker 了**（插件模式，用 Ubuntu 自带的 clang 14.0.0） |
| 4 | WSL | `20-fetch-llvm.sh` + `30-build-llvm.sh`：从 GitHub 源码编 llvm-project 14.0.0（§4） | 下载约 1 分钟；编译挂机：2 核实测约 90 分钟，8 核约半小时 | `clang version 14.0.0` |
| 5 | WSL | `LLVM_KIND=src ./40-smoke.sh`；可选 `50-intree-rehearsal.sh`（§4.4） | 5 分钟；预演 2 核实测 13 分钟（8 核约 4 分钟） | 都是 PASS |
| 6 | 公司 | `fork-facts.sh` 收集公司环境的版本与构建开关；用 `land.py` 一条命令把 I0 探针落进 BXPlugin，按探针 README 跑 R1–R3（§7） | 1 小时（R3 挂机） | 输出贴回给我 |
| 7 | 两边 | 日常循环：我交付的代码包 → 本地编插件、跑用例 → 带去公司 `land.py` 落地、`make -j16`、bxen（§5、§6） | 每轮几分钟 + 一次往返 | 本地用例过了再带去；公司 `make -j16` 通过、bxen 结果贴回 |

第 3 步之后就不必等任何东西：插件模式下，checker 源码改完几秒就能重新加载运行，完全不用重编 clang。
第 4 步可以挂着，和第 3 步之后的开发并行。

---

## 1. 本地环境与公司环境是什么关系

| | 公司 | 本地（WSL） | 一致吗 |
|---|---|---|---|
| LLVM/Clang | `/usr1/llvm-project-14/cmake-build-release`，clang 14.0.0 | `~/llvm-project-14/cmake-build-release`，GitHub `llvmorg-14.0.0`（提交 `329fda39`）；或 Ubuntu 22.04 的 `clang-14`（同为 14.0.0【文档】） | 版本一致；公司那份是否改过，看 `fork-facts.sh` 第 3 节 |
| CSA 宿主 | xir 内嵌 / 搬移的 CSA（`xir/llvm_ast/StaticAnalyzer`），可能有定制 | 上游 clang 14.0.0 的 CSA | **未知**——`fork-facts.sh` 第 6 节给出差异计数 |
| checker 注册 | `Checkers.td` 包块 + 源码 `CMakeLists.txt`，`make -j16` | 日常：**插件**（`-DOOBSUM_PLUGIN_BUILD`，`clang -cc1 -load`）；带去之前：**本地预演** `Checkers.td` 注册（§4.4） | 同一份源码两种注册方式，探针已验证 |
| 运行 | `bxen -exec_mode=onlySa … -checker=rb.enhance.… "-extra_args=…"` | `cc1-run.sh -checker=rb.enhance.… 文件.c`（`clang -cc1 -analyze`，同样带 `-analyzer-max-loop 3`） | checker 名、`-analyzer-config` 写法一致；bxen 另外加载的 `config.ini` / 函数指针配置本地没有 |
| 目标 ABI | 业务代码是 aarch64_be + ILP32（4 字节指针、大端） | 上游 clang 14 **不支持 aarch64 ILP32**：`aarch64_be-linux-gnu_ilp32` 仍按 8 字节指针算，`-mabi=ilp32` 报 `unknown target ABI`【实测数据】；本地近似用 `TRIPLE=armebv7-linux-gnueabi`（4 字节指针 + 大端） | 结构体偏移只能在公司确认（探针 R2） |
| 代码与数据 | 业务代码、全量 `compile_commands.json` | **只有我们自己的代码和 67 个用例**；业务代码不出公司 | |

本地测得到的：checker 逻辑、FIR / 摘要 / 判定、67 个用例与 104 份摘要的对照、注册方式。
只有公司测得到的：xir 的 CSA 定制带来的差异、bxen 的参数与配置、ILP32 布局、业务代码上的告警与性能。

---

## 2. Windows 这边（一次性）

### 2.1 装 WSL2 与 Ubuntu 22.04

管理员身份打开 PowerShell：

```powershell
wsl --install -d Ubuntu-22.04      # 第一次装 WSL 会要求重启；重启后自动打开 Ubuntu，设用户名和密码
wsl -l -v                          # 应显示 Ubuntu-22.04 ... VERSION 2
wsl --update                       # 升到最新的 WSL
```

**为什么是 22.04 而不是默认的 24.04**：22.04 的 gcc 11 直接编 LLVM 14；Ubuntu 22.04 的 `clang-14` 包正好是 **14.0.0**（`1:14.0.0-1ubuntu1.1`【文档】），
与公司一致。24.04 也能用：gcc 13 编 LLVM 14 有两个头文件少 `#include <cstdint>`，`30-build-llvm.sh` 会自动打补丁【实测数据】；
它的 `clang-14` 包是 14.0.6（同一大版本，API 相同）。

### 2.2 给 WSL 分配资源

把 `devenv/wslconfig.example` 复制成 `C:\Users\<你>\.wslconfig`，按电脑改 `memory`、`processors`、`swap`，然后 `wsl --shutdown` 再打开 Ubuntu。
WSL2 默认只用物理内存的一半；编 LLVM 每个编译进程约 1–1.5 GB，链接 clang（lld）约 2–3 GB。

### 2.3 磁盘

WSL 的系统盘是一个 VHDX 文件，默认在 C 盘。整套实测只占约 **3 GB**（源码含 `.git` 1.6 GB + 构建目录 1.0 GB + ccache 0.14 GB）【实测数据】；
算上 apt 包、以后可能再建一个不开断言的构建目录、日常文件，**留 15 GB 以上**就够。
（这是 Release 构建；带调试信息的 Debug 构建要大一个数量级，这里不推荐。）C 盘紧张就挪到别的盘【文档】：

```powershell
wsl --shutdown
wsl --manage Ubuntu-22.04 --move D:\WSL\Ubuntu-22.04     # 较新的 WSL 才有；没有就用下面三行
# wsl --export Ubuntu-22.04 D:\ubuntu.tar ; wsl --unregister Ubuntu-22.04 ; wsl --import Ubuntu-22.04 D:\WSL\Ubuntu-22.04 D:\ubuntu.tar
```

### 2.4 代码放在哪、用什么编辑

* **源码和构建目录一律放在 WSL 的家目录（`~/`）里**，不要放 `/mnt/c`、`/mnt/d`——那是跨系统的文件共享，编译会慢一个数量级。
  Windows 资源管理器里输入 `\\wsl$\Ubuntu-22.04\home\<你>` 能直接看到这些文件。
* 编辑器：**VS Code + WSL 扩展**（在 Ubuntu 里 `code .`），装 clangd 扩展。`plugin-build.sh` 每次会在 `.so` 旁边写一份 `compile_commands.json`，
  clangd 用它做跳转和补全；LLVM 自己的 `cmake-build-release/compile_commands.json` 也在。习惯 CLion 的话，CLion 支持 WSL 工具链。
* **换行符**：`git config --global core.autocrlf input`。脚本要原样带去公司的 Linux，CRLF 会让 bash 报 `$'\r': command not found`。

---

## 3. WSL 里：装依赖、冒烟（第一天就能开工）

```bash
# 把调研包放到家目录（从 Windows 的下载目录解压过来）
cd ~ && tar xzf /mnt/c/Users/<你>/Downloads/infer-research-kit-v3.15.tar.gz
cd ~/infer-research-kit/08-summary2-design/devenv

./00-check-host.sh          # 只读：WSL2？22.04？内存、磁盘、家目录是不是 ext4、git 换行设置、能不能访问 GitHub
./10-install-deps.sh        # sudo apt：gcc、cmake、ninja、lld、ccache、python3-jsonschema，以及 clang-14 / libclang-14-dev / llvm-14-dev
LLVM_KIND=apt ./40-smoke.sh # 用 Ubuntu 自带的 clang 14 做冒烟
```

`40-smoke.sh` 的六项（沙箱里 apt 版 14.0.6 实跑：**PASS 6 / FAIL 0**【实测数据】）：

| # | 检查 | 说明 |
|---|---|---|
| 1 | clang 大版本是 14 | |
| 2 | 上游的 `alpha.security.ArrayBoundV2` 在 `int a[4]; a[4] = 1;` 上报越界 | `clang -cc1 -analyze` 这条路通 |
| 3 | I0 探针编成插件 | 与公司 xir 里**同一份源码**，靠 `-DOOBSUM_PLUGIN_BUILD` 切成插件入口 |
| 4 | `clang -cc1 -load` 加载插件跑探针用例，输出与 `expected-upstream14.txt` 一致 | 20 行 `[OSP]`、2 条探针告警；调用图顺序、递归识别、note 都对 |
| 5 | 目标 ABI | 默认 x86_64（8 字节指针）；`aarch64_be-linux-gnu_ilp32` 在上游 14 上**仍是 8 字节指针**；`armebv7-linux-gnueabi` 给出 4 字节指针 + 大端，作本地近似 |
| 6 | `cases/summaries/replay.py` | 104 份摘要回放 0 处不一致（只要 Python 3） |

---

## 4. 从 GitHub 源码编 llvm-project 14.0.0

### 4.1 为什么还要自己编（apt 版已经能用）

* **断言打开**：`cast<>`、`getAs<>` 用错、迭代器失效，在本地当场断言并给出调用栈，而不是到公司的业务代码上才莫名崩溃。公司那份大概率不开断言。
* 是你要的"基于 GitHub 的 llvm-project"：源码就在手边，CSA 内部（`ExprEngine`、`CallGraph`、`CFG`）可以直接读、加打印、用 gdb 跟。
* 能**预演公司的注册方式**（§4.4）：把 checker 编进 clang 本体，验证 `Checkers.td` 条目、注册函数名、子目录源文件。

### 4.2 命令

```bash
./20-fetch-llvm.sh     # git clone --depth 1 --branch llvmorg-14.0.0 → ~/llvm-project-14，切到本地分支 rb-local，核对 tag 提交 329fda39
./30-build-llvm.sh     # cmake + ninja，产物 ~/llvm-project-14/cmake-build-release（与公司的目录名一致）
LLVM_KIND=src ./40-smoke.sh
```

GitHub 慢或不通：在浏览器里从 GitHub Releases 下载 `llvm-project-14.0.0.src.tar.xz`（约 101 MB），然后

```bash
LLVM_TARBALL=/mnt/c/Users/<你>/Downloads/llvm-project-14.0.0.src.tar.xz ./20-fetch-llvm.sh
```

脚本先核对 sha256（`35ce9edb…e07e23`，沙箱实际下载核对【实测数据】），再解压成一个 git 仓库并打上 `llvmorg-14.0.0` 标签，后面的步骤完全一样。

### 4.3 构建配置（`30-build-llvm.sh`）

| 选项 | 值 | 为什么 |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | 与公司的 `cmake-build-release` 一致；Debug 版构建目录要大几倍 |
| `LLVM_ENABLE_ASSERTIONS` | `ON`（`ASSERTIONS=OFF` 可关） | 见 §4.1；要与公司完全一致时关掉，另用一个 `LLVM_BUILD` 目录 |
| `LLVM_ENABLE_PROJECTS` | `clang` | 只要 clang（含 CSA） |
| `LLVM_TARGETS_TO_BUILD` | `X86` | 分析只用前端；aarch64 的 TargetInfo 在 clang 前端里，不需要 AArch64 后端 |
| `LLVM_USE_LINKER` | `lld` | 链接 clang 快、省内存 |
| `LLVM_CCACHE_BUILD` | `ON` | 切分支、重配置后少编很多 |
| `LLVM_PARALLEL_COMPILE_JOBS` / `LINK_JOBS` | 按内存自动算（内存 GB/2、内存 GB/6） | WSL 里最常见的失败是编译被 OOM 杀掉 |
| `LLVM_ENABLE_PLUGINS` | `ON` | `-load` 插件要靠它 |
| 编译目标 | `clang llvm-config FileCheck count not` | `llvm-config` 给插件取编译参数；后三个是将来跑 lit 测试用的 |

实测【实测数据：沙箱 2 核 7 GB，Ubuntu 24.04 + gcc 13.3，`-j2`】：全量约 **90 分钟**（2 361 个编译步骤），编译时抽查内存占用约 2.5 GB；构建目录 **1.0 GB**（`clang-14` 本体 134 MB），ccache 0.14 GB；
产物 `clang version 14.0.0 (https://github.com/llvm/llvm-project.git <提交号>)`。gcc 13 需要的补丁只有两行（`llvm/include/llvm/Support/Signals.h`、`Base64.h` 各加 `#include <cstdint>`），
脚本把它作为 `rb-local` 上的一个单独提交；gcc 13 还会打出几百条 `-Wmissing-template-keyword` 之类的警告，不影响结果。
8 核按编译并发线性外推约半小时【推断】。

### 4.4 可选：在本地预演公司的注册方式

```bash
./50-intree-rehearsal.sh          # 默认拿 I0 探针做
./50-intree-rehearsal.sh --undo   # 撤销
```

它在单独的分支 `rb-rehearsal` 上用 `land.py --into llvm` 做三件事：给上游 `Checkers.td` 补上 `rb.enhance.core` / `rb.enhance.ipa` 两个包块并把条目追加进去、
把源文件追加到 `clang/lib/StaticAnalyzer/Checkers/CMakeLists.txt`、拷贝源码（入口平铺，引擎在 `OobSummary/` 子目录）；然后增量编译 clang，
**不 `-load`** 直接跑同样的用例，与插件模式和基线比对。沙箱实测【实测数据】：增量编译 801 秒（2 核；改 `Checkers.td` 会让全部 checker 重编，共 201 步），`-analyzer-checker-help` 里出现 `rb.enhance.core.OobSummaryProbe`，
不 `-load` 的输出与插件模式、与基线一致；`--undo` 后重编只用了 43 秒（ccache 命中）。

插件模式验证"代码对不对"，这一步验证"注册对不对"——带去公司之前做一次，往返就少一次。

---

## 5. 日常开发循环

**代码从哪来**：我交付的代码包 `oob-summary-v0.N.tar.gz`（07- 的 WP0、WP1 起），目录布局镜像 BXPlugin，带 `drop.manifest` 和 `land.py`；或者你自己写。
建议在本地建一个 git 仓库 `~/oob-summary`：每个版本解压覆盖后提交，你自己的修改也提交在这里——**带去公司的就是这个仓库的某个提交**，两边对得上。

```bash
cd ~/oob-summary
D=~/infer-research-kit/08-summary2-design/devenv
# 编插件（几秒）：入口 + 子目录里的引擎源码
$D/plugin-build.sh build/OobSummary.so OobSummaryChecker.cpp OobSummary/*/*.cpp -I .
# 跑一个用例：checker 名与公司完全一致
$D/cc1-run.sh -load build/OobSummary.so -checker=rb.enhance.ipa.OobSummary \
    ~/infer-research-kit/08-summary2-design/cases/lv1/lv1_01_stat_enum_butt.c
# 近似公司的 4 字节指针 + 大端
TRIPLE=armebv7-linux-gnueabi $D/cc1-run.sh -load build/OobSummary.so -checker=rb.enhance.ipa.OobSummary <文件.c>
```

（上面的文件名是 WP 代码到了以后的样子；现在可以拿探针把同样的命令走一遍，见 `40-smoke.sh` 第 3、4 步。）

调试：`gdb --args ~/llvm-project-14/cmake-build-release/bin/clang -cc1 -analyze -load build/OobSummary.so …`；
有用的开关：`-analyzer-checker-help`、`-analyzer-config-help`、`-analyzer-display-progress`、`-analyzer-stats`。
插件报 `undefined symbol`：插件和 clang 不是同一套 LLVM（`LLVM_KIND=apt` 编的插件只能给 apt 的 clang 用，`src` 同理），用同一个 `LLVM_KIND` 重编。

---

## 6. 带去公司（每一轮）

1. **本地打包**：只打我们自己的代码，LF 换行，附 sha256。
   ```bash
   cd ~/oob-summary && git archive --format=tar.gz -o ~/oob-summary-v0.N.tar.gz HEAD && sha256sum ~/oob-summary-v0.N.tar.gz
   ```
2. **按公司的安全规定**把这个包传到公司电脑。
3. **公司电脑上**：
   ```bash
   sha256sum oob-summary-v0.N.tar.gz                       # 与本地的对一下
   mkdir -p ~/oob-summary-v0.N && tar xzf oob-summary-v0.N.tar.gz -C ~/oob-summary-v0.N
   cd ~/oob-summary-v0.N
   python3 land.py --drop . --into bxplugin --root /usr1/BXPlugin            # 先演示：列出要拷的文件和三处登记的 diff
   python3 land.py --drop . --into bxplugin --root /usr1/BXPlugin --apply    # 再落地（重复执行安全，落过的跳过）
   cd /usr1/BXPlugin/cmake-build-release-release && make -j16
   # 然后按代码包 README 里的 bxen 命令跑 TestCase 与业务代码
   ```
   `land.py` 做的就是 `csa-checker-dev` 的五处落点：源码进 `xir/llvm_ast/StaticAnalyzer/Checkers/`、`CMakeLists.txt` 的 `add_library()` 末尾追加、
   `Checkers.td` 对应包块的最后追加、用例目录进 `xir/llvm_ast/TestCase/`、`TestCase/CMakeLists.txt` 最后追加 `add_subdirectory`。
   找不到预期结构（比如包块结尾 `} // end "rb.enhance.ipa"`）就停下来，不猜。撤掉：同样的命令加 `--remove --apply`。
4. **带回来**：只带不敏感的结论——告警条数、耗时、我们自己用例上的输出、`fork-facts.sh` 的输出。业务代码与业务告警原文按公司规定处理。

沙箱里在模拟的 BXPlugin 目录上验证过：落地 → 再执行一次（全部跳过）→ 撤掉，`git status` 回到干净【实测数据】。

---

## 7. 第一次去公司：两件事（替代 / 补充 07- 的 I1、I2）

**① `fork-facts.sh`**（只读，约 1 分钟）：

```bash
bash fork-facts.sh > fork-facts.txt      # 路径不同：LLVM14=… BXP=… bash fork-facts.sh
```

| 节 | 回答什么 | 影响本地的什么 |
|---|---|---|
| 1 | 系统、gcc、cmake | 本地选 22.04 还是要对齐别的版本 |
| 2–3 | clang 版本串；llvm-project-14 是不是正好上游 14.0.0（提交 `329fda39`）、之后有没有改动、StaticAnalyzer 改了几个文件 | 本地源码要不要补公司的改动 |
| 4 | LLVM 的构建开关（断言、RTTI、目标、编译器、标准） | `30-build-llvm.sh` 的参数 |
| 5 | BXPlugin 的构建开关 | 插件的编译参数 |
| 6 | **xir 内嵌 CSA 与 llvm-project-14 同名目录的差异计数**（Core / Frontend / Checkers / include） | 本地测试结果在公司能不能复现：改得少，本地就可信 |
| 7 | bxen 链接了哪些 LLVM 库 | |

它不输出任何业务源码；上游 LLVM 的文件名是公开的，xir 自有的文件只给个数。贴回之前请自己过一眼。

**② I0 探针**（原计划不变，落地改用 `land.py`）：

```bash
cd infer-research-kit/08-summary2-design
python3 devenv/land.py --drop probes/i0-integration --into bxplugin --root /usr1/BXPlugin --apply
cd /usr1/BXPlugin/cmake-build-release-release && make -j16
# 按 probes/i0-integration/README.md 跑 R1、R2、R3；用完撤掉：
python3 devenv/land.py --drop probes/i0-integration --into bxplugin --root /usr1/BXPlugin --remove --apply
```

R2 那一行 `[OSP] tu … ptr= long= endian=` 尤其要看：上游 clang 14 不支持 aarch64 ILP32，公司的 bxen 是怎么按 4 字节指针分析业务代码的（或者其实没有），
决定了摘要里的字节布局能不能信。

---

## 8. 常见问题

| 现象 | 原因与处理 |
|---|---|
| 编译中途 `Killed` / `c++: fatal error: Killed signal` | 内存不够被 OOM 杀了：`JOBS=4 ./30-build-llvm.sh` 续编（ninja 只补没编完的）；或在 `.wslconfig` 里加内存、加 swap |
| `$'\r': command not found` | 脚本被转成了 CRLF：`sed -i 's/\r$//' *.sh`，并设 `core.autocrlf input` |
| `'uintptr_t' was not declared`（`Signals.h`） | gcc 13 + LLVM 14；`30-build-llvm.sh` 会自动打补丁——是不是手动跑的 cmake/ninja？ |
| `CMake Error: Unable to find the program ccache` | 没装 ccache：`10-install-deps.sh` 会装；或 `-DLLVM_CCACHE_BUILD=OFF` |
| 插件 `-load` 时 `undefined symbol` | 插件与 clang 不是同一套 LLVM：用同一个 `LLVM_KIND` 重编插件 |
| `error: unknown target ABI 'ilp32'` | 上游 clang 14 不支持 aarch64 `-mabi=ilp32`；本地用 `TRIPLE=armebv7-linux-gnueabi` 近似 |
| WSL 编译很慢 | 源码或构建目录在 `/mnt/c` 下：挪到 `~/` |
| `land.py` 说找到 0 处 `} // end "rb.enhance.ipa"` | 公司 `Checkers.td` 的包块结尾注释写法不同：手工把片段追加到 `RosaRBEnhanceIpa` 块最后，或把实际写法告诉我改脚本 |

---

## 9. 对计划的影响（已同步进 `07-dev-plan.md`）

* 新增输入 **I0′ 本地环境就绪**（第 1–5 步，你自己的时间，不阻塞我写代码）；**I1′ `fork-facts.sh` 的输出**，与 I1（点头决策）同时给。
* 交付形态（I3）细化：代码包除源码与用例外，带 `drop.manifest` 与 `land.py`；**本地先过插件模式与本地预演，再带去公司**。我这边每个代码包交付前，
  在沙箱里用同样的 `devenv/` 脚本跑过插件模式与本地预演（上游 14.0.0 源码构建）。
* 验收不变：G1…G4 仍以公司 fork 上 bxen 的结果为准；本地结果只用来缩短往返。
