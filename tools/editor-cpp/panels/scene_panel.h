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

// 3D viewport panel. In M3: FBO + grid + static camera.
// M4: freefly camera + MeshRenderer render. M5: translate gizmo.
class ScenePanel : public Panel {
public:
    ScenePanel();
    ~ScenePanel();
    void draw(EditorApp& app) override;

private:
    void ensureFbo(int w, int h);
    void renderScene(int w, int h, bool inputAllowed, EditorApp& app);
    void collectLights(obj* node, std::vector<light_t>& out);
    void walkAndRender(obj* node, EditorApp& app,
                       const std::vector<light_t>& lights,
                       obj* fogNode, skybox_t* sky);
    void renderMeshNode(obj* node, EditorApp& app,
                        const std::vector<light_t>& lights,
                        obj* fogNode, skybox_t* sky);
    // Shadow-pass: render meshes to depth only (no lighting / shading).
    void walkShadowPass(obj* node, EditorApp& app);
    void renderMeshShadowOnly(obj* node, EditorApp& app);
    // Resolve the scene's Skybox node into a cached skybox_t* (or nullptr).
    skybox_t* resolveSkybox(EditorApp& app);

    fbo_t    fbo_{};
    camera_t cam_{};
    int      width_  = 0;
    int      height_ = 0;

    std::unordered_map<std::string, model_t>   modelCache_;
    std::unordered_map<std::string, skybox_t>  skyboxCache_;
    std::unordered_map<std::string, uint64_t>  skyboxMtimes_;
    AssetMtimes                                modelMtimes_;
    FailedPathSet                              failedPaths_;

    // Shadowmap (M12-bonus). Lazy init on the first render-frame.
    shadowmap_t sm_{};
    bool        sm_init_ = false;

    // Gizmo drag → transaction edge-detection (M9b).
    bool        wasUsingGizmo_ = false;
    obj*        gizmoTarget_ = nullptr;
    std::string gizmoSnapshotBefore_;

    // Per-frame mesh tracking — used by the Profiler to expose two counters
    // (`Editor.Scene.MeshCount` and `Editor.Scene.UniqueModels`). The ratio
    // between them tells the user whether instancing would help: many meshes
    // sharing a few unique paths → big instancing win; ~1:1 ratio → no win.
    // Cleared at the start of every renderScene; renderMesh inserts into it.
    std::unordered_set<std::string> frameModelPaths_;
    int                             frameMeshCount_ = 0;

    // Per-node (relPath → absPath) cache. asset_path::toAbsolute() calls
    // fs::weakly_canonical() which is a Windows syscall (~170 μs); doing
    // it every frame for every mesh costs ~2 ms / frame in non-trivial
    // scenes. Cache keyed by `obj*` (stable node id); the stored relPath
    // matches the source so we auto-invalidate when the Inspector edits
    // the MeshRenderer.model_path field — no manual eviction needed.
    struct PathCacheEntry { std::string rel; std::string abs; };
    std::unordered_map<obj*, PathCacheEntry> pathCache_;

    // Flat scene-node lists — rebuilt only on tree mutation, iterated
    // every frame instead of walking the obj-tree recursively. With ~42
    // total nodes the recursive walk was ~2.8 ms / frame (child_count +
    // child_at per node); a flat std::vector iteration drops that to
    // single-digit μs. Invalidated by kEvtSceneDirty + kEvtSceneReplaced.
    bool                busWired_         = false;
    bool                flatListsDirty_   = true;
    std::vector<obj*>   meshNodes_;
    std::vector<obj*>   lightNodes_;
    void wireBusIfNeeded_(EditorApp& app);
    void rebuildFlatLists_(obj* root);

    // MaterialOverrides apply cache. The apply does ~1.5 KB material_t
    // struct copy + bit-mask overlay PER MESH PER FRAME — ~1.5 ms total
    // in non-trivial scenes, even when nothing has changed. Set membership
    // == "node's overrides currently applied to its model_t". Cleared on
    // any scene-mutation event (kEvtSceneDirty et al.) → next frame
    // re-applies for every mesh.
    //
    // KNOWN LIMITATION: two MeshRenderer nodes sharing the same model_t
    // (same path) would clobber each other after first apply. The current
    // FNAF scene has UniqueModels == MeshCount (no duplicates) so this
    // doesn't trigger. For duplicate-mesh scenes we'd need per-(node, model)
    // tracking — kept as a TODO.
    std::unordered_set<obj*> overridesApplied_;

    // Plan B Phase 1: editor-side shadow batch. Replaces 72 per-mesh
    // model_render calls per frame with a single beginFace + N draws +
    // endFace per cubemap face. The motor's `model_render` has ~500 μs
    // fixed CPU overhead (analyseshader + shader2_apply + 30 uniform_set2
    // + renderstate_apply); the batch reduces it to ~50 μs / mesh by
    // setting all that once per face.
    ShadowBatch shadow_batch_;
};

}  // namespace editor
