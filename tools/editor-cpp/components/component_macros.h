#pragma once

// Component-pattern boilerplate macros. Two groups:
//   1) STRUCT-field-declarators (Godot-style "every Spatial node has
//      pos/rot/scale" pattern, macro-level embed):
//      - COMPONENT_TRS   — pos+rot+scale (Transform, Mesh/Sprite/Tilemap-renderer)
//      - COMPONENT_POS   — pos only (Light/Camera/Audio)
//      - STRUCT_TRS / STRUCT_POS — the STRUCT()-reg counterparts.
//   2) Accessor-pattern (is_xxx / pos_addr / rot_addr / scale_addr) — Phase 2a.
//
// `Type` is the C-struct name (e.g. `MeshRenderer`). `snake` is the snake-case
// form (e.g. `mesh_renderer`) — must be passed manually because the C
// preprocessor can't do snake-case conversion.
//
// EVERY macro declares with `API`, so __declspec(dllexport) is in effect
// (the LuaJIT FFI resolves these from the process symbols).
//
// Example:
//   EDITOR_COMPONENT_IS(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_POS_ADDR(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_ROT_ADDR(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_SCALE_ADDR(MeshRenderer, mesh_renderer)
//
// Or in one go, if all three transform-fields are present:
//   EDITOR_COMPONENT_BASE(MeshRenderer, mesh_renderer)

// ---- 1) STRUCT-field-embed ------------------------------------------------

// pos+rot+scale at the start of the struct. Usage:
//   typedef struct Mesh { OBJ COMPONENT_TRS char* model_path; ... } Mesh;
#define COMPONENT_TRS                                               \
    vec3 pos;                                                       \
    vec3 rot;                                                       \
    vec3 scale;

// pos only (Light/Camera/Audio — rot/scale don't make sense on them).
#define COMPONENT_POS                                               \
    vec3 pos;

// STRUCT-reg inside AUTORUN for the 3 transform-fields. `Type` is the C-struct.
#define STRUCT_TRS(Type)                                            \
    STRUCT(Type, vec3, pos);                                        \
    STRUCT(Type, vec3, rot);                                        \
    STRUCT(Type, vec3, scale);

#define STRUCT_POS(Type)                                            \
    STRUCT(Type, vec3, pos);

// ---- 2) Accessor-pattern (Phase 2a) ---------------------------------------

#define EDITOR_COMPONENT_IS(Type, snake)                            \
    API int editor_obj_is_##snake(const obj* o) {                   \
        return o && obj_typeid((void*)o) == OBJTYPE(Type);          \
    }

#define EDITOR_COMPONENT_POS_ADDR(Type, snake)                      \
    API vec3* editor_##snake##_pos_addr(obj* o) {                   \
        if (!editor_obj_is_##snake(o)) return NULL;                 \
        return &((Type*)o)->pos;                                    \
    }

#define EDITOR_COMPONENT_ROT_ADDR(Type, snake)                      \
    API vec3* editor_##snake##_rot_addr(obj* o) {                   \
        if (!editor_obj_is_##snake(o)) return NULL;                 \
        return &((Type*)o)->rot;                                    \
    }

#define EDITOR_COMPONENT_SCALE_ADDR(Type, snake)                    \
    API vec3* editor_##snake##_scale_addr(obj* o) {                 \
        if (!editor_obj_is_##snake(o)) return NULL;                 \
        return &((Type*)o)->scale;                                  \
    }

// All-in-one for components with the full transform set
// (Transform, MeshRenderer, SpriteRenderer, TilemapRef).
#define EDITOR_COMPONENT_BASE(Type, snake)                          \
    EDITOR_COMPONENT_IS       (Type, snake)                         \
    EDITOR_COMPONENT_POS_ADDR (Type, snake)                         \
    EDITOR_COMPONENT_ROT_ADDR (Type, snake)                         \
    EDITOR_COMPONENT_SCALE_ADDR(Type, snake)

// For components with `pos` only (LightRef, CameraRef, AudioSource).
#define EDITOR_COMPONENT_POS_ONLY(Type, snake)                      \
    EDITOR_COMPONENT_IS       (Type, snake)                         \
    EDITOR_COMPONENT_POS_ADDR (Type, snake)
