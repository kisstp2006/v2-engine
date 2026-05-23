#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj   // motor function-like macro vs. typedef ütközés
#endif

namespace editor {

class EditorApp;

// Per-type Inspector drawer interface. M16-ban kibővül hint-aware
// rajzolással (asset-picker, color, clamp) és multi-select supporttal.
class IInspectorDrawer {
public:
    virtual ~IInspectorDrawer() = default;
    virtual void draw(obj* o) = 0;
};

// Globális drawer registry. Dispatch: ha van regisztrált drawer a node
// típusára, azt hívjuk; egyébként a default `ui_obj` reflection-render.
class InspectorRegistry {
public:
    static InspectorRegistry& instance();

    void registerDrawer(const char* typeName,
                        std::unique_ptr<IInspectorDrawer> drawer);

    // Egy konkrét node komponens-tartalmának kirajzolása.
    void drawComponents(obj* o);

    // Multi-edit (Phase 2c). A primary (targets[0]) widget-ein keresztül
    // szerkeszthető. Edit-frame-ben a primary mező-értékét memcpy-zi/STRDUP-pal
    // propagálja a többi targets[1..n]-re. Csak HOMOGÉN selection (mind ugyanaz
    // a obj_type) — heterogén esetben a hívó single-mode-ra esik vissza.
    void drawComponentsMulti(const std::vector<obj*>& targets);

    // Phase 6b — public wrapper a default reflection-render köré (a custom
    // drawer-ek is hívhatják, hogy az "alap mezők" után tegyenek a saját
    // UI-jukat).
    void drawDefaults(const std::vector<obj*>& targets);

    // Phase 4a — projekt-kontextus a drop-target/picker relativizálásához.
    // Az InspectorPanel set-eli minden draw elején `app.projectPath()`-ra.
    void setProjectPath(const std::string& p) { projectPath_ = p; }
    const std::string& projectPath() const    { return projectPath_; }

    // Phase 6b — EditorApp* kontextus a drawer-eknek (pl. ScriptDrawer-nek
    // app.scriptHost() és app.bus()). Az InspectorPanel set-eli minden frame
    // elején. Non-owning pointer.
    void       setApp(EditorApp* a) { app_ = a; }
    EditorApp* app() const          { return app_; }

private:
    InspectorRegistry() = default;
    std::unordered_map<std::string, std::unique_ptr<IInspectorDrawer>> drawers_;
    std::string projectPath_;
    EditorApp*  app_ = nullptr;
};

}  // namespace editor
