// STL FIRST.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../core/asset_path.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "game_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../components/components_api.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/profile_scope.h"
#include "../persistence/material_override_io.h"
#include "../runtime/script_host.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

GamePanel::~GamePanel() {
    if (fbo_.id) {
        fbo_destroy(fbo_);
        fbo_ = fbo_t{};
    }
    if (sm_init_) {
        shadowmap_destroy(&sm_);
        sm_init_ = false;
    }
    // Skybox cleanup runs in AssetManager dtor (shared with ScenePanel).
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
    // Flat-list path: iterate the cached lightNodes_ rebuilt only on tree
    // mutation. The `node` argument is kept for ABI parity but unused.
    (void)node;
    out.reserve(lightNodes_.size());
    for (obj* n : lightNodes_) {
        light_t l;
        editor_light_ref_to_light_t(n, &l);
        out.push_back(l);
    }
}

// One-time bus subscription: any scene mutation (Inspector edit, gizmo drag,
// AddNode, etc.) sets flatListsDirty_ + clears overridesApplied_. Wired
// lazily because EditorApp isn't available at ctor time.
void GamePanel::wireBusIfNeeded_(EditorApp& app) {
    if (busWired_) return;
    busWired_ = true;
    auto invalidateAll = [this](const std::any&){
        flatListsDirty_ = true;
        overridesApplied_.clear();
    };
    app.bus().on(kEvtSceneDirty,    invalidateAll);
    app.bus().on(kEvtSceneReplaced, invalidateAll);
    app.bus().on(kEvtNodeAdded,     invalidateAll);
    app.bus().on(kEvtNodeRemoved,   invalidateAll);
}

// DFS rebuild that fills meshNodes_ + lightNodes_ from the scene tree.
// Only runs on tree mutation; render frames iterate the flat lists.
void GamePanel::rebuildFlatLists_(obj* root) {
    meshNodes_.clear();
    lightNodes_.clear();
    std::vector<obj*> stack;
    if (root) stack.push_back(root);
    while (!stack.empty()) {
        obj* n = stack.back(); stack.pop_back();
        if (editor_obj_is_mesh_renderer(n)) meshNodes_.push_back(n);
        else if (editor_obj_is_light_ref(n)) lightNodes_.push_back(n);
        int cnt = editor_obj_child_count(n);
        for (int i = 0; i < cnt; ++i) {
            if (obj* c = editor_obj_child_at(n, i)) stack.push_back(c);
        }
    }
    flatListsDirty_ = false;
}

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

// Depth-first lookup of the first PostFXStack in the scene (NULL if none).
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

// Resolve PostFXStack to a single "is the FX pipeline active?" boolean.
// Active = (PostFXStack node exists) AND (`enabled` field is non-zero).
// Note: fx_begin/end further short-circuit to no-op if 0 passes are enabled,
// so calling them with `true` here is safe even on a fresh stack.
static bool isFXActive(obj* fxNode) {
    if (!fxNode) return false;
    int enabled = 0;
    editor_postfx_stack_get(fxNode, &enabled, nullptr);
    return enabled != 0;
}

// Collect TextRenderer nodes for the HUD-overlay pass.
static void collectTextRenderers(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    if (editor_obj_is_text_renderer(node)) out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectTextRenderers(editor_obj_child_at(node, i), out);
    }
}

// Build a font_print prefix from the (face/color/size) tags. Tag-bytes:
//   face  1..5 → 0x10..0x14   (FONT_FACEn)
//   color 1..6 → 0x1A..0x1F   (FONT_COLORn)
//   size  1..6 → 0x01..0x06   (FONT_Hn)
static void textRendererPrefix(int face, int color, int size, char out[8]) {
    int i = 0;
    if (face  >= 1 && face  <= 5) out[i++] = (char)(0x10 + face - 1);
    if (color >= 1 && color <= 6) out[i++] = (char)(0x1A + color - 1);
    if (size  >= 1 && size  <= 6) out[i++] = (char)size;
    out[i] = 0;
}

