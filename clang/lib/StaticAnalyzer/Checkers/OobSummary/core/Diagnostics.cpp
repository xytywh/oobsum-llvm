//===-- Diagnostics.cpp - Access, layout, range and context ------*- C++ -*-===//
//
// SPEC-0126：范围基于已记录的量；不能把类型上界说成代码给定的长度。
//
//===----------------------------------------------------------------------===//
#include "OobSummary/core/Diagnostics.h"

#include "OobSummary/core/Alert.h"
#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/Key.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"

#include <cctype>
#include <climits>

namespace oobsum {
namespace {

std::string Readable(std::string text, const Decision &decision)
{
    if (text.find("_unmapped:") != std::string::npos || text.find("_unknown") != std::string::npos) {
        return "an unknown value";
    }
    text = StripGlobalSuffix(text);
    for (size_t i = 0; i < text.size(); ++i) {
        bool boundary = i == 0 || (!std::isalnum(static_cast<unsigned char>(text[i - 1])) && text[i - 1] != '_' &&
                                   text[i - 1] != ':');
        if (!boundary || text[i] != 'p') {
            continue;
        }
        size_t end = i + 1;
        while (end < text.size() && std::isdigit(static_cast<unsigned char>(text[end]))) {
            ++end;
        }
        if (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) {
            continue;
        }
        unsigned index = 0;
        if (end == i + 1 || llvm::StringRef(text).substr(i + 1, end - i - 1).getAsInteger(10, index) ||
            index >= decision.parameterNames.size()) {
            continue;
        }
        std::string name = decision.parameterNames[index].empty() ? "argument " + std::to_string(index + 1)
                                                                  : decision.parameterNames[index];
        text.replace(i, end - i, name);
        i += name.size() - 1;
    }
    return text;
}

std::string Range(const std::string &lo, const std::string &hi, const Decision &decision)
{
    return lo == hi ? Readable(hi, decision) : "[" + Readable(lo, decision) + ", " + Readable(hi, decision) + "]";
}

std::string Reason(const Decision &d)
{
    if (d.reason == "type_range_only") {
        return "no usable code constraint on the offset or length; only its C type provides a numeric limit";
    }
    if (d.reason == "untracked_pointer") {
        return "the pointer target and its extent are not tracked";
    }
    if (d.reason == "unknown_callee" || d.reason == "not_in_tu") {
        return "no analyzed definition or memory model for this callee";
    }
    if (d.reason == "cxx_call") {
        return "the C++ member call or constructor is not modeled; this is an unresolved call, not a proven access";
    }
    if (d.reason == "entry_unproven") {
        return "the required bound remains unproved at an analysis entry";
    }
    if (d.reason == "typed_field") {
        return "the declared record type does not establish the actual backing-object extent";
    }
    if (d.reason == "guard_false") {
        return "the access condition is false under these arguments";
    }
    if (d.reason == "subobject_overflow") {
        return "the access can leave an inner array member or row";
    }
    if (d.reason == "fptr") {
        return "the function pointer target is unresolved";
    }
    if (d.reason == "recursion") {
        return "the recursive cycle is not solved to a fixed point";
    }
    if (d.reason == "pointer_stepping") {
        return "no usable loop bound on the pointer offset";
    }
    if (d.reason == "global_value") {
        return "the current global value is unknown at this call";
    }
    return d.reason.empty()     ? "the conclusion applies when this access executes"
           : !d.warnMsg.empty() ? d.warnMsg
                                : d.reason;
}

}  // namespace

std::string DiagnosticGroup(const Decision &decision)
{
    const Site &site = decision.sink.file.empty() ? decision.site : decision.sink;
    return HashHex(decision.target + "|" + site.file + ":" + std::to_string(site.line));
}

std::string DiagnosticMessage(const Decision &d)
{
    const AccessInfo *info = d.accesses.empty() ? nullptr : &d.accesses.front();
    std::string category = info != nullptr ? info->category : "unresolved-access";
    std::string mode = info != nullptr && !info->mode.empty() ? info->mode : d.kind;
    std::string verb = mode == "W" ? "write" : mode == "R" ? "read" : "access";
    std::string expression = info != nullptr ? info->expression : "the operation at this location";
    if (info == nullptr) {
        return d.warnMsg;
    }
    std::string out = "[access=" + category + "] ";
    if (category == "call") {
        out += "Unresolved call: " + (info->operation.empty() ? expression : info->operation);
    } else {
        out += (d.label == Label::Safe       ? "Proven safe "
                : d.label == Label::Definite ? "Out-of-bounds "
                : d.label == Label::Possible ? "Potential out-of-bounds "
                                             : "Bounds unproved for ") +
               verb;
        out += ": " + expression;
        if (info != nullptr && !info->operation.empty()) {
            out += " in " + info->operation;
        }
        if (info != nullptr && info->argument >= 0) {
            out += " (argument " + std::to_string(info->argument + 1) + ")";
        }
    }
    std::vector<summary::Param> params;
    for (const auto &name : d.parameterNames) {
        summary::Param param;
        param.name = name;
        params.push_back(std::move(param));
    }
    std::string object = d.obj.empty() || d.obj == "?" || llvm::StringRef(d.obj).startswith("local:_unknown")
                             ? "target reached by " + expression
                             : alert::ObjName(StripGlobalSuffix(d.obj), params);
    if (category != "call") {
        out += "; Object: " + object;
    }
    int64_t have = 0;
    bool knownHave = !d.have.empty() && !llvm::StringRef(d.have).getAsInteger(10, have) && have >= 0;
    bool unknown = d.have.empty() || d.have.find("_unknown") != std::string::npos ||
                   d.have.find("_unmapped:") != std::string::npos;
    if (category != "call") {
        out += "; Available extent: " + (unknown ? "unknown" : Readable(d.have, d) + " bytes");
    }
    if (d.objectSize >= 0 && (!knownHave || d.objectSize != have)) {
        out += "; Modeled whole-object extent: " + std::to_string(d.objectSize) + " bytes";
    }
    if (info != nullptr && (category != "call" || info->argument >= 0)) {
        out += "; Access type: " + info->type;
        if (info->width > 0) {
            out += ", width " + std::to_string(info->width) + " bytes";
        }
        if (info->elementSize > 0) {
            out += "; Element view: " + info->elementType + ", " + std::to_string(info->elementSize) + " bytes each";
            if (knownHave) {
                out += ", " + std::to_string(have / info->elementSize) + " complete elements available at this pointer";
            }
        }
        if (info->fieldOffset > 0) {
            out += "; Field offset: " + std::to_string(info->fieldOffset) + " bytes in its record view";
        }
        if (!info->index.empty()) {
            out += "; Index expression: " + info->index;
        }
        if (!info->origin.empty()) {
            out += "; Possible pointer source: " + info->origin;
        }
        for (size_t i = 1; i < d.accesses.size(); ++i) {
            out += "; Also at this location: " + d.accesses[i].expression;
        }
    }
    int64_t lo = 0;
    int64_t hi = 0;
    bool numeric = !d.endLo.empty() && !llvm::StringRef(d.endLo).getAsInteger(10, lo) &&
                   !llvm::StringRef(d.endHi).getAsInteger(10, hi);
    bool back = d.dir == "back" || d.dir == "subback";
    if (!d.endHi.empty() && d.reason != "guard_false") {
        out += back ? "; Required bytes before pointer: " : "; Required exclusive end: ";
        out += Range(d.endLo, d.endHi, d) + " bytes";
        if (knownHave && numeric && hi > have && d.label != Label::Safe) {
            out += "; " + std::string(back ? "Underflow" : "Extent exceeded") + " by up to " +
                   std::to_string(hi - have) + " bytes";
        }
        int64_t delta = 0;
        if (!back && info != nullptr && (category == "array-index" || category == "pointer-access") &&
            info->width > 0 && info->elementSize > 0 && info->fieldOffset >= 0 && numeric &&
            !llvm::AddOverflow(info->width, info->fieldOffset, delta) && lo >= INT64_MIN + delta &&
            hi >= INT64_MIN + delta && (lo - delta) % info->elementSize == 0 && (hi - delta) % info->elementSize == 0) {
            out += "; Effective element indices relative to this pointer: [" +
                   std::to_string((lo - delta) / info->elementSize) + ", " +
                   std::to_string((hi - delta) / info->elementSize) + "]";
            if (knownHave && have >= info->elementSize) {
                out += ", valid [0, " + std::to_string(have / info->elementSize - 1) + "]";
            }
        }
    }
    if (d.dir == "sub" || d.dir == "subback") {
        out += "; Bound scope: inner array member or row";
    }
    out += "; Reason: " + Reason(d);
    if (!d.bound.empty()) {
        std::string source = d.reason == "type_range_only" || d.bound == "type" || d.bound == "data"
                                 ? "C type range only"
                             : d.bound == "loop"                        ? "loop condition"
                             : d.bound == "check"                       ? "program check"
                             : d.bound == "guard"                       ? "branch condition"
                             : d.bound == "ret"                         ? "returned value range"
                             : d.bound == "model"                       ? "library size argument"
                             : d.bound == "exact" || d.bound == "const" ? "arguments or constants"
                                                                        : d.bound;
        out += "; Bound source: " + source;
    }
    if (!d.contextOperation.empty()) {
        out += "; Context: " + d.contextOperation;
    }
    if (!d.chain.empty()) {
        out += "; Call chain: ";
        for (size_t i = 0; i < d.chain.size(); ++i) {
            const auto &hop = d.chain[i];
            out += (i == 0 ? "" : " -> ") + hop.fn + " (" + llvm::sys::path::filename(hop.site.file).str() + ":" +
                   std::to_string(hop.site.line) + ")";
        }
    }
    return out;
}

}  // namespace oobsum
