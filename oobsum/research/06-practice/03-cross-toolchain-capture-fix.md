# 跨编译工具链 capture 失败定位与修复：aarch64_be-ilp32 工程

> **来源说明**：本文与同目录 `capture-cross-toolchain.sh` 由使用方在自己的
> HERT_BBU / RTOS 工程上实地定位后写成，收录进本调研包。文中的路径、工具链版本、
> cdb 位置都是那台机器上的实际值，**换工程需要逐个替换**（见 §五 的路径表）。
>
> 定位方法本身是通用的：**infer 会用自带 clang 替换 cdb 里的编译器，
> 且不继承 `--target` / `--sysroot`**——这一条对任何交叉编译工程都成立，
> 也是本包 `01-whole-program-analysis.md` 里"体检 cdb"那一节的下一道坎：
> 路径都解析得到了，不代表编译得动。

> **v3.8 更新（使用方修订脚本）：C++ 标准头改为 gcc-14.3.0 自带的 libstdc++，不再注入 infer 自带的 libc++/v1。**
> §三「方案 A」的命令与参数表已同步；§三「验证」和 §七「全量执行结果」是 libc++/v1 方案时测的，
> 换成 libstdc++ 后需要重跑回填（命令见 §七 末尾）。

> 回答的问题：**`infer capture --compilation-database .../compile_commands.json`
> 在一个 aarch64_be + ilp32 的 RTOS 工程上直接报
> `error: "__WORDSIZE is not defined"` 一片红，怎么定位、怎么修？**

## 一句话答案

infer 用自带 clang-21 替换 cdb 里的编译器，但**不带 `--target` / `--sysroot`**；
cdb 里的 `aarch64_be-linux-gnuilp32-gcc` wrapper 在本机又指向一个不存在的 gnu64 工具链；
cdb 里 `-isystem` / `-I` 还指向已卸载的 `bisheng_embedded_cpu-305.1.0.b003`。
三层叠加 → 自带 clang 按 x86-64 host 编译 → 华为自定义 `bits/wordsize.h` 取不到
`__aarch64__` / `__ILP32__` → 走到 `#error "__WORDSIZE is not defined"`；
即便补上目标宏，C++ 标准头也找不到。

修复无需改 cdb，给 infer 加一组 `--Xclang` / `--clang-isystem-to-override-regex`
参数即可（见下文「方案 A」）。

---

## 一、复现

```bash
infer capture --compilation-database \
  /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json
```

输出（节选）：

```
Starting translating 10232 files
In file included from .../ltrc_innertrace.c:7:
.../rtos_arm32a72ilp32be_optimized/vos_typdef.h:23:
/opt/buildtools/rtos/.../sdkilp32/usr/include/bits/wordsize.h:23:2:
  error: "__WORDSIZE is not defined"
/opt/buildtools/rtos/.../sdkilp32/usr/include/bits/wordsize.h:46:2:
  error: "Unknown __WORDSIZE detected"
... (10232 文件全部失败)
```

---

## 二、定位过程

### 2.1 cdb 里的编译器根本跑不起来

- cdb 共 10640 条，10232 条用 `aarch64_be-linux-gnuilp32-gcc`，408 条用 `gcc`。
- `aarch64_be-linux-gnuilp32-gcc` 是 wrapper 脚本
  (`/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek/bin/aarch64_be-linux-gnuilp32-wrapper.sh`)，
  内部 `exec "${DL_SDK_HOME}"/../gnu64/bin/aarch64_be-linux-gnu-gcc`。
- 本机 `208.12.0/` 下**没有 `gnu64/`**（只有 `arm64be_5.10_ek_llvm/`），
  所以 wrapper 直接 `exit 127: /../gnu64/bin/aarch64_be-linux-gnu-gcc: No such file or directory`。

### 2.2 即便 wrapper 能跑，infer 也不会用它

- infer v1.3.0 自带 clang-21.1.6：
  `infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install/bin/clang`
