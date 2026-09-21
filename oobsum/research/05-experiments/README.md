# 复现实验

对应报告 §13–§17 的每一张数据表。全部用**官方预编译 infer v1.3.0** 即可跑，不需要从源码构建。

## 怎么跑

```bash
export PATH=/path/to/infer-linux-x86_64-v1.3.0/bin:$PATH
./run-all.sh
```

脚本会逐组打印【预期】与实际输出，直接对照报告核对即可。全部跑完约 3–6 分钟（2 vCPU）。
也可以只跑某一组：`cd 01-thirteen-scenarios && infer run --bufferoverrun --pulse -- gcc -c c02_for.c`。

## 每组实验对应报告哪一节

| 目录 | 报告 | 要看的是什么 |
|---|---|---|
| `00-basic-summary/` | §13.1–§13.3 | **最重要的一组。** `example.c` 验证跨两层的越界归因；`sub.c` 展示三层调用链上形参→实参的逐层代换 —— 这是整份调研的核心证据 |
| `01-thirteen-scenarios/` | §14 | 循环 / switch / 指针算术 / 结构体 / 别名 / 全局 / 嵌套 / 递归 / 函数指针 / 复杂 CFG。默认配置与最高灵敏度各跑一遍，看 FN/FP 取舍 |
| `02-call-depth/` | §15 | 2 / 20 / 200 层调用链的耗时、摘要条数、报告归因 |
| `03-path-explosion/` | §16 | 每层多分支（理论 4²⁰ 条路径）+ Pulse 析取上限下主动丢状态的计数 |
| `04-global-arrays/` | §14.3 | 带初始化器 vs 不带初始化器的对照 —— 全局数组漏报的根因就是这组用例定位的 |
| `05-incremental/` | §17 | 改一个底层函数，看哪些摘要被传递失效、哪些被复用 |

## 关键命令备忘

```bash
# ★ 把 BufferOverrun 的摘要打印出来（不是 --dump-json-summaries，那个只覆盖 Pulse）
infer debug --procedures --procedures-name --procedures-summary \
            --procedures-summary-skip-empty --select all

# 最高灵敏度（把"符号条件只传不报"这条分支关掉，用低漏报换误报）
infer run --bufferoverrun-only --bo-debug 4 \
          --enable-issue-type BUFFER_OVERRUN_L4 \
          --enable-issue-type BUFFER_OVERRUN_L5 \
          --enable-issue-type BUFFER_OVERRUN_U5 -- <构建命令>

# 直接读 SQLite 里的摘要 blob
python3 -c "import sqlite3;c=sqlite3.connect('infer-out/results.db');\
print([r[1] for r in c.execute('PRAGMA table_info(specs)')])"
```

## 已知坑（实测踩到的，值得在设计你们自己的 CI 时注意）

1. **`infer analyze` 默认会先清空 specs 表**，只有 `--incremental-analysis` 或 `--continue-analysis`
   才复用上次的摘要。这一条不知道的话，会误以为"摘要根本没被复用"。
2. **`--incremental-analysis` 必须配 `--changed-files-index`**，否则直接报错退出。
3. **这个 index 同时决定"分析哪些文件"**。capture 只捕获变更文件没问题，
   但 analyze 的 index 必须覆盖**需要出结论的全部文件** —— 只写变更文件的话，
   上游 Caller 的摘要会被失效**但不会在本轮重算**，顶层越界就报不出来。
4. **失效数量依赖 capture 历史**。同一份改动，实测出现过 `Invalidated 6 of 8` 和 `7 of 8` 两种结果：
   若上一轮 capture 已把某过程标记为 unchanged，之后该过程真的改了，`changed` 标志有可能没被更新，
   极端情况下会看到 `Invalidated 0 of 8`（源码不变时这是正确的，改动后出现就是异常）。
   `run-all.sh` 的场景 A / 场景 B 各自从全新的 `infer-out` 开始就是为了避开这个。
   **CI 里请以场景 B 的命令序列为准**，并且不要把"失效条数"当成断言指标 ——
   要断言就断言"顶层报告是否重新出现"。
5. 失效判定除了调用图可达，还包含 **`used_tenv_sources`（类型环境依赖）**，
   所以失效集合会比纯调用图可达集大一点，这是设计使然不是 bug。
