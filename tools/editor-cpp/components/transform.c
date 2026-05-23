// Transform — the most fundamental component: position + rotation + scale.
// Registered into the engine reflection system with STRUCT() / OBJTYPEDEF().
// Compiles in MSVC C-mode (cl /Tc); the compound-literal in STRUCT() means
// it can't be migrated to C++.

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

/* `editor_obj_is_transform` is also generated for Transform (we hadn't
   declared it explicitly before — now it's included for macro consistency). */
EDITOR_COMPONENT_BASE(Transform, transform)
