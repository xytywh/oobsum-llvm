//===-- HeaderKeys.cpp - Hash suffix of header-defined functions -*- C++ -*-===//
//
// 见 HeaderKeys.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/HeaderKeys.h"

#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/FirText.h"

#include "llvm/Support/Path.h"

#include <algorithm>
#include <functional>
#include <set>
#include <vector>

namespace oobsum {
namespace fir {

namespace {

// 类型按结构写：记录写"名字 / 大小"（不写本 TU 里的记录号），用到的记录记进 recs
std::string CanonType(const Module &m, TypeId t, std::set<uint32_t> &recs)
{
    if (t >= m.types.size()) {
        return "?";
    }
    const Type &x = m.types[t];
    switch (x.kind) {
        case TypeKind::Ptr:
            return CanonType(m, x.to, recs) + "*";
        case TypeKind::Arr:
            return CanonType(m, x.to, recs) + "[" + (x.count < 0 ? "" : std::to_string(x.count)) + "]";
        case TypeKind::Rec: {
            recs.insert(x.record);
            const Record *r = m.FindRecord(x.record);
            return r == nullptr ? "rec(?)" : "rec(" + r->name + "/" + std::to_string(r->size) + ")";
        }
        default:
            return TypeText(m, t);
    }
}

// 位置写文件名 + 行列（文件表的下标各 TU 不同）
std::string CanonLoc(const Module &m, const Loc &loc)
{
    std::string file = loc.file < m.files.size()
                           ? llvm::sys::path::filename(m.files[loc.file], llvm::sys::path::Style::posix).str()
                           : "?";
    return "@" + file + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.col);
}

void Rename(Module &m, const std::string &from, const std::string &to)
{
    std::string staticOld = "S@" + from + ":";
    std::string staticNew = "S@" + to + ":";
    auto renameStatic = [&](std::string &key) {
        if (llvm::StringRef(key).startswith(staticOld)) {
            key = staticNew + key.substr(staticOld.size());
        }
    };
    for (Function &f : m.funcs) {
        if (f.key == from) {
            f.key = to;
        }
        for (Block &b : f.blocks) {
            for (Inst &inst : b.insts) {
                if (inst.op == InstOp::Call && inst.callee == from) {
                    inst.callee = to;
                }
            }
        }
        for (Expr &e : f.exprs) {
            if (e.op == ExprOp::FAddr && e.text == from) {
                e.text = to;
            } else if (e.op == ExprOp::GAddr) {
                renameStatic(e.text);
            }
        }
    }
    for (Global &g : m.globals) {
        renameStatic(g.key);
    }
}

}  // namespace

bool IsHeaderFunctionKey(const Module &m, llvm::StringRef key)
{
    if (!key.consume_front("F@") || key.contains('#')) {
        return false;
    }
    return key.split(':').first != m.tu;
}

std::string CanonicalText(const Module &m, const Function &f)
{
    std::set<uint32_t> recs;
    std::vector<std::string> lines;
    std::string flags;
    for (const std::string &flag : f.flags) {
        flags += " " + flag;
    }
    lines.push_back("fn " + f.key + (f.ext ? " ext" : "") + " -> " + CanonType(m, f.ret, recs) + " entry b" +
                    std::to_string(f.entry) + " exit b" + std::to_string(f.exit) + flags);
    for (const Local &l : f.locals) {
        lines.push_back("local l" + std::to_string(l.id) + " " + l.name +
                        (l.param >= 0 ? " param" + std::to_string(l.param) : "") + (l.obj ? " obj" : "") +
                        (l.tmp ? " tmp" : "") + ": " + CanonType(m, l.type, recs));
    }
    for (const Block &b : f.blocks) {
        lines.push_back("b" + std::to_string(b.id) + ":");
        for (const Inst &inst : b.insts) {
            lines.push_back("  " + InstText(f, inst) + " " + CanonLoc(m, inst.loc));
        }
        for (const Edge &e : b.succ) {
            lines.push_back("  -> b" + std::to_string(e.to) +
                            (e.assume == kNone ? "" : " if " + ExprText(f, e.assume)));
        }
    }
    // 引用到的全局：键与类型（同名头文件 static 全局的大小不同时，函数哈希也不同）
    std::set<std::string> extra;
    for (const Expr &e : f.exprs) {
        if (e.op != ExprOp::GAddr) {
            continue;
        }
        for (const Global &g : m.globals) {
            if (g.key == e.text) {
                extra.insert("global " + g.key + ": " + CanonType(m, g.type, recs));
                break;
            }
        }
    }
    // 用到的记录（递归展开字段），按文本排序
    std::set<uint32_t> done;
    while (true) {
        auto next = std::find_if(recs.begin(), recs.end(), [&](uint32_t r) { return done.count(r) == 0; });
        if (next == recs.end()) {
            break;
        }
        uint32_t id = *next;
        done.insert(id);
        const Record *r = m.FindRecord(id);
        if (r == nullptr) {
            continue;
        }
        std::string fields;
        for (const Field &field : r->fields) {
            fields += " " + field.name + "@" + std::to_string(field.off) + ": " + CanonType(m, field.type, recs) + ";";
        }
        extra.insert("record " + r->name + "/" + std::to_string(r->size) + " {" + fields + " }");
    }
    lines.insert(lines.end(), extra.begin(), extra.end());
    std::string out;
    for (const std::string &line : lines) {
        out += line + "\n";
    }
    return out;
}

std::map<std::string, std::string> FinalizeHeaderKeys(Module &m)
{
    std::map<std::string, std::string> renamed;
    std::map<std::string, size_t> index;  // 头文件函数的基础键 → 下标
    for (size_t i = 0; i < m.funcs.size(); ++i) {
        if (IsHeaderFunctionKey(m, m.funcs[i].key)) {
            index.emplace(m.funcs[i].key, i);
        }
    }
    if (index.empty()) {
        return renamed;
    }
    // 头文件函数之间的引用（直接调用、函数地址）
    std::map<size_t, std::vector<size_t>> succ;
    for (const auto &entry : index) {
        const Function &f = m.funcs[entry.second];
        std::vector<size_t> &out = succ[entry.second];
        for (const Block &b : f.blocks) {
            for (const Inst &inst : b.insts) {
                auto it = inst.op == InstOp::Call && inst.calleeExpr == kNone ? index.find(inst.callee) : index.end();
                if (it != index.end()) {
                    out.push_back(it->second);
                }
            }
        }
        for (const Expr &e : f.exprs) {
            auto it = e.op == ExprOp::FAddr ? index.find(e.text) : index.end();
            if (it != index.end()) {
                out.push_back(it->second);
            }
        }
    }
    // Tarjan：强连通分量按被调者先的顺序出来
    std::map<size_t, int> order;
    std::map<size_t, int> low;
    std::set<size_t> onStack;
    std::vector<size_t> stack;
    std::vector<std::vector<size_t>> sccs;
    int counter = 0;
    std::function<void(size_t)> visit = [&](size_t v) {
        order[v] = low[v] = counter++;
        stack.push_back(v);
        onStack.insert(v);
        for (size_t w : succ[v]) {
            if (order.count(w) == 0) {
                visit(w);
                low[v] = std::min(low[v], low[w]);
            } else if (onStack.count(w) != 0) {
                low[v] = std::min(low[v], order[w]);
            }
        }
        if (low[v] == order[v]) {
            std::vector<size_t> scc;
            size_t w = 0;
            do {
                w = stack.back();
                stack.pop_back();
                onStack.erase(w);
                scc.push_back(w);
            } while (w != v);
            sccs.push_back(std::move(scc));
        }
    };
    for (const auto &entry : index) {
        if (order.count(entry.second) == 0) {
            visit(entry.second);
        }
    }
    // 一个分量里先都算完再改名：分量内的互相引用写基础键（与访问顺序无关）；分量外的被调者已经是最终键（Merkle）
    for (const std::vector<size_t> &scc : sccs) {
        std::vector<std::pair<std::string, std::string>> batch;
        for (size_t v : scc) {
            const Function &f = m.funcs[v];
            batch.emplace_back(f.key, f.key + "#" + HashHex(CanonicalText(m, f)).substr(0, 8));
        }
        for (const auto &p : batch) {
            Rename(m, p.first, p.second);
            renamed[p.first] = p.second;
        }
    }
    return renamed;
}

}  // namespace fir
}  // namespace oobsum
