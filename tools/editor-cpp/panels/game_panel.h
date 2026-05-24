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
                             obj* fogNode);

    fbo_t fbo_{};
    int   width_  = 0;
    int   height_ = 0;

    std::unordered_map<std::string, model_t>   modelCache_;
    std::unordered_map<std::string, texture_t> textureCache_;
    std::unordered_map<std::string, tiled_t>   tilemapCache_;
    AssetMtimes                                modelMtimes_;
    FailedPathSet                              failedPaths_;
};

}  // namespace editor
