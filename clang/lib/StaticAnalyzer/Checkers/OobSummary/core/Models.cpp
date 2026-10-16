//===-- Models.cpp - Model table for library functions ----------*- C++ -*-===//
//
// 见 Models.h。规则见 SPEC-0018 §2.1；ModelFile 见 SPEC-0033。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Models.h"

#include "OobSummary/core/LinExpr.h"

#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cctype>
#include <set>

// ModelFile 的 YAML 结构（SPEC-0033 §2.2）：先原样读进来，再逐条校验
namespace oobsum {
namespace models {
namespace {

const char *const kDebugPattern = "^oobsum_debug_";  // 调试内建函数（SPEC-0001 §6.3），defaults: false 也保留

// 结构体整体赋值 a = b → __oobsum_copy(&a, &b, sizeof)（06- A-06，SPEC-0051）：W / R 各 sizeof 字节；内部调用，defaults: false 也保留
AccessModel StructCopyModel()
{
    return {"__oobsum_copy", {{0, "p2", "W", "", false}, {1, "p2", "R", "", false}}, -1};
}

struct RawCond {
    std::string base;
    std::string bytes;
    std::string kind;
    std::string when;
    bool upto = false;
};

struct RawAccess {
    std::string name;
    std::string destMax;
    std::vector<RawCond> conds;
};

struct RawAlloc {
    std::string name;
    std::string result;
    std::string size;
    bool maybeNull = false;  // 读入，P0 不用
};

struct RawPure {
    std::vector<std::string> names;
    std::vector<std::string> patterns;
};

struct RawFile {
    bool defaults = true;
    std::vector<RawAccess> access;
    std::vector<RawAlloc> allocators;
    RawPure pure;
    std::vector<std::string> frees;
};

}  // namespace
}  // namespace models
}  // namespace oobsum

LLVM_YAML_IS_SEQUENCE_VECTOR(oobsum::models::RawCond)
LLVM_YAML_IS_SEQUENCE_VECTOR(oobsum::models::RawAccess)
LLVM_YAML_IS_SEQUENCE_VECTOR(oobsum::models::RawAlloc)

namespace llvm {
namespace yaml {

template <>
struct MappingTraits<oobsum::models::RawCond> {
    static void mapping(IO &io, oobsum::models::RawCond &c)
    {
        io.mapRequired("base", c.base);
        io.mapRequired("bytes", c.bytes);
        io.mapRequired("kind", c.kind);
        io.mapOptional("when", c.when);
        io.mapOptional("upto", c.upto);
    }
};

template <>
struct MappingTraits<oobsum::models::RawAccess> {
    static void mapping(IO &io, oobsum::models::RawAccess &a)
    {
        io.mapRequired("name", a.name);
        io.mapOptional("dest_max", a.destMax);
        io.mapRequired("conds", a.conds);
    }
};

template <>
struct MappingTraits<oobsum::models::RawAlloc> {
    static void mapping(IO &io, oobsum::models::RawAlloc &a)
    {
        io.mapRequired("name", a.name);
        io.mapOptional("result", a.result);
        io.mapRequired("size", a.size);
        io.mapOptional("maybe_null", a.maybeNull);
    }
};

template <>
struct MappingTraits<oobsum::models::RawPure> {
    static void mapping(IO &io, oobsum::models::RawPure &p)
    {
        io.mapOptional("names", p.names);
        io.mapOptional("patterns", p.patterns);
    }
};

template <>
struct MappingTraits<oobsum::models::RawFile> {
    static void mapping(IO &io, oobsum::models::RawFile &f)
    {
        io.mapOptional("defaults", f.defaults);
        io.mapOptional("access", f.access);
        io.mapOptional("allocators", f.allocators);
        io.mapOptional("pure", f.pure);
        io.mapOptional("frees", f.frees);
    }
};

}  // namespace yaml
}  // namespace llvm

