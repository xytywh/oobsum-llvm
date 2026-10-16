//===-- Lowering.cpp - Clang AST/CFG to FIR ---------------------*- C++ -*-===//
//
// 见 Lowering.h。骨架见 SPEC-0007，指令见 SPEC-0008。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/lower/Lowering.h"

#include "OobSummary/core/Fingerprint.h"
#include "OobSummary/core/HeaderKeys.h"
#include "OobSummary/core/Key.h"
#include "OobSummary/lower/TypeLayout.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/Builtins.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <map>

using namespace clang;
using clang::ento::AnalysisManager;

namespace oobsum {

namespace {

// 文件表：主文件 0 号，其余按第一次用到的顺序；位置取宏展开位置（D-06）
class FileTable {
public:
    FileTable(const SourceManager &sm, fir::Module &module) : sm(sm), module(module)
    {
        Index(sm.getMainFileID());
    }

    fir::Loc LocOf(SourceLocation loc)
    {
        fir::Loc out;
        if (loc.isInvalid()) {
            return out;
        }
        SourceLocation expansion = sm.getExpansionLoc(loc);
        out.file = Index(sm.getFileID(expansion));
        out.line = sm.getExpansionLineNumber(expansion);
        out.col = sm.getExpansionColumnNumber(expansion);
        return out;
    }

private:
    uint32_t Index(FileID fid)
    {
        auto it = indices.find(fid);
        if (it != indices.end()) {
            return it->second;
        }
        const FileEntry *entry = sm.getFileEntryForID(fid);
        uint32_t index = static_cast<uint32_t>(module.files.size());
        module.files.push_back(entry != nullptr ? NormalizePath(entry->getName()) : std::string("<builtin>"));
        indices[fid] = index;
        return index;
    }

    const SourceManager &sm;
    fir::Module &module;
    llvm::DenseMap<FileID, uint32_t> indices;
};

// 函数体里被取过地址的变量（&x）：它们是内存对象（D-11 的 obj）
class AddressTaken : public RecursiveASTVisitor<AddressTaken> {
public:
    bool VisitUnaryOperator(UnaryOperator *op)
    {
        if (op->getOpcode() == UO_AddrOf) {
            if (const auto *ref = dyn_cast<DeclRefExpr>(op->getSubExpr()->IgnoreParens())) {
                if (const auto *vd = dyn_cast<VarDecl>(ref->getDecl())) {
                    vars.insert(vd->getCanonicalDecl());
                }
            }
        }
        return true;
    }

    llvm::SmallPtrSet<const VarDecl *, 8> vars;
};

// 函数体里的局部变量（不含形参、函数内 static、块作用域 extern），遍历顺序
class LocalCollector : public RecursiveASTVisitor<LocalCollector> {
public:
    bool VisitVarDecl(VarDecl *vd)
    {
        if (!isa<ParmVarDecl>(vd) && vd->hasLocalStorage()) {
            vars.push_back(vd);
        }
        return true;
    }

    std::vector<const VarDecl *> vars;
};

// 一个表达式降完的结果（SPEC-0008 §2.1）：右值（FIR 表达式）/ 标量局部的左值 / 内存左值（地址 + 字节数 + 是否下标访问）
struct Val {
    enum Kind { RValue, LocalLV, MemLV } kind = RValue;
    fir::ExprId expr = fir::kNone;  // RValue：值；MemLV：地址
    uint32_t local = fir::kNone;    // LocalLV
    int64_t size = 0;               // MemLV：字节数
    bool sub = false;               // MemLV：下标访问（A-07）
};

// 常见 libc 函数：__builtin_<名字> 映射成 F:<名字>（A-05）
const char *const kBuiltinLibc[] = {"memcpy", "memmove", "memset",   "memcmp", "memchr",  "strcpy",  "strncpy",
                                    "strcat", "strncat", "strlen",   "strcmp", "strncmp", "strchr",  "strrchr",
                                    "strstr", "sprintf", "snprintf", "malloc", "calloc",  "realloc", "free"};

class FunctionLowerer {
public:
    FunctionLowerer(AnalysisManager &mgr, KeyMaker &keys, TypeLayout &layout, FileTable &files)
        : mgr(mgr), ctx(mgr.getASTContext()), keys(keys), layout(layout), files(files)
    {
    }

    unsigned maxBlocks = 0;  // A-09（SPEC-0037）；0 = 不限

    fir::Function Lower(const FunctionDecl *fd)
    {
        fir::Function f;
        f.key = keys.ForFunction(fd);
        f.name = fd->getNameAsString();
        f.ext = fd->isExternallyVisible();
        f.loc = files.LocOf(fd->getLocation());
        f.ret = layout.Map(fd->getReturnType());
        fn = &f;
        memo.clear();
        localIds.clear();
        tmpCount = 0;
        callOrd = 0;
        LowerLocals(fd, f);
        LowerBody(fd, f);
        fn = nullptr;
        cur = nullptr;
        return f;
    }

private:
    // ---- 局部 ----

    bool IsObject(const VarDecl *vd, const AddressTaken &taken) const
    {
        QualType t = vd->getType().getCanonicalType();
        return t->isArrayType() || t->isRecordType() || taken.vars.count(vd->getCanonicalDecl()) != 0;
    }

    void AddLocal(const VarDecl *vd, int param, const AddressTaken &taken)
    {
        fir::Local local;
        local.id = static_cast<uint32_t>(fn->locals.size());
        local.name = vd->getNameAsString();
        local.param = param;
        local.type = layout.Map(vd->getType());
        local.obj = IsObject(vd, taken);
        localIds[vd->getCanonicalDecl()] = local.id;
        fn->locals.push_back(std::move(local));
    }

    void LowerLocals(const FunctionDecl *fd, fir::Function &f)
    {
        AddressTaken taken;
        LocalCollector collector;
        if (Stmt *body = fd->getBody()) {
            taken.TraverseStmt(body);
            collector.TraverseStmt(body);
        }
        for (unsigned i = 0; i < fd->getNumParams(); ++i) {
            AddLocal(fd->getParamDecl(i), static_cast<int>(i), taken);
        }
        for (const VarDecl *vd : collector.vars) {
            AddLocal(vd, -1, taken);
        }
    }

    uint32_t NewTemp(QualType type)
    {
        fir::Local local;
        local.id = static_cast<uint32_t>(fn->locals.size());
        local.name = "t" + std::to_string(tmpCount++);
        local.type = layout.Map(type);
        local.tmp = true;
        fn->locals.push_back(std::move(local));
        return fn->locals.back().id;
    }

    // ---- 块 ----

