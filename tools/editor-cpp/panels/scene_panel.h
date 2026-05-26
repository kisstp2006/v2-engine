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
#include "../render/render_system_3d.h"

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
    // walkAndRender + renderMeshNode + shadow pass moved to RenderSystem3D
    // (Refaktor F4 steps #2-#4).
    // Resolve the scene's Skybox node into a cached skybox_t* (or nullptr).
    // `render_bg_out`: filled with the Skybox node's `render_background` flag
    // so the caller can decide whether to draw the sky as a background quad
    // (audit Finding I — eliminates the duplicate editor_skybox_get call).
    skybox_t* resolveSkybox(EditorApp& app, int* render_bg_out = nullptr);

    fbo_t    fbo_{};
    camera_t cam_{};
    int      width_  = 0;
    int      height_ = 0;

    // Asset caches (modelCache_, skyboxCache_, modelMtimes_, failedPaths_,
    // pathCache_) live in editor::AssetManager (Refaktor F1) — accessed via
    // app.assets(). No per-panel cache state remains here.

    // Shadowmap state moved to RenderSystem3D (Refaktor F4 step #3).

    // Gizmo drag → transaction edge-detection (M9b).
    bool        wasUsingGizmo_ = false;
    obj*        gizmoTarget_ = nullptr;
    std::string gizmoSnapshotBefore_;

    // Per-frame mesh counters (MeshCount, UniqueModels) moved to
    // RenderSystem3D (Refaktor F4 step #4). Published as Editor.Render.* —
    // aggregated across both viewports.

    // Per-node relPath→absPath cache lives in AssetManager (Refaktor F1) —
    // app.assets().absPathFor(node, relPath) does the same work as the old
    // pathCache_ + asset_path::toAbsolute pair.

    // RenderSystem3D — Refaktor F4. Owns the per-mesh draw loop, shadow
    // pass batch, shadowmap_t, MaterialOverrides apply cache, and the
    // scene-mutation bus subscription. Steps #2-#3 migrated; #4 (walkAndRender)
    // and #5 (collectLights + resolveSkybox) still pending.
    RenderSystem3D renderSystem3D_;

    // Master frustum-cull toggle (panel toolbar). When off, every mesh
    // renders regardless of visibility — useful for debugging "why isn't
    // my mesh showing up?" issues. Per-MeshRenderer `cull_mode` is the
    // finer-grained override, but this is the global kill switch.
    bool frustum_cull_ = true;
};

}  // namespace editor
