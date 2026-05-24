// render_model_gltf.h
// GLTF / GLB loader — cgltf-based parser → iqm_t-shaped struct, so the
// existing model_render/animate pipeline stays unchanged.
//
// Step 2: static mesh loader (geometry + checker placeholder material).
// Step 3+: material/PBR + skinning + animations (later steps).
//
// Planned scope:
//   - Vertex format: the existing 68-byte iqm_vertex (position/texcoord/normal/
//     tangent/blendindexes/blendweights/blendvertexindex/color/texcoord2).
//   - Multi-mesh / multi-primitive GLTF — one combined VBO + IBO + per-mesh
//     iqmmesh record (first_vertex/num_vertexes/first_triangle/num_triangles).
//   - GLB (single-file binary) works now; JSON-only .gltf (with external .bin)
//     requires extending model_from_mem's signature — later step.

#pragma once
#if CODE

// Single "checker" placeholder material (fallback when GLTF has no material).
static
void model_load_textures_gltf_stub(iqm_t *q, model_t *m) {
    (void)q;
    material_t mt = {0};
    mt.name = "gltf_placeholder";
    mt.layer[0].map.color = vec4(1, 1, 1, 1);
    mt.layer[0].map.texture = CALLOC(1, sizeof(texture_t));
    mt.layer[0].map.texture->id = texture_checker().id;
    mt._loaded = true;  // skip model_load_pbr string-pattern matching
    mt.enable_ibl = true;
    mt.enable_shading = true;
    mt.base_reflectivity = vec3(0.04f, 0.04f, 0.04f);
    array_push(m->materials, mt);
}

// ---- Step 3: GLTF material → material_t PBR-mapping ----

// Replicates the default-init block at lines 987-1000 of model_load_pbr. The
// GLTF loader runs this manually because we set `_loaded=true` on the
// material (otherwise model_setshader would overwrite our layers).
static
void gltf_init_material_defaults(material_t *mt) {
    mt->layer[MATERIAL_CHANNEL_NORMALS].map.color   = vec4(0,0,0,0);
    mt->layer[MATERIAL_CHANNEL_ROUGHNESS].map.color = vec4(1,1,1,1);
    mt->layer[MATERIAL_CHANNEL_METALLIC].map.color  = vec4(0,0,0,0);
    mt->layer[MATERIAL_CHANNEL_AO].map.color        = vec4(1,1,1,0);
    mt->layer[MATERIAL_CHANNEL_AMBIENT].map.color   = vec4(0,0,0,1);
    mt->layer[MATERIAL_CHANNEL_EMISSIVE].map.color  = vec4(0,0,0,0);
    mt->layer[MATERIAL_CHANNEL_PARALLAX].map.color  = vec4(0,0,0,0);
    mt->layer[MATERIAL_CHANNEL_EMISSIVE].value      = 1.0f;
    mt->layer[MATERIAL_CHANNEL_PARALLAX].value      = 0.1f;
    mt->layer[MATERIAL_CHANNEL_PARALLAX].value2     = 4.0f;
    mt->enable_ibl       = true;
    mt->enable_shading   = true;
    mt->base_reflectivity = vec3(0.04f, 0.04f, 0.04f);
}

// texture_t from an embedded buffer-view. External JSON-only .gltf image (uri)
// is currently NOT supported — model_from_mem has no base_dir. Data-uris
// (data:image/png;base64,...) work because cgltf_load_buffers handles them.
static
texture_t gltf_load_image(cgltf_image *img, int flags) {
    if (!img) return texture_checker();
    if (img->buffer_view && img->buffer_view->buffer && img->buffer_view->buffer->data) {
        const uint8_t *data = (const uint8_t*)img->buffer_view->buffer->data
                            + img->buffer_view->offset;
        int len = (int)img->buffer_view->size;
        texture_t t = texture_compressed_from_mem(data, len, flags);
        return t;
    }
    if (img->uri) {
        PRINTF("GLTF: external image '%s' not supported (only embedded/GLB-binary)\n",
               img->uri);
    }
    return texture_checker();
}

// Unpack the MR-packed image: G channel → roughness, B channel → metallic.
// Returns two R8 textures (compatible with the existing shader).
// If decoding fails, both outputs become the checker texture.
static
void gltf_split_mr_image(cgltf_image *img,
                         texture_t *out_roughness, texture_t *out_metallic) {
    *out_roughness = texture_checker();
    *out_metallic  = texture_checker();
    if (!img || !img->buffer_view || !img->buffer_view->buffer
        || !img->buffer_view->buffer->data) return;

    const uint8_t *raw = (const uint8_t*)img->buffer_view->buffer->data
                       + img->buffer_view->offset;
    int len = (int)img->buffer_view->size;

    int w = 0, h = 0, comp = 0;
    stbi_uc *rgba = stbi_load_from_memory(raw, len, &w, &h, &comp, 4);
    if (!rgba) {
        PRINTF("GLTF MR-split: stbi_load_from_memory failed\n");
        return;
    }
    int n = w * h;
    uint8_t *rough = (uint8_t*)MALLOC(n);
    uint8_t *metal = (uint8_t*)MALLOC(n);
    for (int i = 0; i < n; i++) {
        rough[i] = rgba[i*4 + 1];  // G
        metal[i] = rgba[i*4 + 2];  // B
    }
    unsigned tflags = TEXTURE_LINEAR | TEXTURE_REPEAT | TEXTURE_MIPMAPS;
    *out_roughness = texture_create((unsigned)w, (unsigned)h, 1, rough, tflags);
    *out_metallic  = texture_create((unsigned)w, (unsigned)h, 1, metal, tflags);
    FREE(rough);
    FREE(metal);
    stbi_image_free(rgba);
}

