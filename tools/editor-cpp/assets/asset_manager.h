#pragma once

#include <string>
#include <unordered_map>

#include "../core/asset_cache.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class EventBus;

// AssetManager — centralized cache for runtime-loaded assets (model_t,
// texture_t, skybox_t, tiled_t). Refactor Phase 1: replaces per-panel cache
// duplication in scene_panel, game_panel, scene_panel_2d.
//
// Responsibilities:
//   - Path resolution (per-obj* cached rel→abs)
//   - Loading + mtime-poll auto-reload
//   - failedPaths timeout (avoid re-stat after a load failure)
//   - Bus events ([Mesh] loaded / reloaded / file not found / load failed)
//
// Lifetime: one instance per EditorApp, accessible via `app.assets()`.
// Thread: main-thread only (motor `model()` / `skybox()` etc. are GL).
class AssetManager {
public:
    AssetManager(EventBus& bus, const std::string& projectPath);
    ~AssetManager();

    AssetManager(const AssetManager&)            = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // --- Path resolve ---
    // Per-obj cached rel→abs. Auto-invalidates if relPath changes (e.g. the
    // Inspector edits MeshRenderer.model_path). Returns a stable reference
    // that remains valid until the next mutation of `pathCache_`.
    const std::string& absPathFor(obj* node, const std::string& relPath);

    // --- Model ---
    // Load-or-fetch. Returns nullptr if the asset is not on disk, recently
    // failed (within FailedPathSet timeout), or load() failed. `relPathForLog`
    // is used only in the bus log messages.
    model_t* loadModel(const std::string& absPath,
                       const std::string& relPathForLog);

    // Cache-only lookup. nullptr if not yet loaded. Used by shadow-pass walks
    // that already iterate the flat node list and want to skip the load logic.
    model_t* modelByAbsPath(const std::string& absPath);

    // --- Skybox ---
    // absSkyPath is the cache key. absReflPath/absEnvPath fall back to absSkyPath
    // if empty (the motor's skybox() builds reflection + env map from the same
    // cubemap if no separate path is given).
    skybox_t* loadSkybox(const std::string& absSkyPath,
                         const std::string& absReflPath,
                         const std::string& absEnvPath);

    // --- Texture ---
    texture_t* loadTexture(const std::string& absPath,
                           const std::string& relPathForLog);

    // --- Tilemap ---
    tiled_t* loadTilemap(const std::string& absPath,
                         const std::string& relPathForLog);

    // --- Bookkeeping ---
    // Clear per-node caches that may reference now-stale obj*s. Hooked to
    // kEvtSceneReplaced / kEvtSceneDirty by AssetManager itself (no manual
    // wiring needed from panels).
    void onSceneMutated();

    // --- Stats ---
    size_t modelCount()   const { return models_.size(); }
    size_t textureCount() const { return textures_.size(); }
    size_t skyboxCount()  const { return skyboxes_.size(); }
    size_t tilemapCount() const { return tilemaps_.size(); }

    // --- Direct map access (Phase 1 migration only) ---
    // Iterate the full model cache. Used by Plan B shadow batch prototype
    // lookup and shadow-pass walks. Will be replaced by explicit
    // shadow-prototype API once RenderSystem3D (F4) lands.
    const std::unordered_map<std::string, model_t>& models() const { return models_; }

private:
    EventBus*   bus_;
    std::string projectRoot_;

    std::unordered_map<std::string, model_t>   models_;
    std::unordered_map<std::string, texture_t> textures_;
    std::unordered_map<std::string, skybox_t>  skyboxes_;
    std::unordered_map<std::string, tiled_t>   tilemaps_;

    AssetMtimes   modelMtimes_;
    AssetMtimes   textureMtimes_;
    AssetMtimes   skyboxMtimes_;
    AssetMtimes   tilemapMtimes_;

    FailedPathSet failedPaths_;

    struct PathCacheEntry { std::string rel; std::string abs; };
    std::unordered_map<obj*, PathCacheEntry> pathCache_;

    // Wired to bus_ in the ctor — clears pathCache_/etc. on scene mutation.
    void wireBus_();
};

}  // namespace editor
