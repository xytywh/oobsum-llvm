# SPEC-0018 模型表第一步：访问类函数（securec / libc）、纯函数、分配器的直接调用

状态：已实现（S7d-1）
来源：`STATUS.md` S7d；06- §3.1 F-03（`memcpy_s` / `memmove_s`）、F-04（`memset_s`）、F-05（libc `memcpy` / `memmove` / `memset`）、§4.4 I-40（模型表）、I-44（同名冲突：模型表优先）、
D-33（分配器的返回值）、D-25（调用点的 `model`）；金标准 `LV2-03/Tlv_WriteVal*`、`LV5-01/Enc_PutBytes`、`LV10-04/Cfg_Save`。

## 1. 要解决什么

`memcpy_s(dest, destMax, src, count)` 是业务代码里最主要的内存操作。S7a–S7c 把它当成普通的未知调用：既不检查目的、也不检查源。
16 个用例里 5 个（LV2-03、LV3-07、LV5-01、LV8-01、LV10-04）的访问点就是它。

## 2. 设计

### 2.1 `core/Models`：模型表（本步是内置的默认表，I-40 的 YAML 文件在 S7d-2）

| 种类 | 内容 | 默认 |
|---|---|---|
| `access` | 名字、若干条件 `{base: 第几个实参, bytes: 实参的线性式, kind: R/W, when: 实参的条件}`、`destMax`（securec 规则用的实参下标，没有为 −1） | `memcpy_s` / `memmove_s`：`{p0, "p1", W}`、`{p2, "p3", R, when "p3 <= p1"}`，destMax = 1；`memset_s`：`{p0, "p1", W}`，destMax = 1；`memcpy` / `memmove`：`{p0, "p2", W}`、`{p1, "p2", R}`；`memset`：`{p0, "p2", W}` |
| `alloc` | 名字、大小（实参的线性式或乘积） | `VOS_MemAlloc`：`p2`（I5 待确认）；`malloc`：`p0`；`calloc`：`p0*p1` |
| `pure` | 名字、正则 | `LOG_Print`、`VOS_Printf`；`^LOG_`、`^DIAG_`、`_Trace$`（I9 待补） |
| `free` | 名字 | `VOS_MemFree`、`free`（不改被跟踪的大小；I-42 的 `MEM_FREE` 读入但 P0 不用） |

查找按 I-44：**模型表优先**，函数体可见也用模型。

### 2.2 访问类函数在调用点（`core/Interp`）

1. 每个实参求值。securec 规则（F-03，`destMax` 下标 ≥ 0 时）：
   * `destMax` 是常数 0 或 > `0x7fffffff`（`SECUREC_MEM_MAX_LEN`）→ 不写、不读，这次调用不产生访问。
   * `destMax` 是**可能回绕的无符号减法** `wrapW(E)`（S7a 的回绕形）→ 回绕后的值大于上限、不写 → 这次调用的访问带守卫 `E >= 0`，`destMax` 取 `E`（LV2-03 TN：`128 − ulOff` → 守卫 `p1 <= 128`、目的末端 `p1 + (128 − p1) = 128`）。
2. 每条条件：基址 = 第 `base` 个实参（指针），字节数 = `bytes` 代入实参的值（确切 / 区间），末端 = 偏移 + 字节数，照常做访问检查（当场证明安全、Req、账本，SPEC-0016 / 0017）。
   `when`（`count <= destMax`）代入实参：确切值且两边忠实 → 原子；常数为假 → 这条不访问；常数为真 → 无条件；其余 → 不加守卫（照常访问，多报不少报）。
3. 守卫按 SPEC-0016 挑：与 Req 的 `have` / `end` / **`start`** 有公共符号的才带上（本步把 `start` 也算进去：LV2-03 TN 的目的一侧末端是常数 128，守卫 `p1 <= 128` 靠 `start = p1` 连上）。
4. 调用点记录照常写（`model: "access"`），求解时被调者不在摘要里、跳过。

### 2.3 分配器的直接调用

`p = VOS_MemAlloc(pid, 0, 64)`：返回值是新的堆对象，`obj = heap@<调用点>`，大小 = `size` 代入实参（区间取下界——对象越小越容易越界，多报不少报；没有下界 → 大小未知，访问时记账 `alloc_size_unknown`）。
`maybe_null` 不用（空指针不在本规则范围）。调用点记录写 `model: "alloc"`。经封装函数传上来的分配（`ret_new` 效果，LV4-01）在 S7d-3。

