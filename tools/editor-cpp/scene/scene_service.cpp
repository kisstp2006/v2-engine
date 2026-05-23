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
    // A motor process-end-ig életben tartja az obj-t; nincs explicit free
    // amíg M5+ nem hoz scene-save/load lifecycle-t.
    root_ = nullptr;
}

void SceneService::replaceRoot(obj* newRoot) {
    root_ = newRoot;
    if (bus_) bus_->emit(kEvtSceneReplaced, newRoot);
}

}  // namespace editor
