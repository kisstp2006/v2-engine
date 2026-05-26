// STL FIRST.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "material_asset_io.h"
#include "../core/asset_path.h"
#include "../core/file_io.h"
#include "../core/texture_loader.h"

namespace editor::material_asset_io {

namespace {

// Channel-name table — index matches the MATERIAL_CHANNEL_* enum.
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

void appendStr(std::string& out, const char* s) {
    out.push_back('"');
    if (s) {
        for (const char* p = s; *p; ++p) {
            if (*p == '"' || *p == '\\') out.push_back('\\');
            out.push_back(*p);
        }
    }
    out.push_back('"');
}

void appendFloat(std::string& out, float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    out += buf;
}

bool isAlbedoOrEmissive(int channelIdx) {
    return channelIdx == MATERIAL_CHANNEL_ALBEDO
        || channelIdx == MATERIAL_CHANNEL_EMISSIVE;
}

}  // namespace

void makeDefault(material_t* out, const char* name) {
    if (!out) return;
    *out = material_t{};
    out->name = name ? STRDUP(name) : STRDUP("untitled");
    // Channel defaults matching gltf_init_material_defaults
    // (render_model_gltf.h:41-55) — sensible PBR baseline.
    out->layer[MATERIAL_CHANNEL_ALBEDO].map.color    = vec4(1,1,1,1);
    out->layer[MATERIAL_CHANNEL_NORMALS].map.color   = vec4(0,0,0,0);
    out->layer[MATERIAL_CHANNEL_ROUGHNESS].map.color = vec4(1,1,1,1);
    out->layer[MATERIAL_CHANNEL_METALLIC].map.color  = vec4(0,0,0,0);
    out->layer[MATERIAL_CHANNEL_AO].map.color        = vec4(1,1,1,0);
    out->layer[MATERIAL_CHANNEL_AMBIENT].map.color   = vec4(0,0,0,1);
    out->layer[MATERIAL_CHANNEL_EMISSIVE].map.color  = vec4(0,0,0,0);
    out->layer[MATERIAL_CHANNEL_PARALLAX].map.color  = vec4(0,0,0,0);
    out->layer[MATERIAL_CHANNEL_EMISSIVE].value      = 1.0f;
    out->layer[MATERIAL_CHANNEL_PARALLAX].value      = 0.1f;
    out->layer[MATERIAL_CHANNEL_PARALLAX].value2     = 4.0f;
    out->base_reflectivity = vec3(0.04f, 0.04f, 0.04f);
    out->cutout_alpha   = 0.0f;
    out->ssr_strength   = 0.0f;
    out->parallax_clip  = false;
    out->enable_shading = true;
    out->enable_ibl     = true;
    out->_loaded        = true;  // skip the engine's string-pattern matching
}

std::string saveMaterial(const material_t& m) {
    std::string out;
    out.reserve(1024);
    out += "{\n  name: ";
    appendStr(out, m.name);
    out += ",\n  shader: \"\",\n  layers: [\n";

    for (int i = 0; i < MAX_CHANNELS_PER_MATERIAL; ++i) {
        const material_layer_t& L = m.layer[i];
        out += "    { channel: ";
        appendStr(out, kChannelNames[i]);
        out += ", texname: ";
        appendStr(out, L.texname);
        out += ", value: ";   appendFloat(out, L.value);
        out += ", value2: ";  appendFloat(out, L.value2);
        out += ", color: [";
        appendFloat(out, L.map.color.x); out += ",";
        appendFloat(out, L.map.color.y); out += ",";
        appendFloat(out, L.map.color.z); out += ",";
        appendFloat(out, L.map.color.w);
        out += "] }";
        if (i + 1 < MAX_CHANNELS_PER_MATERIAL) out += ",";
        out += "\n";
    }
    out += "  ],\n";
    out += "  base_reflectivity: [";
    appendFloat(out, m.base_reflectivity.x); out += ",";
    appendFloat(out, m.base_reflectivity.y); out += ",";
    appendFloat(out, m.base_reflectivity.z);
    out += "],\n";
    out += "  cutout_alpha:   "; appendFloat(out, m.cutout_alpha);   out += ",\n";
    out += "  ssr_strength:   "; appendFloat(out, m.ssr_strength);   out += ",\n";
    out += "  parallax_clip:  "; out += m.parallax_clip ? "true" : "false"; out += ",\n";
    out += "  enable_shading: "; out += m.enable_shading ? "true" : "false"; out += ",\n";
    out += "  enable_ibl:     "; out += m.enable_ibl ? "true" : "false"; out += "\n";
    out += "}\n";
    return out;
}

bool writeFile(const std::string& path, const material_t& m) {
    std::string s = saveMaterial(m);
    // editor::file_io::writeText — atomic (.tmp + rename), creates parent
    // dirs, STL-based (works on OneDrive Documents folders where motor
    // file_write fails). See core/file_io.h for the full rationale.
    return editor::file_io::writeText(path, s);
}

bool loadMaterial(const std::string& path,
                  const std::string& projectRoot,
                  material_t* out) {
    if (!out) return false;
    // editor::file_io::readText — STL-based, bypasses the motor's file_read
    // which fails on certain Windows path configurations (OneDrive Documents
    // online-only files). Same content, more robust.
    std::string scratch = editor::file_io::readText(path);
    if (scratch.empty()) return false;

    json5 root = {};
    char* err = json5_parse(&root, scratch.data(), 0);
    if (err) { json5_free(&root); return false; }
    if (root.type != JSON5_OBJECT) { json5_free(&root); return false; }

    *out = material_t{};
    makeDefault(out, "untitled");   // sensible baseline; fields below overlay

    for (int i = 0; i < (int)root.count; ++i) {
        const json5* f = &root.nodes[i];
        if (!f->name) continue;
        if (!strcmp(f->name, "name") && f->type == JSON5_STRING) {
            if (out->name) FREE(out->name);
            out->name = STRDUP(f->string ? f->string : "");
        } else if (!strcmp(f->name, "cutout_alpha") &&
                   (f->type == JSON5_REAL || f->type == JSON5_INTEGER)) {
            out->cutout_alpha = (float)(f->type == JSON5_REAL ? f->real : f->integer);
        } else if (!strcmp(f->name, "ssr_strength") &&
                   (f->type == JSON5_REAL || f->type == JSON5_INTEGER)) {
            out->ssr_strength = (float)(f->type == JSON5_REAL ? f->real : f->integer);
        } else if (!strcmp(f->name, "parallax_clip") && f->type == JSON5_BOOL) {
            out->parallax_clip = !!f->boolean;
        } else if (!strcmp(f->name, "enable_shading") && f->type == JSON5_BOOL) {
            out->enable_shading = !!f->boolean;
        } else if (!strcmp(f->name, "enable_ibl") && f->type == JSON5_BOOL) {
            out->enable_ibl = !!f->boolean;
        } else if (!strcmp(f->name, "base_reflectivity") &&
                   f->type == JSON5_ARRAY && f->count >= 3) {
            auto fv = [&](int j) {
                const json5* e = &f->array[j];
                return (float)(e->type == JSON5_REAL ? e->real : e->integer);
            };
            out->base_reflectivity = vec3(fv(0), fv(1), fv(2));
        } else if (!strcmp(f->name, "layers") && f->type == JSON5_ARRAY) {
            for (int k = 0; k < (int)f->count; ++k) {
                const json5* L = &f->array[k];
                if (L->type != JSON5_OBJECT) continue;
                int chIdx = -1;
                const char* texname = "";
                float value = 0, value2 = 0;
                vec4 col = vec4(1, 1, 1, 1);
                for (int j = 0; j < (int)L->count; ++j) {
                    const json5* p = &L->nodes[j];
                    if (!p->name) continue;
                    if (!strcmp(p->name, "channel") && p->type == JSON5_STRING) {
                        chIdx = channelFromName(p->string);
                    } else if (!strcmp(p->name, "texname") &&
                               p->type == JSON5_STRING) {
                        texname = p->string ? p->string : "";
                    } else if (!strcmp(p->name, "value") &&
                               (p->type == JSON5_REAL || p->type == JSON5_INTEGER)) {
                        value = (float)(p->type == JSON5_REAL ? p->real : p->integer);
                    } else if (!strcmp(p->name, "value2") &&
                               (p->type == JSON5_REAL || p->type == JSON5_INTEGER)) {
                        value2 = (float)(p->type == JSON5_REAL ? p->real : p->integer);
                    } else if (!strcmp(p->name, "color") &&
                               p->type == JSON5_ARRAY && p->count >= 4) {
                        auto fv = [&](int j2) {
                            const json5* e = &p->array[j2];
                            return (float)(e->type == JSON5_REAL ? e->real
                                                                : e->integer);
                        };
                        col = vec4(fv(0), fv(1), fv(2), fv(3));
                    }
                }
                if (chIdx < 0 || chIdx >= MAX_CHANNELS_PER_MATERIAL) continue;
                material_layer_t* layer = &out->layer[chIdx];
                strncpy(layer->texname, texname,
                        sizeof(layer->texname) - 1);
                layer->texname[sizeof(layer->texname) - 1] = 0;
                layer->value     = value;
                layer->value2    = value2;
                layer->map.color = col;
                // Load the texture (if any) into colormap_t.texture. The path
                // is project-relative — resolve to abs for the texture loader.
                if (texname[0]) {
                    // `texname` may be either a project-relative path
                    // ("assets/textures/foo.png") OR a bare filename
                    // ("foo.png" — common with the glTF-extractor, since
                    // the extractor flattens into `assets/textures/`).
                    // Bare filenames implicitly resolve to
                    // <project>/assets/textures/<filename>. The buffer
                    // is char[128] since M16+; older scenes still load
                    // fine since the JSON5 reader is length-agnostic.
                    std::string resolved;
                    if (strchr(texname, '/') || strchr(texname, '\\')) {
                        resolved = texname;     // explicit project-rel path
                    } else if (!projectRoot.empty()) {
                        resolved = std::string("assets/textures/") + texname;
                    } else {
                        resolved = texname;
                    }
                    std::string abs = projectRoot.empty()
                        ? resolved
                        : asset_path::toAbsolute(resolved, projectRoot);
                    // Route via the unified editor::texture_loader so the
                    // load goes through editor::file_io (works on every
                    // Windows path) instead of the motor's `colormap()` →
                    // `texture_compressed()` → `file_read()` chain.
                    editor::texture_loader::loadIntoColormap(
                        &layer->map, abs, isAlbedoOrEmissive(chIdx));
                }
            }
        }
        // "shader" field reserved; ignored in MVP.
    }
    json5_free(&root);
    out->_loaded = true;
    return true;
}

}  // namespace editor::material_asset_io
