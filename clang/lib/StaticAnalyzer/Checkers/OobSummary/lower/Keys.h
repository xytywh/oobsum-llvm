//===-- Keys.h - Clang decls to OobSummary keys -----------------*- C++ -*-===//
//
// FunctionDecl / VarDecl → 键（SPEC-0003；06- D-01…D-05、D-08）。键的写法在 core/Key.h，这里只做
// "这个声明是哪种链接、定义在哪个文件"的判断——只有 lower/ 碰 Clang AST（06- §9.1）。
//
// 定义文件：定义（没有定义取第一处声明）的展开位置所在文件。头文件里的内部链接函数标 header，
// 以后键再加 #<FIR 哈希前 8 位>（D-02，S4）。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_KEYS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_LOWER_KEYS_H

#include "clang/AST/Decl.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include "llvm/ADT/DenseMap.h"

#include <string>
#include <utility>
#include <vector>

namespace oobsum {

// Dump=keys 的一行（SPEC-0003 §2.3）
struct KeyInfo {
    std::string key;
    std::string kind;     // func / global / static
    bool def = false;     // 本 TU 里有定义（含暂定定义）
    bool header = false;  // 内部链接函数且定义不在主文件
    std::string fileId;   // 位置所在文件的 id
    std::string file;     // 规范化路径
    unsigned line = 0;
    unsigned col = 0;
    std::string scopeName;  // DumpFilter 用：函数名 / 所在函数名 / 全局名
};

class KeyMaker {
public:
    explicit KeyMaker(const clang::SourceManager &sm) : sm(sm)
    {
    }

    std::string ForFunction(const clang::FunctionDecl *fd);
    // 没有键（普通局部、形参）返回空串
    std::string ForVar(const clang::VarDecl *vd);
    // ForFunction / ForVar 再带上位置与标志；没有键返回 false
    bool Describe(const clang::Decl *d, KeyInfo &info);

private:
    // (规范化路径, 文件 id)
    std::pair<std::string, std::string> FileOf(clang::FileID fid);
    clang::SourceLocation HomeLoc(const clang::Decl *home) const;

    const clang::SourceManager &sm;
    llvm::DenseMap<clang::FileID, std::pair<std::string, std::string>> files;
};

// 变量的定义：真正的定义；没有就取源码里最早的暂定定义（C 的 int a[10]; 在文件作用域是暂定定义）；都没有为 null。
// 注意 VarDecl::getActingDefinition() 只在"这一条本身是暂定定义"时非空，在 extern 那条上调用拿不到（实测）。
const clang::VarDecl *VarDefinition(const clang::VarDecl *vd);

// 一个 TU 里有键的实体，按 AST 遍历顺序：有函数体的函数、全局（文件作用域与块作用域 extern）、函数内 static。
// 同一实体的多次声明只出一条。
std::vector<KeyInfo> CollectKeys(const clang::TranslationUnitDecl *tu, KeyMaker &keys);

}  // namespace oobsum

#endif
