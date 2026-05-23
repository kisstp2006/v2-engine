#pragma once

#include <string>

#include "panel.h"

namespace editor {

class ProjectPanel : public Panel {
public:
    ProjectPanel() : Panel("project", "Project") {}
    void draw(EditorApp& app) override;

private:
    // Aktuális mappa (abszolút path). Üres = még nincs inicializálva (első
    // draw-ban `projectPath_/assets`-re állítjuk be).
    std::string current_dir_;
};

}  // namespace editor
