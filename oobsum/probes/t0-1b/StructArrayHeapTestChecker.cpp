// StructArrayHeapTestChecker.cpp
// Debug-only checker: print region kinds and the static size of the base region.
// Only locations visited by the analyzer are observed; no BugReport is emitted.
//
// T0-1b（调研包 v3.4）：在 T0-1 的基础上补测"数组本身那一层"的 extent。
//   T0-1 打印的是 getBaseRegion() 的大小 —— 对 s.d[i] 这类字段数组，那是整个 struct 的大小（36），
//   不是字段数组 d 本身的大小（32）。而 ArrayBoundCheckerV2 真正拿来比较的，是"剥掉 ElementRegion 之后的
//   第一层"（RegionRawOffsetV2::computeOffset），对 s.d[i] 就是 FieldRegion d。
//   本版把两层都打出来，并补上：dynamic extent（malloc 的大小只存在这里）、元素个数、内存空间、源码位置。
//   只打印数组访问（ElementRegion），标量变量 i / p 的读取不再打印，一行对应一个访问，便于直接贴回。

#include "clang/StaticAnalyzer/Checkers/BuiltinCheckerRegistration.h"

#include "clang/AST/Stmt.h"
#include "clang/Basic/SourceManager.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/CheckerManager.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/DynamicExtent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/MemRegion.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SValBuilder.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/SVals.h"

#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace ento;

namespace {

// 从当前编译环境的 Regions.def 生成枚举名称，兼容没有 getKindStr() 的版本。
static const char *GetRegionKindName(MemRegion::Kind kind)
{
    switch (kind) {
#define REGION(Id, Parent)    \
    case MemRegion::Id##Kind: \
        return #Id;
#define ABSTRACT_REGION(Id, Parent)
#define REGION_RANGE(Id, First, Last)
#include "clang/StaticAnalyzer/Core/PathSensitive/Regions.def"
#undef REGION
#undef ABSTRACT_REGION
#undef REGION_RANGE
        default:
            return "UnknownRegion";
    }
}

// 剥掉所有 ElementRegion，返回第一层非 ElementRegion 的祖先。
// 与 ArrayBoundCheckerV2 取"比较基准"的层级一致（LLVM 14 RegionRawOffsetV2::computeOffset）：
//   a[i]        -> VarRegion a
//   s.d[i]      -> FieldRegion d        （不是 VarRegion s）
//   x.m[1][j]   -> FieldRegion m        （二维数组整体，不是某一行）
//   heap[i]     -> SymbolicRegion
static const MemRegion *StripElementRegions(const MemRegion *region)
{
    while (const auto *elem = dyn_cast<ElementRegion>(region)) {
        region = elem->getSuperRegion();
    }
    return region;
}

class StructArrayHeapTestChecker : public Checker<check::Location> {
public:
    void checkLocation(SVal loc, bool isLoad, const Stmt *stmt, CheckerContext &ctx) const
    {
        const MemRegion *region = loc.getAsRegion();
        if (!region) {
            return;
        }
        // 只看数组访问；标量变量（i、p、heap 指针本身）的读写不打印
        const auto *elemRegion = dyn_cast<ElementRegion>(region);
        if (!elemRegion) {
            return;
        }

        ProgramStateRef state = ctx.getState();
        SValBuilder &svb = ctx.getSValBuilder();
        ASTContext &astCtx = ctx.getASTContext();
        MemRegionManager &regionMgr = region->getMemRegionManager();

        const MemRegion *arrayBase = StripElementRegions(elemRegion);   // ArrayBoundCheckerV2 的比较基准
        const MemRegion *outerBase = region->getBaseRegion();            // T0-1 打印的那一层
        QualType elemType = elemRegion->getElementType();

        // ★ 这个 API 在你们 fork 里叫什么、存不存在，正是要确认的
        DefinedOrUnknownSVal arrayStatic = regionMgr.getStaticSize(arrayBase, svb);
        // dynamic extent：先查 ProgramState 里的 DynamicExtentMap（malloc/alloca/VLA 的大小存在这里），
        // 查不到再退回 getStaticSize。ArrayBoundCheckerV2 用的是这个，不是 getStaticSize。
        DefinedOrUnknownSVal arrayDynamic = getDynamicExtent(state, arrayBase, svb);
        DefinedOrUnknownSVal outerStatic = regionMgr.getStaticSize(outerBase, svb);

        // 元素个数 = dynamic extent / sizeof(元素)。不完整类型或 0 大小元素不算，避免除零和断言
        int64_t elemSize = 0;
        if (!elemType.isNull() && !elemType->isIncompleteType()) {
            elemSize = astCtx.getTypeSizeInChars(elemType).getQuantity();
        }

        llvm::raw_ostream &os = llvm::errs();
        os << "[EXT] ";
        if (stmt) {
            const SourceManager &sm = ctx.getSourceManager();
            SourceLocation begin = stmt->getBeginLoc();
            os << sm.getExpansionLineNumber(begin) << ":" << sm.getExpansionColumnNumber(begin) << " ";
        }
        os << (isLoad ? "R " : "W ");
        if (stmt) {
            stmt->printPretty(os, nullptr, astCtx.getPrintingPolicy());
        }
        os << "  elem=" << elemType.getAsString() << "/" << elemSize << "B";
        os << " | array=" << GetRegionKindName(arrayBase->getKind()) << " static=" << arrayStatic
           << " dynamic=" << arrayDynamic << " count=";
        if (elemSize > 0) {
            os << getDynamicElementCount(state, arrayBase, svb, elemType);
        } else {
            os << "n/a";
        }
        os << " | outer=" << GetRegionKindName(outerBase->getKind()) << " static=" << outerStatic;
        os << " | space=" << GetRegionKindName(arrayBase->getMemorySpace()->getKind()) << "\n";
    }
};

} // namespace

void ento::registerStructArrayHeapTestChecker(CheckerManager &mgr)
{
    mgr.registerChecker<StructArrayHeapTestChecker>();
}

bool ento::shouldRegisterStructArrayHeapTestChecker(const CheckerManager &)
{
    return true;
}
