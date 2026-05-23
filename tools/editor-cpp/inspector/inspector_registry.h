#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj   // engine function-like macro vs. typedef collision
#endif

namespace editor {

class EditorApp;

// Per-type Inspector drawer interface. Extended in M16 with hint-aware
// drawing (asset-picker, color, clamp) and multi-select support.
class IInspectorDrawer {
public:
    virtual ~IInspectorDrawer() = default;
    virtual void draw(obj* o) = 0;
};

// Global drawer registry. Dispatch: if a drawer is registered for the
// node's type, call it; otherwise the default `ui_obj` reflection-render.
class InspectorRegistry {
public:
    static InspectorRegistry& instance();

    void registerDrawer(const char* typeName,
                        std::unique_ptr<IInspectorDrawer> drawer);

    // Draw the component contents of a specific node.
    void drawComponents(obj* o);

    // Multi-edit (Phase 2c). Editable through the widgets of the primary
    // (targets[0]). On an edit-frame, the primary's field value is propagated
    // (via memcpy/STRDUP) to the other targets[1..n]. Only HOMOGENEOUS
    // selection (all the same obj_type) — in heterogeneous cases the caller
    // falls back to single-mode.
    void drawComponentsMulti(const std::vector<obj*>& targets);

    // Phase 6b — public wrapper around the default reflection-render (custom
    // drawers can also call it, so they can put their own UI after the
    // "default fields").
    void drawDefaults(const std::vector<obj*>& targets);

    // Phase 4a — project context for relativizing the drop-target/picker.
    // The InspectorPanel sets this at the start of every draw to `app.projectPath()`.
    void setProjectPath(const std::string& p) { projectPath_ = p; }
    const std::string& projectPath() const    { return projectPath_; }

    // Phase 6b — EditorApp* context for the drawers (e.g. ScriptDrawer needs
    // app.scriptHost() and app.bus()). The InspectorPanel sets this at the
    // start of every frame. Non-owning pointer.
    void       setApp(EditorApp* a) { app_ = a; }
    EditorApp* app() const          { return app_; }

private:
    InspectorRegistry() = default;
    std::unordered_map<std::string, std::unique_ptr<IInspectorDrawer>> drawers_;
    std::string projectPath_;
    EditorApp*  app_ = nullptr;
};

}  // namespace editor
