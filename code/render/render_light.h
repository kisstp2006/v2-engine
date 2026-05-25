// -----------------------------------------------------------------------------
// lights

#if !CODE

enum LIGHT_TYPE {
    LIGHT_DIRECTIONAL,
    LIGHT_POINT,
    LIGHT_SPOT,
};

enum SHADOW_TECHNIQUE {
    SHADOW_VSM,
    SHADOW_CSM,
};

typedef struct light_t { OBJ
    char *name;
    unsigned type;
    vec3 diffuse;
    float power;
    vec3 pos, dir;
    float innerCone, outerCone;
    //@todo: cookie, flare

    // Shadowmapping
    bool cast_shadows;
    bool hard_shadows;
    unsigned shadow_technique;
    float shadow_distance;
    float shadow_near_clip;
    mat44 shadow_matrix[NUM_SHADOW_CASCADES];
    float min_variance; //< VSM
    float variance_transition; //< VSM
    float shadow_bias; //< CSM
    float normal_bias; //< CSM
    float shadow_softness;
    float penumbra_size;

    // internals
    bool processed_shadows;
} light_t;
OBJTYPEDEF(light_t,OBJTYPE_light);

API light_t light();

API void    ui_light(light_t *l);
API void    ui_lights(unsigned num_lights, light_t *lights);

// Debug-draw of a light's frustum / volume / direction. Per-type:
//   - LIGHT_DIRECTIONAL: 3×3 grid of parallel arrows along `dir`, plus a
//     centerline arrow + per-cascade frustum boxes (color-coded) using
//     `shadow_matrix[]` if shadow processing already ran this frame.
//   - LIGHT_POINT: influence sphere of radius `shadow_distance`, with a
//     small RGB axis marker at `pos`.
//   - LIGHT_SPOT: cone(s) showing inner+outer cone half-angles, apex at
//     `pos`, base at `pos + dir*shadow_distance`, plus a short direction
//     arrow. Both rings are drawn when `innerCone != outerCone`.
// All draws use the standard ddraw color stack — call between ddraw_flush
// boundaries like any other ddraw primitive.
API void    ddraw_light(light_t *l);
API void    ddraw_lights(unsigned num_lights, light_t *lights);

#else

// -----------------------------------------------------------------------------

static
void light_ctor(light_t *l) {
    l->diffuse = vec3(1,1,1);
    l->dir = vec3(1,-1,-1);
    l->power = 250.0f;
    l->innerCone = 0.85f;// 31 deg
    l->outerCone = 0.9f; // 25 deg
    l->cast_shadows = true;
    l->processed_shadows = false;
    l->hard_shadows = false;
    l->shadow_distance = 400.0f;
    l->shadow_near_clip = 0.01f;
    l->shadow_bias = 0.002f;
    l->normal_bias = 0.007f;
    l->shadow_softness = 1.5f;
    l->penumbra_size = 2.0f;
    l->min_variance = 0.00002f;
    l->variance_transition = 0.2f;
}

#if HAS_OBJ
AUTORUN {
    STRUCT(light_t, char*, name, "Light name");
    STRUCT(light_t, unsigned, type, "Light type");
    STRUCT(light_t, vec3, diffuse, "Diffuse color");
    STRUCT(light_t, float, power, "Radiance (W)");
    STRUCT(light_t, vec3, pos, "Position");
    STRUCT(light_t, vec3, dir, "Direction");
    STRUCT(light_t, float, radius, "Radius");
    STRUCT(light_t, float, innerCone, "Inner cone angle");
    STRUCT(light_t, float, outerCone, "Outer cone angle");
    //@todo: cookie, flare

    // Shadowmapping
    STRUCT(light_t, bool, cast_shadows, "Cast shadows flag");
    STRUCT(light_t, bool, hard_shadows, "Hard shadows flag");
    STRUCT(light_t, unsigned, shadow_technique, "Shadow technique");
    STRUCT(light_t, float, shadow_distance, "Shadow distance");
    STRUCT(light_t, float, shadow_near_clip, "Shadow near clip distance");
    STRUCT(light_t, mat44[NUM_SHADOW_CASCADES], shadow_matrix, "Shadow matrices");
    STRUCT(light_t, float, min_variance, "Minimum variance for VSM");
    STRUCT(light_t, float, variance_transition, "Variance transition for VSM");
    STRUCT(light_t, float, shadow_bias, "Shadow bias for CSM");
    STRUCT(light_t, float, normal_bias, "Normal bias for CSM");
    STRUCT(light_t, float, shadow_softness, "Shadow softness");
    STRUCT(light_t, float, penumbra_size, "Penumbra size");

    // internals
    STRUCT(light_t, bool, processed_shadows, "_Processed shadows flag");

    obj_ctor[OBJTYPE_light] = light_ctor;
}
#endif

light_t light() {
    light_t l = {0};
    light_ctor(&l);
    return l;
}

static inline
char *light_fieldname(const char *fmt, ...) {
    static char buf[32];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return buf;
}

