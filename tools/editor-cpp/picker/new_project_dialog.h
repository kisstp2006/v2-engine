#pragma once

#include <string>

namespace editor {

// Modal dialog új projekt létrehozására.
// Használat: `wantsOpen` flag-et set-elsz amikor a "New Project" gombra
// kattintanak; az `update()` minden frame fut és kezeli a modal life-cycle-t.
// Ha a felhasználó létrehozott egy projektet, `created()` true-t ad vissza
// és `createdPath()` adja a teljes mappa-path-t.
class NewProjectDialog {
public:
    void requestOpen();    // a következő `update()`-nél megnyit
    void update();         // minden frame meghívni
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
    char        nameBuf_[128]    = {};
    char        locationBuf_[1024] = {};
    std::string error_;
    std::string createdPath_;
};

}  // namespace editor
