#pragma once

#include "panel.h"

namespace editor {

class HierarchyPanel : public Panel {
public:
    HierarchyPanel() : Panel("hierarchy", "Hierarchy") {}
    void draw(EditorApp& app) override;
};

}  // namespace editor
