// CameraRef — kamera-komponens. A Game panel render-walk-ja az `is_active=true`
// CameraRef-et használja a render-kameraként. Egy scene-ben egyidejűleg
// csak egy aktív CameraRef ajánlott (a Game panel az elsőt találja).

#include "engine.h"
#include "components_api.h"
#include "component_macros.h"

typedef struct CameraRef {
    OBJ
    COMPONENT_POS
    vec3   dir;          // forward irány (0,0,-1 default)
    float  fov;          // deg
    float  near_clip;
    float  far_clip;
    int    is_active;    // 0/1 (motor p2s nem tud bool-t)
} CameraRef;

OBJTYPEDEF(CameraRef, 69);

AUTORUN {
    STRUCT_POS(CameraRef);
    STRUCT(CameraRef, vec3,  dir);
    STRUCT(CameraRef, float, fov,       "[range 1 179]");
    STRUCT(CameraRef, float, near_clip);
    STRUCT(CameraRef, float, far_clip);
    STRUCT(CameraRef, int,   is_active, "[bool]");
}

obj* editor_obj_new_camera_ref(obj* parent, const char* name) {
    CameraRef* c = obj_new_name(CameraRef, name ? name : "Camera");
    if (parent) obj_attach(parent, c);
    c->pos.x = 0; c->pos.y = 2; c->pos.z = 5;     // alapból kicsit hátul-felül
    c->dir.x = 0; c->dir.y = 0; c->dir.z = -1;    // előre néz
    c->fov = 60.0f;
    c->near_clip = 0.1f;
    c->far_clip = 1000.0f;
    c->is_active = 1;        // alapból aktív (első CameraRef = MainCamera)
    return (obj*)c;
}

EDITOR_COMPONENT_POS_ONLY(CameraRef, camera_ref)

// Direction-mező közvetlen accessor (FPS-script-hez): a Lua a `dir.x/y/z`-t
// frame-enként frissítheti yaw/pitch-ből.
API vec3* editor_camera_ref_dir_addr(obj* o) {
    if (!editor_obj_is_camera_ref(o)) return NULL;
    return &((CameraRef*)o)->dir;
}

int editor_camera_ref_is_active(const obj* o) {
    if (!editor_obj_is_camera_ref(o)) return 0;
    return ((const CameraRef*)o)->is_active ? 1 : 0;
}

void editor_camera_ref_get_params(const obj* o, void* out_pos, void* out_dir,
                                  float* out_fov, float* out_near, float* out_far) {
    if (!editor_obj_is_camera_ref(o)) return;
    const CameraRef* c = (const CameraRef*)o;
    if (out_pos)  *(vec3*)out_pos  = c->pos;
    if (out_dir)  *(vec3*)out_dir  = c->dir;
    if (out_fov)  *out_fov  = c->fov;
    if (out_near) *out_near = c->near_clip;
    if (out_far)  *out_far  = c->far_clip;
}
