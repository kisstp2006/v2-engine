#pragma once

#include <string>

#include "panel.h"

namespace editor {

class ProjectPanel : public Panel {
public:
    ProjectPanel() : Panel("project", "Project") {}
    void draw(EditorApp& app) override;

private:
    // Current folder (absolute path). Empty = not yet initialized (set to
    // `projectPath_/assets` on the first draw).
    std::string current_dir_;
};

}  // namespace editor
