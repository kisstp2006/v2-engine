#pragma once

#include "engine.h"
#ifdef obj
#undef obj   // see explanation in selection_service.h
#endif

namespace editor {

class EventBus;

// Owner of the scene root `obj*`. A single SceneService instance lives
// for the duration of EditorApp's lifetime, and the scene tree starts here.
//
// `replaceRoot()` emits a `kEvtSceneReplaced` event on `bus` (when set),
// so SelectionService/ScriptHost/AssetCache can clean up.
class SceneService {
public:
    SceneService();
    ~SceneService();
    SceneService(const SceneService&) = delete;
    SceneService& operator=(const SceneService&) = delete;

    // EditorApp sets it at init; without it replaceRoot does not emit.
    void setBus(EventBus* bus) { bus_ = bus; }

    obj* root() const { return root_; }
    // Replaces the existing root with the new one. The old one stays in the
    // engine's object pool. Emits a `kEvtSceneReplaced` event on the set bus.
    void replaceRoot(obj* newRoot);

private:
    obj*      root_ = nullptr;
    EventBus* bus_  = nullptr;
};

}  // namespace editor
