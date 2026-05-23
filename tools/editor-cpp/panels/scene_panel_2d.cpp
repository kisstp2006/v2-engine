// STL ELŐSZÖR.
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

#include "scene_panel_2d.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../commands/command.h"
#include "../components/components_api.h"
#include "../core/selection_service.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

namespace {
inline vec3 mkv3(float x, float y, float z) {
    vec3 v; v.x = x; v.y = y; v.z = z; return v;
}

// 2D-barát grid az XY síkon (Z=0). Minor + major rács + tengelyek.
// camX/Y a viewport középe, halfW/H a látható félméret world-unitban.
void drawGrid2D(float camX, float camY, float halfW, float halfH) {
    // Adaptív step: a fov nagysága szerint 10/100/1000 stb. Nagyobb kockák.
    float scale = halfH * 2.0f;            // viewport-magasság világban
    int   step  = 10;
    while (scale / step > 12.0f) step *= 10;   // max ~12 major kocka egy nézetben
    while (scale / step < 2.0f)  step /= 10;
    if (step < 1) step = 1;
    int subStep = step / 5;                    // 5 sub-divízió majoronként
    if (subStep < 1) subStep = 1;

    float l = camX - halfW, r = camX + halfW;
    float b = camY - halfH, t = camY + halfH;

    int lSnap = (int)floorf(l / subStep) * subStep;
    int rSnap = (int)ceilf (r / subStep) * subStep;
    int bSnap = (int)floorf(b / subStep) * subStep;
    int tSnap = (int)ceilf (t / subStep) * subStep;

    // Minor (sub) lines — sötét szürke
    ddraw_color(rgba(60, 60, 70, 255));
    for (int x = lSnap; x <= rSnap; x += subStep) {
        if (x % step == 0) continue;
        ddraw_line(mkv3((float)x, b, 0), mkv3((float)x, t, 0));
    }
    for (int y = bSnap; y <= tSnap; y += subStep) {
        if (y % step == 0) continue;
        ddraw_line(mkv3(l, (float)y, 0), mkv3(r, (float)y, 0));
    }

    // Major lines — világosabb szürke
    ddraw_color(rgba(110, 110, 125, 255));
    for (int x = lSnap; x <= rSnap; x += step) {
        if (x == 0) continue;
        ddraw_line(mkv3((float)x, b, 0), mkv3((float)x, t, 0));
    }
    for (int y = bSnap; y <= tSnap; y += step) {
        if (y == 0) continue;
        ddraw_line(mkv3(l, (float)y, 0), mkv3(r, (float)y, 0));
    }

    // Tengelyek (origó-vonalak) — X piros, Y zöld
    ddraw_color(rgba(220, 60, 60, 255));
    ddraw_line(mkv3(l, 0, 0), mkv3(r, 0, 0));
    ddraw_color(rgba(60, 200, 80, 255));
    ddraw_line(mkv3(0, b, 0), mkv3(0, t, 0));
}
}  // namespace

Scene2DPanel::Scene2DPanel() : Panel("scene2d", "Scene 2D") {
    cam_ = camera();
    // 2D editor-nézet: ortho + kamera a +Z-en, lefelé nézve a XY síkra.
    // Default `camera()` isometric (pitch=35°, position=(10,10,10)) — ez a
    // top-down nézethez nem jó, ezért explicit lookat-tel állítjuk be.
    cam_.orthographic = true;
    cam_.fov          = 200.0f;
    cam_.position     = mkv3(0.0f, 0.0f, 10.0f);
    camera_lookat(&cam_, mkv3(0.0f, 0.0f, 0.0f));
}

Scene2DPanel::~Scene2DPanel() {
    if (fbo_.id) {
        fbo_destroy(fbo_);
        fbo_ = fbo_t{};
    }
}

void Scene2DPanel::ensureFbo(int w, int h) {
    if (w == width_ && h == height_ && fbo_.id) return;
    if (fbo_.id) fbo_destroy(fbo_);
    fbo_ = fbo((unsigned)w, (unsigned)h, 0, 0);
    width_  = w;
    height_ = h;
}