- 对 `--compilation-database` 模式，infer 读取每条命令后**剥离原编译器名**，
  把剩余参数交给自带 clang。从 `infer-out/logs` 抓到的实际调用：

  ```
  clang -### invocation:
    '.../facebook-clang-plugins/clang/install/bin/clang'
    '@cdb_clang_args.tmp.XXXXX.txt'
  ```

  `cdb_clang_args.tmp.XXXXX.txt` 内容里**没有** `--target` / `--sysroot`，
  只有原始的 `-I` / `-D` / `-isystem` / `-c <source>`。

- 这意味着原 wrapper 提供的 `--target=aarch64_be-target-linux-gnu_ilp32
  -mbig-endian --sysroot=.../sdkilp32/` 全部丢失。

### 2.3 自带 clang-21 不支持 aarch64_be + ilp32 ABI

- `clang --print-targets` 列出 `aarch64_be`，但没有 ilp32 ABI 支持。
- 用 `--target=aarch64_be-target-linux-gnu_ilp32` 跑自带 clang：
  ```
  #define __LP64__ 1
  #define __SIZEOF_POINTER__ 8
  #define __aarch64__ 1
  ```
  错误地按 LP64 处理。
- 对比华为 BiSheng clang
  (`/opt/buildtools/rtos/.../arm64be_5.10_ek_llvm/llvm64/bin/clang`，15.0.4 + 自定义补丁)：
  ```
  #define __ILP32__ 1
  #define __SIZEOF_POINTER__ 4
  #define __aarch64__ 1
  ```
  这才是该 RTOS 期望的目标语义。

### 2.4 华为自定义 `bits/wordsize.h` 严格依赖目标宏

`/opt/buildtools/rtos/.../sdkilp32/usr/include/bits/wordsize.h` 是华为定制版本：

```c
#pragma once
#if defined (__bpf__)
  #define __MHWORDSIZE 64
#elif defined (__arm__)
  #define __MHWORDSIZE 32
#elif defined (__aarch64__) && defined (__LP64__)
  #define __MHWORDSIZE 64
#elif defined (__aarch64__)
  #define __MHWORDSIZE 32
#else
  #include <bits/wordsize.h>          // 系统 glibc wordsize.h
  #if defined (__WORDSIZE)
    #define __MHWORDSIZE __WORDSIZE
  #else
    #error "__WORDSIZE is not defined"     ← 报错位置 1
  #endif
#endif

#if __MHWORDSIZE == 32
  #include <bits/wordsize-32.h>           // sdkilp32 里存在
#elif __MHWORDSIZE == 64
  #include <bits/wordsize-64.h>           // sdkilp32 里【不存在】
#else
  #error "Unknown __WORDSIZE detected"    ← 报错位置 2
#endif
```

三种状态对应三种报错：

| 自带 clang 的目标状态 | `__aarch64__` / `__LP64__` | 走哪条分支 | 报错 |
|---|---|---|---|
| 默认 x86-64 host | 都没定义 | else 分支 | `__WORDSIZE is not defined` |
| `--target=aarch64_be-target-linux-gnu_ilp32` | 有 `__aarch64__`，有 `__LP64__` | 64 位分支 | `'bits/wordsize-64.h' file not found` |
| 同上 + `-U__LP64__ -D__ILP32__=1` | 有 `__aarch64__`，无 `__LP64__` | 32 位分支 | ✅ 通过 |

### 2.5 cdb 中 `-isystem` / C++ 头路径全部失效

cdb 里的 `-isystem` 与 C++ 包含路径都指向
`/opt/buildtools/bisheng_embedded_cpu-305.1.0.b003/...`，但本机只装了 `b009`：

```
-isystem /opt/buildtools/bisheng_embedded_cpu-305.1.0.b003/hcc_arm64be/bin/../lib64/gcc/aarch64_be-linux-gnu/14.3.0/include
-isystem /opt/buildtools/bisheng_embedded_cpu-305.1.0.b003/hcc_arm64be/bin/../lib64/gcc/aarch64_be-linux-gnu/14.3.0/include-fixed
-isystem /opt/buildtools/bisheng_embedded_cpu-305.1.0.b003/hcc_arm64be/bin/../lib64/gcc/aarch64_be-linux-gnu/14.3.0/../../../../aarch64_be-linux-gnu/include
-isystem /opt/buildtools/rtos/.../arm64be_5.10_ek/bin/../sdkilp32/usr/include
```

