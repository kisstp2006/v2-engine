#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

    // Asset caches (modelCache_, skyboxCache_, modelMtimes_, failedPaths_,
    // pathCache_) live in editor::AssetManager (Refaktor F1) — accessed via
    // app.assets(). No per-panel cache state remains here.

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

    // Per-node relPath→absPath cache lives in AssetManager (Refaktor F1) —
    // app.assets().absPathFor(node, relPath) does the same work as the old
    // pathCache_ + asset_path::toAbsolute pair.

    // Flat node lists + scene-mutation subscription moved to
    // editor::SceneQuery (Refaktor F3). One shared instance lives in
    // EditorApp::sceneQuery_; both Scene and Game panels read from it.
    // The only bus subscription that remains here is for
    // `overridesApplied_` — see wireBusIfNeeded_() below.
    bool busWired_ = false;
    void wireBusIfNeeded_(EditorApp& app);

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

    // Master frustum-cull toggle (panel toolbar). When off, every mesh
    // renders regardless of visibility — useful for debugging "why isn't
    // my mesh showing up?" issues. Per-MeshRenderer `cull_mode` is the
    // finer-grained override, but this is the global kill switch.
    bool frustum_cull_ = true;
};

}  // namespace editor
