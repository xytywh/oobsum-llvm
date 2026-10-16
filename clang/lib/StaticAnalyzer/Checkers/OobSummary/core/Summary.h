//===-- Summary.h - Function summaries (oobsum-summary/1) -------*- C++ -*-===//
//
// 函数摘要（SPEC-0012 §2.1；06- §5.3 D-20…D-30，字段定义 oobsum/cases/summaries/summary.schema.json）。
// 只含机器字段：读入时整棵忽略键名为 doc 的内容（D-30），其余严格校验（缺键、多键、类型、表达式 / 条件解析、枚举），
// 错误带位置，不 assert（A-96）。表达式与条件存成字符串，比较时用 LinExpr 解析成规范形式。可选键记下"有没有"，写回时原样保留。
// 不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SUMMARY_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SUMMARY_H

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace oobsum {
namespace summary {

constexpr const char *kSummarySchema = "oobsum-summary/1";

struct Param {
    std::string name;
    std::string type;
    llvm::Optional<int64_t> size;  // 指向完整类型的指针：所指类型的字节数（SPEC-0019 §2.2，入口记账 typed_field 用）
};

struct FieldInfo {
    int64_t off = 0;
    int64_t size = 0;
    llvm::Optional<std::string> type;
};

// 真正的访问点（D-23）
struct Access {
    std::string fn;
    std::string key;
    std::string at;
    std::string end;
};

// 被覆盖、不再单列的较弱访问（A-44）
struct Absorbed {
    std::string kind;
    std::string endLo;
    std::string endHi;
    std::string at;
};

// D-23
struct Req {
    std::string id;
    std::string kind;  // R / W
    std::string have;
    std::string obj;
    std::string endLo;
    std::string endHi;
    llvm::Optional<std::string> start;
    std::string bound;
    std::vector<std::string> guard;
    Access access;
    std::vector<std::string> path;
    int64_t depth = 0;
    int64_t effDepth = 0;
    bool blind = false;
    llvm::Optional<std::vector<Absorbed>> absorbed;
};

// D-24
struct Eff {
    std::string kind;
    llvm::Optional<std::string> size;
    llvm::Optional<bool> maybeNull;
    llvm::Optional<std::vector<std::string>> range;
    llvm::Optional<std::string> when;
    llvm::Optional<std::vector<std::string>> then;
    llvm::Optional<std::string> target;
    llvm::Optional<std::string> value;
    llvm::Optional<int64_t> effDepth;
    llvm::Optional<std::string> bound;
};

struct PtrArg;

// 实参（D-25）：整数实参写表达式；指针实参写 {obj, have, …}；与越界无关的为 null
struct ArgValue {
    enum class Kind { Null, Expr, Ptr };
    Kind kind = Kind::Null;
    std::string expr;
    std::shared_ptr<PtrArg> ptr;
};

struct PtrArg {
    std::string obj;
    std::string have;
    llvm::Optional<std::string> alias;
    llvm::Optional<int64_t> effDepth;
    llvm::Optional<std::map<std::string, ArgValue>> fields;  // 值是表达式或指针（不为 null）
};

// 被调者效果代进来后的样子（results[].apply）
struct Apply {
    std::string kind;
    llvm::Optional<std::string> size;
    llvm::Optional<bool> maybeNull;
    llvm::Optional<std::vector<std::string>> range;
    llvm::Optional<std::string> when;
    llvm::Optional<std::vector<std::string>> then;
    llvm::Optional<int64_t> arg;
    llvm::Optional<std::string> field;
    llvm::Optional<std::string> value;
};

// 调用点上被调者每条 Req / Eff 的结局（D-25 的表）
struct Result {
    enum class Kind { Req, Eff, Gen, GenEff, Pure, Open };
    Kind kind = Kind::Req;
    std::string req;      // Req
    std::string outcome;  // Req：up / safe / L1 / L2 / open
    std::string as;       // up
    std::string report;   // L1 / L2
    std::string reason;   // open
    int64_t eff = 0;      // Eff
    Apply apply;          // Eff
    std::vector<std::string> gen;
    std::vector<int64_t> genEff;
    std::string open;  // 未知函数的账本原因
};

struct Local {
    std::string lo;
    std::string hi;
    std::string bound;
};

struct CallGuard {
    std::string cond;
    std::string bound;
};

struct Call {
    std::string site;
    std::string callee;
    llvm::Optional<std::string> model;
    std::vector<ArgValue> args;
    llvm::Optional<std::map<std::string, Local>> locals;
    llvm::Optional<std::vector<CallGuard>> guards;
    std::vector<Result> results;
};

// D-26
struct Rep {
    std::string id;
    std::string level;  // L1 / L2
    bool reported = false;
    std::string site;
    Access access;
    std::string kind;
    std::string obj;
    llvm::Optional<std::string> via;
    std::string have;
    std::string endLo;
    std::string endHi;
    std::string bound;
    int64_t depth = 0;
    int64_t effDepth = 0;
    std::vector<std::string> chain;
    std::string fp;
};

// D-27
struct Open {
    std::string reason;
    std::string at;
    llvm::Optional<std::string> req;
};

struct Summary {
    std::string fn;
    std::string key;
    llvm::Optional<std::string> tu;
    std::string loc;
    std::vector<Param> params;
    llvm::Optional<std::map<std::string, FieldInfo>> fields;
    std::string status;
    std::vector<Req> reqs;
    std::vector<Eff> effs;
    std::vector<Call> calls;
    std::vector<Rep> reps;
    std::vector<Open> opens;
};

// 去掉所有键名为 doc 的内容（D-30：剩下的就是机器字段）
llvm::json::Value StripDoc(const llvm::json::Value &v);

bool ReadSummary(const llvm::json::Value &v, Summary &out, std::string &error);
llvm::json::Value SummaryToJson(const Summary &s);

bool ReadSummaryFile(llvm::StringRef path, Summary &out, std::string &error);
// 先写 <path>.tmp 再改名
bool WriteSummaryFile(llvm::StringRef path, const Summary &s, std::string &error);

}  // namespace summary
}  // namespace oobsum

#endif
