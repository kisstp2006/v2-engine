// STL FIRST — see other editor .cpp files for the rationale.
#include <algorithm>
#include <any>
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "render_system_3d.h"

#include "../app/editor_app.h"
#include "../components/components_api.h"
#include "../core/event_bus.h"
#include "../core/events.h"
#include "../core/profile_scope.h"
#include "../persistence/material_override_io.h"

namespace editor {

RenderSystem3D::~RenderSystem3D() {
    if (sm_init_) {
        shadowmap_destroy(&sm_);
        sm_init_ = false;
    }
}

void RenderSystem3D::wireBusIfNeeded_(EditorApp& app) {
    if (busWired_) return;
    busWired_ = true;
    // Any scene mutation invalidates:
    //   - MaterialOverrides apply cache (Inspector might have changed an
    //     override field on any mesh)
    //   - Shadow batch prototype cache (Finding C — proto's underlying
    //     model_t* might have been mtime-reloaded / erased)
    //   - Uniform-set skip cache (lights / fog / sky / shadowmap params
    //     could have changed; safer to re-bind every model on next frame)
    auto invalidate = [this](const std::any&){
        overridesApplied_.clear();
        shadow_proto_dirty_ = true;
        appliedUniforms_.clear();
    };
    app.bus().on(kEvtSceneDirty,    invalidate);
    app.bus().on(kEvtSceneReplaced, invalidate);
    app.bus().on(kEvtNodeAdded,     invalidate);
    app.bus().on(kEvtNodeRemoved,   invalidate);
}

// FNV-1a — fast non-cryptographic hash for the uniform-set skip cache.
uint64_t RenderSystem3D::fnv1a_(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < n; ++i) {
        h = (h ^ b[i]) * 0x100000001b3ULL;
    }
    return h;
}

// ---------------------------------------------------------------------------
// beginFrame — per-frame derived data build (audit Findings B/D/G/J/K + A)
// ---------------------------------------------------------------------------

void RenderSystem3D::beginFrame(EditorApp& app, camera_t& cam) {
    EDITOR_PROFILE("Editor.Render.BuildRenderables");
    wireBusIfNeeded_(app);

    renderables_.clear();
    frameUniqueModels_.clear();

    const auto& meshes = app.sceneQuery().meshNodes(app.scene().root());
    renderables_.reserve(meshes.size());

    vec3 cam_pos = pos44(cam.view);

    for (obj* m : meshes) {
        const char* relPath = editor_mesh_renderer_path(m);
        if (!relPath || !*relPath) continue;

        // const-ref avoids the std::string copy (audit Finding F).
        const std::string& absPath = app.assets().absPathFor(m, relPath);
        model_t* mp = app.assets().modelByAbsPath(absPath);
        if (!mp) {
            mp = app.assets().loadModel(absPath, relPath);
            if (!mp) continue;
        }

        RenderableMesh r;
        r.node = m;
        r.mp = mp;
        editor_mesh_renderer_compose_pivot(m, r.pivot);
        // Apply the glTF Z-flip ONCE here — was applied 2× per mesh in the
        // pre-audit code (renderMeshNode + renderMeshShadowOnly).
        if (mp->flags & MODEL_GLTF_SKINNED) {
            mat44 zrot180; id44(zrot180); zrot180[0] = -1.0f; zrot180[5] = -1.0f;
            mat44 tmp; multiply44x2(tmp, r.pivot, zrot180);
            memcpy(r.pivot, tmp, sizeof(mat44));
        }
        r.bsphere       = model_bsphere(*mp, r.pivot);
        r.transparent   = model_has_transparency(mp);
        r.shadow_compat = ShadowBatch::isCompatible(mp);
        r.is_skinned    = (mp->flags & MODEL_GLTF_SKINNED) || mp->num_joints > 0;
        r.cull_mode     = editor_mesh_renderer_cull_mode(m);

        vec3 mesh_pos = vec3(r.pivot[12], r.pivot[13], r.pivot[14]);
        vec3 d = sub3(mesh_pos, cam_pos);
        r.dist2 = d.x*d.x + d.y*d.y + d.z*d.z;

        renderables_.push_back(r);
        frameUniqueModels_.insert(mp);
    }
}

void RenderSystem3D::render(EditorApp& app, const FrameParams& params) {
    // STEP 1: empty stub. Panel-side render code still drives the loop.
    // Migration progresses in subsequent steps.
    (void)app;
    (void)params;
}