即使解决了 wordsize.h，接下来还会报 `fatal error: 'stddef.h' file not found`、
`'map' file not found`、`'cstddef' file not found` 等。

---

## 三、修复方案

### 方案 A（推荐）：保留 cdb，给 infer 加一组覆盖参数

不重新生成 cdb，靠 infer 自带的 flag 改写能力绕开缺口：

```bash
GCC_CXX_INC_BASE="/opt/buildtools/gcc-14.3.0/include/c++/14.3.0"   # v3.8：gcc 自带 libstdc++

infer capture \
  --compilation-database /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json \
  --keep-going \
  --Xclang "--target=aarch64_be-target-linux-gnu_ilp32" \
  --Xclang "-mbig-endian" \
  --Xclang "-U__LP64__" \
  --Xclang "-D__ILP32__=1" \
  --Xclang "--sysroot=/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek_llvm/sdkilp32/" \
  --Xclang "-I$GCC_CXX_INC_BASE" \
  --Xclang "-I$GCC_CXX_INC_BASE/x86_64-pc-linux-gnu/32" \
  --Xclang "-I$GCC_CXX_INC_BASE/backward" \
  --clang-isystem-to-override-regex "/opt/buildtools/bisheng_embedded_cpu"
```

| 参数 | 作用 |
|---|---|
| `--Xclang "--target=aarch64_be-target-linux-gnu_ilp32"` | 让自带 clang 定义 `__aarch64__`、按 aarch64_be 处理 |
| `--Xclang "-mbig-endian"` | 大端模式 |
| `--Xclang "-U__LP64__"` + `-D__ILP32__=1` | 强制 wordsize.h 走 32 位分支，绕过自带 clang 不支持 ilp32 ABI 的缺口 |
| `--Xclang "--sysroot=.../arm64be_5.10_ek_llvm/sdkilp32/"` | 提供与 ilp32 匹配的 libc 头 |
| `--Xclang "-I$GCC_CXX_INC_BASE"` | libstdc++ 主头目录，提供 `<algorithm>` / `<map>` / `<set>` / `<cstddef>` / `<cstdint>` / `<string>` 等 C++ 标准头（v3.8） |
| `--Xclang "-I$GCC_CXX_INC_BASE/x86_64-pc-linux-gnu/32"` | 平台相关头（32 位 ABI），提供 `bits/c++config.h` 等（v3.8） |
| `--Xclang "-I$GCC_CXX_INC_BASE/backward"` | 向后兼容头，提供 `<hash_map>` / `<hash_set>` / `<strstream>` 等老式 GNU 扩展（v3.8） |
| `--clang-isystem-to-override-regex "/opt/buildtools/bisheng_embedded_cpu"` | 把 cdb 中失效的 `-isystem .../b003/.../include` 替换为 infer 自带 clang 的 `lib/clang/21/include`，提供 `stddef.h` / `stdint.h` 等（libstdc++ 的 `<cstddef>` 要的编译器自带 `stddef.h` 也靠它补全） |
| `--keep-going` | 单个文件 capture 失败不中断，最终仍能产出可分析的 `infer-out` |

> **为什么不用 `--clang-libcxx-include-to-override-regex`？**
> 最初的设计是把这个选项与 `--clang-isystem-to-override-regex` 并列使用，
> 让它把 cdb 里的 `-I .../b003/.../include/c++/14.3.0` 重定向到 infer 自带 libc++/v1。
> 但实测在 infer v1.3.0 上，两个选项同时设置时，libc++ 选项**没有独立工作**——
> 所有匹配 `bisheng_embedded_cpu` 的 `-I` 和 `-isystem` 都被替换成了
> `clang/21/include`（stddef.h 那个），`c++/v1` 路径完全没出现在最终的
> `clang -###` 输出里。改用 `--Xclang "-I.../c++/v1"` 显式注入后，
> `grep -c 'c++/v1' capture.log` 才有命中，C++ 文件的 `'algorithm' file not found`
> 等 fatal error 也消失了。

