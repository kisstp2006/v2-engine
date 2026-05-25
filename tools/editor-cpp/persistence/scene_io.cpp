// STL FIRST.
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "scene_io.h"
#include "extra_serializer.h"
#include "../core/asset_path.h"
#include "../inspector/hint_parser.h"
#include "../scene/scene_helpers.h"

namespace editor {

namespace {

void escapeJson5(const std::string& in, std::string& out) {
    for (char c : in) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
}

// The engine's `3rd_json5.h` parser takes the string content as-is (it
// doesn't unescape `\\n` to newline, nor `\\\\` to backslash). Our own
// unescape is needed so that `obj_make` correctly gets back the body string.
std::string unescapeJson5(const char* s) {
    std::string out;
    if (!s) return out;
    while (*s) {
        if (*s == '\\' && s[1]) {
            switch (s[1]) {
                case 'n':  out += '\n'; s += 2; continue;
                case 'r':  out += '\r'; s += 2; continue;
                case 't':  out += '\t'; s += 2; continue;
                case '\\': out += '\\'; s += 2; continue;
                case '"':  out += '"';  s += 2; continue;
                case '\'': out += '\''; s += 2; continue;
                case '/':  out += '/';  s += 2; continue;
                default:   out += *s++; continue;
            }
        }
        out += *s++;
    }
    return out;
}

// Phase 4b — node-level auto-migration. For every `char*` field, if there's
// an `[asset:*]` hint + the stored value is an abs path + within the project
// → converts it to relative. Returns the number of migrated fields.
int migrateNodePaths(obj* node, const std::string& projectRoot) {
    if (!node || projectRoot.empty()) return 0;
    const char* type = obj_type(node);
    if (!type) return 0;
    array(reflect_t)* members = members_find(type);
    if (!members) return 0;

    int migrated = 0;
    int n = array_count(*members);
    for (int i = 0; i < n; ++i) {
        reflect_t* R = &(*members)[i];
        const char* mt = R->type;
        if (!mt || strcmp(mt, "char*") != 0) continue;

        HintInfo h = parseHint(R->info);
        if (h.assetType.empty()) continue;   // only migrate [asset:*]

        char** field = (char**)((char*)node + R->sz);
        char*  val   = field ? *field : nullptr;
        if (!val || !*val) continue;
        if (!asset_path::isAbsolute(val))          continue;
        if (!asset_path::isWithinProject(val, projectRoot)) continue;

        std::string rel = asset_path::toProjectRelative(val, projectRoot);
        if (rel == val) continue;

        FREE(val);
        *field = STRDUP(rel.c_str());
        ++migrated;
    }
    return migrated;
}

void collectNodes(obj* node, int parentIdx,
                  std::vector<std::tuple<int, std::string, std::string>>& out) {
    if (!node) return;
    const char* nm = obj_name(node);
    // INI format (`[Type]\nfield.name=value\n...`) — the engine's `obj_make`
    // parses this correctly (typename inside the `[]`). `obj_savejson` on
    // the other hand puts the typename BEFORE the `{`, and the `obj_make`
    // parser can't find it.
    char* body = obj_saveini(node);
    std::string bodyStr(body ? body : "");

    // Editor-side extras: if a serializer is registered for this objType,
    // append the marker + blob. The v2 obj_mergeini IGNORES post-marker lines
    // (they're not `<type>.<name>=` formatted), so this is transparent to the
    // engine reflection rebuild.
    const char* type = obj_type(node);
    IComponentExtraSerializer* ser =
        ExtraSerializerRegistry::instance().lookup(type);
    if (ser) {
        std::string blob = ser->serialize(node);
        if (!blob.empty()) {
            if (!bodyStr.empty() && bodyStr.back() != '\n') bodyStr += '\n';
            bodyStr += ExtraSerializerRegistry::kMarker;
            bodyStr += '\n';
            bodyStr += blob;
        }
    }

    int myIdx = (int)out.size();
    out.emplace_back(parentIdx, std::string(nm ? nm : ""), std::move(bodyStr));

    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectNodes(editor_obj_child_at(node, i), myIdx, out);
    }
}

}  // namespace

std::string SceneIO::saveTree(obj* root) {
    std::vector<std::tuple<int, std::string, std::string>> nodes;
    collectNodes(root, -1, nodes);

    std::string out;
    out.reserve(4096);
    out += "[\n";
    for (size_t i = 0; i < nodes.size(); ++i) {
        int parentIdx = std::get<0>(nodes[i]);
        const auto& name = std::get<1>(nodes[i]);
        const auto& body = std::get<2>(nodes[i]);

        out += "  { parent: ";
        out += std::to_string(parentIdx);
        out += ", name: \"";
        escapeJson5(name, out);
        out += "\", body: \"";
        escapeJson5(body, out);
        out += "\" },\n";
    }
    out += "]\n";
    return out;
}

std::string SceneIO::saveSubtree(obj* node) {
    // saveTree with the given node as the root of the tree.
    return saveTree(node);
}

obj* SceneIO::loadSubtree(obj* parent, const std::string& json,
                          const std::string& projectRoot) {
    LoadResult r = loadTreeDetailed(json, projectRoot);
    if (!r.root) return nullptr;
    if (parent) obj_attach(parent, r.root);
    return r.root;
}

obj* SceneIO::loadTree(const std::string& json) {
    return loadTreeDetailed(json).root;
}

