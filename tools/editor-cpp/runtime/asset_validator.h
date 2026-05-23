#pragma once

// AssetValidator (Phase 5c). Recursive reflection-traverse over the scene
// tree, checking every `char*` field marked with `[asset:*]` hint:
//   - file existence (`is_file`)
//   - extension vs. hint-asset-type (e.g. `[asset:model]` field `.png` →
//     warning)
//   - project-boundary (abs-path outside the project → warning, portability
//     impact)
// Result: `std::vector<AssetIssue>` — the caller (Tools menu or pre-cook
// gate) decides whether to write to Console, show in a modal, or block the cook.

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class EditorApp;

enum class AssetIssueLevel { Info, Warning, Error };

struct AssetIssue {
    AssetIssueLevel  level    = AssetIssueLevel::Info;
    obj*             node     = nullptr;
    std::string      nodeName;       // e.g. "Player" — obj_name(node)
    std::string      typeName;       // e.g. "MeshRenderer" — obj_type(node)
    std::string      fieldName;      // e.g. "model_path"
    std::string      path;            // the stored value (rel or abs)
    std::string      reason;          // e.g. "file not found: ..."
};

class AssetValidator {
public:
    // Full scene-traverse from `app.scene().root()`. Resolves absolute paths
    // based on `app.projectPath()`.
    static std::vector<AssetIssue> validate(EditorApp& app);

    static int countErrors  (const std::vector<AssetIssue>& issues);
    static int countWarnings(const std::vector<AssetIssue>& issues);
};

}  // namespace editor
