// STL FIRST.
#include <string>

#include "engine.h"

#include "toolbar_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../runtime/play_mode.h"

namespace editor {

void ToolbarPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (ImGui::Begin(title_.c_str(), &visible)) {
        PlayMode& pm = app.play();

        // On the left: gizmo mode selector (T/R/S, W/E/R key).
        int& op = app.gizmoOp();
        if (ImGui::Selectable("T", op == 7, 0, ImVec2(28, 0)))    op = 7;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (W)");
        ImGui::SameLine(0, 4);
        if (ImGui::Selectable("R", op == 120, 0, ImVec2(28, 0)))  op = 120;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (E)");
        ImGui::SameLine(0, 4);
        if (ImGui::Selectable("S", op == 896, 0, ImVec2(28, 0)))  op = 896;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (R)");
        ImGui::SameLine();

        // Centered 3-button group: Play / Pause / Stop.
        const float groupW = 96.0f * 3 + 8.0f * 2;
        float availW = ImGui::GetContentRegionAvail().x;
        if (availW > groupW) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX()
                                 + (availW - groupW) * 0.5f);
        }

        // Play (active if Edit; "Resume" if Pause)
        const char* playLabel = pm.isPaused() ? "Resume" : "Play";
        const bool playDisabled = pm.isPlaying();
        ImGui::BeginDisabled(playDisabled);
        if (ImGui::Button(playLabel, ImVec2(96, 0))) {
            if (pm.isPaused()) pm.resume();
            else               pm.start(app);
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0, 8);

        ImGui::BeginDisabled(!pm.isPlaying());
        if (ImGui::Button("Pause", ImVec2(96, 0))) {
            pm.pause();
        }
        ImGui::EndDisabled();

        ImGui::SameLine(0, 8);

        ImGui::BeginDisabled(pm.isEditing());
        if (ImGui::Button("Stop", ImVec2(96, 0))) {
            pm.stop(app);
        }
        ImGui::EndDisabled();

        // State-indicator on the right.
        ImGui::SameLine();
        const char* st = pm.isEditing() ? "EDIT"
                       : pm.isPlaying() ? "PLAYING"
                       : "PAUSED";
        ImVec4 col = pm.isEditing() ? ImVec4(0.6f, 0.6f, 0.6f, 1)
                   : pm.isPlaying() ? ImVec4(0.4f, 0.9f, 0.4f, 1)
                   : ImVec4(0.9f, 0.7f, 0.3f, 1);
        ImGui::TextColored(col, "[%s]", st);
    }
    ImGui::End();
}

REGISTER_PANEL(ToolbarPanel, 100)

}  // namespace editor
