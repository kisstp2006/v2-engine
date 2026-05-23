// Transform — a legalapvetőbb komponens: position + rotation + scale.
// A motor reflection rendszerébe regisztrálva STRUCT() / OBJTYPEDEF()-fel.
// MSVC C-módban (cl /Tc) fordul; a STRUCT() compound-literal-ja miatt
// nem migrálható C++-ba.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct Transform {
    OBJ
    COMPONENT_TRS
} Transform;

OBJTYPEDEF(Transform, 64);

AUTORUN {
    STRUCT_TRS(Transform);
}

obj* editor_obj_new_transform(obj* parent, const char* name) {
    Transform* t = obj_new_name(Transform, name ? name : "GameObject");
    if (parent) obj_attach(parent, t);
    t->scale.x = 1.0f;
    t->scale.y = 1.0f;
    t->scale.z = 1.0f;
    return (obj*)t;
}

/* A Transform-on a `editor_obj_is_transform` is generálódik (egyébként
   ezt eddig nem deklaráltuk explicit — most macro-konzisztencia kedvéért
   bekerül). */
EDITOR_COMPONENT_BASE(Transform, transform)