    // A-01：与 CSA 同一份 CFG；块按号从大到小（入口在前、出口 b0 在后）
    void LowerBody(const FunctionDecl *fd, fir::Function &f)
    {
        const CFG *cfg = mgr.getCFG(fd);
        bool tooLarge = cfg != nullptr && maxBlocks > 0 && cfg->getNumBlockIDs() > maxBlocks;
        if (cfg == nullptr || tooLarge) {
            // 建不出 CFG / 块数超限：不降级函数体，只放一个空块；core 按 skipped 报 SUSPECT（A-09，SPEC-0037）
            f.flags.push_back(tooLarge ? "cfg_too_large" : "no_cfg");
            fir::Block only;
            only.id = 0;
            f.blocks.push_back(std::move(only));
            f.entry = 0;
            f.exit = 0;
            return;
        }
        std::vector<const CFGBlock *> blocks(cfg->begin(), cfg->end());
        std::sort(blocks.begin(), blocks.end(),
                  [](const CFGBlock *a, const CFGBlock *b) { return a->getBlockID() > b->getBlockID(); });

        // SPEC-0009 §2.3：当值用的 ?: / && / ||（是语句元素）各分一个临时局部，汇合块是它所在的块
        std::vector<std::pair<const CFGBlock *, const Expr *>> merges;
        mergeTmps.clear();
        for (const CFGBlock *block : blocks) {
            for (const CFGElement &element : *block) {
                llvm::Optional<CFGStmt> stmt = element.getAs<CFGStmt>();
                const auto *e = stmt ? dyn_cast<Expr>(stmt->getStmt()) : nullptr;
                if (e != nullptr && IsValueBranch(e) && mergeTmps.count(e) == 0) {
                    mergeTmps[e] = NewTemp(e->getType());
                    merges.emplace_back(block, e);
                }
            }
        }

        llvm::DenseMap<unsigned, size_t> index;  // CFG 块号 → f.blocks 下标
        f.blocks.reserve(blocks.size());
        for (const CFGBlock *block : blocks) {
            index[block->getBlockID()] = f.blocks.size();
            f.blocks.emplace_back();
            cur = &f.blocks.back();
            cur->id = block->getBlockID();
            for (const CFGElement &element : *block) {
                if (llvm::Optional<CFGStmt> stmt = element.getAs<CFGStmt>()) {
                    LowerElement(stmt->getStmt());
                }
            }
        }

        // A-04：在汇合块的每个前驱块末尾补 set tmp
        for (const auto &merge : merges) {
            const CFGBlock *join = merge.first;
            const Expr *e = merge.second;
            bool logical = isa<BinaryOperator>(e);
            for (const CFGBlock::AdjacentBlock &pred : join->preds()) {
                const CFGBlock *p = pred.getReachableBlock();
                if (p == nullptr || index.count(p->getBlockID()) == 0) {
                    continue;
                }
                cur = &f.blocks[index[p->getBlockID()]];
                fir::ExprId value = fir::kNone;
                const auto *term = dyn_cast_or_null<BinaryOperator>(p->getTerminatorStmt());
                if (logical && term != nullptr && term->isLogicalOp()) {
                    // 短路的那一跳：走后继 0 时被测值为真
                    unsigned i = 0;
                    for (const CFGBlock::AdjacentBlock &succ : p->succs()) {
                        if (succ.getReachableBlock() == join) {
                            break;
                        }
                        ++i;
                    }
                    value = Const(i == 0 ? 1 : 0);
                } else if (const Expr *last = LastExpr(p)) {
                    value = logical ? AsCondition(RValueOf(last)) : RValueOf(last);
                } else {
                    value = Unknown("cond_value");
                }
                fir::Inst set;
                set.op = fir::InstOp::Set;
                set.dst = mergeTmps[e];
                set.val = value;
                Emit(std::move(set), e);
            }
        }

        for (const CFGBlock *block : blocks) {
            cur = &f.blocks[index[block->getBlockID()]];
            std::vector<fir::ExprId> assumes(block->succ_size(), fir::kNone);
            if (!SwitchEdges(block, assumes)) {
                BranchEdges(block, assumes);
            }
            unsigned i = 0;
            for (const CFGBlock::AdjacentBlock &succ : block->succs()) {
                // 不可达的后继不导出，但按原下标对应真假（SPEC-0009 §2.1）
                if (const CFGBlock *reachable = succ.getReachableBlock()) {
                    fir::Edge edge;
                    edge.to = reachable->getBlockID();
                    edge.assume = assumes[i];
                    cur->succ.push_back(edge);
                }
                ++i;
            }
        }
        f.entry = cfg->getEntry().getBlockID();
        f.exit = cfg->getExit().getBlockID();
    }

    static bool IsValueBranch(const Expr *e)
    {
        const auto *bo = dyn_cast<BinaryOperator>(e);
        return isa<ConditionalOperator>(e) || (bo != nullptr && bo->isLogicalOp());
    }

    // 块里最后一个语句元素（表达式）
    static const Expr *LastExpr(const CFGBlock *block)
    {
        for (auto it = block->rbegin(); it != block->rend(); ++it) {
            if (llvm::Optional<CFGStmt> stmt = it->getAs<CFGStmt>()) {
                const auto *e = dyn_cast<Expr>(stmt->getStmt());
                return e != nullptr ? e->IgnoreParens() : nullptr;
            }
        }
        return nullptr;
    }

    // 被测值 → 条件：比较与 !x 本身就是 0 / 1，其余写成 v != 0
    fir::ExprId AsCondition(fir::ExprId v)
    {
        fir::ExprOp op = fn->exprs[v].op;
        if (op == fir::ExprOp::Cmp || op == fir::ExprOp::LNot) {
            return v;
        }
        return Add(fir::Expr::Cmp(fir::CmpOp::Ne, v, Const(0), false));
    }

    // 条件取反：比较翻转运算符（有无符号不变），!x 的反是 x != 0，其余 !C
    fir::ExprId Negate(fir::ExprId c)
    {
        const fir::Expr e = fn->exprs[c];
        if (e.op == fir::ExprOp::Cmp) {
            static const fir::CmpOp flipped[] = {fir::CmpOp::Ne, fir::CmpOp::Eq, fir::CmpOp::Ge,
                                                 fir::CmpOp::Gt, fir::CmpOp::Le, fir::CmpOp::Lt};
            return Add(fir::Expr::Cmp(flipped[e.sub], e.a, e.b, e.isSigned));
        }
        if (e.op == fir::ExprOp::LNot) {
            return AsCondition(e.a);
        }
        return Add(fir::Expr::Unary(fir::ExprOp::LNot, c));
    }

