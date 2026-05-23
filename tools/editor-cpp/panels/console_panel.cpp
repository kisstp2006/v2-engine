// STL ELŐSZÖR.
#include <string>
#include <vector>

#include "engine.h"
#include "console_panel.h"
#include "../app/panel_registry.h"

namespace editor {

void ConsolePanel::log(const std::string& msg, LogSeverity sev) {
    lines_.push_back({msg, sev});
    // Vissza-cap-elés hogy ne nőjjön végtelenül.
    constexpr size_t kMax = 1000;
    if (lines_.size() > kMax) {
        lines_.erase(lines_.begin(), lines_.begin() + (lines_.size() - kMax));
    }
}

void ConsolePanel::draw(EditorApp& /*app*/) {
    if (!visible) return;
    if (ImGui::Begin(title_.c_str(), &visible)) {
        // Top bar: Clear + auto-scroll toggle
        if (ImGui::SmallButton("Clear")) clear();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &autoScroll_);
        ImGui::Separator();

        // Phase 3e — Severity-szerinti színek.
        constexpr ImVec4 kColInfo  = ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        constexpr ImVec4 kColWarn  = ImVec4(1.00f, 0.85f, 0.30f, 1.0f);
        constexpr ImVec4 kColError = ImVec4(1.00f, 0.40f, 0.40f, 1.0f);

        // Log lines in a child for clip+scroll
        if (ImGui::BeginChild("##log", ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            for (const auto& entry : lines_) {
                const ImVec4& col = (entry.sev == LogSeverity::Error) ? kColError
                                  : (entry.sev == LogSeverity::Warn)  ? kColWarn
                                                                      : kColInfo;
                ImGui::TextColored(col, "%s", entry.msg.c_str());
            }
            if (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

REGISTER_PANEL(ConsolePanel, 500)

}  // namespace editor
