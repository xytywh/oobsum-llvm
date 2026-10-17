# SPEC-0073 写效应 `field`：被调者写形参所指对象的整数字段（含 `*pulLen` 的伪字段 `_0`）

状态：已实现
来源：06- D-24 的 `field`（`target: "p0->ulUsed"`、`value`）/ `out_int` 只列了名字，引擎没产出过（金标准 LV5-01 手写了 `pstCtx->ulUsed := ulLen`）。
现在被调者写了调用方结构体的字段、或经出参写回长度（`*pulLen = n`）之后，调用方只知道"被改过、值不知道"：

```c
static void Cfg_Init(CTX_S *pst) { pst->ulLen = 64; }
static UINT32 Get_Len(UINT32 *pulLen, UINT32 ulMax) { if (ulMax < 8) return ERR; *pulLen = 32; return OK; }
void Top_TP(void) { UINT8 a[16]; CTX_S st; Cfg_Init(&st); Fill(a, &st); }                 /* 写到 64 */
void Len_TP(void) { UINT8 a[16]; UINT32 n = 0; (void)Get_Len(&n, 16); Fill2(a, n); }       /* n ∈ {0, 32} */
```

## 2. 设计

### 2.1 推导（`RecordReturn` / `Effects`）

候选：每个指针形参 K 所指对象里的整数字段（所指是结构体：各整数字段；所指是宽于 `char` 的整数：伪字段 `_0`，SPEC-0071）。
记录那一遍里每个 `ret` 时，每个候选在这条路径上是三种之一：

* **写了**：格里有确切值（接口符号的式子）；
* **没碰**：格不在、进函数后也没改过（`!Changed`）——值就是入口值；
* **不知道**：其余（写了不确切的值、经不定偏移写过、指针交给过被调者…）。

某个候选在所有路径上都不是"不知道"、至少一条"写了"、写了的值都相同 → `{kind: "field", target: "pK->f", value: V}`〔由 SPEC-0074 修订：都是常数但不同时记 `range`〕；有路径"没碰"时加 `may: true`。
`pK->f` 写进摘要的 `fields` 表（调用点按它找偏移）。

### 2.1a 函数末尾自然返回

记录那一遍里，走完一个通向出口块、又没有 `ret` 指令的块，按"隐式返回"再记一次（出参格、字段状态；没有返回值）——`void Init(CTX_S *p) { p->ulLen = 64; }`
这类没写 `return` 的函数原来一条路径都没记，`out_new`（SPEC-0053 / 0061）也同样受益。

### 2.2 调用点

在应用 `mods` **之前**算新值：`V` 换成调用方的说法（`CallerExpr`，SPEC-0072；换不了就不应用，格照旧被 `mods` 清掉；〔由 SPEC-0093 修订：`V` 正好是 `p<k>` 时直接写实参的值，含区间〕）；`may` 时与调用前的值汇合（`JoinValue`）。
应用 `mods` 之后把第 K 个实参所指对象（常数偏移）的那一格写成它。

## 3. 对召回与误报的影响

**召回**：初始化函数写长度、出参写回长度之后的访问按真实值判（原来记账）。可靠：每条路径要么写同一个确切值、要么没碰；"没碰"时取旧值与新值的并。

## 4. 实现位置

`core/Interp.cpp`（`FieldSlots`、`RecordReturn`、记录那一遍的隐式返回、`Effects`、`FieldWrites`、调用点）、`core/Summary.{h,cpp}`（`Eff.may`）、`summary.schema.json`。

## 5. 测试

* lit `field-effect.test`：`Cfg_Init` → `field p0->ulLen := 64`，`Top_TP` → POSSIBLE；`Get_Len` 出错路径没写 → `field p0->_0 := 32, may`，`Len_TP`（`n = 0` 初值）→ 末端 32 → POSSIBLE；
  写 `p1`（形参）的 → 调用方代成实参；对照：一条路径写 32、一条写 8 → 不记；写了不确切的值 → 不记。
* 单测 `Interp.FieldEffect`；`Summary` 读写往返带 `may`。
* 原来按"被调者改过字段 → 记账"写的四处对照按新结果更新（`mod-ref.test`、`field-current.test`、`callee-writes.test` 变成 DEFINITE 100，
  `scalar-pointee.test` 的 `Set(&n)` 变成 POSSIBLE 20）——同一位置、判得更准；`out-new-field.test` 的摘要多一条 `ulLen` 的写效应。
* `run.py csa` 与基线相同；`replay.py`、`engine_check` 不回退。

## 6. 调试

`Dump=summary`（`effs`、`fields`）、`Dump=state`（调用后的格）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| D-24 | `field`（`target: "p0->ulUsed"`、`value`）/ `out_int`（只列了名字） | `field` 的推导与调用点按 §2，加 `may`；`*pK` 的出参写回用伪字段 `pK->_0`（不另设 `out_int`） |

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 只在某个返回值下成立的写效应（`ret == OK ⇒ pst->ulLen := …`，LV5-02，2027H1）；指针字段（`out_new` 已做新对象，SPEC-0061）。