> **为什么又从 libc++/v1 换成 gcc 自带的 libstdc++？（v3.8，使用方修订）**
> 业务代码是按 libstdc++ 写、用 libstdc++ 编的（原工具链的 C++ 头就是 `c++/14.3.0`）。
> 换成 libc++ 等于换了一套标准库实现：libstdc++ 特有的头和扩展（如 `<bits/stdc++.h>`、
> `__gnu_cxx::` 下的扩展）在 libc++ 里没有；代码若依赖 libstdc++ 头之间的隐式包含，
> 在 libc++ 下也会编不过。直接用 gcc-14.3.0 的 libstdc++ 更贴近原工具链。
> 三个 `-I` 的顺序与 gcc 驱动默认的 C++ 头搜索顺序一致：主目录 → 平台目录 → `backward`。
>
> **注意平台目录用的是哪一套**【推断】：`x86_64-pc-linux-gnu/32` 是 x86-64 主机 gcc 的
> **i386 多库**配置，不是 aarch64_be ilp32 自己的。两者在 `bits/c++config.h` 的特性宏、
> `atomic_word.h`、`cxxabi_tweaks.h` 等处有差别，对 capture 和越界分析一般无影响；
> 如果 BiSheng b009 工具链里带有目标自己的 libstdc++（平台目录名里含 `aarch64_be` / `ilp32`），
> 换成它会更贴近原编译。查找命令：
>
> ```bash
> find /opt/buildtools -path '*c++/14.3.0*' -name c++config.h 2>/dev/null | grep -iE 'aarch64|ilp32'
> ```

#### 验证（libc++/v1 方案时的记录，libstdc++ 方案待回填）

用 cdb 中两条原本报错的记录手工跑自带 clang，叠加方案 A 的参数后：

- C 文件 `ltrc_innertrace.c`：仅剩 `-Wpointer-to-int-cast` 类 warning，**无 fatal error**
- C++ 文件 `lacl_relation.cpp`：仅剩 2 条 `cast from pointer to smaller type
  'uintptr_t'` 的 error（来自 libc++/v1 内部，因 `__SIZEOF_POINTER__=8` 而触发，
  被 `-ferror-limit 19` 容忍），**无 fatal error**

证明参数组合有效。

#### 已知遗留风险

- 自带 clang 仍把 `__SIZEOF_POINTER__` 按 8 字节算（builtin，无法用 `-D` 改）。
  若被分析的代码强依赖指针 4 字节（如 `sizeof(void*)`、`intptr_t` 宽度），
  分析结果可能偏。本工程的 `VOS_UINTPTR` 已被 typedef 为 `unsigned int`，
  业务层通常不受影响，但需关注。同时这个 builtin 会让 libc++/v1 内部的
  `reinterpret_cast<uintptr_t>(&__x)` 产生 `cast from pointer to smaller type`
  的 error，靠 `-ferror-limit 19` 容忍；若某个 C++ TU 里这种 error 超过 19 条，
  会触发 `fatal error: too many errors emitted` 导致该文件 capture 失败。
  **v3.8 换成 libstdc++ 后这条仍在**：libstdc++ 里有同类写法，例如 `<memory>` 包含的
  `bits/align.h` 中 `std::align` 的 `reinterpret_cast<uintptr_t>(__ptr)`（libstdc++ 13 的头文件如此，
  14.3.0 以本机头文件为准）。它是非模板的内联函数，包含 `<memory>` 的 C++ TU 都会多出这一条 error，
  也计入 19 条上限。
- `Frontend error: Assert_failure src/clang/cAst_utils.ml:249:53` 是 infer 自带
  clang 前端在某些 C++ 构造（如 `ForStmt`）上的崩溃，与目标配置无关，
  `--keep-going` 会跳过，受影响文件不进 `capture.db`。
