// tools/editor-cpp/editor-cpp.cpp
//
// M0.5a — Two-mode bootstrap:
//   - Project picker (transparent) if no --project flag.
//   - Dockable editor if --project <path> is set.
// Picker's "Open" button restarts the process with --project via system().

// STL includes FIRST — the engine's short `is(...)` macro from engine.h
// clashes with MSVC <xstring> internals. Hence STL → engine order.
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine.h"

#include "app/editor_style.h"
#include "app/editor_app.h"
#include "core/recent_projects.h"
#include "core/process_launcher.h"
#include "picker/project_picker.h"

namespace {

std::string parseProjectFlag() {
    const int n = argc();
    for (int i = 1; i + 1 < n; ++i) {
        const char* a = argv(i);
        if (a && std::strcmp(a, "--project") == 0) {
            const char* p = argv(i + 1);
            return p ? std::string(p) : std::string();
        }
    }
    return {};
}

int runEditor(const std::string& projectPath) {
    // Promote the opened project to Recent.
    editor::RecentProjects recents;
    recents.load();
    recents.touch(projectPath);
    recents.save();

    app_create(80, APP_DOCKING | APP_VIEWPORTS | APP_MSAA4);
    command("ui.debug=0");
    editor::applyStyle();

    editor::EditorApp app(projectPath);
    app.run();
    return 0;
}

int runPicker() {
    app_create(100, APP_TRANSPARENT | APP_MSAA2);
    command("ui.debug=0");
    editor::applyStyle();

    editor::ProjectPicker picker;
    while (app_swap() && !input(KEY_ESC) && !picker.done()) {
        picker.draw();
    }

    if (picker.action() == editor::ProjectPickerAction::Open) {
        // Process restart: new editor-cpp.exe with `--project <path>` flag.
        editor::launchEditorWithProject(argv(0), picker.path());
    }
    return 0;
}

}  // namespace

int main() {
    const std::string projectPath = parseProjectFlag();
    return projectPath.empty() ? runPicker() : runEditor(projectPath);
}