    // SPEC-0009 §2.1：if / while / do / for / ?: / && / || —— 后继 0 是条件为真、后继 1 是条件为假
    void BranchEdges(const CFGBlock *block, std::vector<fir::ExprId> &assumes)
    {
        const Stmt *term = block->getTerminatorStmt();
        if (term == nullptr || block->succ_size() != 2) {
            return;
        }
        const auto *logical = dyn_cast<BinaryOperator>(term);
        bool branch = isa<IfStmt>(term) || isa<WhileStmt>(term) || isa<DoStmt>(term) || isa<ForStmt>(term) ||
                      isa<ConditionalOperator>(term) || (logical != nullptr && logical->isLogicalOp());
        if (!branch) {
            return;
        }
        const auto *cond = dyn_cast_or_null<Expr>(block->getTerminatorCondition(true));
        if (cond == nullptr) {
            return;  // for (;;)
        }
        // 条件本身是 && / || 时，这个块被测的是它最后一个语句元素（同 ExprEngine 的 ResolveCondition）
        const auto *bo = dyn_cast<BinaryOperator>(cond);
        if (bo != nullptr && bo->isLogicalOp()) {
            cond = LastExpr(block);
            if (cond == nullptr) {
                return;
            }
        }
        fir::ExprId value = RValueOf(cond);
        if (fn->exprs[value].op == fir::ExprOp::Const) {
            return;  // while (1)：恒真 / 恒假，Clang 已把不可走的后继标成不可达
        }
        fir::ExprId c = AsCondition(value);
        assumes[0] = c;
        assumes[1] = Negate(c);
    }

    // SPEC-0009 §2.2：case c → v == c；GNU 区间不带条件；default（或 switch 之后）→ 各 != 的按位与（≤ 16 个 case、没有区间时）
    bool SwitchEdges(const CFGBlock *block, std::vector<fir::ExprId> &assumes)
    {
        const auto *ss = dyn_cast_or_null<SwitchStmt>(block->getTerminatorStmt());
        if (ss == nullptr) {
            return false;
        }
        const Expr *cond = ss->getCond();
        bool isSigned = cond->getType()->isIntegerType() && Signed(cond->getType());
        fir::ExprId v = RValueOf(cond);
        bool exact = true;  // 没有区间、所有 case 值都放得进 int64
        std::vector<int64_t> values;
        llvm::DenseMap<const SwitchCase *, int64_t> caseValue;
        for (const SwitchCase *sc = ss->getSwitchCaseList(); sc != nullptr; sc = sc->getNextSwitchCase()) {
            const auto *cs = dyn_cast<CaseStmt>(sc);
            if (cs == nullptr) {
                continue;
            }
            llvm::APSInt c = cs->getLHS()->EvaluateKnownConstInt(ctx);
            bool fits = c.isSigned() ? c.getMinSignedBits() <= 63 : c.getActiveBits() <= 62;
            if (cs->getRHS() != nullptr || !fits) {
                exact = false;
                continue;
            }
            int64_t value = c.isSigned() ? c.getSExtValue() : static_cast<int64_t>(c.getZExtValue());
            values.push_back(value);
            caseValue[cs] = value;
        }
        std::sort(values.begin(), values.end());
        unsigned i = 0;
        for (const CFGBlock::AdjacentBlock &succ : block->succs()) {
            const CFGBlock *target = succ.getReachableBlock();
            const Stmt *label = target != nullptr ? target->getLabel() : nullptr;
            const auto *cs = dyn_cast_or_null<CaseStmt>(label);
            if (cs != nullptr) {
                auto it = caseValue.find(cs);
                if (it != caseValue.end()) {
                    assumes[i] = Add(fir::Expr::Cmp(fir::CmpOp::Eq, v, Const(it->second), isSigned));
                }
            } else if (exact && !values.empty() && values.size() <= 16) {
                fir::ExprId conj = fir::kNone;
                for (int64_t value : values) {
                    fir::ExprId ne = Add(fir::Expr::Cmp(fir::CmpOp::Ne, v, Const(value), isSigned));
                    conj = conj == fir::kNone ? ne : Add(fir::Expr::Bin(fir::BinOp::And, conj, ne));
                }
                assumes[i] = conj;
            }
            ++i;
        }
        return true;
    }

    void LowerElement(const Stmt *stmt)
    {
        if (const auto *e = dyn_cast<Expr>(stmt)) {
            ValueOf(e);
        } else if (const auto *ds = dyn_cast<DeclStmt>(stmt)) {
            for (const Decl *d : ds->decls()) {
                if (const auto *vd = dyn_cast<VarDecl>(d)) {
                    LowerDecl(vd);
                }
            }
        } else if (const auto *rs = dyn_cast<ReturnStmt>(stmt)) {
            LowerReturn(rs);
        } else if (const auto *as = dyn_cast<GCCAsmStmt>(stmt)) {
            for (const Expr *out : as->outputs()) {
                WriteUnknown(ValueOf(out), as, "inline_asm");
            }
        }
        // 其它语句（标号、空语句、控制语句本身）：它们的子表达式已是单独的元素（A-08）
    }

    // ---- 指令与表达式的小工具 ----

    fir::ExprId Add(fir::Expr e)
    {
        return fn->Add(std::move(e));
    }
    fir::ExprId Const(int64_t v)
    {
        // SPEC-0005 §2.3 #5：INT64_MIN / INT64_MAX 读回来分不清是否饱和过
        if (v == INT64_MAX || v == INT64_MIN) {
            return Unknown("const_overflow");
        }
        return Add(fir::Expr::Const(v));
    }
    fir::ExprId Unknown(llvm::StringRef why)
    {
        return Add(fir::Expr::Unknown(why));
    }
    fir::ExprId Local(uint32_t id)
    {
        return Add(fir::Expr::LocalValue(id));
    }
    Val R(fir::ExprId e) const
    {
        Val v;
        v.expr = e;
        return v;
    }
    Val Mem(fir::ExprId addr, int64_t size, bool sub = false) const
    {
        Val v;
        v.kind = Val::MemLV;
        v.expr = addr;
        v.size = size;
        v.sub = sub;
        return v;
    }
    void Emit(fir::Inst inst, const Stmt *at)
    {
        inst.loc = files.LocOf(at->getBeginLoc());
        cur->insts.push_back(std::move(inst));
    }

    // 字节数；不完整类型 0；VLA -1
    int64_t SizeOf(QualType t) const
    {
        QualType c = t.getCanonicalType();
        if (c->isIncompleteType() || c->isFunctionType()) {
            return 0;
        }
        if (c->isVariablyModifiedType() && c->isArrayType()) {
            return -1;
        }
        return ctx.getTypeSizeInChars(c).getQuantity();
    }
    unsigned Width(QualType t) const
    {
        return static_cast<unsigned>(ctx.getTypeSize(t.getCanonicalType()));
    }
    bool Signed(QualType t) const
    {
        return t.getCanonicalType()->isSignedIntegerOrEnumerationType();
    }

