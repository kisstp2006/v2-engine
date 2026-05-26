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

static obj* findShadowSettings(obj* node) {
    if (!node) return nullptr;
    if (editor_obj_is_shadow_settings(node)) return node;
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        obj* r = findShadowSettings(editor_obj_child_at(node, i));
        if (r) return r;
    }
    return nullptr;
}

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
    // OPTIMIZATION: asset_path::toAbsolute internally calls
    // fs::weakly_canonical() — a real Windows syscall (~170 μs each). We cache
    // the (relPath → absPath) leap per `obj*` and skip the syscall as long
    // as the source relPath hasn't changed. Auto-invalidating: a different
    // relPath string mismatches → we recompute.
    std::string absPath;
    {
        EDITOR_PROFILE("Editor.Scene.Mesh.PathResolve");
        auto pcIt = pathCache_.find(node);
        if (pcIt != pathCache_.end() && pcIt->second.rel == relPath) {
            absPath = pcIt->second.abs;
        } else {
            absPath = asset_path::toAbsolute(relPath, app.projectPath());
            pathCache_[node] = PathCacheEntry{relPath, absPath};
        }
    }
    const std::string& path = absPath;

    // FailedPaths timeout: if the user moved/deleted the file, we already
    // know it failed recently — bail without re-stat-ing.
    if (failedPaths_.isFresh(path)) return;

    // OPTIMIZATION: only stat the file when the cache has no entry. A path
    // that's already in modelCache_ was successfully loaded earlier, so
    // file existence is implicit. The mtime-poll below (inside CacheLookup)
    // is the canonical way to detect on-disk changes — and that's also a
    // single stat, not two. Previous code did is_file() + mtimeNs() EVERY
    // frame on EVERY mesh: 12 meshes × 2 stat() syscalls × 60fps ≈ 1440 stat/s
    // on Windows — costed ~3-4 ms per frame just to confirm files we
    // already loaded still exist.
    if (modelCache_.find(path) == modelCache_.end()) {
        EDITOR_PROFILE("Editor.Scene.Mesh.IsFileCheck");
        failedPaths_.erase(path);
        if (!is_file(path.c_str())) {
            failedPaths_.insert(path);
            app.bus().emit("log",
                std::string("[Mesh] file not found: ") + relPath);
            return;
        }
    } else {
        // Cached path → clear any stale failedPaths entry (idempotent).
        failedPaths_.erase(path);
    }

    // Per-frame mesh tracking for the Profiler. Insert AFTER the existence
    // gate so missing-file ghosts don't inflate the counts. We track BEFORE
    // any cache lookup or render work, so the count is "how many meshes
    // the walk actually attempted this frame".
    ++frameMeshCount_;
    frameModelPaths_.insert(path);

    // Cache lookup + mtime poll + (cold path) load. Counts how much we pay
    // just to acquire the cached model_t pointer per mesh per frame.
    std::unordered_map<std::string, model_t>::iterator it;
    {
        EDITOR_PROFILE("Editor.Scene.Mesh.CacheLookup");
        uint64_t mt_now = mtimeNs(path);
        auto mt_it = modelMtimes_.find(path);
        if (mt_it != modelMtimes_.end() && mt_it->second != mt_now) {
            modelCache_.erase(path);
            app.bus().emit("log", std::string("[Mesh] reloaded (mtime): ") + relPath);
        }

        it = modelCache_.find(path);
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
    }

    // Engine-side uniform bind: lights UBO + shadow texture + fog block +
    // skybox/IBL textures. These all happen BEFORE the actual draw call, and
    // each one fires a chain of `uniform_set2` + `shader2_adduniforms` work.
    // Tracked separately from ModelRender so we can tell whether the cost is
    // in the prep or in the GL draw itself.
    {
        EDITOR_PROFILE("Editor.Scene.Mesh.UniformPrep");
        if (!lights.empty()) {
            model_light(&it->second, (unsigned)lights.size(),
                        const_cast<light_t*>(lights.data()));
        } else {
            model_light(&it->second, 0, nullptr);
        }
        model_shadow(&it->second, sm_init_ ? &sm_ : nullptr);

        if (fogNode) {
            int mode = 0; vec3 color = {0,0,0};
            float start = 0.f, end = 1.f, density = 0.f;
            editor_fog_settings_get(fogNode, &mode, &color, &start, &end, &density);
            model_fog(&it->second, (unsigned)mode, color, start, end, density);
        } else {
            vec3 black = {0,0,0};
            model_fog(&it->second, 0u, black, 0.f, 1.f, 0.f);
        }
        if (sky) {
            model_skybox(&it->second, *sky);
        } else {
            skybox_t empty = {0};
            model_skybox(&it->second, empty);
        }
    }

    // Material overrides (Blokk 2.5) — per-slot asset-ref + inline overlay.
    // Cache: skip the ~1.5 KB struct-copy + name-match work if THIS node
    // already had its overrides applied to its model since the last scene
    // mutation. `overridesApplied_` is cleared by the bus handlers above
    // on any kEvtSceneDirty et al., so an Inspector edit re-triggers on
    // the next frame.
    if (overridesApplied_.find(node) == overridesApplied_.end()) {
        EDITOR_PROFILE("Editor.Scene.Mesh.MaterialOverrides");
        material_override_io::applyOverridesToModel(
            node, &it->second, app.projectPath());
        overridesApplied_.insert(node);
    }

    mat44 pivot;
    {
        EDITOR_PROFILE("Editor.Scene.Mesh.PivotCompose");
        editor_mesh_renderer_compose_pivot(node, pivot);
        if (it->second.flags & MODEL_GLTF_SKINNED) {
            mat44 zrot180; id44(zrot180); zrot180[0] = -1.0f; zrot180[5] = -1.0f;
            mat44 tmp; multiply44x2(tmp, pivot, zrot180);
            memcpy(pivot, tmp, sizeof(mat44));
        }
    }
    // pass = -1 → every default pass (lighting, shading, shadow-sampling).
    {
        EDITOR_PROFILE("Editor.Scene.Mesh.ModelRender");
        model_render(&it->second, cam_.proj, cam_.view, &pivot, 1, -1);
    }
}

