# SPEC-0086 全局符号的身份：内部链接的全局带哈希后缀；被调者改了本模块看不到的全局要记账（修三处错误 SAFE）

状态：已实现
来源：STATUS 的老问题"全局符号按名字（不同 TU 同名 static 全局）"。探针（`oobsum_run.py local` 跨 TU 三阶段），三处都是**错误的 SAFE**：

```c
/* ① a.c */ static UINT32 g_len = 100; void PutA(UINT8 *p) { p[g_len] = 0; }
/*    b.c */ static UINT32 g_len; void Top_TP(void) { UINT8 a[4]; g_len = 2; PutA(a); }      /* SAFE：b 的 g_len = 2 被代进 a 的 g:g_len */
/* ② l.c */ static void PutL(UINT8 *p) { static UINT32 n = 100; p[n] = 0; }
/*         */ void Top_L(void) { static UINT32 n; UINT8 a[4]; n = 2; PutL(a); }               /* SAFE：两个函数内 static 都叫 g:n */
/* ③ c.c */ UINT32 g_x; void SetX(UINT32 v) { g_x = v; } void PutX(UINT8 *p) { p[g_x] = 0; }
/*    d.c */ void Mid(UINT8 *p) { SetX(100); PutX(p); }               /* d.c 没声明 g_x */
/*    e.c */ extern UINT32 g_x; void Top_X(void) { UINT8 a[4]; g_x = 2; Mid(a); }             /* SAFE：Mid 里 SetX 改过 g_x，没记 */
```

①②：整数全局标量的接口符号是 `g:<名字>`，不同对象同名就混了。③：`Mid` 调 `SetX` 后 `mods` 里有 `g:g_x`，可 `d.c` 里没有这个全局，
`ApplyMods` 只把它往上传，本函数后面的调用点 `gvals` 里不知道它改过，`PutX` 的 `g:g_x` 被当成 `Mid` 入口的值。

## 2. 设计

* **符号带身份**：外部链接（键 `G:…`）照旧 `g:<名字>`；内部链接（文件内 `static`，键 `G@<文件>:…`；函数内 `static`，键 `S@<函数键>:…`）
  写成 `g:<名字>__S<键的哈希前 16 位>`（仍是标识符，D-21 语法与 schema 不用改）。生成处：读全局标量（`LoadValue`）、调用点 `gvals`（`GlobalsAt`）、
  写效应候选（`FieldSlots`）、`mods`（`AddMod`）。对象名（`obj`，告警里的 `'g_len'`）不变。
* **查表**：按符号查（`GlobalsBySym`）；`mods` 查不到时再按显示名 `g:<名字>` 查（老格式 / 金标准，多失效一些，保守）。写效应 `global` 的目标只按符号精确查
  （写错对象就是错误的值）。
* **看不到的全局**：`ApplyMods` 里 `g:…` 按符号查不到 → 把这个符号记进 `writtenAll`（`g:` 开头，与基址 id 不冲突）；`GlobalsAt` 对这些写 `"?"`
  → 后面调用点上被调者的同一个符号被改名（SPEC-0063 / 0064 的 poison）→ 未知。
* **显示**：`Readable` 去掉 `__S<16 位十六进制>` 后缀；`Dump` 里保留全名。

## 3. 对召回与误报的影响

**召回**：修掉三处错误的 SAFE（变成 SUSPECT `entry_unproven` 等）。外部链接的全局符号不变，同一对象在不同 TU 里仍能代换。
**误报**：无新增（只在不同对象、或确实改过时不再代换）。

## 4. 实现位置

`core/Key.{h,cpp}`（`GlobalSymbol`、`StripGlobalSuffix`）、`core/Interp.cpp`（上面几处 + `IndexGlobals` / `GlobalsBySym` / `ApplyMods` / `FieldWrites`）、
`core/Engine.cpp`（`Readable`）。

## 5. 测试

* lit `global-identity.test`（跨 TU 三阶段）：①②③ 的 `Top_*` 不再 SAFE（判定日志里是 suspect）；对照：同一 TU 里调用方改了同一个 static 再调 → 照旧按值判
  （`g_len = 2` → SAFE、`= 10` → DEFINITE / POSSIBLE）。
* 单测 `Key.GlobalSymbol`（外部 / 文件内 / 函数内、去后缀、不是 16 位十六进制的不动）。
* `mod-ref.test` 的 `mods` 按新符号更新（`g:g_ulCnt__S…`、`g:g_ulLen__S…`，有意）；告警正文统一去后缀（`Engine` 收尾处）。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退；compiler-rt 三阶段压测判定不变。

## 6. 调试

`Dump=state` / `summary`：符号全名 `g:<名字>__S…`。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| §1 接口符号 / D-21 | 全局 `g:g_x` | 外部链接 `g:g_x`；内部链接 `g:g_x__S<键的哈希前 16 位>` |
| D-24 `mods`（SPEC-0045） | `g:名字` | 同上的符号；本模块里没有的全局被写过 → 后面调用点上当"改过" |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 全局指针（`ext(g:…)`）现在不跟踪（P0），以后跟踪时同样用带身份的符号。
