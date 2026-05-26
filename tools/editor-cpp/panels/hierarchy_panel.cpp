// STL FIRST.
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "hierarchy_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../scene/scene_service.h"
#include "../scene/scene_helpers.h"
#include "../core/selection_service.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../commands/command.h"
#include "../components/components_api.h"

namespace editor {

namespace {

// Commit the pending inline-rename: applies `panel->rename_buf_` to
// `panel->renaming_node_` via obj_setname, wrapped in a RenameNodeCommand
// for undo/redo. Empty / whitespace-only new names are rejected (we keep
// the old name). Resets the rename state regardless.
void commitRename(EditorApp& app, HierarchyPanel* panel) {
    if (!panel || !panel->renaming_node_) return;
    obj* node = panel->renaming_node_;
    const char* old_c = obj_name(node);
    std::string oldName = old_c ? old_c : "";
    // Trim leading/trailing whitespace from the proposed new name.
    std::string newName = panel->rename_buf_;
    while (!newName.empty() &&
           (newName.front() == ' ' || newName.front() == '\t'))
        newName.erase(newName.begin());
    while (!newName.empty() &&
           (newName.back() == ' ' || newName.back() == '\t'))
        newName.pop_back();

    if (!newName.empty() && newName != oldName) {
        obj_setname(node, newName.c_str());
        app.commands().execute(std::make_unique<RenameNodeCommand>(
            node, oldName, newName, "Rename"));
        app.bus().emit(kEvtSceneDirty, true);
        app.bus().emit(kEvtNodeRenamed, node);
    }
    panel->renaming_node_     = nullptr;
    panel->rename_buf_[0]     = 0;
    panel->rename_grab_focus_ = false;
}

void cancelRename(HierarchyPanel* panel) {
    if (!panel) return;
    panel->renaming_node_     = nullptr;
    panel->rename_buf_[0]     = 0;
    panel->rename_grab_focus_ = false;
}

void renderNode(EditorApp& app, HierarchyPanel* panel, obj* node) {
    if (!node) return;

    const char* name = obj_name(node);
    const int   childCount = editor_obj_child_count(node);
    const bool  isLeaf  = (childCount == 0);
    const bool  isSelected = app.selection().contains(node);
    const bool  isRoot = (node == app.scene().root());
    const bool  isRenaming = (panel && panel->renaming_node_ == node);

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (isSelected ? ImGuiTreeNodeFlags_Selected : 0) |
        (isLeaf ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen)
                : ImGuiTreeNodeFlags_DefaultOpen);

    // Inline-rename mode: replace the tree-node label with an InputText.
    // We still call TreeNodeEx() with an empty label so the arrow + the
    // indent stay correct. The InputText is placed on the same line.
    bool open;
    if (isRenaming) {
        open = ImGui::TreeNodeEx((void*)node, flags, "%s", "");
        ImGui::SameLine();
        if (panel->rename_grab_focus_) {
            ImGui::SetKeyboardFocusHere();
            panel->rename_grab_focus_ = false;
        }
        // Wider InputText so long names fit. -FLT_MIN = take remaining width.
        ImGui::SetNextItemWidth(-FLT_MIN);
        bool enter = ImGui::InputText("##rename", panel->rename_buf_,
            sizeof(panel->rename_buf_),
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);
        if (enter) {
            commitRename(app, panel);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            cancelRename(panel);
        } else if (ImGui::IsItemDeactivated() &&
                   !ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
            // Click-away outside the field → commit.
            commitRename(app, panel);
        }
        // Skip the rest of the per-item handling (selection / drag-drop /
        // context menu) while in rename mode — the user is busy typing.
        if (open && !isLeaf) {
            for (int i = 0; i < childCount; ++i) {
                renderNode(app, panel, editor_obj_child_at(node, i));
            }
            ImGui::TreePop();
        }
        return;
    }

    open = ImGui::TreeNodeEx((void*)node, flags, "%s",
                             name ? name : "(unnamed)");
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
        // Ctrl+click → toggle, plain click → replaces the selection.
        if (ImGui::GetIO().KeyCtrl) {
            app.selection().toggle(node);
        } else {
            app.selection().setPrimary(node);
        }
    }

