// STL FIRST (engine `obj`/`is` macro-clash).
// core/ide_launcher.h (and ide_helper.hpp inside it) also comes in here,
// BEFORE engine.h reads the `ifdef_*` macros, which would corrupt the
// internals of `<vector>` and `<filesystem>`.
#include <memory>
#include <string>
#include <vector>

#include "../core/ide_launcher.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "script_drawer.h"
#include "inspector_registry.h"
#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../runtime/script_host.h"

namespace editor {

void ScriptDrawer::draw(obj* o) {
    auto& reg = InspectorRegistry::instance();
    EditorApp* app = reg.app();

    // 1) Default reflection (path / auto_reload / enabled).
    reg.drawDefaults({o});

    if (!app) return;

    ImGui::Separator();

    // 2) Last error (red).
    std::string err = app->scriptHost().lastErrorOf(o);
    if (!err.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.40f, 0.40f, 1.0f),
                           "Lua error:");
        // Light-red panel
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
                              ImVec4(0.18f, 0.08f, 0.08f, 0.60f));
        if (ImGui::BeginChild("##script_err",
                              ImVec2(0, 80), true,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.85f, 0.85f, 1.0f));
            ImGui::TextWrapped("%s", err.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();
    } else if (app->scriptHost().hasVm(o)) {
        ImGui::TextDisabled("Lua VM: loaded.");
    } else {
        ImGui::TextDisabled("Lua VM: not loaded (run Play mode).");
    }

    // 3) Reload Script + Open in IDE
    const char* relPath = editor_script_path(o);
    const bool  hasPath = (relPath && *relPath);

    if (ImGui::Button("Reload Script")) {
        if (!hasPath) {
            app->bus().emit(kEvtLogWarn,
                std::string("[Script] no path set — cannot reload"));
        } else {
            bool ok = app->scriptHost().reloadScript(o);
            app->bus().emit(ok ? kEvtLogInfo : kEvtLogWarn,
                std::string(ok ? "[Script] reloaded: " : "[Script] reload failed: ")
                + relPath);
        }
    }
    ImGui::SameLine();

    if (!hasPath) ImGui::BeginDisabled();
    if (ImGui::Button("Open in IDE")) {
        std::string abs = asset_path::toAbsolute(relPath, app->projectPath());
        if (IdeLauncher::instance().openFile(abs)) {
            app->bus().emit(kEvtLogInfo,
                std::string("[IDE] opened: ") + abs);
        } else {
            app->bus().emit(kEvtLogWarn,
                std::string("[IDE] no IDE detected"));
        }
    }
    if (!hasPath) ImGui::EndDisabled();
}

// Static-init registration (like the panel_registry pattern).
namespace {
struct ScriptDrawerRegistrar {
    ScriptDrawerRegistrar() {
        InspectorRegistry::instance().registerDrawer(
            "Script", std::make_unique<ScriptDrawer>());
    }
};
static ScriptDrawerRegistrar _reg;
}  // namespace

}  // namespace editor
