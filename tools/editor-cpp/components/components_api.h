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

// MaterialOverride array on the MeshRenderer (Blokk 2.4). These are NOT
// scene-children — they live in a runtime `array(obj*) material_overrides`
// and are persisted via the editor extra-serializer (Blokk 2.5).
API int          editor_mesh_renderer_overrides_count(const struct obj* o);
API struct obj*  editor_mesh_renderer_override_at    (const struct obj* o, int i);
API void         editor_mesh_renderer_add_override   (struct obj* o, struct obj* mo);
API void         editor_mesh_renderer_remove_override(struct obj* o, int i);
API void         editor_mesh_renderer_clear_overrides(struct obj* o);
API struct obj*  editor_mesh_renderer_find_override_by_name(const struct obj* o,
                                                            const char* name);

// MaterialOverride component (Blokk 2.4). Hybrid asset-ref + inline overlay.
// `name` matches model_t.materials[i].name; `material_asset_path` (optional)
// is a project-relative path to a `.mat.json5`; `override_mask` selects which
// `inline_mat` fields overlay onto the asset.
API struct obj*    editor_obj_new_material_override(const char* name);
API int            editor_obj_is_material_override (const struct obj* o);
API const char*    editor_material_override_name        (const struct obj* o);
API const char*    editor_material_override_asset_path  (const struct obj* o);
API void           editor_material_override_set_name      (struct obj* o, const char* name);
API void           editor_material_override_set_asset_path(struct obj* o, const char* path);
// Mutable handles for direct in-place editing (used by the Inspector UI).
API material_t*    editor_material_override_inline_mat (struct obj* o);
API unsigned*      editor_material_override_mask_addr  (struct obj* o);

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

// New FogSettings node — scene-wide singleton with global fog parameters.
// The 3D Scene/Game render-walks pick up the first FogSettings in the scene
// and apply `model_fog()` on every MeshRenderer.
API struct obj* editor_obj_new_fog_settings(struct obj* parent, const char* name);
API int          editor_obj_is_fog_settings   (const struct obj* o);
// Out-parameters: caller allocates int + vec3 + 3 floats. void* on the vec3
// so this header doesn't need to depend on engine.h's vec3 type.
API void         editor_fog_settings_get(const struct obj* o,
                                         int* out_mode, void* out_color_vec3,
                                         float* out_start, float* out_end,
                                         float* out_density);

// Field-pointer accessors (mutable, like editor_obj_pos_addr). NULL if absent.
// Used by the Lua node.fog_* helpers so scripts can mutate a fog parameter
// in-place without going through a full setter call.
API int*   editor_fog_settings_mode_addr   (struct obj* o);
API vec3*  editor_fog_settings_color_addr  (struct obj* o);
API float* editor_fog_settings_start_addr  (struct obj* o);
API float* editor_fog_settings_end_addr    (struct obj* o);
API float* editor_fog_settings_density_addr(struct obj* o);

// New Skybox node — scene-wide singleton (cubemap / HDR panorama background +
// IBL environment map for PBR materials).
// `sky_path` is the only required field; refl/env fall back to it when empty.
API struct obj* editor_obj_new_skybox(struct obj* parent, const char* name,
                                      const char* sky_path);
API int          editor_obj_is_skybox        (const struct obj* o);
// Read all four fields at once. out-pointers may be NULL for the ones you don't need.
// out_sky/refl/env: const char** — pointer to the stored path string (may itself be NULL).
API void         editor_skybox_get(const struct obj* o,
                                   const char** out_sky, const char** out_refl,
                                   const char** out_env, int* out_render_bg);
// Field-pointer accessors for Lua node.skybox_* helpers (mutable).
API char** editor_skybox_sky_path_addr (struct obj* o);
API char** editor_skybox_refl_path_addr(struct obj* o);
API char** editor_skybox_env_path_addr (struct obj* o);
API int*   editor_skybox_render_bg_addr(struct obj* o);

// Read-only string getters (for Lua node.skybox_*_path helpers — match the
// MeshRenderer.model_path / SpriteRenderer.texture_path pattern).
API const char* editor_skybox_sky_path (const struct obj* o);
API const char* editor_skybox_refl_path(const struct obj* o);
API const char* editor_skybox_env_path (const struct obj* o);

// New ShadowSettings node — scene-wide singleton mirroring the motor's
// `shadowmap_t` global parameters. The Scene/Game render-walks find the
// first ShadowSettings node in the scene and call
// editor_shadow_settings_apply on the panel's shadowmap_t before
// `shadowmap_begin` each frame.
API struct obj* editor_obj_new_shadow_settings(struct obj* parent, const char* name);
API int          editor_obj_is_shadow_settings   (const struct obj* o);
// Apply the ShadowSettings to a `shadowmap_t*` (void* to avoid pulling the
// render-header into this API surface). No-op if `o` is not a ShadowSettings.
API void         editor_shadow_settings_apply(const struct obj* o, void* out_sm);

// New PostFXStack node — scene-wide singleton holding post-process pipeline
// parameters. The 3D Scene / Game render-walks wrap the world-render in
// fx_begin/end when this node exists and `enabled` is true.
// `fx_dir` is project-relative (default "assets/fx") — the shaders live there.
API struct obj* editor_obj_new_postfx_stack(struct obj* parent, const char* name);
API int          editor_obj_is_postfx_stack   (const struct obj* o);
// Read both fields at once. out-pointers may be NULL.
// out_fx_dir: const char** — pointer to the stored path string (may be NULL).
API void         editor_postfx_stack_get(const struct obj* o,
                                         int* out_enabled,
                                         const char** out_fx_dir);
