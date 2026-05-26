// STL FIRST.
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../core/asset_path.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif
#include <cimguizmo/cimguizmo.h>

#include "scene_panel.h"
#include "../render/framebuffer.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../commands/command.h"
#include "../components/components_api.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/profile_scope.h"
#include "../core/selection_service.h"
#include "../persistence/material_override_io.h"
#include "../runtime/script_host.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

ScenePanel::ScenePanel() : Panel("scene", "Scene") {
    cam_ = camera();
}

ScenePanel::~ScenePanel() {
    framebuffer_destroy(fbo_, width_, height_);
    // Shadowmap cleanup moved to RenderSystem3D dtor (Refaktor F4 step #3).
    // Skybox cleanup runs in AssetManager dtor (shared with GamePanel).
}

void ScenePanel::ensureFbo(int w, int h) {
    framebuffer_ensure(fbo_, width_, height_, w, h);
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

// Singleton-node lookups (FogSettings, Skybox, ShadowSettings, etc.) moved
// to editor::SceneQuery (Refaktor F3) — one cached DFS per scene mutation
// instead of three separate walks per frame.

static obj* findPostFXStack(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_postfx_stack(node)) return node;
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findPostFXStack(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

static bool isFXActive(obj* fxNode) {
    if (!fxNode) return false;
    int enabled = 0;
    editor_postfx_stack_get(fxNode, &enabled, nullptr);
    return enabled != 0;
}

// HUD-overlay helpers — duplicated in game_panel.cpp; small enough that a
// shared header would be over-engineered for two call sites.
static void collectTextRenderers_scene(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    if (editor_obj_is_text_renderer(node)) out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectTextRenderers_scene(editor_obj_child_at(node, i), out);
    }
}

static void textRendererPrefix_scene(int face, int color, int size, char out[8]) {
    int i = 0;
    if (face  >= 1 && face  <= 5) out[i++] = (char)(0x10 + face - 1);
    if (color >= 1 && color <= 6) out[i++] = (char)(0x1A + color - 1);
    if (size  >= 1 && size  <= 6) out[i++] = (char)size;
    out[i] = 0;
}

static void drawTextOverlays_scene(obj* root) {
    std::vector<obj*> texts;
    collectTextRenderers_scene(root, texts);
    if (texts.empty()) return;

    ddraw_push_2d();
    for (obj* o : texts) {
        const char* text = nullptr;
        float pos[2] = {0,0};
        int   face = 1, color = 1, size = 4;
        float max_width = 0.f;
        editor_text_renderer_get(o, &text, pos, &face, &color, &size, &max_width);
        if (!text || !*text) continue;

        char prefix[8];
        textRendererPrefix_scene(face, color, size, prefix);
        char buf[2048];
        snprintf(buf, sizeof(buf), "%s%s", prefix, text);

        font_goto(pos[0], pos[1]);
        if (max_width > 0.f) {
            font_clip(buf, vec4(pos[0], pos[1], max_width, 1e6f));
        } else {
            font_print(buf);
        }
    }
    ddraw_pop_2d();
}

// ---- Text3DRenderer — world-space billboard text via ddraw_text -------------

static void collectText3DRenderers_scene(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    if (editor_obj_is_text_renderer_3d(node)) out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectText3DRenderers_scene(editor_obj_child_at(node, i), out);
    }
}

static void drawText3DOverlays_scene(obj* root) {
    std::vector<obj*> t3ds;
    collectText3DRenderers_scene(root, t3ds);
    if (t3ds.empty()) return;

    for (obj* o : t3ds) {
        const char* text = nullptr;
        float pos[3] = {0,0,0};
        float scale = 0.05f;
        unsigned color = 0xFFFFFFFFu;
        editor_text_renderer_3d_get(o, &text, pos, &scale, &color);
        if (!text || !*text) continue;

        ddraw_color(color);
        ddraw_text(vec3(pos[0], pos[1], pos[2]), scale, text);
    }
    ddraw_flush();
}

