//===-- Apply.h - Call-site substitution and decision -----------*- C++ -*-===//
//
// 调用点判定（SPEC-0012 §2.2–§2.4；06- §6.6 A-60…A-64、F-10…F-22）：被调者的 Req / 效果代进调用点之后怎么判。
// 逐项移植 replay.py 的 build_map / subst_obj / eliminate_locals / classify / rep_of / apply_eff 与 replay() 的逐调用点核对；
// CheckCase 输出的文本与 replay.py --dir <机器版> <用例> -v 逐行相同（Dump=apply 的格式，SPEC-0001 §6.1）。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_APPLY_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_APPLY_H

#include "OobSummary/core/LinExpr.h"
#include "OobSummary/core/Range.h"
#include "OobSummary/core/Summary.h"

#include "llvm/ADT/Optional.h"

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace oobsum {
namespace apply {

// 浅层判定交给现有规则（F-20；SPEC-0001 §3 的 InlineDepth 默认值，同 replay.py 的 INLINE_DEPTH）
constexpr int64_t kInlineDepth = 2;

// classify 的结果（同 replay.py 返回的 dict）
struct Outcome {
    std::string outcome;  // safe / L1 / L2 / up / open
    std::string dir;  // 结局对应的 Req 方向：空 = 照被调者的那条；"sub" = 带窗口实参的内层那条（SPEC-0104 §2.3 3b）
    std::string reason;  // open：账本原因；safe：怎么证明的（guard_false / within / guard_entailed / guard_bounds）
    std::string why;  // 说明（只给 Dump=apply 看）
    LinExpr have;
    LinExpr endLo;
    LinExpr endHi;
    std::vector<LinExpr> guards;  // 代换后的守卫原子（各为 L ≤ 0）
    std::string obj;
    llvm::Optional<std::string> via;
    // 以下在"守卫在此为假"之外的结局里才有
    bool hasBase = false;
    LinExpr end2Lo;  // 调用方局部换成区间之后
    LinExpr end2Hi;
    int64_t depth = 0;
    int64_t effDepth = 0;
    std::string bound;
    std::vector<LinExpr> guards2;  // up：带上去的守卫
    bool hasHiEnd = false;         // 守卫界判定（F-12b）
    int64_t hiEnd = 0;
    bool hasLoEnd = false;
    int64_t loEnd = 0;
};

// 调用方的符号环境：形参与字段按类型名取范围（同 replay.py 的 Env(caller.params, caller.fields)）
SymEnv CallerEnv(const summary::Summary &caller);
// 自底向上的顺序：离叶子的层数，同层按名字（同 replay.py 的 topo）
std::vector<std::string> TopoOrder(const std::map<std::string, summary::Summary> &fns);
// 调用点的被调者在摘要表里的索引（SPEC-0027）：有 callee_key 且表里有就用它，否则按名字
const std::string &CalleeOf(const summary::Call &call, const std::map<std::string, summary::Summary> &fns);
// 判定记录逐字段比较（同 replay.py 的 diff_rep）；返回不同的字段
std::vector<std::string> DiffRep(const summary::Rep &want, const summary::Rep &got);

// 调用点处的环境守卫（调用方的守卫，条件 → 原子，带来源标签）
using EnvGuards = std::vector<std::pair<LinExpr, Tag>>;
EnvGuards EnvGuardsOf(const summary::Call &call);

// subLabel：Subobject=label（SPEC-0105）——内层 Req 的 L1 / L2 不改判
Outcome Classify(const summary::Req &req, const summary::Call &call, const summary::Summary &caller,
                 const EnvGuards &envGuards, bool subLabel = false);
// 成员数组实参的外层对象（SPEC-0035）：req 的对象是 *pK、第 K 个实参带 parent → 把它换成外层对象的调用点；否则 None
llvm::Optional<summary::Call> ParentCall(const summary::Req &req, const summary::Call &call);
// 带窗口的实参（SPEC-0104 §2.3 3b）：第 K 个实参换成只剩内层（sub）的调用点；不适用时 None
llvm::Optional<summary::Call> SubCall(const summary::Req &req, const summary::Call &call);
// CharAlias=verify 的一条不别名假设在调用点核对（SPEC-0106 §2.3）：两个根各换成调用方的说法——
// ok（两个不同的具体对象、或调用方的局部 / 堆对调用方的形参所指）/ same（同一个对象，detail 是它）/ unknown（有一边不知道指向哪）/
// up（换成调用方的一对 up，往上传）
struct AssumeCheck {
    std::string verdict;
    std::string detail;
    std::pair<std::string, std::string> up;
};
AssumeCheck CheckAssume(const std::pair<std::string, std::string> &assume, const summary::Call &call);
// 调用点判定的全部结局（SPEC-0035 §2.2）：(结局, 调用点（外层那一条是换过实参的）, 是不是外层对象那一条)
struct SiteClass {
    Outcome mine;
    summary::Call call;
    bool parent = false;
};
std::vector<SiteClass> ClassifyAll(const summary::Req &req, const summary::Call &call, const summary::Summary &caller,
                                   const EnvGuards &envGuards, bool subLabel = false);
// 指纹里的对象：反向 Req 加 "#back"，与同一次访问的正向区分（SPEC-0039）；正向不变
std::string FingerprintObj(const std::string &obj, const std::string &dir);
// 判出 L1 / L2 时的判定记录（与 JSON reps[] 同形，不含 id）
summary::Rep RepOf(const Outcome &mine, const summary::Call &call, const std::string &fn,
                   const summary::Summary &caller, const summary::Req &calleeReq);
// 被调者效果代进调用点；不认识的效果返回 None
llvm::Optional<summary::Apply> ApplyEff(const summary::Eff &eff, const summary::Call &call);

struct CheckResult {
    std::vector<std::string> lines;   // 与 replay.py -v 同格式（Dump=apply）
    std::vector<std::string> errors;  // 不一致的地方
};
// 一个用例（函数名 → 摘要）逐调用点核对（同 replay.py 的 replay() 的前半，不含 @expect 对照）
CheckResult CheckCase(const std::string &caseId, const std::map<std::string, summary::Summary> &fns);

}  // namespace apply
}  // namespace oobsum

#endif
