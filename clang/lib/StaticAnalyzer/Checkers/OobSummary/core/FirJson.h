//===-- FirJson.h - FIR <-> JSON (oobsum-fir/1) -----------------*- C++ -*-===//
//
// FIR 的 JSON 读写（SPEC-0005；06- §5.2、D10：llvm::json，不引入第三方库）。
// 写出与 06- §5.2 的例子逐字段一致；对象键按字母排序，同一个 Module 输出确定。
// 读入严格：不认识的键 / op / k、引用不存在的局部 / 块 / 记录 / 文件都报错（SPEC-0005 §2.3 #8、#9），
// 从不 assert（06- A-96），返回 false 与带位置的原因（funcs[0].blocks[1].insts[0].addr: …）。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRJSON_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIRJSON_H

#include "OobSummary/core/Fir.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"

#include <string>

namespace oobsum {
namespace fir {

constexpr const char *kFirSchema = "oobsum-fir/1";

llvm::json::Value ModuleToJson(const Module &module);
// 失败时 out 处于未定义的中间状态，调用方不得使用（把这个 TU 当成 export 失败）
bool ModuleFromJson(const llvm::json::Value &value, Module &out, std::string &error);

// 紧凑 JSON；先写 <path>.tmp 再改名（06- X-01）
bool WriteModuleFile(llvm::StringRef path, const Module &module, std::string &error);
bool ReadModuleFile(llvm::StringRef path, Module &out, std::string &error);

}  // namespace fir
}  // namespace oobsum

#endif
