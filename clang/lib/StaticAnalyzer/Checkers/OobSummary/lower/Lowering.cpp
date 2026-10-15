//===-- Lowering.cpp - Clang AST/CFG to FIR ---------------------*- C++ -*-===//
//
// 见 Lowering.h。本步（SPEC-0007）只有骨架：函数带 flags: ["body_pending"]（临时，S4c 去掉）。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/lower/Lowering.h"

#include "OobSummary/core/Fingerprint.h"
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
const char *const kBuiltinLibc[] = {"memcpy", "memmove", "memset", "memcmp", "memchr",  "strcpy",  "strncpy",
                                    "strcat", "strncat", "strlen", "strcmp", "strncmp", "strchr",  "strrchr",
                                    "strstr", "sprintf", "snprintf", "malloc", "calloc", "realloc", "free"};

class FunctionLowerer {
public:
    FunctionLowerer(AnalysisManager &mgr, KeyMaker &keys, TypeLayout &layout, FileTable &files)
        : mgr(mgr), ctx(mgr.getASTContext()), keys(keys), layout(layout), files(files)
    {
    }

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

    // A-01：与 CSA 同一份 CFG；块按号从大到小（入口在前、出口 b0 在后）；边先不带 assume（SPEC-0007 §2.4）
    void LowerBody(const FunctionDecl *fd, fir::Function &f)
    {
        const CFG *cfg = mgr.getCFG(fd);
        if (cfg == nullptr) {
            f.flags.push_back("no_cfg");
            fir::Block only;
            only.id = 0;
            f.blocks.push_back(std::move(only));
            f.entry = 0;
            f.exit = 0;
            return;
        }
        f.flags.push_back("body_pending");  // SPEC-0008：S4c-2 做完副作用构造后去掉
        std::vector<const CFGBlock *> blocks(cfg->begin(), cfg->end());
        std::sort(blocks.begin(), blocks.end(),
                  [](const CFGBlock *a, const CFGBlock *b) { return a->getBlockID() > b->getBlockID(); });
        f.blocks.reserve(blocks.size());
        for (const CFGBlock *block : blocks) {
            f.blocks.emplace_back();
            cur = &f.blocks.back();
            cur->id = block->getBlockID();
            for (const CFGElement &element : *block) {
                if (llvm::Optional<CFGStmt> stmt = element.getAs<CFGStmt>()) {
                    LowerElement(stmt->getStmt());
                }
            }
            for (const CFGBlock::AdjacentBlock &succ : block->succs()) {
                if (const CFGBlock *reachable = succ.getReachableBlock()) {
                    fir::Edge edge;
                    edge.to = reachable->getBlockID();
                    cur->succ.push_back(edge);
                }
            }
        }
        f.entry = cfg->getEntry().getBlockID();
        f.exit = cfg->getExit().getBlockID();
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
            return R(Unknown("cond_value"));  // A-04：S5
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
            case UO_Not:
            {
                fir::ExprId inverted = Add(fir::Expr::Unary(fir::ExprOp::BNot, RValueOf(op)));
                return R(Add(fir::Expr::Cast(Width(t), Signed(t), inverted)));
            }
            case UO_LNot:
                return R(Add(fir::Expr::Unary(fir::ExprOp::LNot, RValueOf(op))));
            case UO_PreInc:
            case UO_PreDec:
            case UO_PostInc:
            case UO_PostDec:
                // S4c-2 细化；先写成"写入未知值"（安全的过近似）
                WriteUnknown(ValueOf(op), uo, "pending:inc_dec");
                return R(Unknown("pending:inc_dec"));
            default:
                break;
        }
        return R(Unknown(std::string("unary:") + UnaryOperator::getOpcodeStr(uo->getOpcode()).str()));
    }

    Val Binary(const BinaryOperator *bo)
    {
        const Expr *lhs = bo->getLHS();
        const Expr *rhs = bo->getRHS();
        BinaryOperatorKind opc = bo->getOpcode();
        if (opc == BO_Assign) {
            return Assign(bo);
        }
        if (bo->isCompoundAssignmentOp()) {
            WriteUnknown(ValueOf(lhs), bo, "pending:compound_assign");  // S4c-2
            return R(Unknown("pending:compound_assign"));
        }
        if (opc == BO_Comma) {
            ValueOf(lhs);
            return ValueOf(rhs);
        }
        if (opc == BO_LAnd || opc == BO_LOr) {
            return R(Unknown("cond_value"));  // A-04：S5
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
            WriteUnknown(target, bo, "pending:struct_copy");  // S4c-2：__oobsum_copy
            return R(Unknown("struct_by_value"));
        }
        fir::ExprId value = RValueOf(bo->getRHS());
        Store(target, value, bo);
        return R(value);
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
            WriteUnknown(target, vd->getInit(), "pending:aggregate_init");  // S4c-2：初始化列表逐元素
            return;
        }
        Store(target, RValueOf(init), init);
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
                                 llvm::StringRef mainFile)
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
    ModuleCollector collector(module, keys, layout, files, lowerer);
    collector.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
    return module;
}

}  // namespace oobsum
