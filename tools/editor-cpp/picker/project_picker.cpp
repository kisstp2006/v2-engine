#include <cstring>
#include <string>

#include "engine.h"
#include "../core/folder_picker.h"
#include "project_picker.h"

namespace editor {

namespace {

// Kis "section label" cap-és típusú felirat — szürkén, kicsit kompresszált.
void sectionLabel(const char* text) {
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 2));
}

}  // namespace

ProjectPicker::ProjectPicker()
    : pathBuf_{0}, browsePending_(false) {
    recents_.load();
    (void)browsePending_;
}

void ProjectPicker::selectPath(const std::string& p) {
    path_   = p;
    action_ = ProjectPickerAction::Open;
    done_   = true;
}

void ProjectPicker::pollBrowseResult() {
    // Win32 IFileOpenDialog modális — nincs pollozás, közvetlen visszatérés.
}

void ProjectPicker::drawHeader(float availWidth, float padX) {
    auto center = [&](const char* text, bool dim) {
        ImVec2 ts = ImGui::CalcTextSize(text);
        ImGui::SetCursorPosX((availWidth - ts.x) * 0.5f + padX);
        if (dim) ImGui::TextDisabled("%s", text);
        else     ImGui::TextUnformatted(text);
    };

    ImGui::Dummy(ImVec2(0, 8));
    center("v2 Editor", false);
    center("Project Manager", true);
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::Separator();
}

void ProjectPicker::drawRecentSection() {
    sectionLabel("RECENT PROJECTS");
    ImGui::BeginChild("##recent", ImVec2(0, 140), true);
    const auto& list = recents_.entries();
    if (list.empty()) {
        ImGui::TextDisabled("  No recent projects yet.");
    } else {
        for (size_t i = 0; i < list.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(list[i].c_str(), false,
                                  ImGuiSelectableFlags_AllowDoubleClick)) {
                if (ImGui::IsMouseDoubleClicked(0)) {
                    selectPath(list[i]);
                }
                // single click only highlights — the user double-clicks to open
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Double-click to open");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
}

void ProjectPicker::drawOpenSection() {
    sectionLabel("OPEN EXISTING");

    // Path row: InputText + Browse button.
    const float browseW = 30.0f;
    const float gap     = 4.0f;
    ImGui::PushItemWidth(-(browseW + gap));
    ImGui::InputTextWithHint("##path", "C:\\path\\to\\project", pathBuf_, sizeof(pathBuf_));
    ImGui::PopItemWidth();
    ImGui::SameLine(0, gap);
    if (ImGui::Button("...", ImVec2(browseW, 0))) {
        std::string picked = pickFolder("Open Project Folder");
        if (!picked.empty()) {
            std::strncpy(pathBuf_, picked.c_str(), sizeof(pathBuf_) - 1);
            pathBuf_[sizeof(pathBuf_) - 1] = 0;
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse for folder...");

    // Validate path, enable/disable Open button.
    const bool pathOk = pathBuf_[0] != 0 && is_folder(pathBuf_);

    ImGui::Dummy(ImVec2(0, 4));
    const float openBtnW = 96.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorPosX(avail - openBtnW + ImGui::GetStyle().WindowPadding.x);

    ImGui::BeginDisabled(!pathOk);
    if (ImGui::Button("Open", ImVec2(openBtnW, 0))) {
        selectPath(pathBuf_);
    }
    ImGui::EndDisabled();

    if (!pathOk && pathBuf_[0] != 0) {
        ImGui::TextDisabled("  Path does not exist or is not a folder.");
    }
}

void ProjectPicker::drawNewSection(float availWidth) {
    sectionLabel("NEW PROJECT");
    if (ImGui::Button("New Project...", ImVec2(availWidth, 0))) {
        newDialog_.requestOpen();
    }
}

void ProjectPicker::draw() {
    pollBrowseResult();

    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 size(560, 480);
    ImVec2 pos(vp->WorkPos.x + (vp->WorkSize.x - size.x) * 0.5f,
               vp->WorkPos.y + (vp->WorkSize.y - size.y) * 0.5f);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("Project Manager", nullptr, flags)) {
        const float padX  = ImGui::GetStyle().WindowPadding.x;
        const float avail = ImGui::GetContentRegionAvail().x;

        drawHeader(avail, padX);
        drawRecentSection();
        drawOpenSection();
        drawNewSection(avail);
    }
    ImGui::End();

    // Modal a New Project wizardhoz — a window-on KÍVÜL kell hívni.
    newDialog_.update();
    if (newDialog_.created()) {
        selectPath(newDialog_.createdPath());
        newDialog_.clearCreated();
    }
}

}  // namespace editor