void ScenePanel::renderMeshShadowOnly(obj* node, EditorApp& app) {
    const char* relPath = editor_mesh_renderer_path(node);
    if (!relPath || !*relPath) return;
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    if (failedPaths_.isFresh(absPath)) return;
    auto it = modelCache_.find(absPath);
    if (it == modelCache_.end()) return;   // model not yet cached (next frame)
    mat44 pivot;
    editor_mesh_renderer_compose_pivot(node, pivot);
    // Same skinned-glTF flip as the main render-walk, otherwise the shadow
    // silhouette wouldn't match the visible mesh.
    if (it->second.flags & MODEL_GLTF_SKINNED) {
        mat44 zrot180; id44(zrot180); zrot180[0] = -1.0f; zrot180[5] = -1.0f;
        mat44 tmp; multiply44x2(tmp, pivot, zrot180);
        memcpy(pivot, tmp, sizeof(mat44));
    }
    model_render(&it->second, cam_.proj, cam_.view, &pivot, 1,
                 RENDER_PASS_SHADOW);
}

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
    if (meshNodes_.empty()) return;

    // Find a prototype model — the first compatible cached mesh. The
    // prototype provides the shadow shader handle + cached uniform_t
    // structs the batch uses.
    model_t* proto = nullptr;
    for (obj* m : meshNodes_) {
        auto pcIt = pathCache_.find(m);
        if (pcIt == pathCache_.end()) continue;
        auto mcIt = modelCache_.find(pcIt->second.abs);
        if (mcIt == modelCache_.end()) continue;
        if (ShadowBatch::isCompatible(&mcIt->second)) {
            proto = &mcIt->second;
            break;
        }
    }
    if (!proto) {
        // Nothing compatible — fall back to the old per-mesh path.
        for (obj* m : meshNodes_) renderMeshShadowOnly(m, app);
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
    for (obj* m : meshNodes_) {
        auto pcIt = pathCache_.find(m);
        if (pcIt == pathCache_.end()) continue;
        auto mcIt = modelCache_.find(pcIt->second.abs);
        if (mcIt == modelCache_.end()) continue;

        mat44 pivot;
        editor_mesh_renderer_compose_pivot(m, pivot);

        // Frustum cull. Same gates as the main pass: skinned + cull_mode=1
        // bypass cull (rest-pose bsphere is unreliable; sky/HUD must always
        // cast). `frustum_cull_` panel-toggle controls whether ANY cull runs.
        bool can_cull_sh = frustum_cull_ &&
                           editor_mesh_renderer_cull_mode(m) == 0 &&
                           !(mcIt->second.flags & MODEL_GLTF_SKINNED) &&
                           mcIt->second.num_joints == 0;
        if (can_cull_sh) {
            sphere bs = model_bsphere(mcIt->second, pivot);
            if (!frustum_test_sphere(sh_frustum, bs)) {
                ++sh_culled;
                continue;
            }
        }

        if (ShadowBatch::isCompatible(&mcIt->second)) {
            shadow_batch_.draw(&mcIt->second, pivot);
            ++batched;
        } else {
            // Skinned / incompatible — slow path. Must end the batch first
            // because the per-mesh path may bind a different shader (the
            // skinned-glTF case rebinds the same shader_info[1] but goes
            // through model_render's full setup).
            shadow_batch_.endFace();
            if (mcIt->second.flags & MODEL_GLTF_SKINNED) {
                mat44 zrot180; id44(zrot180);
                zrot180[0] = -1.0f; zrot180[5] = -1.0f;
                mat44 tmp; multiply44x2(tmp, pivot, zrot180);
                memcpy(pivot, tmp, sizeof(mat44));
            }
            model_render(&mcIt->second, cam_.proj, cam_.view, &pivot, 1,
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

void ScenePanel::collectLights(obj* node, std::vector<light_t>& out) {
    // Flat-list path: iterate the cached lightNodes_ instead of walking the
    // tree. Tree mutation events (kEvtSceneDirty / kEvtSceneReplaced) flip
    // flatListsDirty_, which triggers a rebuild before this is called.
    (void)node;  // kept for ABI compatibility; we use the cached list.
    out.reserve(lightNodes_.size());
    for (obj* n : lightNodes_) {
        light_t l;
        editor_light_ref_to_light_t(n, &l);
        out.push_back(l);
    }
}

// One-time bus subscription: any scene mutation (Inspector edit, gizmo drag,
// AddNode, etc.) sets flatListsDirty_, so the next frame rebuilds. Wired
// lazily on first renderScene since EditorApp isn't available at ctor time.
void ScenePanel::wireBusIfNeeded_(EditorApp& app) {
    if (busWired_) return;
    busWired_ = true;
    // Any scene mutation invalidates both caches: (a) flat node lists,
    // because nodes might have been added / removed / reparented; and
    // (b) the MaterialOverrides per-node "is applied?" set, because the
    // edit might have changed an override field on any mesh.
    auto invalidateAll = [this](const std::any&){
        flatListsDirty_ = true;
        overridesApplied_.clear();
    };
    app.bus().on(kEvtSceneDirty,    invalidateAll);
    app.bus().on(kEvtSceneReplaced, invalidateAll);
    app.bus().on(kEvtNodeAdded,     invalidateAll);
    app.bus().on(kEvtNodeRemoved,   invalidateAll);
}

// Recursive walk that fills meshNodes_ + lightNodes_ from the scene tree.
// Only runs on tree mutation — render frames iterate the flat lists.
void ScenePanel::rebuildFlatLists_(obj* root) {
    meshNodes_.clear();
    lightNodes_.clear();
    // Local recursion via a hand-rolled stack (avoids a separate helper).
    std::vector<obj*> stack;
    if (root) stack.push_back(root);
    while (!stack.empty()) {
        obj* n = stack.back();
        stack.pop_back();
        if (editor_obj_is_mesh_renderer(n)) meshNodes_.push_back(n);
        else if (editor_obj_is_light_ref(n)) lightNodes_.push_back(n);
        int cnt = editor_obj_child_count(n);
        for (int i = 0; i < cnt; ++i) {
            if (obj* c = editor_obj_child_at(n, i)) stack.push_back(c);
        }
    }
    flatListsDirty_ = false;
}

void ScenePanel::walkAndRender(obj* node, EditorApp& app,
                               const std::vector<light_t>& lights,
                               obj* fogNode, skybox_t* sky) {
    // Flat-list path: linear iteration over meshNodes_ instead of the
    // recursive tree walk.
    (void)node;
    EDITOR_PROFILE("Editor.Scene.WalkRecurse");

    // Camera-frustum cull. The motor's model_render does its own internal
    // model_is_visible test, but only AFTER allocating instancing buffers
    // and running model_analyseshader — too late to save the editor-side
    // prep work (path resolve, model_light, model_fog, model_skybox,
    // material override apply, pivot compose). We do the cull HERE and
    // skip the entire setup for invisible meshes.
    mat44 projview; multiply44x2(projview, cam_.proj, cam_.view);
    frustum cam_frustum = frustum_build(projview);

    // Cross-mesh transparency sorting. The motor's transparent renderstate
    // enables blend BUT doesn't disable depth-write — so a transparent
    // mesh drawn FIRST writes depth at every fragment (including the
    // discarded/alpha-cutout pixels), occluding opaque meshes drawn AFTER
    // it (e.g. a wire-mesh trash basket would cut out the floor visible
    // through its holes). Fix: render opaque first, then transparent
    // back-to-front by camera distance.
    struct TransparentEntry { obj* node; float dist2; };
    std::vector<TransparentEntry> transparents;
    transparents.reserve(meshNodes_.size() / 4);

    int rendered = 0, culled = 0;
    for (obj* m : meshNodes_) {
        // Resolve the model_t pointer via the same path/modelCache_ chain
        // renderMeshNode uses — we need the bounding sphere BEFORE the
        // full setup. Cheap: pathCache hit + modelCache hit.
        auto pcIt = pathCache_.find(m);
        if (pcIt == pathCache_.end()) {
            // First sighting → renderMeshNode will populate the cache. Don't
            // cull yet; the prep work will run.
            renderMeshNode(m, app, lights, fogNode, sky);
            ++rendered;
            continue;
        }
        auto mcIt = modelCache_.find(pcIt->second.abs);
        if (mcIt == modelCache_.end()) {
            renderMeshNode(m, app, lights, fogNode, sky);
            ++rendered;
            continue;
        }

        // Cull-mode gate:
        //   - cull_mode = 1 (Always Render) → never cull this mesh.
        //   - skinned model → never cull (rest-pose bsphere doesn't reflect
        //     animated bone deformation; the mesh's actual extent can be
        //     2-3 m beyond the static sphere).
        //   - panel toolbar `frustum_cull_` toggle OFF → bypass for debug.
        bool can_cull = frustum_cull_ &&
                        editor_mesh_renderer_cull_mode(m) == 0 &&
                        !(mcIt->second.flags & MODEL_GLTF_SKINNED) &&
                        mcIt->second.num_joints == 0;
        if (can_cull) {
            mat44 cull_pivot;
            editor_mesh_renderer_compose_pivot(m, cull_pivot);
            sphere bs = model_bsphere(mcIt->second, cull_pivot);
            if (!frustum_test_sphere(cam_frustum, bs)) {
                ++culled;
                continue;
            }
        }

        // Defer transparent meshes to a sorted second pass. We use the
        // motor's `model_has_transparency` (true if any submesh has
        // albedo.color.a < 1 OR the albedo texture has a non-trivial
        // alpha channel). Distance is squared (sorting key only) from
        // the mesh's pivot to the camera position.
        if (model_has_transparency(&mcIt->second)) {
            mat44 dist_pivot;
            editor_mesh_renderer_compose_pivot(m, dist_pivot);
            vec3 cam_pos = pos44(cam_.view);
            vec3 mesh_pos = vec3(dist_pivot[12], dist_pivot[13], dist_pivot[14]);
            vec3 d = sub3(mesh_pos, cam_pos);
            float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
            transparents.push_back({m, d2});
            continue;
        }

        renderMeshNode(m, app, lights, fogNode, sky);
        ++rendered;
    }

    // Second pass: transparents, back-to-front (far to near). For correct
    // alpha blending, the GL pipeline needs the most-distant transparent
    // surface drawn FIRST so closer ones blend over it. Within a single
    // model the motor's model_draw_call already z-sorts submeshes; this
    // sort handles the cross-model case.
    if (!transparents.empty()) {
        std::sort(transparents.begin(), transparents.end(),
            [](const TransparentEntry& a, const TransparentEntry& b) {
                return a.dist2 > b.dist2;
            });
        for (const auto& t : transparents) {
            renderMeshNode(t.node, app, lights, fogNode, sky);
            ++rendered;
        }
    }

    editor_profile_set_counter("Editor.Scene.MeshesRendered",     (double)rendered);
    editor_profile_set_counter("Editor.Scene.MeshesCulled",       (double)culled);
    editor_profile_set_counter("Editor.Scene.MeshesTransparent",  (double)transparents.size());
}

void ScenePanel::renderScene(int w, int h, bool inputAllowed, EditorApp& app) {
    EDITOR_PROFILE("Editor.Scene.Total");

    // Reset per-frame mesh tracking before the walk. renderMesh inserts into
    // frameModelPaths_; the published counter at the end reflects how many
    // unique .iqm/.gltf files we actually saw this frame.
    frameModelPaths_.clear();
    frameMeshCount_ = 0;

    // Subscribe to scene-mutation events once. Each rebuild walks the tree
    // exactly ONE time and stores meshNodes_ / lightNodes_ flat lists.
    wireBusIfNeeded_(app);
    if (flatListsDirty_) {
        EDITOR_PROFILE("Editor.Scene.RebuildFlatLists");
        rebuildFlatLists_(app.scene().root());
    }

    editorFreefly(&cam_, !inputAllowed);

    fbo_bind(fbo_.id);
    glViewport(0, 0, w, h);
    glClearColor(0.15f, 0.15f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_enable(&cam_);

    // Spatial audio listener pos = the Scene editor-camera position (in Play mode).
    float listenerPos[3] = { cam_.position.x, cam_.position.y, cam_.position.z };
    app.play().updateAudio(app, listenerPos);

    // 1) light gathering.
    std::vector<light_t> lights;
    {
        EDITOR_PROFILE("Editor.Scene.CollectLights");
        collectLights(app.scene().root(), lights);
    }

    // 2) shadowmap pass — runs iff at least one light has cast_shadows=1.
    // Mirrors demos/16-shadows.c:238-249. shadowmap_begin saves the current
    // FBO + viewport (= the editor FBO bound on line 402), shadowmap_end
    // restores them, so the subsequent fx_begin / world render still lands
    // in the editor FBO. Failing model_render calls during the shadow loop
    // are no-ops (motor sets `skip_render` on degenerate cascade steps),
    // and renderMeshShadowOnly bails early for not-yet-cached models.
    bool any_caster = false;
    for (const auto& l : lights) {
        if (l.cast_shadows) { any_caster = true; break; }
    }
    if (any_caster) {
        EDITOR_PROFILE("Editor.Scene.ShadowPass");
        if (!sm_init_) {
            sm_ = shadowmap();
            sm_init_ = true;
        }
        // Apply scene-wide ShadowSettings (vsm/csm resolution, cascade-split
        // lambda, PCF filter size, etc.) if present. shadowmap_begin picks up
        // the new sizes; filter/window changes trigger an internal rebuild.
        bool shadowNodeFound = false;
        if (obj* shadowNode = findShadowSettings(app.scene().root())) {
            editor_shadow_settings_apply(shadowNode, &sm_);
            shadowNodeFound = true;
        }

        // Debug counters: publish to Profiler so we can verify the apply
        // actually took effect. AppliedResolution should match the value
        // typed into the ShadowSettings node Inspector; NumShadowCasters
        // shows how many lights are doing a full shadow pass this frame.
        editor_profile_set_counter("Editor.Shadow.AppliedResolution",
                                   (double)sm_.vsm_texture_width);
        editor_profile_set_counter("Editor.Shadow.ShadowSettingsFound",
                                   shadowNodeFound ? 1.0 : 0.0);
        int caster_count = 0;
        for (const auto& l : lights) if (l.cast_shadows) ++caster_count;
        editor_profile_set_counter("Editor.Shadow.NumShadowCasters",
                                   (double)caster_count);

        shadowmap_begin(&sm_);
        for (size_t i = 0; i < lights.size(); ++i) {
            if (!lights[i].cast_shadows) continue;
            while (shadowmap_step(&sm_)) {
                shadowmap_light(&sm_, &lights[i], cam_.proj, cam_.view);
                walkShadowPass(app.scene().root(), app);
            }
        }
        shadowmap_end(&sm_);
    }

    // 3) main render pass — with lights + shadowmap + fog + skybox/IBL.
    obj* fogNode = findFogSettings(app.scene().root());

    // Skybox: resolve from the scene's Skybox node (cached + mtime-poll), render
    // background if its render_background flag is on.
    skybox_t* sky = resolveSkybox(app);

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

    if (sky) {
        obj* skyNode = findSkyboxNode(app.scene().root());
        int render_bg = 1;
        if (skyNode) editor_skybox_get(skyNode, nullptr, nullptr, nullptr, &render_bg);
        if (render_bg) skybox_render(sky, cam_.proj, cam_.view);
    }

    {
        EDITOR_PROFILE("Editor.Scene.WalkAndRender");
        walkAndRender(app.scene().root(), app, lights, fogNode, sky);
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

    // Publish per-frame mesh counters into the Profiler panel. Both numbers
    // are reset at the start of the next renderScene. Ratio = duplication
    // factor: MeshCount=20, UniqueModels=4 → instancing would collapse to 4
    // draw "sessions" (huge win); MeshCount=20, UniqueModels=20 → no win.
    editor_profile_set_counter("Editor.Scene.MeshCount",
                               (double)frameMeshCount_);
    editor_profile_set_counter("Editor.Scene.UniqueModels",
                               (double)frameModelPaths_.size());
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
