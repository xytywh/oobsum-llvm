# SPEC-0062 调用点实参是不跟踪的指针：记 `untracked_pointer`，告警不再露出内部占位符

状态：已实现
来源：查 `run.py csa` 里 TN 上的 SUSPECT 时发现，LV4-07（外部分配器 `Ext_PoolAlloc` 只有声明）的告警文本是

```
Cannot prove the write through a 3-level call chain stays within '_unknown0' (size _unknown0): up to byte 64 may be accessed  [entry_unproven]
```

调用点记录（D-25）里不跟踪的指针实参写成占位符 `{obj: "local:_unknownK", have: "_unknownK"}`（SPEC-0015），代换后它被当成"调用方局部、没有界"，
落到 `entry_unproven`；占位符原样进了告警。

## 2. 设计

* 判定（C++ `apply::Classify` 与 `replay.py` 的 `classify` 同步）：守卫为假的检查（①）之后，代换出的对象是占位符（`obj` 是 `local:_unknownK` 且 `have` 就是 `_unknownK`）
  → `open`、原因 `untracked_pointer`。原来这里只可能落到 `entry_unproven`（`have` 不是接口符号，既判不了也传不上去），所以**只换原因**，记不记、报不报不变。
* 文本：对象写成 "the object passed as argument K+1"，大小写成 "size unknown"（`Alert.cpp` 的 `ObjName` 与 `replay.py` 的 `obj_name` 同步；`Engine.cpp` 的 `SizeText`）。

## 3. 对召回与误报的影响

无（只换 SUSPECT 的原因与文本）。

## 4. 实现位置

`core/Apply.cpp`（`Classify`）、`core/Alert.cpp`（`ObjName`）、`core/Engine.cpp`（`SizeText`）、`replay.py`（`classify`、`obj_name`）。

## 5. 测试

* lit `unknown-arg.test`：外部函数返回的指针经两层交给写函数 → `SUSPECT:untracked_pointer`，文本 "stays within the object passed as argument 1 (size unknown)"。
* `run.py csa`：LV4-07 的原因 `entry_unproven` → `untracked_pointer`（同一处仍是 SUSPECT，不算回退）；`replay.py`、`engine_check` 不回退（金标准里没有占位符）。
* 原来检查旧文本的三处对照（`out-new.test`、`ret-new-offset.test`、`model-file.test`）按新原因 / 文本更新。

## 6. 调试

`Dump=apply`（"→ open (untracked_pointer)"）、`Dump=summary`（调用点记录的 `local:_unknownK`）。

## 7. 对主规格的修订

无。

## 8. 公司侧验证

不需要。

## 9. 待定 / 以后

* 占位符带上指针为什么不跟踪（外部函数返回、整数转换…），原因可以更细。