// Field-pointer accessors (mutable, for Lua node.postfx_* helpers later).
API int*   editor_postfx_stack_enabled_addr(struct obj* o);
API char** editor_postfx_stack_fx_dir_addr (struct obj* o);
// Read-only string getter (Lua node.postfx_fx_dir helper, later).
API const char* editor_postfx_stack_fx_dir(const struct obj* o);
// Note: per-pass enabled / uniform snapshot is NOT stored on this component —
// it lives in a `<scene>.postfx.json5` sidecar (see postfx_state_io).

// New TextRenderer node — screen-space text overlay (HUD).
// `pos.x/.y` = viewport pixel position; markup tags in `text` allowed.
API struct obj* editor_obj_new_text_renderer(struct obj* parent,
                                             const char* name,
                                             const char* text);
API int          editor_obj_is_text_renderer(const struct obj* o);
API void         editor_text_renderer_get(const struct obj* o,
                                          const char** out_text,
                                          float out_pos[2],
                                          int* out_face, int* out_color,
                                          int* out_size, float* out_max_width);
// Field-pointer accessors for Lua node.text_* helpers (mutable).
API char** editor_text_renderer_text_addr     (struct obj* o);
API int*   editor_text_renderer_face_addr     (struct obj* o);
API int*   editor_text_renderer_color_addr    (struct obj* o);
API int*   editor_text_renderer_size_addr     (struct obj* o);
API float* editor_text_renderer_max_width_addr(struct obj* o);

// Read-only string getter (for Lua node.text_str helper).
API const char* editor_text_renderer_text(const struct obj* o);

// New Text3DRenderer node — world-space billboard text (ddraw_text).
// `pos.x/.y/.z` = world coords; ddraw_text auto-billboards toward the camera.
API struct obj* editor_obj_new_text_renderer_3d(struct obj* parent,
                                                const char* name,
                                                const char* text);
API int          editor_obj_is_text_renderer_3d(const struct obj* o);
API void         editor_text_renderer_3d_get(const struct obj* o,
                                             const char** out_text,
                                             float out_pos[3],
                                             float* out_scale,
                                             unsigned* out_color);
// Field-pointer accessors for Lua node.text3d_* helpers (mutable).
// Note: scale is the standard TRS scale — use node.scale(o) instead of a
// dedicated text3d_scale helper. The render-walk uses scale.x as a uniform
// size multiplier.
API char**    editor_text_renderer_3d_text_addr (struct obj* o);
API unsigned* editor_text_renderer_3d_color_addr(struct obj* o);

// Read-only string getter (for Lua node.text3d_str helper).
API const char* editor_text_renderer_3d_text(const struct obj* o);

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
API vec3* editor_text_renderer_3d_pos_addr(struct obj* o);

// Rot+scale pointer-access (M15 — rotate/scale gizmo). NULL if absent.
API vec3* editor_transform_rot_addr      (struct obj* o);
API vec3* editor_transform_scale_addr    (struct obj* o);
API vec3* editor_mesh_renderer_rot_addr  (struct obj* o);
API vec3* editor_mesh_renderer_scale_addr(struct obj* o);
API vec3* editor_sprite_renderer_rot_addr  (struct obj* o);
API vec3* editor_sprite_renderer_scale_addr(struct obj* o);
API vec3* editor_tilemap_ref_rot_addr    (struct obj* o);
API vec3* editor_tilemap_ref_scale_addr  (struct obj* o);
API vec3* editor_text_renderer_3d_rot_addr  (struct obj* o);
API vec3* editor_text_renderer_3d_scale_addr(struct obj* o);
API vec3* editor_obj_rot_addr            (struct obj* o);
API vec3* editor_obj_scale_addr          (struct obj* o);

// Universal pos-pointer for every Transform-style component. NULL if absent.
API vec3* editor_obj_pos_addr(struct obj* o);

// ---- Profiler helper (editor_profiler.c) ----------------------------------
// Thin wrapper around the motor's `profiler` map; the panel-side reads it
// via these snapshot functions, and the render-walk inserts samples via
// editor_profile_record_us (driven from a RAII ProfileScope in C++ code).
typedef struct editor_profile_entry_t {
    const char* name;
    float       avg_ms;     // EMA timer cost; 0 for non-timer entries
    double      stat;       // counter value (profile_incstat / setstat); NaN for timers
    int         is_timer;   // 1 if `avg_ms` is meaningful, 0 if `stat` is
} editor_profile_entry_t;

API void editor_profile_record_us       (const char* name, double us);
API void editor_profile_set_counter     (const char* name, double value);
API int  editor_profiler_count          (void);
API int  editor_profiler_collect        (editor_profile_entry_t* out, int max_count);
API void editor_profiler_reset_counters (void);
API void editor_profiler_clear          (void);

// `obj_children(parent)` returns an `array(obj*)*` where child[0] is the
// parent's own parent (back-pointer) and child[1..] are the real children.
// These helpers hide the 0-index offset.
API int           editor_obj_child_count(const struct obj* parent);
API struct obj*   editor_obj_child_at   (const struct obj* parent, int i);

#ifdef __cplusplus
}
#endif
