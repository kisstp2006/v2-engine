#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "engine.h"
// A motor `#define obj(TYPE, ...)` function-like macro-ja kollidál a
// `typedef struct obj obj;`-vel C++ parser-ben. Undef-eljük; a factory-t
// csak a .c wrapper-ekben (scene_helpers.c) használjuk.
#ifdef obj
#undef obj
#endif

namespace editor {

class EventBus;

// Multi-select tárolója (M16b). A `primary()` az első kijelölt node-t adja.
// Bármilyen mutáló művelet `selection_changed`-et emit-el a bus-on.
class SelectionService {
public:
    explicit SelectionService(EventBus& bus) : bus_(bus) {}

    // Backward-compat: az első kijelölt node.
    obj* primary() const { return nodes_.empty() ? nullptr : nodes_.front(); }

    // Teljes selection lista (read-only).
    const std::vector<obj*>& all() const { return nodes_; }
    size_t count() const { return nodes_.size(); }
    bool contains(obj* o) const {
        return std::find(nodes_.begin(), nodes_.end(), o) != nodes_.end();
    }

    // Egyetlen node-ra cseréli a selection-t (= sima kattint).
    void setPrimary(obj* o);

    // Hozzáad a selection-höz (Ctrl+kattint).
    void add(obj* o);

    // Eltávolít a selection-ből.
    void remove(obj* o);

    // Toggle: ha bent van → remove, egyébként add.
    void toggle(obj* o);

    void clear();

    // DFS-szel takarít minden olyan node-pointert, ami `validRoot`-ból nem
    // elérhető. A scene_replaced után hívva védi a Hierarchy/Inspector-t
    // a dangling pointer-eklől. Ha validRoot == nullptr, minden node-t eldob.
    void sanitize(obj* validRoot);

    // Asset selection (Project panel-ből). Független a node-selection-től;
    // az Inspector akkor mutatja asset-preview módot, ha NINCS primary node
    // de van asset-path. emit: kEvtAssetSelectionChanged (payload = path).
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
