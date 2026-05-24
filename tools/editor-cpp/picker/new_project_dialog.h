#pragma once

#include <string>

namespace editor {

// Modal dialog for creating a new project.
// Usage: set the `wantsOpen` flag when the "New Project" button is clicked;
// `update()` runs every frame and manages the modal life-cycle.
// If the user created a project, `created()` returns true and
// `createdPath()` gives the full folder path.
class NewProjectDialog {
public:
    void requestOpen();    // opens on the next `update()`
    void update();         // call every frame
    bool created() const { return created_; }
    void clearCreated() { created_ = false; }
    const std::string& createdPath() const { return createdPath_; }

private:
    void doCreate();
    void resetForm();

    bool        wantsOpen_  = false;
    bool        open_       = false;
    bool        created_    = false;
    int         templateIdx_ = 0;     // 0 = 3D, 1 = 2D
    bool        includeFX_   = true;  // copy bundled PostFX shaders into assets/fx/
    char        nameBuf_[128]    = {};
    char        locationBuf_[1024] = {};
    std::string error_;
    std::string createdPath_;
};

}  // namespace editor
