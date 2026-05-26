// STL FIRST.
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_map>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "material_library.h"
#include "../persistence/material_asset_io.h"

namespace editor {

namespace fs = std::filesystem;

struct MaterialLibrary::Impl {
    struct Entry {
        material_t         mat{};
        fs::file_time_type mtime{};
        bool               loaded = false;
    };
    std::unordered_map<std::string, Entry> map;
};

MaterialLibrary& MaterialLibrary::instance() {
    static MaterialLibrary s;
    s.ensureImpl();
    return s;
}

void MaterialLibrary::ensureImpl() {
    if (!impl_) impl_ = new Impl();
}

material_t* MaterialLibrary::lookup(const std::string& assetPath,
                                    const std::string& projectRoot) {
    if (assetPath.empty()) return nullptr;
    ensureImpl();

    // Resolve relative → absolute for the filesystem mtime + load.
    fs::path abs(assetPath);
    if (!abs.is_absolute() && !projectRoot.empty()) {
        abs = fs::path(projectRoot) / assetPath;
    }
    std::error_code ec;
    fs::file_time_type now = fs::last_write_time(abs, ec);

    // ---- Cache lookup -------------------------------------------------------
    // Three outcomes are cached:
    //   (a) successful load + mtime matches current → return the cached mat.
    //   (b) previous load FAILED at the same mtime → skip the retry. Without
    //       this branch, a missing or unreadable .mat.json5 (OneDrive online-
    //       only file, fs corruption, etc.) made every frame call loadMaterial
    //       again — 1000s of `file_read cannot find` log spam per second.
    //   (c) mtime changed → drop into the reload path below.
    auto it = impl_->map.find(assetPath);
    if (it != impl_->map.end() && it->second.mtime == now) {
        if (it->second.loaded) return &it->second.mat;
        return nullptr;  // previously-failed entry at same mtime — don't retry
    }

    // ---- (Re)load -----------------------------------------------------------
    // Normalize to forward-slash for the motor's path-resolve (DIR_SEP '/').
    // Mixed/back-slash abs paths fail file_read on some Windows configurations
    // even when the file exists (notably OneDrive Documents folders).
    std::string abs_str = abs.generic_string();

    Impl::Entry e;
    if (!material_asset_io::loadMaterial(abs_str, projectRoot, &e.mat)) {
        // Cache the FAILED outcome so subsequent lookups skip the retry until
        // either the mtime changes or the cache is cleared.
        Impl::Entry fail_e;
        fail_e.loaded = false;
        fail_e.mtime  = now;
        impl_->map.insert_or_assign(assetPath, std::move(fail_e));
        return nullptr;
    }
    e.mtime  = now;
    e.loaded = true;
    auto ins = impl_->map.insert_or_assign(assetPath, std::move(e));
    return &ins.first->second.mat;
}

void MaterialLibrary::clear() {
    if (impl_) impl_->map.clear();
}

}  // namespace editor
