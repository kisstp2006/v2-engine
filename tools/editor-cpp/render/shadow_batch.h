// Editor-side shadow batch (Plan B Phase 1 — opaque, no skinning).
//
// PROBLEM: the editor calls `model_render(..., RENDER_PASS_SHADOW)` once
// per mesh per cubemap face. With 12 meshes × 6 faces × 1 light = 72 calls
// per frame, each with ~0.5 ms of fixed CPU overhead (shader_bind +
// shader2_apply + 30+ uniform_set2 calls + renderstate_apply), the
// total shadow CPU overhead reaches ~36 ms / frame.
//
// SOLUTION: bind the shadow shader + face-level uniforms + renderstate
// ONCE per cubemap face, then loop meshes with only the per-mesh state
// changes (MODEL uniform + VAO bind + glDraw). The per-call overhead
// drops from ~500 μs to ~50 μs.
//
// PHASE 1 LIMITATIONS:
//   - Opaque only. Cutout-alpha shadows would need per-mesh albedo bind
//     + cutout_alpha uniform — not implemented. The FNAF scene has no
//     cutout assets so this is fine for now.
//   - Non-skinned only. Skinned meshes use a different shader path with
//     bone-matrix UBO uploads — fall back to the slow per-mesh path
//     for those (see scene_panel.cpp::walkShadowPass).
//
// USAGE (per shadowmap face):
//   batch.beginFace(prototype, cam_proj, cam_view);   // once / face
//   for (each mesh) batch.draw(model, model_mat);     // per mesh
//   batch.endFace();                                  // once / face
//
// `prototype` is any model_t in the scene — used to source the shadow
// shader handle, the shadow renderstate, and the engine's cached
// uniform_t handles. ALL meshes must share the same shadow shader for
// this to work (which they do under a single engine config since
// model_setshader always uses the same `all_defines`).

#pragma once

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

class ShadowBatch {
public:
    // Bind shadow shader + apply face-level uniforms (V/PV from the
    // shadowmap_t after shadowmap_light) + apply shadow renderstate. Call
    // once at the top of each cubemap face. `sm` must be the same shadowmap
    // active_shadowmap was set to by shadowmap_light — the editor passes
    // its `sm_` directly. We set the SHADOW_CAMERA_TO_SHADOW_* uniforms
    // ourselves rather than relying on model_render to do it: model_render
    // can EARLY-RETURN when the prototype is frustum-culled out of this
    // face, which would leave the shadow uniforms STALE from the previous
    // face — causing wrong-position shadows on the other meshes. (See
    // render_model.h:2002-2004 for the early-return path.)
    void beginFace(model_t* prototype, shadowmap_t* sm,
                   mat44 proj, mat44 view);

    // Draw one mesh in the current face. Updates per-mesh uniforms (MODEL,
    // MV, MVP) and submesh-loops with glDrawElementsInstanced. Skips the
    // model_render machinery (no model_analyseshader, no per-call
    // shader_bind, no renderstate re-apply).
    void draw(model_t* m, const mat44 model_mat);

    // Unbind VAO at the end of the face.
    void endFace();

    // Whether this mesh is supported by the batch (opaque, non-skinned).
    // Caller checks before draw() — skinned meshes need the slow fallback.
    static bool isCompatible(const model_t* m);

private:
    model_t* prototype_     = nullptr;
    mat44    proj_ {};
    mat44    view_ {};
    bool     active_        = false;
    // True until the first draw() of a face has executed. The first draw
    // routes through the motor's full `model_render` to set the
    // active_shadowmap-driven SHADOW_CAMERA_TO_SHADOW_* uniforms; all
    // subsequent draws use the fast path with only MODEL/MV/MVP updates.
    bool     first_in_face_ = false;
};

}  // namespace editor
