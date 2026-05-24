// STL FIRST.
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "postfx_debug_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"

namespace editor {

void PostFXDebugPanel::draw(EditorApp& app) {
    (void)app;
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Engine-built-in ui_fxs() — Fázis 5 debug only.");
    ImGui::TextDisabled("Replaced by custom drawer in Fázis 6.");
    ImGui::Separator();

    // Master enable shortcut — the per-FX list below has its own checkboxes,
    // but a one-click "all off" is handy when stacked FX makes the scene
    // unrecognisable.
    if (ImGui::Button("Enable All"))  fx_enable_all(1);
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) fx_enable_all(0);
    ImGui::Separator();

    // The engine list. Every loaded pass shown with checkbox + reorder
    // buttons + per-uniform sliders. Empty-list path = "No Post FXs..."
    // warning label — that's the engine's own message, not ours.
    ui_fxs();

    ImGui::End();
}

REGISTER_PANEL(PostFXDebugPanel, 950)

}  // namespace editor