    // 地址偏移 i × 元素大小（数学整数）；scale 为负时是减；常数下标直接算出来
    fir::ExprId Scaled(fir::ExprId index, int64_t scale)
    {
        const fir::Expr &e = fn->exprs[index];
        int64_t product = 0;
        if (e.op == fir::ExprOp::Const && !llvm::MulOverflow(e.value, scale, product)) {
            return Const(product);
        }
        if (scale == 1) {
            return index;
        }
        return Add(fir::Expr::Bin(fir::BinOp::Mul, index, Const(scale)));
    }
    // padd(base, off)；off 是常数 0 时就是 base
    fir::ExprId PAdd(fir::ExprId base, fir::ExprId off)
    {
        const fir::Expr &e = fn->exprs[off];
        if (e.op == fir::ExprOp::Const && e.value == 0) {
            return base;
        }
        return Add(fir::Expr::PAdd(base, off));
    }
    fir::ExprId Offset(fir::ExprId base, int64_t off)
    {
        return off == 0 ? base : Add(fir::Expr::PAdd(base, Const(off)));
    }

    fir::ExprId AddrOf(const Val &v, const Expr *e)
    {
        switch (v.kind) {
            case Val::MemLV:
                return v.expr;
            case Val::LocalLV:
                return Add(fir::Expr::LAddr(v.local));
            case Val::RValue:
                return v.expr;  // 函数名等本身就是地址
        }
        return Unknown(e->getStmtClassName());
    }

    // 把值写成"未知"：标量局部 havoc，内存 store ?（安全的过近似：确实写了，值不知道）
    void WriteUnknown(const Val &target, const Stmt *at, llvm::StringRef why)
    {
        fir::Inst inst;
        if (target.kind == Val::LocalLV) {
            inst.op = fir::InstOp::Havoc;
            inst.dst = target.local;
            inst.why = why.str();
        } else if (target.kind == Val::MemLV) {
            inst.op = fir::InstOp::Store;
            inst.addr = target.expr;
            inst.size = target.size < 0 ? 0 : target.size;
            inst.sub = target.sub;
        } else {
            return;
        }
        Emit(std::move(inst), at);
    }

    // ---- 求值 ----

    Val ValueOf(const Expr *expr)
    {
        const Expr *e = expr->IgnoreParens();
        auto it = memo.find(e);
        if (it != memo.end()) {
            return it->second;
        }
        Val v = Compute(e);
        memo[e] = v;
        return v;
    }

    fir::ExprId RValueOf(const Expr *expr)
    {
        Val v = ValueOf(expr);
        switch (v.kind) {
            case Val::RValue:
                return v.expr;
            case Val::LocalLV:
                return Local(v.local);
            case Val::MemLV:
                break;
        }
        return Unknown("lvalue_as_rvalue");
    }

    // A-03：整数类型、无副作用、能求值的右值 → 常数
    bool Folds(const Expr *e, int64_t &out) const
    {
        if (!e->isPRValue() || !e->getType()->isIntegralOrEnumerationType() || e->HasSideEffects(ctx)) {
            return false;
        }
        Expr::EvalResult result;
        if (!e->EvaluateAsInt(result, ctx) || !result.Val.isInt()) {
            return false;
        }
        const llvm::APSInt &value = result.Val.getInt();
        if (value.getMinSignedBits() > 64) {
            out = INT64_MAX;  // Const() 会写成 unk(const_overflow)
            return true;
        }
        out = value.isSigned() ? value.getExtValue()
                               : (value.getActiveBits() > 63 ? INT64_MAX : static_cast<int64_t>(value.getZExtValue()));
        return true;
    }

    Val Compute(const Expr *e)
    {
        int64_t folded = 0;
        if (Folds(e, folded)) {
            return R(Const(folded));
        }
        if (const auto *ce = dyn_cast<ConstantExpr>(e)) {
            return ValueOf(ce->getSubExpr());
        }
        if (const auto *ref = dyn_cast<DeclRefExpr>(e)) {
            return DeclRef(ref);
        }
        if (const auto *cast = dyn_cast<CastExpr>(e)) {
            return Cast(cast);
        }
        if (const auto *uo = dyn_cast<UnaryOperator>(e)) {
            return Unary(uo);
        }
        if (const auto *bo = dyn_cast<BinaryOperator>(e)) {
            return Binary(bo);
        }
        if (const auto *as = dyn_cast<ArraySubscriptExpr>(e)) {
            int64_t elem = SizeOf(as->getType());
            fir::ExprId base = RValueOf(as->getBase());
            fir::ExprId index = RValueOf(as->getIdx());
            fir::ExprId off = elem < 0 ? Unknown("vla") : Scaled(index, elem);
            return Mem(PAdd(base, off), elem < 0 ? 0 : elem, true);
        }
        if (const auto *me = dyn_cast<MemberExpr>(e)) {
            return Member(me);
        }
        if (const auto *sl = dyn_cast<StringLiteral>(e)) {
            return String(sl);
        }
        if (const auto *pe = dyn_cast<PredefinedExpr>(e)) {
            if (const StringLiteral *name = pe->getFunctionName()) {
                return String(name);
            }
        }
        if (const auto *call = dyn_cast<CallExpr>(e)) {
            return Call(call);
        }
        if (const auto *choose = dyn_cast<ChooseExpr>(e)) {
            return ValueOf(choose->getChosenSubExpr());
        }
        if (const auto *generic = dyn_cast<GenericSelectionExpr>(e)) {
            if (!generic->isResultDependent()) {
                return ValueOf(generic->getResultExpr());
            }
        }
        if (isa<ConditionalOperator>(e) || isa<BinaryConditionalOperator>(e)) {
            auto it = mergeTmps.find(e);
            return R(it != mergeTmps.end() ? Local(it->second) : Unknown("cond_value"));  // A-04、SPEC-0009 §2.3
        }
        if (isa<ImplicitValueInitExpr>(e) && e->getType()->isScalarType()) {
            return R(Const(0));
        }
        if (e->isGLValue()) {
            return Mem(Unknown(e->getStmtClassName()), SizeOf(e->getType()));
        }
        return R(Unknown(e->getStmtClassName()));  // A-08
    }

    Val DeclRef(const DeclRefExpr *ref)
    {
        const ValueDecl *decl = ref->getDecl();
        if (const auto *vd = dyn_cast<VarDecl>(decl)) {
            auto it = localIds.find(vd->getCanonicalDecl());
            if (it != localIds.end()) {
                const fir::Local &local = fn->locals[it->second];
                if (!local.obj) {
                    Val v;
                    v.kind = Val::LocalLV;
                    v.local = local.id;
                    return v;
                }
                return Mem(Add(fir::Expr::LAddr(local.id)), SizeOf(vd->getType()));
            }
            std::string key = keys.ForVar(vd);
            return Mem(key.empty() ? Unknown("unknown_var") : Add(fir::Expr::GAddr(key)), SizeOf(vd->getType()));
        }
        if (const auto *fd = dyn_cast<FunctionDecl>(decl)) {
            return R(Add(fir::Expr::FAddr(keys.ForFunction(fd))));
        }
        return R(Unknown("decl_ref"));
    }

