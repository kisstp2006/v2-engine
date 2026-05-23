#pragma once

// Wrappers around the engine's `obj_new(...)` macro.
//
// Why: the engine's `OBJ_CTOR` macro uses a C99 compound literal
// (`(TYPE){ ... }`), which MSVC C++ does NOT support. The wrappers are
// called from a C99 .c file, and the C++ side uses them via `extern "C"`.
//
// The `API` macro = __declspec(dllexport) on MSVC → the LuaJIT FFI
// resolves these from the process symbols (for the Script-component
// hot-binding).

#include "engine.h"
#ifdef obj
#undef obj
#endif

#ifdef __cplusplus
extern "C" {
#endif

API struct obj* editor_obj_new_scene(const char* name);

// editor_obj_pos_addr declaration lives in components/components_api.h.
API int          editor_obj_child_count(const struct obj* parent);
API struct obj*  editor_obj_child_at   (const struct obj* parent, int i);

// Component-class classifier — which viewport should show the gizmo:
//   - 2D: SpriteRenderer, TilemapRef
//   - 3D: Transform, MeshRenderer (and anything else with a pos-pointer that is not 2D)
API int          editor_obj_is_2d_component(const struct obj* o);
API int          editor_obj_is_3d_component(const struct obj* o);

// Cycle-detection for reparent. True if `potentialAncestor` is found along
// `node`'s parent chain (or is the node itself). If we dropped `node`
// under `potentialAncestor` a cycle would form → forbidden.
API int          editor_obj_is_ancestor(const struct obj* potentialAncestor,
                                        const struct obj* node);

#ifdef __cplusplus
}
#endif
