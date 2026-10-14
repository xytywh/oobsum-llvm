# devenv：本地开发环境脚本（WSL2 / Ubuntu）

说明文档是上一级的 `09-local-dev-wsl.md`；这里只列脚本。v4.0 起本目录在 llvm-project 分支里（`<仓库根>/oobsum/devenv`），脚本默认把这个仓库当源码树、构建到 `<仓库根>/cmake-build-release/`。全部在沙箱里实际跑过（Ubuntu 24.04、gcc 13.3、2 核 7 GB）。

| 脚本 | 在哪跑 | 做什么 |
|---|---|---|
| `wslconfig.example` | Windows | 复制成 `%UserProfile%\.wslconfig`：给 WSL 分内存、核数、swap |
| `00-check-host.sh` | WSL | 只读检查：WSL2、Ubuntu 版本、内存、磁盘、家目录是否 ext4、git 换行设置、能否访问 GitHub |
| `10-install-deps.sh` | WSL | apt 装编译依赖，以及 Ubuntu 的 `clang-14` / `libclang-14-dev` / `llvm-14-dev`（22.04 上是 14.0.0） |
| `20-fetch-llvm.sh` | WSL | 取 `llvmorg-14.0.0` 源码到 `~/llvm-project-14`（git 浅克隆，或 `LLVM_TARBALL=` 用下载好的源码包，核对 sha256），切到本地分支 `rb-local` |
| `30-build-llvm.sh` | WSL | 编 clang（Release + 断言，只要 X86 后端，lld + ccache，按内存自动定并发）；gcc 13 自动打两行 `<cstdint>` 补丁 |
| `40-smoke.sh` | WSL | 七项冒烟（第 7 项是 OobSummary 的 lit 用例）：版本、上游 checker、探针编成插件、`-load` 跑用例比基线、目标 ABI、摘要回放器。`LLVM_KIND=apt` 或 `src` 选用哪套 LLVM |
| `50-intree-rehearsal.sh` | WSL（可选） | 在 `rb-rehearsal` 分支上按公司的注册方式把 checker 编进 clang 本体再跑；`--undo` 撤销 |
| `plugin-build.sh` | WSL | 把 checker 源码编成插件 `.so`（`-DOOBSUM_PLUGIN_BUILD`），顺带写 `compile_commands.json` 给 clangd |
| `cc1-run.sh` | WSL | `clang -cc1 -analyze` 跑一个文件（本地代替 bxen）；`TRIPLE=armebv7-linux-gnueabi` 近似公司的 4 字节指针 + 大端 |
| `land.py` | WSL / 公司 | 按清单把 checker 落进 BXPlugin（五处落点）：包目录（`drop.manifest`）或仓库内清单（`oobsum/landing/*.manifest`，同步语义）；默认只演示，`--apply` 才写，`--remove` 撤掉，`--check-tree` 查登记是否一致 |
| `fork-facts.sh` | **公司** | 只读：公司的 clang 版本、llvm-project-14 是否上游 14.0.0、构建开关、xir 内嵌 CSA 与上游的差异计数 |
| `env.sh`、`llvm-select.sh` | —— | 公共变量（目录、tag、sha256）与"用哪套 LLVM"的选择，被上面的脚本 source |

最短路径：

```bash
./00-check-host.sh && ./10-install-deps.sh && LLVM_KIND=apt ./40-smoke.sh     # 第一天：PASS 6/6 即可开工
./20-fetch-llvm.sh && ./30-build-llvm.sh && LLVM_KIND=src ./40-smoke.sh       # 挂机：自己编的 14.0.0（带断言）
```
