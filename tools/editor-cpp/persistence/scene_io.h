#pragma once

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// Scene tree save/load to JSON5. ZERO hard-coded component-type checks —
// every field goes through the engine's reflection (`obj_savejson` / `obj_make`).
// Adding a new component = new `STRUCT()` reg → save/load is automatic.
//
// Format (flat array, with parent-index):
//
//   [
//     { parent: -1, name: "Scene", body: "Transform: { ... }" },
//     { parent: 0,  name: "GameObject1", body: "Transform: { ... }" },
//     { parent: 0,  name: "Mesh", body: "MeshRenderer: { ... }" },
//   ]
//
// Detailed load result (Phase 1e). Gives the user a precise error list in
// Console showing which node failed for what reason.
struct LoadResult {
    obj* root = nullptr;
    int  created = 0;
    int  failed = 0;
    // Phase 4b — auto-migration: how many abs path fields were converted to rel.
    // If > 0, the caller (EditorApp::openScene) does Console-log + dirty-state.
    int  migrated_paths = 0;
    std::vector<std::string> errors;
};

class SceneIO {
public:
    // Serialize the whole tree to a JSON5 string.
    static std::string saveTree(obj* root);

    // New scene-tree from a JSON5 string. Backward-compat overload.
    static obj* loadTree(const std::string& json);

    // Extended version — detailed error list. EditorApp::openScene uses this.
    // If `projectRoot` is NOT empty, every char* asset-path field is converted
    // abs→rel (Phase 4b auto-migration).
    static LoadResult loadTreeDetailed(const std::string& json,
                                       const std::string& projectRoot = {});

    // Subtree serialization — one node + its subtree (M16c, prefab). Same
    // flat-array format as saveTree, just starts from the given node.
    static std::string saveSubtree(obj* node);

    // Subtree load under `parent`. Returns: the new root node (the top of
    // the subtree), or NULL. If `projectRoot` is NOT empty → auto-migration (4b).
    static obj* loadSubtree(obj* parent, const std::string& json,
                            const std::string& projectRoot = {});
};

}  // namespace editor
