#pragma once

// Standalone material-asset file format (`.mat.json5`) save / load.
// One file per material, lives in `<project>/assets/materials/`. Shared
// between many MeshRenderers via MaterialOverride.material_asset_path
// (Fázis 2.4) — central edit, every reference picks up the change.
//
// JSON5 schema:
//   {
//     name: "body_steel",
//     shader: "",                  // RESERVED for future custom-shader extension
//     layers: [
//       { channel: "albedo",    texname: "assets/textures/...", value: 0,
//         value2: 0, color: [1,1,1,1] },
//       ... 8 channels (albedo, normals, roughness, metallic, ao, ambient,
//                       emissive, parallax) ...
//     ],
//     base_reflectivity: [0.04, 0.04, 0.04],
//     cutout_alpha:   0.0,
//     ssr_strength:   0.0,
//     parallax_clip:  false,
//     enable_shading: true,
//     enable_ibl:     true
//   }

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor::material_asset_io {

// Serialize a `material_t` into a JSON5 string.
// Texture-pointers are NOT written (runtime-only); the caller stores the
// texname strings in `material_layer_t.texname` and they survive round-trip.
std::string saveMaterial(const material_t& m);

// Read `.mat.json5` from disk → populate `*out`. Returns false on parse-error
// or missing file. The function also re-loads the textures listed in each
// layer's `texname` via `colormap(...)` so the resulting material is render-
// ready (sRGB-flag set automatically on albedo + emissive layers).
//
// `projectRoot` is needed to resolve relative texname paths to absolute for
// the texture loader. If empty, texname strings are taken as-is.
bool loadMaterial(const std::string& path,
                  const std::string& projectRoot,
                  material_t* out);

// Convenience wrapper: write the serialized blob to disk. Wraps
// `saveMaterial` + `file_write`. Returns true on success.
bool writeFile(const std::string& path, const material_t& m);

// Build an "empty default material" — sensible defaults from the engine's
// gltf_init_material_defaults pattern (white albedo, no normals, mid
// roughness, no metallic, full AO, etc.). The caller can edit from here.
void makeDefault(material_t* out, const char* name);

}  // namespace editor::material_asset_io
