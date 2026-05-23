// TilemapRef — TMX tilemap render-komponens. A motor `tiled()` parser-ét
// használjuk, a tmx fájl tartalmát betöltjük `file_read`-del, és a Scene 2D
// render-walk tiled_render-rel rajzolja.

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct TilemapRef {
    OBJ
    COMPONENT_TRS
    char     *tmx_path;
} TilemapRef;

OBJTYPEDEF(TilemapRef, 67);

AUTORUN {
    STRUCT_TRS(TilemapRef);
    STRUCT(TilemapRef, char*, tmx_path, "[asset:tmx]");
}

obj* editor_obj_new_tilemap_ref(obj* parent, const char* name,
                                const char* tmx_path) {
    TilemapRef* t = obj_new_name(TilemapRef, name ? name : "Tilemap");
    if (parent) obj_attach(parent, t);
    if (tmx_path && *tmx_path) {
        t->tmx_path = STRDUP(tmx_path);
    }
    t->scale.x = 1.0f;
    t->scale.y = 1.0f;
    t->scale.z = 1.0f;
    return (obj*)t;
}

EDITOR_COMPONENT_BASE(TilemapRef, tilemap_ref)

const char* editor_tilemap_ref_path(const obj* o) {
    if (!editor_obj_is_tilemap_ref(o)) return NULL;
    return ((const TilemapRef*)o)->tmx_path;
}

void editor_tilemap_ref_get_pos(const obj* o, float out_pos[3]) {
    if (!editor_obj_is_tilemap_ref(o)) return;
    const TilemapRef* t = (const TilemapRef*)o;
    if (out_pos) {
        out_pos[0] = t->pos.x;
        out_pos[1] = t->pos.y;
        out_pos[2] = t->pos.z;
    }
}
