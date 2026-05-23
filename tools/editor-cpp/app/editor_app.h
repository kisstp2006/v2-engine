#pragma once

#include <memory>
#include <string>
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
class CookRunner;

class EditorApp {
public:
    explicit EditorApp(std::string projectPath);
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    // Belép a fő render-loop-ba; ESC-ig vagy `quit()`-ig.
    void run();

    void quit() { quit_ = true; }
    void resetDockLayout() { needResetLayout_ = true; }

    // Visszaad egy érvényes scene-root-ot. Ha `scene_.root() == nullptr`
    // (pl. korrupt scene-fájl Open után), létrejön egy default "Scene" root
    // és Console-warningot ad. Minden createXxx ezt használja parent-NULL
    // esetén — silent fail helyett.
    obj* ensureRoot();

    // Új üres node (Transform) a parent alá (vagy a scene root alá).
    // Selection-re is set-eli az új node-ot.
    obj* createEmpty(obj* parent = nullptr);

    // Új Mesh node (MeshRenderer). model_path lehet üres/null.
    obj* createMesh(const char* model_path = "", obj* parent = nullptr);

    // Új Sprite node (SpriteRenderer). texture_path lehet üres/null.
    obj* createSprite(const char* texture_path = "", obj* parent = nullptr);

    // Új Tilemap node (TilemapRef). tmx_path lehet üres/null.
    obj* createTilemap(const char* tmx_path = "", obj* parent = nullptr);

    // Új Light node. type: 0=DIRECTIONAL, 1=POINT, 2=SPOT.
    obj* createLight(int type, obj* parent = nullptr);

    // Új CameraRef node (alapból is_active=true).
    obj* createCamera(obj* parent = nullptr);

    // Új AudioSource node. clip_path lehet üres/null.
    obj* createAudioSource(const char* clip_path = "", obj* parent = nullptr);

    // Prefab — egy subtree-t fájlba ment (.prefab.json5).
    void saveSelectedAsPrefab();

    // Prefab — .prefab.json5 betöltése, a scene root alá köti.
    void spawnPrefab(const char* path);

    // Új Script node. script_path lehet üres/null.
    obj* createScript(const char* script_path = "", obj* parent = nullptr);

    // Save / Open scene (JSON5).
    void saveScene();       // Ctrl+S — lastSavedPath_-re ment vagy saveSceneAs.
    void saveSceneAs();
    void openScene();                              // pickFile + openScene(path)
    void openScene(const std::string& path);       // közvetlen path-megnyitás

    // Window-title fresh frissítés ("<scene>* — editor-cpp [<project>]").
    // A motor app_swap-pje SDL_SetWindowTitle-lel applikálja.
    void refreshWindowTitle();

    EventBus&         bus()       { return bus_; }
    SceneService&     scene()     { return scene_; }
    SelectionService& selection() { return selection_; }
    CommandStack&     commands()  { return commands_; }
    PlayMode&         play()      { return play_; }
    ScriptHost&       scriptHost();                      /* lazy-init */
    CookRunner&       cookRunner();                       /* lazy-init */
    MainThreadQueue&  mainQueue() { return mainQueue_; }
    int&              gizmoOp()   { return gizmoOp_; }   /* IMGUIZMO_OPERATION */
    const std::string& projectPath() const { return projectPath_; }
    const std::vector<std::unique_ptr<Panel>>& panels() const { return panels_; }

    // Phase 5a — cook-vezérlés (Tools menü és BuildPanel hívja).
    void startCookInPlaceAllAssets();
    void startBuildCookZip();
    void requestCookCancel();

    // Phase 5c — asset-validation (Tools menü "Validate Assets"). Console-
    // ba ír összefoglalót és per-issue részleteket. Pre-cook automatikus
    // hívás belülről történik a start* metódusokban.
    void runAssetValidation();

    // Phase 6a — engine.ffi → engine.d.lua (EmmyLua stub) generálás +
    // .luarc.json + .vscode/settings.json. `force=false` → csak ha az
    // engine.ffi mtime > stub mtime (mtime-cache).
    void generateLuaStubs(bool force);

private:
    void buildPanels();
    void wireUp();
    void drawFrame();
    void drawDockHost();
    void drawMenubar();
    void drawCookValidationPopup();   // Phase 5c

    // Phase 4a — projekt-relatív path-konvertáció. Ha az `inputPath` abszolút
    // és projekt-en belül van → "assets/.../foo.iqm" relatívra. Ha üres vagy
    // projekt-en kívül → változatlanul (kívül-esetben Console warning).
    std::string makeRelativeIfInside(const char* inputPath);

    // Phase 5c — pre-cook validation prompt state. Ha vannak Error-szintű
    // issue-k a validation után, a tényleges cook-ot defer-eljük egy modal-
    // popup eldöntéséig (Continue / Cancel).
    enum class PendingCookKind { None, InPlace, BuildZip };
    struct PendingCookPrompt {
        PendingCookKind          kind = PendingCookKind::None;
        std::vector<std::string> paths;
        std::string              zipPath;
        std::vector<AssetIssue>  issues;
        bool                     openRequested = false;  // 1 frame-re true
    };
    PendingCookPrompt cookPrompt_;

    std::string      projectPath_;
    std::string      lastSavedPath_;     // Save Scene utáni path (Ctrl+S target)
    bool             isDirty_ = false;   // window-title `*` indikátor
    EventBus         bus_;
    MainThreadQueue  mainQueue_;
    SceneService     scene_;
    SelectionService selection_{bus_};
    CommandStack     commands_;
    PlayMode         play_;
    std::unique_ptr<ScriptHost>  scriptHost_;
    std::unique_ptr<CookRunner>  cookRunner_;
    /* IMGUIZMO_OPERATION: 7 = TRANSLATE, 120 = ROTATE, 896 = SCALE_UNI
       (full-axis variánsok). Kezdetben TRANSLATE. */
    int              gizmoOp_ = 7;
    std::vector<std::unique_ptr<Panel>> panels_;
    ConsolePanel* console_ = nullptr;     // non-owning, into panels_
    bool        quit_ = false;
    bool        needResetLayout_ = false;
};

}  // namespace editor