- cdb 中可能还有少量非 `bisheng_embedded_cpu` 路径的失效项需要单独处理
  （脚本摘要里会列出受影响的源文件，可按需追加 `--clang-block-listed-flags` 等参数）。

### 方案 B（更彻底）：用 BiSheng clang 重新生成 cdb

让 build 系统改用
`/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek_llvm/bin/aarch64_be_ilp32-linux-clang`
重新生成 compile_commands.json：

- cdb 中的 `-I` / `-isystem` 会是真实存在的 `b009` 路径
- cdb 中会自带 `--target=..._ilp32 -mbig-endian --sysroot=.../sdkilp32/`
- BiSheng clang 本身定义 `__ILP32__`，无需 `-D` 覆盖

但 infer 仍会把编译器替换为自带 clang-21，所以仍需保留
`--Xclang "-U__LP64__" --Xclang "-D__ILP32__=1"` 与
`--clang-isystem-to-override-regex`（指向 b009 的 BiSheng clang 内部头路径，
避免与 infer clang 21 的 builtin 冲突），只是路径替换更"干净"。

代价：需要联系 build 系统维护者改工具链配置，工程改动大。

### 方案 C（最干净但风险高）：让 infer 直接使用 BiSheng clang

infer 的 clang 路径硬编码在 `facebook-clang-plugins/clang/install/bin/clang`，
无官方 CLI 选项可换。两条路都不可行：

1. 把 BiSheng clang-15 软链到该位置：BiSheng 15.0.4 与 infer 期望的 clang-21 AST
   可能不一致，**大概率崩 plugin**。
2. 重新编译 infer 链接 BiSheng clang：**工程量过大**。

不推荐。

---

## 四、方案 A 的落地脚本

配套脚本：`capture-cross-toolchain.sh`（与本文件同目录）。

脚本职责：
1. 接收 cdb 路径作为参数（必填），输出目录可选（默认 `./infer-out-cross`，结果在其下的 `infer-out/`）。
2. 内置方案 A 的全部参数。
3. 自检 cdb 是否存在、是否非空。
4. 调用 `infer capture`，把 stdout/stderr 全量保存到日志文件。
5. 跑完打印 `infer-out` 大小、`capture.db` 大小、日志中 `error:` 与
   `fatal error:` 的计数、前 20 条不同 fatal error 的去重样例，便于后续按需追加修复。

用法：

```bash
cd /usr1/BXPlugin/xir/FunctionSummary/infer-research-kit/06-practice
./capture-cross-toolchain.sh /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json
# 或指定输出目录
./capture-cross-toolchain.sh /path/to/compile_commands.json /path/to/output-dir
```

---

## 五、参考路径

| 角色 | 路径 |
|---|---|
| infer 主程序 | `/usr1/BXPlugin/xir/FunctionSummary/infer-linux-x86_64-v1.3.0/bin/infer` |
| infer 自带 clang | `.../infer-linux-x86_64-v1.3.0/lib/infer/facebook-clang-plugins/clang/install/bin/clang` (v21.1.6) |
| infer 全局定义头 | `.../infer-linux-x86_64-v1.3.0/lib/infer/infer/lib/clang_wrappers/global_defines.h` |
| BiSheng clang (15.0.4 + ilp32 补丁) | `/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek_llvm/llvm64/bin/clang` |
| ilp32 sysroot | `/opt/buildtools/rtos/opt/RTOS/208.12.0/arm64be_5.10_ek_llvm/sdkilp32/` |
| 华为定制 wordsize.h | `.../sdkilp32/usr/include/bits/wordsize.h` |
| 失效的 b003 工具链 | `/opt/buildtools/bisheng_embedded_cpu-305.1.0.b003/...` (本机不存在) |
| 实际安装的 b009 工具链 | `/opt/buildtools/bisheng_embedded_cpu-305.1.0.b009/...` |

---

## 六、capture 通了以后：先测能力基线，再解读结果

【本包补充，非原文】跨编译工程用了 `--Xclang -U__LP64__ -D__ILP32__=1` 这类
**人为改写目标宏**的手段，等于让 infer 在一个"半真半假"的目标模型下工作。
在把全量结果当结论之前，建议做两件事：

