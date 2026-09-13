#include "../../pch.h"

#include "modules.h"

#include <filesystem>

#include "builtins.h"

namespace pgg {

bool hasImports(const File& file) {
    for (const Node* item : file.items)
        if (item->kind == NodeKind::Import) return true;
    return false;
}

namespace {

class Loader {
public:
    Loader(const std::vector<std::string>& roots, std::vector<Diagnostic>& diags)
        : roots_(roots), diags_(diags) {}

    // Binds the import directives of `file` into `out` (namespace -> module).
    // `preferRoot` is the importing file's directory: sibling imports
    // (`import common` from props/well.pgg) resolve there first.
    void bindImports(const File& file, std::unordered_map<std::string, const ModuleInfo*>& out,
                     const std::string& preferRoot = {}) {
        for (const Node* item : file.items) {
            if (item->kind != NodeKind::Import) continue;
            const auto* im = static_cast<const Import*>(item);
            if (im->hasVersion) {
                error("E201", im->span,
                      "import versioning ('@" + im->version + "') is not supported at stage E5",
                      "version pinning is deferred (spec §7.6)");
                continue;
            }
            const std::string ns = im->hasAlias ? im->alias : im->path.back();
            if (findBuiltin(ns)) {
                error("E102", im->span, "namespace '" + ns + "' collides with a built-in operation",
                      "rename the import with `as` (spec §7.6)");
                continue;
            }
            if (const ModuleInfo* m = resolve(*im, preferRoot)) out[ns] = m;
        }
    }

    ModuleClosure run(const File& mainFile) {
        bindImports(mainFile, closure_.mainNamespaces);
        return std::move(closure_);
    }

private:
    const std::vector<std::string>& roots_;
    std::vector<Diagnostic>& diags_;
    ModuleClosure closure_;
    std::unordered_map<std::string, const ModuleInfo*> byPath_;  // non-owning (closure_ owns)
    std::vector<std::string> loading_;  // canonical paths on the current import stack

    void error(const std::string& code, Span span, std::string msg, std::string hint = {}) {
        diags_.push_back(Diagnostic{code, span, std::move(msg), std::move(hint), false});
    }

    bool existsIn(const std::string& root, const std::string& rel, std::string& found) const {
        if (root.empty()) return false;
        std::error_code ec;
        const std::string candidate = (std::filesystem::path(root) / rel).string();
        if (!std::filesystem::exists(candidate, ec)) return false;
        found = candidate;
        return true;
    }

    const ModuleInfo* resolve(const Import& im, const std::string& preferRoot) {
        std::string rel;
        for (const std::string& part : im.path) rel += (rel.empty() ? "" : "/") + part;
        rel += ".pgg";

        std::string found;
        if (!existsIn(preferRoot, rel, found)) {
            for (const std::string& root : roots_) {
                if (existsIn(root, rel, found)) break;
            }
        }
        if (found.empty()) {
            std::string hint;
            if (roots_.empty() && preferRoot.empty()) {
                hint = "no import roots configured — pass RunParams::importRoots (or run a file: its "
                       "directory is an implicit root)";
            } else {
                hint = "searched:";
                if (!preferRoot.empty()) hint += " " + preferRoot;
                for (const std::string& root : roots_) hint += " " + root;
            }
            std::string name;
            for (const std::string& part : im.path) name += (name.empty() ? "" : ".") + part;
            error("E501", im.span, "module '" + name + "' not found", hint);
            return nullptr;
        }

        std::error_code ec;
        const std::string canonical = std::filesystem::weakly_canonical(found, ec).string();
        const std::string& key = ec ? found : canonical;
        if (auto it = byPath_.find(key); it != byPath_.end()) return it->second;
        for (const std::string& onStack : loading_) {
            if (onStack != key) continue;
            std::string chain;
            for (const std::string& p : loading_) chain += (chain.empty() ? "" : " -> ") + p;
            chain += " -> " + key;
            error("E502", im.span, "import cycle: " + chain, "break the cycle (spec §7.6)");
            return nullptr;
        }

        loading_.push_back(key);
        auto info = std::make_unique<ModuleInfo>();
        info->canonicalPath = key;
        info->displayPath = found;
        closure_.owned.push_back(std::make_unique<Document>(parseFile(found)));
        info->doc = closure_.owned.back().get();
        // The module's own parse/lint findings, namespaced by its path.
        for (const Diagnostic& d : info->doc->diagnostics) {
            Diagnostic copy = d;
            copy.message = "[" + found + "] " + copy.message;
            diags_.push_back(std::move(copy));
        }
        if (info->doc->file) {
            for (const Node* item : info->doc->file->items) {
                if (item->kind != NodeKind::Def) continue;
                const auto* d = static_cast<const Def*>(item);
                info->defs[d->name] = d;
                // Library lint (§6.5): a def in an import-root file must carry
                // its docstring contract.
                if (!d->hasDoc) {
                    error("E506", d->span,
                          "library def '" + d->name + "' has no docstring [" + found + "]",
                          "document the contract: first line what it does, then Requires/Writes (§7.5)");
                }
            }
            // Nested imports search the module's directory first (sibling
            // `import common` from props/*.pgg) then the global roots.
            bindImports(*info->doc->file, info->namespaces,
                        std::filesystem::path(found).parent_path().string());
        }
        loading_.pop_back();
        const ModuleInfo* result = info.get();
        closure_.modules.push_back(result);
        closure_.ownedModules.push_back(std::move(info));
        byPath_.emplace(key, result);
        return result;
    }
};

}  // namespace

ModuleClosure loadModuleClosure(const File& mainFile, const std::vector<std::string>& roots,
                                std::vector<Diagnostic>& diags) {
    Loader loader(roots, diags);
    return loader.run(mainFile);
}

namespace {

std::string walkProductLib(std::filesystem::path cur) {
    std::error_code ec;
    if (cur.empty()) return {};
    if (std::filesystem::is_regular_file(cur, ec)) cur = cur.parent_path();
    cur = std::filesystem::weakly_canonical(cur, ec);
    for (int depth = 0; depth < 16 && !cur.empty(); ++depth) {
        const std::filesystem::path candidate = cur / "resources" / "pgg";
        if (std::filesystem::is_directory(candidate / "lib", ec)) {
            const std::filesystem::path canon = std::filesystem::weakly_canonical(candidate, ec);
            return (ec ? candidate : canon).string();
        }
        const std::filesystem::path parent = cur.parent_path();
        if (parent == cur) break;
        cur = parent;
    }
    return {};
}

}  // namespace

std::string findProductLibRoot(const std::string& from) {
    if (std::string s = walkProductLib(from); !s.empty()) return s;
    std::error_code ec;
    return walkProductLib(std::filesystem::current_path(ec));
}

void appendImportRoot(std::vector<std::string>& roots, const std::string& root) {
    if (root.empty()) return;
    std::error_code ec;
    const std::string wantCanon = std::filesystem::weakly_canonical(root, ec).string();
    const std::string& want = ec ? root : wantCanon;
    for (const std::string& r : roots) {
        const std::string rk = std::filesystem::weakly_canonical(r, ec).string();
        if ((ec ? r : rk) == want) return;
    }
    roots.push_back(root);
}

}  // namespace pgg
