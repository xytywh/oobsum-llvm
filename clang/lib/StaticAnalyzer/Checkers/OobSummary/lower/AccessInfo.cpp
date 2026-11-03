//===-- AccessInfo.cpp - Bounded AST access descriptions --------*- C++ -*-===//
//
// SPEC-0125 / N-10：不取源码文本，不展开模板参数；所有信息只供诊断。
//
//===----------------------------------------------------------------------===//
#include "OobSummary/lower/AccessInfo.h"

#include "OobSummary/lower/Keys.h"
#include "OobSummary/lower/TypeLayout.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/Type.h"

#include <map>
#include <set>

using namespace clang;

namespace oobsum {

namespace {
class Assignments : public RecursiveASTVisitor<Assignments> {
public:
    std::map<const VarDecl *, std::vector<const Expr *>> values;
    bool VisitVarDecl(VarDecl *decl)
    {
        if (decl->getType()->isPointerType() && decl->hasInit()) {
            values[decl->getCanonicalDecl()].push_back(decl->getInit());
        }
        return true;
    }
    bool VisitBinaryOperator(BinaryOperator *expr)
    {
        const auto *ref = dyn_cast<DeclRefExpr>(expr->getLHS()->IgnoreParenCasts());
        const auto *decl = ref != nullptr ? dyn_cast<VarDecl>(ref->getDecl()) : nullptr;
        if (expr->getOpcode() == BO_Assign && decl != nullptr && decl->getType()->isPointerType()) {
            values[decl->getCanonicalDecl()].push_back(expr->getRHS());
        }
        return true;
    }
};
}  // namespace

struct AccessDescriber::Impl {
    const ASTContext &context;
    Assignments assignments;
    const TypeLayout &layout;

    Impl(const ASTContext &context, const FunctionDecl *function, const TypeLayout &layout)
        : context(context), layout(layout)
    {
        assignments.TraverseStmt(const_cast<Stmt *>(function->getBody()));
    }

    int64_t Size(QualType type) const
    {
        if (type.isNull() || type->isIncompleteType() || type->isDependentType() || type->isFunctionType() ||
            type->isPlaceholderType() || type->isUndeducedType() || type->isVariablyModifiedType()) {
            return 0;
        }
        return context.getTypeSizeInChars(type).getQuantity();
    }

    std::string Limit(std::string text) const
    {
        if (text.size() > 512) {
            std::string::size_type end = 509;
            while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) {
                --end;
            }
            text.resize(end);
            text += "...";
        }
        return text;
    }

    std::string TypeName(QualType type, unsigned depth = 0) const
    {
        return Limit(TypeNameImpl(type, depth));
    }

    std::string TypeNameImpl(QualType type, unsigned depth) const
    {
        if (type.isNull() || depth > 4) {
            return "an object";
        }
        if (const auto *alias = dyn_cast<TypedefType>(type.getTypePtr())) {
            return alias->getDecl()->getNameAsString();
        }
        if (const auto *record = type->getAs<RecordType>()) {
            return layout.RecordName(record->getDecl());
        }
        if (const auto *builtin = type->getAs<BuiltinType>()) {
            return builtin->getName(context.getPrintingPolicy()).str();
        }
        if (type->isPointerType() || type->isReferenceType()) {
            return TypeName(type->getPointeeType(), depth + 1) + " *";
        }
        if (const auto *array = context.getAsArrayType(type)) {
            return TypeName(array->getElementType(), depth + 1) + "[]";
        }
        if (const auto *enumeration = type->getAs<EnumType>()) {
            return enumeration->getDecl()->getNameAsString();
        }
        return "an object";
    }

    std::string Expression(const Expr *expr, unsigned depth = 0) const
    {
        return Limit(ExpressionImpl(expr, depth));
    }

