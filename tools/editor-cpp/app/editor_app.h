#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "../core/event_bus.h"
#include "../core/selection_service.h"
#include "../core/task_queue.h"
#include "../scene/scene_service.h"
#include "../commands/command.h"
#include "../runtime/play_mode.h"
#include "../runtime/asset_validator.h"

namespace editor {

class Panel;
class ConsolePanel;
class ScriptHost;
class LuaRepl;
class CookRunner;

class EditorApp {
public:
    explicit EditorApp(std::string projectPath);
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // Enter the main render-loop; until ESC or `quit()`.
    void run();

    void quit() { quit_ = true; }
    void resetDockLayout() { needResetLayout_ = true; }

    // Returns a valid scene-root. If `scene_.root() == nullptr`
    // (e.g. corrupt scene-file after Open), creates a default "Scene" root
    // and emits a Console-warning. Every createXxx uses this when parent
    // is NULL — instead of silent fail.
    obj* ensureRoot();

    // New empty node (Transform) under parent (or under the scene root).
    // Also sets the new node on Selection.
    obj* createEmpty(obj* parent = nullptr);

    // New Mesh node (MeshRenderer). model_path may be empty/null.
    obj* createMesh(const char* model_path = "", obj* parent = nullptr);

    // New Sprite node (SpriteRenderer). texture_path may be empty/null.
    obj* createSprite(const char* texture_path = "", obj* parent = nullptr);

    // New Tilemap node (TilemapRef). tmx_path may be empty/null.
    obj* createTilemap(const char* tmx_path = "", obj* parent = nullptr);

    // New Light node. type: 0=DIRECTIONAL, 1=POINT, 2=SPOT.
    obj* createLight(int type, obj* parent = nullptr);

    // New CameraRef node (is_active=true by default).
    obj* createCamera(obj* parent = nullptr);

    // New AudioSource node. clip_path may be empty/null.
    obj* createAudioSource(const char* clip_path = "", obj* parent = nullptr);

    // New FogSettings node — scene-wide singleton (global fog parameters).
    obj* createFogSettings(obj* parent = nullptr);

    // New Skybox node — scene-wide singleton (environment map background + IBL).
    obj* createSkybox(const char* sky_path = "", obj* parent = nullptr);

    // New PostFXStack node — scene-wide singleton (post-process pipeline).
    obj* createPostFXStack(obj* parent = nullptr);

    // New ShadowSettings node — scene-wide singleton (shadowmap_t globals).
    obj* createShadowSettings(obj* parent = nullptr);

    // New TextRenderer node — screen-space text overlay.
    obj* createTextRenderer(const char* text = "", obj* parent = nullptr);

    // New Text3DRenderer node — world-space billboard text.
    obj* createText3DRenderer(const char* text = "", obj* parent = nullptr);

    // Prefab — save a subtree to a file (.prefab.json5).
    void saveSelectedAsPrefab();

    // Prefab — load .prefab.json5, attach under the scene root.
    void spawnPrefab(const char* path);

    // New Script node. script_path may be empty/null.
    obj* createScript(const char* script_path = "", obj* parent = nullptr);

    // Save / Open scene (JSON5).
    void saveScene();       // Ctrl+S — save to lastSavedPath_ or saveSceneAs.
    void saveSceneAs();
    void openScene();                              // pickFile + openScene(path)
    void openScene(const std::string& path);       // direct path open

    // Window-title fresh update ("<scene>* — editor-cpp [<project>]").
    // The engine's app_swap applies it via SDL_SetWindowTitle.
    void refreshWindowTitle();

    EventBus&         bus()       { return bus_; }
    SceneService&     scene()     { return scene_; }
    SelectionService& selection() { return selection_; }
    CommandStack&     commands()  { return commands_; }
    PlayMode&         play()      { return play_; }
    ScriptHost&       scriptHost();                      /* lazy-init */
    LuaRepl&          luaRepl();                          /* lazy-init */
    CookRunner&       cookRunner();                       /* lazy-init */
    MainThreadQueue&  mainQueue() { return mainQueue_; }
    int&              gizmoOp()   { return gizmoOp_; }   /* IMGUIZMO_OPERATION */
    const std::string& projectPath() const { return projectPath_; }
    const std::vector<std::unique_ptr<Panel>>& panels() const { return panels_; }

    // Phase 5a — cook-control (called by Tools menu and BuildPanel).
    void startCookInPlaceAllAssets();
    void startBuildCookZip();
    void requestCookCancel();

