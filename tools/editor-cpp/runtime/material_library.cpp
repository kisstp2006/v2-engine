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
    if (ec) return nullptr;

    auto it = impl_->map.find(assetPath);
    if (it != impl_->map.end() && it->second.loaded && it->second.mtime == now) {
        return &it->second.mat;
    }

    Impl::Entry e;
    if (!material_asset_io::loadMaterial(abs.string(), projectRoot, &e.mat)) {
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
