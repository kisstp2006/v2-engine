// STL FIRST (because of the engine's `obj`/`set`/etc. macro-clash).
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "script_host.h"
#include "script_node_api.h"
#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/asset_path.h"
#include "../core/event_bus.h"
#include "../core/file_io.h"
#include "../panels/console_panel.h"
#include "../scene/scene_helpers.h"
#include "../scene/scene_service.h"

namespace editor {

namespace {

// Static cache: engine.ffi content. We read it once, every VM gets the same
// thing (1MB string, ~80ms cdef on every VM).
const std::string& engineFFI() {
    static std::string content;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        // editor::file_io::readText — STL-based, CWD-relative resolve works
        // identically to motor file_read here (no Windows path edge-case in
        // play because this is a build-tree path, not a user-project path).
        content = editor::file_io::readText("code/game/embed/engine.ffi");
    }
    return content;
}

// Traceback-wrapper pcall (modeled after game_script_lua2.h:46-58).
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

uint64_t mtimeOf(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();
}

}  // namespace

int ScriptHost::luaPrint(lua_State* L) {
    // upvalue(1) = ScriptHost*
    ScriptHost* host = (ScriptHost*)lua_touserdata(L, lua_upvalueindex(1));
    int n = lua_gettop(L);
    std::string msg;
    for (int i = 1; i <= n; ++i) {
        const char* s = lua_tostring(L, i);
        if (i > 1) msg += "\t";
        msg += s ? s : "nil";
    }
    if (host) host->app_.bus().emit("log", std::string("[Lua] ") + msg);
    return 0;
}

bool ScriptHost::installPrintRedirect(lua_State* L) {
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, &ScriptHost::luaPrint, 1);
    lua_setglobal(L, "print");
    // io.write = print (single-line override; not for everything, but most common).
    luaL_dostring(L, "io.write = print");
    return true;
}

