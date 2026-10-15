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
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallPtrSet.h"

#include <algorithm>

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

class FunctionLowerer {
public:
    FunctionLowerer(AnalysisManager &mgr, KeyMaker &keys, TypeLayout &layout, FileTable &files)
        : mgr(mgr), keys(keys), layout(layout), files(files)
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
        LowerLocals(fd, f);
        LowerCfgSkeleton(fd, f);
        return f;
    }

private:
    bool IsObject(const VarDecl *vd, const AddressTaken &taken) const
    {
        QualType t = vd->getType().getCanonicalType();
        return t->isArrayType() || t->isRecordType() || taken.vars.count(vd->getCanonicalDecl()) != 0;
    }

    void LowerLocals(const FunctionDecl *fd, fir::Function &f)
    {
        AddressTaken taken;
        LocalCollector collector;
        if (Stmt *body = fd->getBody()) {
            taken.TraverseStmt(body);
            collector.TraverseStmt(body);
        }
        uint32_t next = 0;
        for (unsigned i = 0; i < fd->getNumParams(); ++i) {
            const ParmVarDecl *p = fd->getParamDecl(i);
            fir::Local local;
            local.id = next++;
            local.name = p->getNameAsString();
            local.param = static_cast<int>(i);
            local.type = layout.Map(p->getType());
            local.obj = IsObject(p, taken);
            f.locals.push_back(std::move(local));
        }
        for (const VarDecl *vd : collector.vars) {
            fir::Local local;
            local.id = next++;
            local.name = vd->getNameAsString();
            local.type = layout.Map(vd->getType());
            local.obj = IsObject(vd, taken);
            f.locals.push_back(std::move(local));
        }
    }

    // A-01：与 CSA 同一份 CFG；块按号从大到小（入口在前、出口 b0 在后）；边先不带 assume（SPEC-0007 §2.4）
    void LowerCfgSkeleton(const FunctionDecl *fd, fir::Function &f)
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
        f.flags.push_back("body_pending");
        std::vector<const CFGBlock *> blocks(cfg->begin(), cfg->end());
        std::sort(blocks.begin(), blocks.end(),
                  [](const CFGBlock *a, const CFGBlock *b) { return a->getBlockID() > b->getBlockID(); });
        for (const CFGBlock *block : blocks) {
            fir::Block out;
            out.id = block->getBlockID();
            for (const CFGBlock::AdjacentBlock &succ : block->succs()) {
                if (const CFGBlock *reachable = succ.getReachableBlock()) {
                    fir::Edge edge;
                    edge.to = reachable->getBlockID();
                    out.succ.push_back(edge);
                }
            }
            f.blocks.push_back(std::move(out));
        }
        f.entry = cfg->getEntry().getBlockID();
        f.exit = cfg->getExit().getBlockID();
    }

    AnalysisManager &mgr;
    KeyMaker &keys;
    TypeLayout &layout;
    FileTable &files;
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