    // ----- DragDropSource — any node can be dragged, EXCEPT the root.
    // (Reparenting the root makes no sense — it is the scene-tree root.)
    if (!isRoot) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            obj* payload = node;
            ImGui::SetDragDropPayload("HIER_NODE", &payload, sizeof(obj*));
            ImGui::Text("Move: %s", name ? name : "(unnamed)");
            ImGui::EndDragDropSource();
        }
    }

    // ----- DragDropTarget — any node can be a drop-target.
    // Cycle-detection: the drop-target CANNOT be a descendant of the dragged node
    // (and cannot be the dragged node itself).
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("HIER_NODE")) {
            obj* draggedNode = *(obj**)payload->Data;
            if (draggedNode && draggedNode != node) {
                if (editor_obj_is_ancestor(draggedNode, node)) {
                    // Cycle forbidden.
                    app.bus().emit(kEvtLogWarn, std::string(
                        "Reparent rejected: target is descendant of source"));
                } else if (obj_parent(draggedNode) == node) {
                    // No-op — already a child of this node.
                } else {
                    obj* oldParent = obj_parent(draggedNode);
                    obj_attach(node, draggedNode);  // motor detaches first
                    app.commands().execute(
                        std::make_unique<ReparentCommand>(
                            draggedNode, oldParent, node, "Reparent"));
                    app.bus().emit(kEvtLogInfo, std::string("Reparented '") +
                        (obj_name(draggedNode) ? obj_name(draggedNode) : "?") +
                        "' under '" + (name ? name : "?") + "'");
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    // Per-node context menu (right-click on the item). `nullptr` ID =
    // uses the current item's (TreeNodeEx) own ID, which is unique per
    // node-pointer — otherwise the collision would open all popups
    // at once.
    if (ImGui::BeginPopupContextItem(nullptr)) {
        if (!app.selection().contains(node)) {
            app.selection().setPrimary(node);
        }
        if (!isRoot && ImGui::MenuItem("Rename", "F2")) {
            panel->renaming_node_ = node;
            const char* on = obj_name(node);
            std::strncpy(panel->rename_buf_, on ? on : "",
                         sizeof(panel->rename_buf_) - 1);
            panel->rename_buf_[sizeof(panel->rename_buf_) - 1] = 0;
            panel->rename_grab_focus_ = true;
        }
        if (ImGui::MenuItem("Save as Prefab...")) {
            app.saveSelectedAsPrefab();
        }
        ImGui::EndPopup();
    }
    if (open && !isLeaf) {
        for (int i = 0; i < childCount; ++i) {
            renderNode(app, panel, editor_obj_child_at(node, i));
        }
        ImGui::TreePop();
    }
}

}  // namespace

void HierarchyPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (ImGui::Begin(title_.c_str(), &visible)) {
        // Global (empty area) context menu — only `Create Empty`.
        // `Save as Prefab` is in the per-node menu (unambiguous target).
        if (ImGui::BeginPopupContextWindow("##hierctx",
                ImGuiPopupFlags_MouseButtonRight |
                ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("Create Empty")) {
                app.createEmpty();
            }
            ImGui::EndPopup();
        }

        obj* root = app.scene().root();
        if (!root) {
            ImGui::TextDisabled("No scene loaded.");
        } else {
            renderNode(app, this, root);
        }

        // F2 hotkey — start inline-rename on the primary selection (must be
        // focused on the Hierarchy panel + not typing in a text field, same
        // gate as Delete below).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !renaming_node_ &&
            ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
            obj* prim = app.selection().primary();
            if (prim && prim != root) {
                renaming_node_ = prim;
                const char* on = obj_name(prim);
                std::strncpy(rename_buf_, on ? on : "",
                             sizeof(rename_buf_) - 1);
                rename_buf_[sizeof(rename_buf_) - 1] = 0;
                rename_grab_focus_ = true;
            }
        }

        // Delete-key handler — only when the Hierarchy panel is focused
        // (don't steal the text-field delete, and don't activate when
        // working in the Inspector/Scene panel).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            !renaming_node_ &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            // Copy the selection list because we mutate during deletion.
            std::vector<obj*> sel = app.selection().all();
            int deleted = 0;
            for (obj* n : sel) {
                if (!n || n == root) continue;       // root cannot be deleted
                obj* parent = obj_parent(n);
                if (!parent) continue;               // already detached
                app.selection().remove(n);           // selection cleanup
                obj_detach(n);                       // motor: remove from parent
                app.commands().execute(
                    std::make_unique<DeleteNodeCommand>(parent, n, "Delete"));
                ++deleted;
            }
            if (deleted > 0) {
                app.bus().emit(kEvtLogInfo,
                    std::string("Deleted ") + std::to_string(deleted) +
                    " node(s)");
            }
        }
    }
    ImGui::End();
}

REGISTER_PANEL(HierarchyPanel, 200)

}  // namespace editor
