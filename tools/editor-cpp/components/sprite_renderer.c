// SpriteRenderer — 2D sprite render-komponens. Texture-t betölti, és a
// Scene 2D panel render-walk-ja sprite_sheet-tel rajzolja.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct SpriteRenderer {
    OBJ
    COMPONENT_TRS
    char     *texture_path;
    unsigned  tint;
} SpriteRenderer;

OBJTYPEDEF(SpriteRenderer, 66);

AUTORUN {
    STRUCT_TRS(SpriteRenderer);
    STRUCT(SpriteRenderer, char*, texture_path, "[asset:texture]");
    STRUCT(SpriteRenderer, rgba,  tint);
}

obj* editor_obj_new_sprite_renderer(obj* parent, const char* name,
                                    const char* texture_path) {
    SpriteRenderer* s = obj_new_name(SpriteRenderer, name ? name : "Sprite");
    if (parent) obj_attach(parent, s);
    if (texture_path && *texture_path) {
        s->texture_path = STRDUP(texture_path);
    }
    s->scale.x = 1.0f;
    s->scale.y = 1.0f;
    s->scale.z = 1.0f;
    s->tint = 0xFFFFFFFFu;
    return (obj*)s;
}

EDITOR_COMPONENT_BASE(SpriteRenderer, sprite_renderer)

const char* editor_sprite_renderer_path(const obj* o) {
    if (!editor_obj_is_sprite_renderer(o)) return NULL;
    return ((const SpriteRenderer*)o)->texture_path;
}

void editor_sprite_renderer_get_xform(const obj* o,
                                      float out_pos[3],
                                      float* out_rot_deg,
                                      float out_scale[2],
                                      unsigned* out_tint) {
    if (!editor_obj_is_sprite_renderer(o)) return;
    const SpriteRenderer* s = (const SpriteRenderer*)o;
    if (out_pos) {
        out_pos[0] = s->pos.x;
        out_pos[1] = s->pos.y;
        out_pos[2] = s->pos.z;
    }
    if (out_rot_deg) *out_rot_deg = s->rot.z;   // 2D: csak Z-tengely
    if (out_scale) {
        out_scale[0] = s->scale.x;
        out_scale[1] = s->scale.y;
    }
    if (out_tint) *out_tint = s->tint;
}
