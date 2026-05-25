// STL FIRST.
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "material_override_io.h"
#include "extra_serializer.h"
#include "../components/components_api.h"
#include "../runtime/material_library.h"

namespace editor::material_override_io {

namespace {

const char* kChannelNames[MAX_CHANNELS_PER_MATERIAL] = {
    "albedo", "normals", "roughness", "metallic",
    "ao",     "ambient", "emissive",  "parallax",
};

int channelFromName(const char* name) {
    if (!name) return -1;
    for (int i = 0; i < MAX_CHANNELS_PER_MATERIAL; ++i) {
        if (!strcmp(kChannelNames[i], name)) return i;
    }
    return -1;
}

// override_mask bit-layout:
//   bit 0..7  → layer-i full override (whole material_layer_t)
//   bit 8     → base_reflectivity
//   bit 9     → cutout_alpha
//   bit 10    → ssr_strength
//   bit 11    → parallax_clip
//   bit 12    → enable_shading
//   bit 13    → enable_ibl
constexpr unsigned BIT_LAYER(int i)       { return 1u << i; }
constexpr unsigned BIT_BASE_REFL          = 1u << 8;
constexpr unsigned BIT_CUTOUT_ALPHA       = 1u << 9;
constexpr unsigned BIT_SSR_STRENGTH       = 1u << 10;
constexpr unsigned BIT_PARALLAX_CLIP      = 1u << 11;
constexpr unsigned BIT_ENABLE_SHADING     = 1u << 12;
constexpr unsigned BIT_ENABLE_IBL         = 1u << 13;

// ---- Lightweight JSON5 emitters ---------------------------------------------

void appendStr(std::string& out, const char* s) {
    out.push_back('"');
    if (s) for (const char* p = s; *p; ++p) {
        if (*p == '"' || *p == '\\') out.push_back('\\');
        out.push_back(*p);
    }
    out.push_back('"');
}

void appendFloat(std::string& out, float v) {
    char buf[32]; snprintf(buf, sizeof(buf), "%.6g", v); out += buf;
}

void appendVec4(std::string& out, vec4 v) {
    out += "[";
    appendFloat(out, v.x); out += ",";
    appendFloat(out, v.y); out += ",";
    appendFloat(out, v.z); out += ",";
    appendFloat(out, v.w); out += "]";
}

void appendInlineMat(std::string& out, const material_t& m) {
    out += "{ layers: [";
    for (int i = 0; i < MAX_CHANNELS_PER_MATERIAL; ++i) {
        const material_layer_t& L = m.layer[i];
        out += "{ texname: ";  appendStr(out, L.texname);
        out += ", value: ";    appendFloat(out, L.value);
        out += ", value2: ";   appendFloat(out, L.value2);
        out += ", color: ";    appendVec4(out, L.map.color);
        out += " }";
        if (i + 1 < MAX_CHANNELS_PER_MATERIAL) out += ",";
    }
    out += "], base_reflectivity: [";
    appendFloat(out, m.base_reflectivity.x); out += ",";
    appendFloat(out, m.base_reflectivity.y); out += ",";
    appendFloat(out, m.base_reflectivity.z);
    out += "], cutout_alpha: ";   appendFloat(out, m.cutout_alpha);
    out += ", ssr_strength: ";    appendFloat(out, m.ssr_strength);
    out += ", parallax_clip: ";   out += m.parallax_clip   ? "true" : "false";
    out += ", enable_shading: ";  out += m.enable_shading  ? "true" : "false";
    out += ", enable_ibl: ";      out += m.enable_ibl      ? "true" : "false";
    out += " }";
}

void parseInlineMat(const json5* node, material_t* out) {
    if (!node || node->type != JSON5_OBJECT || !out) return;
    for (int i = 0; i < (int)node->count; ++i) {
        const json5* f = &node->nodes[i];
        if (!f->name) continue;
        if (!strcmp(f->name, "layers") && f->type == JSON5_ARRAY) {
            for (int k = 0; k < (int)f->count && k < MAX_CHANNELS_PER_MATERIAL; ++k) {
                const json5* L = &f->array[k];
                if (L->type != JSON5_OBJECT) continue;
                material_layer_t* dst = &out->layer[k];
                for (int j = 0; j < (int)L->count; ++j) {
                    const json5* p = &L->nodes[j];
                    if (!p->name) continue;
                    if (!strcmp(p->name, "texname") && p->type == JSON5_STRING) {
                        strncpy(dst->texname, p->string ? p->string : "",
                                sizeof(dst->texname) - 1);
                        dst->texname[sizeof(dst->texname) - 1] = 0;
                    } else if (!strcmp(p->name, "value") &&
                               (p->type == JSON5_REAL || p->type == JSON5_INTEGER)) {
                        dst->value = (float)(p->type == JSON5_REAL
                            ? p->real : p->integer);
                    } else if (!strcmp(p->name, "value2") &&
                               (p->type == JSON5_REAL || p->type == JSON5_INTEGER)) {
                        dst->value2 = (float)(p->type == JSON5_REAL
                            ? p->real : p->integer);
                    } else if (!strcmp(p->name, "color") &&
                               p->type == JSON5_ARRAY && p->count >= 4) {
                        auto fv = [&](int x) {
                            const json5* e = &p->array[x];
                            return (float)(e->type == JSON5_REAL
                                ? e->real : e->integer);
                        };
                        dst->map.color = vec4(fv(0), fv(1), fv(2), fv(3));
                    }
                }
            }
        } else if (!strcmp(f->name, "base_reflectivity") &&
                   f->type == JSON5_ARRAY && f->count >= 3) {
            auto fv = [&](int x) {
                const json5* e = &f->array[x];
                return (float)(e->type == JSON5_REAL ? e->real : e->integer);
            };
            out->base_reflectivity = vec3(fv(0), fv(1), fv(2));
        } else if (!strcmp(f->name, "cutout_alpha") &&
                   (f->type == JSON5_REAL || f->type == JSON5_INTEGER)) {
            out->cutout_alpha = (float)(f->type == JSON5_REAL
                ? f->real : f->integer);
        } else if (!strcmp(f->name, "ssr_strength") &&
                   (f->type == JSON5_REAL || f->type == JSON5_INTEGER)) {
            out->ssr_strength = (float)(f->type == JSON5_REAL
                ? f->real : f->integer);
        } else if (!strcmp(f->name, "parallax_clip") && f->type == JSON5_BOOL) {
            out->parallax_clip = !!f->boolean;
        } else if (!strcmp(f->name, "enable_shading") && f->type == JSON5_BOOL) {
            out->enable_shading = !!f->boolean;
        } else if (!strcmp(f->name, "enable_ibl") && f->type == JSON5_BOOL) {
            out->enable_ibl = !!f->boolean;
        }
    }
}

// ---- Serializer class -------------------------------------------------------

class MeshRendererExtraSerializer : public IComponentExtraSerializer {
public:
    std::string serialize(obj* o) override {
        if (!editor_obj_is_mesh_renderer(o)) return "";
        int n = editor_mesh_renderer_overrides_count(o);
        if (n == 0) return "";  // no overrides → no extras blob

        std::string out;
        out.reserve(256);
        out += "[\n";
        for (int i = 0; i < n; ++i) {
            obj* mo = editor_mesh_renderer_override_at(o, i);
            if (!mo) continue;
            const char* nm   = editor_material_override_name(mo);
            const char* path = editor_material_override_asset_path(mo);
            unsigned mask    = *editor_material_override_mask_addr(mo);

            out += "  { name: ";          appendStr(out, nm);
            out += ", material_asset_path: "; appendStr(out, path);
            char maskbuf[32]; snprintf(maskbuf, sizeof(maskbuf), "%u", mask);
            out += ", override_mask: ";   out += maskbuf;
            out += ", inline_mat: ";
            appendInlineMat(out, *editor_material_override_inline_mat(mo));
            out += " }";
            if (i + 1 < n) out += ",";
            out += "\n";
        }
        out += "]\n";
        return out;
    }

