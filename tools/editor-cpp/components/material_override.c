// MaterialOverride — per-slot override on a MeshRenderer.
//
// Lives in `MeshRenderer.material_overrides` (a v2-array(obj*)), NOT as a
// scene-hierarchy child. The Hierarchy panel never sees it; the Inspector's
// Materials section (Fázis 2.6) is the only UI to add/remove/edit them.
//
// Hybrid asset-ref + inline-overlay model:
//   - `material_asset_path` (optional) points to a `.mat.json5` asset →
//     when the render-walk applies the override, it first loads the asset's
//     material_t into the model slot.
//   - `override_mask` bits select which fields of `inline_mat` then overlay
//     the asset's values. Bit 0..7 = layer-i full override (texname + value +
//     value2 + map.color), bit 8..13 = top-level fields (cutout, ssr, etc.).
//   - `inline_mat` carries the overlay values; fields whose bit is OFF are
//     ignored.
//
// Slot-matching is by `name` (mirrors `model_t.materials[i].name`); robust
// to model-file changes that reorder/rename mesh slots.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct MaterialOverride {
    OBJ
    char*       name;                  // matches model_t.materials[i].name
    char*       material_asset_path;   // empty / NULL → pure inline override
    unsigned    override_mask;         // bitfield, see editor_material_override_*_bit
    material_t  inline_mat;            // overlay source (mask-selected fields used)
} MaterialOverride;

OBJTYPEDEF(MaterialOverride, 77);

AUTORUN {
    // Only the two char* fields are v2-reflection-registered. The mask + the
    // inline_mat are serialized by MeshRendererExtraSerializer (Fázis 2.5)
    // through the editor's __EXTRAS__ marker — the v2 obj_saveini's `s2p`
    // can't represent a `material_t` (it's a complex struct with arrays).
    STRUCT(MaterialOverride, char*, name);
    STRUCT(MaterialOverride, char*, material_asset_path, "[asset:material]");
}

obj* editor_obj_new_material_override(const char* name) {
    MaterialOverride* mo = obj_new(MaterialOverride);
    mo->name = STRDUP(name ? name : "");
    mo->material_asset_path = STRDUP("");
    mo->override_mask = 0;
    // inline_mat is zero-initialized by obj_new (CALLOC). _loaded = false
    // means the render-walk knows it shouldn't blindly memcpy garbage.
    return (obj*)mo;
}

EDITOR_COMPONENT_IS(MaterialOverride, material_override)

const char* editor_material_override_name(const obj* o) {
    if (!editor_obj_is_material_override(o)) return NULL;
    return ((const MaterialOverride*)o)->name;
}

const char* editor_material_override_asset_path(const obj* o) {
    if (!editor_obj_is_material_override(o)) return NULL;
    return ((const MaterialOverride*)o)->material_asset_path;
}

void editor_material_override_set_asset_path(obj* o, const char* path) {
    if (!editor_obj_is_material_override(o)) return;
    MaterialOverride* mo = (MaterialOverride*)o;
    if (mo->material_asset_path) FREE(mo->material_asset_path);
    mo->material_asset_path = STRDUP(path ? path : "");
}

void editor_material_override_set_name(obj* o, const char* name) {
    if (!editor_obj_is_material_override(o)) return;
    MaterialOverride* mo = (MaterialOverride*)o;
    if (mo->name) FREE(mo->name);
    mo->name = STRDUP(name ? name : "");
}

material_t* editor_material_override_inline_mat(obj* o) {
    if (!editor_obj_is_material_override(o)) return NULL;
    return &((MaterialOverride*)o)->inline_mat;
}

unsigned* editor_material_override_mask_addr(obj* o) {
    if (!editor_obj_is_material_override(o)) return NULL;
    return &((MaterialOverride*)o)->override_mask;
}