    // Phase 5c — asset-validation (Tools menu "Validate Assets"). Writes
    // a summary and per-issue details to Console. Pre-cook automatic
    // calls happen internally inside the start* methods.
    void runAssetValidation();

    // Phase 6a — engine.ffi → engine.d.lua (EmmyLua stub) generation +
    // .luarc.json + .vscode/settings.json. `force=false` → only if
    // engine.ffi mtime > stub mtime (mtime-cache).
    void generateLuaStubs(bool force);

    // PostFX — copy bundled `tools/editor-cpp/embed/fx/*.glsl` into the
    // current project's `assets/fx/`. Skip-existing mode: never clobbers a
    // user-edited shader file. Console-log: "[FX] imported N / 28 (skipped M)".
    void importDefaultFXShaders();

    // Material asset — create a new `<project>/assets/materials/<name>.mat.json5`
    // with default-material contents. If a name collision exists, appends `_2`,
    // `_3`, etc. so we never overwrite existing assets. Console-log on success
    // or failure. Triggered from the Tools menu (modal dialog handled by the
    // menubar — see drawMenubar()).
    void createMaterialAsset(const std::string& name);

    // PostFX — scan `<project>/assets/fx/*.glsl` and feed every NEW shader to
    // `fx_load_from_mem`. State-tracked: each file is loaded at most once
    // per editor session (manual restart picks up newly imported shaders).
    // No-op when no project is loaded or the fx-dir is missing.
    // Called from the EditorApp ctor and from openScene().
    void loadProjectFXShaders();

private:
    void buildPanels();
    void wireUp();
    void drawFrame();
    void drawDockHost();
    void drawMenubar();
    void drawCookValidationPopup();   // Phase 5c
    void drawNewMaterialPopup();      // Blokk 2.2 — Tools → New Material Asset modal

    // Phase 4a — project-relative path conversion. If `inputPath` is absolute
    // and inside the project → "assets/.../foo.iqm" relative. If empty or
    // outside the project → unchanged (outside-case Console warning).
    std::string makeRelativeIfInside(const char* inputPath);

    // Phase 5c — pre-cook validation prompt state. If there are Error-level
    // issues after validation, defer the actual cook until a modal-popup
    // decision (Continue / Cancel).
    enum class PendingCookKind { None, InPlace, BuildZip };
    struct PendingCookPrompt {
        PendingCookKind          kind = PendingCookKind::None;
        std::vector<std::string> paths;
        std::string              zipPath;
        std::vector<AssetIssue>  issues;
        bool                     openRequested = false;  // true for 1 frame
    };
    PendingCookPrompt cookPrompt_;

    // Blokk 2.2 — New Material Asset modal trigger flag (cleared after the
    // popup processes its OpenPopup call). Buffer survives across frames
    // while the modal is open.
    bool             newMaterialPromptOpen_ = false;
    char             newMaterialNameBuf_[128] = {};

    std::string      projectPath_;
    std::string      lastSavedPath_;     // path after Save Scene (Ctrl+S target)
    bool             isDirty_ = false;   // window-title `*` indicator
    // PostFX shader names already fed to fx_load_from_mem (dedupe — the
    // engine-side `fx_load_from_mem` does NOT check for duplicates, unlike
    // `fx_load` which has its own `set(char*) added`). Stored by filename
    // only (e.g. "fxBloom.glsl"), not absolute path, so per-project moves
    // don't re-load duplicates.
    std::unordered_set<std::string> loadedFXNames_;
    EventBus         bus_;
    MainThreadQueue  mainQueue_;
    SceneService     scene_;
    SelectionService selection_{bus_};
    CommandStack     commands_;
    PlayMode         play_;
    std::unique_ptr<ScriptHost>  scriptHost_;
    std::unique_ptr<LuaRepl>     luaRepl_;
    std::unique_ptr<CookRunner>  cookRunner_;
    /* IMGUIZMO_OPERATION: 7 = TRANSLATE, 120 = ROTATE, 896 = SCALE_UNI
       (full-axis variants). Initially TRANSLATE. */
    int              gizmoOp_ = 7;
    std::vector<std::unique_ptr<Panel>> panels_;
    ConsolePanel* console_ = nullptr;     // non-owning, into panels_
    bool        quit_ = false;
    bool        needResetLayout_ = false;
};

}  // namespace editor
