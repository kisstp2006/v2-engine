#pragma once

// FileTypeRegistry — extension → handler dispatch (Phase 3b).
// A Project panel double-click handler-e ezt hívja meg: a path-hoz tartozó
// FileTypeHandler::action callback-jét futtatja. Új fájl-típus = új
// REGISTER_FILE_TYPE makró-bevitel, NINCS Project-panel-módosítás.
//
// Auto-init pattern, mint a panel_registry.h: a regisztrációk static
// initializer-ben futnak `main()` előtt.

#include <functional>
#include <string>
#include <vector>

namespace editor {

class EditorApp;

struct FileTypeHandler {
    // Lehetséges extension-ek lower-case-elt formában, ponttal kezdve.
    // Compound extension is megengedett, pl. ".scene.json5" — longest-match
    // nyer, így a ".scene.json5" előbb illeszkedik mint a ".json5".
    std::vector<std::string>                              extensions;
    // Emberi-olvasható label (Console-log + jövőbeni Import-szűrő).
    std::string                                           label;
    // A double-click akció — kapja a teljes path-ot.
    std::function<void(EditorApp&, const std::string&)>   action;
};

class FileTypeRegistry {
public:
    static FileTypeRegistry& instance();

    // Új handler regisztráció. Mainstream-ben a REGISTER_FILE_TYPE makró
    // hívja static initializer-ben.
    void registerHandler(FileTypeHandler h);

    // A path-hoz tartozó handler-t adja vissza. Longest-extension-match
    // (a ".scene.json5" nyer a ".json5" fölött). nullptr ha nincs match.
    const FileTypeHandler* handlerFor(const std::string& path) const;

    const std::vector<FileTypeHandler>& all() const { return handlers_; }

private:
    std::vector<FileTypeHandler> handlers_;
};

// Static-init helper a REGISTER_FILE_TYPE makróhoz.
struct FileTypeRegistrar {
    explicit FileTypeRegistrar(FileTypeHandler h);
};

}  // namespace editor

// Új fájl-típus regisztrálása. Példa:
//
//   REGISTER_FILE_TYPE(mesh, {
//       {".iqm", ".gltf", ".fbx"}, "Mesh",
//       [](editor::EditorApp& app, const std::string& p) {
//           app.createMesh(p.c_str());
//       }
//   })
//
// A `name` egy unique azonosító (csak a global-változó-nevet képezi belőle).
#define REGISTER_FILE_TYPE(name, ...)                                          \
    namespace {                                                                \
        const ::editor::FileTypeRegistrar _ft_##name##_reg{ __VA_ARGS__ };     \
    }
