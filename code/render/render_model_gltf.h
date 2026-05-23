// render_model_gltf.h
// GLTF / GLB betöltő — cgltf-alapú parser → iqm_t-szerű struct, hogy
// a meglevő model_render/animate-pipeline változatlan maradjon.
//
// Step 2: static mesh loader (geometry + checker placeholder material).
// Step 3+: material/PBR + skinning + animations (későbbi step-ekben).
//
// Tervezett scope:
//   - Vertex-formátum: a meglevő 68-byte iqm_vertex (position/texcoord/normal/
//     tangent/blendindexes/blendweights/blendvertexindex/color/texcoord2).
//   - Multi-mesh / multi-primitive GLTF — egy összevont VBO + IBO + per-mesh
//     iqmmesh rekord (first_vertex/num_vertexes/first_triangle/num_triangles).
//   - GLB (single-file binary) most működik; JSON-only .gltf (.bin external)
//     a model_from_mem signature-bővítését igényli — későbbi step.

#pragma once
#if CODE

// Egyetlen "checker" placeholder material (fallback ha nincs GLTF material).
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

// Replikálja a model_load_pbr 987-1000 sor default-init blokkját. A GLTF-
// betöltő ezt manuálisan futtatja, mert `_loaded=true`-ra állítjuk a
// material-t (különben a model_setshader felülírná a layer-jeinket).
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

// Embedded buffer-view-ből texture_t. JSON-only .gltf external image (uri)
// jelenleg NEM támogatott — a model_from_mem nincs base_dir-rel. Data-uri
// (data:image/png;base64,...) működik mert cgltf_load_buffers oldja meg.
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

// MR-packed image szétpakolás: G-csatorna → roughness, B-csatorna → metallic.
// Két R8-textúrát ad vissza (a meglévő shader-rel kompatibilis).
// Ha decode fail-elt, mindkét output checker-t kap.
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

// `cgltf_material` → `material_t` strukturált mapping.
static
void model_load_materials_gltf(cgltf_data *data, model_t *m) {
    // Ha nincs material → fallback placeholder.
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

// 4x4 column-major mátrix × vec3 (homogeneous w=1) — position-transform.
static inline void gltf_mul_pos(const float m[16], const float v[3], float out[3]) {
    out[0] = m[0]*v[0] + m[4]*v[1] + m[ 8]*v[2] + m[12];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[ 9]*v[2] + m[13];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14];
}

// 3x3 sub-mátrix × vec3 (direction-transform, NO translation) — normalokra.
static inline void gltf_mul_dir(const float m[16], const float v[3], float out[3]) {
    out[0] = m[0]*v[0] + m[4]*v[1] + m[ 8]*v[2];
    out[1] = m[1]*v[0] + m[5]*v[1] + m[ 9]*v[2];
    out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2];
}

// 3x3 sub-mátrix determináns. < 0 esetén a winding-et invertálni kell
// (negative scale a node-on, pl. mirror-export).
static inline float gltf_mat3_det(const float m[16]) {
    return m[0]*(m[5]*m[10] - m[6]*m[9])
         - m[4]*(m[1]*m[10] - m[2]*m[9])
         + m[8]*(m[1]*m[6]  - m[2]*m[5]);
}

