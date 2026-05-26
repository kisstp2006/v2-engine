// STL FIRST (engine `obj`/`is` macro-clash).
#include <cstring>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "material_drop_target.h"
#include "../core/asset_path.h"

namespace editor::material_drop {

namespace {

// sRGB-flag policy: albedo + emissive are visible-color channels (engine
// convention, mirrors gltf_init_material_defaults and material_asset_io's
// loadMaterial). Normals / roughness / metallic / AO / ambient / parallax
// are linear data channels.
bool isSrgbChannel(int channelIdx) {
    return channelIdx == MATERIAL_CHANNEL_ALBEDO
        || channelIdx == MATERIAL_CHANNEL_EMISSIVE;
}

}  // namespace

bool drawTextureChannel(material_t* m,
                        int channelIdx,
                        const std::string& projectRoot,
                        const char* channelLabel) {
    if (!m || channelIdx < 0 || channelIdx >= MAX_CHANNELS_PER_MATERIAL) {
        return false;
    }

    ImGui::PushID(channelIdx);

    // Label column — small fixed width so the InputText below lines up nicely.
    constexpr float kLabelW = 96.0f;
    ImGui::TextUnformatted(channelLabel ? channelLabel : "Channel");
    ImGui::SameLine(kLabelW);

    material_layer_t* L = &m->layer[channelIdx];

    // InputText on the texname buffer (texname is `char[128]` — see
    // material_layer_t in render_material.h). EnterReturnsTrue lets the user
    // commit a manually-typed path; auto-load happens then. The InputText
    // uses sizeof(L->texname), so any future change to the field size
    // propagates automatically without touching this file.
    bool changed = false;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##texpath", L->texname, sizeof(L->texname),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        changed = true;
    }

    // Drag-drop target — the Project panel's source emits "ASSET_PATH"
    // with the absolute path NUL-terminated. We convert to project-relative
    // before storing in texname, so the asset is portable between machines.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload =
                ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            const char* absPath = (const char*)payload->Data;
            if (absPath && *absPath) {
                std::string rel = asset_path::toProjectRelative(
                    absPath, projectRoot);
                // texname is char[128] (engine limit). If the file lives in
                // `assets/textures/`, store the bare filename — load-time
                // re-prefixes that root. Only fall back to project-relative
                // for files outside the conventional location.
                static const char kTexRoot[] = "assets/textures/";
                if (rel.size() > sizeof(kTexRoot) - 1 &&
                    rel.compare(0, sizeof(kTexRoot) - 1, kTexRoot) == 0 &&
                    rel.find('/', sizeof(kTexRoot) - 1) == std::string::npos) {
                    // assets/textures/foo.png (no further subfolder) → "foo.png"
                    rel = rel.substr(sizeof(kTexRoot) - 1);
                }
                strncpy(L->texname, rel.c_str(), sizeof(L->texname) - 1);
                L->texname[sizeof(L->texname) - 1] = 0;
                changed = true;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (changed) {
        // Re-load the texture into colormap_t. If texname is empty, clear
        // the texture pointer (colormap returns false → cm->texture = NULL
        // already, mirror that).
        if (L->texname[0]) {
            std::string abs = projectRoot.empty()
                ? std::string(L->texname)
                : asset_path::toAbsolute(L->texname, projectRoot);
            colormap(&L->map, abs.c_str(), isSrgbChannel(channelIdx));
        } else {
            L->map.texture = nullptr;
        }
    }

    ImGui::PopID();
    return changed;
}

}  // namespace editor::material_drop
