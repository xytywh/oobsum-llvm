//===-- Fir.h - OobSummary intermediate representation ----------*- C++ -*-===//
//
// FIR（oobsum-fir/1）：能决定越界的最少信息——类型只留大小与字段布局，表达式只留整数 / 指针运算，访问都显式化。
// 引擎只看 FIR，不看 Clang（06- D9）。格式见 06- §5.2（D-10…D-14），§5.2 没写死的地方见 SPEC-0005 §2.3。
// JSON 读写在 FirJson.h。不依赖 Clang。
//
// 约定：
//   * 块号、局部号、记录号是任意的唯一整数（沿用 Clang CFG 的块号：入口块号最大、出口块是 0），按号查找。
//   * 表达式放在所属函数的池里（Function::exprs），ExprId 是下标；kNone 表示"没有 / null"。
//   * 类型放在模块的类型表里（Module::types），Module::Intern 去重，比较类型就是比 TypeId。
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIR_H
#define LLVM_CLANG_LIB_STATICANALYZER_CHECKERS_OOBSUMMARY_CORE_FIR_H

#include "llvm/ADT/StringRef.h"

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace oobsum {
namespace fir {

constexpr uint32_t kNone = UINT32_MAX;

using TypeId = uint32_t;
using ExprId = uint32_t;

// D-10
enum class TypeKind : uint8_t { Int, Ptr, Arr, Rec, Void, Other };

struct Type {
    TypeKind kind = TypeKind::Void;
    unsigned width = 0;        // Int：位宽
    bool isSigned = false;     // Int
    TypeId to = kNone;         // Ptr：所指类型；Arr：元素类型
    int64_t count = -1;        // Arr：元素个数；-1 = 柔性 / 不完整（JSON 的 null）
    uint32_t record = kNone;   // Rec：记录号
    int64_t size = 0;          // Other：字节数

    static Type Int(unsigned width, bool isSigned);
    static Type Ptr(TypeId to);
    static Type Arr(TypeId elem, int64_t count);
    static Type Rec(uint32_t record);
    static Type Void();
    static Type Other(int64_t size);

    std::tuple<TypeKind, unsigned, bool, TypeId, int64_t, uint32_t, int64_t> Tie() const
    {
        return std::make_tuple(kind, width, isSigned, to, count, record, size);
    }
};

struct Field {
    std::string name;
    int64_t off = 0;  // 字节偏移（按被分析 TU 的目标 ABI）
    TypeId type = kNone;
};

struct Record {
    uint32_t id = 0;
    std::string name;
    int64_t size = 0;
    std::vector<Field> fields;
};

// D-06：files 下标、行、列（宏展开位置）
struct Loc {
    uint32_t file = 0;
    unsigned line = 0;
    unsigned col = 0;
};

struct Global {
    std::string key;  // SPEC-0003
    std::string name;
    bool def = false;
    TypeId type = kNone;
    Loc loc;
};

// D-11
struct Local {
    uint32_t id = 0;
    std::string name;
    int param = -1;    // 形参下标；-1 = 不是形参
    TypeId type = kNone;
    bool obj = false;  // 内存对象（数组、结构体、取过地址的标量）：读写走 load / store
    bool tmp = false;  // 降级产生的临时值，永远不进摘要
};

// D-13
// 顺序与 JSON 的 op 名表一致（FirJson.cpp 的 kExprOps）
enum class ExprOp : uint8_t {
    Const,
    Local,
    Bin,
    Cmp,
    LNot,
    Neg,
    BNot,
    Cast,
    LAddr,
    GAddr,
    Str,
    FAddr,
    PAdd,
    Fld,
    PSub,
    Unk
};
enum class BinOp : uint8_t { Add, Sub, Mul, Div, Rem, Shl, Shr, And, Or, Xor };
enum class CmpOp : uint8_t { Eq, Ne, Lt, Le, Gt, Ge };

struct Expr {
    ExprOp op = ExprOp::Unk;
    uint8_t sub = 0;        // Bin：BinOp；Cmp：CmpOp
    unsigned width = 0;     // Bin：0 = 数学整数、不回绕（SPEC-0005 §2.3 #1）；Cast：目标位宽
    bool isSigned = false;  // Bin（width > 0 时）/ Cmp / Cast
    int64_t value = 0;      // Const：值；Local / LAddr：局部号；Str：字符串长度；Fld：成员偏移；PSub：元素大小
    int64_t size = 0;       // Fld：成员大小
    ExprId a = kNone;       // Bin / Cmp / LNot / Neg / BNot / Cast / PAdd / Fld / PSub 的第一个操作数
    ExprId b = kNone;       // Bin / Cmp / PAdd / PSub 的第二个操作数
    std::string text;       // GAddr / FAddr：键；Fld：成员名；Unk：原因

    static Expr Const(int64_t value);
    static Expr LocalValue(uint32_t local);
    static Expr Bin(BinOp op, ExprId a, ExprId b, unsigned width = 0, bool isSigned = false);
    static Expr Cmp(CmpOp op, ExprId a, ExprId b, bool isSigned);
    static Expr Unary(ExprOp op, ExprId a);  // LNot / Neg / BNot
    static Expr Cast(unsigned width, bool isSigned, ExprId a);
    static Expr LAddr(uint32_t local);
    static Expr GAddr(llvm::StringRef key);
    static Expr Str(int64_t length);
    static Expr FAddr(llvm::StringRef key);
    static Expr PAdd(ExprId pointer, ExprId offset);
    static Expr Fld(ExprId pointer, int64_t off, int64_t size, llvm::StringRef name);
    static Expr PSub(ExprId p1, ExprId p2, int64_t elem);
    static Expr Unknown(llvm::StringRef why);
};

// D-12
enum class InstOp : uint8_t { Set, Load, Store, Call, Havoc, Ret };

struct Inst {
    InstOp op = InstOp::Havoc;
    uint32_t dst = kNone;       // Set / Load / Havoc：必有；Call：可无
    ExprId addr = kNone;        // Load / Store
    ExprId val = kNone;         // Set：必有；Store / Ret：kNone = null
    int64_t size = 0;           // Load / Store：字节数
    bool sub = false;           // Load / Store：下标访问（A-07）
    std::string callee;         // Call：直接调用的函数键（与 calleeExpr 二选一）
    ExprId calleeExpr = kNone;  // Call：间接调用 ["ind", E]
    std::vector<ExprId> args;   // Call
    uint32_t ord = 0;           // Call：调用点序号
    std::string why;            // Havoc
    Loc loc;
};

// D-14
struct Edge {
    uint32_t to = 0;
    ExprId assume = kNone;  // kNone = 无条件
};

struct Block {
    uint32_t id = 0;
    std::vector<Inst> insts;
    std::vector<Edge> succ;
};

struct Function {
    std::string key;  // SPEC-0003
    std::string name;
    bool ext = false;
    Loc loc;
    TypeId ret = kNone;
    std::vector<Local> locals;
    uint32_t entry = 0;
    uint32_t exit = 0;
    std::vector<Block> blocks;
    std::vector<std::string> flags;  // 如 cfg_too_large（A-09）
    std::vector<Expr> exprs;         // 表达式池

    ExprId Add(Expr expr);
    const Block *FindBlock(uint32_t id) const;
    const Local *FindLocal(uint32_t id) const;
};

struct Target {
    std::string triple;
    unsigned ptr = 8;  // 指针字节数
    unsigned longSize = 8;
    bool bigEndian = false;
};

struct Module {
    std::string tu;    // TU id（D-05）
    std::string file;  // 主文件
    Target target;
    std::vector<std::string> files;
    std::vector<Type> types;  // 类型表（Intern 去重）
    std::vector<Record> records;
    std::vector<Global> globals;
    std::vector<Function> funcs;

    TypeId Intern(const Type &type);
    const Record *FindRecord(uint32_t id) const;

private:
    std::map<std::tuple<TypeKind, unsigned, bool, TypeId, int64_t, uint32_t, int64_t>, TypeId> typeIndex;
};

llvm::StringRef BinOpName(BinOp op);
llvm::StringRef CmpOpName(CmpOp op);

}  // namespace fir
}  // namespace oobsum

#endif
