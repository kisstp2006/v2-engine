// Skybox — scene-wide singleton with environment-map paths.
// The 3D Scene + Game render-walks pick up the first Skybox node in the scene
// and apply skybox_render() (background) + model_skybox() (PBR IBL) on every
// MeshRenderer.
//
// Paths (one or three HDR files, depending on the asset):
//   - sky_path  : main panorama / cubemap (required if non-empty)
//   - refl_path : reflection map (optional; falls back to sky_path)
//   - env_path  : environment irradiance map (optional; falls back to sky_path)
//
// `render_background` toggles the visible sky behind the scene; turn it off to
// keep IBL lighting but render a solid background colour instead.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct Skybox {
    OBJ
    char* sky_path;
    char* refl_path;
    char* env_path;
    int   render_background;
} Skybox;

OBJTYPEDEF(Skybox, 73);

AUTORUN {
    STRUCT(Skybox, char*, sky_path,         "[asset:texture]");
    STRUCT(Skybox, char*, refl_path,        "[asset:texture]");
    STRUCT(Skybox, char*, env_path,         "[asset:texture]");
    STRUCT(Skybox, int,   render_background, "[bool]");
}

obj* editor_obj_new_skybox(obj* parent, const char* name, const char* sky_path) {
    Skybox* s = obj_new_name(Skybox, name ? name : "Skybox");
    if (parent) obj_attach(parent, s);
    if (sky_path && *sky_path) {
        s->sky_path = STRDUP(sky_path);
    }
    s->render_background = 1;
    return (obj*)s;
}

EDITOR_COMPONENT_IS(Skybox, skybox)

void editor_skybox_get(const obj* o,
                       const char** out_sky, const char** out_refl,
                       const char** out_env, int* out_render_bg) {
    if (!editor_obj_is_skybox(o)) return;
    const Skybox* s = (const Skybox*)o;
    if (out_sky)       *out_sky  = s->sky_path;
    if (out_refl)      *out_refl = s->refl_path;
    if (out_env)       *out_env  = s->env_path;
    if (out_render_bg) *out_render_bg = s->render_background;
}

char** editor_skybox_sky_path_addr(obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return &((Skybox*)o)->sky_path;
}
char** editor_skybox_refl_path_addr(obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return &((Skybox*)o)->refl_path;
}
char** editor_skybox_env_path_addr(obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return &((Skybox*)o)->env_path;
}
int* editor_skybox_render_bg_addr(obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return &((Skybox*)o)->render_background;
}

const char* editor_skybox_sky_path(const obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return ((const Skybox*)o)->sky_path;
}
const char* editor_skybox_refl_path(const obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return ((const Skybox*)o)->refl_path;
}
const char* editor_skybox_env_path(const obj* o) {
    if (!editor_obj_is_skybox(o)) return NULL;
    return ((const Skybox*)o)->env_path;
}
