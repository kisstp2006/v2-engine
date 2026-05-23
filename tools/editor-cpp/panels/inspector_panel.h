#pragma once

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "panel.h"

namespace editor {

class InspectorPanel : public Panel {
public:
    InspectorPanel() : Panel("inspector", "Inspector") {}
    void draw(EditorApp& app) override;

private:
    // Multi-edit transaction edge-detection (M9c + Phase 2c).
    bool                     wasEditing_ = false;
    std::vector<obj*>        editTargets_;
    std::vector<std::string> editBefores_;
};

}  // namespace editor
