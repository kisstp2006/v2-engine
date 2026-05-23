#include "selection_service.h"
#include "event_bus.h"
#include "events.h"
#include "../scene/scene_helpers.h"

namespace editor {

namespace {
// DFS: collects every node reachable from validRoot into a std::vector.
void collectReachable(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectReachable(editor_obj_child_at(node, i), out);
    }
}
}  // namespace

void SelectionService::emitChanged() {
    SelectionChange msg;
    msg.all     = nodes_;
    msg.primary = primary();
    bus_.emit(kEvtSelectionChanged, msg);
}

void SelectionService::sanitize(obj* validRoot) {
    if (nodes_.empty()) return;
    if (!validRoot) {
        nodes_.clear();
        emitChanged();
        return;
    }
    std::vector<obj*> reachable;
    collectReachable(validRoot, reachable);
    size_t before = nodes_.size();
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(),
            [&reachable](obj* n) {
                return std::find(reachable.begin(), reachable.end(), n)
                       == reachable.end();
            }),
        nodes_.end());
    if (nodes_.size() != before) emitChanged();
}

void SelectionService::setPrimary(obj* o) {
    nodes_.clear();
    if (o) nodes_.push_back(o);
    // Node-selection overrides asset-selection (mutual exclusivity).
    if (o && !assetPath_.empty()) clearSelectedAsset();
    emitChanged();
}

void SelectionService::add(obj* o) {
    if (!o || contains(o)) return;
    nodes_.push_back(o);
    if (!assetPath_.empty()) clearSelectedAsset();
    emitChanged();
}

void SelectionService::remove(obj* o) {
    auto it = std::find(nodes_.begin(), nodes_.end(), o);
    if (it == nodes_.end()) return;
    nodes_.erase(it);
    emitChanged();
}

void SelectionService::toggle(obj* o) {
    if (!o) return;
    if (contains(o)) remove(o);
    else             add(o);
}

void SelectionService::clear() {
    if (nodes_.empty()) return;
    nodes_.clear();
    emitChanged();
}

void SelectionService::setSelectedAsset(const std::string& absPath) {
    if (assetPath_ == absPath) return;
    assetPath_ = absPath;
    // Asset-selection overrides node-selection (mutual exclusivity).
    if (!absPath.empty() && !nodes_.empty()) {
        nodes_.clear();
        emitChanged();
    }
    bus_.emit(kEvtAssetSelectionChanged, assetPath_);
}

void SelectionService::clearSelectedAsset() {
    if (assetPath_.empty()) return;
    assetPath_.clear();
    bus_.emit(kEvtAssetSelectionChanged, assetPath_);
}

}  // namespace editor