// cgltf_node-okat iteráljuk (NEM a meshes[]-t), így a node-szintű TRS és
// hierarchikus parent-transform is beleszámol. cgltf_node_transform_world
// adja a teljes world-mátrixot.
// FIGYELEM: `q` paraméterben kapva, mert a model_from_mem `m.iqm = q` csak a
// tail-blokkban (sikeres betöltés után) fut. A loader előtte hívódik.
static
bool model_load_meshes_gltf(cgltf_data *data, iqm_t *q, model_t *m, int flags) {
    (void)flags;

    // ---- 1) össz-vertex / össz-triangle / össz-primitive számolás --------
    // Node-szintű iteráció: minden mesh-szel rendelkező node (instancing-ben
    // ugyanaz a mesh-pointer többször előfordulhat — minden példányt külön
    // tölt be).
    int total_verts = 0, total_tris = 0, total_prims = 0;
    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;
        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive *p = &node->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            if (!p->indices) continue;
            cgltf_accessor *pos = NULL;
            for (size_t ai = 0; ai < p->attributes_count; ai++) {
                if (p->attributes[ai].type == cgltf_attribute_type_position) {
                    pos = p->attributes[ai].data;
                    break;
                }
            }
            if (!pos) continue;
            total_verts += (int)pos->count;
            total_tris  += (int)(p->indices->count / 3);
            total_prims++;
        }
    }

    if (total_verts == 0 || total_tris == 0 || total_prims == 0) {
        PRINTF("GLTF: no indexed triangle primitives (verts=%d, tris=%d, prims=%d)\n",
               total_verts, total_tris, total_prims);
        return false;
    }

    // ---- 2) iqm_t mező-alloc --------------------------------------------
    q->nummeshes = total_prims;
    q->numverts  = total_verts;
    q->numtris   = total_tris;
    q->numjoints = 0;        // Step 4: skinning
    q->numframes = 0;        // Step 5: animations
    q->meshes          = CALLOC(total_prims, sizeof(struct iqmmesh));
    q->mesh_materials  = CALLOC(total_prims, sizeof(unsigned));
    q->bounds          = CALLOC(1, sizeof(struct iqmbounds));

    // ---- 3) CPU-side vertex + index buffer ------------------------------
    iqm_vertex          *verts = CALLOC(total_verts, sizeof(iqm_vertex));
    struct iqmtriangle  *tris  = CALLOC(total_tris,  sizeof(struct iqmtriangle));

    int vert_base = 0, tri_base = 0, mesh_idx = 0;
    vec3 bbmin = vec3( 1e30f,  1e30f,  1e30f);
    vec3 bbmax = vec3(-1e30f, -1e30f, -1e30f);

    for (size_t ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh) continue;

        // World-transform (node TRS × parent chain).
        float world[16];
        cgltf_node_transform_world(node, world);

        // Y-flip: a motor IQM-konvenciója Y-down a vertex-koordinátarendszer-
        // ben (a render-pipeline valahol invertálja a Y-tengelyt). A glTF
        // Y-up vertex-eit erre kell tükrözni. Ezt egy pre-multiply Y-mirror-
        // mátrix-szal érjük el a world-on. A negatív det automatikusan
        // triggereli a winding-flippet.
        world[1]  = -world[1];   // sor 1 (Y row)
        world[5]  = -world[5];
        world[9]  = -world[9];
        world[13] = -world[13];

        bool flip_winding = (gltf_mat3_det(world) < 0.0f);

        for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_primitive *p = &node->mesh->primitives[pi];
            if (p->type != cgltf_primitive_type_triangles) continue;
            if (!p->indices) continue;

            cgltf_accessor *acc_pos = NULL, *acc_nrm = NULL, *acc_uv = NULL;
            cgltf_accessor *acc_col = NULL, *acc_tan = NULL;
            for (size_t ai = 0; ai < p->attributes_count; ai++) {
                cgltf_attribute *attr = &p->attributes[ai];
                switch (attr->type) {
                    case cgltf_attribute_type_position: acc_pos = attr->data; break;
                    case cgltf_attribute_type_normal:   acc_nrm = attr->data; break;
                    case cgltf_attribute_type_texcoord: if (!acc_uv)  acc_uv  = attr->data; break;
                    case cgltf_attribute_type_color:    if (!acc_col) acc_col = attr->data; break;
                    case cgltf_attribute_type_tangent:  acc_tan = attr->data; break;
                    default: break;
                }
            }
            if (!acc_pos) continue;

            int nv = (int)acc_pos->count;
            int nt = (int)(p->indices->count / 3);

            // ---- Vertex-loop (vertex-attribute-ok world-transform-mal) ----
            for (int v = 0; v < nv; v++) {
                iqm_vertex *iv = &verts[vert_base + v];

                // position (mandatory) — world-transform-mal
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
                    // re-normalize (a transform skálával eltorzíthatja a hosszt)
                    float l = sqrtf(n_w[0]*n_w[0] + n_w[1]*n_w[1] + n_w[2]*n_w[2]);
                    if (l > 1e-6f) { n_w[0]/=l; n_w[1]/=l; n_w[2]/=l; }
                    iv->normal[0] = n_w[0];
                    iv->normal[1] = n_w[1];
                    iv->normal[2] = n_w[2];
                } else {
                    iv->normal[0] = 0; iv->normal[1] = 1; iv->normal[2] = 0;
                }

                // texcoord (optional, NO transform — UV-térben él)
                if (acc_uv) {
                    float uv[2] = {0, 0};
                    cgltf_accessor_read_float(acc_uv, v, uv, 2);
                    iv->texcoord[0]  = uv[0];
                    iv->texcoord[1]  = uv[1];
                    iv->texcoord2[0] = uv[0];
                    iv->texcoord2[1] = uv[1];
                }

                // tangent (optional, default (1,0,0,1)) — 3x3 direction
                // (a .w mező a bitangent-jel; megőrzendő a transform-on át).
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
            }

            // ---- Index-loop (winding-flip ha det < 0 a node-transform-on) ----
            for (int t = 0; t < nt; t++) {
                unsigned i0 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 0) + vert_base;
                unsigned i1 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 1) + vert_base;
                unsigned i2 = (unsigned)cgltf_accessor_read_index(p->indices, t*3 + 2) + vert_base;
                if (flip_winding) {
                    // negatív scale a node-on → CCW ↔ CW invertálás
                    unsigned tmp = i1; i1 = i2; i2 = tmp;
                }
                tris[tri_base + t].vertex[0] = i0;
                tris[tri_base + t].vertex[1] = i1;
                tris[tri_base + t].vertex[2] = i2;
            }

            // ---- iqmmesh-rekord ----
            q->meshes[mesh_idx].name           = 0;  // no string table for GLTF
            q->meshes[mesh_idx].material       = 0;
            q->meshes[mesh_idx].first_vertex   = (unsigned)vert_base;
            q->meshes[mesh_idx].num_vertexes   = (unsigned)nv;
            q->meshes[mesh_idx].first_triangle = (unsigned)tri_base;
            q->meshes[mesh_idx].num_triangles  = (unsigned)nt;
            // Step 3: a primitive material-indexe (a data->materials[] arrayben).
            // Ha nincs material → 0 (fallback placeholder).
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
    m->verts    = verts;           // CPU-side (a transparent-sort használja)
    m->num_tris = total_tris;
    m->tris     = (void*)tris;     // CPU-side (transparent-sort + debug)

    return true;
}

