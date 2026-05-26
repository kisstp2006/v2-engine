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

// Game viewport panel. M14: renders through the active CameraRef; if there is no
// CameraRef with `is_active=true`, shows a "No Camera" placeholder.
class GamePanel : public Panel {
public:
    GamePanel() : Panel("game", "Game") {}
    ~GamePanel();
    void draw(EditorApp& app) override;

private:
    void ensureFbo(int w, int h);
    void renderWithCamera(obj* cameraNode, int w, int h, EditorApp& app);
    // walkAndRenderMeshes + renderMeshNode_ + shadow pass moved to
    // RenderSystem3D (Refaktor F4 steps #2-#4).
    // Resolve the scene's Skybox node into a cached skybox_t* (or nullptr).
    // `render_bg_out`: filled with the Skybox node's `render_background` flag
    // (audit Finding I — eliminates the duplicate editor_skybox_get call).
    skybox_t* resolveSkybox(EditorApp& app, int* render_bg_out = nullptr);

    fbo_t fbo_{};
    int   width_  = 0;
    int   height_ = 0;

    // Shadowmap state moved to RenderSystem3D (Refaktor F4 step #3).

    // Asset caches live in editor::AssetManager (Refaktor F1) — accessed via
    // app.assets(). No per-panel cache state remains here.

    // Perf parity with ScenePanel (see scene_panel.h for the full rationale).

    // RenderSystem3D — Refaktor F4. Owns the per-mesh draw loop, shadow
    // pass batch, shadowmap_t, MaterialOverrides apply cache, and the
    // scene-mutation bus subscription. Steps #2-#3 migrated; #4
    // (walkAndRenderMeshes) and #5 still pending.
    RenderSystem3D renderSystem3D_;

    // (5) Master frustum-cull toggle (panel toolbar). When off, every mesh
    //     renders regardless of visibility — debug "why isn't this on-screen?"
    bool frustum_cull_ = true;
};

}  // namespace editor
