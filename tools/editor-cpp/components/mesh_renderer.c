// MeshRenderer — IQM modell render-komponens.
// Saját pos/rot/scale-t hordoz (Unity-szerű separation később, M16-ban).
// A tint mező típusa C-ben `unsigned`, de a STRUCT-ban "rgba" string-szel
// jelöljük — az `ui_obj` ezt color4-picker-ként rajzolja (obj.h:41).

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
    m->tint = 0xFFFFFFFFu;  /* fehér */
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
