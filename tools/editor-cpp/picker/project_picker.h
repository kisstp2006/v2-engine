#pragma once

#include <string>

#include "../core/recent_projects.h"
#include "new_project_dialog.h"

namespace editor {

enum class ProjectPickerAction { None, Open, New };

class ProjectPicker {
public:
    ProjectPicker();
    void draw();

    bool done() const { return done_; }
    ProjectPickerAction action() const { return action_; }
    const std::string& path() const { return path_; }

private:
    void pollBrowseResult();
    void drawHeader(float availWidth, float padX);
    void drawRecentSection();
    void drawOpenSection();
    void drawNewSection(float availWidth);

    void selectPath(const std::string& p);

    bool done_ = false;
    ProjectPickerAction action_ = ProjectPickerAction::None;
    std::string path_;

    char  pathBuf_[1024];
    bool  browsePending_;

    RecentProjects   recents_;
    NewProjectDialog newDialog_;
};

}  // namespace editor
