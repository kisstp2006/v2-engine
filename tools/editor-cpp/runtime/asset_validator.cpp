// STL ELŐSZÖR.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "asset_validator.h"
#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/asset_path.h"
#include "../inspector/hint_parser.h"
#include "../scene/scene_helpers.h"

namespace editor {

namespace {

// Lowercase + ".XXX" extension extract (compound ext is honored:
// "foo.prefab.json5" → ".prefab.json5" ha szerepel a listában; egyébként
// csak az utolsó ".json5").
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c){ return std::tolower(c); });
    return s;
}

bool endsWith(const std::string& p, const std::string& e) {
    if (p.size() < e.size()) return false;
    return p.compare(p.size() - e.size(), e.size(), e) == 0;
}

// Asset-type → várható extension-lista. Az ".prefab.json5" előrébb, hogy
// nyerjen a ".json5" fölött a longest-match elven.
const std::unordered_map<std::string, std::vector<std::string>>&
expectedExtensions() {
    static const std::unordered_map<std::string, std::vector<std::string>> m = {
        { "model",   { ".iqm", ".gltf", ".glb", ".fbx", ".obj" } },
        { "texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" } },
        { "clip",    { ".ogg", ".wav", ".mp3", ".flac" } },
        { "tmx",     { ".tmx" } },
        { "script",  { ".lua" } },
        { "prefab",  { ".prefab.json5" } },
    };
    return m;
}

bool extensionMatches(const std::string& path, const std::string& assetType) {
    auto& m = expectedExtensions();
    auto it = m.find(assetType);
    if (it == m.end()) return true;   // ismeretlen hint → nem ellenőrizzük
    std::string lp = toLower(path);
    for (const auto& e : it->second) {
        if (endsWith(lp, e)) return true;
    }
    return false;
}

void validateNode(obj* node, const std::string& projectRoot,
                  std::vector<AssetIssue>& out) {
    if (!node) return;

    const char* tname = obj_type(node);
    const char* nname = obj_name(node);
    if (tname) {
        array(reflect_t)* members = members_find(tname);
        if (members) {
            int n = array_count(*members);
            for (int i = 0; i < n; ++i) {
                reflect_t* R = &(*members)[i];
                if (!R->type || strcmp(R->type, "char*") != 0) continue;

                HintInfo h = parseHint(R->info);
                if (h.assetType.empty()) continue;

                char** field = (char**)((char*)node + R->sz);
                char*  val   = field ? *field : nullptr;
                if (!val || !*val) continue;

                AssetIssue base;
                base.node     = node;
                base.nodeName = nname ? nname : "(unnamed)";
                base.typeName = tname;
                base.fieldName = R->name ? R->name : "?";
                base.path     = val;

                // 1) Extension-check (warning).
                if (!extensionMatches(val, h.assetType)) {
                    AssetIssue i = base;
                    i.level  = AssetIssueLevel::Warning;
                    i.reason = std::string("extension does not match [asset:") +
                               h.assetType + "]";
                    out.push_back(std::move(i));
                }

                // 2) Project-boundary (warning, csak abs-path-eknél).
                if (asset_path::isAbsolute(val) &&
                    !projectRoot.empty() &&
                    !asset_path::isWithinProject(val, projectRoot)) {
                    AssetIssue i = base;
                    i.level  = AssetIssueLevel::Warning;
                    i.reason = "asset outside project (not portable)";
                    out.push_back(std::move(i));
                }

                // 3) File-létezés (ERROR).
                std::string abs = asset_path::isAbsolute(val)
                    ? std::string(val)
                    : asset_path::toAbsolute(val, projectRoot);
                if (!is_file(abs.c_str())) {
                    AssetIssue i = base;
                    i.level  = AssetIssueLevel::Error;
                    i.reason = std::string("file not found: ") + abs;
                    out.push_back(std::move(i));
                }
            }
        }
    }

    // Recurse children.
    int cn = editor_obj_child_count(node);
    for (int i = 0; i < cn; ++i) {
        validateNode(editor_obj_child_at(node, i), projectRoot, out);
    }
}

}  // namespace

std::vector<AssetIssue> AssetValidator::validate(EditorApp& app) {
    std::vector<AssetIssue> issues;
    obj* root = app.scene().root();
    if (!root) return issues;
    validateNode(root, app.projectPath(), issues);
    return issues;
}

int AssetValidator::countErrors(const std::vector<AssetIssue>& issues) {
    int n = 0;
    for (const auto& i : issues)
        if (i.level == AssetIssueLevel::Error) ++n;
    return n;
}

int AssetValidator::countWarnings(const std::vector<AssetIssue>& issues) {
    int n = 0;
    for (const auto& i : issues)
        if (i.level == AssetIssueLevel::Warning) ++n;
    return n;
}

}  // namespace editor