    void deserialize(obj* o, const std::string& blob) override {
        if (!editor_obj_is_mesh_renderer(o) || blob.empty()) return;
        // Re-loading: clear any default-state overrides so we don't double up.
        editor_mesh_renderer_clear_overrides(o);

        std::string scratch = blob;
        json5 root = {};
        char* err = json5_parse(&root, scratch.data(), 0);
        if (err || root.type != JSON5_ARRAY) { json5_free(&root); return; }

        for (int i = 0; i < (int)root.count; ++i) {
            const json5* e = &root.array[i];
            if (e->type != JSON5_OBJECT) continue;

            const char* nm = nullptr;
            const char* path = nullptr;
            unsigned mask = 0;
            const json5* inlineNode = nullptr;
            for (int j = 0; j < (int)e->count; ++j) {
                const json5* f = &e->nodes[j];
                if (!f->name) continue;
                if (!strcmp(f->name, "name") && f->type == JSON5_STRING)
                    nm = f->string;
                else if (!strcmp(f->name, "material_asset_path") &&
                         f->type == JSON5_STRING)
                    path = f->string;
                else if (!strcmp(f->name, "override_mask") &&
                         f->type == JSON5_INTEGER)
                    mask = (unsigned)f->integer;
                else if (!strcmp(f->name, "inline_mat") &&
                         f->type == JSON5_OBJECT)
                    inlineNode = f;
            }
            if (!nm) continue;

            obj* mo = editor_obj_new_material_override(nm);
            if (path) editor_material_override_set_asset_path(mo, path);
            *editor_material_override_mask_addr(mo) = mask;
            if (inlineNode) {
                parseInlineMat(inlineNode,
                               editor_material_override_inline_mat(mo));
            }
            editor_mesh_renderer_add_override(o, mo);
        }
        json5_free(&root);
    }
};

}  // namespace

void registerSerializer() {
    static bool registered = false;
    if (registered) return;
    registered = true;
    ExtraSerializerRegistry::instance().registerSerializer(
        "MeshRenderer", std::make_unique<MeshRendererExtraSerializer>());
}

// ---- Render-walk apply ------------------------------------------------------

void applyOverridesToModel(obj* mr, model_t* mt, const std::string& projectRoot) {
    if (!editor_obj_is_mesh_renderer(mr) || !mt || !mt->materials) return;
    const int nOver = editor_mesh_renderer_overrides_count(mr);
    if (nOver == 0) return;
    const int nMat  = array_count(mt->materials);

    for (int i = 0; i < nOver; ++i) {
        obj* mo = editor_mesh_renderer_override_at(mr, i);
        if (!mo) continue;
        const char* oname = editor_material_override_name(mo);
        if (!oname || !*oname) continue;

        // Name-based slot match (Blokk 2 design — robust to model reorders).
        int slot = -1;
        for (int j = 0; j < nMat; ++j) {
            if (mt->materials[j].name && !strcmp(mt->materials[j].name, oname)) {
                slot = j;
                break;
            }
        }
        if (slot < 0) continue;  // missing slot → silent skip

        material_t* target = &mt->materials[slot];

        // (1) Asset base overlay (if asset_path is set + asset loads OK).
        const char* apath = editor_material_override_asset_path(mo);
        if (apath && *apath) {
            if (material_t* base =
                    MaterialLibrary::instance().lookup(apath, projectRoot)) {
                // Preserve the slot's name (the asset's name may differ).
                char* slotName = target->name;
                *target = *base;
                target->name = slotName;
            }
        }

        // (2) Inline overlay — mask-selected fields.
        const unsigned mask = *editor_material_override_mask_addr(mo);
        const material_t* inl = editor_material_override_inline_mat(mo);
        for (int k = 0; k < MAX_CHANNELS_PER_MATERIAL; ++k) {
            if (mask & BIT_LAYER(k)) {
                target->layer[k] = inl->layer[k];
            }
        }
        if (mask & BIT_BASE_REFL)      target->base_reflectivity = inl->base_reflectivity;
        if (mask & BIT_CUTOUT_ALPHA)   target->cutout_alpha      = inl->cutout_alpha;
        if (mask & BIT_SSR_STRENGTH)   target->ssr_strength      = inl->ssr_strength;
        if (mask & BIT_PARALLAX_CLIP)  target->parallax_clip     = inl->parallax_clip;
        if (mask & BIT_ENABLE_SHADING) target->enable_shading    = inl->enable_shading;
        if (mask & BIT_ENABLE_IBL)     target->enable_ibl        = inl->enable_ibl;
    }
}

}  // namespace editor::material_override_io
