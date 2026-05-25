#pragma once

// MaterialLibrary — central cache for `.mat.json5` assets shared between
// MeshRenderers. The render-walk calls `lookup(path, projectRoot)` every
// frame; we re-load only when the file's mtime changes (hot-reload).
//
// Pure singleton, lifetime = process. Returns a stable `material_t*` so the
// caller can memcpy from it directly (cache owns the storage; do NOT free).

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class MaterialLibrary {
public:
    static MaterialLibrary& instance();

    // Returns a pointer to the cached material, loading on first access (or
    // re-loading on mtime change). NULL if the asset doesn't exist or fails
    // to parse. The pointer is stable as long as `path` is identical;
    // re-lookups may invalidate other paths' pointers via cache resizing,
    // so callers should not hold the pointer across frames.
    material_t* lookup(const std::string& assetPath,
                       const std::string& projectRoot);

    // Drop all cached entries (used on scene-switch to avoid stale state).
    void clear();

private:
    MaterialLibrary() = default;
    struct Impl;
    Impl* impl_ = nullptr;
    void ensureImpl();
};

}  // namespace editor