void Scene2DPanel::renderSpriteNode(obj* node, EditorApp& app) {
    const char* relPath = editor_sprite_renderer_path(node);
    if (!relPath || !*relPath) return;

    // Phase 4a — abs-resolve.
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    const std::string& path = absPath;

    if (failedPaths_.isFresh(path)) return;
    failedPaths_.erase(path);
    if (!is_file(path.c_str())) {
        failedPaths_.insert(path);
        app.bus().emit("log", std::string("[Sprite] file not found: ") + relPath);
        return;
    }

    uint64_t mt_now = mtimeNs(path);
    auto mt_it = textureMtimes_.find(path);
    if (mt_it != textureMtimes_.end() && mt_it->second != mt_now) {
        textureCache_.erase(path);
        app.bus().emit("log", std::string("[Sprite] reloaded (mtime): ") + relPath);
    }

    auto it = textureCache_.find(path);
    if (it == textureCache_.end()) {
        texture_t tx = texture(path.c_str(), 0);
        if (!tx.id) {
            failedPaths_.insert(path);
            app.bus().emit("log", std::string("[Sprite] load failed: ") + relPath);
            return;
        }
        auto ins = textureCache_.emplace(path, tx);
        it = ins.first;
        textureMtimes_[path] = mt_now;
        app.bus().emit("log", std::string("[Sprite] loaded: ") + relPath);
    }

    float pos[3], rot = 0, scale[2] = {1, 1};
    unsigned tint = 0xFFFFFFFFu;
    editor_sprite_renderer_get_xform(node, pos, &rot, scale, &tint);

    // SPRITE_PROJECTED → a kamera mátrix-szal vetít (zoom/pan hat).
    // SPRITE_CENTERED  → a sprite pivot a középpontján van.
    // scale.y negálva  → Y-flip (a motor sprite Y-le-mutat, az ortho Y-fel).
    float sheet[3]     = {0, 0, 0};
    float offset[2]    = {0, 0};
    float drawScale[2] = {scale[0], -scale[1]};
    sprite_sheet(it->second, sheet, pos, rot, offset, drawScale, tint,
                 SPRITE_PROJECTED | SPRITE_CENTERED);
}

void Scene2DPanel::renderTilemapNode(obj* node, EditorApp& app) {
    const char* relPath = editor_tilemap_ref_path(node);
    if (!relPath || !*relPath) return;

    // Phase 4a — abs-resolve.
    std::string absPath = asset_path::toAbsolute(relPath, app.projectPath());
    const std::string& path = absPath;

    if (failedPaths_.isFresh(path)) return;
    failedPaths_.erase(path);
    if (!is_file(path.c_str())) {
        failedPaths_.insert(path);
        app.bus().emit("log", std::string("[Tilemap] file not found: ") + relPath);
        return;
    }

    uint64_t mt_now = mtimeNs(path);
    auto mt_it = tilemapMtimes_.find(path);
    if (mt_it != tilemapMtimes_.end() && mt_it->second != mt_now) {
        tilemapCache_.erase(path);
        tilemapMtimes_[path] = mt_now;
        app.bus().emit("log", std::string("[Tilemap] reloaded (mtime): ") + relPath);
    }

    auto it = tilemapCache_.find(path);
    if (it == tilemapCache_.end()) {
        // `tiled()` a TMX-tartalmat várja, NEM a path-ot — előbb file_read.
        char* content = file_read(path.c_str(), 0);
        if (!content || !*content) {
            failedPaths_.insert(path);
            app.bus().emit("log", std::string("[Tilemap] file_read failed: ") + relPath);
            return;
        }
        tiled_t tm = tiled(content);
        if (!tm.first_gid) {
            failedPaths_.insert(path);
            app.bus().emit("log", std::string("[Tilemap] parse failed: ") + relPath);
            return;
        }
        auto ins = tilemapCache_.emplace(path, tm);
        it = ins.first;
        tilemapMtimes_[path] = mt_now;
        app.bus().emit("log", std::string("[Tilemap] loaded: ") + relPath);
    }

    float pos[3];
    editor_tilemap_ref_get_pos(node, pos);
    vec3 center;
    center.x = pos[0];
    center.y = pos[1];
    center.z = pos[2];
    tiled_render(it->second, center);
}

