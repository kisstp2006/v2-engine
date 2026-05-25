// Default file-type registrations (Phase 3b).
// Adding a new type: just a new REGISTER_FILE_TYPE block. A new file is also OK
// — the static initializer auto-registers.

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "file_type_registry.h"
#include "editor_app.h"

namespace editor {

REGISTER_FILE_TYPE(scene, {
    // ".prefab.json5" wins via longest-match (prefab handler), so plain
    // ".json5" reaches here only for non-prefab scenes. Both extensions are
    // accepted so the user can name files as `level1.json5` or `level1.scene.json5`.
    {".scene.json5", ".json5"}, "Scene",
    [](EditorApp& app, const std::string& p) { app.openScene(p); }
})

REGISTER_FILE_TYPE(prefab, {
    {".prefab.json5"}, "Prefab",
    [](EditorApp& app, const std::string& p) { app.spawnPrefab(p.c_str()); }
})

REGISTER_FILE_TYPE(mesh, {
    {".iqm", ".gltf", ".glb"}, "Mesh",
    [](EditorApp& app, const std::string& p) { app.createMesh(p.c_str()); }
})

REGISTER_FILE_TYPE(sprite, {
    {".png", ".jpg", ".jpeg", ".tga", ".bmp"}, "Sprite",
    [](EditorApp& app, const std::string& p) { app.createSprite(p.c_str()); }
})

REGISTER_FILE_TYPE(script, {
    {".lua"}, "Script",
    // Spawn only — IDE-opening happens via the asset-preview / Script-Inspector
    // explicit "Open in IDE" button (Phase 6c). This way the double-action
    // does not surprise the user without intent.
    [](EditorApp& app, const std::string& p) {
        app.createScript(p.c_str());
    }
})

REGISTER_FILE_TYPE(tilemap, {
    {".tmx"}, "Tilemap",
    [](EditorApp& app, const std::string& p) { app.createTilemap(p.c_str()); }
})

REGISTER_FILE_TYPE(audio, {
    {".ogg", ".wav", ".mp3", ".flac"}, "Audio",
    [](EditorApp& app, const std::string& p) { app.createAudioSource(p.c_str()); }
})

// Material asset — `.mat.json5` is just the label here; the Inspector's
// asset-preview switches into material-editor mode on its own (via the
// extension check in asset_preview.cpp). No double-click spawn: a material
// is referenced by a MaterialOverride on a MeshRenderer (Fázis 2.4+),
// not spawned as a scene node on its own. Empty action = the Spawn button
// in the preview is disabled, which is the desired UX.
REGISTER_FILE_TYPE(material, {
    {".mat.json5"}, "Material",
    nullptr
})

}  // namespace editor
