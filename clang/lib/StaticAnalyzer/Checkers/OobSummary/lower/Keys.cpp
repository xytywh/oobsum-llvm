//===-- Keys.cpp - Clang decls to OobSummary keys ---------------*- C++ -*-===//
//
// 见 Keys.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/lower/Keys.h"

#include "OobSummary/core/Key.h"

#include "clang/AST/DeclBase.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/FileEntry.h"

#include "llvm/ADT/SmallPtrSet.h"

using namespace clang;

namespace oobsum {

// 定义（没有定义时取第一处声明）：键里的文件、位置都以它为准（SPEC-0003 §2.1）
static const FunctionDecl *HomeOf(const FunctionDecl *fd)
{
    const FunctionDecl *def = fd->getDefinition();
    return def != nullptr ? def : fd->getFirstDecl();
}

static const VarDecl *DefinitionOf(const VarDecl *vd)
{
    const VarDecl *def = vd->getDefinition();
    return def != nullptr ? def : vd->getActingDefinition();
}

static const VarDecl *HomeOf(const VarDecl *vd)
{
    const VarDecl *def = DefinitionOf(vd);
    return def != nullptr ? def : vd->getFirstDecl();
}

static bool IsGlobal(const VarDecl *vd)
{
    // 块作用域的 extern 也指向全局；isLocalExternDecl 在 14 里不是 const
    return vd->isFileVarDecl() || const_cast<VarDecl *>(vd)->isLocalExternDecl();
}

SourceLocation KeyMaker::HomeLoc(const Decl *home) const
{
    return sm.getExpansionLoc(home->getLocation());
}

std::pair<std::string, std::string> KeyMaker::FileOf(FileID fid)
{
    auto it = files.find(fid);
    if (it != files.end()) {
        return it->second;
    }
    const FileEntry *entry = sm.getFileEntryForID(fid);
    std::string path = entry != nullptr ? NormalizePath(entry->getName()) : std::string("<builtin>");
    std::string id = FileId(path);
    return files.insert({fid, {path, id}}).first->second;
}

std::string KeyMaker::ForFunction(const FunctionDecl *fd)
{
    const FunctionDecl *home = HomeOf(fd);
    bool external = home->isExternallyVisible();
    std::string fileId = external ? "" : FileOf(sm.getFileID(HomeLoc(home))).second;
    return FunctionKey(home->getName(), external, fileId);
}

std::string KeyMaker::ForVar(const VarDecl *vd)
{
    if (vd->isStaticLocal()) {
        const auto *owner = dyn_cast_or_null<FunctionDecl>(vd->getParentFunctionOrMethod());
        return owner != nullptr ? StaticLocalKey(ForFunction(owner), vd->getName()) : "";
    }
    if (!IsGlobal(vd)) {
        return "";
    }
    const VarDecl *home = HomeOf(vd);
    bool external = home->isExternallyVisible();
    std::string fileId = external ? "" : FileOf(sm.getFileID(HomeLoc(home))).second;
    return GlobalKey(home->getName(), external, fileId);
}

bool KeyMaker::Describe(const Decl *d, KeyInfo &info)
{
    const Decl *home = nullptr;
    if (const auto *fd = dyn_cast<FunctionDecl>(d)) {
        home = HomeOf(fd);
        info.key = ForFunction(fd);
        info.kind = "func";
        info.def = fd->getDefinition() != nullptr;
        info.scopeName = fd->getName().str();
    } else if (const auto *vd = dyn_cast<VarDecl>(d)) {
        info.key = ForVar(vd);
        if (info.key.empty()) {
            return false;
        }
        home = HomeOf(vd);
        info.kind = vd->isStaticLocal() ? "static" : "global";
        info.def = DefinitionOf(vd) != nullptr;
        const auto *owner = dyn_cast_or_null<FunctionDecl>(vd->getParentFunctionOrMethod());
        info.scopeName = (vd->isStaticLocal() && owner != nullptr ? owner->getName() : vd->getName()).str();
    } else {
        return false;
    }
    SourceLocation loc = HomeLoc(home);
    FileID fid = sm.getFileID(loc);
    std::pair<std::string, std::string> file = FileOf(fid);
    info.file = file.first;
    info.fileId = file.second;
    info.line = sm.getExpansionLineNumber(loc);
    info.col = sm.getExpansionColumnNumber(loc);
    info.header = info.kind == "func" && !cast<NamedDecl>(home)->isExternallyVisible() && fid != sm.getMainFileID();
    return true;
}

namespace {

class KeyCollector : public RecursiveASTVisitor<KeyCollector> {
public:
    KeyCollector(KeyMaker &keys, std::vector<KeyInfo> &out) : keys(keys), out(out)
    {
    }

    bool VisitFunctionDecl(FunctionDecl *fd)
    {
        if (!fd->isImplicit() && fd->doesThisDeclarationHaveABody()) {
            add(fd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitVarDecl(VarDecl *vd)
    {
        if (!vd->isImplicit() && !isa<ParmVarDecl>(vd)) {
            add(vd->getCanonicalDecl());
        }
        return true;
    }

private:
    void add(const Decl *canonical)
    {
        if (!seen.insert(canonical).second) {
            return;
        }
        KeyInfo info;
        if (keys.Describe(canonical, info)) {
            out.push_back(std::move(info));
        }
    }

    KeyMaker &keys;
    std::vector<KeyInfo> &out;
    llvm::SmallPtrSet<const Decl *, 32> seen;
};

}  // namespace

std::vector<KeyInfo> CollectKeys(const TranslationUnitDecl *tu, KeyMaker &keys)
{
    std::vector<KeyInfo> out;
    KeyCollector collector(keys, out);
    collector.TraverseDecl(const_cast<TranslationUnitDecl *>(tu));
    return out;
}

}  // namespace oobsum