bool ScriptHost::bindEngineFFI(lua_State* L) {
    // 1) ffi = require("ffi"); C = ffi.C
    if (luaL_dostring(L, "ffi = require('ffi'); C = ffi.C") != 0) {
        const char* err = lua_tostring(L, -1);
        app_.bus().emit("log",
            std::string("[Script] ffi require failed: ") + (err ? err : "?"));
        lua_pop(L, 1);
        return false;
    }
    // 2) ffi.cdef(stripped engine.ffi content)
    const std::string& ffi = engineFFI();
    if (ffi.empty()) {
        app_.bus().emit("log",
            "[Script] engine.ffi cache empty (file_read failed?)");
        return false;
    }
    // Windows-macro types pre-prelude. The engine's engine.ffi contains
    // Windows network structs (sockaddr_in6_old, INTERFACE_INFO) that
    // reference <windows.h> typedefs — LuaJIT needs to know these in advance.
    static const char* WIN_PRELUDE =
        "typedef short SHORT;\n"
        "typedef unsigned short USHORT;\n"
        "typedef unsigned short WORD;\n"
        "typedef long LONG;\n"
        "typedef unsigned long ULONG;\n"
        "typedef unsigned long DWORD;\n"
        "typedef int BOOL;\n"
        "typedef unsigned char BYTE;\n"
        "typedef char CHAR;\n"
        "typedef void* HANDLE;\n"
        "typedef void* HWND;\n"
        "typedef void* HMODULE;\n"
        "typedef void* LPVOID;\n"
        "typedef void* PVOID;\n"
        "typedef struct in6_addr_t { unsigned char Byte[16]; } IN6_ADDR;\n";

    // Editor-specific API (exposed by tools/editor-cpp/). The engine's
    // engine.ffi does NOT contain these because they're our helpers. The
    // LuaJIT FFI resolves these from the process symbols (editor-cpp.exe).
    // `vec3` is a union in engine.ffi (engine); `obj` is an opaque typedef.
    // That's why we use typedef-style references (NOT `struct vec3*`),
    // otherwise LuaJIT gives an "attempt to redefine 'vec3'" error.
    // NOTE — duplication risk: this list MIRRORS components_api.h. Every new
    // `editor_*` helper used from Lua must be cdef-ed here (LuaJIT FFI requires
    // a typedef for any `C.fn` access — without it, `_C.fn` raises an error
    // at the LOOKUP, not at the call). Missing entries silently break the
    // node-api init chunk (`script_node_api.h::kNodeApiLua`), and everything
    // declared AFTER the bad line — including `scene = scene or {}` — never
    // runs. Auto-generation from components_api.h is a future refactor.
    static const char* EDITOR_PRELUDE =
        "/* editor helpers (tools/editor-cpp/) */\n"
        "vec3* editor_obj_pos_addr           (obj* o);\n"
        "vec3* editor_obj_rot_addr           (obj* o);\n"
        "vec3* editor_obj_scale_addr         (obj* o);\n"
        "vec3* editor_transform_pos_addr     (obj* o);\n"
        "vec3* editor_transform_rot_addr     (obj* o);\n"
        "vec3* editor_transform_scale_addr   (obj* o);\n"
        "vec3* editor_mesh_renderer_pos_addr (obj* o);\n"
        "vec3* editor_mesh_renderer_rot_addr (obj* o);\n"
        "vec3* editor_mesh_renderer_scale_addr(obj* o);\n"
        "vec3* editor_sprite_renderer_pos_addr(obj* o);\n"
        "vec3* editor_tilemap_ref_pos_addr   (obj* o);\n"
        "vec3* editor_light_ref_pos_addr     (obj* o);\n"
        "vec3* editor_camera_ref_pos_addr    (obj* o);\n"
        "vec3* editor_camera_ref_dir_addr    (obj* o);\n"
        "vec3* editor_audio_source_pos_addr  (obj* o);\n"
        "vec3* editor_text_renderer_3d_pos_addr(obj* o);\n"
        "const char*  editor_mesh_renderer_path     (const obj* o);\n"
        "const char*  editor_sprite_renderer_path   (const obj* o);\n"
        "const char*  editor_tilemap_ref_path       (const obj* o);\n"
        "const char*  editor_audio_source_path      (const obj* o);\n"
        "const char*  editor_script_path            (const obj* o);\n"
        "int          editor_obj_child_count        (const obj* parent);\n"
        "obj*         editor_obj_child_at           (const obj* parent, int i);\n"
        "int          editor_obj_is_2d_component    (const obj* o);\n"
        "int          editor_obj_is_3d_component    (const obj* o);\n"
        "int          editor_obj_is_mesh_renderer   (const obj* o);\n"
        "int          editor_obj_is_sprite_renderer (const obj* o);\n"
        "int          editor_obj_is_tilemap_ref     (const obj* o);\n"
        "int          editor_obj_is_light_ref       (const obj* o);\n"
        "int          editor_obj_is_camera_ref      (const obj* o);\n"
        "int          editor_obj_is_audio_source    (const obj* o);\n"
        "int          editor_obj_is_script          (const obj* o);\n"
        "int          editor_script_enabled         (const obj* o);\n"
        "void         editor_script_set_enabled     (obj* o, int v);\n"
        /* ---- Fog / Skybox / Text / Text3D / PostFX (script_node_api.h) ---- */
        "int          editor_obj_is_fog_settings    (const obj* o);\n"
        "int          editor_obj_is_skybox          (const obj* o);\n"
        "int          editor_obj_is_text_renderer   (const obj* o);\n"
        "int          editor_obj_is_text_renderer_3d(const obj* o);\n"
        "int          editor_obj_is_postfx_stack    (const obj* o);\n"
        "int*         editor_fog_settings_mode_addr   (obj* o);\n"
        "vec3*        editor_fog_settings_color_addr  (obj* o);\n"
        "float*       editor_fog_settings_start_addr  (obj* o);\n"
        "float*       editor_fog_settings_end_addr    (obj* o);\n"
        "float*       editor_fog_settings_density_addr(obj* o);\n"
        "const char*  editor_skybox_sky_path        (const obj* o);\n"
        "const char*  editor_skybox_refl_path       (const obj* o);\n"
        "const char*  editor_skybox_env_path        (const obj* o);\n"
        "int*         editor_skybox_render_bg_addr  (obj* o);\n"
        "const char*  editor_text_renderer_text     (const obj* o);\n"
        "int*         editor_text_renderer_face_addr     (obj* o);\n"
        "int*         editor_text_renderer_color_addr    (obj* o);\n"
        "int*         editor_text_renderer_size_addr     (obj* o);\n"
        "float*       editor_text_renderer_max_width_addr(obj* o);\n"
        "const char*    editor_text_renderer_3d_text    (const obj* o);\n"
        "unsigned*      editor_text_renderer_3d_color_addr(obj* o);\n"
        "int*         editor_postfx_stack_enabled_addr(obj* o);\n"
        "const char*  editor_postfx_stack_fx_dir     (const obj* o);\n"
        /* ---- MaterialOverride + MeshRenderer overrides array (Blokk 2.4) ---- */
        "int          editor_obj_is_material_override     (const obj* o);\n"
        "obj*         editor_obj_new_material_override    (const char* name);\n"
        "const char*  editor_material_override_name       (const obj* o);\n"
        "const char*  editor_material_override_asset_path (const obj* o);\n"
        "void         editor_material_override_set_name      (obj* o, const char* name);\n"
        "void         editor_material_override_set_asset_path(obj* o, const char* path);\n"
        "material_t*  editor_material_override_inline_mat (obj* o);\n"
        "unsigned*    editor_material_override_mask_addr  (obj* o);\n"
        "int          editor_mesh_renderer_overrides_count(const obj* o);\n"
        "obj*         editor_mesh_renderer_override_at    (const obj* o, int i);\n"
        "void         editor_mesh_renderer_add_override   (obj* o, obj* mo);\n"
        "void         editor_mesh_renderer_remove_override(obj* o, int i);\n"
        "void         editor_mesh_renderer_clear_overrides(obj* o);\n"
        "obj*         editor_mesh_renderer_find_override_by_name(const obj* o, const char* name);\n";

    // "API " token strip (modeled after luaj_bind).
    std::string clean(WIN_PRELUDE);
    clean += ffi;
    // We process EDITOR_PRELUDE in a SEPARATE cdef call, so that an engine.ffi
    // syntax error doesn't swallow our own declarations.
    for (size_t pos = 0; (pos = clean.find("API ", pos)) != std::string::npos;) {
        clean[pos]='/'; clean[pos+1]='*'; clean[pos+2]='*'; clean[pos+3]='/';
        pos += 4;
    }
    // Line 10679+ of the engine's engine.ffi contains Windows network inlines
    // (sockaddr_*, IN6_IS_ADDR_*) that are not compatible with LuaJIT cdef
    // and are anyway irrelevant to the script API. We cut them off near the
    // end of the file.
    size_t cut = clean.find("struct sockaddr_in6_old");
    if (cut != std::string::npos) {
        clean.resize(cut);
        clean += "\n/* truncated at Windows network section */\n";
    }
    // 1) ffi.cdef(engine.ffi) — modeled after the engine's luaj_init: if it
    // finds an error, the parser aborts, but the declarations processed up
    // to that point are retained (the demos work the same way). We only log
    // errors, we don't stop.
    lua_getglobal(L, "ffi");
    lua_getfield(L, -1, "cdef");
    lua_pushlstring(L, clean.c_str(), clean.size());
    int rc = pcallWithTraceback(L, 1, 0);
    std::string errMsg;
    if (rc != 0) {
        const char* err = lua_tostring(L, -1);
        errMsg = err ? err : "(no message)";
        lua_pop(L, 1);
    }
    lua_pop(L, 1);   // pop ffi
    if (rc != 0) {
        // ONLY info-log — it's normal in the engine's engine.ffi to stop
        // at the Windows network section. The demos (hello.lua) work this way too.
        app_.bus().emit("log",
            std::string("[Script] engine.ffi cdef stopped at: ") + errMsg);
    }

    // 2) Editor-specific helpers in a SEPARATE cdef call — because if
    // engine.ffi aborted, declarations appended after it would be lost.
    // Separate call = fresh parser, our own declarations are guaranteed to land.
    lua_getglobal(L, "ffi");
    lua_getfield(L, -1, "cdef");
    lua_pushstring(L, EDITOR_PRELUDE);
    int rc2 = pcallWithTraceback(L, 1, 0);
    std::string err2;
    if (rc2 != 0) {
        const char* err = lua_tostring(L, -1);
        err2 = err ? err : "(no message)";
        lua_pop(L, 1);
    }
    lua_pop(L, 1);   // pop ffi
    if (rc2 != 0) {
        app_.bus().emit("log",
            std::string("[Script] editor cdef failed: ") + err2);
        return false;
    }
    return true;
}

