# SPEC-0078 告警 note 按 F-33：总链 note、每一跳说清做了什么、分配点

状态：已实现
来源：STATUS S8c-2 "告警 note 按 F-33 的三种写法"。现在每一跳各有一条 `call chain: <函数名>` 的 note（只做了 ②，而且没说这一跳做了什么），
F-33 ① 的总链 note、② 里的访问点与大小来源（分配点）都没有。

## 2. 设计（`report/Reporter.cpp`，只改 note，告警正文不变）

调用链有两跳及以上时：

1. **总链 note**（F-33 ①），锚在告警位置：`Call chain: Top_TP (engine.c:34) -> Up (engine.c:28) -> Mid (engine.c:23) -> Leaf (engine.c:18)`（文件写文件名）。
   不在本 TU 的跳也写在这里（F-33 ③）。
2. **每一跳**（F-33 ②，只给本 TU 可见文件里的）：中间的写 `<函数> calls <下一个函数> here`，最后一跳（访问点）写 `<函数> accesses the object here`。
3. **分配点**（F-33 ② 的大小来源）：对象是 `heap@<文件名>:<行>` 时，在本 TU 已打开的文件里按文件名找到它，写 `the object is allocated here`；找不到就不写。

调用链只有一跳（入口账本、本函数账本）时不加 note（与告警位置重复）。

## 3. 对召回与误报的影响

无（只改 note）。

## 4. 实现位置

`report/Reporter.cpp`（`Flush`、`LocateBase`）。

## 5. 测试

* lit `engine.c`：note 改成新写法（总链 note、`calls … here`、`accesses the object here`）。
* lit `f33-notes.test`：三层链 + 堆对象 → 总链 note、每跳 note、分配点 note；一跳的账本没有 note。
* `run.py csa` 与基线相同（只看判定日志）。

## 6. 调试

无（看告警输出）。

## 7. 对主规格的修订

| 条目 | 原写法 | 新写法 |
|---|---|---|
| F-33 | ①②③ | 照做；② 的文本定为 `<函数> calls <下一个> here` / `<函数> accesses the object here` / `the object is allocated here`；一跳的不加 |

## 8. 公司侧验证

可选：在 bxen 的 HTML / plist 输出里看 note 的显示效果。

## 9. 待定 / 以后

无。