void RenderSystem3D::renderMeshNode(obj* node, EditorApp& app, camera_t& cam,
                                    model_t* m, const mat44& pivot,
                                    const std::vector<light_t>& lights,
                                    obj* fogNode, skybox_t* sky) {
    if (!m) return;
    wireBusIfNeeded_(app);
    (void)fogNode;
    (void)sky;

    // Uniform-set skip cache: the motor's model_light/shadow/fog/skybox
    // just set model_t internal fields. If the values are unchanged for
    // THIS model_t* since the last call, skip the setter entirely.
    // ~3-8 ms / frame savings on a 14-mesh, no-Inspector-edit scene.
    AppliedUniforms& st = appliedUniforms_[m];

    if (!st.applied || st.lights_hash != frame_lights_hash_) {
        if (!lights.empty()) {
            model_light(m, (unsigned)lights.size(),
                        const_cast<light_t*>(lights.data()));
        } else {
            model_light(m, 0, nullptr);
        }
        st.lights_hash = frame_lights_hash_;
    }
    if (!st.applied || st.sm_ptr != frame_sm_) {
        model_shadow(m, frame_sm_);
        st.sm_ptr = frame_sm_;
    }
    if (!st.applied || st.fog_hash != frame_fog_hash_) {
        // Fog params are pre-fetched into frame_fog_* in renderMainPass —
        // no per-mesh editor_fog_settings_get call here.
        model_fog(m, (unsigned)frame_fog_mode_, frame_fog_color_,
                  frame_fog_start_, frame_fog_end_, frame_fog_density_);
        st.fog_hash = frame_fog_hash_;
    }
    if (!st.applied || st.sky_ptr != frame_sky_) {
        if (frame_sky_) {
            model_skybox(m, *frame_sky_);
        } else {
            skybox_t empty = {0};
            model_skybox(m, empty);
        }
        st.sky_ptr = frame_sky_;
    }
    st.applied = true;

    // Material overrides — skip the ~1.5 KB struct-copy + bit-mask overlay
    // if THIS node already had its overrides applied since the last scene
    // mutation. wireBusIfNeeded_ subscribes to the invalidation events.
    if (overridesApplied_.find(node) == overridesApplied_.end()) {
        material_override_io::applyOverridesToModel(
            node, m, app.projectPath());
        overridesApplied_.insert(node);
    }

    // Pivot already composed (with skinned-glTF flip) in beginFrame —
    // just hand it to model_render. pass=-1 → every default pass.
    // `mat44` is a `float[16]` typedef → memcpy, not assignment.
    mat44 pivot_copy;
    memcpy(pivot_copy, pivot, sizeof(mat44));
    model_render(m, cam.proj, cam.view, &pivot_copy, 1, -1);
}

// ---------------------------------------------------------------------------
// Shadow pass (step #3)
// ---------------------------------------------------------------------------

bool RenderSystem3D::hasAnyShadowCaster(const std::vector<light_t>& lights) {
    for (const auto& l : lights) {
        if (l.cast_shadows) return true;
    }
    return false;
}

void RenderSystem3D::renderShadowPass(EditorApp& app, camera_t& cam,
                                      const std::vector<light_t>& lights,
                                      bool frustum_cull) {
    if (!hasAnyShadowCaster(lights)) return;

    if (!sm_init_) {
        sm_ = shadowmap();
        sm_init_ = true;
    }
    // Apply scene-wide ShadowSettings (vsm/csm resolution, cascade-split
    // lambda, PCF filter size, etc.) if present. shadowmap_begin picks up
    // the new sizes; filter/window changes trigger an internal rebuild.
    bool shadowNodeFound = false;
    if (obj* shadowNode = app.sceneQuery().shadowSettings(app.scene().root())) {
        editor_shadow_settings_apply(shadowNode, &sm_);
        shadowNodeFound = true;
    }

    // Debug counters: published to Profiler so the user can verify the apply
    // took effect, and see how many lights are casting shadows this frame.
    editor_profile_set_counter("Editor.Shadow.AppliedResolution",
                               (double)sm_.vsm_texture_width);
    editor_profile_set_counter("Editor.Shadow.ShadowSettingsFound",
                               shadowNodeFound ? 1.0 : 0.0);
    int caster_count = 0;
    for (const auto& l : lights) if (l.cast_shadows) ++caster_count;
    editor_profile_set_counter("Editor.Shadow.NumShadowCasters",
                               (double)caster_count);

    shadowmap_begin(&sm_);
    for (size_t i = 0; i < lights.size(); ++i) {
        if (!lights[i].cast_shadows) continue;
        while (shadowmap_step(&sm_)) {
            shadowmap_light(&sm_,
                            const_cast<light_t*>(&lights[i]),
                            cam.proj, cam.view);
            walkShadowPass_(app, cam, frustum_cull);
        }
    }
    shadowmap_end(&sm_);
}