    std::string ExpressionImpl(const Expr *expr, unsigned depth) const
    {
        if (expr == nullptr || depth > 8) {
            return "(expression)";
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            return ref->getDecl()->getNameAsString();
        }
        if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            return Expression(member->getBase(), depth + 1) + (member->isArrow() ? "->" : ".") +
                   member->getMemberDecl()->getNameAsString();
        }
        if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
            return Expression(subscript->getBase(), depth + 1) + "[" + Expression(subscript->getIdx(), depth + 1) + "]";
        }
        if (const auto *integer = dyn_cast<IntegerLiteral>(expr)) {
            return std::to_string(integer->getValue().getLimitedValue());
        }
        if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            return "(" + Expression(binary->getLHS(), depth + 1) + " " + binary->getOpcodeStr().str() + " " +
                   Expression(binary->getRHS(), depth + 1) + ")";
        }
        if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            return UnaryOperator::getOpcodeStr(unary->getOpcode()).str() + Expression(unary->getSubExpr(), depth + 1);
        }
        if (const auto *call = dyn_cast<CallExpr>(expr)) {
            const FunctionDecl *callee = call->getDirectCallee();
            std::string name = callee != nullptr ? DisplayName(callee) : Expression(call->getCallee(), depth + 1);
            if (const auto *member = dyn_cast<MemberExpr>(call->getCallee()->IgnoreParenCasts())) {
                name = Expression(member, depth + 1);
            }
            std::string text = name + "(";
            for (unsigned i = 0; i < call->getNumArgs() && i < 8; ++i) {
                text += (i == 0 ? "" : ", ") + Expression(call->getArg(i), depth + 1);
            }
            return text + (call->getNumArgs() > 8 ? ", ...)" : ")");
        }
        if (const auto *ctor = dyn_cast<CXXConstructExpr>(expr)) {
            return DisplayName(ctor->getConstructor()) + "(...)";
        }
        if (isa<StringLiteral>(expr)) {
            return "a string literal";
        }
        if (isa<CXXThisExpr>(expr)) {
            return "this";
        }
        if (isa<UnaryExprOrTypeTraitExpr>(expr)) {
            return "sizeof(...)";
        }
        return "(expression)";
    }

    void Origins(const Expr *expr, std::set<const VarDecl *> &seen, std::set<std::string> &out,
                 unsigned depth = 0) const
    {
        if (expr == nullptr || depth > 8) {
            return;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *ref = dyn_cast<DeclRefExpr>(expr)) {
            const auto *decl = dyn_cast<VarDecl>(ref->getDecl());
            if (decl == nullptr || !seen.insert(decl->getCanonicalDecl()).second) {
                return;
            }
            if (decl->hasGlobalStorage()) {
                out.insert(std::string(decl->getType()->isPointerType() ? "global pointer '" : "global object '") +
                           decl->getNameAsString() + "'");
            } else if (isa<ParmVarDecl>(decl)) {
                out.insert("parameter '" + decl->getNameAsString() + "'");
            } else {
                auto found = assignments.values.find(decl->getCanonicalDecl());
                if (found != assignments.values.end()) {
                    for (const Expr *value : found->second) {
                        Origins(value, seen, out, depth + 1);
                    }
                }
            }
            return;
        }
        if (isa<CXXThisExpr>(expr)) {
            out.insert("C++ receiver 'this'");
            return;
        }
        if (const auto *call = dyn_cast<CallExpr>(expr)) {
            const FunctionDecl *callee = call->getDirectCallee();
            out.insert("return value of '" + (callee != nullptr ? DisplayName(callee) : "an indirect call") + "'");
        } else if (const auto *conditional = dyn_cast<ConditionalOperator>(expr)) {
            Origins(conditional->getTrueExpr(), seen, out, depth + 1);
            Origins(conditional->getFalseExpr(), seen, out, depth + 1);
        } else if (const auto *member = dyn_cast<MemberExpr>(expr)) {
            if (member->getType()->isPointerType()) {
                out.insert("pointer field '" + Expression(member) + "'");
            }
            Origins(member->getBase(), seen, out, depth + 1);
        } else if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(expr)) {
            Origins(subscript->getBase(), seen, out, depth + 1);
        } else if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            const Expr *pointer = binary->getLHS()->getType()->isPointerType() ? binary->getLHS() : binary->getRHS();
            Origins(pointer, seen, out, depth + 1);
        } else if (const auto *unary = dyn_cast<UnaryOperator>(expr)) {
            Origins(unary->getSubExpr(), seen, out, depth + 1);
        }
    }

    bool Nonlinear(const Expr *expr, unsigned depth = 0) const
    {
        if (expr == nullptr || depth > 8) {
            return false;
        }
        expr = expr->IgnoreParenCasts();
        if (const auto *binary = dyn_cast<BinaryOperator>(expr)) {
            if (binary->getOpcode() == BO_Mul && !binary->getLHS()->isIntegerConstantExpr(context) &&
                !binary->getRHS()->isIntegerConstantExpr(context)) {
                return true;
            }
        }
        for (const Stmt *child : expr->children()) {
            if (const auto *operand = dyn_cast_or_null<Expr>(child)) {
                if (Nonlinear(operand, depth + 1)) {
                    return true;
                }
            }
        }
        return false;
    }

    AccessInfo Of(const Expr *expr) const
    {
        AccessInfo info;
        info.category = "object-access";
        info.expression = Expression(expr);
        if (expr == nullptr) {
            return info;
        }
        expr = expr->IgnoreParenCasts();
        info.type = TypeName(expr->getType());
        info.elementType = info.type;
        info.elementSize = Size(expr->getType());
        info.nonlinear = Nonlinear(expr);
        std::set<const VarDecl *> seen;
        std::set<std::string> sources;
        Origins(expr, seen, sources);
        for (const auto &source : sources) {
            info.origin = Limit(info.origin + (info.origin.empty() ? "" : "; ") + source);
            if (info.origin.size() >= 512) {
                break;
            }
        }
        const Expr *base = expr;
        while (const auto *member = dyn_cast<MemberExpr>(base)) {
            info.category = "member-access";
            if (const auto *field = dyn_cast<FieldDecl>(member->getMemberDecl())) {
                const RecordDecl *record = field->getParent();
                if (record->isCompleteDefinition() && !record->isDependentContext()) {
                    info.fieldOffset += context.getASTRecordLayout(record).getFieldOffset(field->getFieldIndex()) / 8;
                }
            }
            base = member->getBase()->IgnoreParenCasts();
            QualType type = base->getType();
            if (type->isPointerType()) {
                type = type->getPointeeType();
            }
            info.elementType = TypeName(type);
            info.elementSize = Size(type);
        }
        if (const auto *subscript = dyn_cast<ArraySubscriptExpr>(base)) {
            info.category = "array-index";
            info.index = Expression(subscript->getIdx());
            info.elementType = TypeName(subscript->getType());
            info.elementSize = Size(subscript->getType());
        } else if (const auto *unary = dyn_cast<UnaryOperator>(base)) {
            if (unary->getOpcode() == UO_Deref) {
                info.category = "pointer-access";
            }
        }
        return info;
    }
};

