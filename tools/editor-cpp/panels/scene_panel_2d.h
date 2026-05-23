#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../core/asset_cache.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "panel.h"

namespace editor {

// 2D viewport panel. M6: FBO + ortho kamera + grid + MMB pan + scroll zoom.
// M7: SpriteRenderer render. TilemapRef M7b-ben.
class Scene2DPanel : public Panel {
public:
    Scene2DPanel();
    ~Scene2DPanel();
    void draw(EditorApp& app) override;

private:
    void ensureFbo(int w, int h);
    void renderScene(int w, int h, bool inputAllowed, EditorApp& app);
    void walkAndRender(obj* node, EditorApp& app);
    void renderSpriteNode(obj* node, EditorApp& app);
    void renderTilemapNode(obj* node, EditorApp& app);

    fbo_t    fbo_{};
    camera_t cam_{};
    int      width_  = 0;
    int      height_ = 0;

    std::unordered_map<std::string, texture_t> textureCache_;
    std::unordered_map<std::string, tiled_t>   tilemapCache_;
    AssetMtimes                                textureMtimes_;
    AssetMtimes                                tilemapMtimes_;
    FailedPathSet                              failedPaths_;

    // Gizmo drag → transaction edge-detection (M9b).
    bool        wasUsingGizmo_ = false;
    obj*        gizmoTarget_ = nullptr;
    std::string gizmoSnapshotBefore_;
};

}  // namespace editor
