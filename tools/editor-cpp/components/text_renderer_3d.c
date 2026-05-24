// Text3DRenderer — world-space billboard text using the engine's ddraw_text
// API (render_ddraw.h). Unlike the screen-space TextRenderer, this one floats
// at a 3D position (world coords), always faces the camera, and scales with
// the camera distance.
//
// Typical use: floating labels above NPCs ("Player 1"), debug joint names,
// damage numbers, in-world signposts.
//
// Note: ddraw_text doesn't support inline FONT_* markup or word-wrap — it's
// a single styled line at one color. For richer text use the 2D TextRenderer
// (HUD overlay) on the 3D Scene/Game panel instead.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

// pos/rot/scale come from COMPONENT_TRS (consistent with Mesh/Sprite/Tilemap).
// - pos:   world-space label position
// - rot:   ignored (ddraw_text always billboards toward the camera)
// - scale: ddraw_text wants a single float — we use scale.x as the size
//          multiplier. Default scale = (0.05, 0.05, 0.05).
typedef struct Text3DRenderer {
    OBJ
    COMPONENT_TRS
    char*    text;             // text to draw (no markup; ddraw_text is plain)
    unsigned color;            // 0xRRGGBBAA — tagged "rgba" for color-picker
} Text3DRenderer;

OBJTYPEDEF(Text3DRenderer, 75);

AUTORUN {
    STRUCT_TRS(Text3DRenderer);
    STRUCT(Text3DRenderer, char*, text,  "[multiline]");
    STRUCT(Text3DRenderer, rgba,  color);
}

obj* editor_obj_new_text_renderer_3d(obj* parent, const char* name,
                                     const char* text) {
    Text3DRenderer* t = obj_new_name(Text3DRenderer, name ? name : "Text3D");
    if (parent) obj_attach(parent, t);
    if (text && *text) {
        t->text = STRDUP(text);
    } else {
        t->text = STRDUP("Hello, world!");
    }
    t->scale.x = 0.05f;
    t->scale.y = 0.05f;
    t->scale.z = 0.05f;
    t->color = 0xFFFFFFFFu;   // white
    return (obj*)t;
}

EDITOR_COMPONENT_BASE(Text3DRenderer, text_renderer_3d)

void editor_text_renderer_3d_get(const obj* o,
                                 const char** out_text,
                                 float out_pos[3],
                                 float* out_scale,
                                 unsigned* out_color) {
    if (!editor_obj_is_text_renderer_3d(o)) return;
    const Text3DRenderer* t = (const Text3DRenderer*)o;
    if (out_text)  *out_text  = t->text;
    if (out_pos) { out_pos[0] = t->pos.x; out_pos[1] = t->pos.y; out_pos[2] = t->pos.z; }
    if (out_scale) *out_scale = t->scale.x;   // uniform scale assumption
    if (out_color) *out_color = t->color;
}

char** editor_text_renderer_3d_text_addr(obj* o) {
    if (!editor_obj_is_text_renderer_3d(o)) return NULL;
    return &((Text3DRenderer*)o)->text;
}
unsigned* editor_text_renderer_3d_color_addr(obj* o) {
    if (!editor_obj_is_text_renderer_3d(o)) return NULL;
    return &((Text3DRenderer*)o)->color;
}

const char* editor_text_renderer_3d_text(const obj* o) {
    if (!editor_obj_is_text_renderer_3d(o)) return NULL;
    return ((const Text3DRenderer*)o)->text;
}
