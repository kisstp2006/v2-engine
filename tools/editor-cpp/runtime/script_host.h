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

// Egy aktív Lua VM — per-Script.
struct ScriptVM {
    lua_State*  L = nullptr;
    uint64_t    last_mtime = 0;
    bool        load_ok = false;
    std::string last_error;
    std::string source_path;   // saved a hot-reloadhoz
};

// Per-EditorApp ScriptHost. A komponens-node-ot kulcsként használja egy
// `unordered_map`-ben; a VM-ek izoláltak (script_init_env(SCRIPT_LUA) → fresh
// lua_State + custom-package-loader). Hot-reload mtime-poll, hiba esetén
// Console-log + Script.enabled = 0.
class ScriptHost {
public:
    explicit ScriptHost(EditorApp& app) : app_(app) {}
    ~ScriptHost() { unloadAll(); }

    // Egy Script-node-ra: lua_State, engine.ffi cdef, file load, on_init.
    // Visszatér: success.
    bool loadScript(obj* scriptNode);

    // Lekapcsolja a script-et (lua_close + map.erase).
    void unloadScript(obj* scriptNode);

    // Lua callback hívás traceback-wrapper-rel. `dt < 0` → 0 argumentum,
    // egyébként `dt` átadása `on_update`-szerű függvénynek.
    // Visszatér: success. Hiba esetén Console-log + Script.enabled = 0.
    bool callFn(obj* scriptNode, const char* fnName, float dt);

    // Manual reload (Inspector [Reload] gomb). Auto-reload mtime-poll
    // a tickAll-ban.
    bool reloadScript(obj* scriptNode);

    // Play-mode lifecycle. Snapshot-safe: minden traverz előtt vector-be
    // gyűjti a Script-node-okat, hogy a Lua-mutáció (obj_attach/detach)
    // ne zavarja az iterációt.
    void startAll();
    void stopAll();
    void tickAll(float dt);     // on_update + mtime-poll
    void drawAll();              // on_draw (csak GamePanel-ben hívni!)

    void unloadAll();

    // Phase 6b — Inspector lekérdezés. Visszaadja a node-hoz tartozó VM
    // `last_error` mezőjét (üres string ha nincs VM vagy nincs hiba).
    std::string lastErrorOf(obj* scriptNode) const;

    // true ha a node-hoz létezik aktív VM (loadScript sikerült).
    bool hasVm(obj* scriptNode) const;

private:
    void collectScriptNodes(obj* node, std::vector<obj*>& out);
    bool bindEngineFFI(lua_State* L);  // ffi.cdef(engine.ffi)
    bool installPrintRedirect(lua_State* L); // print/io.write → Console
    static int luaPrint(lua_State* L);       // upvalue: ScriptHost*

    EditorApp&                                       app_;
    std::unordered_map<obj*, std::unique_ptr<ScriptVM>> vms_;
};

}  // namespace editor