skybox_t* ScenePanel::resolveSkybox(EditorApp& app, int* render_bg_out) {
    obj* skyNode = app.sceneQuery().skybox(app.scene().root());
    if (!skyNode) return nullptr;

    const char *skyPath = nullptr, *reflPath = nullptr, *envPath = nullptr;
    int render_bg = 1;
    editor_skybox_get(skyNode, &skyPath, &reflPath, &envPath, &render_bg);
    if (render_bg_out) *render_bg_out = render_bg;
    if (!skyPath || !*skyPath) return nullptr;

    std::string absSky  = asset_path::toAbsolute(skyPath,  app.projectPath());
    std::string absRefl = (reflPath && *reflPath)
                        ? asset_path::toAbsolute(reflPath, app.projectPath()) : std::string{};
    std::string absEnv  = (envPath && *envPath)
                        ? asset_path::toAbsolute(envPath,  app.projectPath()) : std::string{};
    return app.assets().loadSkybox(absSky, absRefl, absEnv);
}

// renderMeshNode moved to editor::RenderSystem3D (Refaktor F4 step #4).
// The panel no longer needs a wrapper — renderSystem3D_.renderMainPass
// handles per-mesh load + frustum cull + transparency sort + draw.

// renderMeshShadowOnly + walkShadowPass moved to editor::RenderSystem3D
// (Refaktor F4 step #3). The panel now calls renderSystem3D_.renderShadowPass
// which drives the whole shadowmap_step loop internally.
#if 0
void ScenePanel::walkShadowPass(obj* node, EditorApp& app) {
    // Plan B Phase 1: batched shadow pass. Called once per cubemap face by
    // the shadowmap_step loop. We bind the shadow shader + face uniforms +
    // renderstate ONCE here, then loop meshNodes_ doing only the per-mesh
    // MODEL/MV uniforms + VAO bind + glDraw. The motor's `model_render`
    // overhead drops from ~500 μs / mesh to ~50 μs / mesh.
    //
    // FALLBACK: skinned-glTF meshes go through the old per-mesh path
    // because the batch doesn't handle the bone-UBO upload yet (Phase 3).
    (void)node;
    const auto& meshes = app.sceneQuery().meshNodes(app.scene().root());
    if (meshes.empty()) return;

    // Find a prototype model — the first compatible cached mesh. The
    // prototype provides the shadow shader handle + cached uniform_t
    // structs the batch uses. Refaktor F1: iterate the shared AssetManager
    // model cache; any loaded model with a compatible shadow shader works.
    model_t* proto = nullptr;
    for (const auto& kv : app.assets().models()) {
        // Cast away const for ShadowBatch (it only reads the model_t).
        model_t* candidate = const_cast<model_t*>(&kv.second);
        if (ShadowBatch::isCompatible(candidate)) {
            proto = candidate;
            break;
        }
    }
    if (!proto) {
        // Nothing compatible — fall back to the old per-mesh path.
        for (obj* m : meshes) renderMeshShadowOnly(m, app);
        return;
    }

    shadow_batch_.beginFace(proto, &sm_, cam_.proj, cam_.view);
    int batched = 0, fellback = 0, sh_culled = 0;
    // Per-face shadow-frustum cull. shadowmap_light updated sm_.shadow_frustum
    // for the CURRENT cubemap face before walkShadowPass was called, so the
    // sphere-vs-frustum test correctly skips meshes outside this face's view.
    // For 12 meshes x 6 faces, expect ~50-65% culled per face (each face
    // only covers ~1/6 of a sphere).
    const frustum sh_frustum = sm_.shadow_frustum;
    for (obj* m : meshes) {
        const char* relPath = editor_mesh_renderer_path(m);
        if (!relPath || !*relPath) continue;
        std::string absPath = app.assets().absPathFor(m, relPath);
        model_t* mp = app.assets().modelByAbsPath(absPath);
        if (!mp) continue;

        mat44 pivot;
        editor_mesh_renderer_compose_pivot(m, pivot);

        // Frustum cull. Same gates as the main pass: skinned + cull_mode=1
        // bypass cull (rest-pose bsphere is unreliable; sky/HUD must always
        // cast). `frustum_cull_` panel-toggle controls whether ANY cull runs.
        bool can_cull_sh = frustum_cull_ &&
                           editor_mesh_renderer_cull_mode(m) == 0 &&
                           !(mp->flags & MODEL_GLTF_SKINNED) &&
                           mp->num_joints == 0;
        if (can_cull_sh) {
            sphere bs = model_bsphere(*mp, pivot);
            if (!frustum_test_sphere(sh_frustum, bs)) {
                ++sh_culled;
                continue;
            }
        }

        if (ShadowBatch::isCompatible(mp)) {
            shadow_batch_.draw(mp, pivot);
            ++batched;
        } else {
            // Skinned / incompatible — slow path. Must end the batch first
            // because the per-mesh path may bind a different shader (the
            // skinned-glTF case rebinds the same shader_info[1] but goes
            // through model_render's full setup).
            shadow_batch_.endFace();
            if (mp->flags & MODEL_GLTF_SKINNED) {
                mat44 zrot180; id44(zrot180);
                zrot180[0] = -1.0f; zrot180[5] = -1.0f;
                mat44 tmp; multiply44x2(tmp, pivot, zrot180);
                memcpy(pivot, tmp, sizeof(mat44));
            }
            model_render(mp, cam_.proj, cam_.view, &pivot, 1,
                         RENDER_PASS_SHADOW);
            shadow_batch_.beginFace(proto, &sm_, cam_.proj, cam_.view);
            ++fellback;
        }
    }
    shadow_batch_.endFace();

    // Per-face counters — updated each face but only the LAST one ends
    // up in the EMA / counter slot. For 6 faces × identical mesh set,
    // they all see the same numbers, so this is fine for diagnostics.
    editor_profile_set_counter("Editor.Shadow.BatchPerFace",     (double)batched);
    editor_profile_set_counter("Editor.Shadow.FallbackPerFace",  (double)fellback);
    editor_profile_set_counter("Editor.Shadow.CulledPerFace",    (double)sh_culled);
}
#endif

