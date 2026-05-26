#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/asset_cache.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "panel.h"
#include "../render/shadow_batch.h"

namespace editor {

// Game viewport panel. M14: renders through the active CameraRef; if there is no
// CameraRef with `is_active=true`, shows a "No Camera" placeholder.
class GamePanel : public Panel {
public:
    GamePanel() : Panel("game", "Game") {}
    ~GamePanel();
    void draw(EditorApp& app) override;

private:
    void ensureFbo(int w, int h);
    obj* findActiveCamera(obj* node);
    void renderWithCamera(obj* cameraNode, int w, int h, EditorApp& app);
    void collectLights(obj* node, std::vector<light_t>& out);
    void walkAndRenderMeshes(obj* node, EditorApp& app, camera_t& cam,
                             const std::vector<light_t>& lights,
                             obj* fogNode, skybox_t* sky);
    // M16 — shadowmap shadow-caster pass. Walks the scene tree and re-renders
    // every cached MeshRenderer through model's RENDER_PASS_SHADOW path. Not-yet-
    // cached models are silently skipped (next frame's main pass loads them).
    void renderMeshShadowOnly(obj* node, camera_t& cam, EditorApp& app);
    void walkShadowPass(obj* node, camera_t& cam, EditorApp& app);
    // Per-mesh setup + draw helper — extracted from walkAndRenderMeshes
    // so the opaque pass and the deferred transparent pass can share the
    // exact same per-frame uniform setup + model_render call.
    void renderMeshNode_(obj* m, EditorApp& app, camera_t& cam,
                         const std::vector<light_t>& lights,
                         obj* fogNode, skybox_t* sky, model_t* model);
    // Resolve the scene's Skybox node into a cached skybox_t* (or nullptr).
    skybox_t* resolveSkybox(EditorApp& app);

    fbo_t fbo_{};
    int   width_  = 0;
    int   height_ = 0;

    // Shadowmap (lazy-init on first frame with at least one shadow caster).
    shadowmap_t sm_{};
    bool        sm_init_ = false;

    std::unordered_map<std::string, model_t>   modelCache_;
    std::unordered_map<std::string, texture_t> textureCache_;
    std::unordered_map<std::string, tiled_t>   tilemapCache_;
    std::unordered_map<std::string, skybox_t>  skyboxCache_;
    std::unordered_map<std::string, uint64_t>  skyboxMtimes_;
    AssetMtimes                                modelMtimes_;
    FailedPathSet                              failedPaths_;

    // Perf parity with ScenePanel (see scene_panel.h for the full rationale):
    //
    // (1) Per-node relPath -> absPath cache. asset_path::toAbsolute calls
    //     fs::weakly_canonical() which is a ~170 us Windows syscall on every
    //     mesh on every frame without this. Auto-invalidated on relPath
    //     mismatch.
    struct PathCacheEntry { std::string rel; std::string abs; };
    std::unordered_map<obj*, PathCacheEntry> pathCache_;

    // (2) Flat node lists, rebuilt only on tree mutation, iterated every
    //     frame instead of the recursive child_count + child_at chain.
    //     ~2-3 ms / frame saved in non-trivial scenes.
    bool                busWired_       = false;
    bool                flatListsDirty_ = true;
    std::vector<obj*>   meshNodes_;
    std::vector<obj*>   lightNodes_;
    void wireBusIfNeeded_(EditorApp& app);
    void rebuildFlatLists_(obj* root);

    // (3) MaterialOverrides apply cache. The motor's per-mesh material_t
    //     struct-copy (~1.5 KB) + bit-mask overlay runs per mesh per frame
    //     unless we skip when nothing edited. Cleared on scene mutation.
    std::unordered_set<obj*> overridesApplied_;

    // (4) Editor-side shadow batch (Plan B Phase 1). Replaces the 72 per-
    //     mesh model_render calls per cubemap face with one shader_bind +
    //     N fast draws. The single biggest CPU saving for shadow-on scenes
    //     (~40 ms / frame on a 12-mesh point-light scene).
    ShadowBatch shadow_batch_;

    // (5) Master frustum-cull toggle (panel toolbar). When off, every mesh
    //     renders regardless of visibility — debug "why isn't this on-screen?"
    bool frustum_cull_ = true;
};

}  // namespace editor
