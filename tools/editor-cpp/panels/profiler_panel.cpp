// STL FIRST.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "profiler_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../components/components_api.h"

namespace editor {

namespace {

// Substring-match helper for the filter input. Case-insensitive, empty
// pattern matches everything.
bool matchesFilter(const char* name, const char* pattern) {
    if (!pattern || !*pattern) return true;
    if (!name) return false;
    // Lowercase compare without allocating.
    size_t plen = strlen(pattern);
    size_t nlen = strlen(name);
    if (nlen < plen) return false;
    for (size_t i = 0; i + plen <= nlen; ++i) {
        bool hit = true;
        for (size_t j = 0; j < plen; ++j) {
            char a = (char)tolower((unsigned char)name[i + j]);
            char b = (char)tolower((unsigned char)pattern[j]);
            if (a != b) { hit = false; break; }
        }
        if (hit) return true;
    }
    return false;
}

// Snapshot the profiler map into a std::vector. We re-pull every frame —
// the profiler map typically has <100 entries so the copy is trivial.
std::vector<editor_profile_entry_t> snapshot() {
    int n = editor_profiler_count();
    std::vector<editor_profile_entry_t> out(n);
    if (n > 0) {
        int got = editor_profiler_collect(out.data(), n);
        out.resize(got);
    }
    return out;
}

}  // namespace

void ProfilerPanel::pushHistory_(float fps, float frame_ms) {
    if (pause_history_) return;
    frame_ms_hist_[hist_offset_] = frame_ms;
    fps_hist_     [hist_offset_] = fps;
    hist_offset_++;
    if (hist_offset_ >= kHistoryMax) {
        hist_offset_ = 0;
        hist_full_   = true;
    }
}

void ProfilerPanel::drawHistoryPlot_() {
    const int count = hist_full_ ? kHistoryMax : hist_offset_;
    if (count <= 1) {
        ImGui::TextDisabled("(collecting samples…)");
        return;
    }
    // Reorder the rolling buffer into a contiguous view for PlotLines. With
    // a wrap-around offset, ImGui's `offset` parameter handles the cycle
    // start so we don't need to copy.
    const int offset = hist_full_ ? hist_offset_ : 0;

    // Auto-scale Y to the recent max+headroom (smooths to avoid jitter).
    // Parens around `std::max` defeat Windows.h's `max` macro expansion.
    float max_ms = 0.f;
    for (int i = 0; i < count; ++i) {
        if (frame_ms_hist_[i] > max_ms) max_ms = frame_ms_hist_[i];
    }
    if (max_ms < 16.0f) max_ms = 16.0f;     // baseline 60fps line
    max_ms *= 1.15f;

    // ms plot (top).
    char overlay[64];
    snprintf(overlay, sizeof(overlay), "frame: %.2f ms (max %.1f)",
             frame_ms_hist_[(hist_offset_ + kHistoryMax - 1) % kHistoryMax],
             max_ms);
    ImGui::PlotLines("##frame_ms", frame_ms_hist_.data(), count, offset,
                     overlay, 0.0f, max_ms, ImVec2(-1, 60));
}

void ProfilerPanel::drawTimingsTable_() {
    auto entries = snapshot();
    // Filter + keep timers only.
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [this](const editor_profile_entry_t& e) {
            return !e.is_timer || !matchesFilter(e.name, filter_);
        }), entries.end());
    // Sort by avg_ms descending so the worst offenders bubble to the top.
    std::sort(entries.begin(), entries.end(),
        [](const editor_profile_entry_t& a, const editor_profile_entry_t& b) {
            return a.avg_ms > b.avg_ms;
        });

    if (ImGui::BeginTable("##timings", 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
          | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Section");
        ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("99.99 ms").x + 16.0f);
        ImGui::TableHeadersRow();

        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.name ? e.name : "(unnamed)");
            ImGui::TableSetColumnIndex(1);
            // Color hot lines red, mid yellow, fast green.
            ImVec4 col = (e.avg_ms > 8.0f) ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                       : (e.avg_ms > 2.0f) ? ImVec4(1.0f, 0.9f, 0.4f, 1.0f)
                                           : ImVec4(0.6f, 1.0f, 0.6f, 1.0f);
            ImGui::TextColored(col, "%6.3f", e.avg_ms);
        }
        ImGui::EndTable();
    }
}

