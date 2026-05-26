// LuaReplPanel — see header for design rationale.

// STL FIRST.
#include <cstring>
#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "lua_repl_panel.h"
#include "../app/editor_app.h"
#include "../app/panel_registry.h"
#include "../runtime/lua_repl.h"

namespace editor {

namespace {

// Color per history-entry kind. Picked so they read well on the editor's
// dark theme; the input echo is a subdued gray to clearly separate it from
// any output/error.
ImVec4 colorFor(LuaRepl::EntryKind k) {
    switch (k) {
        case LuaRepl::EntryKind::kInput:  return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
        case LuaRepl::EntryKind::kOutput: return ImVec4(0.92f, 0.92f, 0.92f, 1.0f);
        case LuaRepl::EntryKind::kError:  return ImVec4(1.00f, 0.45f, 0.45f, 1.0f);
        case LuaRepl::EntryKind::kInfo:   return ImVec4(0.55f, 0.78f, 1.00f, 1.0f);
    }
    return ImVec4(1, 1, 1, 1);
}

const char* prefixFor(LuaRepl::EntryKind k) {
    switch (k) {
        case LuaRepl::EntryKind::kInput:  return "> ";
        case LuaRepl::EntryKind::kOutput: return "  ";
        case LuaRepl::EntryKind::kError:  return "! ";
        case LuaRepl::EntryKind::kInfo:   return "i ";
    }
    return "  ";
}

}  // namespace

void LuaReplPanel::runInput_(EditorApp& app) {
    std::string code(input_buf_);
    // Strip trailing whitespace (Enter on multi-line leaves a trailing \n).
    while (!code.empty() &&
           (code.back() == '\n' || code.back() == '\r' || code.back() == ' '))
        code.pop_back();
    if (code.empty()) return;

    // Append to input history (no dup-against-immediately-previous entry).
    if (input_history_.empty() || input_history_.back() != code) {
        input_history_.push_back(code);
    }
    history_cursor_ = -1;

    app.luaRepl().eval(code);

    // Clear the input buffer for the next entry, refocus.
    input_buf_[0] = '\0';
    focus_input_  = true;
    autoscroll_   = true;
}

void LuaReplPanel::drawHistory_(EditorApp& app) {
    const auto& hist = app.luaRepl().history();
    // Footer reserves space for the input row below.
    const float input_h = ImGui::GetFrameHeightWithSpacing() *
                          (multi_line_ ? 4.0f : 1.0f);
    ImGui::BeginChild("##repl_history", ImVec2(0, -input_h - 4.0f),
                      true, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& e : hist) {
        ImGui::PushStyleColor(ImGuiCol_Text, colorFor(e.kind));
        // Multi-line entries (e.g. print("a\nb")) render as multiple wrapped
        // rows; ImGui's TextWrapped handles that.
        ImGui::TextUnformatted(prefixFor(e.kind));
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextWrapped("%s", e.text.c_str());
        ImGui::PopStyleColor();
    }
    // Auto-scroll only when something new came in AND the user is at the
    // bottom already (don't yank them back if they scrolled up to read).
    int now = (int)hist.size();
    if (now != last_seen_count_) {
        last_seen_count_ = now;
        if (autoscroll_ && ImGui::GetScrollMaxY() > 0.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    // User-driven re-anchor: if they've scrolled back to the bottom, resume
    // auto-scrolling for future entries.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        autoscroll_ = true;
    } else if (ImGui::IsWindowFocused()) {
        autoscroll_ = false;
    }
    ImGui::EndChild();
}

void LuaReplPanel::drawInput_(EditorApp& app) {
    if (focus_input_) {
        ImGui::SetKeyboardFocusHere();
        focus_input_ = false;
    }

    ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                ImGuiInputTextFlags_CallbackHistory |
                                ImGuiInputTextFlags_CallbackCompletion;

    // History-recall callback. Up/Down navigate `input_history_`.
    auto cb = +[](ImGuiInputTextCallbackData* data) -> int {
        auto* panel = static_cast<LuaReplPanel*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
            if (panel->input_history_.empty()) return 0;
            int prev = panel->history_cursor_;
            if (data->EventKey == ImGuiKey_UpArrow) {
                if (panel->history_cursor_ == -1)
                    panel->history_cursor_ = (int)panel->input_history_.size() - 1;
                else if (panel->history_cursor_ > 0)
                    panel->history_cursor_--;
            } else if (data->EventKey == ImGuiKey_DownArrow) {
                if (panel->history_cursor_ != -1) {
                    panel->history_cursor_++;
                    if (panel->history_cursor_ >= (int)panel->input_history_.size())
                        panel->history_cursor_ = -1;
                }
            }
            if (prev != panel->history_cursor_) {
                data->DeleteChars(0, data->BufTextLen);
                if (panel->history_cursor_ >= 0) {
                    const std::string& s =
                        panel->input_history_[panel->history_cursor_];
                    data->InsertChars(0, s.c_str(), s.c_str() + s.size());
                }
            }
        }
        return 0;
    };

    bool entered;
    if (multi_line_) {
        ImVec2 sz(-FLT_MIN, ImGui::GetFrameHeightWithSpacing() * 3.0f);
        // Multi-line text input — Enter inserts a newline. Ctrl+Enter (caught
        // outside via shortcut check) runs the chunk.
        entered = ImGui::InputTextMultiline(
            "##repl_input_ml", input_buf_, kInputBufSize, sz,
            flags, cb, this);
    } else {
        entered = ImGui::InputText(
            "##repl_input", input_buf_, kInputBufSize,
            flags, cb, this);
    }

    bool ctrl_enter = ImGui::IsItemFocused() &&
                      ImGui::IsKeyPressed(ImGuiKey_Enter, false) &&
                      (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper);

    if (entered || (multi_line_ && ctrl_enter)) {
        runInput_(app);
    }
}

void LuaReplPanel::draw(EditorApp& app) {
    if (!visible) return;
    if (!ImGui::Begin(title_.c_str(), &visible)) {
        ImGui::End();
        return;
    }

    // Toolbar row.
    if (ImGui::Button("Run")) {
        runInput_(app);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        app.luaRepl().clear();
        last_seen_count_ = 0;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Multi-line", &multi_line_);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s)", multi_line_
        ? "Ctrl+Enter to run, Enter for newline"
        : "Enter to run, Up/Down for history");

    ImGui::Separator();

    drawHistory_(app);
    drawInput_(app);

    ImGui::End();
}

REGISTER_PANEL(LuaReplPanel, 970)

}  // namespace editor
