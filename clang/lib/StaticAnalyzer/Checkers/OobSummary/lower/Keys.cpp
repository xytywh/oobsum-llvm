//===-- Keys.cpp - Clang decls to OobSummary keys ---------------*- C++ -*-===//
//
// 见 Keys.h。
//
//===----------------------------------------------------------------------===//

#include "OobSummary/lower/Keys.h"

#include "OobSummary/core/Key.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Mangle.h"
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

const VarDecl *VarDefinition(const VarDecl *vd)
{
    if (const VarDecl *def = vd->getDefinition()) {
        return def;
    }
    // 声明链从最新往前走，最后碰到的暂定定义就是源码里最早的那个
    const VarDecl *earliest = nullptr;
    for (const VarDecl *r = vd->getMostRecentDecl(); r != nullptr; r = r->getPreviousDecl()) {
        if (r->isThisDeclarationADefinition() == VarDecl::TentativeDefinition) {
            earliest = r;
        }
    }
    return earliest;
}

static const VarDecl *HomeOf(const VarDecl *vd)
{
    const VarDecl *def = VarDefinition(vd);
    return def != nullptr ? def : vd->getFirstDecl();
}

static bool IsGlobal(const VarDecl *vd)
{
    // 块作用域的 extern 也指向全局；isLocalExternDecl 在 14 里不是 const
    return vd->isFileVarDecl() || const_cast<VarDecl *>(vd)->isLocalExternDecl();
}

KeyMaker::KeyMaker(const SourceManager &sm) : sm(sm)
{
}

KeyMaker::~KeyMaker() = default;

void KeyMaker::SetContext(ASTContext &context)
{
    ctx = &context;
    mangler = context.getLangOpts().CPlusPlus ? std::make_unique<ASTNameGenerator>(context) : nullptr;
}

static bool IsExternC(const NamedDecl *d)
{
    if (const auto *fd = dyn_cast<FunctionDecl>(d)) {
        return fd->isExternC();
    }
    if (const auto *vd = dyn_cast<VarDecl>(d)) {
        return vd->isExternC();
    }
    return false;
}

std::string KeyMaker::NameOf(const NamedDecl *d)
{
    // C++ 链接的函数 / 全局（SPEC-0121）：mangle 名；模板原型（依赖上下文）不 mangle（ASTNameGenerator 会失败），也不会被降级
    if (mangler != nullptr && (isa<FunctionDecl>(d) || isa<VarDecl>(d)) && !IsExternC(d) &&
        !d->getDeclContext()->isDependentContext()) {
        std::string name = mangler->getName(d);
        if (!name.empty()) {
            return name;
        }
    }
    return d->getIdentifier() != nullptr ? d->getName().str() : d->getNameAsString();
}

// 一层的名字：不带模板实参（getQualifiedNameAsString 会把类模板特化的实参整个打印出来，嵌套的模板能长到打印不完——
// 上游 CodeGenCXX/pr29160.cpp 实测超时）；类型转换函数的名字里是目标类型，同理不打印
static std::string PartName(const NamedDecl *d)
{
    if (d->getIdentifier() != nullptr) {
        return d->getName().str();
    }
    if (isa<CXXConversionDecl>(d)) {
        return "operator conversion";
    }
    std::string name = d->getNameAsString();
    return name.empty() ? "(anonymous)" : name;
}

std::string DisplayName(const NamedDecl *d)
{
    std::string name = PartName(d);
    if (!d->getASTContext().getLangOpts().CPlusPlus) {
        return name;
    }
    for (const DeclContext *dc = d->getDeclContext(); dc != nullptr && !dc->isTranslationUnit(); dc = dc->getParent()) {
        if (const auto *scope = dyn_cast<NamedDecl>(dc)) {
            name = PartName(scope) + "::" + name;
        }
    }
    return name;
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
    return FunctionKey(NameOf(home), external, fileId);
}

std::string KeyMaker::ForVar(const VarDecl *vd)
{
    if (vd->isStaticLocal()) {
        const auto *owner = dyn_cast_or_null<FunctionDecl>(vd->getParentFunctionOrMethod());
        return owner != nullptr ? StaticLocalKey(ForFunction(owner), vd->getNameAsString()) : "";
    }
    if (!IsGlobal(vd)) {
        return "";
    }
    const VarDecl *home = HomeOf(vd);
    bool external = home->isExternallyVisible();
    std::string fileId = external ? "" : FileOf(sm.getFileID(HomeLoc(home))).second;
    return GlobalKey(NameOf(home), external, fileId);
}

bool KeyMaker::Describe(const Decl *d, KeyInfo &info)
{
    const Decl *home = nullptr;
    if (const auto *fd = dyn_cast<FunctionDecl>(d)) {
        home = HomeOf(fd);
        info.key = ForFunction(fd);
        info.kind = "func";
        info.def = fd->getDefinition() != nullptr;
        info.scopeName = DisplayName(fd);
    } else if (const auto *vd = dyn_cast<VarDecl>(d)) {
        info.key = ForVar(vd);
        if (info.key.empty()) {
            return false;
        }
        home = HomeOf(vd);
        info.kind = vd->isStaticLocal() ? "static" : "global";
        info.def = VarDefinition(vd) != nullptr;
        const auto *owner = dyn_cast_or_null<FunctionDecl>(vd->getParentFunctionOrMethod());
        info.scopeName = vd->isStaticLocal() && owner != nullptr ? DisplayName(owner) : DisplayName(vd);
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

    // C++ 的模板只看实例化，原型（依赖上下文）跳过（SPEC-0121）
    bool shouldVisitTemplateInstantiations() const
    {
        return true;
    }

    bool VisitFunctionDecl(FunctionDecl *fd)
    {
        if (!fd->isImplicit() && fd->doesThisDeclarationHaveABody() && !fd->isDependentContext()) {
            add(fd->getCanonicalDecl());
        }
        return true;
    }

    bool VisitVarDecl(VarDecl *vd)
    {
        if (!vd->isImplicit() && !isa<ParmVarDecl>(vd) && !vd->getDeclContext()->isDependentContext() &&
            !vd->getType()->isDependentType()) {
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
