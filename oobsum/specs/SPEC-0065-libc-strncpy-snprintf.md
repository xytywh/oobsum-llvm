# SPEC-0065 内置模型补上 libc 的 `strncpy` / `snprintf` / `vsnprintf`

状态：已实现
来源：探针（常见库函数）：`memcpy` / `memmove` / `memset` 与 securec 族都有模型，`strncpy`、`snprintf`、`vsnprintf` 没有——只记 `not_in_tu`（SUSPECT），
`strncpy(b8, s, 32)` 这种确定越界的写判不出级别。06- F-05 把 libc 的拷贝函数当成"代价很小，顺带支持"。业务代码以 securec 为主，libc 函数少但会出现。

## 2. 设计

| 函数 | 条件 | 依据 |
|---|---|---|
| `strncpy(d, s, n)` | 写 `d` 的 `n` 字节（确切）；读 `s` 最多 `n` 字节（`upto`） | C 标准：不足 `n` 时用 NUL 补满 `n` 字节；源读到 NUL 或 `n` 字节为止 |
| `snprintf(d, n, …)`、`vsnprintf(d, n, …)` | 写 `d` 最多 `n` 字节（`upto`） | C 标准：最多写 `n` 字节（含结尾 NUL） |

都不带 securec 的 `destMax` 规则（`dest_max` 不写）。`strcpy` / `strcat` / `sprintf` / `vsprintf` 的写入长度取决于源串，仍按 `not_in_tu` 记账（源串长度以后）。
内置表与示例模型文件 `oobsum/config/oob_models.example.yaml` 同步（`model-file.test` 对照两边）。

## 3. 对召回与误报的影响

**召回**：`strncpy` 的 `n` 超过目的大小判 DEFINITE / POSSIBLE（原来只有 SUSPECT）；`snprintf` 的 `n` 超过目的大小判 POSSIBLE。`n` 在界内的判 SAFE（原来 SUSPECT）。

## 4. 实现位置

`core/Models.cpp`（内置表）、`oobsum/config/oob_models.example.yaml`。

## 5. 测试

* lit `libc-models.test`：`strncpy(b8, s, 32)` → DEFINITE、`strncpy(b8, s, 8)` → SAFE；`snprintf(b8, 32, …)` → POSSIBLE、`snprintf(b8, 8, …)` → SAFE；对照 `strcpy` 照旧 `not_in_tu`；`Dump=models` 有三条。
* `model-file.test`（内置表 = 示例文件）不变地通过；`run.py csa` 不回退（用例里没有这几个函数）。

## 6. 调试

`Dump=models`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-05 | libc `memcpy` / `memmove` / `memset` | 另加 `strncpy`（写 `n`、读最多 `n`）、`snprintf` / `vsnprintf`（写最多 `n`） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* `strcpy` / `strcat` / `sprintf` 的源串长度（字符串常量的长度已知时可以算）。
