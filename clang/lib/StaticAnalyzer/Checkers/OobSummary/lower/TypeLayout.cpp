//===-- TypeLayout.cpp - Clang types to FIR types ---------------*- C++ -*-===//
//
// 见 TypeLayout.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/lower/TypeLayout.h"

#include "clang/AST/DeclBase.h"
#include "clang/AST/RecordLayout.h"

using namespace clang;

namespace oobsum {

TypeLayout::TypeLayout(const ASTContext &ctx, fir::Module &module) : ctx(ctx), module(module)
{
    // typedef 反查缓存：TU 顶层的 typedef，同一记录取源码里第一个
    for (const Decl *d : ctx.getTranslationUnitDecl()->decls()) {
        const auto *td = dyn_cast<TypedefNameDecl>(d);
        if (td == nullptr) {
            continue;
        }
        const auto *rt = td->getUnderlyingType()->getAs<RecordType>();
        if (rt != nullptr) {
            typedefNames.try_emplace(cast<RecordDecl>(rt->getDecl()->getCanonicalDecl()), td->getNameAsString());
        }
    }
}

std::string TypeLayout::RecordName(const RecordDecl *record) const
{
    auto it = typedefNames.find(cast<RecordDecl>(record->getCanonicalDecl()));
    if (it != typedefNames.end()) {
        return it->second;
    }
    std::string name = record->getNameAsString();  // C++ 的类名可能不是简单标识符（SPEC-0121）
    if (!name.empty()) {
        return name;
    }
    return "<anonymous struct/union>";
}

uint32_t TypeLayout::RecordId(const RecordDecl *definition)
{
    const auto *canonical = cast<RecordDecl>(definition->getCanonicalDecl());
    auto it = records.find(canonical);
    if (it != records.end()) {
        return it->second;
    }
    // 先分号、占位，再算字段（自引用的 struct node *next 不会无限递归）；号就是 records 下标
    uint32_t id = static_cast<uint32_t>(module.records.size());
    records[canonical] = id;
    module.records.emplace_back();
    fir::Record record;
    record.id = id;
    record.name = RecordName(definition);
    record.size = ctx.getTypeSizeInChars(ctx.getRecordType(definition)).getQuantity();
    const ASTRecordLayout &layout = ctx.getASTRecordLayout(definition);
    for (const FieldDecl *field : definition->fields()) {
        if (field->isUnnamedBitfield()) {
            continue;  // 填充
        }
        fir::Field out;
        out.name = field->getNameAsString();
        uint64_t bits = layout.getFieldOffset(field->getFieldIndex());
        out.off = static_cast<int64_t>(bits / 8);
        if (field->isBitField()) {
            // SPEC-0007 §2.2：位域的类型是位宽
            QualType ft = field->getType().getCanonicalType();
            out.type =
                module.Intern(fir::Type::Int(field->getBitWidthValue(ctx), ft->isSignedIntegerOrEnumerationType()));
        } else {
            out.type = Map(field->getType());
        }
        record.fields.push_back(std::move(out));
    }
    module.records[id] = std::move(record);
    return id;
}

fir::TypeId TypeLayout::Map(QualType type)
{
    QualType t = type.getCanonicalType().getUnqualifiedType();
    if (t.isNull() || t->isVoidType()) {
        return module.Intern(fir::Type::Void());
    }
    // C++（SPEC-0121）：模板原型里的依赖类型、未推导的 auto 算不出大小（ASTContext 会断言）；引用按指针建模
    if (t->isDependentType() || t->isUndeducedType() || t->isPlaceholderType()) {
        return module.Intern(fir::Type::Other(0));
    }
    if (const auto *ref = t->getAs<ReferenceType>()) {
        return module.Intern(fir::Type::Ptr(Map(ref->getPointeeType())));
    }
    if (const auto *atomic = t->getAs<AtomicType>()) {
        return Map(atomic->getValueType());
    }
    if (const auto *et = t->getAs<EnumType>()) {
        QualType underlying = et->getDecl()->getIntegerType();
        if (underlying.isNull()) {
            return module.Intern(fir::Type::Other(0));
        }
        return Map(underlying);
    }
    if (t->isIntegerType()) {
        return module.Intern(
            fir::Type::Int(static_cast<unsigned>(ctx.getTypeSize(t)), t->isSignedIntegerOrEnumerationType()));
    }
    if (const auto *pt = t->getAs<PointerType>()) {
        return module.Intern(fir::Type::Ptr(Map(pt->getPointeeType())));
    }
    if (const auto *ca = dyn_cast<ConstantArrayType>(t.getTypePtr())) {
        fir::TypeId elem = Map(ca->getElementType());
        return module.Intern(fir::Type::Arr(elem, static_cast<int64_t>(ca->getSize().getLimitedValue(INT64_MAX - 1))));
    }
    if (const auto *at = dyn_cast<ArrayType>(t.getTypePtr())) {
        // [] / VLA：元素个数未知（D-10 的 n: null）
        return module.Intern(fir::Type::Arr(Map(at->getElementType()), -1));
    }
    if (const auto *rt = t->getAs<RecordType>()) {
        const RecordDecl *definition = rt->getDecl()->getDefinition();
        if (definition == nullptr || definition->isInvalidDecl() || definition->isDependentType()) {
            return module.Intern(fir::Type::Other(0));
        }
        return module.Intern(fir::Type::Rec(RecordId(definition)));
    }
    if (t->isFunctionType() || t->isIncompleteType()) {
        return module.Intern(fir::Type::Other(0));
    }
    return module.Intern(fir::Type::Other(ctx.getTypeSizeInChars(t).getQuantity()));
}

}  // namespace oobsum
