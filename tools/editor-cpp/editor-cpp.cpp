// tools/editor-cpp/editor-cpp.cpp
//
// M0.5a — Kétmódú bootstrap:
//   - Project picker (transzparens) ha nincs --project flag.
//   - Dockolható szerkesztő ha van --project <path>.
// Picker "Open" gombja system()-mel újraindítja a process-t --project-tel.

// STL include-ok ELŐSZÖR — a motor engine.h `is(...)` rövid makrója
// ütközik az MSVC <xstring> belsejével. Ezért STL → motor sorrend.
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
    // Recent-be felemeljük a megnyitott projektet.
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
        // Process restart: új editor-cpp.exe `--project <path>` flag-gel.
        editor::launchEditorWithProject(argv(0), picker.path());
    }
    return 0;
}

}  // namespace

int main() {
    const std::string projectPath = parseProjectFlag();
    return projectPath.empty() ? runPicker() : runEditor(projectPath);
}
