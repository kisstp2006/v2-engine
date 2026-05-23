#pragma once

#include "engine.h"
#ifdef obj
#undef obj   // lásd magyarázat a selection_service.h-ban
#endif

namespace editor {

class EventBus;

// A scene root `obj*`-jának owner-je. A SceneService egy példánya
// az EditorApp lifetime-ja alatt él, és a scene fát itt indítjuk.
//
// `replaceRoot()` `kEvtSceneReplaced` event-et emit a `bus`-on (ha be van
// állítva), hogy a SelectionService/ScriptHost/AssetCache takaríthassanak.
class SceneService {
public:
    SceneService();
    ~SceneService();
    SceneService(const SceneService&) = delete;
    SceneService& operator=(const SceneService&) = delete;

    // Az EditorApp beállítja init-kor; nélküle a replaceRoot nem emit-el.
    void setBus(EventBus* bus) { bus_ = bus; }

    obj* root() const { return root_; }
    // A meglévő root-ot lecseréli az új-ra. A régi a motor object-pool-ban
    // marad. A `kEvtSceneReplaced` event-et emit a beállított bus-on.
    void replaceRoot(obj* newRoot);

private:
    obj*      root_ = nullptr;
    EventBus* bus_  = nullptr;
};

}  // namespace editor