void Scene2DPanel::walkAndRender(obj* node, EditorApp& app) {
    if (!node) return;
    if (editor_obj_is_sprite_renderer(node)) {
        renderSpriteNode(node, app);
    }
    if (editor_obj_is_tilemap_ref(node)) {
        renderTilemapNode(node, app);
    }
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        walkAndRender(editor_obj_child_at(node, i), app);
    }
}

void Scene2DPanel::renderScene(int w, int h, bool inputAllowed, EditorApp& app) {
    if (inputAllowed) {
        // Pan — MMB drag, fov-arányos sebességgel (1 pixel mouse = 1 unit a
        // viewport méretéhez normalizálva). A lookat-target is együtt pan-el,
        // hogy a lookdir mindig (0, 0, -1) maradjon (top-down view).
        if (input(MOUSE_M) && h > 0) {
            float panSpeed = (2.0f * cam_.fov) / (float)h;
            cam_.position.x -= input_diff(MOUSE_X) * panSpeed;
            cam_.position.y += input_diff(MOUSE_Y) * panSpeed;
            camera_lookat(&cam_,
                          mkv3(cam_.position.x, cam_.position.y, 0.0f));
        }
        // Zoom — scroll wheel, a fov mezőt változtatja (ortho-vetület size).
        float wheel = input_diff(MOUSE_W);
        if (wheel != 0.0f) {
            cam_.fov *= (1.0f - wheel * 0.1f);
            if (cam_.fov < 10.0f)   cam_.fov = 10.0f;
            if (cam_.fov > 5000.0f) cam_.fov = 5000.0f;
        }
    }

    fbo_bind(fbo_.id);
    glViewport(0, 0, w, h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    camera_enable(&cam_);

    // 2D-barát grid (XY sík, fov-adaptív, tengelyekkel).
    float aspect = (h > 0) ? (float)w / (float)h : 1.0f;
    drawGrid2D(cam_.position.x, cam_.position.y, cam_.fov * aspect, cam_.fov);
    ddraw_flush();

    // Spatial audio listener — 2D-ben a Scene2D-kamera pozícióját adjuk.
    float listenerPos[3] = { cam_.position.x, cam_.position.y, cam_.position.z };
    app.play().updateAudio(app, listenerPos);

    walkAndRender(app.scene().root(), app);
    sprite_flush();

    fbo_unbind();
}

void Scene2DPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = (int)avail.x;
    int h = (int)avail.y;
    const bool gizmoBusy = ImGuizmo_IsUsing();
    const bool inputAllowed =
        (ImGui::IsWindowHovered() || ImGui::IsWindowFocused()) && !gizmoBusy;

    if (w > 0 && h > 0) {
        ensureFbo(w, h);
        ImGui::SetNextWindowPos(ImVec2(-9999, -9999));
        ImGui::SetNextWindowSize(ImVec2(1, 1));
        ImGui::Begin("##fbo-render-trap-scene2d", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
        renderScene(w, h, inputAllowed, app);
        ImGui::End();
        ImGui::Image((ImTextureID)(uintptr_t)fbo_.texture_color.id,
                     ImVec2((float)w, (float)h),
                     ImVec2(0, 1), ImVec2(1, 0));

        // Drop target — 2D-vel kompatibilis assetek (.png/.jpg/.bmp/.tga → Sprite,
        // .tmx → Tilemap).
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                std::string path((const char*)p->Data);
                std::string ext = std::filesystem::path(path).extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c){ return std::tolower(c); });
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg"
                    || ext == ".bmp" || ext == ".tga") {
                    app.createSprite(path.c_str());
                } else if (ext == ".tmx") {
                    app.createTilemap(path.c_str());
                } else if (ext == ".ogg" || ext == ".wav" || ext == ".mp3"
                           || ext == ".flac") {
                    app.createAudioSource(path.c_str());
                } else if (ext == ".json5"
                           && path.find(".prefab.") != std::string::npos) {
                    app.spawnPrefab(path.c_str());
                } else if (ext == ".lua") {
                    app.createScript(path.c_str());
                }
            }
            ImGui::EndDragDropTarget();
        }

        // 2D gizmo — CSAK 2D komponensekre (Sprite, TilemapRef).
        // T/R/S mind támogatott, de korlátozottan: rotate csak Z-tengelyen,
        // scale csak X-Y. Z-pos (depth) NEM mozog.
        obj* sel = app.selection().primary();
        vec3* posPtr   = (sel && editor_obj_is_2d_component(sel))
                         ? editor_obj_pos_addr(sel) : nullptr;
        vec3* rotPtr   = sel ? editor_obj_rot_addr(sel)   : nullptr;
        vec3* scalePtr = sel ? editor_obj_scale_addr(sel) : nullptr;
        if (posPtr) {
            ImVec2 imgPos  = ImGui::GetItemRectMin();
            ImVec2 imgSize = ImGui::GetItemRectSize();

            ImGuizmo_SetRect(imgPos.x, imgPos.y, imgSize.x, imgSize.y);
            ImGuizmo_SetDrawlist(ImGui::GetWindowDrawList());
            ImGuizmo_SetOrthographic(true);

            float matrix[16];
            float t[3] = { posPtr->x, posPtr->y, posPtr->z };
            float r[3] = { rotPtr   ? rotPtr->x   : 0,
                           rotPtr   ? rotPtr->y   : 0,
                           rotPtr   ? rotPtr->z   : 0 };
            float s[3] = { scalePtr ? scalePtr->x : 1,
                           scalePtr ? scalePtr->y : 1,
                           scalePtr ? scalePtr->z : 1 };
            ImGuizmo_RecomposeMatrixFromComponents(t, r, s, matrix);

            // 2D-szelektív op-maszkok: translate csak X|Y, rotate csak Z,
            // scale csak X|Y.
            int globalOp = app.gizmoOp();
            IMGUIZMO_OPERATION op;
            if (globalOp == 120) {        // ROTATE → csak Z
                op = (IMGUIZMO_OPERATION)IMGUIZMO_ROTATE_Z;
            } else if (globalOp == 896) { // SCALE → csak X|Y
                op = (IMGUIZMO_OPERATION)(IMGUIZMO_SCALE_X | IMGUIZMO_SCALE_Y);
            } else {                      // TRANSLATE (default) → X|Y
                op = (IMGUIZMO_OPERATION)
                     (IMGUIZMO_TRANSLATE_X | IMGUIZMO_TRANSLATE_Y);
            }
            if (ImGuizmo_Manipulate(cam_.view, cam_.proj,
                                    op, IMGUIZMO_WORLD,
                                    matrix,
                                    nullptr, nullptr, nullptr, nullptr)) {
                float t2[3], r2[3], s2[3];
                ImGuizmo_DecomposeMatrixToComponents(matrix, t2, r2, s2);
                posPtr->x = t2[0];
                posPtr->y = t2[1];
                if (rotPtr)   rotPtr->z   = r2[2];
                if (scalePtr) {
                    scalePtr->x = s2[0];
                    scalePtr->y = s2[1];
                }
            }
        }

        // Gizmo drag → transaction edge-detection (M9b).
        bool isUsing = ImGuizmo_IsUsing();
        if (isUsing && !wasUsingGizmo_) {
            gizmoTarget_ = sel;
            gizmoSnapshotBefore_ = ObjectStateCommand::snapshot(sel);
        }
        if (!isUsing && wasUsingGizmo_) {
            std::string after = ObjectStateCommand::snapshot(gizmoTarget_);
            if (after != gizmoSnapshotBefore_) {
                app.commands().execute(std::make_unique<ObjectStateCommand>(
                    gizmoTarget_, gizmoSnapshotBefore_, after, "Move (2D)"));
            }
            gizmoTarget_ = nullptr;
            gizmoSnapshotBefore_.clear();
        }
        wasUsingGizmo_ = isUsing;
    }

    ImGui::End();
}

REGISTER_PANEL(Scene2DPanel, 650)

}  // namespace editor
