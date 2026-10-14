// Checkers.td 注册条目模板（LLVM 14）。
// 路径：BXPlugin/xir/include/clang/StaticAnalyzer/Checkers/Checkers.td
//
// 复制后替换（两个占位，注意 Checker 后缀的有无）：
//   <CheckerName>Checker        -> def 名（与 C++ 类名一致，带 Checker 后缀），如 ParamMemoryAccessChecker
//   <CheckerNameWithoutChecker> -> Checker<"..."> 里的运行短名（不带 Checker 后缀），即 -checker= 用名，如 ParamMemoryAccess
//
// 规则：
//   - def 名 = C++ 类名（带 Checker 后缀）；register/shouldRegister 函数名也跟 def 名（带后缀）。
//   - Checker<"..."> 字符串是运行短名（不带 Checker 后缀），-checker= = 包名 + 该短名。
//     例：def ParamMemoryAccessChecker : Checker<"ParamMemoryAccess">  ->  -checker=rb.enhance.core.ParamMemoryAccess
//   - 所有描述文本（HelpText 等）必须英文。
//   - 新增条目追加到对应 package 块的最后。

// ── AST 规则 / 单函数规则 / 单函数路径敏感规则 -> rb.enhance.core ──
let ParentPackage = RosaRBEnhanceCore in {

def <CheckerName>Checker : Checker<"<CheckerNameWithoutChecker>">,
    HelpText<"Detect ...">,
    Documentation<NotDocumented>;

} // end "rb.enhance.core"


// ── 跨函数、单文件 路径敏感规则 -> rb.enhance.ipa（按需启用，运行时开 IPA）──
//
// let ParentPackage = RosaRBEnhanceIpa in {
//
// def <CheckerName>Checker : Checker<"<CheckerNameWithoutChecker>">,
//     HelpText<"Detect ...">,
//     Documentation<NotDocumented>;
//
// } // end "rb.enhance.ipa"
//
// 跨文件 / 跨 TU / 全工程规则：先与用户确认 package，不要默认放置。


// ── 带 option 的写法（可选）──
//
// def <CheckerName>Checker : Checker<"<CheckerNameWithoutChecker>">,
//     HelpText<"Detect ...">,
//     CheckerOptions<[
//       CmdLineOption<Boolean, "SomeOption", "What it does", "false", InAlpha>
//     ]>,
//     Documentation<NotDocumented>;
