#pragma once

// Extern-C declarations for the built-in component-helper functions.
// The components themselves live in C99 files (see transform.c, etc.) — the
// `STRUCT(...)` / `OBJTYPEDEF(...)` macros with compound-literals do not
// compile in C++.
//
// EVERY declaration is marked with `API` (= __declspec(dllexport) on MSVC) —
// so the LuaJIT FFI resolves these from the process symbols (for the Script
// component hot-binding).

#include "engine.h"
#ifdef obj
#undef obj   // engine function-like macro vs typedef
#endif

#ifdef __cplusplus
extern "C" {
#endif

// New "empty" node under parent (or under the scene root if parent == NULL).
// The node type is Transform (pos/rot/scale), default scale = (1,1,1).
API struct obj* editor_obj_new_transform(struct obj* parent, const char* name);

// New MeshRenderer node (pos/rot/scale + model_path + cast_shadows + tint).
// `model_path` can be NULL/empty — the user can type it in the Inspector.
API struct obj* editor_obj_new_mesh_renderer(struct obj* parent, const char* name,
                                             const char* model_path);

// Render-side query helpers (for the Scene panel render-walk).
API int          editor_obj_is_mesh_renderer    (const struct obj* o);
API const char*  editor_mesh_renderer_path      (const struct obj* o);
// out_pivot is a `float[16]` (mat44) composed from pos/rot/scale.
API void         editor_mesh_renderer_compose_pivot(const struct obj* o,
                                                    float out_pivot[16]);

// New SpriteRenderer node (pos/rot/scale + texture_path + tint).
API struct obj* editor_obj_new_sprite_renderer(struct obj* parent, const char* name,
                                               const char* texture_path);

// Sprite query helpers for the Scene2D render-walk.
API int          editor_obj_is_sprite_renderer (const struct obj* o);
API const char*  editor_sprite_renderer_path   (const struct obj* o);
API void         editor_sprite_renderer_get_xform(
                     const struct obj* o, float out_pos[3], float* out_rot_deg,
                     float out_scale[2], unsigned* out_tint);

// New TilemapRef node (pos + tmx_path).
API struct obj* editor_obj_new_tilemap_ref(struct obj* parent, const char* name,
                                           const char* tmx_path);

API int          editor_obj_is_tilemap_ref     (const struct obj* o);
API const char*  editor_tilemap_ref_path       (const struct obj* o);
API void         editor_tilemap_ref_get_pos    (const struct obj* o,
                                                float out_pos[3]);

// New LightRef node. type: 0=DIRECTIONAL, 1=POINT, 2=SPOT.
API struct obj* editor_obj_new_light_ref(struct obj* parent, const char* name,
                                         int type);

API int          editor_obj_is_light_ref      (const struct obj* o);
// Out-parameter `light_t*` (caller allocates; void* so the header does not
// depend on the render-header).
API void         editor_light_ref_to_light_t  (const struct obj* o, void* out_light);

// New CameraRef node.
API struct obj* editor_obj_new_camera_ref(struct obj* parent, const char* name);
API int          editor_obj_is_camera_ref     (const struct obj* o);
API int          editor_camera_ref_is_active  (const struct obj* o);
// Out-parameters: void* (cast as vec3*), so the header does not need
// engine.h's vec3. The caller allocates the vec3s.
API void         editor_camera_ref_get_params (const struct obj* o,
                                               void* out_pos, void* out_dir,
                                               float* out_fov, float* out_near,
                                               float* out_far);

// New AudioSource node.
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

// New Script node (M17). script_path can be empty/null.
API struct obj* editor_obj_new_script         (struct obj* parent, const char* name,
                                               const char* script_path);
API int          editor_obj_is_script          (const struct obj* o);
API const char*  editor_script_path            (const struct obj* o);
API int          editor_script_auto_reload     (const struct obj* o);
API int          editor_script_enabled         (const struct obj* o);
API void         editor_script_set_enabled     (struct obj* o, int v);
// The ScriptHost (C++) uses the vm_handle slot as an opaque pointer;
// here we hand back a stable address so it can set/read it.
API void**       editor_script_vm_handle_addr  (struct obj* o);

// CameraRef forward-direction pointer (FPS-script mutation from Lua).
API vec3* editor_camera_ref_dir_addr     (struct obj* o);

// Per-component pos-pointer access. The scene_helpers `editor_obj_pos_addr()`
// dispatches these (Transform, MeshRenderer, SpriteRenderer, later
// LightRef, etc.). NULL if the node is not of the given type.
API vec3* editor_transform_pos_addr      (struct obj* o);
API vec3* editor_mesh_renderer_pos_addr  (struct obj* o);
API vec3* editor_sprite_renderer_pos_addr(struct obj* o);
API vec3* editor_tilemap_ref_pos_addr    (struct obj* o);
API vec3* editor_light_ref_pos_addr      (struct obj* o);
API vec3* editor_camera_ref_pos_addr     (struct obj* o);
API vec3* editor_audio_source_pos_addr   (struct obj* o);

// Rot+scale pointer-access (M15 — rotate/scale gizmo). NULL if absent.
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

// Universal pos-pointer for every Transform-style component. NULL if absent.
API vec3* editor_obj_pos_addr(struct obj* o);

// `obj_children(parent)` returns an `array(obj*)*` where child[0] is the
// parent's own parent (back-pointer) and child[1..] are the real children.
// These helpers hide the 0-index offset.
API int           editor_obj_child_count(const struct obj* parent);
API struct obj*   editor_obj_child_at   (const struct obj* parent, int i);

#ifdef __cplusplus
}
#endif
