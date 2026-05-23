#pragma once

// ScriptDrawer (Phase 6b). Custom Inspector drawer for the Script component:
//   - Default reflection (script_path / auto_reload / enabled).
//   - Red `TextColored` if `ScriptHost::lastErrorOf(node)` is not empty.
//   - "Reload Script" button → app.scriptHost().reloadScript(node).
//   - "Open in IDE" button → IdeLauncher::openFile(absPath) (Phase 6c).
//
// Registration: the bottom of the `.cpp` file registers via static-init
// with `InspectorRegistry::instance().registerDrawer("Script", ...)`.

#include "inspector_registry.h"

namespace editor {

class ScriptDrawer : public IInspectorDrawer {
public:
    void draw(obj* o) override;
};

}  // namespace editor
