// LuaReplPanel — interactive Lua console for the editor.
//
// Top:    Run / Clear / Save buttons + Multi-line toggle.
// Middle: Scrollable history (input echoed as `> code`, output black,
//         errors red, info gray).
// Bottom: Single-line OR multi-line input field. Enter runs (in single-line
//         mode); Ctrl+Enter runs (in multi-line mode).
//
// Up / Down arrows in the input recall previous user-input from history.
// The panel owns a LuaRepl instance via EditorApp's lua_repl() accessor.

#pragma once

#include <string>
#include <vector>

#include "panel.h"

namespace editor {

class LuaReplPanel : public Panel {
public:
    LuaReplPanel() : Panel("lua_repl", "Lua REPL") { visible = false; }
    void draw(EditorApp& app) override;

private:
    // Buffer for the input field. ImGui InputText writes into a fixed char[]
    // buffer; we use a std::string only to copy back/forth easily for
    // arrow-history recall. 4 KB is generous for typical one-liners + small
    // multi-line snippets.
    static constexpr int kInputBufSize = 4096;
    char        input_buf_[kInputBufSize] = {};
    bool        multi_line_     = false;
    bool        focus_input_    = true;     // grab focus on next draw

    // History of previously-RUN input lines (separate from LuaRepl's output
    // log — this one is for Up/Down arrow recall). Append on Run.
    std::vector<std::string> input_history_;
    int                      history_cursor_ = -1; // -1 = not browsing

    // Auto-scroll the output region to the bottom on next draw (set when a
    // new entry was appended). The user can override by scrolling up; we
    // suppress auto-scroll until they're back at the bottom.
    bool        autoscroll_     = true;
    int         last_seen_count_ = 0;

    void runInput_(class EditorApp& app);
    void drawHistory_(class EditorApp& app);
    void drawInput_(class EditorApp& app);
};

}  // namespace editor
