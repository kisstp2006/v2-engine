// STL FIRST.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../core/asset_cache.h"
#include "../core/asset_path.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif
#include <cimguizmo/cimguizmo.h>

#include "scene_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../commands/command.h"
#include "../components/components_api.h"
#include "../core/selection_service.h"
#include "../runtime/script_host.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

ScenePanel::ScenePanel() : Panel("scene", "Scene") {
    cam_ = camera();
}

ScenePanel::~ScenePanel() {
    if (fbo_.id) {
        fbo_destroy(fbo_);
        fbo_ = fbo_t{};
    }
    if (sm_init_) {
        shadowmap_destroy(&sm_);
        sm_init_ = false;
    }
    for (auto& kv : skyboxCache_) skybox_destroy(&kv.second);
    skyboxCache_.clear();
    skyboxMtimes_.clear();
}

void ScenePanel::ensureFbo(int w, int h) {
    if (w == width_ && h == height_ && fbo_.id) return;
    if (fbo_.id) fbo_destroy(fbo_);
    fbo_ = fbo((unsigned)w, (unsigned)h, 0, 0);
    width_  = w;
    height_ = h;
}

namespace {

inline vec3 mkv3(float x, float y, float z) {
    vec3 v; v.x = x; v.y = y; v.z = z; return v;
}

// Replica of the motor's camera_freefly without the `ui_hovered() || ui_active()`
// check — because the Scene panel's ImGui::Image is itself a hovered widget, and
// that would always cause the motor freefly to block the camera. We
// determine the `blocked` parameter ourselves at the panel level.
void editorFreefly(camera_t* cam, bool blocked) {
    cam->damping = true;
    bool active = !blocked && (input(MOUSE_L) || input(MOUSE_M) || input(MOUSE_R));
    // Phase 3e — ESC always releases the cursor from freefly (cursor-stuck
    // on focus-loss / Alt+Tab). The run-loop ESC-quit has been removed.
    if (input(KEY_ESC)) active = false;
    mouse_show(!active);
    int mult_speed = input(KEY_LSHIFT) || input(KEY_LALT);

    if (active) {
        cam->speed = clampf(cam->speed + input_diff(MOUSE_W) * 0.1f, 0.05f, 5.0f);
    }

    float mx =  input_diff(MOUSE_X) * 0.2f * (active ? 1.0f : 0.0f);
    float my = -input_diff(MOUSE_Y) * 0.2f * (active ? 1.0f : 0.0f);

    float a = active ? 1.0f : 0.0f;
    float wx = (input(KEY_D) - input(KEY_A)) * cam->speed * a;
    float wy = (input(KEY_E) - (input(KEY_C) || input(KEY_Q))) * cam->speed * a;
    float wz = (input(KEY_W) - input(KEY_S)) * cam->speed * a;

    float wlen2 = wx*wx + wy*wy + wz*wz;
    if (wlen2 > 0.0f) {
        cam->speed_buildup +=
            cam->speed * cam->accel * (2.0f * mult_speed + 1.0f) * app_delta();
    } else {
        cam->speed_buildup = 1.0f;
    }

    float k = app_delta() * 60.0f * cam->speed_buildup * (2.0f * mult_speed + 1.0f);
    camera_moveby(cam, mkv3(wx * k, wy * k, wz * k));
    camera_fps(cam, mx, my);
}

}  // namespace

