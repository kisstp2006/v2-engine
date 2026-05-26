#pragma once

#include "panel.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class HierarchyPanel : public Panel {
public:
    HierarchyPanel() : Panel("hierarchy", "Hierarchy") {}
    void draw(EditorApp& app) override;

    // Inline rename state. `renaming_node_` non-null means we render
    // an ImGui::InputText in place of the tree label for this node.
    // The buffer is sized for typical scene-node names (Unity caps at
    // 64; we double that for safety).
    obj*  renaming_node_     = nullptr;
    char  rename_buf_[128]   = {};
    bool  rename_grab_focus_ = false;
};

}  // namespace editor
