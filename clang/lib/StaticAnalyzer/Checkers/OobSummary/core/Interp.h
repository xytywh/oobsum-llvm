//===-- Interp.h - Intraprocedural abstract interpretation ------*- C++ -*-===//
//
// 函数内解释（SPEC-0014；06- §6.2–§6.4 A-10…A-12、A-20…A-29、A-40…A-45）：把 FIR 的值算成接口符号的线性式，
// 在每条 load / store 上做访问检查，产出 depth 0 的 Req（与 cases/summaries 里的局部事实同形）。
// 已有：无环代码（S7a）、条件边与守卫（S7b，SPEC-0016）、循环（S7c，SPEC-0017：工作表不动点、循环头 widening、下降 2 遍）。
// 不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_INTERP_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_INTERP_H

#include "OobSummary/core/Fir.h"
#include "OobSummary/core/LinExpr.h"
#include "OobSummary/core/Models.h"
#include "OobSummary/core/Range.h"
#include "OobSummary/core/Summary.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace oobsum {
namespace interp {

// 整数值（A-10 的子集）：确切值 / 区间（端点为线性式或无界，带来源标签）/ 未知（带原因）
struct IntVal {
    enum class Kind { Exact, Range, Top };
    Kind kind = Kind::Top;
    LinExpr value;  // Exact
    bool hasLo = false;
    bool hasHi = false;
    LinExpr lo;  // Range
    LinExpr hi;
    Tag tag = Tag::None;
    std::string why;  // Top

    static IntVal Exact(const LinExpr &e);
    static IntVal Range(const llvm::Optional<LinExpr> &lo, const llvm::Optional<LinExpr> &hi, Tag tag);
    static IntVal Top(llvm::StringRef why);

    llvm::Optional<LinExpr> Lower() const;
    llvm::Optional<LinExpr> Upper() const;
    bool IsConst() const
    {
        return kind == Kind::Exact && value.IsConst();
    }
    bool operator==(const IntVal &o) const;
    std::string Show() const;
};

// 指针的基址（A-11 的子集）
enum class BaseKind : uint8_t { Unknown, Param, ParamField, ParamPtr, Global, Local, Str, Func, Heap };

struct PtrVal {
    BaseKind kind = BaseKind::Unknown;
    std::string id;        // 基址的身份（内存格的键用）
    std::string obj;       // 对象写法（D-20）：*p0、p0->aucBuf、*p0->pucBuf、g:名、local:名
    bool hasSize = false;  // 基址大小已知（形参为 ext(pi)）
    LinExpr size;
    IntVal off = IntVal::Exact(LinExpr::Const(0));  // 字节偏移
    std::string why;                                // Unknown 的原因

    bool operator==(const PtrVal &o) const;
};

struct Value {
    bool isPtr = false;
    IntVal i = IntVal::Top("uninit");
    PtrVal p;

    bool operator==(const Value &o) const
    {
        return isPtr == o.isPtr && (isPtr ? p == o.p : i == o.i);
    }
};

// 一个函数的局部事实（SPEC-0014 §2.4、SPEC-0015）：depth 0 的 Req、调用点记录、用到的形参字段、账本
struct FunctionFacts {
    std::string status = "analyzed";  // D-28；partial:iter_limit（循环头更新超过 20 次，不产出 Req）
    std::vector<summary::Req> reqs;   // depth 0，已覆盖合并
    std::vector<summary::Req> safe;  // 本函数里就证明了安全的访问（A-43 / A-44，SPEC-0016 §2.2；判定日志记 SAFE）
    std::vector<summary::Call> calls;                  // 调用点记录（D-25；results 留空，由求解填）
    std::map<std::string, summary::FieldInfo> fields;  // 用到的 pi->字段（D-23 的 fields 表）
    std::vector<summary::Open> opens;
};

// stateTrace 非空时，每个块入口的状态各一行（Dump=state）；models：库函数的模型表（SPEC-0018）
FunctionFacts AnalyzeFunction(const fir::Module &m, const fir::Function &f,
                              std::vector<std::string> *stateTrace = nullptr,
                              const models::ModelTable &models = models::ModelTable::Default());
// 局部事实 → 摘要（只有局部事实；上传的 Req、判定、账本由求解补上）
summary::Summary BuildSummary(const fir::Module &m, const fir::Function &f, const FunctionFacts &facts);

// FIR 类型的字节数；未知（不完整、void、函数）返回 false
bool TypeSize(const fir::Module &m, fir::TypeId type, int64_t &out);
// 位置的写法：文件名:行（同金标准）
std::string SiteOf(const fir::Module &m, const fir::Loc &loc);

}  // namespace interp
}  // namespace oobsum

#endif
