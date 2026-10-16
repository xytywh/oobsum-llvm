//===-- Alert.cpp - Final alerts from decision records ----------*- C++ -*-===//
//
// 见 Alert.h。文本逐字对照 replay.py（SPEC-0013 §2.2）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Alert.h"

#include "llvm/ADT/StringRef.h"

#include <cctype>
#include <set>
#include <utility>

namespace oobsum {
namespace alert {

namespace {

// 同 replay.py 的 BOUND_PHRASE
std::string BoundPhrase(const std::string &bound)
{
    static const std::pair<const char *, const char *> phrases[] = {
        {"loop", "a loop condition"},    {"check", "a check"},
        {"guard", "a guard condition"},  {"mask", "a bit mask"},
        {"branch", "a branch constant"}, {"ret", "a return value range"},
        {"const", "a constant"},         {"model", "a size argument of a library function"},
        {"exact", "the call arguments"}};  // exact 的 L2：函数指针的多个目标之一（SPEC-0058）
    for (const auto &p : phrases) {
        if (bound == p.first) {
            return p.second;
        }
    }
    return bound;
}

// "f@site" → (f, site)
std::pair<std::string, std::string> Hop(const std::string &hop)
{
    std::pair<llvm::StringRef, llvm::StringRef> parts = llvm::StringRef(hop).split('@');
    return {parts.first.str(), parts.second.str()};
}

// site 去掉最后一个 :行号 → 文件
std::string FileOf(const std::string &site)
{
    return llvm::StringRef(site).rsplit(':').first.str();
}

std::string ChainText(const std::vector<std::string> &chain)
{
    std::string out;
    for (size_t i = 0; i < chain.size(); ++i) {
        std::pair<std::string, std::string> h = Hop(chain[i]);
        out += (i == 0 ? "" : " -> ") + h.first + " (" + h.second + ")";
    }
    return out;
}

}  // namespace

std::string ObjName(const std::string &obj, const std::vector<summary::Param> &params)
{
    llvm::StringRef o = obj;
    unsigned arg = 0;
    if (o.startswith("local:_unknown") && !o.drop_front(14).getAsInteger(10, arg)) {
        return "the object passed as argument " + std::to_string(arg + 1);  // 不跟踪的指针实参（SPEC-0062）
    }
    if (o.startswith("g:") || o.startswith("local:")) {
        return "'" + o.split(':').second.str() + "'";
    }
    if (o.startswith("heap@")) {
        return "heap object allocated at " + o.drop_front(5).str();
    }
    // 同 re.sub(r'p(\d+)', 形参名, obj)：每个 p<数字> 换成形参名（下标越界的不换）
    std::string out;
    size_t i = 0;
    while (i < obj.size()) {
        if (obj[i] == 'p' && i + 1 < obj.size() && std::isdigit(static_cast<unsigned char>(obj[i + 1]))) {
            size_t j = i + 1;
            while (j < obj.size() && std::isdigit(static_cast<unsigned char>(obj[j]))) {
                ++j;
            }
            unsigned long long k = 0;
            bool bad = llvm::StringRef(obj).substr(i + 1, j - i - 1).getAsInteger(10, k);
            out += !bad && k < params.size() ? params[k].name : obj.substr(i, j - i);
            i = j;
        } else {
            out += obj[i++];
        }
    }
    return "'" + out + "'";
}

std::string AlertMessage(const summary::Rep &rep, const std::vector<summary::Param> &params)
{
    std::string w = rep.kind == "W" ? "write" : "read";
    std::string name = ObjName(rep.obj, params);
    std::string size = rep.have + " bytes";
    if (rep.via) {
        size += "; via " + llvm::StringRef(*rep.via).split(':').second.str();
    }
    std::string s;
    if (!rep.dir.empty()) {  // 负偏移：越到指针前面的字节数（SPEC-0039 §2.3，同 replay.py 的 alert_msg）
        s = std::string(rep.level == "L1" ? "Definite" : "Possible") + " out-of-bound " + w + " through a " +
            std::to_string(rep.depth) + "-level call chain: up to " + rep.endHi + " bytes before " + name + " (" +
            size + " available before the pointer) " + (rep.level == "L1" ? "are accessed" : "may be accessed");
        if (rep.level != "L1") {
            s += "; the bound comes from " + BoundPhrase(rep.bound);
        }
    } else if (rep.level == "L1") {
        s = "Definite out-of-bound " + w + " through a " + std::to_string(rep.depth) +
            "-level call chain: up to byte " + rep.endHi + " of " + name + " (" + size + ") is accessed";
    } else {
        s = "Possible out-of-bound " + w + " through a " + std::to_string(rep.depth) +
            "-level call chain: up to byte " + rep.endHi + " of " + name + " (" + size +
            ") may be accessed; the bound comes from " + BoundPhrase(rep.bound);
    }
    std::set<std::string> files;
    for (const std::string &hop : rep.chain) {
        files.insert(FileOf(Hop(hop).second));
    }
    if (files.size() > 1) {
        s += " [" + ChainText(rep.chain) + "]";
    }
    return s;
}

Alert MakeAlert(const std::string &fn, const summary::Summary &caller, const summary::Rep &rep)
{
    Alert a;
    a.bugType =
        std::string("Out-of-bound access via call chain (") + (rep.level == "L1" ? "definite" : "possible") + ")";
    a.level = rep.level;
    a.fn = fn;
    a.loc = rep.site;
    a.msg = AlertMessage(rep, caller.params);
    a.notes.push_back("Call chain: " + ChainText(rep.chain));
    std::string here = FileOf(rep.site);
    for (size_t i = 0; i < rep.chain.size(); ++i) {
        std::pair<std::string, std::string> h = Hop(rep.chain[i]);
        if (FileOf(h.second) != here) {
            continue;
        }
        if (i + 1 < rep.chain.size()) {
            a.notes.push_back(h.second + ": call to " + Hop(rep.chain[i + 1]).first);
        } else {
            a.notes.push_back(h.second + ": the " + (rep.kind == "W" ? std::string("write") : "read") +
                              " happens here");
        }
    }
    a.fp = rep.fp;
    return a;
}

llvm::json::Value AlertToJson(const Alert &a)
{
    llvm::json::Array notes;
    for (const std::string &n : a.notes) {
        notes.push_back(n);
    }
    return llvm::json::Object{
        {"checker", a.checker}, {"bug_type", a.bugType}, {"category", a.category},    {"level", a.level}, {"fn", a.fn},
        {"loc", a.loc},         {"msg", a.msg},          {"notes", std::move(notes)}, {"fp", a.fp}};
}

}  // namespace alert
}  // namespace oobsum