static void drawTextOverlays(obj* root) {
    std::vector<obj*> texts;
    collectTextRenderers(root, texts);
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
        textRendererPrefix(face, color, size, prefix);
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

static void collectText3DRenderers(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    if (editor_obj_is_text_renderer_3d(node)) out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectText3DRenderers(editor_obj_child_at(node, i), out);
    }
}

static void drawText3DOverlays(obj* root) {
    std::vector<obj*> t3ds;
    collectText3DRenderers(root, t3ds);
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

skybox_t* GamePanel::resolveSkybox(EditorApp& app) {
    obj* skyNode = findSkyboxNode(app.scene().root());
    if (!skyNode) return nullptr;

    const char *skyPath = nullptr, *reflPath = nullptr, *envPath = nullptr;
    int render_bg = 1;
    editor_skybox_get(skyNode, &skyPath, &reflPath, &envPath, &render_bg);
    if (!skyPath || !*skyPath) return nullptr;

    std::string absSky  = asset_path::toAbsolute(skyPath,  app.projectPath());
    std::string absRefl = (reflPath && *reflPath)
                        ? asset_path::toAbsolute(reflPath, app.projectPath()) : std::string{};
    std::string absEnv  = (envPath && *envPath)
                        ? asset_path::toAbsolute(envPath,  app.projectPath()) : std::string{};
    return app.assets().loadSkybox(absSky, absRefl, absEnv);
}

void GamePanel::walkAndRenderMeshes(obj* node, EditorApp& app, camera_t& cam,
                                    const std::vector<light_t>& lights,
                                    obj* fogNode, skybox_t* sky) {
    // Flat-list path with the same caches as ScenePanel — no recursion.
    // `node` is kept for ABI parity but ignored; we use meshNodes_ instead.
    (void)node;

    // Camera-frustum cull (same rationale as ScenePanel::walkAndRender).
    mat44 projview; multiply44x2(projview, cam.proj, cam.view);
    frustum cam_frustum = frustum_build(projview);
    int rendered = 0, culled = 0;

    // Cross-mesh transparency sort — same fix as ScenePanel. Defer
    // transparent meshes to a second pass, back-to-front by distance.
    // Refaktor F1: model_t* lives in AssetManager; the entry just carries
    // the resolved pointer so the second pass doesn't re-lookup.
    struct TransparentEntry {
        obj*     node;
        float    dist2;
        model_t* mp;
    };
    std::vector<TransparentEntry> transparents;
    transparents.reserve(meshNodes_.size() / 4);
    vec3 cam_pos = pos44(cam.view);

    for (obj* m : meshNodes_) {
        const char* relPath = editor_mesh_renderer_path(m);
        if (!relPath || !*relPath) continue;

        // Refaktor F1: rel→abs resolve + load go through shared AssetManager.
        std::string absPath = app.assets().absPathFor(m, relPath);
        model_t* mp = app.assets().loadModel(absPath, relPath);
        if (!mp) continue;

        // Camera-frustum cull — skip the per-frame uniform setup + draw
        // when the mesh's bsphere doesn't intersect the camera frustum.
        // Bypassed for: panel-toggle OFF / cull_mode = Always Render /
        // skinned models (rest-pose bsphere unreliable under animation).
        bool can_cull = frustum_cull_ &&
                        editor_mesh_renderer_cull_mode(m) == 0 &&
                        !(mp->flags & MODEL_GLTF_SKINNED) &&
                        mp->num_joints == 0;
        if (can_cull) {
            mat44 cull_pivot;
            editor_mesh_renderer_compose_pivot(m, cull_pivot);
            sphere bs = model_bsphere(*mp, cull_pivot);
            if (!frustum_test_sphere(cam_frustum, bs)) {
                ++culled;
                continue;
            }
        }
        // Defer transparent meshes to a sorted second pass (back-to-front
        // by camera distance). The motor's RENDER_PASS_TRANSPARENT enables
        // blend but doesn't disable depth-write, so an unsorted transparent
        // mesh would occlude any opaque mesh drawn after it (e.g. a wire
        // basket would cut out the floor visible through its holes).
        if (model_has_transparency(mp)) {
            mat44 dist_pivot;
            editor_mesh_renderer_compose_pivot(m, dist_pivot);
            vec3 mesh_pos = vec3(dist_pivot[12], dist_pivot[13], dist_pivot[14]);
            vec3 d = sub3(mesh_pos, cam_pos);
            float d2 = d.x*d.x + d.y*d.y + d.z*d.z;
            transparents.push_back({m, d2, mp});
            continue;
        }

        renderMeshNode_(m, app, cam, lights, fogNode, sky, mp);
        ++rendered;
    }

    // Second pass: transparent meshes, far → near.
    if (!transparents.empty()) {
        std::sort(transparents.begin(), transparents.end(),
            [](const TransparentEntry& a, const TransparentEntry& b) {
                return a.dist2 > b.dist2;
            });
        for (const auto& t : transparents) {
            renderMeshNode_(t.node, app, cam, lights, fogNode, sky, t.mp);
            ++rendered;
        }
    }

    editor_profile_set_counter("Editor.Game.MeshesRendered",    (double)rendered);
    editor_profile_set_counter("Editor.Game.MeshesCulled",      (double)culled);
    editor_profile_set_counter("Editor.Game.MeshesTransparent", (double)transparents.size());
}

void GamePanel::renderMeshNode_(obj* m, EditorApp& app, camera_t& cam,
                                const std::vector<light_t>& lights,
                                obj* fogNode, skybox_t* sky,
                                model_t* model) {
    if (!model) return;

    // Per-frame uniforms — light list, shadowmap, fog, skybox/IBL.
    if (!lights.empty()) {
        model_light(model, (unsigned)lights.size(),
                    const_cast<light_t*>(lights.data()));
    } else {
        model_light(model, 0, nullptr);
    }
    model_shadow(model, sm_init_ ? &sm_ : nullptr);
    if (fogNode) {
        int mode = 0; vec3 color = {0,0,0};
        float start = 0.f, end = 1.f, density = 0.f;
        editor_fog_settings_get(fogNode, &mode, &color, &start, &end, &density);
        model_fog(model, (unsigned)mode, color, start, end, density);
    } else {
        vec3 black = {0,0,0};
        model_fog(model, 0u, black, 0.f, 1.f, 0.f);
    }
    if (sky) {
        model_skybox(model, *sky);
    } else {
        skybox_t empty = {0};
        model_skybox(model, empty);
    }

    // MaterialOverrides cache.
    if (overridesApplied_.find(m) == overridesApplied_.end()) {
        material_override_io::applyOverridesToModel(
            m, model, app.projectPath());
        overridesApplied_.insert(m);
    }

    mat44 pivot;
    editor_mesh_renderer_compose_pivot(m, pivot);
    if (model->flags & MODEL_GLTF_SKINNED) {
        mat44 zrot180; id44(zrot180);
        zrot180[0] = -1.0f; zrot180[5] = -1.0f;
        mat44 tmp; multiply44x2(tmp, pivot, zrot180);
        memcpy(pivot, tmp, sizeof(mat44));
    }
    model_render(model, cam.proj, cam.view, &pivot, 1, -1);
}

void GamePanel::renderMeshShadowOnly(obj* node, camera_t& cam, EditorApp& app) {
    const char* relPath = editor_mesh_renderer_path(node);
    if (!relPath || !*relPath) return;
    std::string absPath = app.assets().absPathFor(node, relPath);
    model_t* mp = app.assets().modelByAbsPath(absPath);
    if (!mp) return;   // not yet cached — main pass will load on its frame
    mat44 pivot;
    editor_mesh_renderer_compose_pivot(node, pivot);
    // Match the main-pass skinned-glTF flip so the cast shadow matches the
    // visible silhouette.
    if (mp->flags & MODEL_GLTF_SKINNED) {
        mat44 zrot180; id44(zrot180); zrot180[0] = -1.0f; zrot180[5] = -1.0f;
        mat44 tmp; multiply44x2(tmp, pivot, zrot180);
        memcpy(pivot, tmp, sizeof(mat44));
    }
    model_render(mp, cam.proj, cam.view, &pivot, 1,
                 RENDER_PASS_SHADOW);
}

void GamePanel::walkShadowPass(obj* node, camera_t& cam, EditorApp& app) {
    // Plan B Phase 1: batched shadow pass — bind shader + face uniforms +
    // renderstate ONCE per face, then per-mesh fast path (uniform_set MODEL
    // + glBindVertexArray + single glDrawElementsInstanced). Mirrors
    // ScenePanel::walkShadowPass. Skinned meshes fall back to model_render.
    (void)node;
    if (meshNodes_.empty()) return;

    // Find a prototype model (first compatible cached mesh) for the batch
    // to source shader/uniform handles from. Refaktor F1: iterate the shared
    // AssetManager model cache instead of pathCache+modelCache lookup.
    model_t* proto = nullptr;
    for (const auto& kv : app.assets().models()) {
        model_t* candidate = const_cast<model_t*>(&kv.second);
        if (ShadowBatch::isCompatible(candidate)) {
            proto = candidate;
            break;
        }
    }
    if (!proto) {
        // No compatible meshes — fall back entirely to the old per-mesh path.
        for (obj* m : meshNodes_) renderMeshShadowOnly(m, cam, app);
        return;
    }

    shadow_batch_.beginFace(proto, &sm_, cam.proj, cam.view);
    // Per-face shadow-frustum cull. shadowmap_light updated sm_.shadow_frustum
    // for THIS face. Sphere-vs-frustum skips meshes that this cubemap face
    // can't possibly see — typically ~50-65% per face for a 90° cubemap face
    // covering only 1/6 of the light's sphere.
    const frustum sh_frustum = sm_.shadow_frustum;
    int sh_culled = 0;
    for (obj* m : meshNodes_) {
        const char* relPath = editor_mesh_renderer_path(m);
        if (!relPath || !*relPath) continue;
        std::string absPath = app.assets().absPathFor(m, relPath);
        model_t* mp = app.assets().modelByAbsPath(absPath);
        if (!mp) continue;

        mat44 pivot;
        editor_mesh_renderer_compose_pivot(m, pivot);

        // Frustum cull. Same gates as the main pass.
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
        } else {
            // Skinned / incompatible — slow path. End the batch first so the
            // motor's full model_render can do its own shader/state setup.
            shadow_batch_.endFace();
            if (mp->flags & MODEL_GLTF_SKINNED) {
                mat44 zrot180; id44(zrot180);
                zrot180[0] = -1.0f; zrot180[5] = -1.0f;
                mat44 tmp; multiply44x2(tmp, pivot, zrot180);
                memcpy(pivot, tmp, sizeof(mat44));
            }
            model_render(mp, cam.proj, cam.view, &pivot, 1,
                         RENDER_PASS_SHADOW);
            shadow_batch_.beginFace(proto, &sm_, cam.proj, cam.view);
        }
    }
    shadow_batch_.endFace();
    editor_profile_set_counter("Editor.Game.ShadowCulledPerFace", (double)sh_culled);
}

