#pragma once

#include "panel.h"

namespace editor {

// Temporary debug panel for PostFX Fázis 5 testing. Exposes the engine's
// built-in `ui_fxs()` UI — a list of every loaded FX pass with per-pass
// enable checkbox + uniform sliders. The custom drawer (Fázis 6) replaces
// this once the per-FX UI is wired up to the PostFXStack node.
//
// Default-hidden — toggle from Window menu.
class PostFXDebugPanel : public Panel {
public:
    PostFXDebugPanel() : Panel("postfx-debug", "PostFX (debug)") {
        visible = false;
    }
    void draw(EditorApp& app) override;
};

}  // namespace editor
