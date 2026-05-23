#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine.h"
// The engine's `#define obj(TYPE, ...)` function-like macro collides
// with `typedef struct obj obj;` in the C++ parser. We undef it; the factory
// is used only in the .c wrappers (scene_helpers.c).
#ifdef obj
#undef obj
#endif

namespace editor {

class EventBus;

// Multi-select store (M16b). `primary()` returns the first selected node.
// Any mutating operation emits `selection_changed` on the bus.
class SelectionService {
public:
    explicit SelectionService(EventBus& bus) : bus_(bus) {}

    // Backward-compat: the first selected node.
    obj* primary() const { return nodes_.empty() ? nullptr : nodes_.front(); }

    // Full selection list (read-only).
    const std::vector<obj*>& all() const { return nodes_; }
    size_t count() const { return nodes_.size(); }
    bool contains(obj* o) const {
        return std::find(nodes_.begin(), nodes_.end(), o) != nodes_.end();
    }

    // Replaces the selection with a single node (= plain click).
    void setPrimary(obj* o);

    // Adds to the selection (Ctrl+click).
    void add(obj* o);

    // Removes from the selection.
    void remove(obj* o);

    // Toggle: if inside → remove, otherwise add.
    void toggle(obj* o);

    void clear();

    // DFS-cleans every node-pointer not reachable from `validRoot`.
    // Called after scene_replaced to protect the Hierarchy/Inspector from
    // dangling pointers. If validRoot == nullptr, drops every node.
    void sanitize(obj* validRoot);

    // Asset selection (from Project panel). Independent of node-selection;
    // the Inspector shows asset-preview mode when there is NO primary node
    // but there is an asset-path. emit: kEvtAssetSelectionChanged (payload = path).
    void               setSelectedAsset(const std::string& absPath);
    const std::string& selectedAsset() const { return assetPath_; }
    void               clearSelectedAsset();

private:
    void emitChanged();

    EventBus&         bus_;
    std::vector<obj*> nodes_;
    std::string       assetPath_;
};

}  // namespace editor