// Structured `cgltf_material` → `material_t` mapping.
static
void model_load_materials_gltf(cgltf_data *data, model_t *m) {
    // If there is no material → fallback placeholder.
    if (data->materials_count == 0) {
        model_load_textures_gltf_stub(m->iqm, m);
        return;
    }

    for (size_t i = 0; i < data->materials_count; i++) {
        cgltf_material *gm = &data->materials[i];

        material_t mt = {0};
        mt.name = STRDUP(gm->name ? gm->name : "gltf_material");
        gltf_init_material_defaults(&mt);

        unsigned tflags = TEXTURE_LINEAR | TEXTURE_REPEAT | TEXTURE_MIPMAPS
                        | TEXTURE_ANISOTROPY;

        // ---- ALBEDO (base color) ----
        if (gm->has_pbr_metallic_roughness) {
            cgltf_pbr_metallic_roughness *pbr = &gm->pbr_metallic_roughness;
            mt.layer[MATERIAL_CHANNEL_ALBEDO].map.color = vec4(
                pbr->base_color_factor[0], pbr->base_color_factor[1],
                pbr->base_color_factor[2], pbr->base_color_factor[3]);
            if (pbr->base_color_texture.texture && pbr->base_color_texture.texture->image) {
                texture_t t = gltf_load_image(pbr->base_color_texture.texture->image,
                                              tflags | TEXTURE_SRGB);
                if (t.id) {
                    mt.layer[MATERIAL_CHANNEL_ALBEDO].map.texture = CALLOC(1, sizeof(texture_t));
                    *mt.layer[MATERIAL_CHANNEL_ALBEDO].map.texture = t;
                }
            }
            // ---- METALLIC + ROUGHNESS (packed: G=rough, B=metal) ----
            mt.layer[MATERIAL_CHANNEL_ROUGHNESS].map.color = vec4(
                pbr->roughness_factor, pbr->roughness_factor, pbr->roughness_factor, 1);
            mt.layer[MATERIAL_CHANNEL_METALLIC].map.color = vec4(
                pbr->metallic_factor, pbr->metallic_factor, pbr->metallic_factor, 1);
            if (pbr->metallic_roughness_texture.texture
                && pbr->metallic_roughness_texture.texture->image) {
                texture_t tr, tm;
                gltf_split_mr_image(pbr->metallic_roughness_texture.texture->image, &tr, &tm);
                if (tr.id && tr.id != texture_checker().id) {
                    mt.layer[MATERIAL_CHANNEL_ROUGHNESS].map.texture = CALLOC(1, sizeof(texture_t));
                    *mt.layer[MATERIAL_CHANNEL_ROUGHNESS].map.texture = tr;
                }
                if (tm.id && tm.id != texture_checker().id) {
                    mt.layer[MATERIAL_CHANNEL_METALLIC].map.texture = CALLOC(1, sizeof(texture_t));
                    *mt.layer[MATERIAL_CHANNEL_METALLIC].map.texture = tm;
                }
            }
        }

        // ---- NORMAL ----
        if (gm->normal_texture.texture && gm->normal_texture.texture->image) {
            texture_t t = gltf_load_image(gm->normal_texture.texture->image, tflags);
            if (t.id) {
                mt.layer[MATERIAL_CHANNEL_NORMALS].map.texture = CALLOC(1, sizeof(texture_t));
                *mt.layer[MATERIAL_CHANNEL_NORMALS].map.texture = t;
            }
        }

        // ---- OCCLUSION (AO) ----
        if (gm->occlusion_texture.texture && gm->occlusion_texture.texture->image) {
            texture_t t = gltf_load_image(gm->occlusion_texture.texture->image, tflags);
            if (t.id) {
                mt.layer[MATERIAL_CHANNEL_AO].map.texture = CALLOC(1, sizeof(texture_t));
                *mt.layer[MATERIAL_CHANNEL_AO].map.texture = t;
            }
        }

        // ---- EMISSIVE ----
        mt.layer[MATERIAL_CHANNEL_EMISSIVE].map.color = vec4(
            gm->emissive_factor[0], gm->emissive_factor[1], gm->emissive_factor[2], 1);
        if (gm->emissive_texture.texture && gm->emissive_texture.texture->image) {
            texture_t t = gltf_load_image(gm->emissive_texture.texture->image,
                                          tflags | TEXTURE_SRGB);
            if (t.id) {
                mt.layer[MATERIAL_CHANNEL_EMISSIVE].map.texture = CALLOC(1, sizeof(texture_t));
                *mt.layer[MATERIAL_CHANNEL_EMISSIVE].map.texture = t;
            }
        }

        // ---- Alpha-mode ----
        if (gm->alpha_mode == cgltf_alpha_mode_mask) {
            mt.cutout_alpha = gm->alpha_cutoff;
        }

        // _loaded=true: skip model_setshader's string-pattern model_load_pbr.
        mt._loaded = true;

        array_push(m->materials, mt);
    }
}

// 4x4 column-major matrix × vec3 (homogeneous w=1) — position-transform.
static inline void gltf_mul_pos(const float m[16], const float v[3], float out[3]) {
    out[0] = m[0]*v[0] + m[4]*v[1] + m[ 8]*v[2] + m[12];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[ 9]*v[2] + m[13];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
}

// 3x3 sub-matrix × vec3 (direction-transform, NO translation) — for normals.
static inline void gltf_mul_dir(const float m[16], const float v[3], float out[3]) {
    out[0] = m[0]*v[0] + m[4]*v[1] + m[ 8]*v[2];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[ 9]*v[2];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2];
}

// 3x3 sub-matrix determinant. If < 0 the winding must be inverted
// (negative scale on the node, e.g. mirror-export).
static inline float gltf_mat3_det(const float m[16]) {
    return m[0]*(m[5]*m[10] - m[6]*m[9])
         - m[4]*(m[1]*m[10] - m[2]*m[9])
         + m[8]*(m[1]*m[6]  - m[2]*m[5]);
}