    Val Cast(const CastExpr *cast)
    {
        const Expr *op = cast->getSubExpr();
        QualType to = cast->getType();
        switch (cast->getCastKind()) {
            case CK_LValueToRValue: {
                Val v = ValueOf(op);
                if (v.kind == Val::LocalLV) {
                    return R(Local(v.local));
                }
                if (v.kind != Val::MemLV) {
                    return v;
                }
                // 结构体按值读也是一次 R 访问（整个结构体的字节数），照样生成 load；值本身不跟踪（SPEC-0008 §2.4）
                fir::Inst load;
                load.op = fir::InstOp::Load;
                load.dst = NewTemp(to);
                load.addr = v.expr;
                load.size = v.size < 0 ? 0 : v.size;
                load.sub = v.sub;
                uint32_t dst = load.dst;
                Emit(std::move(load), op);
                return to->isRecordType() ? R(Unknown("struct_by_value")) : R(Local(dst));
            }
            case CK_ArrayToPointerDecay:
                return R(AddrOf(ValueOf(op), op));
            case CK_FunctionToPointerDecay:
            case CK_BuiltinFnToFnPtr:
                return ValueOf(op);
            case CK_IntegralCast: {
                fir::ExprId v = RValueOf(op);
                QualType from = op->getType();
                if (Width(from) == Width(to) && Signed(from) == Signed(to)) {
                    return R(v);
                }
                return R(Add(fir::Expr::Cast(Width(to), Signed(to), v)));
            }
            case CK_IntegralToBoolean:
            case CK_PointerToBoolean:
                return R(Add(fir::Expr::Cmp(fir::CmpOp::Ne, RValueOf(op), Const(0), false)));
            case CK_NullToPointer:
                return R(Const(0));
            case CK_NoOp:
            case CK_BitCast:
            case CK_LValueBitCast:
            case CK_AtomicToNonAtomic:
            case CK_NonAtomicToAtomic:
                return ValueOf(op);  // 指针类型转换在 FIR 里消失（A-03）
            case CK_ToVoid:
                ValueOf(op);
                return R(Unknown("void"));
            case CK_PointerToIntegral:
                return R(Unknown("ptr_to_int"));
            case CK_IntegralToPointer:
                return R(Unknown("int_to_ptr"));
            case CK_IntegralToFloating:
            case CK_FloatingToIntegral:
            case CK_FloatingCast:
            case CK_FloatingToBoolean:
            case CK_FloatingRealToComplex:
            case CK_FloatingComplexToReal:
            case CK_FloatingComplexCast:
            case CK_IntegralRealToComplex:
            case CK_IntegralComplexToReal:
                return R(Unknown("float"));
            default:
                break;
        }
        return R(Unknown(std::string("cast:") + cast->getCastKindName()));
    }

    Val Unary(const UnaryOperator *uo)
    {
        const Expr *op = uo->getSubExpr();
        QualType t = uo->getType();
        switch (uo->getOpcode()) {
            case UO_AddrOf:
                return R(AddrOf(ValueOf(op), op));
            case UO_Deref: {
                fir::ExprId p = RValueOf(op);
                if (t->isFunctionType()) {
                    return R(p);  // (*fp)(…)
                }
                return Mem(p, SizeOf(t));
            }
            case UO_Plus:
            case UO_Extension:
                return ValueOf(op);
            case UO_Minus:
                if (!t->isIntegerType()) {
                    return R(Unknown("float"));
                }
                return R(Add(fir::Expr::Bin(fir::BinOp::Sub, Const(0), RValueOf(op), Width(t), Signed(t))));
            case UO_Not: {
                fir::ExprId inverted = Add(fir::Expr::Unary(fir::ExprOp::BNot, RValueOf(op)));
                return R(Add(fir::Expr::Cast(Width(t), Signed(t), inverted)));
            }
            case UO_LNot:
                return R(Add(fir::Expr::Unary(fir::ExprOp::LNot, RValueOf(op))));
            case UO_PreInc:
            case UO_PreDec:
            case UO_PostInc:
            case UO_PostDec:
                return IncDec(uo);
            default:
                break;
        }
        return R(Unknown(std::string("unary:") + UnaryOperator::getOpcodeStr(uo->getOpcode()).str()));
    }

    // 读左值的旧值：标量局部是 l<id>（引用"当前值"），内存左值生成 load（一次 R 访问）
    fir::ExprId ReadOld(const Val &target, QualType type, const Stmt *at)
    {
        if (target.kind == Val::LocalLV) {
            return Local(target.local);
        }
        if (target.kind != Val::MemLV) {
            return target.expr;
        }
        fir::Inst load;
        load.op = fir::InstOp::Load;
        load.dst = NewTemp(type);
        load.addr = target.expr;
        load.size = target.size < 0 ? 0 : target.size;
        load.sub = target.sub;
        uint32_t dst = load.dst;
        Emit(std::move(load), at);
        return Local(dst);
    }

    // 赋值类表达式的值：标量局部取赋值之后的 l<id>（右边那棵树可能引用了被改写的局部，不能再用），内存左值取写进去的值
    Val AssignedValue(const Val &target, fir::ExprId stored) const
    {
        Val v;
        if (target.kind == Val::LocalLV) {
            v.kind = Val::LocalLV;
            v.local = target.local;
            return v;
        }
        return R(stored);
    }

    // 整数转换（宽度或符号变了才 cast）
    fir::ExprId Convert(fir::ExprId v, QualType from, QualType to)
    {
        if (!from->isIntegerType() || !to->isIntegerType()) {
            return v;
        }
        if (Width(from) == Width(to) && Signed(from) == Signed(to)) {
            return v;
        }
        return Add(fir::Expr::Cast(Width(to), Signed(to), v));
    }

    // p ± n（n 是元素个数）
    fir::ExprId PointerStep(fir::ExprId p, QualType pointerType, fir::ExprId n, bool minus)
    {
        int64_t elem = SizeOf(pointerType->getPointeeType());
        if (elem <= 0) {
            return Add(fir::Expr::PAdd(p, Unknown("pointee_size")));
        }
        fir::ExprId off = Scaled(n, minus ? -elem : elem);
        if (elem == 1 && minus && fn->exprs[n].op != fir::ExprOp::Const) {
            off = Add(fir::Expr::Unary(fir::ExprOp::Neg, n));
        }
        return PAdd(p, off);
    }

