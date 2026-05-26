#pragma once

#include <array>
#include <string>
#include <vector>

#include "panel.h"

namespace editor {

// Profiler panel — surfaces the motor's `profiler` map (FPS, frame-time,
// `profile()` timers, `profile_incstat/setstat` counters) plus our own
// EDITOR_PROFILE-recorded sections. Built to be persistent and extensible:
// new render-walk timings just need a new EDITOR_PROFILE("Editor.<name>")
// scope; they show up here automatically.
class ProfilerPanel : public Panel {
public:
    ProfilerPanel() : Panel("profiler", "Profiler") { visible = false; }
    void draw(EditorApp& app) override;

private:
    // Rolling frame-time / FPS history for the plot. ~4 seconds @ 60fps.
    static constexpr int kHistoryMax = 240;

    bool   enabled_              = true;
    bool   reset_counters_yearly_ = true;   // reset stats at end of every frame
    bool   pause_history_        = false;
    char   filter_[128]          = {};

    std::array<float, kHistoryMax> frame_ms_hist_{};
    std::array<float, kHistoryMax> fps_hist_{};
    int                            hist_offset_ = 0;
    bool                           hist_full_   = false;

    void pushHistory_(float fps, float frame_ms);
    void drawHistoryPlot_();
    void drawTimingsTable_();
    void drawCountersTable_();
};

}  // namespace editor
