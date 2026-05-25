// STL FIRST.
#include <cstdint>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "material_drawer.h"
#include "material_drop_target.h"

namespace editor {

namespace {

const char* kChannelLabels[MAX_CHANNELS_PER_MATERIAL] = {
    "Albedo", "Normals", "Roughness", "Metallic",
    "AO",     "Ambient", "Emissive",  "Parallax",
};

// 64-pixel thumbnail — matches the project-panel asset preview style.
constexpr float kThumbSize = 64.0f;

}  // namespace

bool drawMaterial(material_t* m, const std::string& projectRoot) {
    if (!m) return false;
    bool changed = false;

    // ---- Top-level material flags + values ---------------------------------
    if (ImGui::Checkbox("Enable Shading", &m->enable_shading)) changed = true;
    if (ImGui::Checkbox("Enable IBL",     &m->enable_ibl))     changed = true;
    if (ImGui::ColorEdit3("Base Reflectivity",
                          &m->base_reflectivity.x)) changed = true;
    if (ImGui::DragFloat("SSR Strength", &m->ssr_strength,
                         0.01f, 0.0f, 10.0f)) changed = true;
    if (ImGui::Checkbox("Parallax Clip", &m->parallax_clip)) changed = true;
    if (ImGui::DragFloat("Cutout Alpha", &m->cutout_alpha,
                         0.01f, 0.0f, 1.0f)) changed = true;

    ImGui::Separator();
    ImGui::TextDisabled("Layers");

    // ---- Per-channel collapsible: texture + color + per-channel specials ---
    for (int i = 0; i < MAX_CHANNELS_PER_MATERIAL; ++i) {
        ImGui::PushID(i);

        if (ImGui::CollapsingHeader(kChannelLabels[i])) {
            ImGui::Indent();

            // Texture row: InputText + drop-target → colormap() reload.
            // `material_drop` returns true when texname changed; we forward
            // that signal up so the asset-preview Save-button can flash, etc.
            if (material_drop::drawTextureChannel(
                    m, i, projectRoot, "Texture")) {
                changed = true;
            }

            // Color (full vec4 — albedo uses .w as opacity factor in some
            // engine paths). ColorEdit4 covers all 4 components.
            if (ImGui::ColorEdit4("Color", &m->layer[i].map.color.x)) {
                changed = true;
            }

            // Texture preview thumbnail — same trick the engine's
            // ui_material() uses: ImGui::Image with the texture-id.
            if (m->layer[i].map.texture && m->layer[i].map.texture->id) {
                ImGui::Text("Preview:");
                // Upside-down on Windows GL; flip via UV0/UV1 like the
                // panel-FBO blit so the thumbnail isn't mirrored.
                ImGui::Image(
                    (ImTextureID)(uintptr_t)m->layer[i].map.texture->id,
                    ImVec2(kThumbSize, kThumbSize),
                    ImVec2(0, 1), ImVec2(1, 0));
            }

            // Per-channel special floats (mirrors the engine's ui_material
            // special-cases — render_model.h:954-961).
            if (i == MATERIAL_CHANNEL_PARALLAX) {
                if (ImGui::DragFloat("Parallax Scale",
                                     &m->layer[i].value, 0.01f, 0.0f, 1.0f)) {
                    changed = true;
                }
                if (ImGui::DragFloat("Parallax Shadow Power",
                                     &m->layer[i].value2, 0.1f, 0.0f, 32.0f)) {
                    changed = true;
                }
            } else if (i == MATERIAL_CHANNEL_EMISSIVE) {
                if (ImGui::DragFloat("Emissive Value",
                                     &m->layer[i].value, 0.01f, 0.0f, 10.0f)) {
                    changed = true;
                }
            }

            ImGui::Unindent();
        }

        ImGui::PopID();
    }

    return changed;
}

}  // namespace editor
