// Script — Lua script-component (M17). Stores the path to the .lua file;
// Play mode `ScriptHost` activates each one: per-Script `lua_State*`,
// on_init / on_update(dt) / on_draw / on_quit callbacks with pcall.
// Mtime-poll auto-reload. The runtime state (`vm_handle`) lives in the
// C++ ScriptHost; here we keep an opaque pointer and do NOT serialize it.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct Script {
    OBJ
    char *script_path;
    int   auto_reload;   // 0/1 [bool]
    int   enabled;       // 0/1 [bool]
    void *vm_handle;     // opaque — NOT registered with STRUCT()
} Script;

OBJTYPEDEF(Script, 71);

AUTORUN {
    STRUCT(Script, char*, script_path, "[asset:script]");
    STRUCT(Script, int,   auto_reload, "[bool]");
    STRUCT(Script, int,   enabled,     "[bool]");
    /* vm_handle intentionally skipped: void* is not serializable,
       the runtime state lives in the ScriptHost C++-map. */
}

obj* editor_obj_new_script(obj* parent, const char* name,
                           const char* script_path) {
    Script* s = obj_new_name(Script, name ? name : "Script");
    if (parent) obj_attach(parent, s);
    if (script_path && *script_path) s->script_path = STRDUP(script_path);
    s->auto_reload = 1;   // default ON (user-decided in Phase 3)
    s->enabled    = 1;
    s->vm_handle  = NULL;
    return (obj*)s;
}

EDITOR_COMPONENT_IS(Script, script)

const char* editor_script_path(const obj* o) {
    if (!editor_obj_is_script(o)) return NULL;
    return ((const Script*)o)->script_path;
}

int editor_script_auto_reload(const obj* o) {
    if (!editor_obj_is_script(o)) return 0;
    return ((const Script*)o)->auto_reload;
}

int editor_script_enabled(const obj* o) {
    if (!editor_obj_is_script(o)) return 0;
    return ((const Script*)o)->enabled;
}

void editor_script_set_enabled(obj* o, int v) {
    if (!editor_obj_is_script(o)) return;
    ((Script*)o)->enabled = v ? 1 : 0;
}

void** editor_script_vm_handle_addr(obj* o) {
    if (!editor_obj_is_script(o)) return NULL;
    return &((Script*)o)->vm_handle;
}
