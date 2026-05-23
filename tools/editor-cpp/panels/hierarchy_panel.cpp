// STL FIRST.
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

void renderNode(EditorApp& app, obj* node) {
    if (!node) return;

    const char* name = obj_name(node);
    const int   childCount = editor_obj_child_count(node);
    const bool  isLeaf  = (childCount == 0);
    const bool  isSelected = app.selection().contains(node);
    const bool  isRoot = (node == app.scene().root());

    ImGuiTreeNodeFlags flags =
        ImGuiTreeNodeFlags_OpenOnArrow |
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
        ImGuiTreeNodeFlags_SpanAvailWidth |
        (isSelected ? ImGuiTreeNodeFlags_Selected : 0) |
        (isLeaf ? (ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen)
                : ImGuiTreeNodeFlags_DefaultOpen);

    bool open = ImGui::TreeNodeEx((void*)node, flags, "%s",
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
        if (ImGui::MenuItem("Save as Prefab...")) {
            app.saveSelectedAsPrefab();
        }
        ImGui::EndPopup();
    }
    if (open && !isLeaf) {
        for (int i = 0; i < childCount; ++i) {
            renderNode(app, editor_obj_child_at(node, i));
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
            renderNode(app, root);
        }

        // Delete-key handler — only when the Hierarchy panel is focused
        // (don't steal the text-field delete, and don't activate when
        // working in the Inspector/Scene panel).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
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
