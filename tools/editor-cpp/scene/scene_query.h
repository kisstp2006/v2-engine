#pragma once

#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class EventBus;

// Centralized scene-walk + singleton cache. Refaktor F3 (EngineQuery).
//
// Replaces the pre-refactor pattern where every viewport panel did:
//   - rebuildFlatLists_(root)       — DFS for MeshRenderer + LightRef
//   - findSkyboxNode(root)          — DFS again (used 2× per frame in Game panel)
//   - findFogSettings(root)         — DFS again
//   - findActiveCamera(root)        — DFS again (Game panel only)
//
// → 3-4 full DFS walks per frame per panel. With Scene+Game both running
//   that's 6-8 walks; with 50-node scenes that's ~300-400 child_count +
//   child_at calls per frame just to find the same 4-5 singletons.
//
// New behavior: ONE walk per scene mutation, results cached. The bus
// (kEvtSceneReplaced / Dirty / NodeAdded / Removed) flips a dirty flag;
// the next query rebuilds. Every panel shares one SceneQuery → Scene panel
// rebuilding the cache also serves the Game panel that same frame.
//
// Perf: ~0 work per frame in the steady state (no scene edits). Each query
// is one bool branch + one vector/pointer return.
//
// Thread: main-thread only (the motor's obj-tree isn't thread-safe).
class SceneQuery {
public:
    explicit SceneQuery(EventBus& bus);

    // --- Flat lists ---
    // Reference is valid until the next bus event. Don't retain across
    // event-loop ticks.
    const std::vector<obj*>& meshNodes(obj* root);
    const std::vector<obj*>& lightNodes(obj* root);

    // --- Cached singletons ---
    // First occurrence in DFS order. nullptr if not present in the scene.
    obj* skybox(obj* root);
    obj* fog(obj* root);
    // First CameraRef with is_active=true. The Game viewport renders through
    // this (no active camera → "No Camera" placeholder).
    obj* activeCamera(obj* root);
    obj* shadowSettings(obj* root);
    obj* postFXStack(obj* root);

    // --- Diagnostics ---
    // Number of DFS rebuilds since construction (incremented in rebuildIfNeeded_).
    // Useful for the Profiler — high values mean the scene is being mutated
    // every frame.
    int rebuildCount() const { return rebuildCount_; }

private:
    EventBus* bus_;
    bool dirty_         = true;
    obj* lastRoot_      = nullptr;
    int  rebuildCount_  = 0;

    std::vector<obj*> meshes_;
    std::vector<obj*> lights_;
    obj* skybox_         = nullptr;
    obj* fog_            = nullptr;
    obj* activeCamera_   = nullptr;
    obj* shadowSettings_ = nullptr;
    obj* postFXStack_    = nullptr;

    void rebuildIfNeeded_(obj* root);
    void wireBus_();
};

}  // namespace editor
