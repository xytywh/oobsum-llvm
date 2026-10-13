// 追加到 BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td 的
// let ParentPackage = RosaRBEnhanceCore in { ... } 块的最后（探针是单 TU 的 AST 级只读规则，按包路由归 core）。
// 用完即可删除（连同源码、CMakeLists 行与 TestCase 目录）。

def OobSummaryProbeChecker : Checker<"OobSummaryProbe">,
    HelpText<"Integration probe for CSA Summary 2.0 (read-only; prints [OSP] lines, writes per-TU JSON)">,
    CheckerOptions<[
      CmdLineOption<String, "WorkDir",
                    "Directory for per-TU probe JSON (empty: do not write)",
                    "", Released>,
      CmdLineOption<Boolean, "EmitProbeReport",
                    "Emit one probe report per translation unit (test cases only)",
                    "false", Released>
    ]>,
    Documentation<NotDocumented>;
