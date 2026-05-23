#pragma once

// Component-pattern boilerplate macro-k. Két csoport:
//   1) STRUCT-mező-deklarálók (Godot-szerű "minden Spatial node-nak van
//      pos/rot/scale" pattern, makró-szintű embed):
//      - COMPONENT_TRS   — pos+rot+scale (Transform, Mesh/Sprite/Tilemap-renderer)
//      - COMPONENT_POS   — csak pos (Light/Camera/Audio)
//      - STRUCT_TRS / STRUCT_POS — a STRUCT()-reg ugyanezekre.
//   2) Accessor-pattern (is_xxx / pos_addr / rot_addr / scale_addr) — Phase 2a.
//
// A `Type` a C-struct neve (pl. `MeshRenderer`). A `snake` a snake-case
// alak (pl. `mesh_renderer`) — ezt manuálisan kell megadni, mert a C
// preprocessor nem tud snake-case konverziót csinálni.
//
// MINDEGYIK macro `API`-val deklarál, így a __declspec(dllexport) érvényes
// (a LuaJIT FFI a process-szimbólumokból resolválja ezeket).
//
// Példa:
//   EDITOR_COMPONENT_IS(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_POS_ADDR(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_ROT_ADDR(MeshRenderer, mesh_renderer)
//   EDITOR_COMPONENT_SCALE_ADDR(MeshRenderer, mesh_renderer)
//
// Vagy egyszerre, ha mindhárom transform-mező megvan:
//   EDITOR_COMPONENT_BASE(MeshRenderer, mesh_renderer)

// ---- 1) STRUCT-mező-embed --------------------------------------------------

// pos+rot+scale a struct-elejére. Használat:
//   typedef struct Mesh { OBJ COMPONENT_TRS char* model_path; ... } Mesh;
#define COMPONENT_TRS                                               \
    vec3 pos;                                                       \
    vec3 rot;                                                       \
    vec3 scale;

// csak pos (Light/Camera/Audio — rot/scale értelmetlen rajtuk).
#define COMPONENT_POS                                               \
    vec3 pos;

// STRUCT-reg AUTORUN-on belül a 3 transform-mezőre. A `Type` a C-struct.
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

// All-in-one a teljes transform-tagolású komponensekre
// (Transform, MeshRenderer, SpriteRenderer, TilemapRef).
#define EDITOR_COMPONENT_BASE(Type, snake)                          \
    EDITOR_COMPONENT_IS       (Type, snake)                         \
    EDITOR_COMPONENT_POS_ADDR (Type, snake)                         \
    EDITOR_COMPONENT_ROT_ADDR (Type, snake)                         \
    EDITOR_COMPONENT_SCALE_ADDR(Type, snake)

// Csak `pos`-tartalmazó komponensekre (LightRef, CameraRef, AudioSource).
#define EDITOR_COMPONENT_POS_ONLY(Type, snake)                      \
    EDITOR_COMPONENT_IS       (Type, snake)                         \
    EDITOR_COMPONENT_POS_ADDR (Type, snake)
