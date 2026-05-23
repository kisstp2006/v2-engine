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
    return (obj*)l;
}

EDITOR_COMPONENT_POS_ONLY(LightRef, light_ref)

void editor_light_ref_to_light_t(const obj* o, void* out_light) {
    if (!editor_obj_is_light_ref(o) || !out_light) return;
    const LightRef* lr = (const LightRef*)o;
    light_t* dst = (light_t*)out_light;
    *dst = light();   // ctor defaults
    dst->type        = (unsigned)lr->type;
    dst->pos         = lr->pos;
    dst->dir         = lr->dir;
    dst->diffuse     = lr->color;
    dst->power       = lr->power;
    dst->innerCone   = lr->inner_cone;
    dst->outerCone   = lr->outer_cone;
    // The engine shadowmap pipeline crashes on some light-configs, so we
    // forward cast_shadows to the renderer as a HARDCODED false. The
    // component-field (Inspector-checkbox) remains for future wireup.
    dst->cast_shadows = false;
}
