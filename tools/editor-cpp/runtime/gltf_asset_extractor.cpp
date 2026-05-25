// STL FIRST (engine `obj`/`is` macro-clash).
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "gltf_asset_extractor.h"
#include "../persistence/material_asset_io.h"

namespace editor::gltf_asset_extractor {

namespace fs = std::filesystem;

namespace {

// Sanitize a name (material-name / file-stem) into a safe-filename token.
std::string sanitize(const std::string& s, const char* fallback = "untitled") {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (isalnum((unsigned char)c) || c == '_' || c == '-') out.push_back(c);
        else if (c == ' ' || c == '/' || c == '\\') out.push_back('_');
    }
    return out.empty() ? std::string(fallback) : out;
}

// Decode raw image bytes (from any glTF-supported format) and write as PNG.
// `bytes` may be JPEG/PNG/BMP — stbi handles all stbi-supported formats and
// stbi_write_png re-encodes as PNG. Returns true on success.
bool writePngFromBytes(const uint8_t* bytes, int len, const fs::path& outPath) {
    if (!bytes || len <= 0) return false;
    int w = 0, h = 0, comp = 0;
    stbi_uc* rgba = stbi_load_from_memory(bytes, len, &w, &h, &comp, 4);
    if (!rgba) return false;
    std::error_code ec;
    fs::create_directories(outPath.parent_path(), ec);
    int rc = stbi_write_png(outPath.string().c_str(), w, h, 4, rgba, w * 4);
    stbi_image_free(rgba);
    return rc != 0;
}

// Extract one cgltf_image to disk. Returns the file-name (relative to the
// project's `assets/textures/` root) that went into the resulting file, or
// "" on failure. CRITICAL: must fit `material_layer_t.texname` = char[32]
// (engine limit). We therefore: (a) skip per-model subfolders, (b) write
// straight into `assets/textures/`, (c) use 3-letter channel suffixes —
// see the caller for the naming scheme.
//
// Three cases handled (mirrors gltf_load_image at render_model_gltf.h:60-75):
//   (1) image has buffer_view (embedded GLB-binary, or a data:URI that
//       cgltf_load_buffers has unpacked) → stbi-decode + write_png
//   (2) image has external `uri` → resolve relative to the .gltf's parent dir,
//       and either: a) copy the file as-is if it's already PNG, b) decode +
//       re-encode as PNG otherwise.
//   (3) Nothing → empty string + warning.
std::string extractImage(cgltf_image* img,
                         const fs::path& gltfDir,
                         const fs::path& outDir,
                         const std::string& outBaseName,
                         const std::string& /*projectRoot*/,
                         std::vector<std::string>* warnings) {
    if (!img) return "";

    fs::path outFile = outDir / (outBaseName + ".png");

    // Case 1: embedded buffer_view (GLB binary + data:URI both end up here
    // after cgltf_load_buffers).
    if (img->buffer_view && img->buffer_view->buffer
            && img->buffer_view->buffer->data) {
        const uint8_t* data = (const uint8_t*)img->buffer_view->buffer->data
                            + img->buffer_view->offset;
        int len = (int)img->buffer_view->size;
        if (!writePngFromBytes(data, len, outFile)) {
            if (warnings) {
                warnings->push_back(std::string("embedded image decode failed: ")
                                    + outBaseName);
            }
            return "";
        }
        // Return just the filename (no `assets/textures/` prefix) — keeps the
        // string short enough for material_layer_t.texname's 32-byte limit.
        // load-time resolves the implicit `assets/textures/` prefix.
        return outBaseName + ".png";
    }

    // Case 2: external uri (cgltf_load_buffers has already turned data:URIs
    // into buffer_views, so this is a real file path).
    if (img->uri && img->uri[0]) {
        fs::path src = gltfDir / img->uri;
        std::error_code ec;
        if (!fs::exists(src, ec)) {
            if (warnings) {
                warnings->push_back(std::string("external image missing: ")
                                    + img->uri);
            }
            return "";
        }
        // If already a PNG with the right name, just copy as-is (lossless,
        // preserves original encoding). For other formats, decode + re-encode
        // as PNG so we always have a uniform format in assets/textures/.
        std::string ext = src.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c){ return (char)std::tolower(c); });
        bool isPng = (ext == ".png");

        fs::create_directories(outDir, ec);

        if (isPng) {
            fs::copy_file(src, outFile,
                fs::copy_options::overwrite_existing, ec);
            if (ec) {
                if (warnings) {
                    warnings->push_back(std::string("copy failed: ") +
                                        ec.message() + " — " + src.string());
                }
                return "";
            }
        } else {
            // Decode + re-encode. Read whole file → stbi.
            int sz = 0;
            char* raw = file_read(src.string().c_str(), &sz);
            if (!raw || sz <= 0 ||
                !writePngFromBytes((const uint8_t*)raw, sz, outFile)) {
                if (warnings) {
                    warnings->push_back(std::string(
                        "decode/encode failed for external image: ") +
                        src.string());
                }
                return "";
            }
        }
        return outBaseName + ".png";
    }

    if (warnings) {
        warnings->push_back(std::string("image has neither buffer_view nor uri: ")
                            + outBaseName);
    }
    return "";
}

}  // namespace

