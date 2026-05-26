// LuaRepl — see header for design rationale.

#include "lua_repl.h"
#include "script_host.h"
#include "../app/editor_app.h"

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include <string>

namespace editor {

namespace {

// Traceback-wrapper pcall (mirrors script_host.cpp's helper). Without this,
// Lua errors come back as one-line strings — with it we get a full Lua
// stack trace including line numbers from the user's REPL input.
int luaTraceback(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        if (lua_isnoneornil(L, 1) || !luaL_callmeta(L, 1, "__tostring")
            || !lua_isstring(L, -1)) return 1;
        lua_remove(L, 1);
    }
    luaL_traceback(L, L, lua_tostring(L, 1), 1);
    return 1;
}

int pcallWithTraceback(lua_State* L, int nargs, int nresults) {
    int errfunc = lua_gettop(L) - nargs;
    lua_pushcfunction(L, luaTraceback);
    lua_insert(L, errfunc);
    int rc = lua_pcall(L, nargs, nresults, errfunc);
    lua_remove(L, errfunc);
    if (rc != 0) lua_gc(L, LUA_GCCOLLECT, 0);
    return rc;
}

// Pretty-print one value at stack index `idx` into a string. Handles:
//   - nil, boolean, number, string  → tostring()
//   - table                          → "{...}" (Phase 2 will recurse)
//   - cdata / userdata / function    → "<cdata>" / "<func>"
std::string valueToString(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    switch (t) {
        case LUA_TNIL:     return "nil";
        case LUA_TBOOLEAN: return lua_toboolean(L, idx) ? "true" : "false";
        case LUA_TNUMBER:
        case LUA_TSTRING:  return lua_tostring(L, idx) ? lua_tostring(L, idx) : "";
        case LUA_TTABLE:   return "<table>";
        case LUA_TFUNCTION:return "<function>";
        case LUA_TUSERDATA:return "<userdata>";
        case LUA_TLIGHTUSERDATA: return "<lightuserdata>";
        case 10 /* LUA_TCDATA */: {
            // Fall back to the engine's `tostring` metamethod if present.
            if (luaL_callmeta(L, idx, "__tostring") && lua_isstring(L, -1)) {
                std::string s = lua_tostring(L, -1);
                lua_pop(L, 1);
                return s;
            }
            return "<cdata>";
        }
        default: return "<unknown>";
    }
}

}  // namespace

LuaRepl::~LuaRepl() {
    if (L_) {
        lua_close(L_);
        L_ = nullptr;
    }
}

int LuaRepl::luaPrint_(lua_State* L) {
    LuaRepl* self = (LuaRepl*)lua_touserdata(L, lua_upvalueindex(1));
    if (!self) return 0;
    int n = lua_gettop(L);
    std::string msg;
    for (int i = 1; i <= n; ++i) {
        if (i > 1) msg += "\t";
        msg += valueToString(L, i);
    }
    self->push_(EntryKind::kOutput, std::move(msg));
    return 0;
}

void LuaRepl::push_(EntryKind kind, std::string text) {
    history_.push_back({kind, std::move(text)});
}

