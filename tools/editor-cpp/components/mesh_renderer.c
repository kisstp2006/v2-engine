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
    // Runtime array of MaterialOverride* (Blokk 2.4). NOT registered with
    // v2-reflection (array(obj*) isn't supported by obj_saveini); persisted
    // by MeshRendererExtraSerializer via the editor __EXTRAS__ marker.
    // These are NOT scene-children — they live exclusively in this array
    // and never appear in the Hierarchy panel.
    array(obj*) material_overrides;
} MeshRenderer;

OBJTYPEDEF(MeshRenderer, 65);

AUTORUN {
    STRUCT_TRS(MeshRenderer);
    STRUCT(MeshRenderer, char*, model_path, "[asset:model]");
    STRUCT(MeshRenderer, int,   cast_shadows, "[bool]");
    STRUCT(MeshRenderer, rgba,  tint);
    // material_overrides intentionally NOT registered here — serializer
    // handles it.
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

// ---- MaterialOverride array API ---------------------------------------------
// MaterialOverride*-pointers live in the v2 `array(obj*)`. Lifetime: this
// component owns them — clear_overrides FREEs the underlying obj memory.
// On scene-clear / DeleteNodeCommand, the caller must call clear_overrides
// before freeing the MeshRenderer to avoid leaks. (No v2 obj-destructor
// hook is wired in this codebase yet — see Blokk 2 risk-mátrix #4.)

int editor_mesh_renderer_overrides_count(const obj* o) {
    if (!editor_obj_is_mesh_renderer(o)) return 0;
    return array_count(((const MeshRenderer*)o)->material_overrides);
}

obj* editor_mesh_renderer_override_at(const obj* o, int i) {
    if (!editor_obj_is_mesh_renderer(o)) return NULL;
    const MeshRenderer* m = (const MeshRenderer*)o;
    if (i < 0 || i >= array_count(m->material_overrides)) return NULL;
    return m->material_overrides[i];
}

void editor_mesh_renderer_add_override(obj* o, obj* mo) {
    if (!editor_obj_is_mesh_renderer(o) || !mo) return;
    array_push(((MeshRenderer*)o)->material_overrides, mo);
}

void editor_mesh_renderer_remove_override(obj* o, int i) {
    if (!editor_obj_is_mesh_renderer(o)) return;
    MeshRenderer* m = (MeshRenderer*)o;
    int n = array_count(m->material_overrides);
    if (i < 0 || i >= n) return;
    // FREE the obj memory we created in editor_obj_new_material_override.
    // (The MaterialOverride owns its STRDUP'd `name`/`material_asset_path`
    // strings — those leak in MVP; a later pass should add a proper dtor.)
    obj* dead = m->material_overrides[i];
    for (int j = i; j < n - 1; ++j) {
        m->material_overrides[j] = m->material_overrides[j + 1];
    }
    array_pop(m->material_overrides);
    if (dead) FREE(dead);
}

void editor_mesh_renderer_clear_overrides(obj* o) {
    if (!editor_obj_is_mesh_renderer(o)) return;
    MeshRenderer* m = (MeshRenderer*)o;
    int n = array_count(m->material_overrides);
    for (int i = 0; i < n; ++i) {
        if (m->material_overrides[i]) FREE(m->material_overrides[i]);
    }
    array_resize(m->material_overrides, 0);
}

obj* editor_mesh_renderer_find_override_by_name(const obj* o, const char* name) {
    if (!editor_obj_is_mesh_renderer(o) || !name) return NULL;
    const MeshRenderer* m = (const MeshRenderer*)o;
    int n = array_count(m->material_overrides);
    for (int i = 0; i < n; ++i) {
        obj* mo = m->material_overrides[i];
        const char* mn = editor_material_override_name(mo);
        if (mn && !strcmp(mn, name)) return mo;
    }
    return NULL;
}