// Fő entry-pont a `model_from_mem`-ből hívva. mem/len = a betöltött GLTF/GLB
// blob. GLB-ben minden buffer benne van (cgltf_parse szétpakolja); JSON-only
// .gltf-nél a `.bin` external file most NEM töltődik (későbbi step).
static
int model_from_mem_gltf(model_t *m, iqm_t *q, const void *mem, int len, int flags) {
    cgltf_options options = {0};
    cgltf_data    *data    = NULL;

    cgltf_result r = cgltf_parse(&options, mem, (cgltf_size)len, &data);
    if (r != cgltf_result_success) {
        PRINTF("GLTF: cgltf_parse failed (code=%d)\n", (int)r);
        return 1;
    }

    // GLB-esetén cgltf_parse már a binary-chunk-ot a `data->buffers[].data`-ba
    // tölti. JSON-only .gltf esetén `cgltf_load_buffers` kéne a base_dir-rel,
    // de a model_from_mem szignatúra most nem ad base_dir-t.
    // Próbáljunk legalább a memory-uri (data:application/octet-stream;base64)
    // buffer-eket bekapcsolni (NULL base = OK ezekre).
    cgltf_load_buffers(&options, data, NULL);

    int error = 0;
    if (!(flags & MODEL_NO_MESHES)) {
        if (!model_load_meshes_gltf(data, q, m, flags)) error = 1;
    }
    if (!error && !(flags & MODEL_NO_TEXTURES)) {
        // Step 3: strukturált PBR mapping (a stub helyett).
        model_load_materials_gltf(data, m);
    }

    cgltf_free(data);
    return error;
}

#endif // CODE
