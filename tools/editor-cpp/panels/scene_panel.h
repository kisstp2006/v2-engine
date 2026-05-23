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
                       const std::vector<light_t>& lights);
    void renderMeshNode(obj* node, EditorApp& app,
                        const std::vector<light_t>& lights);
    // Shadow-pass: render meshes to depth only (no lighting / shading).
    void walkShadowPass(obj* node, EditorApp& app);
    void renderMeshShadowOnly(obj* node, EditorApp& app);

    fbo_t    fbo_{};
    camera_t cam_{};
    int      width_  = 0;
    int      height_ = 0;

    std::unordered_map<std::string, model_t> modelCache_;
    AssetMtimes                              modelMtimes_;
    FailedPathSet                            failedPaths_;

    // Shadowmap (M12-bonus). Lazy init on the first render-frame.
    shadowmap_t sm_{};
    bool        sm_init_ = false;

    // Gizmo drag → transaction edge-detection (M9b).
    bool        wasUsingGizmo_ = false;
    obj*        gizmoTarget_ = nullptr;
    std::string gizmoSnapshotBefore_;
};

}  // namespace editor
