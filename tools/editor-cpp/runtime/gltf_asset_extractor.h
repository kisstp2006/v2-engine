#pragma once

// glTF / GLB asset extractor (Blokk 2.8).
//
// The v2-engine already loads a glTF and renders it (model() + the cgltf
// loader). What's missing for a Unity-style "drop the glTF, edit the
// materials" workflow:
//   - Embedded textures (GLB binary chunk, data: URIs) need to be written
//     out as standalone PNGs so the user can see them in the Project panel
//     and edit them in a paint app.
//   - External textures (.png next to the .gltf) need to be copied into
//     `<project>/assets/textures/<gltf_base>/` so the project is portable.
//   - Each glTF material → a `<project>/assets/materials/<gltf_base>_<mat>.mat.json5`
//     asset, with texname pointing at the extracted PNGs. This lets the
//     user override or share materials across MeshRenderers via the
//     MaterialOverride asset-picker (Blokk 2.6).
//
// Idempotency: by default we SKIP existing `.mat.json5` files (don't clobber
// user edits) and OVERWRITE textures (the glTF is the source of truth for
// raw texture bytes; if the user wants to edit, they can rename the file
// afterwards). The MeshRenderer-spawn step ALWAYS happens — the extractor
// returns the list of paths for the caller (createMesh) to wire up overrides.

#include <string>
#include <vector>

namespace editor::gltf_asset_extractor {

// One generated material-asset description: name + relative path
// (`assets/materials/<gltf>_<mat>.mat.json5`). Used by `createMesh` to
// wire up MaterialOverride asset-refs.
struct GeneratedMaterial {
    std::string slotName;        // matches model_t.materials[i].name
    std::string assetRelPath;    // project-relative path
};

struct ExtractResult {
    std::vector<GeneratedMaterial> materials;
    int textures_written = 0;
    int textures_skipped = 0;      // unsupported format / failed write
    int materials_skipped_existing = 0;
    std::vector<std::string>       warnings;
    std::string                    error;     // non-empty → fatal (parse fail)
};

// Parse the glTF/GLB at `gltfAbsPath`, extract all textures into
// `<projectRoot>/assets/textures/<basename>/`, generate `.mat.json5`
// assets in `<projectRoot>/assets/materials/`. Safe to call on an
// already-imported glTF — existing material-assets are kept.
ExtractResult extractGltfAssets(const std::string& gltfAbsPath,
                                const std::string& projectRoot);

}  // namespace editor::gltf_asset_extractor
