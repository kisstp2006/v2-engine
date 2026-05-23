// STL ELŐSZÖR.
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
        // Ctrl+kattint → toggle, sima kattint → cseréli a selection-t.
        if (ImGui::GetIO().KeyCtrl) {
            app.selection().toggle(node);
        } else {
            app.selection().setPrimary(node);
        }
    }

    // ----- DragDropSource — minden node-ból lehet húzni, KIVÉVE a root-ot.
    // (A root reparent-elése nem értelmezhető — a scene-fa gyökere.)
    if (!isRoot) {
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            obj* payload = node;
            ImGui::SetDragDropPayload("HIER_NODE", &payload, sizeof(obj*));
            ImGui::Text("Move: %s", name ? name : "(unnamed)");
            ImGui::EndDragDropSource();
        }
    }

    // ----- DragDropTarget — bármely node lehet drop-target.
    // Cycle-detection: drop-target NEM lehet a dragged node leszármazottja
    // (és nem lehet maga a dragged node sem).
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("HIER_NODE")) {
            obj* draggedNode = *(obj**)payload->Data;
            if (draggedNode && draggedNode != node) {
                if (editor_obj_is_ancestor(draggedNode, node)) {
                    // Ciklus tilos.
                    app.bus().emit(kEvtLogWarn, std::string(
                        "Reparent rejected: target is descendant of source"));
                } else if (obj_parent(draggedNode) == node) {
                    // No-op — már ennek a node-nak a child-je.
                } else {
                    obj* oldParent = obj_parent(draggedNode);
                    obj_attach(node, draggedNode);  // motor előtte detach
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

    // Per-node kontextus-menü (jobb-klikk az item-en). `nullptr` ID =
    // az aktuális item (TreeNodeEx) saját ID-jét használja, ami unique a
    // node-pointer-rel — különben az ütközés egyszerre nyitja az összes
    // popup-ot.
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
        // Globális (üres terület) kontextus-menü — csak `Create Empty`.
        // A `Save as Prefab` a per-node menüben szerepel (egyértelmű target).
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

        // Delete-billentyű handler — csak amikor a Hierarchy panel a fókuszban
        // van (ne fogja el a text-mező delete-ját, és ne aktiválódjon ha
        // Inspector/Scene panelben dolgozunk).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
            !ImGui::GetIO().WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            // Másoljuk a selection-listát, mert a törlés közben mutáljuk.
            std::vector<obj*> sel = app.selection().all();
            int deleted = 0;
            for (obj* n : sel) {
                if (!n || n == root) continue;       // root nem törölhető
                obj* parent = obj_parent(n);
                if (!parent) continue;               // már detach-elt
                app.selection().remove(n);           // selection takarítás
                obj_detach(n);                       // motor: kivesz a parent-ből
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