namespace oobsum {
namespace models {

const ModelTable &ModelTable::Default()
{
    static const ModelTable table = [] {
        ModelTable t;
        // F-03 / F-04：securec——目的一侧总是写 destMax 字节；源一侧只在 count <= destMax 时读
        AccessCond destMax{0, "p1", "W", "", false};
        AccessCond src{2, "p3", "R", "p3 <= p1", false};
        t.access.push_back({"memcpy_s", {destMax, src}, 1});
        t.access.push_back({"memmove_s", {destMax, src}, 1});
        t.access.push_back({"memset_s", {destMax}, 1});
        // F-07（SPEC-0034）：securec 的字符串函数最多写 destMax 字节（实际长度 + 1）；destMax 规则同 F-03；源串长度不建模
        AccessCond uptoDestMax{0, "p1", "W", "", true};
        for (const char *name : {"strcpy_s", "strncpy_s", "strcat_s", "strncat_s", "sprintf_s", "vsprintf_s",
                                 "snprintf_s", "vsnprintf_s"}) {
            t.access.push_back({name, {uptoDestMax}, 1});
        }
        // F-05：libc
        t.access.push_back({"memcpy", {{0, "p2", "W", "", false}, {1, "p2", "R", "", false}}, -1});
        t.access.push_back({"memmove", {{0, "p2", "W", "", false}, {1, "p2", "R", "", false}}, -1});
        t.access.push_back({"memset", {{0, "p2", "W", "", false}}, -1});
        // A-06：结构体整体赋值（lower 生成的内部调用，SPEC-0051）
        t.access.push_back(StructCopyModel());
        // 底层分配器（VOS_MemAlloc 的字节数是第几个形参待用户确认：07- 输入 I5）
        t.allocators.push_back({"VOS_MemAlloc", "p2"});
        t.allocators.push_back({"malloc", "p0"});
        t.allocators.push_back({"calloc", "p0*p1"});
        // 纯函数（清单待用户补：07- 输入 I9）
        t.pureNames = {"LOG_Print", "VOS_Printf"};
        t.purePatterns = {"^LOG_", "^DIAG_", "_Trace$", "^oobsum_debug_"};  // 最后一个：调试内建函数（SPEC-0001 §6.3）
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

namespace {

// "pN" → N；不是 → −1
int ParamOf(llvm::StringRef text)
{
    unsigned k = 0;
    if (!text.consume_front("p") || text.empty() || text.getAsInteger(10, k) || k > 64) {
        return -1;
    }
    return static_cast<int>(k);
}

// 式子里的名字都得是 pN（I-41）
bool OnlyParams(llvm::StringRef text, std::string &error)
{
    for (size_t i = 0; i < text.size();) {
        if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_') {
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) {
                ++j;
            }
            if (ParamOf(text.slice(i, j)) < 0) {
                error = "'" + text.slice(i, j).str() + "' is not an argument (p0, p1, ...)";
                return false;
            }
            i = j;
        } else {
            ++i;
        }
    }
    return true;
}

bool CheckLin(llvm::StringRef what, const std::string &text, std::string &error)
{
    LinExpr e;
    std::string why;
    if (!ParseLin(text, e, why)) {
        error = what.str() + " '" + text + "': " + why;
        return false;
    }
    if (!OnlyParams(text, why)) {
        error = what.str() + " '" + text + "': " + why;
        return false;
    }
    return true;
}

bool ToAccess(const RawAccess &raw, AccessModel &out, std::string &error)
{
    out.name = raw.name;
    out.destMax = -1;
    if (!raw.destMax.empty() && (out.destMax = ParamOf(raw.destMax)) < 0) {
        error = "dest_max '" + raw.destMax + "' is not an argument (p0, p1, ...)";
        return false;
    }
    if (raw.conds.empty()) {
        error = "no conds";
        return false;
    }
    for (const RawCond &c : raw.conds) {
        AccessCond cond;
        if ((cond.base = ParamOf(c.base)) < 0) {
            error = "base '" + c.base + "' is not an argument (p0, p1, ...)";
            return false;
        }
        if (!CheckLin("bytes", c.bytes, error)) {
            return false;
        }
        if (c.kind != "R" && c.kind != "W") {
            error = "kind '" + c.kind + "' is not R or W";
            return false;
        }
        std::vector<LinExpr> when;
        std::string why;
        if (!c.when.empty() && (!ParseCond(c.when, when, why) || !OnlyParams(c.when, why))) {
            error = "when '" + c.when + "': " + why;
            return false;
        }
        cond.bytes = c.bytes;
        cond.kind = c.kind;
        cond.when = c.when;
        cond.upto = c.upto;
        out.conds.push_back(cond);
    }
    return true;
}

// 一个名字只属于一类（SPEC-0033 §2.2）
void Forget(ModelTable &t, const std::string &name)
{
    t.access.erase(
        std::remove_if(t.access.begin(), t.access.end(), [&](const AccessModel &m) { return m.name == name; }),
        t.access.end());
    t.allocators.erase(
        std::remove_if(t.allocators.begin(), t.allocators.end(), [&](const AllocModel &m) { return m.name == name; }),
        t.allocators.end());
    t.pureNames.erase(std::remove(t.pureNames.begin(), t.pureNames.end(), name), t.pureNames.end());
    t.frees.erase(std::remove(t.frees.begin(), t.frees.end(), name), t.frees.end());
}

void OnDiag(const llvm::SMDiagnostic &d, void *context)
{
    std::string &out = *static_cast<std::string *>(context);
    if (out.empty()) {
        out = std::to_string(d.getLineNo()) + ":" + std::to_string(d.getColumnNo() + 1) + ": " + d.getMessage().str();
    }
}

}  // namespace

bool MergeModelYaml(llvm::StringRef text, ModelTable &table, std::vector<std::string> &warnings, std::string &error)
{
    RawFile raw;
    std::string diag;
    llvm::yaml::Input in(text, nullptr, OnDiag, &diag);
    in >> raw;
    if (in.error()) {
        error = diag.empty() ? "invalid YAML" : diag;
        return false;
    }
    ModelTable t = raw.defaults ? table : ModelTable();
    if (!raw.defaults) {
        t.purePatterns.push_back(kDebugPattern);
        t.access.push_back(StructCopyModel());
    }
    std::set<std::string> seen;
    auto take = [&](llvm::StringRef what, const std::string &name) {
        if (name.empty()) {
            warnings.push_back(what.str() + ": entry without a name, skipped");
            return false;
        }
        if (!seen.insert(name).second) {
            warnings.push_back(what.str() + " '" + name + "': listed more than once, the later entry wins");
        }
        return true;
    };
    for (const RawAccess &a : raw.access) {
        AccessModel m;
        std::string why;
        if (!take("access", a.name)) {
            continue;
        }
        if (!ToAccess(a, m, why)) {
            warnings.push_back("access '" + a.name + "': " + why + ", skipped");
            continue;
        }
        Forget(t, a.name);
        t.access.push_back(m);
    }
    for (const RawAlloc &a : raw.allocators) {
        std::string why;
        if (!take("allocators", a.name)) {
            continue;
        }
        if (!a.result.empty() && a.result != "ret") {
            warnings.push_back("allocators '" + a.name + "': result '" + a.result +
                               "' is not supported (only ret), skipped");
            continue;
        }
        if (!CheckLin("size", a.size, why)) {
            warnings.push_back("allocators '" + a.name + "': " + why + ", skipped");
            continue;
        }
        Forget(t, a.name);
        t.allocators.push_back({a.name, a.size});
    }
    for (const std::string &name : raw.pure.names) {
        if (take("pure", name)) {
            Forget(t, name);
            t.pureNames.push_back(name);
        }
    }
    for (const std::string &pattern : raw.pure.patterns) {
        std::string why;
        if (!llvm::Regex(pattern).isValid(why)) {
            warnings.push_back("pure pattern '" + pattern + "': " + why + ", skipped");
            continue;
        }
        if (!llvm::is_contained(t.purePatterns, pattern)) {
            t.purePatterns.push_back(pattern);
        }
    }
    for (const std::string &name : raw.frees) {
        if (take("frees", name)) {
            Forget(t, name);
            t.frees.push_back(name);
        }
    }
    table = std::move(t);
    return true;
}

std::vector<std::string> DescribeModels(const ModelTable &table)
{
    std::vector<std::string> out;
    for (const AccessModel &m : table.access) {
        std::string line = "access " + m.name;
        if (m.destMax >= 0) {
            line += " dest_max=p" + std::to_string(m.destMax);
        }
        for (const AccessCond &c : m.conds) {
            line += " " + c.kind + " p" + std::to_string(c.base) + "[" + (c.upto ? "<=" : "") + c.bytes + "]";
            if (!c.when.empty()) {
                line += " when " + c.when;
            }
        }
        out.push_back(line);
    }
    for (const AllocModel &m : table.allocators) {
        out.push_back("alloc " + m.name + " size=" + m.size);
    }
    for (const std::string &name : table.frees) {
        out.push_back("free " + name);
    }
    for (const std::string &name : table.pureNames) {
        out.push_back("pure name " + name);
    }
    for (const std::string &pattern : table.purePatterns) {
        out.push_back("pure pattern " + pattern);
    }
    return out;
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
