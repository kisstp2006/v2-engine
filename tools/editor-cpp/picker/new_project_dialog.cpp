// STL FIRST.
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "engine.h"
#include "../core/folder_picker.h"
#include "new_project_dialog.h"

namespace editor {

namespace fs = std::filesystem;

namespace {

constexpr const char* kPopupId = "New Project";

bool nonEmpty(const char* s) { return s && s[0]; }

}  // namespace

void NewProjectDialog::requestOpen() {
    wantsOpen_ = true;
}

void NewProjectDialog::resetForm() {
    nameBuf_[0] = 0;
    locationBuf_[0] = 0;
    templateIdx_ = 0;
    error_.clear();
}

void NewProjectDialog::doCreate() {
    error_.clear();

    if (!nonEmpty(nameBuf_)) {
        error_ = "Project name is required.";
        return;
    }
    if (!nonEmpty(locationBuf_)) {
        error_ = "Location is required.";
        return;
    }

    fs::path loc(locationBuf_);
    std::error_code ec;
    if (!fs::is_directory(loc, ec)) {
        error_ = "Location folder does not exist.";
        return;
    }

    fs::path projectRoot = loc / nameBuf_;
    if (fs::exists(projectRoot, ec)) {
        error_ = "A folder with that name already exists at this location.";
        return;
    }

    fs::create_directories(projectRoot / "assets" / "scenes", ec);
    if (ec) { error_ = "Failed to create project folders."; return; }
    fs::create_directories(projectRoot / "assets" / "scripts", ec);
    if (ec) { error_ = "Failed to create assets/scripts."; return; }
    fs::create_directories(projectRoot / "assets" / "prefabs", ec);
    if (ec) { error_ = "Failed to create assets/prefabs."; return; }

    const char* kind = (templateIdx_ == 0) ? "3D" : "2D";
    std::ofstream f(projectRoot / "project.json5");
    if (!f) { error_ = "Failed to write project.json5."; return; }
    f << "{\n"
      << "    name: \"" << nameBuf_ << "\",\n"
      << "    type: \"" << kind     << "\",\n"
      << "    editor_version: 1,\n"
      << "}\n";
    f.close();

    createdPath_ = projectRoot.string();
    created_     = true;
    open_        = false;
}

void NewProjectDialog::update() {
    if (wantsOpen_) {
        wantsOpen_ = false;
        resetForm();
        ImGui::OpenPopup(kPopupId);
        open_ = true;
    }

    // Center the modal.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 0));

    if (!ImGui::BeginPopupModal(kPopupId, &open_,
                                ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::TextDisabled("Set up a new v2 project folder.");
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::TextUnformatted("Name");
    ImGui::PushItemWidth(-1);
    ImGui::InputText("##name", nameBuf_, sizeof(nameBuf_));
    ImGui::PopItemWidth();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextUnformatted("Location");
    const float browseW = 30.0f;
    const float gap     = 4.0f;
    ImGui::PushItemWidth(-(browseW + gap));
    ImGui::InputTextWithHint("##loc", "Parent folder...", locationBuf_,
                             sizeof(locationBuf_));
    ImGui::PopItemWidth();
    ImGui::SameLine(0, gap);
    if (ImGui::Button("...", ImVec2(browseW, 0))) {
        std::string picked = pickFolder("Choose parent folder");
        if (!picked.empty()) {
            std::strncpy(locationBuf_, picked.c_str(), sizeof(locationBuf_) - 1);
            locationBuf_[sizeof(locationBuf_) - 1] = 0;
        }
    }

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::TextUnformatted("Template");
    ImGui::RadioButton("3D scene", &templateIdx_, 0);
    ImGui::SameLine();
    ImGui::RadioButton("2D scene", &templateIdx_, 1);

    if (!error_.empty()) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
        ImGui::TextWrapped("%s", error_.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 6));

    const float btnW = 96.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(avail - (btnW * 2 + 8.0f) +
                         ImGui::GetStyle().WindowPadding.x);

    if (ImGui::Button("Cancel", ImVec2(btnW, 0))) {
        ImGui::CloseCurrentPopup();
        open_ = false;
    }
    ImGui::SameLine(0, 8);

    const bool canCreate = nonEmpty(nameBuf_) && nonEmpty(locationBuf_);
    ImGui::BeginDisabled(!canCreate);
    if (ImGui::Button("Create", ImVec2(btnW, 0))) {
        doCreate();
        if (created_) ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
}

}  // namespace editor
