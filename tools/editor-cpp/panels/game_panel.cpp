// STL ELŐSZÖR.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/asset_cache.h"
#include "../core/asset_path.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "game_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../components/components_api.h"
#include "../runtime/script_host.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

GamePanel::~GamePanel() {
    if (fbo_.id) {
        fbo_destroy(fbo_);
        fbo_ = fbo_t{};
    }
}

void GamePanel::ensureFbo(int w, int h) {
    if (w == width_ && h == height_ && fbo_.id) return;
    if (fbo_.id) fbo_destroy(fbo_);
    fbo_ = fbo((unsigned)w, (unsigned)h, 0, 0);
    width_  = w;
    height_ = h;
}

obj* GamePanel::findActiveCamera(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_camera_ref(node) && editor_camera_ref_is_active(node)) {
        return node;
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findActiveCamera(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

void GamePanel::collectLights(obj* node, std::vector<light_t>& out) {
    if (!node) return;
    if (editor_obj_is_light_ref(node)) {
        light_t l;
        editor_light_ref_to_light_t(node, &l);
        out.push_back(l);
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectLights(editor_obj_child_at(node, i), out);
    }
}

void GamePanel::walkAndRenderMeshes(obj* node, EditorApp& app, camera_t& cam,
                                    const std::vector<light_t>& lights) {
    if (!node) return;
    if (editor_obj_is_mesh_renderer(node)) {
        const char* relPath = editor_mesh_renderer_path(node);
        std::string absPath = (relPath && *relPath)
            ? asset_path::toAbsolute(relPath, app.projectPath())
            : std::string();
        const std::string& path = absPath;
        if (!path.empty() && !failedPaths_.isFresh(path) && is_file(path.c_str())) {
            failedPaths_.erase(path);
            uint64_t mt_now = mtimeNs(path);
            auto mt_it = modelMtimes_.find(path);
            if (mt_it != modelMtimes_.end() && mt_it->second != mt_now) {
                modelCache_.erase(path);
            }
            auto it = modelCache_.find(path);
            if (it == modelCache_.end()) {
                model_t mt = model(path.c_str(), 0);
                if (mt.iqm) {
                    auto ins = modelCache_.emplace(path, mt);
                    it = ins.first;
                    modelMtimes_[path] = mt_now;
                } else {
                    failedPaths_.insert(path);
                }
            }
            if (it != modelCache_.end()) {
                if (!lights.empty()) {
                    model_light(&it->second, (unsigned)lights.size(),
                                const_cast<light_t*>(lights.data()));
                } else {
                    model_light(&it->second, 0, nullptr);
                }
                model_shadow(&it->second, nullptr);
                mat44 pivot;
                editor_mesh_renderer_compose_pivot(node, pivot);
                model_render(&it->second, cam.proj, cam.view, &pivot, 1, -1);
            }
        }
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        walkAndRenderMeshes(editor_obj_child_at(node, i), app, cam, lights);
    }
}

void GamePanel::renderWithCamera(obj* cameraNode, int w, int h, EditorApp& app) {
    // CameraRef params → camera_t.
    vec3 pos, dir;
    float fov, nclip, fclip;
    editor_camera_ref_get_params(cameraNode, &pos, &dir, &fov, &nclip, &fclip);

    camera_t cam = camera();
    cam.position = pos;
    cam.fov = fov;
    cam.near_clip = nclip;
    cam.far_clip = fclip;
    vec3 target;
    target.x = pos.x + dir.x;
    target.y = pos.y + dir.y;
    target.z = pos.z + dir.z;
    camera_lookat(&cam, target);

    fbo_bind(fbo_.id);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    camera_enable(&cam);

    std::vector<light_t> lights;
    collectLights(app.scene().root(), lights);
    walkAndRenderMeshes(app.scene().root(), app, cam, lights);

    // Script on_draw — itt az FBO és kamera már bekötve, így a Lua `C.ddraw_*`
    // / `C.model_render` ide rajzol. A ddraw_flush kell az aktuális buffer
    // FBO-ra renderéséhez.
    if (app.play().isPlaying()) {
        app.scriptHost().drawAll();
        ddraw_flush();
    }

    fbo_unbind();
}

void GamePanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    obj* cameraNode = findActiveCamera(app.scene().root());
    if (!cameraNode) {
        auto centerText = [](const char* text) {
            float w = ImGui::CalcTextSize(text).x;
            float availW = ImGui::GetContentRegionAvail().x;
            if (availW > w) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                                     + (availW - w) * 0.5f);
            }
            ImGui::TextDisabled("%s", text);
        };

        ImVec2 sz = ImGui::GetContentRegionAvail();
        float lineH = ImGui::GetTextLineHeightWithSpacing();
        // Függőleges centrálás (kb. 2 sornyi szöveg).
        ImGui::Dummy(ImVec2(0, (sz.y - 2 * lineH) * 0.5f));
        centerText("No active Camera in scene.");
        centerText("(GameObject \xE2\x86\x92 Camera \xE2\x86\x92 Camera)");
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x, h = (int)avail.y;
    if (w > 0 && h > 0) {
        ensureFbo(w, h);
        ImGui::SetNextWindowPos(ImVec2(-9999, -9999));
        ImGui::SetNextWindowSize(ImVec2(1, 1));
        ImGui::Begin("##fbo-render-trap-game", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        renderWithCamera(cameraNode, w, h, app);
        ImGui::End();
        ImGui::Image((ImTextureID)(uintptr_t)fbo_.texture_color.id,
                     ImVec2((float)w, (float)h),
                     ImVec2(0, 1), ImVec2(1, 0));
    }

    ImGui::End();
}

REGISTER_PANEL(GamePanel, 700)

}  // namespace editor
