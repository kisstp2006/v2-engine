#include "engine.h"
#include "scene_helpers.h"
#include "../components/components_api.h"

obj* editor_obj_new_scene(const char* name) {
    obj* o = obj_new_name(obj, name ? name : "Scene");
    return o;
}

int editor_obj_child_count(const obj* parent) {
    if (!parent) return 0;
    array(obj*)* ch = obj_children(parent);
    if (!ch) return 0;
    int n = array_count(*ch);
    /* child[0] = back-pointer to parent (engine convention), [1..] real children */
    return n > 1 ? n - 1 : 0;
}

obj* editor_obj_child_at(const obj* parent, int i) {
    if (!parent) return NULL;
    array(obj*)* ch = obj_children(parent);
    if (!ch) return NULL;
    int n = array_count(*ch);
    int idx = i + 1; /* skip [0] parent back-pointer */
    if (idx < 1 || idx >= n) return NULL;
    return (*ch)[idx];
}

vec3* editor_obj_pos_addr(obj* o) {
    vec3* p;
    if ((p = editor_transform_pos_addr(o)))         return p;
    if ((p = editor_mesh_renderer_pos_addr(o)))     return p;
    if ((p = editor_sprite_renderer_pos_addr(o)))   return p;
    if ((p = editor_tilemap_ref_pos_addr(o)))       return p;
    if ((p = editor_light_ref_pos_addr(o)))         return p;
    if ((p = editor_camera_ref_pos_addr(o)))        return p;
    if ((p = editor_audio_source_pos_addr(o)))      return p;
    if ((p = editor_text_renderer_3d_pos_addr(o)))  return p;
    return NULL;
}

int editor_obj_is_2d_component(const obj* o) {
    return editor_obj_is_sprite_renderer(o)
        || editor_obj_is_tilemap_ref(o);
}

int editor_obj_is_3d_component(const obj* o) {
    if (!o) return 0;
    if (editor_obj_is_2d_component(o)) return 0;
    return editor_obj_pos_addr((obj*)o) != NULL;   /* Transform / MeshRenderer */
}

vec3* editor_obj_rot_addr(obj* o) {
    vec3* p;
    if ((p = editor_transform_rot_addr(o)))         return p;
    if ((p = editor_mesh_renderer_rot_addr(o)))     return p;
    if ((p = editor_sprite_renderer_rot_addr(o)))   return p;
    if ((p = editor_tilemap_ref_rot_addr(o)))       return p;
    if ((p = editor_text_renderer_3d_rot_addr(o)))  return p;
    return NULL;   /* LightRef / TextRenderer (2D HUD) have no rot */
}

vec3* editor_obj_scale_addr(obj* o) {
    vec3* p;
    if ((p = editor_transform_scale_addr(o)))         return p;
    if ((p = editor_mesh_renderer_scale_addr(o)))     return p;
    if ((p = editor_sprite_renderer_scale_addr(o)))   return p;
    if ((p = editor_tilemap_ref_scale_addr(o)))       return p;
    if ((p = editor_text_renderer_3d_scale_addr(o))) return p;
    return NULL;
}

int editor_obj_is_ancestor(const obj* potentialAncestor, const obj* node) {
    if (!potentialAncestor || !node) return 0;
    if (potentialAncestor == node)   return 1;
    obj* p = obj_parent((void*)node);
    while (p) {
        if (p == potentialAncestor) return 1;
        p = obj_parent(p);
    }
    return 0;
}
