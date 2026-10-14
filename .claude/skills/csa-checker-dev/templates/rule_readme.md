# <CheckerName>

> package / 运行名：`rb.enhance.core.<CheckerNameWithoutChecker>`（跨函数规则为 `rb.enhance.ipa.<...>`）

## 1. 规则目的

说明该 checker 检测什么缺陷模式，以及为什么需要检测。

## 2. 风险说明

对应的安全 / 可靠性 / 可维护性 / 编码规范风险。

## 3. 检测策略

- [ ] AST-only checker
- [ ] 路径敏感 CSA checker
- [ ] LibTooling 预扫描 + CSA（附录场景）
- [ ] 混合

关键 source 与 sink：……
路径/展开假设（path-sensitive 适用）：loop 展开 3；IPA 模式（core=none / ipa=开启）。

## 4. 支持场景

1. ……

## 5. 检测边界与取舍（TP/TN/FP/FN）

| 象限 | 场景 | 处理 | 理由 |
|---|---|---|---|
| TP | … | 报 | … |
| TN | … | 不报 | … |
| FP | … | 误报 | 接受 / 缓解：… |
| FN | … | 漏报 | 工业取舍：为压低误报而裁掉，因为 … |

不支持场景（示例措辞）：

> 该 checker 当前不做完整的用户自定义 sanitizer 函数过程间识别。这是有意取舍，因为在没有函数契约的情况下识别任意校验函数，容易引入大量误报或漏报。

工业偏好：优先低漏报，但不以不可接受的误报为代价；每处边界裁剪给 justification。

## 6. 示例

### 6.1 违规示例

```c
void Bad(void)
{
    // ...
}
```

预期告警（English）：

```text
<English warning message>
```

### 6.2 合规示例

```c
void Good(void)
{
    // ...
}
```

### 6.3 边界示例

```c
void Boundary(void)
{
    // ...
}
```

## 7. 测试覆盖

| 场景 | 预期 |
|---|---|
| 直接违规 (TP) | 告警 |
| 安全反例 (TN) | 不告警 |
| 已知误报 (FP) | 告警（已记录） |
| 已知漏报 (FN) | 不告警（已记录） |
| 边界 / 类型转换 | 按规则 |
| 嵌套成员路径（若涉及） | 告警含稳定路径 |
| 回归 | 按修复预期 |

测试文件：`BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/TC_<CheckerName>.c`
预期告警数量与关键子串：……

## 8. 注册与构建

- 源码：`BXPlugin/xir/llvm_ast/StaticAnalyzer/Checkers/<CheckerName>.cpp`
- 源码 CMakeLists：`.../Checkers/CMakeLists.txt` 的 `add_library()` 追加到最后，保持对齐
- Checkers.td：`BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td`，package（core / ipa / 已确认），追加到对应包块最后
- 测试目录：`BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/` 内放 `CMakeLists.txt`（单/多文件见 registration-build-run.md）；并在 `TestCase/CMakeLists.txt` 最后追加 `add_subdirectory(<CheckerName>)`
- 编译：

```bash
cd /usr1/BXPlugin/cmake-build-release-release
cmake -G "Unix Makefiles" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_VERBOSE_MAKEFILE=ON \
  -Darch=64 -Dos=linux -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_ROOT=/usr1/llvm-project-14/cmake-build-release/ \
  -Dprovider=llvm_ast -Dptype=lib \
  -DCLANG_TABLEGEN_EXE=/usr1/llvm-project-14/cmake-build-release/bin/clang-tblgen ../
make -j16
```

## 9. 运行方式

```bash
/usr1/BXPlugin/cmake-build-release-release/bxen \
  -exec_mode=onlySa \
  -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
  -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/TC_<CheckerName>.c \
  -checker=rb.enhance.core.<CheckerNameWithoutChecker> \
  "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
```

多文件分散用 `-analysis_folder=<目录>/`；`rb.enhance.ipa` 跨函数规则去掉 `ipa=none` 并开启 IPA。

## 10. 备注

实现说明、CSA 已知限制、后续优化方向。
