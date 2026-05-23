// Script — Lua szkript-komponens (M17). A .lua-fájl path-ját tárolja; a
// Play mode `ScriptHost` aktiválja: per-Script `lua_State*`, on_init /
// on_update(dt) / on_draw / on_quit callback-ek pcall-lal. Mtime-poll
// auto-reload. A runtime állapot (`vm_handle`) a C++ ScriptHost-ban él,
// itt csak opaque pointerként tároljuk és NEM serializáljuk.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct Script {
    OBJ
    char *script_path;
    int   auto_reload;   // 0/1 [bool]
    int   enabled;       // 0/1 [bool]
    void *vm_handle;     // opaque — NEM regisztrált STRUCT()-tal
} Script;

OBJTYPEDEF(Script, 71);

AUTORUN {
    STRUCT(Script, char*, script_path, "[asset:script]");
    STRUCT(Script, int,   auto_reload, "[bool]");
    STRUCT(Script, int,   enabled,     "[bool]");
    /* vm_handle szándékosan kihagyva: void* nem serializálható,
       a runtime állapot a ScriptHost C++-map-ben van. */
}

obj* editor_obj_new_script(obj* parent, const char* name,
                           const char* script_path) {
    Script* s = obj_new_name(Script, name ? name : "Script");
    if (parent) obj_attach(parent, s);
    if (script_path && *script_path) s->script_path = STRDUP(script_path);
    s->auto_reload = 1;   // default ON (felhasználói döntés Phase 3-ban)
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
