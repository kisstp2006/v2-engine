// FogSettings — scene-wide singleton with global fog parameters.
// The 3D Scene + Game render-walks look up the first FogSettings node in the
// scene and apply its parameters to every MeshRenderer via `model_fog()`.
// Only one FogSettings per scene is expected; if multiple exist, the first
// found (depth-first, document order) wins.
//
// Fog modes (engine FOG_MODE enum, render_model.h:22):
//   0 = NONE     — fog disabled
//   1 = LINEAR   — uses start/end, distance-based
//   2 = EXP      — uses density, exponential falloff
//   3 = EXP2     — uses density, exponential-squared falloff
//   4 = DEPTH    — uses start/end, depth-buffer-based

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct FogSettings {
    OBJ
    int     mode;        // 0..4 (NONE/LINEAR/EXP/EXP2/DEPTH)
    vec3    color;       // RGB, 0..1
    float   start;       // LINEAR / DEPTH
    float   end;         // LINEAR / DEPTH
    float   density;     // EXP / EXP2
} FogSettings;

OBJTYPEDEF(FogSettings, 72);

AUTORUN {
    STRUCT(FogSettings, int,   mode,    "[range 0 4]");
    STRUCT(FogSettings, vec3,  color,   "[color3]");
    STRUCT(FogSettings, float, start,   "[range 0 1000]");
    STRUCT(FogSettings, float, end,     "[range 0 5000]");
    STRUCT(FogSettings, float, density, "[range 0 1]");
}

obj* editor_obj_new_fog_settings(obj* parent, const char* name) {
    FogSettings* f = obj_new_name(FogSettings, name ? name : "Fog");
    if (parent) obj_attach(parent, f);
    f->mode    = 1;        // default LINEAR (visible out-of-the-box)
    f->color.x = 0.7f;
    f->color.y = 0.8f;
    f->color.z = 0.9f;     // light bluish-grey
    f->start   = 50.0f;
    f->end     = 500.0f;
    f->density = 0.01f;
    return (obj*)f;
}

EDITOR_COMPONENT_IS(FogSettings, fog_settings)

void editor_fog_settings_get(const obj* o,
                             int* out_mode, void* out_color_vec3,
                             float* out_start, float* out_end, float* out_density) {
    if (!editor_obj_is_fog_settings(o)) return;
    const FogSettings* f = (const FogSettings*)o;
    if (out_mode)    *out_mode    = f->mode;
    if (out_color_vec3) *(vec3*)out_color_vec3 = f->color;
    if (out_start)   *out_start   = f->start;
    if (out_end)     *out_end     = f->end;
    if (out_density) *out_density = f->density;
}

// ---- Field-pointer accessors (mutable; for Lua node.fog_* helpers) ---------

int* editor_fog_settings_mode_addr(obj* o) {
    if (!editor_obj_is_fog_settings(o)) return NULL;
    return &((FogSettings*)o)->mode;
}
vec3* editor_fog_settings_color_addr(obj* o) {
    if (!editor_obj_is_fog_settings(o)) return NULL;
    return &((FogSettings*)o)->color;
}
float* editor_fog_settings_start_addr(obj* o) {
    if (!editor_obj_is_fog_settings(o)) return NULL;
    return &((FogSettings*)o)->start;
}
float* editor_fog_settings_end_addr(obj* o) {
    if (!editor_obj_is_fog_settings(o)) return NULL;
    return &((FogSettings*)o)->end;
}
float* editor_fog_settings_density_addr(obj* o) {
    if (!editor_obj_is_fog_settings(o)) return NULL;
    return &((FogSettings*)o)->density;
}