    // SPEC-0008 §2.8：++x / x++ / --x / x--；后缀形的值是旧值
    Val IncDec(const UnaryOperator *uo)
    {
        const Expr *op = uo->getSubExpr();
        QualType t = op->getType();
        Val target = ValueOf(op);
        bool inc = uo->isIncrementOp();
        if (!t->isIntegerType() && !t->isPointerType()) {
            WriteUnknown(target, uo, "float");
            return R(Unknown("float"));
        }
        fir::ExprId old = ReadOld(target, t, uo);
        if (uo->isPostfix() && target.kind == Val::LocalLV) {
            // l<id> 引用的是当前值，写回之前先把旧值存进临时局部
            fir::Inst save;
            save.op = fir::InstOp::Set;
            save.dst = NewTemp(t);
            save.val = old;
            uint32_t saved = save.dst;
            Emit(std::move(save), uo);
            old = Local(saved);
        }
        fir::ExprId next;
        if (t->isPointerType()) {
            next = PointerStep(old, t, Const(1), !inc);
        } else if (t->isBooleanType()) {
            next = inc ? Const(1) : Add(fir::Expr::Unary(fir::ExprOp::LNot, old));
        } else {
            next = Add(fir::Expr::Bin(inc ? fir::BinOp::Add : fir::BinOp::Sub, old, Const(1), Width(t), Signed(t)));
        }
        Store(target, next, uo);
        return uo->isPostfix() ? R(old) : AssignedValue(target, next);
    }

    // SPEC-0008 §2.8：x op= e——读旧值，按计算类型算新值，转回左边的类型写回
    Val CompoundAssign(const CompoundAssignOperator *ca)
    {
        const Expr *lhs = ca->getLHS();
        QualType lt = lhs->getType();
        Val target = ValueOf(lhs);
        if (!lt->isIntegerType() && !lt->isPointerType()) {
            WriteUnknown(target, ca, "float");
            return R(Unknown("float"));
        }
        fir::ExprId old = ReadOld(target, lt, ca);
        fir::ExprId rhs = RValueOf(ca->getRHS());
        fir::ExprId next;
        BinaryOperatorKind opc = BinaryOperator::getOpForCompoundAssignment(ca->getOpcode());
        if (lt->isPointerType() && (opc == BO_Add || opc == BO_Sub)) {
            next = PointerStep(old, lt, rhs, opc == BO_Sub);
        } else {
            static const std::map<BinaryOperatorKind, fir::BinOp> arith = {
                {BO_Add, fir::BinOp::Add}, {BO_Sub, fir::BinOp::Sub}, {BO_Mul, fir::BinOp::Mul},
                {BO_Div, fir::BinOp::Div}, {BO_Rem, fir::BinOp::Rem}, {BO_Shl, fir::BinOp::Shl},
                {BO_Shr, fir::BinOp::Shr}, {BO_And, fir::BinOp::And}, {BO_Or, fir::BinOp::Or},
                {BO_Xor, fir::BinOp::Xor}};
            auto it = arith.find(opc);
            QualType computeLhs = ca->getComputationLHSType();
            QualType computeResult = ca->getComputationResultType();
            if (it == arith.end() || !computeResult->isIntegerType()) {
                next = Unknown("compound_assign");
            } else {
                fir::ExprId value = Add(fir::Expr::Bin(it->second, Convert(old, lt, computeLhs), rhs,
                                                       Width(computeResult), Signed(computeResult)));
                next = Convert(value, computeResult, lt);
            }
        }
        Store(target, next, ca);
        return AssignedValue(target, next);
    }

    // A-06：结构体整体赋值 / 用结构体初始化 → call __oobsum_copy(&dst, &src, sizeof)
    void StructCopy(fir::ExprId dst, const Expr *source, int64_t size, const Stmt *at)
    {
        const Expr *src = source->IgnoreParenImpCasts();
        Val from = ValueOf(src);
        fir::Inst call;
        call.op = fir::InstOp::Call;
        call.callee = "F:__oobsum_copy";
        call.args = {dst, from.kind == Val::MemLV ? from.expr : Unknown("struct_rvalue"), Const(size)};
        call.ord = callOrd++;
        Emit(std::move(call), at);
    }

    Val Binary(const BinaryOperator *bo)
    {
        const Expr *lhs = bo->getLHS();
        const Expr *rhs = bo->getRHS();
        BinaryOperatorKind opc = bo->getOpcode();
        if (opc == BO_Assign) {
            return Assign(bo);
        }
        if (const auto *ca = dyn_cast<CompoundAssignOperator>(bo)) {
            return CompoundAssign(ca);
        }
        if (opc == BO_Comma) {
            ValueOf(lhs);
            return ValueOf(rhs);
        }
        if (opc == BO_LAnd || opc == BO_LOr) {
            auto it = mergeTmps.find(bo);
            return R(it != mergeTmps.end() ? Local(it->second) : Unknown("cond_value"));  // A-04、SPEC-0009 §2.3
        }
        QualType lt = lhs->getType();
        QualType rt = rhs->getType();
        if (bo->isComparisonOp()) {
            bool isSigned = lt->isIntegerType() && Signed(lt);
            if (lt->isRealFloatingType() || rt->isRealFloatingType()) {
                return R(Unknown("float"));
            }
            static const std::map<BinaryOperatorKind, fir::CmpOp> cmp = {
                {BO_EQ, fir::CmpOp::Eq}, {BO_NE, fir::CmpOp::Ne}, {BO_LT, fir::CmpOp::Lt},
                {BO_LE, fir::CmpOp::Le}, {BO_GT, fir::CmpOp::Gt}, {BO_GE, fir::CmpOp::Ge}};
            auto it = cmp.find(opc);
            if (it == cmp.end()) {
                return R(Unknown("cmp"));
            }
            return R(Add(fir::Expr::Cmp(it->second, RValueOf(lhs), RValueOf(rhs), isSigned)));
        }
        // 指针算术：偏移按字节（下标乘元素大小在降级时完成，D-13）
        if ((opc == BO_Add || opc == BO_Sub) && (lt->isPointerType() || rt->isPointerType())) {
            if (lt->isPointerType() && rt->isPointerType()) {
                int64_t elem = SizeOf(lt->getPointeeType());
                return R(Add(fir::Expr::PSub(RValueOf(lhs), RValueOf(rhs), elem > 0 ? elem : 1)));
            }
            const Expr *ptr = lt->isPointerType() ? lhs : rhs;
            const Expr *idx = lt->isPointerType() ? rhs : lhs;
            int64_t elem = SizeOf(ptr->getType()->getPointeeType());
            fir::ExprId p = RValueOf(ptr);
            fir::ExprId i = RValueOf(idx);
            fir::ExprId off = elem <= 0 ? Unknown("pointee_size") : Scaled(i, opc == BO_Sub ? -elem : elem);
            if (elem == 1 && opc == BO_Sub && fn->exprs[i].op != fir::ExprOp::Const) {
                off = Add(fir::Expr::Unary(fir::ExprOp::Neg, i));
            }
            return R(PAdd(p, off));
        }
        QualType t = bo->getType();
        if (!t->isIntegerType()) {
            return R(Unknown("float"));
        }
        static const std::map<BinaryOperatorKind, fir::BinOp> arith = {
            {BO_Add, fir::BinOp::Add}, {BO_Sub, fir::BinOp::Sub}, {BO_Mul, fir::BinOp::Mul}, {BO_Div, fir::BinOp::Div},
            {BO_Rem, fir::BinOp::Rem}, {BO_Shl, fir::BinOp::Shl}, {BO_Shr, fir::BinOp::Shr}, {BO_And, fir::BinOp::And},
            {BO_Or, fir::BinOp::Or},   {BO_Xor, fir::BinOp::Xor}};
        auto it = arith.find(opc);
        if (it == arith.end()) {
            return R(Unknown(std::string("binary:") + bo->getOpcodeStr().str()));
        }
        // C 的整数运算：按结果类型回绕（SPEC-0005 §2.3 #1、SPEC-0008 §2.4）
        return R(Add(fir::Expr::Bin(it->second, RValueOf(lhs), RValueOf(rhs), Width(t), Signed(t))));
    }

