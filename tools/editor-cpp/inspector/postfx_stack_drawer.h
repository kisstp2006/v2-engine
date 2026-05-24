#pragma once

// PostFXStackDrawer (PostFX Fázis 6). Custom Inspector drawer for the
// PostFXStack scene-singleton:
//   - Default reflection (`enabled` master + `fx_dir`).
//   - Master "Enable All" / "Disable All" buttons.
//   - Per-FX list: checkbox + name + collapsible that hosts the engine's
//     built-in `ui_fx(pass)` per-uniform UI (also has Move up/down).
//
// Registration: bottom of the .cpp file static-inits with
//   InspectorRegistry::instance().registerDrawer("PostFXStack", ...)

#include "inspector_registry.h"

namespace editor {

class PostFXStackDrawer : public IInspectorDrawer {
public:
    void draw(obj* o) override;
};

}  // namespace editor
