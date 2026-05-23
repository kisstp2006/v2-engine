// Default fájl-típus regisztrációk (Phase 3b).
// Új típus hozzáadása: csak egy új REGISTER_FILE_TYPE blokk. Új fájl is OK
// — a static initializer auto-regisztrál.

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "file_type_registry.h"
#include "editor_app.h"

namespace editor {

REGISTER_FILE_TYPE(scene, {
    {".scene.json5"}, "Scene",
    [](EditorApp& app, const std::string& p) { app.openScene(p); }
})

REGISTER_FILE_TYPE(prefab, {
    {".prefab.json5"}, "Prefab",
    [](EditorApp& app, const std::string& p) { app.spawnPrefab(p.c_str()); }
})

REGISTER_FILE_TYPE(mesh, {
    {".iqm", ".gltf", ".fbx", ".obj"}, "Mesh",
    [](EditorApp& app, const std::string& p) { app.createMesh(p.c_str()); }
})

REGISTER_FILE_TYPE(sprite, {
    {".png", ".jpg", ".jpeg", ".tga", ".bmp"}, "Sprite",
    [](EditorApp& app, const std::string& p) { app.createSprite(p.c_str()); }
})

REGISTER_FILE_TYPE(script, {
    {".lua"}, "Script",
    // Csak spawn — az IDE-megnyitás az asset-preview / Script-Inspector
    // explicit "Open in IDE" gombján keresztül történik (Phase 6c). Így a
    // dupla-akció nem csap rá a felhasználóra szándék nélkül.
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

}  // namespace editor
