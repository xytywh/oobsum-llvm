//===-- SourceMap.cpp - Proven diagnostic source paths ----------*- C++ -*-===//
// SPEC-0136 / D-06、A-91。
#include "OobSummary/core/SourceMap.h"
#include "OobSummary/core/Key.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Path.h"

namespace oobsum {
namespace {
std::string Base(const std::string &file)
{
    return llvm::sys::path::filename(file, llvm::sys::path::Style::posix).str();
}
void Insert(std::map<std::string, std::string> &paths, const std::string &file)
{
    std::string normalized = NormalizePath(file);
    auto it = paths.find(normalized);
    if (it == paths.end() || file < it->second) {
        paths[normalized] = file;
    }
}
std::string Short(const fir::Module &module, const fir::Loc &loc)
{
    return loc.file < module.files.size() ? Base(module.files[loc.file]) + ":" + std::to_string(loc.line) : "";
}
}  // namespace

SourceMap::SourceMap(const std::vector<const fir::Module *> &modules)
{
    for (const auto *module : modules) {
        auto &files = moduleFiles[module];
        for (const auto &file : module->files) {
            Insert(files[Base(file)], file);
            Insert(allFiles[Base(file)], file);
        }
        for (const auto &fn : module->funcs) {
            if (owners.emplace(fn.key, Owner{module, &fn}).second) {
                names[fn.name].push_back(fn.key);
            }
        }
    }
}

SourceMap::Paths SourceMap::Candidates(const std::string &key, const std::string &source, const std::string &base) const
{
    auto owner = owners.find(key);
    if (owner == owners.end()) {
        return {};
    }
    const auto &module = *owner->second.module;
    auto files = moduleFiles.at(&module).find(base);
    if (files == moduleFiles.at(&module).end()) {
        return {};
    }
    if (files->second.size() == 1) {
        return files->second;
    }
    auto cached = functionSites.find(key);
    if (cached == functionSites.end()) {
        auto &index = functionSites[key];
        auto add = [&](const fir::Loc &loc) {
            if (loc.file < module.files.size()) {
                Insert(index[Short(module, loc)], module.files[loc.file]);
            }
        };
        add(owner->second.function->loc);
        for (const auto &block : owner->second.function->blocks) {
            for (const auto &inst : block.insts) {
                add(inst.loc);
            }
        }
        cached = functionSites.find(key);
    }
    auto found = cached->second.find(source);
    return found != cached->second.end() ? found->second : Paths();
}

SourceResolution SourceMap::Resolve(const std::vector<std::string> &keys, const std::string &source) const
{
    SourceResolution out;
    auto parts = llvm::StringRef(source).rsplit(':');
    unsigned line = 0;
    bool valid = !parts.first.empty() && !parts.second.getAsInteger(10, line) && line > 0;
    std::string file = parts.first.str();
    std::string base = Base(file);
    out.site.line = line;
    Paths candidates;
    if (valid && llvm::sys::path::has_parent_path(file)) {
        // 显式完整/相对路径必须存在于输入文件表，不加载任意碰巧存在的文件。
        auto all = allFiles.find(base);
        if (all != allFiles.end()) {
            auto exact = all->second.find(NormalizePath(file));
            if (exact != all->second.end()) {
                candidates.insert(*exact);
            }
        }
    } else if (valid) {
        for (const auto &key : keys) {
            auto own = Candidates(key, source, base);
            candidates.insert(own.begin(), own.end());
        }
        // 只有缺少函数键的旧路径才允许全域唯一文件回退；有键但找不到位置不能借其它函数猜。
        if (keys.empty()) {
            auto all = allFiles.find(base);
            if (all != allFiles.end()) {
                candidates = all->second;
            }
        }
    }
    out.candidates = candidates.size();
    out.status = candidates.size() == 1 ? "unique" : candidates.empty() ? "missing" : "ambiguous";
    out.site.file = candidates.size() == 1 ? candidates.begin()->second
                                           : std::string(kUnresolvedSourcePrefix) + (base.empty() ? "?" : base);
    return out;
}
std::vector<std::string> SourceMap::KeysForName(const std::string &name) const
{
    auto found = names.find(name);
    return found != names.end() ? found->second : std::vector<std::string>();
}
std::string SourceMap::Name(const std::string &key) const
{
    auto found = owners.find(key);
    return found == owners.end() ? "" : found->second.function->name;
}
}  // namespace oobsum
