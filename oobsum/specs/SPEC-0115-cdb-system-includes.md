# SPEC-0115 `run.py cdb` 写编译器的系统头文件目录；文档里的 bxen 命令写成一行

状态：已实现
来源：用户 2026-10-26 在公司按 COMPANY.md §3.1 用 bxen 跑 67 个分级用例：每个 TU 都报 `common.h: fatal error: 'stddef.h' file not found`。
bxen 把 `compile_commands.json` 里的命令换成自己的分析器，不会去找 gcc 的系统头文件，所以命令里要显式写（公司 TestCase 的 CMakeLists 就是显式加的
`-isystem <gcc>/lib/gcc/<triple>/<版本>/include`、`/usr/local/include`、`<gcc>/include`、`…/include-fixed`、`/usr/include`）。`run.py cdb` 原来只写 `cc -c 文件`。

## 2. 设计

1. `run.py cdb` 先问 `--cc` 的默认头文件搜索路径：`<cc> -E -x c - -v` 的 stderr 里 `#include <...> search starts here:` 到 `End of search list.` 之间的目录，
   按原顺序写成 `-isystem <目录>`，放在 `-c` 之前（与 gcc 自己的搜索顺序相同）。拿不到（编译器不存在、输出不认识）时打印警告，照样生成。
2. 新参数：`--flags="…"` 追加到每条命令（要带等号，因为值以 `-` 开头）（别的 `-isystem`、`-D`、目标参数）；`--no-sysinc` 不写系统头文件目录（复现原来的行为）。
3. 打印 `已写 …：N 个源文件（编译器 X，系统头文件目录 K 个）`。
4. 文档里的 bxen 命令写成一行、`-extra_args="…"`（与用户在公司用的写法相同；bash 里与 `"-extra_args=…"` 等价，但多行 `\` 续写粘进 IDE 的运行配置会坏）。
   `oobsum_run.py` 打印的 bxen 命令同样写 `-extra_args="…"`（值里有 `"`、`$`、反引号、`\`、`!` 时仍用 shell 引用）。`-cc_json` 统一写目录。

## 3. 对召回与误报的影响

无（只是让 bxen 能编译用例）。本地模拟（见 §5）与原来的预期相同。

## 4. 实现位置

`oobsum/cases/run.py`（`_system_includes`、`cmd_cdb`、参数）；`oobsum/tools/oobsum_run.py`（`show`）；lit `cases-csa.test`、`driver.test`；
COMPANY.md、`testcase/OobSummaryChecker/`、`probes/i0-integration/`、06- §11 的命令写法。

## 5. 测试

* lit `cases-csa.test`：`cdb` 的命令形如 `gcc [-isystem 目录]… -c 文件`；`--no-sysinc --flags=-DX=1` → `gcc -DX=1 -c 文件`。`driver.test`：打印的是 `-extra_args="…"`。
* 本地模拟 bxen（cc1 不给 resource-dir，只靠 cdb 命令里的参数找头文件，`-fgnuc-version=4.2.1`）：`--no-sysinc` 生成的 cdb 70 个 TU 全部
  `'stddef.h' file not found`（复现公司的报错）；默认生成的 cdb（本机 gcc 11，系统头文件目录 4 个）0 失败，`run.py csa --from-workdir`：
  通过 106、通过+S 20、级别不同 2、超出预期 4、需跨TU 2、回退 0（与 COMPANY.md §3.1 的预期相同）。

## 6. 调试

`run.py cdb` 打印系统头文件目录的个数；看 `compile_commands.json` 的第一条命令。

## 7. 对主规格的修订

无（工具）。

## 8. 公司侧验证

`run.py cdb --out /usr1/oobsum-cases-cdb --cc /usr/local/bin/cc`（与 TestCase 同一个编译器）：命令里的 `-isystem` 应与 BXPlugin 的 `compile_commands.json`
里 TestCase 那条一致（`…/gcc-14.3.0/lib/gcc/x86_64-pc-linux-gnu/14.3.0/include` 等）；再按 COMPANY.md §3.1 跑 bxen 与 `--from-workdir`。

## 9. 待定 / 以后

无。
