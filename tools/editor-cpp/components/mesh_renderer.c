// MeshRenderer — IQM model render-component.
// Carries its own pos/rot/scale (transform-component split later, in M16).
// The tint field is C `unsigned`, but in STRUCT we tag it with the "rgba"
// string — `ui_obj` draws it as a color4-picker (obj.h:41).

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct MeshRenderer {
    OBJ
    COMPONENT_TRS
    char     *model_path;
    int       cast_shadows;
    unsigned  tint;
} MeshRenderer;

OBJTYPEDEF(MeshRenderer, 65);

AUTORUN {
    STRUCT_TRS(MeshRenderer);
    STRUCT(MeshRenderer, char*, model_path, "[asset:model]");
    STRUCT(MeshRenderer, int,   cast_shadows, "[bool]");
    STRUCT(MeshRenderer, rgba,  tint);
}

obj* editor_obj_new_mesh_renderer(obj* parent, const char* name,
                                  const char* model_path) {
    MeshRenderer* m = obj_new_name(MeshRenderer, name ? name : "Mesh");
    if (parent) obj_attach(parent, m);
    if (model_path && *model_path) {
        m->model_path = STRDUP(model_path);
    }
    m->scale.x = 1.0f;
    m->scale.y = 1.0f;
    m->scale.z = 1.0f;
    m->cast_shadows = 1;
    m->tint = 0xFFFFFFFFu;  /* white */
    return (obj*)m;
}

EDITOR_COMPONENT_BASE(MeshRenderer, mesh_renderer)

const char* editor_mesh_renderer_path(const obj* o) {
    if (!editor_obj_is_mesh_renderer(o)) return NULL;
    return ((const MeshRenderer*)o)->model_path;
}

void editor_mesh_renderer_compose_pivot(const obj* o, mat44 out_pivot) {
    if (!editor_obj_is_mesh_renderer(o)) return;
    const MeshRenderer* m = (const MeshRenderer*)o;
    compose44(out_pivot, m->pos, eulerq(m->rot), m->scale);
}
