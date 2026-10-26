# 给 Claude Code 的提示词

在 WSL 里 `cd` 到仓库根（有 `CLAUDE.md` 的那一层），运行 `claude`，把对应的一段整段粘进去。
`CLAUDE.md` 每次会自动加载，所以提示词只说"这次要做什么"；规则以 `CLAUDE.md` 为准。

---

## A. 首次启动（只用一次）

```text
你接手 OobSummary（rb.enhance.ipa.OobSummary，CSA Summary 2.0）的开发：用函数摘要检测跨多层调用的内存越界。
之前的设计与讨论都在别处进行，现在全部在这个仓库里（llvm-project 14.0.0 + 分支 oobsum/dev），提交记录就是历史。

第一步：建立上下文，先不要写代码。
1. 读 CLAUDE.md（已加载）、oobsum/HANDOFF.md、oobsum/STATUS.md、oobsum/specs/README.md、oobsum/specs/SPEC-0001-labels-recall-debug.md。
2. 看 oobsum/06-spec.md 的目录与 §5（摘要格式）、§6（算法），oobsum/07-dev-plan.md 的 §4（工作包）；具体条目用到时再细读。
3. 看 oobsum/cases/INDEX.md（67 个用例的索引）、oobsum/cases/summaries/README.md，跑一次 python3 oobsum/cases/summaries/replay.py。
4. git log --oneline -25、git status、git remote -v。
5. 用 10 行以内告诉我：目标、现状、你打算先做的三个小步（对应 STATUS.md 的 S1、S2、S3），以及你看到的风险或疑问。

第二步：确认环境。
- cmake-build-release/bin/clang 存在吗？存在就跑 oobsum/devenv/40-smoke.sh，期望 7 项 PASS（第 7 项是 OobSummary 的 6 个 lit 用例）。
- 不存在：告诉我先跑 oobsum/devenv/30-build-llvm.sh（全量构建很久，由我来跑），不要自己在前台跑全量构建。
- 确认 origin 指向我的私有仓库、工作区干净。

第三步：按 STATUS.md 的"下一步"小步开发。每一步严格按这个循环：
  a) 在 oobsum/specs/ 新增或更新 SPEC（模板见 specs/README.md）：来源、设计、对召回的影响、实现位置、测试、调试手段、对主规格的修订、公司侧验证。
  b) 写代码（CLAUDE.md §6：LLVM 14、C++14、core/ 不依赖 Clang、英文告警、中文注释、OobSummary/.clang-format）。
  c) 写测试：lit（clang/test/Analysis/oobsum/），core/ 的纯逻辑配单元测试；新的引擎环节配 Dump 种类，并有一个用到它的 lit 用例。
  d) 构建（ninja -C cmake-build-release clang；改了 Checkers.td 会重编全部 checker，放后台跑再轮询）→ lit 全过 →
     replay.py 不回退 → python3 oobsum/devenv/land.py --drop oobsum/landing/OobSummaryChecker.manifest --check-tree 一致。
  e) 更新 oobsum/STATUS.md；git commit（中文，csa-checker-dev 格式，写 SPEC 号与验证命令）；git push origin oobsum/dev。
  f) 两三行告诉我这一步做了什么、怎么验证的，然后接着做下一步。
  粒度：一个能独立验证的小能力（例如"函数键 + 3 个 lit 用例"），通常几十到几百行；不要一次做完一个工作包。

始终遵守的四条（我的硬性要求）：
1. 小步快跑：每个小特性、小修改就提交并 push；不攒改动，不提交编不过或测试不过的代码。
2. 告警打标签：每条判定都是 DEFINITE / POSSIBLE / SUSPECT / SAFE 之一并带原因；明确越界和明确安全都要能识别（SPEC-0001）。
3. 宁可多报，不要漏报：拿不准就报 SUSPECT（写清原因），只有证明了安全才不报；所有判定（包括不报的）都写进判定日志，
   后面交给大模型复核过滤。任何提高精度的改动，都不能让原来报的变成不报，除非证明了安全——用回归用例守住。
4. 先 spec 后代码，并且有可落盘的调试输出：每次设计 / 修改写 oobsum/specs/SPEC-NNNN；每个引擎环节（FIR、抽象状态、摘要、
   调用点代换、判定）都能用 Dump 打到 stderr 和 WorkDir/dump/，不影响结果。结果不符合预期时先看 dump 定位，再改代码。

需要我决定的事、只有公司环境才能验证的事：停下来列给我，并写进 STATUS.md 的"等用户提供"，不要猜。
先完成第一步并汇报。
```

---

## B. 之后每次开新窗口

```text
继续 OobSummary 的开发。按 CLAUDE.md §2 建立上下文：读 oobsum/STATUS.md、git log --oneline -20、git status、与下一步相关的 SPEC。
用 3–5 行说明现状和接下来的小步，然后按 CLAUDE.md §5 的循环继续：SPEC → 代码 → 测试与 Dump → 构建与全部测试 → 更新 STATUS → 中文提交 → push。
四条硬性要求不变：小步提交推送；判定带标签；宁多勿漏、所有判定写日志；先 spec、有可落盘的调试输出。
遇到需要我决定或要去公司验证的，停下来列给我。
```

---

## C. 带回公司的验证结果

```text
这是公司环境（BXPlugin + bxen）的验证结果，按 oobsum/COMPANY.md §0（清单版本 <版本号>）的带回模板填的：
<粘贴：带回模板 / 编译报错原文 / stats --counts-only 的输出 / fork-facts.txt>

请：1) 把结论记进 oobsum/STATUS.md 的"公司验证结果"一节（只记不敏感的结论）；2) 判断哪些与本地不一致、原因是什么；
3) 需要改的按小步流程修（SPEC → 代码 → 测试 → 提交推送）；4) 按 CLAUDE.md §5 第 8 条重写 oobsum/COMPANY.md §0（下次在公司按顺序做的清单、
预期、带回模板），对话里只告诉我清单版本号和改了什么。
```

---

## D. 版本节点（一组功能做完、准备去公司验证时）

```text
准备一个版本节点：更新 oobsum/CHANGELOG.md（版本号沿用 v4.x），打 tag oobsum-v4.x 并 push tag；
在 oobsum/testcase/OobSummaryChecker/ 里更新公司 TestCase 与文件头里的预期；在对应 SPEC 的"公司侧验证"写清 bxen 命令与预期结果；
最后给我一段可以直接照做的公司侧步骤（git pull → land.py → make -j16 → bxen → decisions.py）。
```