bool ScriptHost::loadScript(obj* scriptNode) {
    if (!scriptNode || !editor_obj_is_script(scriptNode)) return false;
    const char* relPath = editor_script_path(scriptNode);
    if (!relPath || !*relPath) return false;

    // Phase 4a — abs-resolve. The VM's source_path is abs (the hot-reload
    // mtime-poll works on abs).
    std::string absPath = asset_path::toAbsolute(relPath, app_.projectPath());
    const char* path = absPath.c_str();
    if (!is_file(path)) {
        app_.bus().emit("log",
            std::string("[Script] file not found: ") + relPath);
        return false;
    }

    // Discard the old VM, if there was one.
    unloadScript(scriptNode);

    auto vm = std::make_unique<ScriptVM>();
    vm->source_path = absPath;
    vm->last_mtime  = mtimeOf(path);
    vm->L = (lua_State*)script_init_env(SCRIPT_LUA);
    if (!vm->L) {
        app_.bus().emit("log",
            std::string("[Script] script_init_env failed: ") + path);
        return false;
    }

    // print/error redirect to the Console
    installPrintRedirect(vm->L);

    // engine.ffi cdef + C namespace
    if (!bindEngineFFI(vm->L)) {
        lua_close(vm->L);
        return false;
    }

    // Pre-load `node` helper module (script_node_api.h) — nil-safe shortcuts
    // around the engine's editor_obj_* / obj_* APIs. We silently skip if
    // there's an error (the script can still run with the pure `C.*` API).
    if (luaL_loadstring(vm->L, kNodeApiLua) == 0) {
        if (pcallWithTraceback(vm->L, 0, 0) != 0) {
            const char* err = lua_tostring(vm->L, -1);
            app_.bus().emit("log",
                std::string("[Script] node-api load failed: ") +
                (err ? err : "?"));
            lua_pop(vm->L, 1);
        }
    } else {
        lua_pop(vm->L, 1);
    }

    // self = (struct obj*)scriptNode (lightuserdata, the script casts it to
    // a struct obj* pointer using ffi.cast).
    lua_pushlightuserdata(vm->L, scriptNode);
    lua_setglobal(vm->L, "self");

    // chunk load + run (defines the global on_init/on_update/etc.)
    int rc = luaL_loadfile(vm->L, path);
    if (rc != 0) {
        const char* err = lua_tostring(vm->L, -1);
        vm->last_error = err ? err : "load failed";
        app_.bus().emit("log",
            std::string("[Script] load failed (") + path + "): " + vm->last_error);
        lua_close(vm->L);
        return false;
    }
    if (pcallWithTraceback(vm->L, 0, 0) != 0) {
        const char* err = lua_tostring(vm->L, -1);
        vm->last_error = err ? err : "chunk error";
        app_.bus().emit("log",
            std::string("[Script] chunk error (") + path + "): " + vm->last_error);
        lua_pop(vm->L, 1);
        lua_close(vm->L);
        return false;
    }

    vm->load_ok = true;
    void** slot = editor_script_vm_handle_addr(scriptNode);
    if (slot) *slot = vm->L;     // opaque marker that "it's alive"
    vms_[scriptNode] = std::move(vm);
    app_.bus().emit("log", std::string("[Script] loaded: ") + path);
    return true;
}