void RenderSystem3D::walkShadowPass_(EditorApp& app, camera_t& cam,
                                     bool frustum_cull) {
    // Plan B Phase 1: batched shadow pass. Called once per cubemap face by
    // the shadowmap_step loop. We bind the shadow shader + face uniforms +
    // renderstate ONCE here, then loop the cached `renderables_` doing only
    // the per-mesh MODEL/MV uniforms + VAO bind + glDraw.
    //
    // Audit Finding B/D/G/J/K: NO per-mesh path resolve / pivot compose /
    // bsphere / isCompatible re-derived here — all of it is in renderables_,
    // built once in beginFrame() and reused for all 6 cubemap faces × all
    // shadow-casting lights.
    if (renderables_.empty()) return;

    // Shadow prototype cache (Finding C). Was a linear scan of all loaded
    // models per face × per light; now resolved once, invalidated by
    // kEvtScene* events via wireBusIfNeeded_.
    if (shadow_proto_dirty_) {
        shadow_proto_ = nullptr;
        for (const auto& kv : app.assets().models()) {
            model_t* candidate = const_cast<model_t*>(&kv.second);
            if (ShadowBatch::isCompatible(candidate)) {
                shadow_proto_ = candidate;
                break;
            }
        }
        shadow_proto_dirty_ = false;
    }
    if (!shadow_proto_) {
        // Nothing compatible — fall back to per-mesh model_render. The
        // pivot is already pre-composed in renderables_.
        for (const auto& r : renderables_) {
            mat44 pivot_copy;
            memcpy(pivot_copy, r.pivot, sizeof(mat44));
            model_render(r.mp, cam.proj, cam.view, &pivot_copy, 1,
                         RENDER_PASS_SHADOW);
        }
        return;
    }

    shadow_batch_.beginFace(shadow_proto_, &sm_, cam.proj, cam.view);
    int batched = 0, fellback = 0, sh_culled = 0;
    // Per-face shadow-frustum cull. shadowmap_light updated sm_.shadow_frustum
    // for THIS face — sphere-vs-frustum skips meshes that this cubemap face
    // can't possibly see. Typical: 50-65% culled per face.
    const frustum sh_frustum = sm_.shadow_frustum;

    for (const auto& r : renderables_) {
        // Frustum cull. Same gates as the main pass: skinned + cull_mode=1
        // bypass cull (rest-pose bsphere is unreliable; sky/HUD must always
        // cast). `frustum_cull` (panel toolbar) is the master kill switch.
        bool can_cull_sh = frustum_cull && r.cull_mode == 0 && !r.is_skinned;
        if (can_cull_sh) {
            if (!frustum_test_sphere(sh_frustum, r.bsphere)) {
                ++sh_culled;
                continue;
            }
        }

        if (r.shadow_compat) {
            shadow_batch_.draw(r.mp, r.pivot);
            ++batched;
        } else {
            // Skinned / incompatible — slow path. End the batch first because
            // the per-mesh path rebinds shader/state through model_render.
            shadow_batch_.endFace();
            mat44 pivot_copy;
            memcpy(pivot_copy, r.pivot, sizeof(mat44));
            model_render(r.mp, cam.proj, cam.view, &pivot_copy, 1,
                         RENDER_PASS_SHADOW);
            shadow_batch_.beginFace(shadow_proto_, &sm_, cam.proj, cam.view);
            ++fellback;
        }
    }
    shadow_batch_.endFace();

    editor_profile_set_counter("Editor.Shadow.BatchPerFace",    (double)batched);
    editor_profile_set_counter("Editor.Shadow.FallbackPerFace", (double)fellback);
    editor_profile_set_counter("Editor.Shadow.CulledPerFace",   (double)sh_culled);
}

// ---------------------------------------------------------------------------
// Main pass (step #4)
// ---------------------------------------------------------------------------

