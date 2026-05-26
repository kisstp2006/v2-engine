#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

// LuaJIT C API
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace editor {

class EditorApp;

// One active Lua VM — per-Script.
struct ScriptVM {
    lua_State*  L = nullptr;
    uint64_t    last_mtime = 0;
    bool        load_ok = false;
    std::string last_error;
    std::string source_path;   // saved for hot-reload
};

// Per-EditorApp ScriptHost. Uses the component-node as a key in an
// `unordered_map`; the VMs are isolated (script_init_env(SCRIPT_LUA) → fresh
// lua_State + custom-package-loader). Hot-reload mtime-poll, on error
// Console-log + Script.enabled = 0.
class ScriptHost {
public:
    explicit ScriptHost(EditorApp& app) : app_(app) {}
    ~ScriptHost() { unloadAll(); }

    // For a Script-node: lua_State, engine.ffi cdef, file load, on_init.
    // Returns: success.
    bool loadScript(obj* scriptNode);

    // Disables the script (lua_close + map.erase).
    void unloadScript(obj* scriptNode);

    // Lua callback call with traceback-wrapper. `dt < 0` → 0 arguments,
    // otherwise pass `dt` to an `on_update`-like function.
    // Returns: success. On error Console-log + Script.enabled = 0.
    bool callFn(obj* scriptNode, const char* fnName, float dt);

    // Manual reload (Inspector [Reload] button). Auto-reload mtime-poll
    // happens in tickAll.
    bool reloadScript(obj* scriptNode);

    // Play-mode lifecycle. Snapshot-safe: before every traversal it
    // collects the Script-nodes into a vector, so that Lua-mutation
    // (obj_attach/detach) doesn't disturb the iteration.
    void startAll();
    void stopAll();
    void tickAll(float dt);     // on_update + mtime-poll
    void drawAll();              // on_draw (only call inside GamePanel!)

    void unloadAll();

    // Phase 6b — Inspector query. Returns the `last_error` field of the
    // VM belonging to the node (empty string if there's no VM or no error).
    std::string lastErrorOf(obj* scriptNode) const;

    // true if an active VM exists for the node (loadScript succeeded).
    bool hasVm(obj* scriptNode) const;

    // Initialize a free-standing Lua state with the editor's standard
    // environment (engine.ffi cdef + node/scene Lua helpers). Does NOT
    // install a print redirect — the caller chooses where output goes.
    // Used by the LuaRepl panel to set up its persistent eval VM.
    // Returns false on hard failure (FFI cdef abort + node API both broken).
    bool initStandaloneState(lua_State* L);

private:
    void collectScriptNodes(obj* node, std::vector<obj*>& out);
    bool bindEngineFFI(lua_State* L);  // ffi.cdef(engine.ffi)
    bool installPrintRedirect(lua_State* L); // print/io.write → Console
    static int luaPrint(lua_State* L);       // upvalue: ScriptHost*

    EditorApp&                                       app_;
    std::unordered_map<obj*, std::unique_ptr<ScriptVM>> vms_;
};

}  // namespace editor