AccessDescriber::AccessDescriber(const ASTContext &context, const FunctionDecl *function, const TypeLayout &layout)
    : impl(new Impl(context, function, layout))
{
}
AccessDescriber::~AccessDescriber() = default;

std::vector<AccessInfo> AccessDescriber::Describe(const Stmt *at, const fir::Inst &inst,
                                                  const fir::Function &function) const
{
    std::vector<AccessInfo> out;
    const auto *expr = dyn_cast_or_null<Expr>(at);
    if (inst.op == fir::InstOp::Call) {
        const auto *assignment = dyn_cast_or_null<BinaryOperator>(expr);
        if (assignment != nullptr && assignment->isAssignmentOp()) {
            for (unsigned i = 0; i < 2; ++i) {
                AccessInfo info = impl->Of(i == 0 ? assignment->getLHS() : assignment->getRHS());
                info.category = "call";
                info.operation = impl->Expression(expr);
                info.argument = i;
                out.push_back(std::move(info));
            }
        } else if (const auto *call = dyn_cast_or_null<CallExpr>(expr)) {
            for (unsigned i = 0; i < call->getNumArgs(); ++i) {
                const Expr *arg = call->getArg(i);
                const FunctionDecl *callee = call->getDirectCallee();
                bool reference = callee != nullptr && i < callee->getNumParams() &&
                                 callee->getParamDecl(i)->getType()->isReferenceType();
                if (!arg->getType()->isPointerType() && !arg->getType()->isArrayType() && !reference) {
                    continue;
                }
                AccessInfo info = impl->Of(arg->IgnoreParenCasts());
                QualType type = arg->IgnoreParenCasts()->getType();
                if (type->isPointerType()) {
                    type = type->getPointeeType();
                } else if (const auto *array = impl->context.getAsArrayType(type)) {
                    type = array->getElementType();
                }
                info.elementType = impl->TypeName(type);
                info.elementSize = impl->Size(type);
                info.category = "call";
                info.nonlinear |= impl->Nonlinear(call);
                info.operation = impl->Expression(call);
                info.argument = i;
                out.push_back(std::move(info));
            }
            if (out.empty()) {
                AccessInfo info = impl->Of(expr);
                info.category = "call";
                info.operation = impl->Expression(expr);
                out.push_back(std::move(info));
            }
        } else {
            AccessInfo info = impl->Of(expr);
            info.category = "call";
            info.operation = expr != nullptr ? impl->Expression(expr) : "a constructor or implicit call";
            out.push_back(std::move(info));
        }
    } else if (inst.op == fir::InstOp::Load || inst.op == fir::InstOp::Store) {
        const Expr *target = expr;
        if (const auto *binary = dyn_cast_or_null<BinaryOperator>(expr)) {
            if (binary->isAssignmentOp()) {
                target = binary->getLHS();
            }
        } else if (const auto *unary = dyn_cast_or_null<UnaryOperator>(expr)) {
            if (unary->isIncrementDecrementOp()) {
                target = unary->getSubExpr();
            }
        }
        AccessInfo info = impl->Of(target);
        info.width = inst.size;
        info.mode = inst.op == fir::InstOp::Load ? "R" : "W";
        if (inst.op == fir::InstOp::Store && target == expr && inst.addr < function.exprs.size()) {
            const auto &address = function.exprs[inst.addr];
            if (address.op == fir::ExprOp::LAddr) {
                if (const auto *local = function.FindLocal(address.value)) {
                    info.expression = local->name;
                    info.type = "local object";
                    info.elementType = info.type;
                    info.elementSize = inst.size;
                }
            }
        }
        out.push_back(std::move(info));
    }
    return out;
}

}  // namespace oobsum
