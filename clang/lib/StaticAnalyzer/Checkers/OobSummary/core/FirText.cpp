//===-- FirText.cpp - Text notation of FIR ----------------------*- C++ -*-===//
//
// 见 FirText.h。写法见 SPEC-0006 §2。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/FirText.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"

namespace oobsum {
namespace fir {

namespace {

const char *const kBinSymbols[] = {"+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^"};
const char *const kCmpSymbols[] = {"==", "!=", "<", "<=", ">", ">="};

std::string IntName(unsigned width, bool isSigned)
{
    return (isSigned ? "i" : "u") + std::to_string(width);
}

// bin（含回绕的）与 cmp 作操作数时加括号，免得 (i16)(a - b):u32 这种写法有歧义（SPEC-0006 §2.3）
std::string Operand(const Function &f, ExprId id)
{
    bool wrap = id < f.exprs.size() && (f.exprs[id].op == ExprOp::Bin || f.exprs[id].op == ExprOp::Cmp);
    return wrap ? "(" + ExprText(f, id) + ")" : ExprText(f, id);
}

std::string FileName(const Module &m, uint32_t file)
{
    if (file >= m.files.size()) {
        return "?";
    }
    return llvm::sys::path::filename(m.files[file], llvm::sys::path::Style::posix).str();
}

// 模块级：总带文件名；函数里：主文件只写行列
std::string LocText(const Module &m, const Loc &loc, bool inFunction)
{
    std::string lineCol = std::to_string(loc.line) + ":" + std::to_string(loc.col);
    bool mainFile = loc.file < m.files.size() && m.files[loc.file] == m.file;
    return "@" + (inFunction && mainFile ? lineCol : FileName(m, loc.file) + ":" + lineCol);
}

}  // namespace

std::string TypeText(const Module &m, TypeId type)
{
    if (type >= m.types.size()) {
        return "?";
    }
    const Type &t = m.types[type];
    switch (t.kind) {
        case TypeKind::Int:
            return IntName(t.width, t.isSigned);
        case TypeKind::Ptr:
            return TypeText(m, t.to) + "*";
        case TypeKind::Arr:
            return TypeText(m, t.to) + "[" + (t.count < 0 ? "" : std::to_string(t.count)) + "]";
        case TypeKind::Rec:
            return "rec#" + std::to_string(t.record);
        case TypeKind::Void:
            return "void";
        case TypeKind::Other:
            return "other" + std::to_string(t.size);
    }
    return "?";
}

std::string ExprText(const Function &f, ExprId expr)
{
    if (expr >= f.exprs.size()) {
        return "?";
    }
    const Expr &e = f.exprs[expr];
    switch (e.op) {
        case ExprOp::Const:
            return std::to_string(e.value);
        case ExprOp::Local:
            return "l" + std::to_string(e.value);
        case ExprOp::Bin: {
            std::string body = Operand(f, e.a) + " " + kBinSymbols[e.sub] + " " + Operand(f, e.b);
            return e.width == 0 ? body : "(" + body + "):" + IntName(e.width, e.isSigned);
        }
        case ExprOp::Cmp:
            return Operand(f, e.a) + " " + kCmpSymbols[e.sub] + (e.isSigned ? "s" : "u") + " " + Operand(f, e.b);
        case ExprOp::LNot:
            return "!" + Operand(f, e.a);
        case ExprOp::Neg:
            return "-" + Operand(f, e.a);
        case ExprOp::BNot:
            return "~" + Operand(f, e.a);
        case ExprOp::Cast:
            return "(" + IntName(e.width, e.isSigned) + ")" + Operand(f, e.a);
        case ExprOp::LAddr:
            return "&l" + std::to_string(e.value);
        case ExprOp::GAddr:
        case ExprOp::FAddr:
            return "&" + e.text;
        case ExprOp::Str:
            return "str" + std::to_string(e.value);
        case ExprOp::PAdd:
            return "padd(" + ExprText(f, e.a) + ", " + ExprText(f, e.b) + ")";
        case ExprOp::Fld:
            return "fld(" + ExprText(f, e.a) + ", " + e.text + "@" + std::to_string(e.value) + ":" +
                   std::to_string(e.size) + ")";
        case ExprOp::PSub:
            return "psub(" + ExprText(f, e.a) + ", " + ExprText(f, e.b) + ", " + std::to_string(e.value) + ")";
        case ExprOp::Unk:
            return "unk(" + e.text + ")";
        case ExprOp::SubObj:
            return "subobj(" + ExprText(f, e.a) + ", " + std::to_string(e.size) + ")";
    }
    return "?";
}

std::string InstText(const Function &f, const Inst &inst)
{
    std::string dst = inst.dst == kNone ? "" : "l" + std::to_string(inst.dst) + " = ";
    std::string sub = inst.sub ? " sub" : "";
    switch (inst.op) {
        case InstOp::Set:
            return dst + ExprText(f, inst.val);
        case InstOp::Load:
            return dst + "load" + std::to_string(inst.size) + " [" + ExprText(f, inst.addr) + "]" + sub;
        case InstOp::Store:
            return "store" + std::to_string(inst.size) + " [" + ExprText(f, inst.addr) +
                   "] = " + (inst.val == kNone ? "?" : ExprText(f, inst.val)) + sub;
        case InstOp::Call: {
            std::string callee = inst.calleeExpr == kNone ? inst.callee : "*(" + ExprText(f, inst.calleeExpr) + ")";
            std::string args;
            for (ExprId arg : inst.args) {
                args += (args.empty() ? "" : ", ") + ExprText(f, arg);
            }
            return dst + "call " + callee + "(" + args + ") #" + std::to_string(inst.ord);
        }
        case InstOp::Havoc:
            return dst + "havoc(" + inst.why + ")";
        case InstOp::Ret:
            return inst.val == kNone ? "ret" : "ret " + ExprText(f, inst.val);
    }
    return "?";
}

std::vector<std::string> ModuleHeaderText(const Module &m)
{
    std::vector<std::string> out;
    out.push_back("module tu=" + m.tu + " file=" + m.file + " target=" + m.target.triple +
                  " ptr=" + std::to_string(m.target.ptr) + " long=" + std::to_string(m.target.longSize) +
                  (m.target.bigEndian ? " big-endian" : " little-endian"));
    for (const Record &r : m.records) {
        std::string fields;
        for (const Field &field : r.fields) {
            fields += (fields.empty() ? " " : "; ") + field.name + "@" + std::to_string(field.off) + ": " +
                      TypeText(m, field.type);
        }
        out.push_back("record #" + std::to_string(r.id) + " " + r.name + " size=" + std::to_string(r.size) + " {" +
                      fields + " }");
    }
    for (const Global &g : m.globals) {
        out.push_back("global " + g.key + ": " + TypeText(m, g.type) + (g.def ? " def " : " decl ") +
                      LocText(m, g.loc, false));
    }
    return out;
}

std::vector<std::string> FunctionText(const Module &m, const Function &f)
{
    std::vector<std::string> out;
    std::string flags;
    for (const std::string &flag : f.flags) {
        flags += (flags.empty() ? " [" : " ") + flag;
    }
    out.push_back("fn " + f.key + (f.ext ? " ext" : "") + " -> " + TypeText(m, f.ret) + " " + LocText(m, f.loc, false) +
                  (flags.empty() ? "" : flags + "]"));
    for (const Local &l : f.locals) {
        out.push_back("  local l" + std::to_string(l.id) + " " + l.name +
                      (l.param >= 0 ? " param" + std::to_string(l.param) : "") + (l.obj ? " obj" : "") +
                      (l.tmp ? " tmp" : "") + ": " + TypeText(m, l.type));
    }
    for (const Block &b : f.blocks) {
        out.push_back("  b" + std::to_string(b.id) + (b.id == f.entry ? " entry" : "") +
                      (b.id == f.exit ? " exit" : "") + ":");
        for (const Inst &inst : b.insts) {
            out.push_back("    " + InstText(f, inst) + "  " + LocText(m, inst.loc, true));
        }
        for (const Edge &e : b.succ) {
            std::string cond = e.assume == kNone ? "" : " if " + ExprText(f, e.assume);
            out.push_back("    -> b" + std::to_string(e.to) + cond);
        }
    }
    return out;
}

}  // namespace fir
}  // namespace oobsum
