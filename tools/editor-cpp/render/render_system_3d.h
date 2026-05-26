#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "shadow_batch.h"

namespace editor {

class EditorApp;

// Per-frame derived data for a single mesh. Built ONCE in beginFrame()
// from the SceneQuery-cached mesh list, then read by renderShadowPass +
// renderMainPass + walkShadowPass_ — instead of re-deriving in each pass
// (audit Findings B/D/G/J/K). Cuts ~150-300 µs/frame from a 12-mesh
// shadow-on scene by skipping:
//   - 8× `editor_mesh_renderer_path` per mesh (path)
//   - 2-3× main + 6× shadow `editor_mesh_renderer_compose_pivot` (pivot)
//   - 2× `model_bsphere` (bsphere)
//   - per-frame `model_has_transparency` (transparent)
//   - per-face `ShadowBatch::isCompatible` (shadow_compat)
struct RenderableMesh {
    obj*     node          = nullptr;
    model_t* mp            = nullptr;
    // `mat44` is a `float[16]` typedef — no aggregate or assignment init
    // allowed. Left uninitialized; buildRenderables_ fully populates before
    // any read. Same for `sphere` (C-style struct with arrays inside).
    mat44    pivot;
    sphere   bsphere;
    float    dist2         = 0.0f;      // sq distance to camera (transparency sort)
    bool     transparent   = false;     // model_has_transparency cache
    bool     shadow_compat = false;     // ShadowBatch::isCompatible cache
    bool     is_skinned    = false;     // skinned-glTF || has joints
    int      cull_mode     = 0;         // editor_mesh_renderer_cull_mode cache
};

// 3D render system — shared between ScenePanel and GamePanel viewports.
// Refaktor F4.
//
// Replaces the verbatim-copied render loops that lived in scene_panel.cpp
// and game_panel.cpp pre-refactor:
//   - walkAndRender / walkAndRenderMeshes  (frustum cull + transparency sort)
//   - renderMeshNode / renderMeshNode_      (per-mesh setup + draw)
//   - renderMeshShadowOnly                  (shadow pass per-mesh)
//   - walkShadowPass                        (shadow batch loop)
//   - collectLights, resolveSkybox          (helpers)
//
// Lifetime: ONE instance per viewport panel. The panel owns its own
// shadowmap_t + ShadowBatch state (different cameras / shadow cubemaps), so
// the two viewports cannot share a single instance. AssetManager and
// SceneQuery, by contrast, ARE shared (asset cache + scene-walk results
// are camera-independent).
//
// Hot path: `render()` is called once per panel per frame. All per-frame
// scratch buffers are members (no heap thrash); the only allocation in the
// steady state is one `light_t` per LightRef on first sighting (and that
// will move into SceneQuery in Perf #3).
//
// Thread: main-thread only (motor GL calls are not thread-safe).
class RenderSystem3D {
public:
    RenderSystem3D() = default;
    ~RenderSystem3D();

    RenderSystem3D(const RenderSystem3D&)            = delete;
    RenderSystem3D& operator=(const RenderSystem3D&) = delete;

    // Per-frame parameters. Each panel fills this in and hands it to
    // `render()`. The viewport panel is responsible for FBO binding +
    // glClear + camera_enable BEFORE calling render() — the render system
    // does not own the framebuffer or clear semantics.
    struct FrameParams {
        // Camera (already configured by the panel — editor-freefly in
        // ScenePanel, CameraRef-resolved in GamePanel). Copied here so we
        // can read proj/view without aliasing the panel's mutable camera.
        camera_t cam;

        // Viewport size in pixels — used for fx_begin_res and for any
        // viewport-dependent rendering.
        int viewport_w = 0;
        int viewport_h = 0;

        // Master frustum-cull toggle (panel toolbar checkbox).
        bool frustum_cull = true;

        // Profiler scope prefix — "Editor.Scene" or "Editor.Game". Lets the
        // Profiler panel distinguish per-viewport timings.
        const char* profile_prefix = "Editor.Render";
    };

    // Entry point. Walks the scene through `params.cam`, renders shadows
    // (if any cast_shadows lights), background sky, opaque meshes, and the
    // transparency-sorted second pass. The panel handles ImGui, FBO bind,
    // gizmo, debug overlays, etc.
    //
    // STEP 1 (current): empty stub. Subsequent migration steps (#2-#5)
    // move the per-mesh and per-pass logic out of the panels and into here.
    void render(EditorApp& app, const FrameParams& params);

    // Top-of-frame setup. Builds the per-frame `RenderableMesh` list (model
    // load + cache, pivot compose w/ skinned flip, bsphere, transparent +
    // shadow-compat flags, camera dist²) for the SceneQuery-cached meshes.
    // MUST be called before renderShadowPass / renderMainPass on the same
    // frame. `cam` is the active camera (editor freefly in Scene panel,
    // CameraRef-resolved in Game panel).
    void beginFrame(EditorApp& app, camera_t& cam);

    // Per-mesh main-pass draw. Caller passes the already-resolved `mp` and
    // the already-composed `pivot` (both from the RenderableMesh built in
    // beginFrame). The motor `model_render` takes non-const `float[]` for
    // proj/view, hence non-const `camera_t&`.
    void renderMeshNode(obj* node, EditorApp& app, camera_t& cam,
                        model_t* m, const mat44& pivot,
                        const std::vector<light_t>& lights,
                        obj* fogNode, skybox_t* sky);

