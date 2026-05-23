#pragma once

// AssetValidator (Phase 5c). A scene-fában rekurzív reflection-traverse-szel
// minden `[asset:*]` hint-tel ellátott `char*` mezőt ellenőriz:
//   - fájl-létezés (`is_file`)
//   - kiterjesztés vs. hint-asset-type (pl. `[asset:model]` mező `.png` →
//     warning)
//   - projekt-boundary (projekt-en kívüli abs-path → warning, portability-
//     impact)
// Eredmény: `std::vector<AssetIssue>` — a hívó (Tools menü vagy pre-cook
// gate) dönti, hogy Console-ba ír, modal-ban mutat, vagy blokkolja a cook-ot.

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
    std::string      nodeName;       // pl. "Player" — obj_name(node)
    std::string      typeName;       // pl. "MeshRenderer" — obj_type(node)
    std::string      fieldName;      // pl. "model_path"
    std::string      path;            // a tárolt érték (rel vagy abs)
    std::string      reason;          // pl. "file not found: ..."
};

class AssetValidator {
public:
    // Teljes scene-traverse az `app.scene().root()`-tól. `app.projectPath()`
    // alapján resolve-eli az abszolút utakat.
    static std::vector<AssetIssue> validate(EditorApp& app);

    static int countErrors  (const std::vector<AssetIssue>& issues);
    static int countWarnings(const std::vector<AssetIssue>& issues);
};

}  // namespace editor
