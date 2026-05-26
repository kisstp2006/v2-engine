// LightRef — light-component. The Scene render-walk collects LightRef nodes
// every frame, converts each to a light_t, and passes them with `model_light()`
// into every MeshRenderer render-call.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct LightRef {
    OBJ
    int      type;          // 0=DIRECTIONAL, 1=POINT, 2=SPOT
    COMPONENT_POS
    vec3     dir;
    vec3     color;         // diffuse 0..1
    float    power;
    float    inner_cone;    // SPOT only
    float    outer_cone;    // SPOT only
    int      cast_shadows;  // 0/1 (engine p2s has no bool)

    // ---- Shadow tuning (forwarded to the motor when cast_shadows=1). ----
    // All defaults mirror the motor's `light_ctor` (render_light.h:54). Older
    // scenes loaded without these fields fall back to value-init = 0; the
    // editor_light_ref_to_light_t starts with `*dst = light()` (motor ctor)
    // so a zero field there means "use the motor default", which we then
    // overwrite ONLY when the LightRef has a non-zero value. See the
    // forward-or-default macro in editor_light_ref_to_light_t below.
    float    shadow_distance;     // far-plane / cascade extent. Default 400
    float    shadow_near_clip;    // Default 0.01
    float    shadow_bias;         // CSM (directional) only. Default 0.002
    float    normal_bias;         // CSM only. Default 0.007
    float    shadow_softness;     // Default 1.5
    float    penumbra_size;       // Default 2.0
    float    min_variance;        // VSM (point/spot) only. Default 0.00002
    float    variance_transition; // VSM only. Default 0.2
    int      hard_shadows;        // 0/1. Default 0
} LightRef;

OBJTYPEDEF(LightRef, 68);

AUTORUN {
    STRUCT(LightRef, int,   type,       "[range 0 2]");
    STRUCT_POS(LightRef);
    STRUCT(LightRef, vec3,  dir);
    STRUCT(LightRef, vec3,  color,      "[color3]");
    STRUCT(LightRef, float, power,      "[range 0 2000]");
    STRUCT(LightRef, float, inner_cone, "[range 0 1]");
    STRUCT(LightRef, float, outer_cone, "[range 0 1]");
    STRUCT(LightRef, int,   cast_shadows, "[bool]");

    // Shadow tuning — the registry ranges roughly mirror the motor's
    // sensible bounds; the user can still type any value in the Inspector.
    STRUCT(LightRef, float, shadow_distance,     "[range 0 5000]");
    STRUCT(LightRef, float, shadow_near_clip,    "[range 0.001 10]");
    STRUCT(LightRef, float, shadow_bias,         "[range 0 0.05]");
    STRUCT(LightRef, float, normal_bias,         "[range 0 0.1]");
    STRUCT(LightRef, float, shadow_softness,     "[range 0 10]");
    STRUCT(LightRef, float, penumbra_size,       "[range 0 10]");
    STRUCT(LightRef, float, min_variance,        "[range 0 0.01]");
    STRUCT(LightRef, float, variance_transition, "[range 0 1]");
    STRUCT(LightRef, int,   hard_shadows,        "[bool]");
}

obj* editor_obj_new_light_ref(obj* parent, const char* name, int type) {
    LightRef* l = obj_new_name(LightRef, name ? name : "Light");
    if (parent) obj_attach(parent, l);
    l->type = type;
    l->color.x = 1.0f;
    l->color.y = 1.0f;
    l->color.z = 1.0f;
    l->power = 250.0f;
    l->inner_cone = 0.85f;
    l->outer_cone = 0.9f;
    l->cast_shadows = 0;       // default OFF — toggle on in the Inspector to experiment
    /* Default dir for every type (the directional one actually uses it). */
    l->dir.x =  1.0f;
    l->dir.y = -1.0f;
    l->dir.z = -1.0f;
    /* Shadow-tuning defaults = motor `light_ctor` (render_light.h:54-72). */
    l->shadow_distance     = 400.0f;
    l->shadow_near_clip    = 0.01f;
    l->shadow_bias         = 0.002f;
    l->normal_bias         = 0.007f;
    l->shadow_softness     = 1.5f;
    l->penumbra_size       = 2.0f;
    l->min_variance        = 0.00002f;
    l->variance_transition = 0.2f;
    l->hard_shadows        = 0;
    return (obj*)l;
}

EDITOR_COMPONENT_POS_ONLY(LightRef, light_ref)

void editor_light_ref_to_light_t(const obj* o, void* out_light) {
    if (!editor_obj_is_light_ref(o) || !out_light) return;
    const LightRef* lr = (const LightRef*)o;
    light_t* dst = (light_t*)out_light;
    *dst = light();   // ctor defaults (motor)
    dst->type        = (unsigned)lr->type;
    dst->pos         = lr->pos;
    dst->dir         = lr->dir;
    dst->diffuse     = lr->color;
    dst->power       = lr->power;
    dst->innerCone   = lr->inner_cone;
    dst->outerCone   = lr->outer_cone;
    // Forward the LightRef's cast_shadows checkbox to the motor. The Scene/Game
    // panels run the shadowmap_begin/step/light/end loop iff at least one light
    // in the collected list has cast_shadows=1.
    dst->cast_shadows = (lr->cast_shadows != 0);

    // Shadow-tuning fields. Forward only when the LightRef value is non-zero
    // (treat 0 as "use motor default that already lives in `dst` from
    // `light()` above"). Scenes saved before these fields existed will load
    // with zeros → motor defaults preserved automatically.
    if (lr->shadow_distance      > 0.0f) dst->shadow_distance     = lr->shadow_distance;
    if (lr->shadow_near_clip     > 0.0f) dst->shadow_near_clip    = lr->shadow_near_clip;
    if (lr->shadow_bias          > 0.0f) dst->shadow_bias         = lr->shadow_bias;
    if (lr->normal_bias          > 0.0f) dst->normal_bias         = lr->normal_bias;
    if (lr->shadow_softness      > 0.0f) dst->shadow_softness     = lr->shadow_softness;
    if (lr->penumbra_size        > 0.0f) dst->penumbra_size       = lr->penumbra_size;
    if (lr->min_variance         > 0.0f) dst->min_variance        = lr->min_variance;
    if (lr->variance_transition  > 0.0f) dst->variance_transition = lr->variance_transition;
    dst->hard_shadows = (lr->hard_shadows != 0);
}
