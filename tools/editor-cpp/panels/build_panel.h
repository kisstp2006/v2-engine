#pragma once

#include <string>
#include <vector>

#include "panel.h"

namespace editor {

// Build / Cook UI panel (Phase 5b). A CookRunner kEvtCookStarted/Progress/
// Finished/Cancelled event-jeire iratkozik fel (auto-visible cook-induláskor).
// Mutatja: állapot, progress-bar (current/total), aktuális fájl, cancel
// gomb, log-tail (utolsó N üzenet a Console-tól külön).
class BuildPanel : public Panel {
public:
    BuildPanel() : Panel("build", "Build") { visible = false; }
    void draw(EditorApp& app) override;

private:
    void wireUpIfNeeded(EditorApp& app);

    // Event-driven state (a CookRunner-től érkező event-ek frissítik).
    int                       cur_   = 0;
    int                       total_ = 0;
    std::string               currentFile_;
    std::string               lastResult_;   // "Done: 145 ok, 5 fail"
    std::vector<std::string>  logTail_;      // utolsó 200 cook-üzenet
    bool                      cookActive_ = false;
    bool                      wired_      = false;
};

}  // namespace editor
