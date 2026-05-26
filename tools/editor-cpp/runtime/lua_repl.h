// LuaRepl — singleton interactive Lua VM for the editor's REPL panel.
//
// Lives independently from the per-Script ScriptHost VMs: the user can type
// expressions in the panel and inspect / mutate the running scene without
// touching any Script component. The VM has the SAME bindings as a regular
// editor Script (engine.ffi cdef + scene/node Lua helpers + the same engine
// `_C` namespace), so `scene.find_mesh("door").pos.y = 2.0` works exactly
// like inside a script.
//
// `print()` and uncaught errors are captured into `history()` as separate
// entries — the REPL panel renders them inline with the user's input.
//
// Lifetime:
//   - Owned by EditorApp (one LuaRepl per editor instance).
//   - The Lua state is created lazily on the first `eval()` (cheap on every
//     editor start otherwise).
//   - `lua_close` happens in the destructor.

#pragma once

#include <string>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace editor {

class EditorApp;

class LuaRepl {
public:
    explicit LuaRepl(EditorApp& app) : app_(app) {}
    ~LuaRepl();

    // History entry kind. The panel renders each kind with a different color.
    enum class EntryKind {
        kInput,    // user-typed line (echoed back as `> code`)
        kOutput,   // print() output OR pretty-printed return value
        kError,    // load/runtime error
        kInfo      // editor-side info message (e.g. "VM initialized")
    };
    struct Entry { EntryKind kind; std::string text; };

    // Evaluate one chunk. Returns true on success. The captured output (print
    // + return value tostring + any error) is appended to `history()`. Empty
    // input is a no-op. Caller does NOT pre-pend "return " — we try as a
    // statement first, then as an expression (to allow `2+2` to print 4).
    bool eval(const std::string& code);

    // Append-only event log. Used by LuaReplPanel.
    const std::vector<Entry>& history() const { return history_; }
    void clear() { history_.clear(); }

    // True if the Lua state is initialized and the engine.ffi cdef succeeded.
    bool isReady() const { return L_ != nullptr; }

private:
    // Creates the lua_State, installs the print redirect, runs engine.ffi cdef
    // + the node/scene Lua helpers. Idempotent: first call does the work, the
    // rest are no-ops. Errors during init append to history() as kError and
    // return false (the REPL stays "not ready" — eval() will retry next call).
    bool ensureInit_();

    // Custom Lua `print` that routes into history() instead of stdout/Console.
    // Upvalue 1 = LuaRepl*.
    static int luaPrint_(lua_State* L);

    void push_(EntryKind kind, std::string text);

    EditorApp& app_;
    lua_State* L_              = nullptr;
    bool       init_attempted_ = false;

    std::vector<Entry> history_;
};

}  // namespace editor
