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
        // Asset-preview mód: ha nincs primary node DE van selected asset
        // (Project panelből 1-click), az Inspector az asset-info-t mutatja.
        const std::string& assetPath = app.selection().selectedAsset();
        if (!o && !assetPath.empty()) {
            drawAssetPreview(app, assetPath);
        } else if (!o) {
            ImGui::TextDisabled("No selection.");
        } else {
            const std::vector<obj*>& all = app.selection().all();
            const size_t cnt = all.size();
            const char*  name = obj_name(o);
            const char*  type = obj_type(o);

            // Header: ha multi-select → "N selected", egyébként név + típus.
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

            // Phase 4a — registry projekt-kontextusa (drop-target relativizál).
            InspectorRegistry::instance().setProjectPath(app.projectPath());
            // Phase 6b — EditorApp* kontextus a custom drawer-eknek.
            InspectorRegistry::instance().setApp(&app);

            // Multi-aware rendering: csak homogén selection esetén tényleges
            // multi-edit; egyébként primary-on rajzol.
            InspectorRegistry::instance().drawComponentsMulti(all);

            // Edit transaction edge-detection. Multi-target esetén minden
            // node-ra snapshot-ol + MultiObjectStateCommand-pal undozható.
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
