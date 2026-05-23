#pragma once

// FileTypeRegistry — extension → handler dispatch (Phase 3b).
// The Project panel's double-click handler calls this: it runs the
// FileTypeHandler::action callback for the path. New file-type = new
// REGISTER_FILE_TYPE macro entry, NO Project-panel modification.
//
// Auto-init pattern, like panel_registry.h: registrations run in a static
// initializer before `main()`.

#include <functional>
#include <string>
#include <vector>

namespace editor {

class EditorApp;

struct FileTypeHandler {
    // Possible extensions in lower-cased form, starting with a dot.
    // Compound extensions are allowed, e.g. ".scene.json5" — longest-match
    // wins, so ".scene.json5" matches before ".json5".
    std::vector<std::string>                              extensions;
    // Human-readable label (Console-log + future Import-filter).
    std::string                                           label;
    // The double-click action — receives the full path.
    std::function<void(EditorApp&, const std::string&)>   action;
};

class FileTypeRegistry {
public:
    static FileTypeRegistry& instance();

    // New handler registration. Normally called by the REGISTER_FILE_TYPE
    // macro in a static initializer.
    void registerHandler(FileTypeHandler h);

    // Returns the handler matching the path. Longest-extension-match
    // (".scene.json5" wins over ".json5"). nullptr if no match.
    const FileTypeHandler* handlerFor(const std::string& path) const;

    const std::vector<FileTypeHandler>& all() const { return handlers_; }

private:
    std::vector<FileTypeHandler> handlers_;
};

// Static-init helper for the REGISTER_FILE_TYPE macro.
struct FileTypeRegistrar {
    explicit FileTypeRegistrar(FileTypeHandler h);
};

}  // namespace editor

// Register a new file-type. Example:
//
//   REGISTER_FILE_TYPE(mesh, {
//       {".iqm", ".gltf", ".fbx"}, "Mesh",
//       [](editor::EditorApp& app, const std::string& p) {
//           app.createMesh(p.c_str());
//       }
//   })
//
// `name` is a unique identifier (only used to form the global-variable name).
#define REGISTER_FILE_TYPE(name, ...)                                          \
    namespace {                                                                \
        const ::editor::FileTypeRegistrar _ft_##name##_reg{ __VA_ARGS__ };     \
    }