// collectLights / rebuildFlatLists_ moved to editor::SceneQuery (Refaktor F3).
// renderScene inlines the LightRef → light_t conversion (5 lines, single
// call site). Tree-walk runs once per scene mutation, shared with GamePanel.
//
// MaterialOverrides apply cache + its bus subscription moved to
// editor::RenderSystem3D (Refaktor F4 step #2) — owned by the render system,
// auto-wired on first `renderMeshNode` call.

// walkAndRender moved to editor::RenderSystem3D::renderMainPass
// (Refaktor F4 step #4). Counters published as Editor.Render.* (aggregated
// across both viewports) — the Editor.Scene.MeshesRendered/Culled/Transparent
// names are gone; check Editor.Render.MeshesDrawn/Culled/Transparent instead.

void ScenePanel::renderScene(int w, int h, bool inputAllowed, EditorApp& app) {
    EDITOR_PROFILE("Editor.Scene.Total");

    // Per-frame counters (MeshCount, UniqueModels) reset + publish inside
    // RenderSystem3D::renderMainPass (Refaktor F4 step #4).

    editorFreefly(&cam_, !inputAllowed);

    fbo_bind(fbo_.id);
    glViewport(0, 0, w, h);
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_enable(&cam_);

    // Spatial audio listener pos = the Scene editor-camera position (in Play mode).
    float listenerPos[3] = { cam_.position.x, cam_.position.y, cam_.position.z };
    app.play().updateAudio(app, listenerPos);

    // 1) light gathering. Iterate the SceneQuery-cached LightRef nodes (Refaktor
    // F3) and convert each to a motor `light_t`. Tree walk happens at most
    // once per scene mutation, shared with GamePanel.
    std::vector<light_t> lights;
    {
        EDITOR_PROFILE("Editor.Scene.CollectLights");
        const auto& lightNodes = app.sceneQuery().lightNodes(app.scene().root());
        lights.reserve(lightNodes.size());
        for (obj* n : lightNodes) {
            light_t l;
            editor_light_ref_to_light_t(n, &l);
            lights.push_back(l);
        }
    }

    // 2) shadowmap pass — full lazy-init + ShadowSettings apply +
    // shadowmap_step loop + batched walk in RenderSystem3D (Refaktor F4
    // step #3). No-op if no light has cast_shadows=1. Debug counters
    // published from there too.
    // beginFrame builds the per-frame RenderableMesh list (model load + cache,
    // pivot compose w/ skinned flip, bsphere, transparent + shadow-compat
    // flags, camera dist²) ONCE. Shared with renderShadowPass + renderMainPass.
    renderSystem3D_.beginFrame(app, cam_);

    if (RenderSystem3D::hasAnyShadowCaster(lights)) {
        EDITOR_PROFILE("Editor.Scene.ShadowPass");
        renderSystem3D_.renderShadowPass(app, cam_, lights, frustum_cull_);
    }

    // 3) main render pass — with lights + shadowmap + fog + skybox/IBL.
    obj* fogNode = app.sceneQuery().fog(app.scene().root());

    // Skybox: resolve from the scene's Skybox node (cached + mtime-poll), render
    // background if its render_background flag is on. The flag comes back in
    // sky_render_bg (audit Finding I — eliminates the duplicate node fetch).
    int sky_render_bg = 1;
    skybox_t* sky = resolveSkybox(app, &sky_render_bg);

    // PostFX: same flow as game_panel — fx_begin/end wraps the world-render.
    // fbo_unbind() inside fx_end() pops back to fbo_.id via the motor's
    // bind-stack, so the FX-applied result lands in the editor FBO. Passing
    // 0/0 makes fx_end use its own internal color/depth as the first-pass
    // source (= the image we just drew with fx_begin's fbo bound).
    obj* fxNode = findPostFXStack(app.scene().root());
    const bool fx_active = isFXActive(fxNode);

    if (fx_active) fx_begin_res(w, h);

    // Grid is an editor world-element; it's expected to be affected by FX
    // (a Bloom pass should bloom the grid too), so it stays inside fx_begin.
    ddraw_grid(0);
    ddraw_flush();

    if (sky && sky_render_bg) skybox_render(sky, cam_.proj, cam_.view);

    {
        EDITOR_PROFILE("Editor.Scene.WalkAndRender");
        renderSystem3D_.renderMainPass(app, cam_, lights, fogNode, sky,
                                       frustum_cull_);
    }

    // 3D label pass — Text3DRenderer nodes drawn in world-space via ddraw_text.
    drawText3DOverlays_scene(app.scene().root());

    // 4) Script `on_draw` callbacks (only in Play-mode). We also show in the
    // editor Scene panel so that script-effects are visible immediately
    // even without a Camera-node. The ddraw_flush is needed so the script's
    // ddraw_* calls actually render into the FBO.
    if (app.play().isPlaying()) {
        app.scriptHost().drawAll();
        ddraw_flush();
    }

    if (fx_active) fx_end(0, 0);

    // HUD pass — TextRenderer nodes drawn in 2D viewport-pixel space.
    // Intentionally AFTER fx_end: a Bloom/CRT/etc effect on UI text usually
    // looks like a render bug, so the HUD overlay stays FX-free.
    drawTextOverlays_scene(app.scene().root());

    fbo_unbind();
    // Per-frame mesh counters published inside RenderSystem3D::renderMainPass
    // (Editor.Render.MeshesDrawn / .MeshesCulled / .MeshesTransparent /
    // .UniqueModels) — aggregated across both viewports.
}

void ScenePanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    // Toolbar row — debug toggles. Kept tight so the FBO area stays large.
    ImGui::Checkbox("Frustum Cull", &frustum_cull_);
    ImGui::SameLine();
    ImGui::TextDisabled("(uncheck to disable cull for debugging)");

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
