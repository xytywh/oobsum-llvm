//===-- Solver.h - Bottom-up propagation of requirements --------*- C++ -*-===//
//
// 从局部事实推到底（SPEC-0013 §2.1；06- A-44、A-80）：只用每个函数自己的事实（depth 0 的 Req 及其 absorbed、调用点的实参 / 局部区间 /
// 守卫），按自底向上的顺序把上传的 Req、判定（L1 / L2）与入口账本全部推出来。与 replay.py 的 derive / absorb 逐项同语义；
// 摘要里上传来的 Req、reps、opens 一概不读。完整的调度（调用图、SCC、递归，A-80…A-82）在 S8c。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SOLVER_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SOLVER_H

#include "OobSummary/core/Apply.h"
#include "OobSummary/core/LinExpr.h"
#include "OobSummary/core/Summary.h"

#include <map>
#include <string>
#include <vector>

namespace oobsum {
namespace solver {

// 推出的一条 Req
struct DerivedReq {
    LinExpr have;
    std::string obj;
    LinExpr lo;
    LinExpr hi;
    std::vector<LinExpr> guards;  // 各为 L ≤ 0
    std::string gkey;             // 守卫的规范文本集合（覆盖合并用）
    int64_t depth = 0;
    int64_t effDepth = 0;
    std::string bound;
    std::vector<std::string> path;
    summary::Access access;
    std::string kind;
};

// 调用点上被调者一条 Req 的结局（Dump=apply 与判定日志用，SPEC-0021 §2.2）
struct SiteOutcome {
    std::string site;
    std::string callee;
    apply::Outcome mine;
    DerivedReq req;  // 被调者的那条
};

struct FunctionResult {
    std::vector<DerivedReq> reqs;  // 覆盖合并之后
    std::vector<SiteOutcome> sites;  // 调用点上的每个结局（判出 L1 / L2 的另在 reps，上传的并进 reqs）
    std::vector<summary::Rep> reps;       // 本函数判出的（L1 / L2）
    std::vector<std::string> entryOpens;  // 入口函数：每条推出的 Req 的账本原因
    bool entry = false;                   // 没有调用方
};

struct PropagateResult {
    std::vector<std::string> order;  // 求解顺序
    std::map<std::string, FunctionResult> fns;
    unsigned up = 0;     // 上传的 Req 条数
    unsigned rep = 0;    // 判定条数
    unsigned entry = 0;  // 入口账本条数
};

PropagateResult Propagate(const std::map<std::string, summary::Summary> &fns);
// A-44：同一 have、对象、守卫，end 上界恒不大于另一条的那条并掉（相等时留前面的）
std::vector<DerivedReq> Absorb(const std::vector<DerivedReq> &cand);
// 比较用的键（同 replay.py 的 req_key：have、对象、末端、守卫集合、depth）
std::string ReqKey(const DerivedReq &r);
std::string ReqKey(const summary::Req &r);

}  // namespace solver
}  // namespace oobsum

#endif
