//===-- SourceMap.h - Proven diagnostic source paths ------------*- C++ -*-===//
// SPEC-0136 / D-06：仅回译诊断，不参与推理或指纹。
#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SOURCEMAP_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_SOURCEMAP_H
#include "OobSummary/core/Decision.h"
#include "OobSummary/core/Fir.h"
#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace oobsum {
constexpr const char *kUnresolvedSourcePrefix = "<oobsum-unresolved>/";
struct SourceResolution {
    Site site;
    std::string status;
    size_t candidates = 0;
};
class SourceMap {
public:
    explicit SourceMap(const std::vector<const fir::Module *> &modules);
    SourceResolution Resolve(const std::vector<std::string> &keys, const std::string &source) const;
    std::vector<std::string> KeysForName(const std::string &name) const;
    std::string Name(const std::string &key) const;

private:
    using Paths = std::map<std::string, std::string>;  // 规范路径 → 文件表原路径
    struct Owner {
        const fir::Module *module;
        const fir::Function *function;
    };
    std::map<std::string, Owner> owners;
    std::map<std::string, std::vector<std::string>> names;
    std::map<const fir::Module *, std::map<std::string, Paths>> moduleFiles;
    std::map<std::string, Paths> allFiles;
    // 仅模块存在同basename且实际请求过时，建该函数的位置索引。
    mutable std::map<std::string, std::map<std::string, Paths>> functionSites;
    Paths Candidates(const std::string &key, const std::string &source, const std::string &base) const;
};
}  // namespace oobsum
#endif
