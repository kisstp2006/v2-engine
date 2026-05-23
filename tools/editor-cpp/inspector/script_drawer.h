#pragma once

// ScriptDrawer (Phase 6b). Custom Inspector drawer a Script komponensre:
//   - Default reflection (script_path / auto_reload / enabled).
//   - Piros `TextColored` ha `ScriptHost::lastErrorOf(node)` nem üres.
//   - "Reload Script" gomb → app.scriptHost().reloadScript(node).
//   - "Open in IDE" gomb → IdeLauncher::openFile(absPath) (Phase 6c).
//
// Registráció: a `.cpp` fájl alján static-init regisztrál
// `InspectorRegistry::instance().registerDrawer("Script", ...)`-pal.

#include "inspector_registry.h"

namespace editor {

class ScriptDrawer : public IInspectorDrawer {
public:
    void draw(obj* o) override;
};

}  // namespace editor
