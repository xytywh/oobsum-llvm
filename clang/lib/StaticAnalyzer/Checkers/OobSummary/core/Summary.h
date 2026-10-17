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
    std::string dir;  // "back"：反向 Req，have 是 bext(…)、end 是越到基址前面的字节数（负偏移，SPEC-0039）；空 = 正向
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
    llvm::Optional<bool> may;  // field：有的返回路径没写（调用方取旧值与新值的并，SPEC-0073）
    // ret_ptr：返回的指针在一个 sub 字节的子对象（成员数组 / 多维数组的一行）里，偏移 subOff（SPEC-0107 §2.2）
    llvm::Optional<std::string> sub;
    llvm::Optional<std::string> subOff;
};

struct PtrArg;

// 实参（D-25）：整数实参写表达式；指针实参写 {obj, have, …}；与越界无关的为 null
struct ArgValue {
    enum class Kind { Null, Expr, Ptr };
    Kind kind = Kind::Null;
    std::string expr;
    std::shared_ptr<PtrArg> ptr;
};

// 成员数组实参的外层对象（SPEC-0035）
struct OuterObj {
    std::string obj;
    std::string have;
};

struct PtrArg {
    std::string obj;
    std::string have;
    llvm::Optional<OuterObj> parent;  // 实参是形参所指结构体里的成员数组时：外层对象（SPEC-0035）
    llvm::Optional<std::string> back;  // 这个指针前面还有多少字节（调用方的说法；负偏移，SPEC-0039）
    // 指针偏移是区间时（SPEC-0095）：下界（不知道时没有）、上界、上界的来源标签；have 是按上界算的最小剩余
    llvm::Optional<std::string> offLo;
    llvm::Optional<std::string> offHi;
    llvm::Optional<std::string> offBound;
    // 实参指向成员数组 / 多维数组的一行时：从这个指针起子对象还剩多少字节（取最小的；SPEC-0104 §2.3 3b）
    llvm::Optional<std::string> sub;
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
    bool parent = false;  // Req：外层对象那一条（SPEC-0035 §2.2）
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
    llvm::Optional<std::string> calleeKey;  // 被调者的函数键（D-07；引擎写，有它时按它找被调者，SPEC-0027）
    llvm::Optional<std::string> model;
    std::vector<ArgValue> args;
    llvm::Optional<std::map<std::string, Local>> locals;
    llvm::Optional<std::vector<CallGuard>> guards;
    std::vector<Result> results;
    bool alt = false;  // 函数指针有多个可能的目标、这是其中一个：判定时末端下界当 0（SPEC-0058）
    bool ind = false;  // 由间接调用解析出来的（只给调试与量尺看，不影响判定，SPEC-0058）
    // 调用前已经不是入口值的整数全局：g:名 → 当前确切值，值不知道写 "?"；"g:*" → "?" 表示任何全局都可能改过（SPEC-0064）
    llvm::Optional<std::map<std::string, std::string>> gvals;
};

// D-26
struct Rep {
    std::string id;
    std::string level;  // L1 / L2
    std::string dir;    // 同 Req::dir（SPEC-0039）
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
    // 可能写的位置（mod-ref，SPEC-0045 §2.1）：pK、pK+off:size、*pK->f、g:名字、g:*、*；空 = 不写被跟踪的内存
    std::vector<std::string> mods;
    // CharAlias=verify 的不别名假设（SPEC-0106）：根（p<k>、p<k>->f、g:名）的无序对；空 = 没有
    std::vector<std::pair<std::string, std::string>> assumes;
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