bool LuaRepl::ensureInit_() {
    if (L_) return true;
    if (init_attempted_) return false;
    init_attempted_ = true;

    // Reuse the engine's standard Lua-state ctor (same as ScriptHost). It
    // pulls in LuaJIT + the engine's built-in `script_*` bindings.
    lua_State* L = (lua_State*)script_init_env(SCRIPT_LUA);
    if (!L) {
        push_(EntryKind::kError, "REPL init failed: script_init_env returned NULL");
        return false;
    }

    // Install our custom print BEFORE asking ScriptHost to set up FFI — that
    // way any diagnostic logs from cdef-parse stops land in OUR history, not
    // bouncing through the Console.
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &LuaRepl::luaPrint_, 1);
    lua_setglobal(L, "print");
    luaL_dostring(L, "io.write = print");

    // Delegate engine.ffi cdef + node/scene Lua helpers to ScriptHost so we
    // don't duplicate the ~300 lines of EDITOR_PRELUDE. The ScriptHost still
    // emits its own diagnostic logs to the Console for cdef-parse stops
    // (those happen at the Windows network section and are expected — see
    // script_host.cpp).
    if (!app_.scriptHost().initStandaloneState(L)) {
        push_(EntryKind::kError,
              "REPL init failed: FFI cdef + node-api setup returned false");
        lua_close(L);
        return false;
    }

    // Convenience: `_C` global alias for `C` (= ffi.C). The motor's demos
    // and the kNodeApiLua module use `_C` internally, so users who
    // copy-paste demo snippets expect it. The `C` global is also kept.
    //
    // We also install a `help()` Lua function — a tiny cheat-sheet for
    // people new to the editor's Lua surface. The function lives in the
    // REPL-state's global table and prints into our captured output via
    // the `print` redirect.
    luaL_dostring(L,
        "_C = C\n"
        "function help()\n"
        "  print('--- Lua REPL cheat-sheet ---')\n"
        "  print('Engine (real C functions, NOT macros — `app_fps` is a macro):')\n"
        "  print('  _C.fps()                  -- current FPS')\n"
        "  print('  _C.app_delta()            -- frame dt (seconds)')\n"
        "  print('  _C.app_frame()            -- frame counter')\n"
        "  print('  _C.time_ss()              -- seconds since start')\n"
        "  print('Editor / scene:')\n"
        "  print('  _C.scene_root()           -- obj* of the scene root')\n"
        "  print('  scene.find_first(root, node.is_mesh)  -- DFS by predicate')\n"
        "  print('  scene.find_fog(root) / find_skybox(root) / ...')\n"
        "  print('Node helpers (see runtime/script_node_api.h):')\n"
        "  print('  node.name(o)  node.parent(o)  node.children(o)')\n"
        "  print('  node.pos(o)   node.rot(o)     node.scale(o)')\n"
        "  print('  node.is_mesh(o) / .is_light(o) / .is_camera(o) / ...')\n"
        "  print('Tip: `print(x)` and bare-expression `2+2` both work.')\n"
        "  print('Tip: `_C.app_fps()` FAILS — `app_fps` is a #define for fps().')\n"
        "  return nil\n"
        "end\n");

    L_ = L;
    push_(EntryKind::kInfo,
          "Lua REPL ready. Type `help()` for a cheat-sheet.");
    return true;
}

bool LuaRepl::eval(const std::string& code) {
    if (code.empty()) return true;
    push_(EntryKind::kInput, code);

    if (!ensureInit_()) return false;

    // Try as expression first ("return <code>"): lets the user type `2+2`
    // and see `4` without having to write `print(2+2)`. If it fails to
    // load (syntax error wrapping in return), fall back to a statement.
    std::string expr = "return " + code;
    int rc = luaL_loadstring(L_, expr.c_str());
    if (rc != 0) {
        lua_pop(L_, 1);
        rc = luaL_loadstring(L_, code.c_str());
        if (rc != 0) {
            const char* err = lua_tostring(L_, -1);
            push_(EntryKind::kError, err ? err : "(syntax error)");
            lua_pop(L_, 1);
            return false;
        }
    }

    int top_before = lua_gettop(L_) - 1;   // - the chunk we just pushed
    if (pcallWithTraceback(L_, 0, LUA_MULTRET) != 0) {
        const char* err = lua_tostring(L_, -1);
        push_(EntryKind::kError, err ? err : "(runtime error)");
        lua_pop(L_, 1);
        return false;
    }

    // Pretty-print any return values. Multiple values are tab-separated,
    // matching Lua's `print()`.
    int n = lua_gettop(L_) - top_before;
    if (n > 0) {
        std::string out;
        for (int i = 1; i <= n; ++i) {
            if (i > 1) out += "\t";
            out += valueToString(L_, top_before + i);
        }
        push_(EntryKind::kOutput, std::move(out));
        lua_pop(L_, n);
    }
    return true;
}

}  // namespace editor
