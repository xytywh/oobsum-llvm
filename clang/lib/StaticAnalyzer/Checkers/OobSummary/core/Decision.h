//===-- Decision.h - One verdict of the OobSummary engine -------*- C++ -*-===//
//
// 一条判定（06- D-26 的 reps[] 加上 SPEC-0001 的标签）。引擎在汇合点、入口、断链处
// 各产出一条；Policy 决定它报不报，DecisionLog 不论报不报都记下来（调试与复核用）。
// 位置一律是 文件:行:列（FIR 的说法），由 report/ 换成 SourceLocation（06- A-91）。
// 不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DECISION_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_DECISION_H

#include "OobSummary/core/Label.h"

#include <string>
#include <vector>

namespace oobsum {

struct Site {
    std::string file;
    unsigned line = 0;
    unsigned col = 0;
};

// 调用链上的一跳：函数名 + 本跳的调用点（最后一跳是访问点）
struct Hop {
    std::string fn;
    Site site;
};

struct Decision {
    Label label = Label::Suspect;
    std::string reason;  // SUSPECT：账本原因（entry_unproven、type_range_only…）；SAFE：怎么证明的
    Site site;           // 告警位置：汇合点那次调用 / 入口 / 断链的调用点（F-30）
    std::string fn;      // 汇合点函数键
    std::string kind;    // "R" / "W"
    std::string obj;     // 对象（D-20 机器写法）
    std::string have;    // 对象剩余字节（规范表达式）
    std::string endLo;   // 访问末端区间
    std::string endHi;
    std::string bound;  // 上界来源标签（D-22）
    std::string dir;    // "back"：负偏移（越到指针前面的字节数，SPEC-0039）；空 = 正向
    unsigned depth = 0;
    unsigned effDepth = 0;
    std::vector<Hop> chain;
    std::string msg;     // F-32 正文（不含标签前缀）
    std::string fp;      // F-34 指纹
    std::string origin;  // 谁产生的：engine / debug-intrinsic
    std::string dupOf;  // 同一入口同一次访问的另一条账本（报出的那条）的指纹：日志照写、不报（SPEC-0036）
};

}  // namespace oobsum

#endif
