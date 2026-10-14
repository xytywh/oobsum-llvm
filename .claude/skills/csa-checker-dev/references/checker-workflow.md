# Checker 开发工作流

## 1. 需求归一化（编码前必做）

任何非平凡 checker，编码前先把需求整理成下面的结构，缺项要么推断要么与用户确认：

```text
规则名：
checker 包：           # rb.enhance.core / rb.enhance.ipa / 待确认
问题类别：
分析目标：             # 变量 / 字段 / 全局变量 / 指针目标 / 动态分配结果 / 函数参数 / 返回值 / AST 结构
触发点 / sink：        # 数组下标 / 指针算术 / memcpy·memset·安全 C API / 成员访问 / 函数调用 / 赋值 / 返回语句
需证明的条件：         # definite bug / possible bug / 风格或策略违规
支持场景：
不支持场景：
误报取舍：             # 工业偏好：优先降低漏报，但不以不可接受的误报为代价
漏报取舍：
```

示例（真实规则 `StructMemberPtrArrayAccessChecker`）：

```text
规则名：StructMemberPtrArrayAccessChecker
checker 包：rb.enhance.core
问题类别：结构体成员指针 / 数组不安全访问
分析目标：结构体成员指针 / 数组
触发点 / sink：直接成员访问、数组下标、指针算术
需证明的条件：策略违规，本地模式明确
支持场景：直接访问、类型转换、嵌套成员
不支持场景：复杂过程间校验
误报取舍：普通 API 的纯实参传递不告警
漏报取舍：memset_s、strncpy_s 等敏感函数即使作为实参传递也继续告警
```

## 2. 选范式（详见 §决策与两份范式 reference）

- 语法/类型层可判 → AST-visitor（`Checker<check::ASTCodeBody>` + `RecursiveASTVisitor`）。
- 依赖分支可行性/符号值/约束/别名 → path-sensitive（`checkPreStmt`/`checkPreCall`/`checkBind`/`checkDeadSymbols` 等）。
- 需全工程信息/跨 TU 唯一性/全局扫描且 CSA 不划算 → LibTooling 预扫描备选（附录，需求方未明确则不用）。

## 3. 实现检查清单

- [ ] 加必要 CSA 头文件，含 `BuiltinCheckerRegistration.h`，include 不排序。
- [ ] 用 LLVM/Clang 14 兼容 API。
- [ ] 报告默认 `generateNonFatalErrorNode()`；BugType category 非空英文、按性质、不用于过滤。
- [ ] 判类型用 `IgnoreParenCasts`，取 SVal/MemRegion 用原始 Expr。
- [ ] 结构体名走 typedef 反查（clean→tag→匿名）。
- [ ] 告警文案与 `Checkers.td` 英文；保留用户注释；最小化修改。
- [ ] 源码落 `Checkers/`、用例落 `TestCase/<CheckerName>/`（大驼峰目录）。
- [ ] 测试目录根放 `CMakeLists.txt`（单文件用 `add_library(<target> TC_xxx.cpp)`；多文件用 `file(GLOB ...)`+`add_library`）。
- [ ] 在 `TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(<CheckerName>)`。
- [ ] 源码 `CMakeLists.txt` 的 `add_library()` 追加新源文件到最后，保持对齐，不重排。
- [ ] `Checkers.td` 按包路由注册，追加到对应包块最后。
- [ ] 测试覆盖 TP/TN/FP/FN + 边界/回归。
- [ ] 行为变化时更新 README（含边界/取舍）。
- [ ] 重新 CMake 配置 + `make -j16`，再用 `bxen` 跑实际用例。
- [ ] 给中文 git commit message（scope=规则全名）。

## 4. 10 步调试流程

checker 行为不符预期时按序排查：

1. 目标 AST 节点是否会触发当前 callback。
2. 必要时 dump 相关 AST 节点确认形态。
3. checker 是否真的进入了对应 callback。
4. cast stripping 是否改变了 AST 形态（`IgnoreParenCasts` 用对了吗）。
5. `getSVal(originalExpr)` 是否返回预期值/region（是否误用了 stripped expr 取值）。
6. `MemRegion` 链能否回溯到预期 origin。
7. `ProgramState` 写入/读取用的 key（trait）是否一致。
8. report node 是否为 null（`generateNonFatalErrorNode` 返回 null 时已正确 return）。
9. 是否因使用 sink/fatal node 导致后续路径被中止、吞掉了后续报告。
10. 复现后补一条回归测试，锁定该问题。

## 5. 新增 checker 的完整闭环

```text
源码实现
  -> 源码 CMakeLists.txt 的 add_library() 追加（到最后，保持对齐）
  -> Checkers.td 按包路由注册（追加到对应包块最后）
  -> TestCase/<CheckerName>/ 新增测试（TP/TN/FP/FN + 边界/回归）
       + 该目录放自身 CMakeLists.txt（单/多文件写法见 test-and-readme.md）
       + TestCase/CMakeLists.txt 最后追加 add_subdirectory(<CheckerName>)
  -> 重新 CMake 配置 + make -j16
  -> bxen 实际运行测试用例，核对实际告警数 vs 预期
  -> README（含边界/取舍） + 中文 commit message
```

任一环缺失都视为未完成。编译或运行失败的排查与输出格式见 `references/registration-build-run.md`。
