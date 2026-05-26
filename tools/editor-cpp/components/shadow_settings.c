// ShadowSettings — scene-wide singleton mirroring the motor's `shadowmap_t`
// global parameters. The Scene + Game render-walks look up the first
// ShadowSettings node in the scene and apply its parameters to the panel's
// `sm_` (shadowmap_t) before `shadowmap_begin`. Only one ShadowSettings per
// scene is expected; if multiple exist, the first found (DFS) wins.
//
// All defaults mirror the motor's `shadowmap()` ctor (render_light_shadowmap.h:158)
// and render_config.h compile-time defaults. Older scenes loaded without these
// fields fall back to value-init = 0; the apply function treats 0 as "keep
// motor default" so missing fields don't zero-out the shadowmap state.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct ShadowSettings {
    OBJ
    int   vsm_resolution;        // 256   — point/spot cubemap face px
    int   csm_resolution;        // 2048  — directional cascade map px
    int   max_cascades;          // 4     — clamped to NUM_SHADOW_CASCADES
    float cascade_lambda;        // 1.8   — log-split bias
    int   filter_size;           // 4     — PCF kernel side (Poisson)
    int   window_size;           // 8     — random-offset texture size
    float blend_region;          // 0.0   — cascade transition blend width
    float blend_region_offset;   // 0.2   — cascade overlap
    int   cascade_debug;         // 0/1   — color-tint per cascade
} ShadowSettings;

OBJTYPEDEF(ShadowSettings, 78);

AUTORUN {
    STRUCT(ShadowSettings, int,   vsm_resolution,      "[range 32 4096]");
    STRUCT(ShadowSettings, int,   csm_resolution,      "[range 256 8192]");
    STRUCT(ShadowSettings, int,   max_cascades,        "[range 1 4]");
    STRUCT(ShadowSettings, float, cascade_lambda,      "[range 0.1 10]");
    STRUCT(ShadowSettings, int,   filter_size,         "[range 1 16]");
    STRUCT(ShadowSettings, int,   window_size,         "[range 1 32]");
    STRUCT(ShadowSettings, float, blend_region,        "[range 0 1]");
    STRUCT(ShadowSettings, float, blend_region_offset, "[range 0 1]");
    STRUCT(ShadowSettings, int,   cascade_debug,       "[bool]");
}

obj* editor_obj_new_shadow_settings(obj* parent, const char* name) {
    ShadowSettings* s = obj_new_name(ShadowSettings, name ? name : "ShadowSettings");
    if (parent) obj_attach(parent, s);
    s->vsm_resolution      = 256;
    s->csm_resolution      = 2048;
    s->max_cascades        = 4;
    s->cascade_lambda      = 1.8f;
    s->filter_size         = 4;
    s->window_size         = 8;
    s->blend_region        = 0.0f;
    s->blend_region_offset = 0.2f;
    s->cascade_debug       = 0;
    return (obj*)s;
}

EDITOR_COMPONENT_IS(ShadowSettings, shadow_settings)

// Apply the ShadowSettings to a shadowmap_t. Treats zero values as "keep
// existing", so loading older scenes with missing fields doesn't clobber the
// motor's defaults. The C++ render-walk owns the `shadowmap_t` and calls this
// once per frame BEFORE `shadowmap_begin` (the begin() picks up the new
// vsm/csm sizes, and a filter/window change triggers an internal rebuild).
void editor_shadow_settings_apply(const obj* o, void* out_sm) {
    if (!editor_obj_is_shadow_settings(o) || !out_sm) return;
    const ShadowSettings* s = (const ShadowSettings*)o;
    shadowmap_t* sm = (shadowmap_t*)out_sm;
    if (s->vsm_resolution      > 0  ) sm->vsm_texture_width   = s->vsm_resolution;
    if (s->csm_resolution      > 0  ) sm->csm_texture_width   = s->csm_resolution;
    if (s->max_cascades        > 0  ) sm->max_cascades        = s->max_cascades;
    if (s->cascade_lambda      > 0.f) sm->cascade_lambda      = s->cascade_lambda;
    if (s->filter_size         > 0  ) sm->filter_size         = s->filter_size;
    if (s->window_size         > 0  ) sm->window_size         = s->window_size;
    if (s->blend_region        > 0.f) sm->blend_region        = s->blend_region;
    if (s->blend_region_offset > 0.f) sm->blend_region_offset = s->blend_region_offset;
    sm->cascade_debug = (s->cascade_debug != 0);
}
