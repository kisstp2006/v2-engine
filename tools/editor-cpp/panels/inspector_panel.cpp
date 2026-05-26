#include "engine.h"

#include "inspector_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../commands/command.h"
#include "../core/selection_service.h"
#include "../inspector/inspector_registry.h"
#include "../inspector/asset_preview.h"

namespace editor {

void InspectorPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (ImGui::Begin(title_.c_str(), &visible)) {
        obj* o = app.selection().primary();
        // Asset-preview mode: if there is no primary node BUT there is a selected asset
        // (1-click from the Project panel), the Inspector shows the asset-info.
        const std::string& assetPath = app.selection().selectedAsset();

        // Per-selection undo / redo arrows. Operate on the CURRENT primary
        // node, or — if no node is selected — on the selected asset. The
        // arrows scan the command stack from the top down for the most
        // recent match; arrows are disabled when nothing matches.
        {
            const bool nodeMode  = (o != nullptr);
            const bool assetMode = (!nodeMode && !assetPath.empty());

            const bool canU = nodeMode  ? app.commands().canUndoForObject(o)
                            : assetMode ? app.commands().canUndoForAsset(assetPath)
                            : false;
            const bool canR = nodeMode  ? app.commands().canRedoForObject(o)
                            : assetMode ? app.commands().canRedoForAsset(assetPath)
                            : false;

            if (!canU) ImGui::BeginDisabled();
            if (ImGui::ArrowButton("##undo_sel", ImGuiDir_Left)) {
                if (nodeMode)  app.commands().undoForObject(o);
                else if (assetMode) app.commands().undoForAsset(assetPath);
            }
            if (!canU) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(canU
                    ? "Undo last change to this selection"
                    : "No recent change for this selection");
            }

            ImGui::SameLine();

            if (!canR) ImGui::BeginDisabled();
            if (ImGui::ArrowButton("##redo_sel", ImGuiDir_Right)) {
                if (nodeMode)  app.commands().redoForObject(o);
                else if (assetMode) app.commands().redoForAsset(assetPath);
            }
            if (!canR) ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip(canR
                    ? "Redo next change to this selection"
                    : "No redo for this selection");
            }

            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
        }

        if (!o && !assetPath.empty()) {
            drawAssetPreview(app, assetPath);
        } else if (!o) {
            ImGui::TextDisabled("No selection.");
        } else {
            const std::vector<obj*>& all = app.selection().all();
            const size_t cnt = all.size();
            const char*  name = obj_name(o);
            const char*  type = obj_type(o);

            // Header: if multi-select → "N selected", otherwise name + type.
            if (cnt > 1) {
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.4f, 1),
                                   "%zu Objects selected (editing primary, "
                                   "auto-sync to all)", cnt);
                ImGui::TextDisabled("Primary: %s (%s)",
                                    name ? name : "(unnamed)",
                                    type ? type : "?");
            } else {
                ImGui::TextUnformatted(name ? name : "(unnamed)");
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", type ? type : "?");
            }
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0, 4));

            // Phase 4a — registry project context (drop-target relativizes paths).
            InspectorRegistry::instance().setProjectPath(app.projectPath());
            // Phase 6b — EditorApp* context for custom drawers.
            InspectorRegistry::instance().setApp(&app);

            // Multi-aware rendering: actual multi-edit only on homogeneous selection;
            // otherwise draws on primary.
            InspectorRegistry::instance().drawComponentsMulti(all);

            // Edit transaction edge-detection. For multi-target, snapshots
            // every node + undoable via MultiObjectStateCommand.
            const bool isEditing =
                ImGui::IsAnyItemActive() && ImGui::IsWindowFocused();
            if (isEditing && !wasEditing_) {
                editTargets_ = all;
                editBefores_ = MultiObjectStateCommand::snapshotAll(all);
            }
            if (!isEditing && wasEditing_) {
                auto afters = MultiObjectStateCommand::snapshotAll(editTargets_);
                bool anyChanged = false;
                for (size_t i = 0; i < editTargets_.size()
                                   && i < editBefores_.size()
                                   && i < afters.size(); ++i) {
                    if (afters[i] != editBefores_[i]) { anyChanged = true; break; }
                }
                if (anyChanged) {
                    if (editTargets_.size() > 1) {
                        app.commands().execute(
                            std::make_unique<MultiObjectStateCommand>(
                                editTargets_, editBefores_, afters,
                                "Edit (Inspector, multi)"));
                    } else if (editTargets_.size() == 1 && editTargets_[0]) {
                        app.commands().execute(
                            std::make_unique<ObjectStateCommand>(
                                editTargets_[0], editBefores_[0], afters[0],
                                "Edit (Inspector)"));
                    }
                }
                editTargets_.clear();
                editBefores_.clear();
            }
            wasEditing_ = isEditing;
        }
    }
    ImGui::End();
}

REGISTER_PANEL(InspectorPanel, 300)

}  // namespace editor