// We iterate cgltf_nodes (NOT meshes[]), so the node-level TRS and the
// hierarchical parent-transform are also accounted for. cgltf_node_transform_world
// gives the full world matrix.
// NOTE: `q` is passed in, because model_from_mem's `m.iqm = q` runs only in
// the tail block (after a successful load). The loader is called before that.
static
bool model_load_meshes_gltf(cgltf_data *data, iqm_t *q, model_t *m, int flags) {
    (void)flags;

    // ---- 1) total-vertex / total-triangle / total-primitive count --------
    // Node-level iteration: every node that has a mesh (in instancing the same
    // mesh pointer can appear multiple times — each instance loaded separately).
    // Non-indexed primitives (no `indices` accessor) are supported as implicit
    // triangle-lists where vertex 3k/3k+1/3k+2 form triangle k. Fox.glb is a
    // common example.
    int total_verts = 0, total_tris = 0, total_prims = 0;
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;
        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive *p = &node->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            cgltf_accessor *pos = NULL;
            for (size_t ai = 0; ai < p->attributes_count; ai++) {
                if (p->attributes[ai].type == cgltf_attribute_type_position) {
                    pos = p->attributes[ai].data;
                    break;
                }
            }
            if (!pos) continue;
            int idx_count = p->indices ? (int)p->indices->count : (int)pos->count;
            if (idx_count < 3 || (idx_count % 3) != 0) continue;
            total_verts += (int)pos->count;
            total_tris  += idx_count / 3;
            total_prims++;
        }
    }

    if (total_verts == 0 || total_tris == 0 || total_prims == 0) {
        PRINTF("GLTF: no indexed triangle primitives (verts=%d, tris=%d, prims=%d)\n",
               total_verts, total_tris, total_prims);
        return false;
    }

    // ---- 2) iqm_t field allocation --------------------------------------
    q->nummeshes = total_prims;
    q->numverts  = total_verts;
    q->numtris   = total_tris;
    q->numjoints = 0;        // Step 4: model_load_skin_gltf fills this
    q->numframes = 0;        // Step 5: real anims (Step 4 dummy frame too)
    q->meshes          = CALLOC(total_prims, sizeof(struct iqmmesh));
    q->mesh_materials  = CALLOC(total_prims, sizeof(unsigned));
    q->bounds          = CALLOC(1, sizeof(struct iqmbounds));
    // Mark ownership so model_destroy frees them (IQM-loader points these into
    // the q->buf arena instead; see render_model.h iqm_t.external_allocs).
    // mesh_materials is unconditionally FREEd by model_destroy → no flag needed.
    q->external_allocs |= 1 /*meshes*/ | 8 /*bounds*/;

    // ---- 3) CPU-side vertex + index buffer ------------------------------
    iqm_vertex          *verts = CALLOC(total_verts, sizeof(iqm_vertex));
    struct iqmtriangle  *tris  = CALLOC(total_tris,  sizeof(struct iqmtriangle));

    int vert_base = 0, tri_base = 0, mesh_idx = 0;
    vec3 bbmin = vec3( 1e30f,  1e30f,  1e30f);
    vec3 bbmax = vec3(-1e30f, -1e30f, -1e30f);

    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;

        // World-transform (node TRS × parent chain). glTF and the v2 engine
        // both render Y-up: IQM-loader memcpy's positions verbatim
        // (render_model.h:781) and demos/13-model.c uses a near-identity
        // pivot — no Y-flip anywhere in the IQM path. We mirror that here:
        // glTF vertices go into the VBO in their native Y-up space.
        float world[16];
        cgltf_node_transform_world(node, world);

        // glTF 2.0 spec: when a node has a skin, its TRS properties have NO
        // EFFECT — the joints fully determine the vertex positions. We use a
        // different world here for skinned nodes: the world transform of the
        // skin's above-skin root (= skin.joints[0]->parent). This brings the
        // mesh-local vertices into the same "skin coordinate system" that the
        // skin-internal baseframe[]/frames[] live in, so vsBoneMatrix * att_pos
        // comes out in a coherent space (which the render pipeline then takes
        // as world-space).
        //
        // Without this, Fox + RiggedSimple bake Z-up→Y-up rotations into their
        // skeleton root (`_rootJoint`/`b_Root_00`/`Armature`) but those rotations
        // are missed by our skin-internal Pass-2 parent-chain (the parent isn't
        // in skin.joints[]), so the mesh ends up upside-down or 90°-leaned.
        if (node->skin) {
            cgltf_node *above_skin = (node->skin->joints_count > 0)
                                   ? node->skin->joints[0]->parent
                                   : NULL;
            if (above_skin) {
                cgltf_node_transform_world(above_skin, world);
            } else {
                for (int k = 0; k < 16; k++) world[k] = 0.0f;
                world[0] = world[5] = world[10] = world[15] = 1.0f;  // identity
            }
        }

        // Y-flip policy:
        //   - STATIC mesh: bake the Y-flip into the vertex bind here so the
        //     MeshRenderer pivot can stay identity (IQM convention).
        //   - SKINNED mesh: SKIP the Y-flip — the vertex stays in glTF-native
        //     Y-up space, the same basis baseframe[]/vsBoneMatrix live in.
        //     The editor render-walk compensates by applying a Y-flip to the
        //     model's pivot (gated on MODEL_GLTF_SKINNED, set below).
        //     This keeps skinning math algebraically consistent under any
        //     animation delta, not just the small-delta common case.
        if (!node->skin) {
            world[1]  = -world[1];
            world[5]  = -world[5];
            world[9]  = -world[9];
            world[13] = -world[13];
        }

        // Winding-flip: the Y-flip above contributes a negative determinant,
        // so this flag toggles for normal-orientation nodes (and toggles back
        // off for nodes with an additional mirror in their TRS).
        bool flip_winding = (gltf_mat3_det(world) < 0.0f);

        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive *p = &node->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;

            cgltf_accessor *acc_pos = NULL, *acc_nrm = NULL, *acc_uv = NULL;
            cgltf_accessor *acc_col = NULL, *acc_tan = NULL;
            cgltf_accessor *acc_joints = NULL, *acc_weights = NULL;  // Step 4
            for (size_t ai = 0; ai < p->attributes_count; ai++) {
                cgltf_attribute *attr = &p->attributes[ai];
                switch (attr->type) {
                    case cgltf_attribute_type_position: acc_pos = attr->data; break;
                    case cgltf_attribute_type_normal:   acc_nrm = attr->data; break;
                    case cgltf_attribute_type_texcoord: if (!acc_uv)  acc_uv  = attr->data; break;
                    case cgltf_attribute_type_color:    if (!acc_col) acc_col = attr->data; break;
                    case cgltf_attribute_type_tangent:  acc_tan = attr->data; break;
                    case cgltf_attribute_type_joints:   if (!acc_joints)  acc_joints  = attr->data; break;
                    case cgltf_attribute_type_weights:  if (!acc_weights) acc_weights = attr->data; break;
                    default: break;
                }
            }
            if (!acc_pos) continue;

            int nv = (int)acc_pos->count;
            int idx_count = p->indices ? (int)p->indices->count : nv;
            if (idx_count < 3 || (idx_count % 3) != 0) continue;
            int nt = idx_count / 3;

            // ---- Vertex-loop (vertex attributes with world-transform) ----
            for (int v = 0; v < nv; v++) {
                iqm_vertex *iv = &verts[vert_base + v];

                // position (mandatory) — with world-transform
                float pos_raw[3] = {0, 0, 0};
                float pos_w[3];
                cgltf_accessor_read_float(acc_pos, v, pos_raw, 3);
                gltf_mul_pos(world, pos_raw, pos_w);
                iv->position[0] = pos_w[0];
                iv->position[1] = pos_w[1];
                iv->position[2] = pos_w[2];
                if (pos_w[0] < bbmin.x) bbmin.x = pos_w[0];
                if (pos_w[0] > bbmax.x) bbmax.x = pos_w[0];
                if (pos_w[1] < bbmin.y) bbmin.y = pos_w[1];
                if (pos_w[1] > bbmax.y) bbmax.y = pos_w[1];
                if (pos_w[2] < bbmin.z) bbmin.z = pos_w[2];
                if (pos_w[2] > bbmax.z) bbmax.z = pos_w[2];

                // normal (optional, default Y-up) — 3x3 direction-transform
                if (acc_nrm) {
                    float n_raw[3] = {0, 1, 0};
                    float n_w[3];
                    cgltf_accessor_read_float(acc_nrm, v, n_raw, 3);
                    gltf_mul_dir(world, n_raw, n_w);
                    // re-normalize (transform scale can distort the length)
                    float l = sqrtf(n_w[0]*n_w[0] + n_w[1]*n_w[1] + n_w[2]*n_w[2]);
                    if (l > 1e-6f) { n_w[0]/=l; n_w[1]/=l; n_w[2]/=l; }
                    iv->normal[0] = n_w[0];
                    iv->normal[1] = n_w[1];
                    iv->normal[2] = n_w[2];
                } else {
                    iv->normal[0] = 0; iv->normal[1] = 1; iv->normal[2] = 0;
                }

                // texcoord (optional, NO transform — lives in UV space)
                if (acc_uv) {
                    float uv[2] = {0, 0};
                    cgltf_accessor_read_float(acc_uv, v, uv, 2);
                    iv->texcoord[0]  = uv[0];
                    iv->texcoord[1]  = uv[1];
                    iv->texcoord2[0] = uv[0];
                    iv->texcoord2[1] = uv[1];
                }

                // tangent (optional, default (1,0,0,1)) — 3x3 direction
                // (the .w field is the bitangent sign; preserve through transform).
                if (acc_tan) {
                    float t_raw[4] = {1, 0, 0, 1};
                    float t_w[3];
                    cgltf_accessor_read_float(acc_tan, v, t_raw, 4);
                    gltf_mul_dir(world, t_raw, t_w);
                    float l = sqrtf(t_w[0]*t_w[0] + t_w[1]*t_w[1] + t_w[2]*t_w[2]);
                    if (l > 1e-6f) { t_w[0]/=l; t_w[1]/=l; t_w[2]/=l; }
                    iv->tangent[0] = t_w[0]; iv->tangent[1] = t_w[1];
                    iv->tangent[2] = t_w[2]; iv->tangent[3] = t_raw[3];
                } else {
                    iv->tangent[0] = 1; iv->tangent[1] = 0;
                    iv->tangent[2] = 0; iv->tangent[3] = 1;
                }

                // color (optional, default white) — NO transform
                if (acc_col) {
                    float c[4] = {1, 1, 1, 1};
                    cgltf_accessor_read_float(acc_col, v, c, 4);
                    iv->color[0] = c[0]; iv->color[1] = c[1];
                    iv->color[2] = c[2]; iv->color[3] = c[3];
                } else {
                    iv->color[0] = 1; iv->color[1] = 1;
                    iv->color[2] = 1; iv->color[3] = 1;
                }

                // Step 4: skinning indexes/weights (NO world-transform — they
                // are joint-space indices). cgltf_accessor_read_float
                // auto-normalizes u8/u16/f32, so we just clamp + scale to
                // the iqm_vertex uint8 fields. The shader (model_vs.glsl:78-82)
                // reads att_indexes as floats; values must be plain joint
                // indices in [0, numjoints-1].
                if (acc_joints) {
                    float j[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_float(acc_joints, v, j, 4);
                    for (int k = 0; k < 4; k++) {
                        float jc = j[k] < 0.f ? 0.f : (j[k] > 255.f ? 255.f : j[k]);
                        iv->blendindexes[k] = (uint8_t)jc;
                    }
                } else {
                    // Non-skin primitive in an otherwise-skinned model: bind
                    // every vertex to joint 0 with full weight. With the
                    // dummy 1-frame identity anim from model_load_skin_gltf,
                    // joint 0's transform is identity * inversebaseframe[0]
                    // — so the mesh renders untransformed in bind-space.
                    iv->blendindexes[0] = 0; iv->blendindexes[1] = 0;
                    iv->blendindexes[2] = 0; iv->blendindexes[3] = 0;
                }
                if (acc_weights) {
                    float w[4] = {1, 0, 0, 0};
                    cgltf_accessor_read_float(acc_weights, v, w, 4);
                    // Sum-normalize defensively (glTF spec says =1, but some
                    // exporters drift; the shader sums weighted bones).
                    float sum = w[0] + w[1] + w[2] + w[3];
                    if (sum > 1e-6f) {
                        w[0] /= sum; w[1] /= sum; w[2] /= sum; w[3] /= sum;
                    }
                    for (int k = 0; k < 4; k++) {
                        int wi = (int)(w[k] * 255.f + 0.5f);
                        if (wi < 0) wi = 0; if (wi > 255) wi = 255;
                        iv->blendweights[k] = (uint8_t)wi;
                    }
                } else {
                    iv->blendweights[0] = 255;
                    iv->blendweights[1] = 0;
                    iv->blendweights[2] = 0;
                    iv->blendweights[3] = 0;
                }
                // IQM-loader writes this with the global vertex index
                // (render_model.h:792-794); a few shaders use it for
                // per-vertex picking / debug. Keep parity.
                {
                    float vi = (float)(vert_base + v);
                    memcpy(&iv->blendvertexindex, &vi, sizeof(float));
                }
            }

            // ---- Index-loop (winding-flip if det < 0 on the node-transform) ----
            // Indexed vs. non-indexed primitive: when there are no indices,
            // the triangle list is implicit — vertex 3k/3k+1/3k+2 = triangle k.
            for (int t = 0; t < nt; t++) {
                unsigned i0, i1, i2;
                if (p->indices) {
                    i0 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 0) + vert_base;
                    i1 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 1) + vert_base;
                    i2 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 2) + vert_base;
                } else {
                    i0 = (unsigned)(vert_base + t*3 + 0);
                    i1 = (unsigned)(vert_base + t*3 + 1);
                    i2 = (unsigned)(vert_base + t*3 + 2);
                }
                if (flip_winding) {
                    // negative scale on the node → CCW ↔ CW inversion
                    unsigned tmp = i1; i1 = i2; i2 = tmp;
                }
                tris[tri_base + t].vertex[0] = i0;
                tris[tri_base + t].vertex[1] = i1;
                tris[tri_base + t].vertex[2] = i2;
            }

            // ---- iqmmesh record ----
            q->meshes[mesh_idx].name           = 0;  // no string table for GLTF
            q->meshes[mesh_idx].material       = 0;
            q->meshes[mesh_idx].first_vertex   = (unsigned)vert_base;
            q->meshes[mesh_idx].num_vertexes   = (unsigned)nv;
            q->meshes[mesh_idx].first_triangle = (unsigned)tri_base;
            q->meshes[mesh_idx].num_triangles  = (unsigned)nt;
            // Step 3: the primitive's material index (in the data->materials[] array).
            // No material → 0 (fallback placeholder).
            unsigned mat_index = 0;
            if (p->material) {
                mat_index = (unsigned)(p->material - data->materials);
            }
            q->mesh_materials[mesh_idx] = mat_index;

            vert_base += nv;
            tri_base  += nt;
            mesh_idx++;
        }
    }

    // ---- 4) Bounds ----
    q->bounds[0].min3 = bbmin;
    q->bounds[0].max3 = bbmax;
    vec3 sz = sub3(bbmax, bbmin);
    q->bounds[0].radius   = len3(sz) * 0.5f;
    q->bounds[0].xyradius = sqrtf(sz.x*sz.x + sz.y*sz.y) * 0.5f;

    // ---- 5) VAO + VBO + IBO upload ----
    glGenVertexArrays(1, &q->vao);
    glBindVertexArray(q->vao);

    glGenBuffers(1, &q->ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, q->ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 total_tris * sizeof(struct iqmtriangle), tris, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    glGenBuffers(1, &q->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, q->vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 total_verts * sizeof(iqm_vertex), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindVertexArray(0);

    m->stride   = sizeof(iqm_vertex);
    m->verts    = verts;           // CPU-side (used by transparent-sort)
    m->num_tris = total_tris;
    m->tris     = (void*)tris;     // CPU-side (transparent-sort + debug)

    return true;
}

// Step 4 — Skinning loader. Builds q->joints/baseframe/inversebaseframe/outframe
// and installs a dummy 1-frame anim that flips the SKINNED vertex-shader
// uniform on. Bit-pointed at the IQM-loader math:
//   - bind-pose parent-chain: render_model.h:748-757
//   - frames[] layout:        render_model.h:875,904
//   - SKINNED uniform gate:   render_model.h:487  (bool skinned = !!numanims)
//
// Coordinate-system note (RISK #13 in the plan):
// model_load_meshes_gltf above applies a Y-flip to every vertex world-matrix
// (render_model_gltf.h:301-304) — glTF is Y-up but the engine renders Y-down.
// To keep att_pos and vsBoneMatrix in the SAME space we apply the matching
// Y-mirror to the joint TRS here BEFORE compose34. Without this the skinning
// math lands in a different basis than the vertex and the mesh inverts.
//
// Returns true even when the file has no skin (no-op, model stays static).
static
bool model_load_skin_gltf(cgltf_data *data, iqm_t *q) {
    if (data->skins_count == 0) return true;
    if (data->skins_count > 1) {
        PRINTF("GLTF: %d skins found, only skins[0] is used\n",
               (int)data->skins_count);
    }
    cgltf_skin *skin = &data->skins[0];

    int nj = (int)skin->joints_count;
    if (nj <= 0) return true;
    if (nj > 64) {
        PRINTF("GLTF skin WARN: %d joints (>64) — may underperform on mobile\n", nj);
    }
    if (nj > 110) {
        // model_vs.glsl:7 defines MAX_BONES=110 — uniform-array overrun is hard fail.
        PRINTF("GLTF skin ERROR: %d joints exceeds shader MAX_BONES=110\n", nj);
        return false;
    }

    q->numjoints        = nj;
    q->joints           = CALLOC(nj, sizeof(struct iqmjoint));
    q->baseframe        = CALLOC(nj, sizeof(mat34));
    q->inversebaseframe = CALLOC(nj, sizeof(mat34));
    q->outframe         = CALLOC(nj, sizeof(mat34));
    q->external_allocs |= 2;   // joints owned by us (others are unconditionally FREEd)

    // 1) Parse joints — store raw glTF TRS into q->joints[] (Step 5 anim-loop
    // reads these as the fallback for missing T/R/S channels). Build the
    // Y-mirrored bind-pose into baseframe[].
    for (int i = 0; i < nj; i++) {
        cgltf_node *jn = skin->joints[i];

        float Tx = jn->has_translation ? jn->translation[0] : 0.f;
        float Ty = jn->has_translation ? jn->translation[1] : 0.f;
        float Tz = jn->has_translation ? jn->translation[2] : 0.f;
        float Rx = jn->has_rotation    ? jn->rotation[0]    : 0.f;
        float Ry = jn->has_rotation    ? jn->rotation[1]    : 0.f;
        float Rz = jn->has_rotation    ? jn->rotation[2]    : 0.f;
        float Rw = jn->has_rotation    ? jn->rotation[3]    : 1.f;
        float Sx = jn->has_scale       ? jn->scale[0]       : 1.f;
        float Sy = jn->has_scale       ? jn->scale[1]       : 1.f;
        float Sz = jn->has_scale       ? jn->scale[2]       : 1.f;

        if (jn->has_matrix) {
            // TODO: decompose jn->matrix[16] into T/R/S — uncommon in practice;
            // most exporters emit TRS-form joints.
            PRINTF("GLTF skin: joint %d has matrix-form transform, "
                   "TRS-decompose not yet supported — using identity\n", i);
            Tx = Ty = Tz = 0.f;
            Rx = Ry = Rz = 0.f; Rw = 1.f;
            Sx = Sy = Sz = 1.f;
        }

        q->joints[i].name = 0;
        q->joints[i].translate[0] = Tx; q->joints[i].translate[1] = Ty; q->joints[i].translate[2] = Tz;
        q->joints[i].rotate[0]    = Rx; q->joints[i].rotate[1]    = Ry; q->joints[i].rotate[2]    = Rz; q->joints[i].rotate[3] = Rw;
        q->joints[i].scale[0]     = Sx; q->joints[i].scale[1]     = Sy; q->joints[i].scale[2]     = Sz;

        // Parent-index lookup inside the skin->joints[] array (O(N^2), N<=110 OK).
        q->joints[i].parent = -1;
        if (jn->parent) {
            for (int p = 0; p < nj; p++) {
                if (skin->joints[p] == jn->parent) {
                    q->joints[i].parent = p;
                    break;
                }
            }
        }

        // IQM-mintára (render_model.h:752): compose local TRS into baseframe[i],
        // parent-chain in Pass 2.
        vec3 T = vec3(Tx, Ty, Tz);
        quat R = quat(Rx, Ry, Rz, Rw);
        vec3 S = vec3(Sx, Sy, Sz);

        compose34(q->baseframe[i], T, normq(R), S);
    }

    // 2) Parent-chain pass (IQM-mintára render_model.h:754).
    // Done in a second pass so a child can rely on parents being already built
    // — glTF doesn't guarantee parent-before-child order in skin->joints[].
    for (int i = 0; i < nj; i++) {
        int p = q->joints[i].parent;
        if (p >= 0 && p < nj) {
            mat34 tmp; copy34(tmp, q->baseframe[i]);
            multiply34x2(q->baseframe[i], q->baseframe[p], tmp);
        }
    }

    // 3) Inverse bind-matrices = invert(baseframe). IQM-mintára (render_model.h:753).
    for (int i = 0; i < nj; i++) {
        invert34(q->inversebaseframe[i], q->baseframe[i]);
    }

    // 4) Dummy 2-frame identity anim — Step 5 FREE's this and replaces it with
    // the real animations. Two design points:
    //
    //   a) We need numanims > 0 NOW so the vertex-shader SKINNED uniform flips
    //      on (render_model.h:487: `bool skinned = !!q->numanims`).
    //
    //   b) We need numframes >= 2 (NOT 1) because pose() in render_anim.h:75
    //      computes `distance = maxframe - minframe` and does `fmod(x, distance)`.
    //      With a single-frame clip distance == 0 → fmod is NaN → (int)NaN is
    //      undefined behavior on MSVC and crashes lerp34 inside model_animate_clip.
    //      model_from_mem calls model_animate(m, 0) on every load, so this would
    //      SIGSEGV at load time. Two frames give distance = 1 → fmod is safe.
    //
    //   c) Each frame is plain identity (NOT baseframe[parent] * id * inversebase[j]).
    //      The IQM math is `outframe[i] = outframe[parent] * frames[i]`, so if
    //      frames[*] = identity then outframe[*] = identity recursively, and the
    //      vertex shader's vsBoneMatrix[bone] * att_pos leaves att_pos in its
    //      bind-pose model-space — which is exactly the correct static render.
    //      (Using the IQM-style `baseframe[parent]*id*inversebase[j]` pattern
    //      would project every vertex into joint-local space and produce a
    //      collapsed mess.)
    q->numanims  = 1;
    q->numframes = 2;
    q->anims  = CALLOC(1, sizeof(struct iqmanim));
    q->anims[0].name        = 0;
    q->anims[0].first_frame = 0;
    q->anims[0].num_frames  = 2;
    q->anims[0].framerate   = 30.f;
    q->anims[0].flags       = IQM_LOOP;
    q->external_allocs |= 4;   // anims owned by us

    q->frames = CALLOC(q->numframes * nj, sizeof(mat34));
    for (int f = 0; f < q->numframes; f++) {
        for (int j = 0; j < nj; j++) {
            id34(q->frames[f * nj + j]);
        }
    }
    // q->frames is always FREE'd by model_destroy unconditionally (IQM-loader
    // also CALLOCs it at render_model.h:875), so no external_allocs bit.

    return true;
}

// ---- Step 5 — Animation loader (LINEAR, fixed 30 Hz resampling) -----------

// Returns the latest keyframe-time across all samplers of an animation.
// glTF doesn't store a duration field; we derive it from sampler->input[].
static
float gltf_anim_max_time(const cgltf_animation *a) {
    float max_t = 0.f;
    for (size_t s = 0; s < a->samplers_count; s++) {
        cgltf_accessor *t_acc = a->samplers[s].input;
        if (!t_acc || t_acc->count == 0) continue;
        float t_last = 0.f;
        cgltf_accessor_read_float(t_acc, t_acc->count - 1, &t_last, 1);
        if (t_last > max_t) max_t = t_last;
    }
    return max_t;
}

// Binary-search the keyframe-pair bracketing t_query. O(log N) per query.
// alpha = (t_query - t[k0]) / (t[k1] - t[k0]), clamped to keyframe boundaries.
// For single-keyframe samplers returns (0, 0, 0). For t outside the range
// snaps to the boundary keyframe with alpha=0 (no extrapolation).
static
void gltf_find_keyframe_pair(cgltf_accessor *t_acc, float t_query,
                             int *k0, int *k1, float *alpha) {
    int n = (int)t_acc->count;
    if (n <= 1) { *k0 = 0; *k1 = 0; *alpha = 0.f; return; }

    float t_first, t_last;
    cgltf_accessor_read_float(t_acc, 0,     &t_first, 1);
    cgltf_accessor_read_float(t_acc, n - 1, &t_last,  1);
    if (t_query <= t_first) { *k0 = 0;     *k1 = 0;     *alpha = 0.f; return; }
    if (t_query >= t_last)  { *k0 = n - 1; *k1 = n - 1; *alpha = 0.f; return; }

    // Bisect: find lo such that t_acc[lo] <= t_query < t_acc[lo+1].
    int lo = 0, hi = n - 1;
    while (lo + 1 < hi) {
        int mid = (lo + hi) >> 1;
        float t_mid;
        cgltf_accessor_read_float(t_acc, mid, &t_mid, 1);
        if (t_mid <= t_query) lo = mid;
        else                  hi = mid;
    }
    *k0 = lo; *k1 = lo + 1;
    float t0, t1;
    cgltf_accessor_read_float(t_acc, *k0, &t0, 1);
    cgltf_accessor_read_float(t_acc, *k1, &t1, 1);
    *alpha = (t1 > t0) ? (t_query - t0) / (t1 - t0) : 0.f;
}

// Step 5 — Replaces the dummy 1-anim from model_load_skin_gltf with all
// cgltf_animations sampled at a fixed 30 Hz. Layout is bit-identical to the
// IQM-loader's frames[] (render_model.h:875,904), so model_animate_clip works
// unchanged. Anim-names are NOT stored (the API is frame-index-based; Lua
// users address clips via q->anims[i].first_frame/num_frames).
//
// Per-anim memory: num_frames * num_joints * sizeof(mat34). 30 Hz fits typical
// glTF cycles (Fox: 87 frames, CesiumMan: 61 frames, BrainStem: ~256 frames).
//
// Coordinate-system note: same Y-mirror conjugation as the bind-pose pass in
// model_load_skin_gltf — TRS Y-mirror BEFORE compose34, so the sampled
// per-frame pose lives in the same Y-flipped space as att_pos.
//
// Interpolation policy: LINEAR only. STEP samplers treat as LINEAR
// (negligible visual diff at 30 Hz). CUBICSPLINE is skipped channel-wise
// (incompatible accessor layout — outputs are 3× the keyframe count).
//
// Returns true if there are no animations (no-op) or all animations loaded OK.
static
bool model_load_anims_gltf(cgltf_data *data, iqm_t *q) {
    if (data->animations_count == 0) return true;
    if (q->numjoints == 0)            return true;
    if (data->skins_count == 0)       return true;
    cgltf_skin *skin = &data->skins[0];

    // Tear down the dummy 1-anim from Step 4 (model_load_skin_gltf).
    // anims was CALLOC'd with external_allocs bit 4; frames is unconditional FREE.
    FREE(q->anims);  q->anims  = NULL;
    FREE(q->frames); q->frames = NULL;

    int na = (int)data->animations_count;
    q->anims = CALLOC(na, sizeof(struct iqmanim));   // bit 4 already set
    q->numanims = na;

    // Pass 1 — fill iqmanim headers, sum total_frames.
    int total_frames = 0;
    for (int a = 0; a < na; a++) {
        float max_t = gltf_anim_max_time(&data->animations[a]);
        int nf = (int)ceilf(max_t * 30.f) + 1;
        if (nf < 2) nf = 2;   // pose() safety: distance >= 1
        q->anims[a].name        = 0;    // no string table for GLTF
        q->anims[a].first_frame = (unsigned)total_frames;
        q->anims[a].num_frames  = (unsigned)nf;
        q->anims[a].framerate   = 30.f;
        q->anims[a].flags       = IQM_LOOP;
        total_frames += nf;
    }
    q->numframes = total_frames;
    q->frames    = CALLOC((size_t)total_frames * (size_t)q->numjoints, sizeof(mat34));

    int step_warned = 0, cubic_warned = 0;

    // Pass 2 — sample per-frame per-joint TRS, compose into frames[].
    for (int a = 0; a < na; a++) {
        cgltf_animation *anim = &data->animations[a];
        int first = (int)q->anims[a].first_frame;
        int nf    = (int)q->anims[a].num_frames;
        float max_t = gltf_anim_max_time(anim);

        for (int f = 0; f < nf; f++) {
            float t_query = (float)f / 30.f;
            if (t_query > max_t) t_query = max_t;

            for (int j = 0; j < q->numjoints; j++) {
                cgltf_node *jn = skin->joints[j];

                // Start from this joint's bind-pose TRS (Step 4 stored these
                // in q->joints[j] as raw glTF-native values, not Y-mirrored).
                float Tx = q->joints[j].translate[0];
                float Ty = q->joints[j].translate[1];
                float Tz = q->joints[j].translate[2];
                float Rx = q->joints[j].rotate[0];
                float Ry = q->joints[j].rotate[1];
                float Rz = q->joints[j].rotate[2];
                float Rw = q->joints[j].rotate[3];
                float Sx = q->joints[j].scale[0];
                float Sy = q->joints[j].scale[1];
                float Sz = q->joints[j].scale[2];

                // Scan all channels that target this joint, apply T/R/S overrides.
                for (int c = 0; c < (int)anim->channels_count; c++) {
                    cgltf_animation_channel *ch = &anim->channels[c];
                    if (ch->target_node != jn) continue;
                    cgltf_animation_sampler *sam = ch->sampler;
                    if (!sam || !sam->input || !sam->output) continue;

                    if (sam->interpolation == cgltf_interpolation_type_cubic_spline) {
                        // CUBICSPLINE output has 3× the keyframe count (in, value, out
                        // tangents). Reading naively would mis-align — skip channel.
                        if (!cubic_warned) {
                            PRINTF("GLTF anim WARN: CUBICSPLINE interpolation not "
                                   "supported, channel skipped\n");
                            cubic_warned = 1;
                        }
                        continue;
                    }
                    if (sam->interpolation == cgltf_interpolation_type_step) {
                        if (!step_warned) {
                            PRINTF("GLTF anim WARN: STEP interpolation treated as LINEAR\n");
                            step_warned = 1;
                        }
                    }

                    int k0, k1; float alpha;
                    gltf_find_keyframe_pair(sam->input, t_query, &k0, &k1, &alpha);

                    switch (ch->target_path) {
                        case cgltf_animation_path_type_translation: {
                            float v0[3] = {Tx, Ty, Tz};
                            float v1[3] = {Tx, Ty, Tz};
                            cgltf_accessor_read_float(sam->output, k0, v0, 3);
                            cgltf_accessor_read_float(sam->output, k1, v1, 3);
                            Tx = v0[0] + (v1[0] - v0[0]) * alpha;
                            Ty = v0[1] + (v1[1] - v0[1]) * alpha;
                            Tz = v0[2] + (v1[2] - v0[2]) * alpha;
                        } break;
                        case cgltf_animation_path_type_rotation: {
                            float v0[4] = {Rx, Ry, Rz, Rw};
                            float v1[4] = {Rx, Ry, Rz, Rw};
                            cgltf_accessor_read_float(sam->output, k0, v0, 4);
                            cgltf_accessor_read_float(sam->output, k1, v1, 4);
                            // mixq is sign-flip-safe NLERP (game_math.h:502 negates
                            // 'a' when dotq(a,b) < 0). Output is already normalized.
                            quat r = mixq(quat(v0[0], v0[1], v0[2], v0[3]),
                                          quat(v1[0], v1[1], v1[2], v1[3]),
                                          alpha);
                            Rx = r.x; Ry = r.y; Rz = r.z; Rw = r.w;
                        } break;
                        case cgltf_animation_path_type_scale: {
                            float v0[3] = {Sx, Sy, Sz};
                            float v1[3] = {Sx, Sy, Sz};
                            cgltf_accessor_read_float(sam->output, k0, v0, 3);
                            cgltf_accessor_read_float(sam->output, k1, v1, 3);
                            Sx = v0[0] + (v1[0] - v0[0]) * alpha;
                            Sy = v0[1] + (v1[1] - v0[1]) * alpha;
                            Sz = v0[2] + (v1[2] - v0[2]) * alpha;
                        } break;
                        default: break;  // weights / morph: out-of-scope
                    }
                }

                // No Y-mirror — bind-pose and anim TRS both live in the
                // glTF-native Y-up space, exactly like the IQM-loader uses
                // the raw fileformat TRS values (render_model.h:903).
                vec3 T = vec3(Tx, Ty, Tz);
                quat R = quat(Rx, Ry, Rz, Rw);
                vec3 S = vec3(Sx, Sy, Sz);

                mat34 m_local; compose34(m_local, T, normq(R), S);

                // IQM-mintára (render_model.h:903-905):
                //   frames[idx] = baseframe[parent] * compose(T,R,S) * inversebaseframe[j]
                //          OR    compose(T,R,S) * inversebaseframe[j]   (root)
                int idx = (first + f) * q->numjoints + j;
                int p   = q->joints[j].parent;
                if (p >= 0) {
                    multiply34x3(q->frames[idx],
                                 q->baseframe[p], m_local, q->inversebaseframe[j]);
                } else {
                    multiply34x2(q->frames[idx], m_local, q->inversebaseframe[j]);
                }
            }
        }
    }

    return true;
}

// Main entry-point, called from `model_from_mem`. mem/len = the loaded
// GLTF/GLB blob. In GLB every buffer is contained (cgltf_parse unpacks them);
// for JSON-only .gltf the external `.bin` file is NOT loaded yet (later step).
static
int model_from_mem_gltf(model_t *m, iqm_t *q, const void *mem, int len, int flags) {
    cgltf_options options = {0};
    cgltf_data    *data    = NULL;

    cgltf_result r = cgltf_parse(&options, mem, (cgltf_size)len, &data);
    if (r != cgltf_result_success) {
        PRINTF("GLTF: cgltf_parse failed (code=%d)\n", (int)r);
        return 1;
    }

    // For GLB, cgltf_parse already loads the binary chunk into
    // `data->buffers[].data`. For JSON-only .gltf we would need
    // `cgltf_load_buffers` with a base_dir, but model_from_mem's signature
    // does not currently provide one. Let us at least enable memory-uris
    // (data:application/octet-stream;base64) — NULL base is OK for those.
    cgltf_load_buffers(&options, data, NULL);

    int error = 0;
    if (!(flags & MODEL_NO_MESHES)) {
        if (!model_load_meshes_gltf(data, q, m, flags)) error = 1;
    }
    if (!error && !(flags & MODEL_NO_TEXTURES)) {
        // Step 3: structured PBR mapping (instead of the stub).
        model_load_materials_gltf(data, m);
    }
    if (!error && !(flags & MODEL_NO_ANIMATIONS)) {
        // Step 4: skinning (joints + bind-pose + dummy 2-frame identity anim).
        // Step 5: replaces the dummy anim with the real cgltf_animations.
        // Both return true even when there is no skin / no animations (no-op).
        if (!model_load_skin_gltf(data, q)) error = 1;
        if (!error && !model_load_anims_gltf(data, q)) error = 1;
    }

    // Mark skinned-glTF so the editor render-walk applies a Y-flip on the
    // pivot. The vertex-loop above intentionally leaves skinned vertices in
    // glTF-native Y-up space (matching baseframe[]/vsBoneMatrix), so we owe
    // the pipeline one Y-flip somewhere — the per-model pivot is the
    // cleanest place that doesn't interfere with the IQM-mintán math.
    if (!error && data->skins_count > 0) {
        m->flags |= MODEL_GLTF_SKINNED;
    }

    cgltf_free(data);
    return error;
}

#endif // CODE
