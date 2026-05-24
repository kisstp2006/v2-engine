// STL FIRST (engine `obj`/`is` macro-clash).
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "mesh_renderer_drawer.h"
#include "inspector_registry.h"
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
