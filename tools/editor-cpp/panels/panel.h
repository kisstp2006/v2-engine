#pragma once

#include <string>

namespace editor {

class EditorApp;

// Abstract base class for editor panels.
// Sub-classes implement `draw(app)`, ami a panel ImGui-tartalmát rajzolja.
class Panel {
public:
    Panel(std::string id, std::string title)
        : id_(std::move(id)), title_(std::move(title)) {}
    virtual ~Panel() = default;

    virtual void draw(EditorApp& app) = 0;

    const std::string& id() const { return id_; }
    const std::string& title() const { return title_; }

    bool visible = true;

protected:
    std::string id_;
    std::string title_;
};

}  // namespace editor
