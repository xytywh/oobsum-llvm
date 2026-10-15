//===-- Models.cpp - Model table for library functions ----------*- C++ -*-===//
//
// 见 Models.h。规则见 SPEC-0018 §2.1。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Models.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Regex.h"

namespace oobsum {
namespace models {

const ModelTable &ModelTable::Default()
{
    static const ModelTable table = [] {
        ModelTable t;
        // F-03 / F-04：securec——目的一侧总是写 destMax 字节；源一侧只在 count <= destMax 时读
        AccessCond destMax{0, "p1", "W", ""};
        AccessCond src{2, "p3", "R", "p3 <= p1"};
        t.access.push_back({"memcpy_s", {destMax, src}, 1});
        t.access.push_back({"memmove_s", {destMax, src}, 1});
        t.access.push_back({"memset_s", {destMax}, 1});
        // F-05：libc
        t.access.push_back({"memcpy", {{0, "p2", "W", ""}, {1, "p2", "R", ""}}, -1});
        t.access.push_back({"memmove", {{0, "p2", "W", ""}, {1, "p2", "R", ""}}, -1});
        t.access.push_back({"memset", {{0, "p2", "W", ""}}, -1});
        // 底层分配器（VOS_MemAlloc 的字节数是第几个形参待用户确认：07- 输入 I5）
        t.allocators.push_back({"VOS_MemAlloc", "p2"});
        t.allocators.push_back({"malloc", "p0"});
        t.allocators.push_back({"calloc", "p0*p1"});
        // 纯函数（清单待用户补：07- 输入 I9）
        t.pureNames = {"LOG_Print", "VOS_Printf"};
        t.purePatterns = {"^LOG_", "^DIAG_", "_Trace$"};
        t.frees = {"VOS_MemFree", "free"};
        return t;
    }();
    return table;
}

const AccessModel *ModelTable::Access(llvm::StringRef name) const
{
    for (const AccessModel &m : access) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

const AllocModel *ModelTable::Alloc(llvm::StringRef name) const
{
    for (const AllocModel &m : allocators) {
        if (m.name == name) {
            return &m;
        }
    }
    return nullptr;
}

Kind ModelTable::KindOf(llvm::StringRef name) const
{
    if (Access(name) != nullptr) {
        return Kind::Access;
    }
    if (Alloc(name) != nullptr) {
        return Kind::Alloc;
    }
    if (llvm::is_contained(frees, name)) {
        return Kind::Free;
    }
    if (llvm::is_contained(pureNames, name)) {
        return Kind::Pure;
    }
    for (const std::string &pattern : purePatterns) {
        if (llvm::Regex(pattern).match(name)) {
            return Kind::Pure;
        }
    }
    return Kind::None;
}

llvm::StringRef KindName(Kind kind)
{
    switch (kind) {
        case Kind::Access:
            return "access";
        case Kind::Alloc:
            return "alloc";
        case Kind::Pure:
            return "pure";
        case Kind::Free:
            return "free";
        case Kind::None:
            return "";
    }
    return "";
}

}  // namespace models
}  // namespace oobsum
