#include "engine.h"

#include "scene_service.h"
#include "scene_helpers.h"
#include "../core/event_bus.h"
#include "../core/events.h"

namespace editor {

SceneService::SceneService() {
    root_ = editor_obj_new_scene("Scene");
}

SceneService::~SceneService() {
    // The engine keeps the obj alive until process-end; no explicit free
    // until M5+ brings a scene-save/load lifecycle.
    root_ = nullptr;
}

void SceneService::replaceRoot(obj* newRoot) {
    root_ = newRoot;
    if (bus_) bus_->emit(kEvtSceneReplaced, newRoot);
}

}  // namespace editor