bool ScriptHost::initStandaloneState(lua_State* L) {
    if (!L) return false;
    // 1) engine.ffi cdef + C namespace. bindEngineFFI uses app_.bus().emit for
    //    diagnostic logs; setup-time issues land in the Console (acceptable —
    //    the user can still investigate from there).
    if (!bindEngineFFI(L)) return false;
    // 2) `node` + `scene` helper module. Silently swallow load errors (the
    //    state remains usable with raw `_C.*` calls).
    if (luaL_loadstring(L, kNodeApiLua) == 0) {
        if (pcallWithTraceback(L, 0, 0) != 0) {
            const char* err = lua_tostring(L, -1);
            app_.bus().emit("log",
                std::string("[REPL] node-api load failed: ") +
                (err ? err : "?"));
            lua_pop(L, 1);
        }
    } else {
        lua_pop(L, 1);
    }
    return true;
}

void ScriptHost::unloadScript(obj* scriptNode) {
    auto it = vms_.find(scriptNode);
    if (it == vms_.end()) return;
    if (it->second->L) lua_close(it->second->L);
    void** slot = editor_script_vm_handle_addr(scriptNode);
    if (slot) *slot = nullptr;
    vms_.erase(it);
}

bool ScriptHost::callFn(obj* scriptNode, const char* fnName, float dt) {
    auto it = vms_.find(scriptNode);
    if (it == vms_.end() || !it->second->L || !it->second->load_ok) return false;
    lua_State* L = it->second->L;

    lua_getglobal(L, fnName);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return true;    // no such callback — OK
    }

    int narg = 0;
    if (dt >= 0.0f) {
        lua_pushnumber(L, dt);
        narg = 1;
    }
    if (pcallWithTraceback(L, narg, 0) != 0) {
        const char* err = lua_tostring(L, -1);
        it->second->last_error = err ? err : "pcall error";
        app_.bus().emit("log",
            std::string("[Script] ") + fnName + " error: " + it->second->last_error);
        lua_pop(L, 1);
        editor_script_set_enabled(scriptNode, 0);
        return false;
    }
    return true;
}

