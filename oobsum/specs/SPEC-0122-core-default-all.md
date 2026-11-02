# SPEC-0122 默认报告所有层数，迁入 rb.enhance.core

状态：已实现
来源：用户 2026-11-02 公司全量反馈；传统规则浅层也可能漏报，摘要规则应兜底。公司 TestCase 对照证明 ipa=none 不改变结果。

## 1. 要解决什么
默认 InlineDepth=2 抑制了浅层确定 / 可能越界；包名 ipa 让使用者误以为依赖 CSA 的内联。

## 2. 设计
InlineDepth 默认 0，仍允许显式设 2 使用旧策略。运行短名与选项前缀改为 rb.enhance.core.OobSummary，不留双重注册。
只挂 ASTDecl<TranslationUnitDecl>，摘要引擎照常跨函数 / 跨 TU；所有 bxen 命令显式 ipa=none。
land.py 必须把公司已有的 def 从 ipa 块移动到 core 块，重复同步幂等。历史 SPEC 与历史记录保留旧名。

## 3. 对召回与误报的影响
只增加原来被 inline_depth 过滤的告警，不改变标签、推理与全部判定写日志的规则。金标准摘要与 alerts 不改。

## 4. 实现位置
Checkers.td、Policy.h、land.py、清单、运行工具、当前文档、lit 与单测。

## 5. 测试
默认 0 的浅层 DEFINITE / POSSIBLE；显式 2 的兼容；同一用例 ipa=none / dynamic-bifurcate 的判定日志逐字相同；全部 lit 109/109、core 161/161、replay 0 处不一致、csa 回退 0 / TN 零强告警、check-tree 一致。
67 个用例 / 70 份 TU 日志在 IPA 两种配置下逐字相同；金标准 engine_check 维持 14/16（原有关系域局限）。
落地模拟验证迁移与重复同步，规范检查 PASS 31 / WARN 1 / FAIL 0；TestCase 的 FP/FN 边界由 README 解释。

## 6. 调试
Dump=options+stats 与 WorkDir/dump/<tu>/options.txt；判定日志中的 emitted / inline_depth。

## 7. 对主规格的修订
F-20 / I-12 / T-04：默认所有层数报告；§4.1 注册改为 core；跨函数能力不由 CSA IPA 开关控制。

## 8. 公司侧验证
COMPANY.md §0 新清单。旧名的规则配置、clang_check 分类、gbl_config 路径同步改为 core。TestCase 预期 5 条 / decisions=7 emitted=5。

## 9. 待定 / 以后
无。