void RenderSystem3D::renderMainPass(EditorApp& app, camera_t& cam,
                                    const std::vector<light_t>& lights,
                                    obj* fogNode, skybox_t* sky,
                                    bool frustum_cull) {
    EDITOR_PROFILE("Editor.Render.MainPass");

    // Compute per-frame state hashes for the uniform-set skip cache. Each
    // mesh's renderMeshNode compares these against its own AppliedUniforms
    // and skips model_light / model_shadow / model_fog / model_skybox if
    // unchanged. Done ONCE per frame here, not N times per mesh.
    frame_lights_hash_ = lights.empty()
        ? 0
        : fnv1a_(lights.data(), lights.size() * sizeof(light_t));
    frame_sky_ = sky;
    frame_sm_  = sm_init_ ? &sm_ : nullptr;
    if (fogNode) {
        editor_fog_settings_get(fogNode, &frame_fog_mode_, &frame_fog_color_,
                                &frame_fog_start_, &frame_fog_end_, &frame_fog_density_);
        // Hash the 6 fog params (mode + color + start/end/density).
        struct {
            int   mode;
            vec3  color;
            float start;
            float end;
            float density;
        } fog_params = { frame_fog_mode_, frame_fog_color_,
                         frame_fog_start_, frame_fog_end_, frame_fog_density_ };
        frame_fog_hash_ = fnv1a_(&fog_params, sizeof(fog_params));
    } else {
        frame_fog_mode_    = 0;
        frame_fog_color_   = vec3(0,0,0);
        frame_fog_start_   = 0.0f;
        frame_fog_end_     = 1.0f;
        frame_fog_density_ = 0.0f;
        // Non-zero sentinel so the "no fog" state is distinct from the
        // default-constructed (applied = false) AppliedUniforms entry.
        frame_fog_hash_    = 0xDEADBEEFCAFEBABEULL;
    }

    // All per-mesh derived data (model_t*, pivot, bsphere, transparent,
    // shadow_compat, dist2, cull_mode, is_skinned) is pre-built in
    // beginFrame(). This pass: frustum cull → transparency sort → draw.
    //
    // Cross-mesh transparency sort. The motor's transparent renderstate
    // enables blend BUT doesn't disable depth-write — so a transparent mesh
    // drawn FIRST writes depth at every fragment (including discarded
    // alpha-cutout pixels), occluding opaque meshes drawn AFTER it (e.g. a
    // wire-mesh trash basket cutting out the floor visible through its holes).
    // Fix: render opaque first, then transparent back-to-front by camera dist.
    mat44 projview; multiply44x2(projview, cam.proj, cam.view);
    frustum cam_frustum = frustum_build(projview);

    transparent_indices_.clear();
    int rendered = 0, culled = 0;

    for (size_t i = 0; i < renderables_.size(); ++i) {
        const RenderableMesh& r = renderables_[i];

        // Cull-mode gate:
        //   - cull_mode != 0 (Always Render) → never cull
        //   - skinned model → never cull (rest-pose bsphere is unreliable;
        //     animated extent can extend 2-3 m beyond the static sphere)
        //   - panel toolbar frustum_cull OFF → bypass for debug
        bool can_cull = frustum_cull && r.cull_mode == 0 && !r.is_skinned;
        if (can_cull) {
            if (!frustum_test_sphere(cam_frustum, r.bsphere)) {
                ++culled;
                continue;
            }
        }

        // Defer transparent meshes to a sorted second pass.
        if (r.transparent) {
            transparent_indices_.push_back((int)i);
            continue;
        }

        renderMeshNode(r.node, app, cam, r.mp, r.pivot, lights, fogNode, sky);
        ++rendered;
    }

    // Second pass: transparents, far → near.
    if (!transparent_indices_.empty()) {
        std::sort(transparent_indices_.begin(), transparent_indices_.end(),
            [this](int a, int b) {
                return renderables_[a].dist2 > renderables_[b].dist2;
            });
        for (int idx : transparent_indices_) {
            const RenderableMesh& r = renderables_[idx];
            renderMeshNode(r.node, app, cam, r.mp, r.pivot, lights, fogNode, sky);
            ++rendered;
        }
    }

    editor_profile_set_counter("Editor.Render.MeshesDrawn",       (double)rendered);
    editor_profile_set_counter("Editor.Render.MeshesCulled",      (double)culled);
    editor_profile_set_counter("Editor.Render.MeshesTransparent", (double)transparent_indices_.size());
    editor_profile_set_counter("Editor.Render.UniqueModels",      (double)frameUniqueModels_.size());
}

}  // namespace editor
