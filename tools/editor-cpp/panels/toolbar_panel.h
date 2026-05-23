#pragma once

#include "panel.h"

namespace editor {

class ToolbarPanel : public Panel {
public:
    ToolbarPanel() : Panel("toolbar", "Toolbar") {}
    void draw(EditorApp& app) override;
};

}  // namespace editor