void GamePanel::renderWithCamera(obj* cameraNode, int w, int h, EditorApp& app) {
    EDITOR_PROFILE("Editor.Game.Total");

    // One-time bus subscription + lazy rebuild of the flat node lists.
    // Without these, the per-frame walks would still recurse the obj-tree
    // and asset_path::toAbsolute would syscall every mesh every frame.
    wireBusIfNeeded_(app);
    if (flatListsDirty_) {
        EDITOR_PROFILE("Editor.Game.RebuildFlatLists");
        rebuildFlatLists_(app.scene().root());
    }

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
    {
        EDITOR_PROFILE("Editor.Game.CollectLights");
        collectLights(app.scene().root(), lights);
    }
    obj* fogNode = findFogSettings(app.scene().root());

    // Shadow caster pass — see scene_panel for the parallel comment. Runs
    // BEFORE fx_begin so shadowmap_begin saves the editor FBO and end restores
    // it; only fires when at least one light has cast_shadows=1.
    bool any_caster = false;
    for (const auto& l : lights) {
        if (l.cast_shadows) { any_caster = true; break; }
    }
    if (any_caster) {
        EDITOR_PROFILE("Editor.Game.ShadowPass");
        if (!sm_init_) {
            sm_ = shadowmap();
            sm_init_ = true;
        }
        if (obj* shadowNode = findShadowSettings(app.scene().root())) {
            editor_shadow_settings_apply(shadowNode, &sm_);
        }
        shadowmap_begin(&sm_);
        for (size_t i = 0; i < lights.size(); ++i) {
            if (!lights[i].cast_shadows) continue;
            while (shadowmap_step(&sm_)) {
                shadowmap_light(&sm_, &lights[i], cam.proj, cam.view);
                walkShadowPass(app.scene().root(), cam, app);
            }
        }
        shadowmap_end(&sm_);
    }

    // Skybox: resolve from the scene's Skybox node (cached + mtime-poll), render
    // background if its render_background flag is on.
    skybox_t* sky = resolveSkybox(app);

    // PostFX: wrap the world-render in fx_begin/end. fbo_unbind() inside
    // fx_end() pops back to fbo_.id (motor uses a bind-stack), so the
    // FX-applied result lands in the editor FBO automatically. Passing 0/0
    // makes fx_end use its own internal color/depth as the first-pass input
    // (which is what we just rendered into during fx_begin), instead of
    // sampling the editor FBO's textures (which would skip the world-render).
    obj* fxNode = findPostFXStack(app.scene().root());
    const bool fx_active = isFXActive(fxNode);

    if (fx_active) fx_begin_res(w, h);

    if (sky) {
        obj* skyNode = findSkyboxNode(app.scene().root());
        int render_bg = 1;
        if (skyNode) editor_skybox_get(skyNode, nullptr, nullptr, nullptr, &render_bg);
        if (render_bg) skybox_render(sky, cam.proj, cam.view);
    }

    {
        EDITOR_PROFILE("Editor.Game.WalkAndRender");
        walkAndRenderMeshes(app.scene().root(), app, cam, lights, fogNode, sky);
    }

    // 3D label pass — Text3DRenderer nodes drawn in world-space via ddraw_text.
    drawText3DOverlays(app.scene().root());

    // Script on_draw — at this point the FBO and camera are already bound, so the Lua
    // `C.ddraw_*` / `C.model_render` draws here. The ddraw_flush is needed
    // to render the current buffer to the FBO.
    if (app.play().isPlaying()) {
        app.scriptHost().drawAll();
        ddraw_flush();
    }

    if (fx_active) fx_end(0, 0);

    // HUD pass — TextRenderer nodes drawn in 2D viewport-pixel space.
    // Intentionally AFTER fx_end: a Bloom/CRT/etc effect on UI text usually
    // looks like a render bug, so the HUD overlay stays FX-free.
    drawTextOverlays(app.scene().root());

    fbo_unbind();
}

void GamePanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    // Toolbar row — debug toggles. Mirrors the Scene panel layout.
    ImGui::Checkbox("Frustum Cull", &frustum_cull_);
    ImGui::SameLine();
    ImGui::TextDisabled("(uncheck to disable cull for debugging)");

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
        // Vertical centering (approx 2 lines of text).
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
