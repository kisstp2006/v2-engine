// STL FIRST (engine `obj`/`is` macro-clash).
#include <memory>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "postfx_stack_drawer.h"
#include "inspector_registry.h"
#include "../components/components_api.h"

namespace editor {

namespace {

// The motor exposes no `fx_pass_count()`; iterate via `fx_name(i)` which
// returns "" for out-of-range slots. A pass is always loaded with a
// non-empty STRDUP'd name (render_postfx.h:191), so the empty check is safe.
int fxPassCount() {
    int n = 0;
    while (true) {
        const char* nm = fx_name(n);
        if (!nm || !*nm) break;
        ++n;
    }
    return n;
}

}  // namespace

void PostFXStackDrawer::draw(obj* o) {
    auto& reg = InspectorRegistry::instance();

    // 1) Default reflection — `enabled` (master on/off) + `fx_dir` text field.
    reg.drawDefaults({o});

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Effects");
    ImGui::Dummy(ImVec2(0, 2));

    // 2) Master buttons. fx_enable_all(0/1) flips every loaded pass at once.
    if (ImGui::Button("Enable All"))  fx_enable_all(1);
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) fx_enable_all(0);
    ImGui::Separator();

    // 3) Per-FX list. Empty list = the project's assets/fx/ is empty (or the
    // editor was launched from the wrong CWD so `loadProjectFXShaders` found
    // nothing). Point the user at the Tools menu remedy.
    int passCount = fxPassCount();
    if (passCount == 0) {
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextDisabled("No FX shaders loaded.");
        ImGui::TextDisabled("Tools \xE2\x86\x92 Import Default FX Shaders");
        return;
    }

    for (int i = 0; i < passCount; ++i) {
        ImGui::PushID(i);

        // Master enable checkbox for this pass. The engine's `fx_enable`
        // also updates `fx->enabled` based on whether any pass is on, so
        // the render-walk's master-flag stays in sync automatically.
        bool enabled = fx_enabled(i) != 0;
        if (ImGui::Checkbox("##en", &enabled)) {
            fx_enable(i, enabled ? 1 : 0);
        }
        ImGui::SameLine();

        // Collapsible header — open it for per-uniform sliders + Move up/down
        // (rendered by the engine's `ui_fx(pass)`). SpanAvailWidth makes the
        // entire row clickable, not just the small triangle.
        const char* name = fx_name(i);
        if (ImGui::TreeNodeEx(name, ImGuiTreeNodeFlags_SpanAvailWidth)) {
            // Engine UI — Move up/down buttons + a slider per shader uniform.
            // Uses cimgui (same ImGui-context as the editor), so the widgets
            // appear inline. PushID above keeps slot-ids unique.
            ui_fx(i);
            ImGui::TreePop();
        }

        ImGui::PopID();
    }

    (void)o;  // o is the PostFXStack node; per-pass state lives engine-side.
}

// Static-init registration (mirror of MeshRendererDrawer/ScriptDrawer).
namespace {
struct PostFXStackDrawerRegistrar {
    PostFXStackDrawerRegistrar() {
        InspectorRegistry::instance().registerDrawer(
            "PostFXStack", std::make_unique<PostFXStackDrawer>());
    }
};
static PostFXStackDrawerRegistrar _reg;
}  // namespace

}  // namespace editor
