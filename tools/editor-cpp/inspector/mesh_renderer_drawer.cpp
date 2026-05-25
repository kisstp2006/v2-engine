// STL FIRST (engine `obj`/`is` macro-clash).
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "mesh_renderer_drawer.h"
#include "inspector_registry.h"
#include "material_drawer.h"
#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/asset_path.h"

namespace editor {

namespace {

// Lowercase the path's extension (".glb", ".iqm", etc.) — returns empty if no dot.
std::string lower_extension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return (char)std::tolower(c); });
    return ext;
}

const char* format_label(const std::string& ext) {
    if (ext == ".iqm")  return "IQM";
    if (ext == ".gltf") return "glTF";
    if (ext == ".glb")  return "GLB";
    return "?";
}

ImVec4 format_color(const std::string& ext) {
    if (ext == ".iqm")  return ImVec4(0.95f, 0.70f, 0.30f, 1.0f);  // amber
    if (ext == ".gltf") return ImVec4(0.40f, 0.80f, 0.50f, 1.0f);  // green
    if (ext == ".glb")  return ImVec4(0.40f, 0.80f, 0.50f, 1.0f);  // green
    return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);                       // grey
}

}  // namespace

void MeshRendererDrawer::draw(obj* o) {
    auto& reg = InspectorRegistry::instance();

    // 1) Default reflection (model_path / cast_shadows / tint + Transform fields).
    reg.drawDefaults({o});

    // 2) Model Info — only when there is a path and the model loads.
    const char* relPath = editor_mesh_renderer_path(o);
    if (!relPath || !*relPath) return;

    std::string absPath = asset_path::toAbsolute(relPath, reg.projectPath());
    if (!is_file(absPath.c_str())) return;

    // model() is cached engine-side (render_model.h:1297 `model_cache`), so
    // calling it every frame is a hash-map lookup, not a re-load.
    model_t mt = model(absPath.c_str(), 0);
    if (!mt.iqm) return;

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Model Info");

    // Format chip — small colored badge with the file format.
    std::string ext = lower_extension(relPath);
    const char* fmt = format_label(ext);
    ImVec4 col = format_color(ext);
    ImGui::PushStyleColor(ImGuiCol_Button, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
    ImGui::SmallButton(fmt);
    ImGui::PopStyleColor(3);
    ImGui::SameLine();
    ImGui::TextDisabled("format");

    // Counts: meshes / triangles are always present; joints / anims / frames
    // only meaningful for skinned models.
    ImGui::Text("Meshes:    %u", mt.num_meshes);
    ImGui::Text("Triangles: %u", mt.num_triangles);
    if (mt.num_joints > 0) {
        ImGui::Text("Joints:    %u", mt.num_joints);
        ImGui::Text("Anims:     %u", mt.num_anims);
        ImGui::Text("Frames:    %u", mt.num_frames);
    } else {
        ImGui::TextDisabled("(static mesh)");
    }

    // ---- 3) Materials (Blokk 2.6) — per-slot asset-picker + inline overlay
    if (!mt.materials || array_count(mt.materials) == 0) return;

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::TextDisabled("Materials");

    // Scan available material assets under `<project>/assets/materials/`.
    // Cheap (small folder, frame-rate scan is fine for typical projects).
    namespace fs = std::filesystem;
    std::vector<std::string> matAssets;
    {
        fs::path matsDir = fs::path(reg.projectPath()) / "assets" / "materials";
        std::error_code ec;
        if (fs::is_directory(matsDir, ec)) {
            for (auto& e : fs::directory_iterator(matsDir, ec)) {
                if (!e.is_regular_file()) continue;
                auto p = e.path();
                std::string filename = p.filename().string();
                // .mat.json5 suffix check (case-insensitive).
                std::string lower = filename;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c){ return (char)std::tolower(c); });
                if (lower.size() < 10 ||
                    lower.compare(lower.size() - 10, 10, ".mat.json5") != 0) {
                    continue;
                }
                matAssets.push_back("assets/materials/" + filename);
            }
        }
    }

    constexpr unsigned kMaskInlineAll = 0xFFFFu;  // every layer + meta bit

    const int nMat = array_count(mt.materials);
    for (int i = 0; i < nMat; ++i) {
        ImGui::PushID(i);
        const char* slotName = mt.materials[i].name ? mt.materials[i].name : "?";

        // Find existing override for this slot (matched by name).
        obj* mo = editor_mesh_renderer_find_override_by_name(o, slotName);
        const char* curPath = mo ? editor_material_override_asset_path(mo) : "";

        // Header row: slot name + asset-picker combo + remove-button.
        ImGui::TextUnformatted(slotName);
        ImGui::SameLine(140.0f);

        const char* comboPreview = (curPath && *curPath) ? curPath : "(none)";
        ImGui::SetNextItemWidth(-30);
        if (ImGui::BeginCombo("##matpicker", comboPreview)) {
            // "(none)" → if an override exists, switch to pure-inline mode
            // (the inline_mat is initialized to the engine's material at
            // creation, see comment in the per-asset branch below).
            bool noneSel = (!curPath || !*curPath);
            if (ImGui::Selectable("(none)", noneSel)) {
                if (!mo) {
                    mo = editor_obj_new_material_override(slotName);
                    // Seed inline_mat from the engine's current material
                    // so the user starts editing from a sensible baseline.
                    *editor_material_override_inline_mat(mo) = mt.materials[i];
                    editor_mesh_renderer_add_override(o, mo);
                }
                editor_material_override_set_asset_path(mo, "");
                *editor_material_override_mask_addr(mo) = kMaskInlineAll;
            }
            for (const auto& path : matAssets) {
                bool sel = (curPath && path == curPath);
                if (ImGui::Selectable(path.c_str(), sel)) {
                    if (!mo) {
                        mo = editor_obj_new_material_override(slotName);
                        editor_mesh_renderer_add_override(o, mo);
                    }
                    editor_material_override_set_asset_path(mo, path.c_str());
                    *editor_material_override_mask_addr(mo) = 0;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        // Remove-override button — flag-only; the removal happens after the
        // per-slot UI so we don't iterate a mutating array.
        bool wantRemove = mo && ImGui::SmallButton("X");
        if (mo && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove override (revert to model's material)");
        }

        // Inline-overlay editor — only when in "(none)" mode (mask = 0xFFFF).
        if (mo && (!curPath || !*curPath)) {
            ImGui::Indent();
            if (ImGui::TreeNodeEx("Inline overlay",
                                  ImGuiTreeNodeFlags_SpanAvailWidth)) {
                drawMaterial(editor_material_override_inline_mat(mo),
                             reg.projectPath());
                ImGui::TreePop();
            }
            ImGui::Unindent();
        }

        if (wantRemove) {
            // Linear-scan to find the index; small N, simple > clever.
            int oc = editor_mesh_renderer_overrides_count(o);
            for (int j = 0; j < oc; ++j) {
                if (editor_mesh_renderer_override_at(o, j) == mo) {
                    editor_mesh_renderer_remove_override(o, j);
                    break;
                }
            }
        }
        ImGui::PopID();
    }
}

// Static-init registration (like ScriptDrawer).
namespace {
struct MeshRendererDrawerRegistrar {
    MeshRendererDrawerRegistrar() {
        InspectorRegistry::instance().registerDrawer(
            "MeshRenderer", std::make_unique<MeshRendererDrawer>());
    }
};
static MeshRendererDrawerRegistrar _reg;
}  // namespace

}  // namespace editor
