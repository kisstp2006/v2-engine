#pragma once

// Wrapper-ek a motor `obj_new(...)` macro köré.
//
// Miért kell: a motor `OBJ_CTOR` macro C99 compound literal-t használ
// (`(TYPE){ ... }`), amit MSVC C++ NEM támogat. A wrappereket C99 .c
// fájlban hívjuk, és a C++ oldal `extern "C"`-en át használja.
//
// Az `API` macro = __declspec(dllexport) MSVC-n → a LuaJIT FFI a process-
// szimbólumokból resolválja ezeket (Script-komponens hot-bindingjéhez).

#include "engine.h"
#ifdef obj
#undef obj
#endif

#ifdef __cplusplus
extern "C" {
#endif

API struct obj* editor_obj_new_scene(const char* name);

// editor_obj_pos_addr deklaráció a components/components_api.h-ban van.
API int          editor_obj_child_count(const struct obj* parent);
API struct obj*  editor_obj_child_at   (const struct obj* parent, int i);

// Komponens-osztály classifier — melyik viewport mutassa a gizmot:
//   - 2D: SpriteRenderer, TilemapRef
//   - 3D: Transform, MeshRenderer (és minden további pos-pointerrel ami nem 2D)
API int          editor_obj_is_2d_component(const struct obj* o);
API int          editor_obj_is_3d_component(const struct obj* o);

// Cycle-detection a reparent-hez. true ha `potentialAncestor` a `node`
// szülő-lánca mentén megtalálható (vagy önmaga). Ha node-ot
// potentialAncestor alá ejtenénk, ciklus keletkezne → tilos.
API int          editor_obj_is_ancestor(const struct obj* potentialAncestor,
                                        const struct obj* node);

#ifdef __cplusplus
}
#endif