### 2.4 函数指针实参

写 `null`（金标准 LV10-04 的 `OM_RegMsg`）：函数指针没有"还能访问多少字节"。

## 3. 对召回与误报的影响

* 新增检查：securec / libc 内存函数的目的与源。
* `destMax` 回绕守卫照 F-03 原文实现。**已知的漏洞（待用户决定，见 STATUS）**：`E = 128 − ulOff` 在 `ulOff > 2^31 + 128` 时 `wrap32(E) = E + 2^32 ∈ (128, 2^31)`，
  不大于上限，securec **照样写**，而守卫 `p1 <= 128` 说它不写——这个区间（偏移超过 2 GB）的越界会被漏掉。补救是再加一条 Req（守卫 `E <= −2^31 − 1`、`destMax = E + 2^32`），
  代价是金标准里经过的每一层都多一条上传。本步不做，等用户决定。
* `when` 只在两边忠实时才变成守卫；否则照常访问。
* **与金标准的分歧（待用户决定，见 STATUS）**：F-02 要求经指针读写字段 `p->f` 也生成条件 `off(f) + sizeof(f) <= ext(p)`，引擎照此生成
  （LV10-04 `pstMsg->usMsgLen` → `R ext(p0) end 4`、LV5-01 `pstCtx->pucBuf` / `ulBufLen`）；16 个用例的金标准摘要里一律没写这类 Req。
  在入口函数上它们会变成 `entry_unproven`（SUSPECT）——业务代码里每个读消息字段的入口都会有一条。改金标准、还是对"按声明类型、常数偏移的字段访问"另定规则，由用户定。

## 4. 实现位置

`OobSummary/core/Models.{h,cpp}`（新）；`core/Interp.cpp`：`ApplyAccessModel`、`EvalModelExpr`、`Access` 的字节数改成整数值、分配器的返回值、`RecordCall` 的 `model`；
`IsPureModel` 换成模型表。

## 5. 测试

* 单元测试 `ModelsTest`：默认表的查找（名字、正则、优先级）。
* 单元测试 `InterpTest`：`memcpy_s` 的目的 / 源 / `when` 守卫；`destMax = 128 − off` 的回绕守卫；`destMax` 为 0 或过大不访问；`memset_s`、`memcpy`；`count > destMax` 为常数时不读源；
  `VOS_MemAlloc` 的堆对象；函数指针实参为 null。
* lit `models.c`：`memcpy_s` 的 destMax 回绕守卫、`memset_s` 写过堆对象、`^LOG_` 纯函数、函数指针实参。
* `engine-check.test` 加入 LV2-03：8 个用例一致。LV10-04 的上传条数已对上，入口账本多 2 条来自上面的 F-02 分歧，另缺 `unknown_callee`（S8c）。

## 6. 调试

`Dump=state` 的 `safe` 行与 `Dump=summary` 的 Req 照旧；调用点记录里的 `model` 字段说明用了哪个模型。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| I-40 | `access` 的条件 `{base, bytes, kind}` | 加 `when`（访问发生的条件）与 `destMax`（securec 规则）；加 `free` 一类 |
| A-44 / SPEC-0016 §2.2 | 与 `have` / `end` 有公共符号的守卫 | 与 `have` / `end` / `start` 有公共符号的守卫 |

## 8. 公司侧验证

I5（`VOS_MemAlloc` 的字节数是第几个形参）、I8（内部封装的访问函数清单）、I9（纯函数清单）仍待用户提供；本步用 06- §4.4 的默认值。

## 9. 待定 / 以后

* S7d-2：`ModelFile`（YAML）读取、`strcpy_s` 等其余 securec 函数（I8）。
* S7d-3：`ret_new` 效果（封装分配器）；S7d-4：条件效应（校验函数的返回值，LV4-06）；局部结构体字段带进调用点（LV5-01 的 `fields`）。
* S8c：未知被调者（所有 TU 都没有定义、也不在模型表里）的 `unknown_callee` 账本——单个 TU 里判不了（跨 TU 的被调者在本 TU 里也没有函数体）。
* 偏移与长度的抵消（A-41，LV3-07、LV8-01）。
