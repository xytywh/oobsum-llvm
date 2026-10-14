<type>(rb.enhance.core.<CheckName>): <中文概括>

修改背景：
- <为什么需要这次修改，例如补齐某类漏报、降低某类误报、完善规则边界等。>

核心改动：
- <改动点 1>
- <改动点 2>

规则边界：
- 支持：<新增或确认支持的场景>
- 不支持：<仍不支持的场景及原因>
- 取舍：<误报 / 漏报取舍（工业偏好：优先低漏报，不以不可接受的误报为代价）>

测试说明：
- 新增 / 更新 <测试文件或测试函数>，覆盖 TP / TN / FP / FN + 边界 / 回归
- 验证命令：
  /usr1/BXPlugin/cmake-build-release-release/bxen -exec_mode=onlySa \
    -cc_json=/usr1/BXPlugin/cmake-build-release-release/ \
    -analysis_file=/usr1/BXPlugin/xir/llvm_ast/TestCase/<CheckerName>/TC_<CheckerName>.c \
    -checker=rb.enhance.core.<CheckName> \
    "-extra_args=-analyzer-max-loop 3 -analyzer-config ipa=none -rbchecker-config=/usr1/clang_check/src/cfg/ini/config.ini -funcptr-config=/usr1/clang_check/src/cfg/ini/config_funcptr.ini"
- 实际告警数 / 预期告警数：<...>

影响范围：
- <影响的 checker、README、测试用例、CMakeLists / Checkers.td 注册项等>

Issue: DTSXXXX

# 说明：
# - type 取 feat / fix / refactor / test / docs / build。
# - scope 用规则全名：rb.enhance.core.<CheckName> 或 rb.enhance.ipa.<CheckName>。
# - 正文默认中文（用户明确要求英文才用英文）。