ExtractResult extractGltfAssets(const std::string& gltfAbsPath,
                                const std::string& projectRoot) {
    ExtractResult r;

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result cr = cgltf_parse_file(&options, gltfAbsPath.c_str(), &data);
    if (cr != cgltf_result_success) {
        r.error = "cgltf_parse_file failed (code=" + std::to_string((int)cr) + ")";
        return r;
    }
    cgltf_load_buffers(&options, data, gltfAbsPath.c_str());

    fs::path gltfPath(gltfAbsPath);
    std::string gltfBase = sanitize(gltfPath.stem().string(), "model");
    // No per-glTF subfolder — `material_layer_t.texname` is char[32], so the
    // stored path can't include `assets/textures/<basename>/` (16 + N + 1 +
    // filename + .png easily blows the limit). We dump straight into
    // `assets/textures/` and load-time auto-prefixes that root.
    fs::path texDir = fs::path(projectRoot) / "assets" / "textures";
    fs::path matDir = fs::path(projectRoot) / "assets" / "materials";

    std::error_code ec;
    fs::create_directories(texDir, ec);
    fs::create_directories(matDir, ec);

    for (size_t i = 0; i < data->materials_count; ++i) {
        cgltf_material* gm = &data->materials[i];
        std::string matName = sanitize(
            gm->name ? gm->name : "gltf_material", "material");
        std::string assetFilename = gltfBase + "_" + matName + ".mat.json5";
        fs::path matFile = matDir / assetFilename;

        // Idempotency: skip if the .mat.json5 already exists — the user may
        // have edited it. The MaterialOverride auto-link below still wires
        // up the existing asset, so re-importing is safe.
        bool matExists = fs::exists(matFile, ec);

        // Build the in-memory material_t — mirror model_load_materials_gltf
        // but route texture pointers to the extracted PNG paths.
        material_t mt;
        material_asset_io::makeDefault(&mt, matName.c_str());

        // Short channel suffixes — keep the filename within the texname
        // 32-byte limit. (Full names like "metallicRoughness" blow the
        // budget on any non-trivial material name.)
        auto extractInto = [&](cgltf_image* img, int channelIdx,
                               const char* shortChan) {
            if (!img) return;
            std::string outBase = matName + "_" + shortChan;
            std::string rel = extractImage(img, gltfPath.parent_path(),
                                           texDir, outBase, projectRoot,
                                           &r.warnings);
            if (rel.empty()) { ++r.textures_skipped; return; }
            // `rel` is now just the bare filename (e.g. "Duck_alb.png").
            // load-time will resolve it against <project>/assets/textures/.
            strncpy(mt.layer[channelIdx].texname, rel.c_str(),
                    sizeof(mt.layer[channelIdx].texname) - 1);
            mt.layer[channelIdx].texname[
                sizeof(mt.layer[channelIdx].texname) - 1] = 0;
            if ((int)rel.size() >= (int)sizeof(mt.layer[channelIdx].texname)) {
                r.warnings.push_back(
                    "filename truncated (>=32 bytes): " + rel);
            }
            ++r.textures_written;
        };

        // ALBEDO + base_color_factor
        if (gm->has_pbr_metallic_roughness) {
            cgltf_pbr_metallic_roughness* pbr = &gm->pbr_metallic_roughness;
            mt.layer[MATERIAL_CHANNEL_ALBEDO].map.color = vec4(
                pbr->base_color_factor[0], pbr->base_color_factor[1],
                pbr->base_color_factor[2], pbr->base_color_factor[3]);
            if (pbr->base_color_texture.texture) {
                extractInto(pbr->base_color_texture.texture->image,
                            MATERIAL_CHANNEL_ALBEDO, "alb");
            }
            mt.layer[MATERIAL_CHANNEL_ROUGHNESS].map.color = vec4(
                pbr->roughness_factor, pbr->roughness_factor,
                pbr->roughness_factor, 1);
            mt.layer[MATERIAL_CHANNEL_METALLIC].map.color = vec4(
                pbr->metallic_factor, pbr->metallic_factor,
                pbr->metallic_factor, 1);
            // MR is a packed image (G=rough, B=metal). MVP: dump the packed
            // PNG into the roughness slot — the metallic slot keeps the
            // factor-only color. User can split + re-route via the editor.
            if (pbr->metallic_roughness_texture.texture) {
                extractInto(pbr->metallic_roughness_texture.texture->image,
                            MATERIAL_CHANNEL_ROUGHNESS, "mr");
            }
        }
        if (gm->normal_texture.texture) {
            extractInto(gm->normal_texture.texture->image,
                        MATERIAL_CHANNEL_NORMALS, "nrm");
        }
        if (gm->occlusion_texture.texture) {
            extractInto(gm->occlusion_texture.texture->image,
                        MATERIAL_CHANNEL_AO, "ao");
        }
        if (gm->emissive_texture.texture) {
            extractInto(gm->emissive_texture.texture->image,
                        MATERIAL_CHANNEL_EMISSIVE, "emi");
        }
        mt.layer[MATERIAL_CHANNEL_EMISSIVE].map.color = vec4(
            gm->emissive_factor[0], gm->emissive_factor[1],
            gm->emissive_factor[2], 1);

        // Persist the material asset (skip-if-exists).
        if (matExists) {
            ++r.materials_skipped_existing;
        } else {
            material_asset_io::writeFile(matFile.string(), mt);
        }

        // Record the slot-name → asset-path mapping for createMesh's
        // MaterialOverride auto-link step.
        GeneratedMaterial gm_out;
        gm_out.slotName     = gm->name ? gm->name : "gltf_material";
        std::error_code ec2;
        fs::path relMat = fs::relative(matFile, projectRoot, ec2);
        std::string s = relMat.empty() ? matFile.string() : relMat.string();
        std::replace(s.begin(), s.end(), '\\', '/');
        gm_out.assetRelPath = s;
        r.materials.push_back(std::move(gm_out));

        // Free the STRDUP'd name from makeDefault to avoid the leak (we
        // don't push the material_t into a model_cache here).
        if (mt.name) FREE(mt.name);
    }

    cgltf_free(data);
    return r;
}

}  // namespace editor::gltf_asset_extractor