    Val Assign(const BinaryOperator *bo)
    {
        Val target = ValueOf(bo->getLHS());
        if (bo->getType()->isRecordType()) {
            StructCopy(AddrOf(target, bo->getLHS()), bo->getRHS(), SizeOf(bo->getType()), bo);
            return R(Unknown("struct_by_value"));
        }
        fir::ExprId value = RValueOf(bo->getRHS());
        Store(target, value, bo);
        return AssignedValue(target, value);
    }

    void Store(const Val &target, fir::ExprId value, const Stmt *at)
    {
        fir::Inst inst;
        if (target.kind == Val::LocalLV) {
            inst.op = fir::InstOp::Set;
            inst.dst = target.local;
            inst.val = value;
        } else if (target.kind == Val::MemLV) {
            inst.op = fir::InstOp::Store;
            inst.addr = target.expr;
            inst.size = target.size < 0 ? 0 : target.size;
            inst.sub = target.sub;
            inst.val = value;
        } else {
            return;
        }
        Emit(std::move(inst), at);
    }

    Val Member(const MemberExpr *me)
    {
        const auto *field = dyn_cast<FieldDecl>(me->getMemberDecl());
        if (field == nullptr) {
            return Mem(Unknown("member"), SizeOf(me->getType()));
        }
        fir::ExprId base = me->isArrow() ? RValueOf(me->getBase()) : AddrOf(ValueOf(me->getBase()), me->getBase());
        const RecordDecl *parent = field->getParent();
        const ASTRecordLayout &rl = ctx.getASTRecordLayout(parent);
        uint64_t bits = rl.getFieldOffset(field->getFieldIndex());
        int64_t off = static_cast<int64_t>(bits / 8);
        QualType ft = field->getType().getCanonicalType();
        if (field->isBitField()) {
            // 字节数取位域实际跨的字节数（SPEC-0008 §3）
            int64_t span = static_cast<int64_t>((bits % 8 + field->getBitWidthValue(ctx) + 7) / 8);
            return Mem(Offset(base, off), span);
        }
        if (ft->isArrayType()) {
            const auto *ca = dyn_cast<ConstantArrayType>(ft.getTypePtr());
            const FieldDecl *lastField = nullptr;
            for (const FieldDecl *f : parent->fields()) {
                lastField = f;
            }
            bool last = lastField == field;
            bool flexible = ca == nullptr || ca->getSize() == 0 || (last && ca->getSize() == 1);
            if (flexible) {
                return Mem(Offset(base, off), 0);  // D-37：不按成员收窄
            }
            // D-31：数组成员以成员为界
            return Mem(Add(fir::Expr::Fld(base, off, SizeOf(ft), field->getName())), SizeOf(ft));
        }
        return Mem(Offset(base, off), SizeOf(ft));
    }

    Val String(const StringLiteral *sl)
    {
        if (sl->getCharByteWidth() != 1) {
            return Mem(Unknown("wide_string"), SizeOf(sl->getType()));
        }
        return Mem(Add(fir::Expr::Str(sl->getLength())), sl->getLength() + 1);
    }

    Val Call(const CallExpr *call)
    {
        const FunctionDecl *callee = call->getDirectCallee();
        fir::Inst inst;
        inst.op = fir::InstOp::Call;
        if (callee != nullptr) {
            llvm::StringRef name = callee->getName();
            if (callee->getBuiltinID() == Builtin::BI__builtin_expect && call->getNumArgs() >= 1) {
                return ValueOf(call->getArg(0));  // A-05：__builtin_expect(x, c) → x
            }
            inst.callee = keys.ForFunction(callee);
            if (name.consume_front("__builtin_") && llvm::is_contained(kBuiltinLibc, name)) {
                inst.callee = "F:" + name.str();
            }
        } else {
            inst.calleeExpr = RValueOf(call->getCallee());
        }
        for (const Expr *arg : call->arguments()) {
            inst.args.push_back(arg->getType()->isRecordType() ? Unknown("struct_by_value") : RValueOf(arg));
        }
        QualType ret = call->getType();
        bool hasValue = !ret->isVoidType() && !ret->isRecordType();
        if (hasValue) {
            inst.dst = NewTemp(ret);
        }
        inst.ord = callOrd++;
        uint32_t dst = inst.dst;
        Emit(std::move(inst), call);
        if (ret->isRecordType()) {
            return R(Unknown("struct_by_value"));
        }
        return hasValue ? R(Local(dst)) : R(Unknown("void"));
    }

    void LowerDecl(const VarDecl *vd)
    {
        auto it = localIds.find(vd->getCanonicalDecl());
        const Expr *init = vd->getInit();
        if (it == localIds.end() || init == nullptr) {
            return;  // 函数内 static / extern 不在这里；没有初始化的不生成指令
        }
        const fir::Local &local = fn->locals[it->second];
        Val target;
        if (local.obj) {
            target = Mem(Add(fir::Expr::LAddr(local.id)), SizeOf(vd->getType()));
        } else {
            target.kind = Val::LocalLV;
            target.local = local.id;
        }
        QualType t = vd->getType().getCanonicalType();
        if (t->isArrayType() || t->isRecordType()) {
            const Expr *bare = init->IgnoreParenImpCasts();
            if (t->isRecordType() && !isa<InitListExpr>(bare)) {
                StructCopy(target.expr, init, SizeOf(t), init);  // CTX_S st = *p;
                return;
            }
            unsigned budget = kMaxInitStores;
            InitAggregate(target.expr, t, init, 0, budget, init);
            return;
        }
        Store(target, RValueOf(init), init);
    }