LoadResult SceneIO::loadTreeDetailed(const std::string& json,
                                     const std::string& projectRoot) {
    LoadResult result;
    json5 root = {0};
    char* err = json5_parse(&root, (char*)json.c_str(), 0);
    if (err) {
        result.errors.push_back(std::string("JSON5 parse error: ") + err);
        json5_free(&root);
        return result;
    }
    if (root.type != JSON5_ARRAY) {
        result.errors.push_back("expected JSON5 array at top level");
        json5_free(&root);
        return result;
    }

    std::vector<obj*> created;
    created.reserve(root.count);

    for (int i = 0; i < root.count; ++i) {
        json5* n = &root.nodes[i];
        if (n->type != JSON5_OBJECT) {
            result.errors.push_back("node[" + std::to_string(i) +
                                    "]: not a JSON5 object");
            created.push_back(nullptr);
            ++result.failed;
            continue;
        }
        int parentIdx = -1;
        const char* name = nullptr;
        const char* body = nullptr;
        for (int j = 0; j < n->count; ++j) {
            const char* fn = n->nodes[j].name;
            if (!fn) continue;
            if      (!strcmp(fn, "parent")) parentIdx = (int)n->nodes[j].integer;
            else if (!strcmp(fn, "name"))   name = n->nodes[j].string;
            else if (!strcmp(fn, "body"))   body = n->nodes[j].string;
        }

        std::string realBody = unescapeJson5(body);

        // Split off the editor-extras blob (if any) before handing the body to
        // obj_make. The v2 obj_mergeini would also ignore post-marker content
        // (no `<type>.<name>=` lines), but stripping it explicitly keeps the
        // ini-portion clean and lets us pass the blob to the right serializer.
        std::string extrasBlob;
        {
            const std::string marker = ExtraSerializerRegistry::kMarker;
            size_t mpos = realBody.find(marker);
            if (mpos != std::string::npos) {
                size_t after = mpos + marker.size();
                if (after < realBody.size() && realBody[after] == '\n') ++after;
                extrasBlob = realBody.substr(after);
                // Trim the marker + blob off the body before obj_make.
                realBody.resize(mpos);
                while (!realBody.empty() && realBody.back() == '\n') {
                    realBody.pop_back();
                }
            }
        }

        obj* o = !realBody.empty() ? (obj*)obj_make(realBody.c_str()) : nullptr;
        if (!o) {
            std::string tHint;
            if (!realBody.empty()) {
                const char* colon = strchr(realBody.c_str(), ':');
                tHint = colon ? std::string(realBody.c_str(),
                                            colon - realBody.c_str())
                              : "?";
            }
            result.errors.push_back("node[" + std::to_string(i) +
                                    "]: obj_make failed (type=" +
                                    tHint + ", name=" +
                                    (name ? name : "?") + ")");
            ++result.failed;
        } else {
            if (name && *name) obj_setname(o, name);
            if (parentIdx >= 0 && parentIdx < (int)created.size()
                && created[parentIdx]) {
                obj_attach(created[parentIdx], o);
            } else if (!result.root) {
                result.root = o;
            }
            // Phase 4b — auto-migration: we convert every [asset:*] char*
            // field to relative, if abs+in-project.
            result.migrated_paths += migrateNodePaths(o, projectRoot);
            // Editor-extras: if a serializer is registered for this objType,
            // hand it the blob (no-op if no blob or no serializer).
            if (!extrasBlob.empty()) {
                const char* type = obj_type(o);
                if (auto* ser = ExtraSerializerRegistry::instance().lookup(type)) {
                    ser->deserialize(o, extrasBlob);
                }
            }
            ++result.created;
        }
        created.push_back(o);
    }

    json5_free(&root);
    return result;
}

#if 0  // old loadTree implementation — replaced by loadTreeDetailed.
obj* SceneIO::__unused_loadTree(const std::string& json) {
    json5 root = {0};
    char* err = json5_parse(&root, (char*)json.c_str(), 0);
    if (err) {
        json5_free(&root);
        return nullptr;
    }
    if (root.type != JSON5_ARRAY) {
        json5_free(&root);
        return nullptr;
    }

    std::vector<obj*> created;
    created.reserve(root.count);
    obj* firstRoot = nullptr;

    for (int i = 0; i < root.count; ++i) {
        json5* n = &root.nodes[i];
        if (n->type != JSON5_OBJECT) {
            created.push_back(nullptr);
            continue;
        }

        int parentIdx = -1;
        const char* name = nullptr;
        const char* body = nullptr;
        for (int j = 0; j < n->count; ++j) {
            const char* fn = n->nodes[j].name;
            if (!fn) continue;
            if      (!strcmp(fn, "parent")) parentIdx = (int)n->nodes[j].integer;
            else if (!strcmp(fn, "name"))   name = n->nodes[j].string;
            else if (!strcmp(fn, "body"))   body = n->nodes[j].string;
        }

        // Un-escape the body (the engine's json5_parse leaves `\\n` as-is etc).
        std::string realBody = unescapeJson5(body);
        obj* o = !realBody.empty() ? (obj*)obj_make(realBody.c_str()) : nullptr;
        if (o) {
            if (name && *name) obj_setname(o, name);
            if (parentIdx >= 0 && parentIdx < (int)created.size()
                && created[parentIdx]) {
                obj_attach(created[parentIdx], o);
            } else if (!firstRoot) {
                firstRoot = o;
            }
        }
        created.push_back(o);
    }

    json5_free(&root);
    return firstRoot;
}
#endif

}  // namespace editor
