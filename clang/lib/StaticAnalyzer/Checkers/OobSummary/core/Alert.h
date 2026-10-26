//===-- Alert.h - Final alerts from decision records ------------*- C++ -*-===//
//
// 告警成品（SPEC-0013 §2.2；06- F-30…F-35、A-92）：只由判定记录的机器字段生成位置、BugType、消息、调用链 note、指纹，
// 与 replay.py 的 make_alert / alert_msg / obj_name 逐字相同（cases/summaries/alerts.json 是金标准）。
// 这里的告警是数据；SPEC-0001 的标签前缀与四个 BugType 由 report/Reporter 接上时套上（S8c）。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ALERT_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ALERT_H

#include "OobSummary/core/Summary.h"

#include "llvm/Support/JSON.h"

#include <string>
#include <vector>

namespace oobsum {
namespace alert {

struct Alert {
    std::string checker = "OobSummary";
    std::string bugType;
    std::string category = "Memory error";
    std::string level;  // L1 / L2
    std::string fn;     // 汇合点函数
    std::string loc;    // 汇合点调用（F-30）
    std::string warnMsg;  // F-32（原名 msg，与 Decision::warnMsg 同理改名，避免与 xir 里的同名符号冲突，SPEC-0114）
    std::vector<std::string> notes;  // F-33
    std::string fp;                  // F-34
};

// F-32 的 {object}
std::string ObjName(const std::string &obj, const std::vector<summary::Param> &params);
// F-32 的消息正文（跨文件的链附在末尾，F-33 ③）
std::string AlertMessage(const summary::Rep &rep, const std::vector<summary::Param> &params);
Alert MakeAlert(const std::string &fn, const summary::Summary &caller, const summary::Rep &rep);
// 与 alerts.json 的一条同形（不含 case）
llvm::json::Value AlertToJson(const Alert &a);

}  // namespace alert
}  // namespace oobsum

#endif
