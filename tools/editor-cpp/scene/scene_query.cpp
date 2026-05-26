// STL FIRST.
#include <any>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "scene_query.h"

#include "scene_helpers.h"
#include "../components/components_api.h"
#include "../core/event_bus.h"
#include "../core/events.h"

namespace editor {

SceneQuery::SceneQuery(EventBus& bus) : bus_(&bus) {
    wireBus_();
}

void SceneQuery::wireBus_() {
    auto invalidate = [this](const std::any&) { dirty_ = true; };
    bus_->on(kEvtSceneReplaced, invalidate);
    bus_->on(kEvtSceneDirty,    invalidate);
    bus_->on(kEvtNodeAdded,     invalidate);
    bus_->on(kEvtNodeRemoved,   invalidate);
    // kEvtNodeRenamed deliberately NOT subscribed — name changes don't
    // affect which obj*s belong in the flat lists.
}

void SceneQuery::rebuildIfNeeded_(obj* root) {
    // Defensive: detect a silent root swap (replaceRoot() w/o emit). All
    // motors-of-record DO emit, so this is belt-and-suspenders.
    if (root != lastRoot_) {
        dirty_     = true;
        lastRoot_  = root;
    }
    if (!dirty_) return;

    meshes_.clear();
    lights_.clear();
    skybox_         = nullptr;
    fog_            = nullptr;
    activeCamera_   = nullptr;
    shadowSettings_ = nullptr;
    postFXStack_    = nullptr;

    if (!root) { dirty_ = false; return; }

    // Hand-rolled DFS — same iterative pattern the panels used. The single
    // `if/else if` chain ensures every node hits AT MOST ONE predicate per
    // visit (mesh_renderer + skybox + camera_ref etc. are mutually exclusive
    // component types in the motor's obj-tree).
    //
    // Reserve a generous initial stack; typical scenes have 30-100 nodes
    // so this almost always avoids reallocation.
    std::vector<obj*> stack;
    stack.reserve(64);
    stack.push_back(root);

    while (!stack.empty()) {
        obj* n = stack.back();
        stack.pop_back();

        if      (editor_obj_is_mesh_renderer(n))                              meshes_.push_back(n);
        else if (editor_obj_is_light_ref(n))                                  lights_.push_back(n);
        else if (!skybox_         && editor_obj_is_skybox(n))                 skybox_         = n;
        else if (!fog_            && editor_obj_is_fog_settings(n))           fog_            = n;
        else if (!shadowSettings_ && editor_obj_is_shadow_settings(n))        shadowSettings_ = n;
        else if (!postFXStack_    && editor_obj_is_postfx_stack(n))           postFXStack_    = n;
        else if (!activeCamera_ &&
                 editor_obj_is_camera_ref(n) &&
                 editor_camera_ref_is_active(n))                              activeCamera_   = n;

        int cnt = editor_obj_child_count(n);
        for (int i = 0; i < cnt; ++i) {
            if (obj* c = editor_obj_child_at(n, i)) stack.push_back(c);
        }
    }

    dirty_ = false;
    ++rebuildCount_;
}

const std::vector<obj*>& SceneQuery::meshNodes(obj* root)  { rebuildIfNeeded_(root); return meshes_; }
const std::vector<obj*>& SceneQuery::lightNodes(obj* root) { rebuildIfNeeded_(root); return lights_; }
obj* SceneQuery::skybox(obj* root)                         { rebuildIfNeeded_(root); return skybox_; }
obj* SceneQuery::fog(obj* root)                            { rebuildIfNeeded_(root); return fog_; }
obj* SceneQuery::activeCamera(obj* root)                   { rebuildIfNeeded_(root); return activeCamera_; }
obj* SceneQuery::shadowSettings(obj* root)                 { rebuildIfNeeded_(root); return shadowSettings_; }
obj* SceneQuery::postFXStack(obj* root)                    { rebuildIfNeeded_(root); return postFXStack_; }

}  // namespace editor
