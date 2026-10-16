//===-- Models.h - Model table for library functions ------------*- C++ -*-===//
//
// 模型表（SPEC-0018；06- §4.4 I-40…I-44）：访问类函数（securec / libc 的内存函数）、分配器、纯函数、释放函数。
// 内置的默认表（06- I-40 的默认值）；ModelFile（YAML，SPEC-0033）合并进来。不依赖 Clang。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_MODELS_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_MODELS_H

#include "llvm/ADT/StringRef.h"

#include <string>
#include <vector>

namespace oobsum {
namespace models {

// 访问类函数的一条条件：第 base 个实参指向的对象上读 / 写 bytes 字节（bytes、when 是实参 p0、p1… 的式子，D-21 的写法）
struct AccessCond {
    int base = 0;
    std::string bytes;
    std::string kind;   // R / W
    std::string when;   // 访问发生的条件（如 "p3 <= p1"）；空 = 总是
    bool upto = false;  // bytes 只是上界：实际访问 1 … bytes 字节（strcpy_s 族，SPEC-0034）
};

struct AccessModel {
    std::string name;
    std::vector<AccessCond> conds;
    int destMax = -1;  // securec 规则（F-03）用的实参下标：0 或大于 0x7fffffff 不写；回绕的减法 → 守卫；−1 = 没有
};

struct AllocModel {
    std::string name;
    std::string size;  // 实参的线性式或乘积（I-41）
};

enum class Kind { None, Access, Alloc, Pure, Free };

class ModelTable {
public:
    // 06- §4.4 I-40 的默认表
    static const ModelTable &Default();

    Kind KindOf(llvm::StringRef name) const;
    const AccessModel *Access(llvm::StringRef name) const;
    const AllocModel *Alloc(llvm::StringRef name) const;

    std::vector<AccessModel> access;
    std::vector<AllocModel> allocators;
    std::vector<std::string> pureNames;
    std::vector<std::string> purePatterns;  // 正则（llvm::Regex）
    std::vector<std::string> frees;
};

// 摘要里 model 字段的写法：access / alloc / pure / free；None → 空
llvm::StringRef KindName(Kind kind);

// ModelFile（SPEC-0033 §2.2）：YAML 文本合并进 table——同名条目替换（不论原来在哪一类），新名字追加；defaults: false 时先清空
// （保留调试内建函数的模式）。坏条目跳过并写一条 warning（table 里原有的同名条目保留）。
// YAML 语法 / 结构错：返回 false、error 写原因，table 不变。
bool MergeModelYaml(llvm::StringRef text, ModelTable &table, std::vector<std::string> &warnings, std::string &error);

// Dump=models（SPEC-0033 §2.3）：每条模型一行
std::vector<std::string> DescribeModels(const ModelTable &table);

}  // namespace models
}  // namespace oobsum

#endif
