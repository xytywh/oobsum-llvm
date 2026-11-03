//===-- AccessInfo.h - Diagnostic facts about an access ----------*- C++ -*-===//
//
// SPEC-0125：仅供诊断；不参与任何安全证明。表达式由 AST 规范化，不含源码行或字符串内容。
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ACCESSINFO_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_ACCESSINFO_H

#include <cstdint>
#include <string>

namespace llvm {
namespace json {
class Value;
class Object;
}  // namespace json
}  // namespace llvm

namespace oobsum {

struct AccessInfo {
    std::string category;
    std::string mode;  // R / W；未知调用的模式为空，不臆测它访问了内存
    std::string expression;
    std::string operation;
    std::string type;
    std::string elementType;
    std::string index;
    std::string origin;
    int64_t elementSize = 0;
    int64_t width = 0;
    int64_t fieldOffset = 0;
    int64_t argument = -1;
    bool nonlinear = false;
};

llvm::json::Object AccessInfoToJson(const AccessInfo &info);
bool AccessInfoFromJson(const llvm::json::Value &value, AccessInfo &out, std::string &error);

}  // namespace oobsum
#endif
