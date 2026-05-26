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

    // After a directory double-click we change current_dir_, but the second
    // click's mouse-release event lands on the NEXT frame where the new
    // listing is showing — which can cause whatever asset is at the cursor
    // position to be auto-selected. We block setSelectedAsset for a short
    // window after a dir change to suppress that ghost-click. Time-based
    // (ImGui::GetTime) so framerate doesn't change the window.
    double      select_cooldown_until_ = 0.0;

    // New-asset modal state. `pending_new_kind_` is non-zero when a
    // right-click → "New <Type>" was chosen but the name prompt hasn't
    // closed yet. The popup is rendered inline at the end of draw().
    enum NewKind { NK_None = 0, NK_Folder, NK_Material, NK_Scene, NK_Script };
    int   pending_new_kind_ = NK_None;
    char  new_name_buf_[128] = {};
    bool  new_name_grab_focus_ = false;
};

}  // namespace editor
