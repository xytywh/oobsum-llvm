//===-- Fir.cpp - OobSummary intermediate representation --------*- C++ -*-===//
//
// 见 Fir.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/core/Fir.h"

namespace oobsum {
namespace fir {

Type Type::Int(unsigned width, bool isSigned)
{
    Type t;
    t.kind = TypeKind::Int;
    t.width = width;
    t.isSigned = isSigned;
    return t;
}

Type Type::Ptr(TypeId to)
{
    Type t;
    t.kind = TypeKind::Ptr;
    t.to = to;
    return t;
}

Type Type::Arr(TypeId elem, int64_t count)
{
    Type t;
    t.kind = TypeKind::Arr;
    t.to = elem;
    t.count = count;
    return t;
}

Type Type::Rec(uint32_t record)
{
    Type t;
    t.kind = TypeKind::Rec;
    t.record = record;
    return t;
}

Type Type::Void()
{
    return Type();
}

Type Type::Other(int64_t size)
{
    Type t;
    t.kind = TypeKind::Other;
    t.size = size;
    return t;
}

Expr Expr::Const(int64_t value)
{
    Expr e;
    e.op = ExprOp::Const;
    e.value = value;
    return e;
}

Expr Expr::LocalValue(uint32_t local)
{
    Expr e;
    e.op = ExprOp::Local;
    e.value = local;
    return e;
}

Expr Expr::Bin(BinOp op, ExprId a, ExprId b, unsigned width, bool isSigned)
{
    Expr e;
    e.op = ExprOp::Bin;
    e.sub = static_cast<uint8_t>(op);
    e.a = a;
    e.b = b;
    e.width = width;
    e.isSigned = width > 0 && isSigned;
    return e;
}

Expr Expr::Cmp(CmpOp op, ExprId a, ExprId b, bool isSigned)
{
    Expr e;
    e.op = ExprOp::Cmp;
    e.sub = static_cast<uint8_t>(op);
    e.a = a;
    e.b = b;
    e.isSigned = isSigned;
    return e;
}

Expr Expr::Unary(ExprOp op, ExprId a)
{
    Expr e;
    e.op = op;
    e.a = a;
    return e;
}

Expr Expr::Cast(unsigned width, bool isSigned, ExprId a)
{
    Expr e;
    e.op = ExprOp::Cast;
    e.width = width;
    e.isSigned = isSigned;
    e.a = a;
    return e;
}

Expr Expr::LAddr(uint32_t local)
{
    Expr e;
    e.op = ExprOp::LAddr;
    e.value = local;
    return e;
}

Expr Expr::GAddr(llvm::StringRef key)
{
    Expr e;
    e.op = ExprOp::GAddr;
    e.text = key.str();
    return e;
}

Expr Expr::Str(int64_t length)
{
    Expr e;
    e.op = ExprOp::Str;
    e.value = length;
    return e;
}

Expr Expr::FAddr(llvm::StringRef key)
{
    Expr e;
    e.op = ExprOp::FAddr;
    e.text = key.str();
    return e;
}

Expr Expr::PAdd(ExprId pointer, ExprId offset)
{
    Expr e;
    e.op = ExprOp::PAdd;
    e.a = pointer;
    e.b = offset;
    return e;
}

Expr Expr::SubObj(ExprId pointer, int64_t size)
{
    Expr e;
    e.op = ExprOp::SubObj;
    e.a = pointer;
    e.size = size;
    return e;
}

Expr Expr::Fld(ExprId pointer, int64_t off, int64_t size, llvm::StringRef name)
{
    Expr e;
    e.op = ExprOp::Fld;
    e.a = pointer;
    e.value = off;
    e.size = size;
    e.text = name.str();
    return e;
}

Expr Expr::PSub(ExprId p1, ExprId p2, int64_t elem)
{
    Expr e;
    e.op = ExprOp::PSub;
    e.a = p1;
    e.b = p2;
    e.value = elem;
    return e;
}

Expr Expr::Unknown(llvm::StringRef why)
{
    Expr e;
    e.op = ExprOp::Unk;
    e.text = why.str();
    return e;
}

ExprId Function::Add(Expr expr)
{
    exprs.push_back(std::move(expr));
    return static_cast<ExprId>(exprs.size() - 1);
}

const Block *Function::FindBlock(uint32_t id) const
{
    for (const Block &block : blocks) {
        if (block.id == id) {
            return &block;
        }
    }
    return nullptr;
}

const Local *Function::FindLocal(uint32_t id) const
{
    for (const Local &local : locals) {
        if (local.id == id) {
            return &local;
        }
    }
    return nullptr;
}

TypeId Module::Intern(const Type &type)
{
    auto it = typeIndex.find(type.Tie());
    if (it != typeIndex.end()) {
        return it->second;
    }
    TypeId id = static_cast<TypeId>(types.size());
    types.push_back(type);
    typeIndex.emplace(type.Tie(), id);
    return id;
}

const Record *Module::FindRecord(uint32_t id) const
{
    for (const Record &record : records) {
        if (record.id == id) {
            return &record;
        }
    }
    return nullptr;
}

llvm::StringRef BinOpName(BinOp op)
{
    static const char *const names[] = {"add", "sub", "mul", "div", "rem", "shl", "shr", "and", "or", "xor"};
    return names[static_cast<unsigned>(op)];
}

llvm::StringRef CmpOpName(CmpOp op)
{
    static const char *const names[] = {"eq", "ne", "lt", "le", "gt", "ge"};
    return names[static_cast<unsigned>(op)];
}

}  // namespace fir
}  // namespace oobsum
