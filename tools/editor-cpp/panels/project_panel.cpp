// STL FIRST.
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "engine.h"

#include "project_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../app/file_type_registry.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/file_picker.h"

namespace editor {

namespace {

namespace fs = std::filesystem;

// Path helper: forward-slash normalization (Windows uses backslash
// in `fs::path::string()`, and the breadcrumb/console-log looks ugly).
std::string normSlash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// Filename sanitizer for the new-asset modal — keeps A-Z a-z 0-9 _ - .
// (allow dot for extensions like "my.thing"), maps spaces to underscore,
// drops everything else. Used for folder + scene + script names.
std::string sanitizeFilename(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (char c : in) {
        if (std::isalnum((unsigned char)c) ||
            c == '_' || c == '-' || c == '.') out.push_back(c);
        else if (c == ' ') out.push_back('_');
    }
    return out;
}

// Find a unique filename in `dir` by appending _2, _3, ... before the
// extension if the requested name already exists.
fs::path uniqueTarget(const fs::path& dir, const std::string& stem,
                      const std::string& ext) {
    fs::path target = dir / (stem + ext);
    std::error_code ec;
    int suffix = 2;
    while (fs::exists(target, ec)) {
        target = dir / (stem + "_" + std::to_string(suffix++) + ext);
    }
    return target;
}

// Minimal default scene file. The motor's SceneIO::loadTree can read this
// as an empty root with no children — the user fills it in by attaching
// nodes from the editor.
const char* kDefaultSceneJson5 = R"({
    "scene": {
        "name": "scene",
        "children": []
    }
}
)";

// Lua script template. The editor's Script component invokes these
// callbacks (see runtime/script_host.cpp).
const char* kDefaultScriptLua = R"(-- Auto-generated script template.
-- `self` is the obj* of this Script node; use `node.parent(self)` to walk
-- up to the entity it's attached to.

function on_init()
end

function on_update(dt)
end

function on_destroy()
end
)";

}  // namespace

void ProjectPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    if (app.projectPath().empty()) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }

    const fs::path assetsRoot = fs::path(app.projectPath()) / "assets";
    std::error_code ec;
    if (!fs::is_directory(assetsRoot, ec)) {
        ImGui::TextDisabled("(no assets/ folder in project)");
        ImGui::TextDisabled("expected: %s", assetsRoot.string().c_str());
        ImGui::End();
        return;
    }

    // First-draw init: current_dir_ = assetsRoot.
    if (current_dir_.empty()) {
        current_dir_ = assetsRoot.string();
    }

    fs::path current = fs::path(current_dir_);
    // Safeguard: if current_dir_ no longer exists / is outside
    // assets, we fall back to assetsRoot.
    if (!fs::is_directory(current, ec)) {
        current = assetsRoot;
        current_dir_ = current.string();
    }

    // ---- Top bar: Up + Breadcrumb + Import --------------------------------
    const bool atRoot = fs::equivalent(current, assetsRoot, ec);
    if (ImGui::SmallButton("..")) {
        if (!atRoot) {
            current_dir_ = current.parent_path().string();
            select_cooldown_until_ = ImGui::GetTime() + 0.25;
        }
    }
    ImGui::SameLine();

    // Breadcrumb — clickable segments from assetsRoot to current.
    {
        // Relative part: assets / sub1 / sub2 / ...
        fs::path relTail = fs::relative(current, assetsRoot.parent_path(), ec);
        if (ec) relTail = current.filename();
        std::vector<std::string> segs;
        for (auto& part : relTail) {
            std::string s = part.string();
            if (!s.empty()) segs.push_back(s);
        }
        fs::path acc = assetsRoot.parent_path();
        for (size_t i = 0; i < segs.size(); ++i) {
            acc /= segs[i];
            ImGui::SameLine();
            ImGui::PushID((int)i);
            if (ImGui::SmallButton(segs[i].c_str())) {
                current_dir_ = acc.string();
                select_cooldown_until_ = ImGui::GetTime() + 0.25;
            }
            ImGui::PopID();
            if (i + 1 < segs.size()) {
                ImGui::SameLine();
                ImGui::TextUnformatted("/");
            }
        }
    }

    // Import button on the right side.
    {
        const char* importLabel = "Import Asset...";
        float btnW = ImGui::CalcTextSize(importLabel).x +
                     ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SameLine(ImGui::GetWindowWidth() - btnW -
                        ImGui::GetStyle().WindowPadding.x);
        if (ImGui::SmallButton(importLabel)) {
            std::string src = pickFile("Import Asset", "", false);
            if (!src.empty()) {
                fs::path dst = current / fs::path(src).filename();
                std::error_code cec;
                fs::copy_file(src, dst,
                              fs::copy_options::overwrite_existing, cec);
                if (cec) {
                    app.bus().emit(kEvtLogError,
                        std::string("[Import] failed: ") + cec.message() +
                        " — " + normSlash(dst.string()));
                } else {
                    app.bus().emit(kEvtLogInfo,
                        std::string("[Import] copied → ") +
                        normSlash(dst.string()));
                }
            }
        }
    }

    // Right-click in the empty area of the listing → "New" context menu.
    // (Per-file context menus are NOT added here — we use the empty-area
    // window-context popup, which matches the Hierarchy panel's pattern.)
    if (ImGui::BeginPopupContextWindow("##projctx",
            ImGuiPopupFlags_MouseButtonRight |
            ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::BeginMenu("New")) {
            if (ImGui::MenuItem("Folder")) {
                pending_new_kind_      = NK_Folder;
                new_name_buf_[0]       = 0;
                new_name_grab_focus_   = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Material Asset (.mat.json5)")) {
                pending_new_kind_      = NK_Material;
                new_name_buf_[0]       = 0;
                new_name_grab_focus_   = true;
            }
            if (ImGui::MenuItem("Scene (.json5)")) {
                pending_new_kind_      = NK_Scene;
                new_name_buf_[0]       = 0;
                new_name_grab_focus_   = true;
            }
            if (ImGui::MenuItem("Lua Script (.lua)")) {
                pending_new_kind_      = NK_Script;
                new_name_buf_[0]       = 0;
                new_name_grab_focus_   = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    ImGui::Separator();

    // ---- Folder content listing -------------------------------------------
    // Folders first, then files — alphabetical.
    std::vector<fs::directory_entry> dirs, files;
    for (auto& e : fs::directory_iterator(current, ec)) {
        if (e.is_directory(ec)) dirs.push_back(e);
        else                    files.push_back(e);
    }
    auto cmp = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(dirs.begin(),  dirs.end(),  cmp);
    std::sort(files.begin(), files.end(), cmp);

    // Folders — double-click enters, plain click only selects.
    for (auto& d : dirs) {
        std::string name = d.path().filename().string();
        std::string full = d.path().string();
        ImGui::PushID(full.c_str());
        ImGui::Selectable((std::string("[DIR] ") + name).c_str(),
                          false, ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            current_dir_ = full;
            // Suppress the next ~250ms of asset clicks. The double-click's
            // second release event will land on the NEW listing's content
            // and would otherwise auto-select whatever asset happens to be
            // under the cursor.
            select_cooldown_until_ = ImGui::GetTime() + 0.25;
        }
        ImGui::PopID();
    }

    // Files — drag-source (M10b preserved) + double-click via registry.
    // 1-click: SelectionService.setSelectedAsset → Inspector asset-preview.
    const FileTypeRegistry& reg = FileTypeRegistry::instance();
    const std::string& selectedAsset = app.selection().selectedAsset();
    for (auto& f : files) {
        std::string name = f.path().filename().string();
        std::string full = f.path().string();
        ImGui::PushID(full.c_str());
        const bool isSel = (selectedAsset == full);
        ImGui::Selectable(name.c_str(), isSel,
                          ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemHovered()) {
            // Tooltip: full path + handler-label (if any).
            const FileTypeHandler* h = reg.handlerFor(full);
            if (h) {
                ImGui::SetTooltip("%s\n[%s — double-click to open]",
                                  full.c_str(), h->label.c_str());
            } else {
                ImGui::SetTooltip("%s", full.c_str());
            }
        }
        // Drag source (M10b preserved) — payload = full absolute path
        // (NUL-terminated).
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
            ImGui::SetDragDropPayload("ASSET_PATH", full.c_str(),
                                      full.size() + 1);
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }
        // 1-click → asset selection (Inspector preview).
        // Release-on-hover without crossing the drag threshold, so that
        // starting a drag-and-drop on the item does NOT re-select it.
        // Also gated by the dir-change cooldown so a double-click on the
        // parent directory doesn't accidentally select the asset that
        // landed under the cursor in the new listing.
        if (ImGui::IsItemHovered()
            && ImGui::IsMouseReleased(ImGuiMouseButton_Left)
            && !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left)
            && ImGui::GetTime() >= select_cooldown_until_) {
            app.selection().setSelectedAsset(full);
        }
        // Double-click → FileTypeRegistry dispatch.
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const FileTypeHandler* h = reg.handlerFor(full);
            if (h && h->action) {
                h->action(app, full);
            } else {
                app.bus().emit(kEvtLogWarn,
                    std::string("[Project] no handler for: ") + name);
            }
        }
        ImGui::PopID();
    }

    // ---- New-asset name prompt --------------------------------------------
    // Opens on the SAME frame the menu item was clicked. The popup ID
    // is fixed per kind so OpenPopup + BeginPopupModal match up.
    if (pending_new_kind_ != NK_None) {
        const char* kind_label =
            pending_new_kind_ == NK_Folder   ? "New Folder"    :
            pending_new_kind_ == NK_Material ? "New Material"  :
            pending_new_kind_ == NK_Scene    ? "New Scene"     :
            pending_new_kind_ == NK_Script   ? "New Lua Script": "New";

        if (!ImGui::IsPopupOpen("##new_asset_modal")) {
            ImGui::OpenPopup("##new_asset_modal");
        }
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing,
                                ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("##new_asset_modal", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(kind_label);
            ImGui::TextDisabled("Target: %s",
                normSlash(current.string()).c_str());
            ImGui::Dummy(ImVec2(0, 6));

            ImGui::TextUnformatted("Name:");
            ImGui::SetNextItemWidth(320.0f);
            if (new_name_grab_focus_) {
                ImGui::SetKeyboardFocusHere();
                new_name_grab_focus_ = false;
            }
            bool submit = ImGui::InputText("##newassetname",
                new_name_buf_, sizeof(new_name_buf_),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::TextDisabled("Allowed: A-Z a-z 0-9 _ - . (spaces → underscore)");

            ImGui::Dummy(ImVec2(0, 6));
            std::string sanitized = sanitizeFilename(new_name_buf_);
            const bool canCreate = !sanitized.empty();
            ImGui::BeginDisabled(!canCreate);
            const bool create_clicked =
                ImGui::Button("Create", ImVec2(96, 0)) ||
                (submit && canCreate);
            ImGui::EndDisabled();
            ImGui::SameLine();
            const bool cancel_clicked = ImGui::Button("Cancel", ImVec2(96, 0));

            if (create_clicked) {
                std::error_code cec;
                if (pending_new_kind_ == NK_Folder) {
                    fs::path target = uniqueTarget(current, sanitized, "");
                    fs::create_directories(target, cec);
                    app.bus().emit(cec ? kEvtLogError : kEvtLogInfo,
                        std::string(cec ? "[New Folder] failed: "
                                        : "[New Folder] created: ") +
                        normSlash(target.string()) +
                        (cec ? " — " + cec.message() : ""));
                } else if (pending_new_kind_ == NK_Material) {
                    // createMaterialAsset writes to <project>/assets/materials/
                    // regardless of current_dir_; matches Tools → New Material.
                    app.createMaterialAsset(sanitized);
                } else if (pending_new_kind_ == NK_Scene) {
                    // Strip a trailing .json5 if the user typed it.
                    std::string stem = sanitized;
                    auto trim_ext = [&](const char* ext) {
                        size_t n = std::strlen(ext);
                        if (stem.size() > n &&
                            stem.compare(stem.size()-n, n, ext) == 0)
                            stem.resize(stem.size() - n);
                    };
                    trim_ext(".json5");
                    fs::path target = uniqueTarget(current, stem, ".json5");
                    std::ofstream f(target);
                    if (f) f << kDefaultSceneJson5;
                    app.bus().emit(f ? kEvtLogInfo : kEvtLogError,
                        std::string(f ? "[New Scene] created: "
                                      : "[New Scene] failed: ") +
                        normSlash(target.string()));
                } else if (pending_new_kind_ == NK_Script) {
                    std::string stem = sanitized;
                    if (stem.size() > 4 &&
                        stem.compare(stem.size()-4, 4, ".lua") == 0)
                        stem.resize(stem.size() - 4);
                    fs::path target = uniqueTarget(current, stem, ".lua");
                    std::ofstream f(target);
                    if (f) f << kDefaultScriptLua;
                    app.bus().emit(f ? kEvtLogInfo : kEvtLogError,
                        std::string(f ? "[New Script] created: "
                                      : "[New Script] failed: ") +
                        normSlash(target.string()));
                }
                pending_new_kind_ = NK_None;
                new_name_buf_[0]  = 0;
                ImGui::CloseCurrentPopup();
            } else if (cancel_clicked ||
                       ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                pending_new_kind_ = NK_None;
                new_name_buf_[0]  = 0;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::End();
}

REGISTER_PANEL(ProjectPanel, 400)

}  // namespace editor