    // Full shadow pass — lazy-inits the shadowmap on first call with a
    // caster, applies ShadowSettings if the scene has one, then drives the
    // `shadowmap_step` loop via the Plan B batched walk. No-op when there
    // are no shadow casters in `lights`.
    //
    // Counters published: Editor.Shadow.AppliedResolution,
    //   NumShadowCasters, BatchPerFace, FallbackPerFace, CulledPerFace.
    void renderShadowPass(EditorApp& app, camera_t& cam,
                          const std::vector<light_t>& lights,
                          bool frustum_cull);

    // True if any light in `lights` has `cast_shadows` set. Use to
    // short-circuit before calling renderShadowPass.
    static bool hasAnyShadowCaster(const std::vector<light_t>& lights);

    // Main render pass — walks the SceneQuery-cached mesh list, frustum-culls
    // each mesh, defers transparent meshes, and finally renders opaque (any
    // order) + transparent (back-to-front by camera distance). Internally
    // calls `renderMeshNode` for every mesh that survives the cull.
    //
    // Counters published: Editor.Render.MeshesDrawn,
    //   Editor.Render.MeshesCulled, Editor.Render.MeshesTransparent,
    //   Editor.Render.UniqueModels.
    //
    // Profile scope: Editor.Render.MainPass.
    void renderMainPass(EditorApp& app, camera_t& cam,
                        const std::vector<light_t>& lights,
                        obj* fogNode, skybox_t* sky,
                        bool frustum_cull);

private:
    // Lazy bus wiring — invalidates the override apply cache on scene mutation.
    bool busWired_ = false;
    void wireBusIfNeeded_(EditorApp& app);

    // Shadowmap state — lazy init on the first frame with at least one
    // shadow-casting light. Persists across frames (depth cubemap stays
    // allocated).
    shadowmap_t sm_{};
    bool        sm_init_ = false;

    // Plan B shadow batch — bypasses the motor's per-mesh model_render
    // overhead during the shadow pass (~500 us → ~50 us per mesh).
    ShadowBatch shadow_batch_;

    // Internal shadow-pass helper — drives the Plan B batched walk over
    // `renderables_` for one cubemap face. renderMeshShadowOnly_ was
    // removed; the fallback path now inline-renders via model_render
    // (the `renderables_` cache already has the pivot composed).
    void walkShadowPass_(EditorApp& app, camera_t& cam, bool frustum_cull);

    // FNV-1a hash over `n` bytes — used by the uniform-set skip cache.
    static uint64_t fnv1a_(const void* p, size_t n);

    // Per-frame derived data list. Built once in beginFrame(), iterated by
    // both renderShadowPass + renderMainPass. Persistent member so capacity
    // is preserved across frames (zero heap churn in steady state).
    std::vector<RenderableMesh> renderables_;

    // Reusable scratch for the transparency sort — stores indices into
    // `renderables_`. clear()+sort() in renderMainPass; preserves capacity.
    std::vector<int> transparent_indices_;

    // Unique model_t* set for the UniqueModels counter (audit Finding A).
    // Was previously `unordered_set<std::string>` — string copy + heap alloc
    // per insert per mesh per frame. Pointer-keyed = no allocation.
    std::unordered_set<model_t*> frameUniqueModels_;

    // Cached shadow-batch prototype (audit Finding C). Was previously a
    // linear scan of `app.assets().models()` per face × per light. Now
    // resolved on first need, invalidated by kEvtScene* events.
    model_t* shadow_proto_       = nullptr;
    bool     shadow_proto_dirty_ = true;

    // Uniform-set skip cache (post-audit perf — not on the original Finding
    // list). The motor's model_light/model_shadow/model_fog/model_skybox
    // just SET model_t internal state (no GL bind, no UBO upload). Calling
    // them with unchanged values is pure redundancy: 14 mesh × 4 setters
    // × 60 fps = 3360 wasted setter-calls / sec. model_fog is the worst —
    // it pushes 5 shader2_adduniforms hash-inserts per call.
    //
    // We compare per-(model_t*) cached state hashes; if unchanged, skip.
    // Invalidated by the same bus events as overridesApplied_ (any scene
    // mutation could have changed lights / fog / sky / shadowmap params).
    struct AppliedUniforms {
        uint64_t        lights_hash = 0;
        uint64_t        fog_hash    = 0;
        skybox_t*       sky_ptr     = nullptr;
        shadowmap_t*    sm_ptr      = nullptr;
        // `applied` is true once we've populated this entry at least once;
        // a freshly default-constructed entry shouldn't accidentally match
        // the per-frame state (which could legitimately hash to 0 in some
        // edge case).
        bool            applied     = false;
    };
    std::unordered_map<model_t*, AppliedUniforms> appliedUniforms_;

    // Per-frame "current" state, populated at the top of renderMainPass.
    // renderMeshNode reads these and compares against appliedUniforms_[mp].
    uint64_t        frame_lights_hash_ = 0;
    uint64_t        frame_fog_hash_    = 0;
    skybox_t*       frame_sky_         = nullptr;
    shadowmap_t*    frame_sm_          = nullptr;

    // Cached fog params — read from `fogNode` ONCE at the top of
    // renderMainPass instead of per-mesh in renderMeshNode. Saves
    // (num_meshes - 1) `editor_fog_settings_get` calls per frame.
    int   frame_fog_mode_    = 0;
    vec3  frame_fog_color_;
    float frame_fog_start_   = 0.0f;
    float frame_fog_end_     = 1.0f;
    float frame_fog_density_ = 0.0f;

    // MaterialOverrides apply cache — skip the ~1.5 KB struct-copy + bit-mask
    // overlay if THIS node already had its overrides applied since the last
    // scene mutation. Cleared via wireBusIfNeeded_'s subscription. Migrated
    // from the panels in step #2 (renderMeshNode).
    std::unordered_set<obj*> overridesApplied_;
};

}  // namespace editor
