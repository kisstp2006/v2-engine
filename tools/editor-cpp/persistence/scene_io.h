#pragma once

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// Scene tree save/load JSON5-be. NULLA hard-coded komponens-típus check —
// minden mező a motor reflection-jén át (`obj_savejson` / `obj_make`).
// Új komponens hozzáadása = új `STRUCT()` reg → save/load automatikus.
//
// Formátum (flat array, parent-index-szel):
//
//   [
//     { parent: -1, name: "Scene", body: "Transform: { ... }" },
//     { parent: 0,  name: "GameObject1", body: "Transform: { ... }" },
//     { parent: 0,  name: "Mesh", body: "MeshRenderer: { ... }" },
//   ]
//
// Részletes betöltés-eredmény (Phase 1e). A felhasználó számára Console-ban
// pontos hiba-listát ad, melyik node milyen okból nem jött létre.
struct LoadResult {
    obj* root = nullptr;
    int  created = 0;
    int  failed = 0;
    // Phase 4b — auto-migration: hány abs path-mező lett rel-re konvertálva.
    // Ha > 0 a hívó (EditorApp::openScene) Console-log + dirty-state.
    int  migrated_paths = 0;
    std::vector<std::string> errors;
};

class SceneIO {
public:
    // Egész fa serializálása JSON5 string-be.
    static std::string saveTree(obj* root);

    // JSON5 string-ből egy új scene-tree. Backward-compat overload.
    static obj* loadTree(const std::string& json);

    // Bővebb verzió — részletes hibalista. EditorApp::openScene ezt használja.
    // `projectRoot` ha NEM üres, minden char* asset-path-mező abs→rel
    // konvertál (Phase 4b auto-migration).
    static LoadResult loadTreeDetailed(const std::string& json,
                                       const std::string& projectRoot = {});

    // Subtree serializálás — egy node + alfái (M16c, prefab). Ugyanaz a flat-
    // array formátum mint a saveTree, csak az adott node-tól indul.
    static std::string saveSubtree(obj* node);

    // Subtree betöltés a `parent` alá. Visszaad: az új root-node (a subtree
    // teteje), vagy NULL. `projectRoot` ha NEM üres → auto-migration (4b).
    static obj* loadSubtree(obj* parent, const std::string& json,
                            const std::string& projectRoot = {});
};

}  // namespace editor
