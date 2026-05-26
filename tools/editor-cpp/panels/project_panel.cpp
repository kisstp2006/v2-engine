// STL FIRST.
#include <algorithm>
#include <filesystem>
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

    ImGui::End();
}

REGISTER_PANEL(ProjectPanel, 400)

}  // namespace editor