typedef struct light_node_t {
    vec4 diffuse;
    vec4 pos;
    vec4 dir;
    float power;
    float innerCone;
    float outerCone;
    float shadow_bias;
    float normal_bias;
    float min_variance;
    float variance_transition;
    float shadow_softness;
    float penumbra_size;
    int type;
    int processed_shadows;
    int hard_shadows;
} light_node_t;

static inline
void light_update(model_t *mdl, unsigned* ubo, unsigned num_lights, light_t *lv) {
    if (num_lights > MAX_LIGHTS) {
        num_lights = MAX_LIGHTS;
    }

    light_node_t lights[MAX_LIGHTS] = {0};
    for (unsigned i = 0; i < num_lights; i++) {
        light_node_t *light = &lights[i];
        {
            light->type = lv[i].type;
            light->diffuse = vec34(lv[i].diffuse, 0.0f);
            light->pos = vec34(lv[i].pos, 0.0f);
            light->dir = vec34(lv[i].dir, 0.0f);
            light->power = lv[i].power;
            light->innerCone = lv[i].innerCone;
            light->outerCone = lv[i].outerCone;
            light->processed_shadows = !lv[i].cast_shadows ? false : lv[i].processed_shadows;
            light->hard_shadows = lv[i].hard_shadows;
            light->shadow_bias = lv[i].shadow_bias;
            light->normal_bias = lv[i].normal_bias;
            light->min_variance = lv[i].min_variance;
            light->variance_transition = lv[i].variance_transition;
            light->shadow_softness = lv[i].shadow_softness;
            light->penumbra_size = lv[i].penumbra_size;
        }
    }

    model_uniforms_t *u = &mdl->uniforms[0];

    uniform_set2(&u->U_NUM_LIGHTS, &num_lights);
    ASSERT(ubo);

    if (num_lights == 0) {
        return;
    }

    if (*ubo == 0 /* buffer not created */) {
        *ubo = ubo_create(&lights[0], sizeof(light_node_t) * MAX_LIGHTS, STREAM_DRAW);
    } else {
        ubo_update(*ubo, 0, &lights[0], sizeof(light_node_t) * num_lights);
    }

    ubo_bind(*ubo, 0);

    for (unsigned i=0; i < num_lights; ++i) {
        bool processed = false;
        if (lv[i].processed_shadows && lv[i].shadow_technique == SHADOW_CSM) {
            processed = true;
            uniform_set4(&u->LIGHT_SHADOW_MATRIX_CSM, 0, NUM_SHADOW_CASCADES, lv[i].shadow_matrix);
        }
        if (processed) break;
    }
}

void ui_light(light_t *l) {
    const char *types[] = {
        "LIGHT_DIRECTIONAL",
        "LIGHT_POINT",
        "LIGHT_SPOT",
    };

    ui_list("Type", &l->type, countof(types), types);
    ui_float3("Position", &l->pos.x);
    ui_float3("Direction", &l->dir.x);
    ui_color3f("Diffuse", &l->diffuse.x);
    ui_floatabs("Power", &l->power);
    ui_float("Inner Cone", &l->innerCone);
    ui_float("Outer Cone", &l->outerCone);
    ui_bool("Cast Shadows", &l->cast_shadows);
    ui_bool("Hard Shadows", &l->hard_shadows);
    ui_floatabs("Shadow Distance", &l->shadow_distance);
    ui_floatabs("Shadow Bias", &l->shadow_bias);
    ui_floatabs("Normal Bias", &l->normal_bias);
    ui_floatabs("Shadow Softness", &l->shadow_softness);
    ui_floatabs("Penumbra Size", &l->penumbra_size);
    ui_floatabs("Min Variance", &l->min_variance);
    ui_floatabs("Variance Transition", &l->variance_transition);
}

void ui_lights(unsigned num_lights, light_t *lights) {
    for (unsigned i = 0; i < num_lights; ++i) {
        if (ui_collapse(va("Light %d", i), va("light_%d", i))) {
            ui_light(&lights[i]);
            ui_collapse_end();
        }
    }
}

// ----------------------------------------------------------------------------
// debug-draw

// Build a non-degenerate perpendicular basis for an arbitrary unit vector.
// `dir` must be normalized. Picks world-Y as the seed unless `dir` is nearly
// parallel to it (then world-X), to avoid the degenerate cross product.
static
void ddraw_light_basis_(vec3 dir, vec3 *out_right, vec3 *out_up) {
    vec3 seed = absf(dir.y) < 0.99f ? vec3(0,1,0) : vec3(1,0,0);
    *out_right = norm3(cross3(dir, seed));
    *out_up    = norm3(cross3(*out_right, dir));
}