void ProfilerPanel::drawCountersTable_() {
    auto entries = snapshot();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
        [this](const editor_profile_entry_t& e) {
            return e.is_timer || !matchesFilter(e.name, filter_);
        }), entries.end());
    std::sort(entries.begin(), entries.end(),
        [](const editor_profile_entry_t& a, const editor_profile_entry_t& b) {
            // Counters: sort by name.
            return strcmp(a.name ? a.name : "", b.name ? b.name : "") < 0;
        });

    if (ImGui::BeginTable("##counters", 2,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
          | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
            ImVec2(0, 0))) {
        ImGui::TableSetupColumn("Counter");
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
                                ImGui::CalcTextSize("1234567890").x + 16.0f);
        ImGui::TableHeadersRow();

        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(e.name ? e.name : "(unnamed)");
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%g", e.stat);
        }
        ImGui::EndTable();
    }
}

void ProfilerPanel::draw(EditorApp& /*app*/) {
    if (!visible) return;
    if (ImGui::Begin(title_.c_str(), &visible)) {
        // --- top bar ---
        ImGui::Checkbox("Enabled", &enabled_);
        ImGui::SameLine();
        ImGui::Checkbox("Pause", &pause_history_);
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset counters")) {
            editor_profiler_reset_counters();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear all")) {
            editor_profiler_clear();
            // Also reset the history plot so the graph starts fresh.
            hist_offset_ = 0;
            hist_full_   = false;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180);
        ImGui::InputTextWithHint("##filter", "Filter (e.g. Editor.)",
                                 filter_, sizeof(filter_));

        if (!enabled_) {
            ImGui::TextDisabled("Profiler is paused (the Enabled toggle is off).");
            ImGui::End();
            return;
        }

        // --- frame headline + plot ---
        // NOTE: app_fps() is `#define app_fps() fps()` and would shadow a
        // local named `fps` — keep this variable named differently.
        const float fps_now = (float)app_fps();
        const float dt_ms   = (float)(app_delta() * 1000.0);
        pushHistory_(fps_now, dt_ms);

        ImGui::Separator();
        ImGui::Text("FPS: ");
        ImGui::SameLine();
        ImVec4 fps_col = (fps_now >= 58.0f) ? ImVec4(0.6f, 1.0f, 0.6f, 1.0f)
                       : (fps_now >= 30.0f) ? ImVec4(1.0f, 0.9f, 0.4f, 1.0f)
                                            : ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
        ImGui::TextColored(fps_col, "%.1f", fps_now);
        ImGui::SameLine();
        ImGui::TextDisabled("   |   frame: %.2f ms", dt_ms);

        drawHistoryPlot_();

        // --- timings / counters ---
        ImGui::Separator();
        ImGui::TextDisabled("Timings (avg, ms — sorted by cost)");
        // Constrain the timings table to ~half the remaining height so the
        // counters table beneath still has room.
        const float remH = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##timings_box", ImVec2(0, remH * 0.55f), false);
        drawTimingsTable_();
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::TextDisabled("Counters (drawcalls, triangles, etc.)");
        ImGui::BeginChild("##counters_box", ImVec2(0, 0), false);
        drawCountersTable_();
        ImGui::EndChild();
    }
    ImGui::End();

    // End-of-frame counter reset. The motor's `ui_profiler` does this inline
    // while iterating; we mirror it so a counter like `Render.num_drawcalls`
    // shows per-frame deltas rather than accumulating forever.
    if (enabled_ && reset_counters_yearly_) {
        editor_profiler_reset_counters();
    }
}

REGISTER_PANEL(ProfilerPanel, 960)

}  // namespace editor