// Depth-first lookup of the first FogSettings in the scene (NULL if none).
static obj* findFogSettings(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_fog_settings(node)) return node;
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findFogSettings(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

static obj* findSkyboxNode(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_skybox(node)) return node;
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findSkyboxNode(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

skybox_t* ScenePanel::resolveSkybox(EditorApp& app) {
    obj* skyNode = findSkyboxNode(app.scene().root());
    if (!skyNode) return nullptr;

    const char *skyPath = nullptr, *reflPath = nullptr, *envPath = nullptr;
    int render_bg = 1;
    editor_skybox_get(skyNode, &skyPath, &reflPath, &envPath, &render_bg);
    if (!skyPath || !*skyPath) return nullptr;

    std::string absSky = asset_path::toAbsolute(skyPath, app.projectPath());
    if (!is_file(absSky.c_str())) return nullptr;

    uint64_t mt_now = mtimeNs(absSky);
    auto mt_it = skyboxMtimes_.find(absSky);
    auto it    = skyboxCache_.find(absSky);
    if (it != skyboxCache_.end() && mt_it != skyboxMtimes_.end() && mt_it->second != mt_now) {
        skybox_destroy(&it->second);
        skyboxCache_.erase(it);
        it = skyboxCache_.end();
    }
    if (it == skyboxCache_.end()) {
        std::string absRefl = (reflPath && *reflPath)
                            ? asset_path::toAbsolute(reflPath, app.projectPath()) : absSky;
        std::string absEnv  = (envPath && *envPath)
                            ? asset_path::toAbsolute(envPath, app.projectPath()) : absSky;
        skybox_t sky = skybox(absSky.c_str(), absRefl.c_str(), absEnv.c_str());
        auto ins = skyboxCache_.emplace(absSky, sky);
        it = ins.first;
        skyboxMtimes_[absSky] = mt_now;
    }
    return &it->second;
}

void ScenePanel::renderMeshNode(obj* node, EditorApp& app,
                                const std::vector<light_t>& lights,
                                obj* fogNode, skybox_t* sky) {
    const char* relPath = editor_mesh_renderer_path(node);
    if (!relPath || !*relPath) return;

    // Phase 4a — render-time abs-resolve. The stored path is project-relative,
    // but the motor's `model()` / `is_file()` expect absolute. The cache-key is abs.
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    const std::string& path = absPath;

    // FailedPaths timeout — if the user replaced the file on disk,
    // retry after 2 seconds. Mtime-poll: if the file's mtime has changed
    // (e.g. a new .iqm), cache-evict.
    if (failedPaths_.isFresh(path)) return;
    failedPaths_.erase(path);
    if (!is_file(path.c_str())) {
        failedPaths_.insert(path);
        app.bus().emit("log", std::string("[Mesh] file not found: ") + relPath);
        return;
    }

    uint64_t mt_now = mtimeNs(path);
    auto mt_it = modelMtimes_.find(path);
    if (mt_it != modelMtimes_.end() && mt_it->second != mt_now) {
        modelCache_.erase(path);
        app.bus().emit("log", std::string("[Mesh] reloaded (mtime): ") + relPath);
    }

    auto it = modelCache_.find(path);
    if (it == modelCache_.end()) {
        model_t mt = model(path.c_str(), 0);
        if (!mt.iqm) {
            failedPaths_.insert(path);
            app.bus().emit("log", std::string("[Mesh] load failed: ") + relPath);
            return;
        }
        auto ins = modelCache_.emplace(path, mt);
        it = ins.first;
        modelMtimes_[path] = mt_now;
        app.bus().emit("log", std::string("[Mesh] loaded: ") + relPath);
    }

    // Feed the lights + shadowmap to the model before render
    // (`model_light/shadow` expects a non-const pointer).
    if (!lights.empty()) {
        model_light(&it->second, (unsigned)lights.size(),
                    const_cast<light_t*>(lights.data()));
    } else {
        model_light(&it->second, 0, nullptr);
    }
    // Shadowmap pipeline disabled → we always pass nullptr.
    model_shadow(&it->second, nullptr);
    (void)lights;

    // Per-frame fog uniforms. shader2_adduniforms dedupes on type_name so this
    // doesn't grow the model's uniform array.
    if (fogNode) {
        int mode = 0; vec3 color = {0,0,0};
        float start = 0.f, end = 1.f, density = 0.f;
        editor_fog_settings_get(fogNode, &mode, &color, &start, &end, &density);
        model_fog(&it->second, (unsigned)mode, color, start, end, density);
    } else {
        // No scene-wide FogSettings → explicitly disable fog (the model may
        // have been cached with a previous setup).
        vec3 black = {0,0,0};
        model_fog(&it->second, 0u, black, 0.f, 1.f, 0.f);
    }
    // Skybox / IBL — bind the resolved skybox_t to the model (empty if no
    // Skybox node, which resets the PBR shader's HAS_TEX_SKY* uniforms).
    if (sky) {
        model_skybox(&it->second, *sky);
    } else {
        skybox_t empty = {0};
        model_skybox(&it->second, empty);
    }

    mat44 pivot;
    editor_mesh_renderer_compose_pivot(node, pivot);
    // Skinned-glTF Y-flip compensation: the loader leaves the vertex in
    // glTF-native Y-up so the skinning math is clean, and we apply the
    // Y-flip here on the model pivot instead.
    if (it->second.flags & MODEL_GLTF_SKINNED) {
        mat44 yflip; id44(yflip); yflip[5] = -1.0f;
        mat44 tmp; multiply44x2(tmp, pivot, yflip);
        memcpy(pivot, tmp, sizeof(mat44));
    }
    // pass = -1 → every default pass (lighting, shading, shadow-sampling).
    model_render(&it->second, cam_.proj, cam_.view, &pivot, 1, -1);
}

void ScenePanel::renderMeshShadowOnly(obj* node, EditorApp& app) {
    const char* relPath = editor_mesh_renderer_path(node);
    if (!relPath || !*relPath) return;
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    if (failedPaths_.isFresh(absPath)) return;
    auto it = modelCache_.find(absPath);
    if (it == modelCache_.end()) return;   // model not yet cached
    mat44 pivot;
    editor_mesh_renderer_compose_pivot(node, pivot);
    model_render(&it->second, cam_.proj, cam_.view, &pivot, 1,
                 RENDER_PASS_SHADOW);
}

void ScenePanel::walkShadowPass(obj* node, EditorApp& app) {
    if (!node) return;
    if (editor_obj_is_mesh_renderer(node)) {
        renderMeshShadowOnly(node, app);
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        walkShadowPass(editor_obj_child_at(node, i), app);
    }
}

void ScenePanel::collectLights(obj* node, std::vector<light_t>& out) {
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

void ScenePanel::walkAndRender(obj* node, EditorApp& app,
                               const std::vector<light_t>& lights,
                               obj* fogNode, skybox_t* sky) {
    if (!node) return;
    if (editor_obj_is_mesh_renderer(node)) {
        renderMeshNode(node, app, lights, fogNode, sky);
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        walkAndRender(editor_obj_child_at(node, i), app, lights, fogNode, sky);
    }
}

void ScenePanel::renderScene(int w, int h, bool inputAllowed, EditorApp& app) {
    editorFreefly(&cam_, !inputAllowed);

    fbo_bind(fbo_.id);
    glViewport(0, 0, w, h);
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_enable(&cam_);

    ddraw_grid(0);
    ddraw_flush();

    // Spatial audio listener pos = the Scene editor-camera position (in Play mode).
    float listenerPos[3] = { cam_.position.x, cam_.position.y, cam_.position.z };
    app.play().updateAudio(app, listenerPos);

    // 1) light gathering.
    std::vector<light_t> lights;
    collectLights(app.scene().root(), lights);

    // 2) shadowmap pass — DISABLED (motor pipeline crashes). The LightRef's
    // `cast_shadows` field is handed to the motor as hardcoded false by
    // `editor_light_ref_to_light_t`, so shadowmap_* doesn't run here either. Fix in M16+.

    // 3) main render pass — with lights + shadowmap + fog + skybox/IBL.
    obj* fogNode = findFogSettings(app.scene().root());

    // Skybox: resolve from the scene's Skybox node (cached + mtime-poll), render
    // background if its render_background flag is on.
    skybox_t* sky = resolveSkybox(app);
    if (sky) {
        obj* skyNode = findSkyboxNode(app.scene().root());
        int render_bg = 1;
        if (skyNode) editor_skybox_get(skyNode, nullptr, nullptr, nullptr, &render_bg);
        if (render_bg) skybox_render(sky, cam_.proj, cam_.view);
    }

    walkAndRender(app.scene().root(), app, lights, fogNode, sky);

    // 4) Script `on_draw` callbacks (only in Play-mode). We also show in the
    // editor Scene panel so that script-effects are visible immediately
    // even without a Camera-node. The ddraw_flush is needed so the script's
    // ddraw_* calls actually render into the FBO.
    if (app.play().isPlaying()) {
        app.scriptHost().drawAll();
        ddraw_flush();
    }

    fbo_unbind();
}

void ScenePanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x;
    int h = (int)avail.y;

    // Disable the freefly camera during gizmo-drag.
    const bool gizmoBusy = ImGuizmo_IsUsing();
    const bool inputAllowed = (ImGui::IsWindowHovered() ||
                               ImGui::IsWindowFocused()) && !gizmoBusy;

    if (w > 0 && h > 0) {
        ensureFbo(w, h);
        // Trap-window: the motor's `igText("uniform ... not found")` warnings
        // would write into the current ImGui-window (the Scene). We redirect them
        // into an off-screen trap so they don't get in the way.
        ImGui::SetNextWindowPos(ImVec2(-9999, -9999));
        ImGui::SetNextWindowSize(ImVec2(1, 1));
        ImGui::Begin("##fbo-render-trap-scene", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        renderScene(w, h, inputAllowed, app);
        ImGui::End();

        // The OpenGL texture v-coord starts at the top, ImGui at the bottom → uv0/uv1 flip.
        ImGui::Image((ImTextureID)(uintptr_t)fbo_.texture_color.id,
                     ImVec2((float)w, (float)h),
                     ImVec2(0, 1), ImVec2(1, 0));

        // Drop target — 3D mesh assets (.iqm, .gltf, .glb).
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::string path((const char*)p->Data);
                std::string ext = std::filesystem::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (ext == ".iqm" || ext == ".gltf" || ext == ".glb") {
                    app.createMesh(path.c_str());
                } else if (ext == ".json5"
                           && path.find(".prefab.") != std::string::npos) {
                    app.spawnPrefab(path.c_str());
                } else if (ext == ".lua") {
                    app.createScript(path.c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Translate/Rotate/Scale gizmo overlay — ONLY for 3D components.
        // The gizmo for 2D components (Sprite, Tilemap) appears in the Scene 2D panel.
        obj* sel = app.selection().primary();
        vec3* posPtr   = (sel && editor_obj_is_3d_component(sel))
                         ? editor_obj_pos_addr(sel) : nullptr;
        vec3* rotPtr   = sel ? editor_obj_rot_addr(sel)   : nullptr;
        vec3* scalePtr = sel ? editor_obj_scale_addr(sel) : nullptr;
        if (posPtr) {
            ImVec2 imgPos  = ImGui::GetItemRectMin();
            ImVec2 imgSize = ImGui::GetItemRectSize();

            ImGuizmo_SetRect(imgPos.x, imgPos.y, imgSize.x, imgSize.y);
            ImGuizmo_SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo_SetOrthographic(false);

            // Pos + rot + scale model-matrix (M15: T/R/S all supported).
            float matrix[16];
            float t[3] = { posPtr->x, posPtr->y, posPtr->z };
            float r[3] = { rotPtr   ? rotPtr->x   : 0,
                           rotPtr   ? rotPtr->y   : 0,
                           rotPtr   ? rotPtr->z   : 0 };
            float s[3] = { scalePtr ? scalePtr->x : 1,
                           scalePtr ? scalePtr->y : 1,
                           scalePtr ? scalePtr->z : 1 };
            ImGuizmo_RecomposeMatrixFromComponents(t, r, s, matrix);

            IMGUIZMO_OPERATION op = (IMGUIZMO_OPERATION)app.gizmoOp();
            if (ImGuizmo_Manipulate(cam_.view, cam_.proj,
                                    op, IMGUIZMO_WORLD,
                                    matrix,
                                    nullptr, nullptr, nullptr, nullptr)) {
                float t2[3], r2[3], s2[3];
                ImGuizmo_DecomposeMatrixToComponents(matrix, t2, r2, s2);
                posPtr->x = t2[0];
                posPtr->y = t2[1];
                posPtr->z = t2[2];
                if (rotPtr) {
                    rotPtr->x = r2[0];
                    rotPtr->y = r2[1];
                    rotPtr->z = r2[2];
                }
                if (scalePtr) {
                    scalePtr->x = s2[0];
                    scalePtr->y = s2[1];
                    scalePtr->z = s2[2];
                }
            }
        }

        // Gizmo drag → transaction edge-detection.
        bool isUsing = ImGuizmo_IsUsing();
        if (isUsing && !wasUsingGizmo_) {
            // Drag start — snapshot before
            gizmoTarget_ = sel;
            gizmoSnapshotBefore_ = ObjectStateCommand::snapshot(sel);
        }
        if (!isUsing && wasUsingGizmo_) {
            // Drag end — snapshot after + execute (only if actually changed)
            std::string after = ObjectStateCommand::snapshot(gizmoTarget_);
            if (after != gizmoSnapshotBefore_) {
                app.commands().execute(std::make_unique<ObjectStateCommand>(
                    gizmoTarget_, gizmoSnapshotBefore_, after, "Move (Scene)"));
            }
            gizmoTarget_ = nullptr;
            gizmoSnapshotBefore_.clear();
        }
        wasUsingGizmo_ = isUsing;
    }

    ImGui::End();
}

REGISTER_PANEL(ScenePanel, 600)

}  // namespace editor
