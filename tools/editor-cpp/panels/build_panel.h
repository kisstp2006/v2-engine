#pragma once

#include <string>
#include <vector>

#include "panel.h"

namespace editor {

// Build / Cook UI panel (Phase 5b). Subscribes to the CookRunner's
// kEvtCookStarted/Progress/Finished/Cancelled events (auto-visible on cook-start).
// Shows: state, progress-bar (current/total), current file, cancel
// button, log-tail (last N messages, separate from the Console).
class BuildPanel : public Panel {
public:
    BuildPanel() : Panel("build", "Build") { visible = false; }
    void draw(EditorApp& app) override;

private:
    void wireUpIfNeeded(EditorApp& app);

    // Event-driven state (updated by events from the CookRunner).
    int                       cur_   = 0;
    int                       total_ = 0;
    std::string               currentFile_;
    std::string               lastResult_;   // "Done: 145 ok, 5 fail"
    std::vector<std::string>  logTail_;      // last 200 cook-messages
    bool                      cookActive_ = false;
    bool                      wired_      = false;
};

}  // namespace editor
