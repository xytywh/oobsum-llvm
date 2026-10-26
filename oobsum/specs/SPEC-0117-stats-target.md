# SPEC-0117 `oobsum_run.py stats` 打印分析时用的目标；多架构的跑法与 I0 探针的现状

状态：已实现
来源：用户 2026-10-26：业务代码有几个架构的编译数据库（每个架构一个 cdb 目录），问"大小会不会影响结果、`ptr=` 在哪里看"；问 I0 探针的 R1–R3 是什么。

## 2. 设计

1. 每次运行（任何 `Phase`、不用加 `Dump`）都会写 `WorkDir/fir/<tu>.json`，其中的 `target` 记着分析时用的目标：`triple`、`ptr`（指针字节数）、`long`、`big_endian`。
   `stats` 第一行起按目标分组计数：`目标：<三元组> ptr=<n> long=<n> <big-endian|little-endian>（K 个 TU）`。只解析文件末尾的 `target` 那一段，不读整个 FIR。
   没有 `fir/` 时打印"没有 fir/*.json"。
2. 文档（COMPANY.md §3.2）：目标怎么看、三种结果分别意味着什么、每个架构的 cdb 各跑各的（各自一个 WorkDir）。
3. I0 探针（2026-10-13 写正式代码之前的摸底）的 9 项里，1、2、4–7 已被正式规则在 bxen 里的运行（TestCase、fork-facts）确认，3（目标 ABI）由本 SPEC 的
   `stats` 代替；只剩 8、9（全量时多进程写 WorkDir、全量的规模分布）——可以用 §4.0 的正式规则全量运行 + `stats` 代替。探针改为可选，不再要求跑。

## 3. 对召回与误报的影响

无。

## 4. 实现位置

`oobsum/tools/oobsum_run.py`（`_targets`、`cmd_stats`）；lit `driver-tools.test`；COMPANY.md §3.2、§6；`probes/i0-integration/README.md` 文件头。

## 5. 测试

lit `driver-tools.test`：`local` 跑两个 TU（默认 `armebv7`）后 `stats` 第一行 `目标：… ptr=4 long=4 big-endian（2 个 TU）`。
本地模拟 bxen 的 WorkDir：x86_64 → `ptr=8 long=8 little-endian（70 个 TU）`；i386 → `ptr=4 long=4 little-endian`。

## 6. 调试

单个 TU：`grep -A5 '"target"' WorkDir/fir/<tu>.json`；或 `Dump=fir` 时 stderr 第一行 `[oobsum:fir] module … target=… ptr=… long=… …-endian`。

## 7. 对主规格的修订

无（工具）。

## 8. 公司侧验证

对已经跑过的三个 WorkDir 各执行一次 `stats`，带回每个的"目标："那一行（三元组、ptr、long、大小端，不含业务信息）。

## 9. 待定 / 以后

若单板产品的 cdb 被分析成 `x86_64 ptr=8`：试在 `-extra_args` 里指定目标（如 `-target armebv7-linux-gnueabi`，上游 clang 14 没有 aarch64 ILP32，用 32 位大端 ARM 近似，
与本地 `run.py csa` 的口径相同），先确认 bxen 是否接受，再定。