// A single cone wireframe (apex + base ring + N spokes), parameterized by
// the cosine of its half-angle. Used twice for spot lights (inner + outer).
// Bails on degenerate inputs so a freshly-zeroed light_t doesn't draw garbage.
static
void ddraw_light_spot_cone_(vec3 apex, vec3 dir_n, float dist, float cos_half,
                            unsigned color, int spokes) {
    if (dist <= 0.0f) return;
    if (cos_half >=  1.0f - 1e-4f) return; // ~0° → nothing meaningful
    if (cos_half <= -1.0f + 1e-4f) return; // ~180° → not a cone, skip

    float half_angle = acosf(cos_half);
    float r = dist * tanf(half_angle);
    if (r <= 0.0f) return;

    vec3 base = add3(apex, scale3(dir_n, dist));
    vec3 right, up; ddraw_light_basis_(dir_n, &right, &up);

    ddraw_color_push(color);
    ddraw_circle(base, dir_n, r);
    for (int i = 0; i < spokes; i++) {
        float a = (float)i * 2.0f * C_PI / (float)spokes;
        vec3 pt = add3(base,
                       add3(scale3(right, cosf(a) * r),
                            scale3(up,    sinf(a) * r)));
        ddraw_line(apex, pt);
    }
    ddraw_color_pop();
}

void ddraw_light(light_t *l) {
    if (!l) return;

    // Skip drawing direction-derived geometry if dir is zero (would NaN out
    // of norm3 + acosf). Point lights have no dir requirement.
    bool has_dir = (l->dir.x != 0.0f || l->dir.y != 0.0f || l->dir.z != 0.0f);

    if (l->type == LIGHT_DIRECTIONAL) {
        if (!has_dir) return;
        vec3 dir = norm3(l->dir);
        vec3 anchor = l->pos; // typically (0,0,0)
        float len = 5.0f;
        vec3 right, up; ddraw_light_basis_(dir, &right, &up);

        // 3×3 grid of parallel arrows showing the directional spread.
        // Skipping (0,0) so the centerline arrow (white) stands out.
        ddraw_color_push(YELLOW);
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                if (i == 0 && j == 0) continue;
                vec3 off = add3(scale3(right, (float)i * 1.5f),
                                scale3(up,    (float)j * 1.5f));
                ddraw_arrow(add3(anchor, off),
                            add3(anchor, add3(off, scale3(dir, len))));
            }
        }
        ddraw_color(WHITE);
        ddraw_arrow(anchor, add3(anchor, scale3(dir, len * 1.4f)));

        // CSM cascade frustums (live from the last shadowmap pass).
        // `shadow_matrix[i]` is the projview for cascade i; ddraw_frustum
        // builds the world-space box via inverse-clip. Color-coded by index.
        if (l->cast_shadows && l->processed_shadows
                && l->shadow_technique == SHADOW_CSM) {
            unsigned colors[4] = {RED, GREEN, BLUE, ORANGE};
            for (int i = 0; i < NUM_SHADOW_CASCADES; i++) {
                ddraw_color(colors[i & 3]);
                ddraw_frustum(l->shadow_matrix[i]);
            }
        }
        ddraw_color_pop();
    }
    else if (l->type == LIGHT_POINT) {
        float r = l->shadow_distance > 0.0f ? l->shadow_distance : 5.0f;

        ddraw_color_push(YELLOW);
        ddraw_sphere(l->pos, r);

        // RGB axis cross at the position (5% of sphere radius).
        float m = r * 0.05f;
        if (m < 0.05f) m = 0.05f;
        ddraw_color(RED);
        ddraw_line(sub3(l->pos, vec3(m,0,0)), add3(l->pos, vec3(m,0,0)));
        ddraw_color(GREEN);
        ddraw_line(sub3(l->pos, vec3(0,m,0)), add3(l->pos, vec3(0,m,0)));
        ddraw_color(BLUE);
        ddraw_line(sub3(l->pos, vec3(0,0,m)), add3(l->pos, vec3(0,0,m)));
        ddraw_color_pop();
    }
    else if (l->type == LIGHT_SPOT) {
        if (!has_dir) return;
        vec3 dir = norm3(l->dir);
        float dist = l->shadow_distance > 0.0f ? l->shadow_distance : 10.0f;

        // Engine semantics: innerCone / outerCone store COSINES of the
        // half-angles, and the shader fades attenuation linearly between
        // them (angle > innerCone → start fading in, > outerCone → fully on).
        // For viz we render BOTH cones — the wider one (= smaller cosine) in
        // YELLOW, the narrower one in ORANGE. We sort the cosines so we
        // tolerate either field-ordering convention.
        float cos_wider    = minf(l->innerCone, l->outerCone);
        float cos_narrower = maxf(l->innerCone, l->outerCone);

        ddraw_light_spot_cone_(l->pos, dir, dist, cos_wider,    YELLOW, 12);
        if (absf(cos_narrower - cos_wider) > 1e-3f) {
            ddraw_light_spot_cone_(l->pos, dir, dist, cos_narrower, ORANGE, 12);
        }

        // Short forward arrow at the apex so the spot's facing is obvious.
        ddraw_color_push(WHITE);
        ddraw_arrow(l->pos, add3(l->pos, scale3(dir, dist * 0.2f)));
        ddraw_color_pop();
    }
}

void ddraw_lights(unsigned num_lights, light_t *lights) {
    for (unsigned i = 0; i < num_lights; ++i) {
        ddraw_light(&lights[i]);
    }
}

#endif