    // SPEC-0008 §2.8：初始化列表逐个标量元素按偏移 store（每个对象至多 kMaxInitStores 个；
    // 隐式补零、字符串初始化、位域不降——值未知，对召回是安全的）
    static constexpr unsigned kMaxInitStores = 64;

    void InitAggregate(fir::ExprId base, QualType type, const Expr *init, int64_t off, unsigned &budget, const Stmt *at)
    {
        QualType t = type.getCanonicalType();
        const Expr *e = init->IgnoreParens();
        if (budget == 0 || isa<ImplicitValueInitExpr>(e) || isa<StringLiteral>(e)) {
            return;
        }
        if (const auto *list = dyn_cast<InitListExpr>(e)) {
            if (const auto *array = dyn_cast<ConstantArrayType>(t.getTypePtr())) {
                int64_t elem = SizeOf(array->getElementType());
                for (unsigned i = 0; i < list->getNumInits() && elem > 0; ++i) {
                    InitAggregate(base, array->getElementType(), list->getInit(i), off + i * elem, budget, at);
                }
                return;
            }
            if (const auto *rt = t->getAs<RecordType>()) {
                const RecordDecl *rd = rt->getDecl()->getDefinition();
                if (rd == nullptr) {
                    return;
                }
                const ASTRecordLayout &rl = ctx.getASTRecordLayout(rd);
                if (rd->isUnion()) {
                    const FieldDecl *field = list->getInitializedFieldInUnion();
                    if (field != nullptr && list->getNumInits() == 1 && !field->isBitField()) {
                        InitAggregate(base, field->getType(), list->getInit(0), off, budget, at);
                    }
                    return;
                }
                unsigned i = 0;
                for (const FieldDecl *field : rd->fields()) {
                    if (field->isUnnamedBitfield()) {
                        continue;
                    }
                    if (i >= list->getNumInits()) {
                        break;
                    }
                    const Expr *fieldInit = list->getInit(i++);
                    if (!field->isBitField()) {
                        int64_t fieldOff = static_cast<int64_t>(rl.getFieldOffset(field->getFieldIndex()) / 8);
                        InitAggregate(base, field->getType(), fieldInit, off + fieldOff, budget, at);
                    }
                }
                return;
            }
            if (list->getNumInits() == 1) {
                InitAggregate(base, t, list->getInit(0), off, budget, at);  // int x = {5};
            }
            return;
        }
        if (!t->isScalarType()) {
            return;  // 嵌套的结构体整体初始化等：值未知
        }
        --budget;
        Store(Mem(Offset(base, off), SizeOf(t)), RValueOf(e), at);
    }

    void LowerReturn(const ReturnStmt *rs)
    {
        fir::Inst inst;
        inst.op = fir::InstOp::Ret;
        if (const Expr *value = rs->getRetValue()) {
            inst.val = value->getType()->isRecordType() ? Unknown("struct_by_value") : RValueOf(value);
        }
        Emit(std::move(inst), rs);
    }

    AnalysisManager &mgr;
    const ASTContext &ctx;
    KeyMaker &keys;
    TypeLayout &layout;
    FileTable &files;
    fir::Function *fn = nullptr;
    fir::Block *cur = nullptr;
    llvm::DenseMap<const Expr *, Val> memo;
    llvm::DenseMap<const VarDecl *, uint32_t> localIds;
    llvm::DenseMap<const Expr *, uint32_t> mergeTmps;  // 当值用的 ?: / && / || → 临时局部（SPEC-0009 §2.3）
    unsigned tmpCount = 0;
    uint32_t callOrd = 0;
};

// TU 遍历：全局（文件作用域、块作用域 extern、函数内 static）与有函数体的函数，AST 遍历顺序
class ModuleCollector : public RecursiveASTVisitor<ModuleCollector> {
public:
    ModuleCollector(fir::Module &module, KeyMaker &keys, TypeLayout &layout, FileTable &files, FunctionLowerer &lowerer)
        : module(module), keys(keys), layout(layout), files(files), lowerer(lowerer)
    {
    }

    bool VisitVarDecl(VarDecl *vd)
    {
        if (isa<ParmVarDecl>(vd) || vd->isImplicit()) {
            return true;
        }
        std::string key = keys.ForVar(vd);
        if (key.empty() || !seen.insert(vd->getCanonicalDecl()).second) {
            return true;
        }
        const VarDecl *definition = VarDefinition(vd);
        const VarDecl *home = definition != nullptr ? definition : vd->getFirstDecl();
        fir::Global global;
        global.key = key;
        global.name = vd->getNameAsString();
        global.def = definition != nullptr;
        // 类型取最新一条声明：Sema 把合成类型放在它上面（extern int a[]; 之后的 int a[10]; → i32[10]）
        global.type = layout.Map(vd->getMostRecentDecl()->getType());
        global.loc = files.LocOf(home->getLocation());
        module.globals.push_back(std::move(global));
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl *fd)
    {
        if (!fd->isImplicit() && fd->doesThisDeclarationHaveABody()) {
            module.funcs.push_back(lowerer.Lower(fd));
        }
        return true;
    }

private:
    fir::Module &module;
    KeyMaker &keys;
    TypeLayout &layout;
    FileTable &files;
    FunctionLowerer &lowerer;
    llvm::SmallPtrSet<const VarDecl *, 32> seen;
};

}  // namespace

fir::Module LowerTranslationUnit(const TranslationUnitDecl *tu, AnalysisManager &mgr, KeyMaker &keys,
                                 llvm::StringRef mainFile, unsigned maxBlocks)
{
    const ASTContext &ctx = mgr.getASTContext();
    const TargetInfo &target = ctx.getTargetInfo();
    fir::Module module;
    module.tu = TuId(mainFile);
    module.file = NormalizePath(mainFile);
    module.target.triple = target.getTriple().str();
    module.target.ptr = static_cast<unsigned>(target.getPointerWidth(0) / 8);
    module.target.longSize = static_cast<unsigned>(target.getLongWidth() / 8);
    module.target.bigEndian = target.isBigEndian();
    FileTable files(mgr.getSourceManager(), module);
    TypeLayout layout(ctx, module);
    FunctionLowerer lowerer(mgr, keys, layout, files);
    lowerer.maxBlocks = maxBlocks;
    ModuleCollector collector(module, keys, layout, files, lowerer);
    collector.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
    fir::FinalizeHeaderKeys(module);  // 头文件函数的键加 #<FIR 哈希>（D-02，SPEC-0059）
    return module;
}

}  // namespace oobsum