bool ScriptHost::reloadScript(obj* scriptNode) {
    bool ok = loadScript(scriptNode);
    if (ok) callFn(scriptNode, "on_init", -1.0f);
    return ok;
}

// Phase 6b — Inspector helpers.
std::string ScriptHost::lastErrorOf(obj* scriptNode) const {
    auto it = vms_.find(scriptNode);
    if (it == vms_.end() || !it->second) return {};
    return it->second->last_error;
}

bool ScriptHost::hasVm(obj* scriptNode) const {
    auto it = vms_.find(scriptNode);
    return it != vms_.end() && it->second && it->second->L;
}

void ScriptHost::collectScriptNodes(obj* node, std::vector<obj*>& out) {
    if (!node) return;
    if (editor_obj_is_script(node)) out.push_back(node);
    int n = editor_obj_child_count(node);
    for (int i = 0; i < n; ++i) {
        collectScriptNodes(editor_obj_child_at(node, i), out);
    }
}

void ScriptHost::startAll() {
    std::vector<obj*> nodes;
    collectScriptNodes(app_.scene().root(), nodes);
    for (obj* n : nodes) {
        if (!editor_script_enabled(n)) continue;
        if (loadScript(n)) callFn(n, "on_init", -1.0f);
    }
}

void ScriptHost::stopAll() {
    // order: 1) on_quit, 2) lua_close on every VM (unloadAll does this too).
    std::vector<obj*> nodes;
    nodes.reserve(vms_.size());
    for (auto& kv : vms_) nodes.push_back(kv.first);
    for (obj* n : nodes) callFn(n, "on_quit", -1.0f);
    unloadAll();
}

void ScriptHost::tickAll(float dt) {
    // mutation-safety: snapshot
    std::vector<obj*> nodes;
    nodes.reserve(vms_.size());
    for (auto& kv : vms_) nodes.push_back(kv.first);
    for (obj* n : nodes) {
        if (!editor_script_enabled(n)) continue;
        // mtime-poll (auto-reload)
        if (editor_script_auto_reload(n)) {
            auto it = vms_.find(n);
            if (it != vms_.end()) {
                uint64_t m = mtimeOf(it->second->source_path);
                if (m != 0 && m != it->second->last_mtime) {
                    app_.bus().emit("log",
                        std::string("[Script] reloaded: ") + it->second->source_path);
                    reloadScript(n);
                }
            }
        }
        callFn(n, "on_update", dt);
    }
}

void ScriptHost::drawAll() {
    std::vector<obj*> nodes;
    nodes.reserve(vms_.size());
    for (auto& kv : vms_) nodes.push_back(kv.first);
    for (obj* n : nodes) {
        if (!editor_script_enabled(n)) continue;
        callFn(n, "on_draw", -1.0f);
    }
}

void ScriptHost::unloadAll() {
    for (auto& kv : vms_) {
        if (kv.second && kv.second->L) lua_close(kv.second->L);
        void** slot = editor_script_vm_handle_addr(kv.first);
        if (slot) *slot = nullptr;
    }
    vms_.clear();
}

}  // namespace editor