1. **量化 capture 成功率**。`capture.summary.txt` 里的 fatal error 计数只是下限，
   真正该看的是 `capture.db` 里实际落了多少个源文件，再跟 cdb 的**唯一源文件数**
   （不是记录数）对比：

   ```bash
   # cdb 唯一源文件数（正确分母）
   python3 -c "import json,os;c=json.load(open('compile_commands.json'));print(len(set(os.path.normpath(e['file']) for e in c)))"
   # capture.db 落库数
   sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM source_files;"
   # 两者相等即 100% 落库；cdb 里的重复条目不是失败
   ```

2. **跑一遍能力探针**，确认在这套目标配置下 Infer 的基线能力没被改坏：

   ```bash
   cd ../07-capability-boundary && ./run-boundary.sh tp default
   ```

   如果探针结果跟 `07-capability-boundary/01-capability-boundary-fp-fn.md` 里记录的
   18/22 对不上，说明目标模型的改写影响到了分析本身
   （最可能是 `__SIZEOF_POINTER__` 那条遗留风险），此时全量结果的漏报要打折看。

> **更新**：`capture.summary.txt` 早期版本用 cdb 记录数做分母，得出"96.2%"。
> 实际 cdb 的 10640 条里有 408 条是同文件多 flag 的重复条目，infer 按源文件去重
> 落库，正确分母是 10232 个唯一源文件。capture.db 里正好 10232 条，**100% 落库**。
> 详见下方第七节。

---

## 七、全量执行结果（2026-09-20，libc++/v1 方案）

> **v3.8**：本节数字是 C++ 头还用 infer 自带 libc++/v1 时跑的。换成 libstdc++ 后请按本节末尾的命令重跑，
> 把新数字补成一列对照（C 文件不受这次改动影响，变化只会出现在 C++ 文件上）。

对 1383 全量 cdb（10640 条记录）跑完方案 A，实际结果如下：

| 指标 | 数值 |
|---|---|
| cdb 记录数 | 10640 |
| cdb 唯一源文件数 | 10232 |
| cdb 重复条目（同文件多组 flag） | 408 |
| capture.db 源文件数 | **10232** |
| capture.db 过程数 | 328602 |
| **按唯一源文件的 capture 成功率** | **100.0%** |

### 7.1 修正一个容易误判的点

早期版本摘要用 `cdb 记录数 (10640)` 做分母，得出"96.2% 成功、408 失败"的结论，
看起来还有 408 个文件没 capture 进来。**这是分母选错导致的误报。**

infer capture 按**源文件**去重落库：同一 `.cpp` 在 cdb 里出现 N 次
（不同 build target、不同 `-D`），infer 只 capture 一次，`source_files` 表只落一行。
因此正确分母是 cdb 里的**唯一源文件数**，不是记录数。

```
cdb 记录数 10640 − 重复条目 408 = 唯一源文件 10232 = capture.db 源文件数
```

408 条"差值"全部是重复条目，不是失败。脚本已按此修正。

### 7.2 日志里的 error 是真实的，但没有阻止 capture

日志统计：

| 日志指标 | 行数 | 涉及唯一源文件 |
|---|---|---|
| `error: cast from pointer to smaller type 'VOS_UINTPTR' ...` | 6638 | — |
| `fatal error: too many errors emitted` | 129 | 1 |
| `Frontend error: Assert_failure src/clang/cAst_utils.ml:249:53` | 136 | 17 |

这三类错误都是真实的，但在 `--keep-going` 下都没有阻止文件落库：

