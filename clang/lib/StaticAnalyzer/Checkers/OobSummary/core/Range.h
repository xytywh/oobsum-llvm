//===-- Range.h - Numeric ranges, guards and entailment ---------*- C++ -*-===//
//
// 取值范围与蕴含（SPEC-0011；06- A-50…A-52、A-62、D-22、F-12）：调用点判定要回答"这个线性式最大 / 最小能取多少、界来自哪里"
// 以及"守卫是否蕴含 d ≤ 0"。与 replay.py 的 Env（_type / add_guard / bound / range_of）、entails_le0 逐项同语义。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_RANGE_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_RANGE_H

#include "OobSummary/core/LinExpr.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace oobsum {

// D-22 的来源标签。枚举顺序就是合并时的优先级：多个界合在一起时取最靠前的（type 最"差"，同 replay.py）
// Model：库函数的大小实参给的上界（访问条件 upto，SPEC-0034）
enum class Tag : uint8_t { None, Type, Widen, Data, Model, Check, Guard, Mask, Loop, Branch, Ret, Const, Exact };

llvm::StringRef TagName(Tag tag);  // None → ""
llvm::Optional<Tag> ParseTag(llvm::StringRef name);
// F-12：代码给出的界（可判 L2）
bool IsCodeTag(Tag tag);
// 两个标签合在一起取哪个
Tag WorseTag(Tag a, Tag b);

// 一个界：有没有、值、来源
struct Limit {
    bool known = false;
    int64_t value = 0;
    Tag tag = Tag::None;

    static Limit Of(int64_t value, Tag tag)
    {
        Limit l;
        l.known = true;
        l.value = value;
        l.tag = tag;
        return l;
    }
};

// 摘要里的类型名 → 取值范围（同 replay.py 的 TYPE_MAX / TYPE_MIN）；指针与不认识的类型返回 false
bool TypeRangeOf(llvm::StringRef typeName, int64_t &lo, int64_t &hi);

class SymEnv {
public:
    // 类型范围，标签 type
    void SetTypeRange(llvm::StringRef name, int64_t lo, int64_t hi);
    // L ≤ 0 形式的守卫；只记单符号的（同 replay.py 的 add_guard）
    void AddGuard(const LinExpr &le0, Tag tag);
    Limit Lower(llvm::StringRef name) const;
    Limit Upper(llvm::StringRef name) const;

    Limit Bound(const Atom &atom, bool upper) const;
    // 线性式的上 / 下界；有一项没有界或 int64 溢出 → 没有界
    Limit RangeOf(const LinExpr &l, bool upper) const;

private:
    std::map<std::string, Limit> lo;
    std::map<std::string, Limit> hi;
};

// A-50：守卫（各为 L ≤ 0）与环境是否蕴含 d ≤ 0（同 replay.py 的 entails_le0；不做两两组合）
bool EntailsLe0(const LinExpr &d, const std::vector<LinExpr> &guards, const SymEnv &env);

}  // namespace oobsum

#endif
