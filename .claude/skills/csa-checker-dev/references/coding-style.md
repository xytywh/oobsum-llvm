# 代码风格

## 1. `.clang-format`（等价配置）

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
PointerAlignment: Right          # int *p
SortIncludes: false              # 手工分组，不排序
UseTab: Never
BreakBeforeBraces: Custom        # 仅 AfterFunction: true，其余 false
AllowShortFunctionsOnASingleLine: None
AllowShortBlocksOnASingleLine: false
AccessModifierOffset: -4         # public:/private: 与 class 齐平
NamespaceIndentation: None
Standard: Cpp11
ContinuationIndentWidth: 4
MaxEmptyLinesToKeep: 1
```

`BreakBeforeBraces` 效果：**函数/方法定义**的左大括号换行；控制流（`if`/`for`/`while`/`do`）、`class`、`namespace` 的左大括号不换行。

```cpp
void DoSomething()        // 函数定义：{ 换行
{
    if (cond) {           // 控制流：{ 不换行；单语句也要 {}
        Handle();
    }
}

class MyChecker : public Checker<check::ASTCodeBody>   // class：{ 不换行
{
public:                   // 与 class 齐平
    void checkASTCodeBody(const Decl *decl, AnalysisManager &mgr, BugReporter &br) const;
};
```

## 2. 大括号

所有控制语句必须带大括号，单语句也不例外：

```cpp
if (node == nullptr) {
    return;
}
```

禁止 `if (node == nullptr) return;`。

## 3. 命名

**默认规则**：

- **变量** —— 局部变量、函数参数、类成员变量 —— 一律 **lowerCamelCase**。
- **函数** —— 普通函数、类成员函数 —— 默认 **UpperCamelCase**。
- **唯一例外（同类一致性）**：当一个类继承/重写的回调本身就是 `checkXXX` 小驼峰（路径敏感的 `checkPreCall`/`checkBind`/`checkPreStmt` 等，**以及 AST checker 外层类的 `checkASTCodeBody`**），则该类内的**所有其它函数与私有 helper 也统一使用 lowerCamelCase**，与回调保持一致，**不**按默认改用大驼峰。RAV 嵌套 `Visitor`（回调 `VisitXX` 本就是大驼峰）则照默认走大驼峰。

| 场景 | 风格 | 例 |
|---|---|---|
| 局部变量 / 函数参数 / 类成员变量 | lowerCamelCase | `const Expr *baseExpr; const Expr *argExpr; std::string fieldName;` |
| 普通函数 / 类成员函数（默认） | UpperCamelCase | `static std::string ResolveRecordName(const RecordDecl *recordDecl);` |
| `RecursiveASTVisitor` 的 `VisitXX` 及同级方法 | UpperCamelCase（与默认一致） | `bool VisitMemberExpr(MemberExpr *memberExpr);` |
| path-sensitive `checkXX` 回调 | lowerCamelCase（贴合继承的 CSA API） | `void checkPreStmt(const ArraySubscriptExpr *expr, CheckerContext &C) const;` |
| **回调为 `checkXXX` 的类内其它函数/helper**（路径敏感类，或外层 `checkASTCodeBody` 类） | **lowerCamelCase（全类统一，与回调一致）** | `void reportBug(...) const; bool isTaintedArg(...) const;` |

> 判定口径：看这个**类**继承/重写的回调风格——`checkXXX`（小驼峰，含路径敏感回调及外层 `checkASTCodeBody`）⇒ 全类小驼峰；RAV 嵌套 `Visitor` 的 `VisitXX`（大驼峰）或无回调的独立 helper ⇒ 默认大驼峰。一个类内不要大小驼峰混用。

## 4. 注释

- 默认保留用户已有注释；仅在注释明显错误/过时/与新逻辑冲突时才纠正或删除。
- 只在解释非显然的 analyzer 行为、规则取舍、边界时新增注释，不加复述代码的噪音注释。
- C/C++ checker 代码新增注释可中文或英文，但**告警文案必须英文**。
- **Python 注释和 docstring 默认中文。**

## 5. 告警语言

checker 诊断信息与 `Checkers.td` 内容**必须英文**。

## 6. 必需 include

```cpp
#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"
```

include 按逻辑分组手工维护（`SortIncludes: false`），不要自动字母序排序。
