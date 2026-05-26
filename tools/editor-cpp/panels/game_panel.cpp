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
#include "../render/framebuffer.h"
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
    framebuffer_destroy(fbo_, width_, height_);
    // Shadowmap cleanup moved to RenderSystem3D dtor (Refaktor F4 step #3).
    // Skybox cleanup runs in AssetManager dtor (shared with ScenePanel).
}

void GamePanel::ensureFbo(int w, int h) {
    framebuffer_ensure(fbo_, width_, height_, w, h);
}

// findActiveCamera / collectLights / rebuildFlatLists_ / findFogSettings /
// findSkyboxNode / findShadowSettings / findPostFXStack — all moved to
// editor::SceneQuery (Refaktor F3). One cached DFS per scene mutation
// instead of 4-5 separate walks per frame. SceneQuery is shared with
// ScenePanel so both viewports pay the rebuild cost only once.

// MaterialOverrides apply cache + its bus subscription moved to
// editor::RenderSystem3D (Refaktor F4 step #2) — owned by the render system,
// auto-wired on first `renderMeshNode` call.

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

skybox_t* GamePanel::resolveSkybox(EditorApp& app, int* render_bg_out) {
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

// walkAndRenderMeshes + renderMeshNode_ moved to RenderSystem3D::renderMainPass
// (Refaktor F4 step #4). Counters published as Editor.Render.* (aggregated
// across both viewports) — the Editor.Game.MeshesRendered/Culled/Transparent
// names are gone; check Editor.Render.MeshesDrawn/Culled/Transparent instead.

// renderMeshShadowOnly + walkShadowPass moved to editor::RenderSystem3D
// (Refaktor F4 step #3). The panel now calls renderSystem3D_.renderShadowPass
// which drives the whole shadowmap_step loop internally.

void GamePanel::renderWithCamera(obj* cameraNode, int w, int h, EditorApp& app) {
    EDITOR_PROFILE("Editor.Game.Total");

    // Scene-mutation bus subscriptions live in RenderSystem3D / SceneQuery
    // (Refaktor F3 + F4) — they auto-wire on first use.

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

    // Light gathering — iterate SceneQuery-cached LightRef nodes (Refaktor F3),
    // convert each to a motor light_t. Tree walk runs at most once per scene
    // mutation, shared with ScenePanel.
    std::vector<light_t> lights;
    {
        EDITOR_PROFILE("Editor.Game.CollectLights");
        const auto& lightNodes = app.sceneQuery().lightNodes(app.scene().root());
        lights.reserve(lightNodes.size());
        for (obj* n : lightNodes) {
            light_t l;
            editor_light_ref_to_light_t(n, &l);
            lights.push_back(l);
        }
    }
    obj* fogNode = app.sceneQuery().fog(app.scene().root());

    // Shadow caster pass — full lazy-init + ShadowSettings apply +
    // shadowmap_step loop + batched walk in RenderSystem3D (Refaktor F4
    // step #3). Runs BEFORE fx_begin so shadowmap_begin saves the editor
    // FBO + viewport, shadowmap_end restores them.
    // beginFrame builds the per-frame RenderableMesh list once (audit
    // cross-cutting). Shared with renderShadowPass + renderMainPass.
    renderSystem3D_.beginFrame(app, cam);

    if (RenderSystem3D::hasAnyShadowCaster(lights)) {
        EDITOR_PROFILE("Editor.Game.ShadowPass");
        renderSystem3D_.renderShadowPass(app, cam, lights, frustum_cull_);
    }

    // Skybox: resolve from the scene's Skybox node (cached + mtime-poll), render
    // background if its render_background flag is on (audit Finding I).
    int sky_render_bg = 1;
    skybox_t* sky = resolveSkybox(app, &sky_render_bg);

    // PostFX: wrap the world-render in fx_begin/end. fbo_unbind() inside
    // fx_end() pops back to fbo_.id (motor uses a bind-stack), so the
    // FX-applied result lands in the editor FBO automatically. Passing 0/0
    // makes fx_end use its own internal color/depth as the first-pass input
    // (which is what we just rendered into during fx_begin), instead of
    // sampling the editor FBO's textures (which would skip the world-render).
    obj* fxNode = app.sceneQuery().postFXStack(app.scene().root());
    const bool fx_active = isFXActive(fxNode);

    if (fx_active) fx_begin_res(w, h);

    if (sky && sky_render_bg) skybox_render(sky, cam.proj, cam.view);

    {
        EDITOR_PROFILE("Editor.Game.WalkAndRender");
        renderSystem3D_.renderMainPass(app, cam, lights, fogNode, sky,
                                       frustum_cull_);
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

    obj* cameraNode = app.sceneQuery().activeCamera(app.scene().root());
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
