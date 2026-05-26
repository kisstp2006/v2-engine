// Editor-side shadow batch — see header for design rationale.
//
// IMPORTANT: This file uses ONLY PUBLIC engine API. The motor's `iqm_t`
// struct (containing internal VAO/IBO/submesh layout) lives behind a
// `#if CODE` guard in render_model.h and is NOT visible to editor TUs.
// We work entirely off `model_t`'s public fields (`vao`, `num_triangles`,
// `uniforms[]`, `shaderinfo[]`, `rs[]`).
//
// SUBMESH BATCHING: the engine's `model_render` loops submeshes per-call
// for per-material binds; for shadow pass we don't bind materials so we
// can issue ONE glDrawElements covering all triangles in the model
// (model_t.num_triangles is the flat total).

#include "shadow_batch.h"
#include <cstring>

namespace editor {

bool ShadowBatch::isCompatible(const model_t* m) {
    if (!m) return false;
    // Skinned glTF needs bone-UBO uploads not covered by Phase 1.
    if (m->flags & MODEL_GLTF_SKINNED) return false;
    // num_joints>0 → IQM rig → also skinned.
    if (m->num_joints > 0) return false;
    // num_triangles must be > 0 — empty / procedural models aren't supported.
    if (m->num_triangles == 0) return false;
    return true;
}

void ShadowBatch::beginFace(model_t* prototype, shadowmap_t* sm,
                            mat44 proj, mat44 view) {
    if (!prototype || !sm) return;

    prototype_ = prototype;
    memcpy(proj_, proj, sizeof(mat44));
    memcpy(view_, view, sizeof(mat44));

    // 1. Bind shadow shader (slot 1). All our models use the same program
    //    since model_setshader builds with identical defines for them.
    shader_bind(prototype->shaderinfo[1].program);

    // 2. Apply shadow renderstate once per face.
    renderstate_apply(&prototype->rs[RENDER_PASS_SHADOW]);

    // 3. Set the face-level shadow uniforms OURSELVES from the shadowmap_t
    //    (sm == editor's sm_, which IS the active_shadowmap after
    //    shadowmap_light(sm, light) was called). Doing this here — NOT
    //    via the first model_render — protects against the engine's
    //    early-return bug: render_model.h:2002-2004 returns when count==0
    //    after frustum-culling all instances, which skips
    //    model_set_uniforms and leaves SHADOW_CAMERA_TO_SHADOW_* stale
    //    from the previous face. Result: other meshes in this face
    //    render their shadows with the WRONG face's V/PV → completely
    //    wrong shadow positions, "shadow not refreshing when moving".
    model_uniforms_t* u = prototype->uniforms + 1;
    uniform_set2(&u->SHADOW_CAMERA_TO_SHADOW_VIEW,      sm->V);
    uniform_set2(&u->SHADOW_CAMERA_TO_SHADOW_PROJECTOR, sm->PV);
    uniform_set2(&u->SHADOW_TECHNIQUE,                  &sm->shadow_technique);

    active_        = true;
    first_in_face_ = true;
}

void ShadowBatch::draw(model_t* m, const mat44 model_mat) {
    if (!active_ || !m) return;

    if (first_in_face_) {
        // Use the motor's full model_render to set up the active_shadowmap
        // uniforms (SHADOW_CAMERA_TO_SHADOW_VIEW/PROJECTOR/TECHNIQUE) AND
        // to render the first mesh. The motor's path also re-binds the
        // shader, but glUseProgram with the same program is a fast no-op.
        // Subsequent meshes in this face use the fast path.
        model_render(m, proj_, view_, (mat44*)model_mat, 1, RENDER_PASS_SHADOW);
        first_in_face_ = false;
        return;
    }

    // Fast path: per-mesh uniforms + VAO bind + single draw.
    //
    // CRITICAL: use prototype_->uniforms[1] (NOT m->uniforms[1]). The
    // motor's `model_init_uniforms` is only called once per model_t (via
    // model_render's shader2_apply trigger). Models that have NEVER had
    // model_render(slot=1) called on them have `m->uniforms[1]` zero-
    // initialized — including u.location = 0 — which uniform_set2 would
    // misinterpret as a valid GL uniform location and write to slot 0!
    // The prototype got its uniforms[1] initialized via the first_in_face_
    // model_render call, so its uniform_t struct has valid locations
    // resolved against the same shader program used by all meshes here.
    model_uniforms_t* u = prototype_->uniforms + 1;

    mat44 mv;  multiply44x2(mv,  view_, (float*)model_mat);
    mat44 mvp; multiply44x2(mvp, proj_, mv);

    uniform_set2(&u->MODEL, model_mat);
    uniform_set2(&u->MV,    mv);
    uniform_set2(&u->MVP,   mvp);

    // Bind the model's VAO. For IQM-loaded models, model_t.vao mirrors
    // iqm_t.vao (the loader sets both). The VAO carries vertex attribute
    // pointers + IBO + VBO bindings from model_set_state.
    glBindVertexArray((GLuint)m->vao);

    // Draw ALL triangles in one call. For shadow pass we don't need per-
    // submesh material binding (no texture sampling in shadow shader).
    // num_triangles is the flat total across all submeshes.
    glDrawElementsInstanced(
        GL_TRIANGLES,
        3 * (GLsizei)m->num_triangles,
        GL_UNSIGNED_INT,
        nullptr,   // offset 0 — all submeshes are contiguous in the EBO
        /*instances=*/1
    );

    // NOTE: skipping `profile_incstat("Render.num_drawcalls", +1)` because
    // the motor macro uses anonymous-struct types that don't survive a C++
    // TU. The Profiler's Render.num_drawcalls counter will UNDERSHOOT the
    // true count by exactly the batched-mesh count per frame; the editor's
    // own Editor.Shadow.BatchPerFace counter shows the real number.
}

void ShadowBatch::endFace() {
    if (!active_) return;
    glBindVertexArray(0);
    active_  = false;
    prototype_ = nullptr;
}

}  // namespace editor
