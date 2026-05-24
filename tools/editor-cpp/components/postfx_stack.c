// PostFXStack — scene-wide singleton with post-processing pipeline parameters.
// The 3D Scene + Game render-walks pick up the first PostFXStack node in the
// scene and apply the engine's fx_begin/end pipeline around the world-render.
// Only one PostFXStack per scene is expected; if multiple exist, the first
// found (depth-first, document order) wins.
//
// Fields:
//   - enabled : master on/off — when 0, the render-walk skips fx_begin/end
//               entirely (no GPU cost).
//   - fx_dir  : project-relative directory holding the FX shader files
//               (default "assets/fx"). The render-walk hands this to
//               fx_load(<abs_project>/<fx_dir>/fx*.glsl) on scene-load.
//
// Per-pass enabled-state + uniform-values are NOT persisted in this MVP —
// the user re-toggles them in the Inspector after scene-load. (Later:
// optional enabled_fx_csv mező + per-uniform JSON-blob.)

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct PostFXStack {
    OBJ
    int   enabled;      // 0 = master off, 1 = master on
    char* fx_dir;       // project-relative; default "assets/fx"
} PostFXStack;

OBJTYPEDEF(PostFXStack, 76);

AUTORUN {
    STRUCT(PostFXStack, int,   enabled, "[bool]");
    STRUCT(PostFXStack, char*, fx_dir);
}

obj* editor_obj_new_postfx_stack(obj* parent, const char* name) {
    PostFXStack* s = obj_new_name(PostFXStack, name ? name : "PostFX Stack");
    if (parent) obj_attach(parent, s);
    s->enabled = 1;                            // on by default
    s->fx_dir  = STRDUP("assets/fx");          // default project-relative dir
    return (obj*)s;
}

EDITOR_COMPONENT_IS(PostFXStack, postfx_stack)

void editor_postfx_stack_get(const obj* o,
                             int* out_enabled, const char** out_fx_dir) {
    if (!editor_obj_is_postfx_stack(o)) return;
    const PostFXStack* s = (const PostFXStack*)o;
    if (out_enabled) *out_enabled = s->enabled;
    if (out_fx_dir)  *out_fx_dir  = s->fx_dir;
}

// ---- Field-pointer accessors (mutable; for Lua node.postfx_* later) --------

int* editor_postfx_stack_enabled_addr(obj* o) {
    if (!editor_obj_is_postfx_stack(o)) return NULL;
    return &((PostFXStack*)o)->enabled;
}

char** editor_postfx_stack_fx_dir_addr(obj* o) {
    if (!editor_obj_is_postfx_stack(o)) return NULL;
    return &((PostFXStack*)o)->fx_dir;
}

const char* editor_postfx_stack_fx_dir(const obj* o) {
    if (!editor_obj_is_postfx_stack(o)) return NULL;
    return ((const PostFXStack*)o)->fx_dir;
}