1. **`cast from pointer to smaller type`（6638 条）**
   - 根因：ilp32 下 `VOS_UINTPTR = unsigned int`（4 字节），但 infer 自带 clang 的
     `__SIZEOF_POINTER__` 是 8（builtin，无法用 `-D` 覆盖）。
     `reinterpret_cast<VOS_UINTPTR>(ptr)` 在 C++ 中是**硬 error**（不是 warning），
     `-Wno-*` 系列无法降级。
   - 影响：clang error recovery 仍会生成 AST，infer 照常 capture。
     这 6638 条 error 落在远少于 6638 个文件上（宏展开导致一行代码报多条），
     实际只有 `csrm_comm_rat_tlv.cpp` 一个文件因超 `-ferror-limit 19` 触发 fatal。
   - 该文件**仍已落库**（部分 AST），见
     `sqlite3 capture.db "SELECT COUNT(*) FROM source_files WHERE source_file LIKE '%csrm_comm_rat_tlv%';"` = 1。

2. **`too many errors emitted`（129 条，1 个文件）**
   - 只是上一条的连锁反应：单文件 error 数超 clang 默认上限 19 后的终止提示。
   - `--keep-going` 让 infer 用已恢复的 AST 继续，文件仍进 capture.db。

3. **`Frontend error`（136 条，17 个文件）**
   - infer 的 clang 插件（OCaml `cAst_utils.ml`）在某些 C++ 构造上 assert 崩溃。
     这是 **infer 自身的前端 bug**，与目标配置无关。
   - 影响方式：按**方法**跳过（`Aborting translation of method ...`），
     同文件其他方法不受影响，文件本身仍落库。
   - 这 17 个文件全部已在 capture.db 中（已逐一核对）。
   - 如果需要补全这些被跳过的方法，只能升级 infer 版本或给 infer 社区报 bug，
     没有命令行 flag 可以绕过。

### 7.3 结论

方案 A 在 1383 全量工程上的 capture 阶段已经**没有失败文件**，
可以进入 `infer analyze` 阶段：

```bash
infer analyze --results-dir result/infer-out --keep-going
infer report --results-dir result/infer-out
```

需要留意的遗留风险（不阻塞，但影响分析质量）：

- `__SIZEOF_POINTER__=8` 与真实 ilp32（4 字节指针）不一致，
  涉及指针宽度敏感的检查（如指针截断、指针算术溢出）可能漏报。
- 17 个文件的部分方法被 infer 前端跳过，这些方法的分析结果是空的。

### 7.4 v3.8 换成 libstdc++ 后的重跑与对照（待回填）

```bash
cd /usr1/BXPlugin/xir/FunctionSummary/infer-research-kit/06-practice
./capture-cross-toolchain.sh \
    /usr1/518C10/HERT_BBU/project/makesoter/compdb/1383/all/compile_commands.json \
    /usr1/BXPlugin/xir/FunctionSummary/capture-libstdcxx
cd /usr1/BXPlugin/xir/FunctionSummary/capture-libstdcxx

# ① 确认三个 -I 真的进了编译命令、没有残留 c++/v1
grep -c 'gcc-14.3.0/include/c++/14.3.0' capture.log
grep -c 'c++/v1' capture.log                                    # 预期 0
# ② 与上表逐项对照
sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM source_files;"   # 上表 10232
sqlite3 infer-out/capture.db "SELECT COUNT(*) FROM procedures;"     # 上表 328602
grep -c "cast from pointer to smaller type" capture.log             # 上表 6638
grep -c 'too many errors emitted' capture.log                       # 上表 129
grep -c 'Frontend error:' capture.log                               # 上表 136
# ③ 新出现的 C++ 头相关 fatal error（换库后最可能出问题的地方）
grep 'fatal error:' capture.log | grep -oE "'[^']+' file not found" | sort | uniq -c | sort -rn | head
```

① 的第一个命令如果是 0，说明 infer 没把这些 `--Xclang` 打印进日志，改用单文件
`clang -###` 看最终命令行（与 §三「为什么不用 `--clang-libcxx-include-to-override-regex`」里的方法相同）。

| 指标 | libc++/v1（2026-09-20） | libstdc++（v3.8） |
|---|---|---|
| capture.db 源文件数 | 10232 | |
| capture.db 过程数 | 328602 | |
| `cast from pointer to smaller type` | 6638 | |
| `too many errors emitted`（涉及文件） | 129（1） | |
| `Frontend error`（涉及文件） | 136（17） | |
