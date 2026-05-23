#pragma once

// Extern-C deklarációk a built-in komponens-helper függvényekhez.
// A komponensek maguk C99 fájlokban élnek (lásd transform.c, etc.) — a
// `STRUCT(...)` / `OBJTYPEDEF(...)` macrok compound-literal-ekkel C++-ban
// nem fordulnak.
//
// MINDEN deklaráció `API`-val van jelölve (= __declspec(dllexport) MSVC-n) —
// így a LuaJIT FFI a process-szimbólumokból resolválja ezeket (Script
// komponens hot-bindingjéhez).

#include "engine.h"
#ifdef obj
#undef obj   // motor function-like macro vs typedef
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Új "üres" node a parent alá (vagy a scene root alá ha parent == NULL).
// A node típusa Transform (pos/rot/scale), default scale = (1,1,1).
API struct obj* editor_obj_new_transform(struct obj* parent, const char* name);

// Új MeshRenderer node (pos/rot/scale + model_path + cast_shadows + tint).
// `model_path` lehet NULL/üres — a felhasználó az Inspectorban beírhatja.
API struct obj* editor_obj_new_mesh_renderer(struct obj* parent, const char* name,
                                             const char* model_path);

// Render-side query helpers (a Scene panel render-walk-jához).
API int          editor_obj_is_mesh_renderer    (const struct obj* o);
API const char*  editor_mesh_renderer_path      (const struct obj* o);
// out_pivot egy `float[16]` (mat44) ami pos/rot/scale-ből kompozit mátrix.
API void         editor_mesh_renderer_compose_pivot(const struct obj* o,
                                                    float out_pivot[16]);

// Új SpriteRenderer node (pos/rot/scale + texture_path + tint).
API struct obj* editor_obj_new_sprite_renderer(struct obj* parent, const char* name,
                                               const char* texture_path);

// Sprite query helpers a Scene2D render-walk-jához.
API int          editor_obj_is_sprite_renderer (const struct obj* o);
API const char*  editor_sprite_renderer_path   (const struct obj* o);
API void         editor_sprite_renderer_get_xform(
                     const struct obj* o, float out_pos[3], float* out_rot_deg,
                     float out_scale[2], unsigned* out_tint);

// Új TilemapRef node (pos + tmx_path).
API struct obj* editor_obj_new_tilemap_ref(struct obj* parent, const char* name,
                                           const char* tmx_path);

API int          editor_obj_is_tilemap_ref     (const struct obj* o);
API const char*  editor_tilemap_ref_path       (const struct obj* o);
API void         editor_tilemap_ref_get_pos    (const struct obj* o,
                                                float out_pos[3]);

// Új LightRef node. type: 0=DIRECTIONAL, 1=POINT, 2=SPOT.
API struct obj* editor_obj_new_light_ref(struct obj* parent, const char* name,
                                         int type);

API int          editor_obj_is_light_ref      (const struct obj* o);
// Out-paraméter `light_t*` (a hívó allokál; void* hogy a header ne függjön
// a render-header-től).
API void         editor_light_ref_to_light_t  (const struct obj* o, void* out_light);

// Új CameraRef node.
API struct obj* editor_obj_new_camera_ref(struct obj* parent, const char* name);
API int          editor_obj_is_camera_ref     (const struct obj* o);
API int          editor_camera_ref_is_active  (const struct obj* o);
// Out-paraméterek: void* (vec3* castelve), hogy a header ne kelljen az
// engine.h vec3-jára. A hívó allokál vec3-okat.
API void         editor_camera_ref_get_params (const struct obj* o,
                                               void* out_pos, void* out_dir,
                                               float* out_fov, float* out_near,
                                               float* out_far);

// Új AudioSource node.
API struct obj* editor_obj_new_audio_source(struct obj* parent, const char* name,
                                            const char* clip_path);
API int          editor_obj_is_audio_source(const struct obj* o);
API const char*  editor_audio_source_path  (const struct obj* o);
API void         editor_audio_source_get_params(const struct obj* o,
                                                void* out_pos,
                                                float* out_volume,
                                                float* out_pitch,
                                                int* out_loop,
                                                int* out_spatial);

// Új Script node (M17). script_path lehet üres/null.
API struct obj* editor_obj_new_script         (struct obj* parent, const char* name,
                                               const char* script_path);
API int          editor_obj_is_script          (const struct obj* o);
API const char*  editor_script_path            (const struct obj* o);
API int          editor_script_auto_reload     (const struct obj* o);
API int          editor_script_enabled         (const struct obj* o);
API void         editor_script_set_enabled     (struct obj* o, int v);
// A ScriptHost (C++) használja a vm_handle slot-ot opaque pointer-ként;
// itt egy stabil cím adunk vissza neki, hogy be tudja állítani/olvasni.
API void**       editor_script_vm_handle_addr  (struct obj* o);

// CameraRef forward-direction-pointer (FPS-script mutáció Lua-ból).
API vec3* editor_camera_ref_dir_addr     (struct obj* o);

// Per-komponens pos-pointer access. A scene_helpers `editor_obj_pos_addr()`
// dispatcheli ezeket (Transform, MeshRenderer, SpriteRenderer, később
// LightRef, stb.). NULL ha a node nem az adott típusú.
API vec3* editor_transform_pos_addr      (struct obj* o);
API vec3* editor_mesh_renderer_pos_addr  (struct obj* o);
API vec3* editor_sprite_renderer_pos_addr(struct obj* o);
API vec3* editor_tilemap_ref_pos_addr    (struct obj* o);
API vec3* editor_light_ref_pos_addr      (struct obj* o);
API vec3* editor_camera_ref_pos_addr     (struct obj* o);
API vec3* editor_audio_source_pos_addr   (struct obj* o);

// Rot+scale pointer-access (M15 — rotate/scale gizmo). NULL ha nincs.
API vec3* editor_transform_rot_addr      (struct obj* o);
API vec3* editor_transform_scale_addr    (struct obj* o);
API vec3* editor_mesh_renderer_rot_addr  (struct obj* o);
API vec3* editor_mesh_renderer_scale_addr(struct obj* o);
API vec3* editor_sprite_renderer_rot_addr  (struct obj* o);
API vec3* editor_sprite_renderer_scale_addr(struct obj* o);
API vec3* editor_tilemap_ref_rot_addr    (struct obj* o);
API vec3* editor_tilemap_ref_scale_addr  (struct obj* o);
API vec3* editor_obj_rot_addr            (struct obj* o);
API vec3* editor_obj_scale_addr          (struct obj* o);

// Univerzális pos-pointer minden Transform-jellegű komponensre. NULL ha nincs.
API vec3* editor_obj_pos_addr(struct obj* o);

// `obj_children(parent)` egy `array(obj*)*`-ot ad, ahol child[0] a parent
// szülője (back-pointer), child[1..] a valódi gyermekek. Ezek a helperek
// elrejtik a 0-index offset-et.
API int           editor_obj_child_count(const struct obj* parent);
API struct obj*   editor_obj_child_at   (const struct obj* parent, int i);

#ifdef __cplusplus
}
#endif
