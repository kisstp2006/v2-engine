// TextRenderer — screen-space text overlay. The 3D Scene + Game render-walks
// pick up TextRenderer nodes and draw them via the engine's font_* API
// (render_font.h). Text is positioned by `pos.x/pos.y` in viewport-pixels
// (top-left = 0,0); `pos.z` is unused.
//
// Markup support: the engine's font_print accepts inline tags (FONT_FACEn,
// FONT_COLORn, FONT_Hn, FONT_LEFT/CENTER/RIGHT, ...). The component fields
// `face`/`color`/`size` are pre-pended as a prefix; the user can still embed
// per-glyph tags inside `text` for richer styling.
//
// Engine font setup (face / color allocation) happens once at editor startup
// — see editor_app.cpp font_face() calls. The renderer-side picks slots 1..N
// from there.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct TextRenderer {
    OBJ
    COMPONENT_POS              // x/y = viewport pixel position (z ignored)
    char*  text;               // text to draw (may contain FONT_* tags inline)
    int    face;               // 1..5 (FONT_FACEn). 0 = default (FACE1).
    int    color;              // 1..6 (FONT_COLORn). 0 = default (COLOR1 = white).
    int    size;               // 1..6 (FONT_Hn). 0 = default (H4 = normal).
    float  max_width;          // 0 = no wrap; > 0 = font_wrap on this width.
} TextRenderer;

OBJTYPEDEF(TextRenderer, 74);

AUTORUN {
    STRUCT_POS(TextRenderer);
    STRUCT(TextRenderer, char*, text,      "[multiline]");
    STRUCT(TextRenderer, int,   face,      "[range 0 5]");
    STRUCT(TextRenderer, int,   color,     "[range 0 6]");
    STRUCT(TextRenderer, int,   size,      "[range 0 6]");
    STRUCT(TextRenderer, float, max_width, "[range 0 4096]");
}

obj* editor_obj_new_text_renderer(obj* parent, const char* name,
                                  const char* text) {
    TextRenderer* t = obj_new_name(TextRenderer, name ? name : "Text");
    if (parent) obj_attach(parent, t);
    if (text && *text) {
        t->text = STRDUP(text);
    } else {
        t->text = STRDUP("Hello, world!");
    }
    t->pos.x = 20.0f;
    t->pos.y = 20.0f;
    t->face  = 1;    // FONT_FACE1
    t->color = 1;    // FONT_COLOR1 (white)
    t->size  = 4;    // FONT_H4 (normal)
    t->max_width = 0.f;
    return (obj*)t;
}

EDITOR_COMPONENT_POS_ONLY(TextRenderer, text_renderer)

void editor_text_renderer_get(const obj* o,
                              const char** out_text,
                              float out_pos[2],
                              int* out_face, int* out_color, int* out_size,
                              float* out_max_width) {
    if (!editor_obj_is_text_renderer(o)) return;
    const TextRenderer* t = (const TextRenderer*)o;
    if (out_text)      *out_text      = t->text;
    if (out_pos)     { out_pos[0]     = t->pos.x; out_pos[1] = t->pos.y; }
    if (out_face)      *out_face      = t->face;
    if (out_color)     *out_color     = t->color;
    if (out_size)      *out_size      = t->size;
    if (out_max_width) *out_max_width = t->max_width;
}

// Lua/mutable accessors — same pattern as node.fog_* / node.skybox_*.
char** editor_text_renderer_text_addr(obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return &((TextRenderer*)o)->text;
}
int* editor_text_renderer_face_addr(obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return &((TextRenderer*)o)->face;
}
int* editor_text_renderer_color_addr(obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return &((TextRenderer*)o)->color;
}
int* editor_text_renderer_size_addr(obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return &((TextRenderer*)o)->size;
}
float* editor_text_renderer_max_width_addr(obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return &((TextRenderer*)o)->max_width;
}

const char* editor_text_renderer_text(const obj* o) {
    if (!editor_obj_is_text_renderer(o)) return NULL;
    return ((const TextRenderer*)o)->text;
}
