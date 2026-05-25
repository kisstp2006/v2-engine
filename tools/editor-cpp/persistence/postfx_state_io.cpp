// STL FIRST (engine `obj`/`is` macro-clash).
#include <cstdio>
#include <string>
#include <unordered_set>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "postfx_state_io.h"

namespace editor::postfx_state_io {

namespace {

// Engine-defined uniform slot names — the `passfx.uniforms[16]` slots in
// render_postfx.h:213-242. These are re-bound every frame by the
// render-loop (iTime, mouse, channel resolutions, color/depth samplers),
// so persisting them is meaningless.
const std::unordered_set<std::string>& engineSlotNames() {
    static const std::unordered_set<std::string> s = {
        "iTime", "iFrame", "iWidth", "iHeight",
        "iMousex", "iMousey", "iMousez", "iMousew",
        "tex", "tex0", "tex1", "tColor", "tDiffuse", "tDepth",
        "iChannel0", "iChannel1",
        "iChannelRes0x", "iChannelRes0y",
        "iChannelRes1x", "iChannelRes1y",
    };
    return s;
}

// JSON5 string-escape — only the two characters that can appear unescaped
// in our flat content (no embedded newlines or control chars expected in
// shader uniform names).
void appendStr(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
}

void appendFloat(std::string& out, float v) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6g", v);
    out += buf;
}

void appendInt(std::string& out, int v) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", v);
    out += buf;
}

// Serialize one uniform as `"name": value` or `"name": [v0,v1,...]`.
// Returns true if the type is supported and the value was written; false
// causes the caller to skip the entry entirely (no comma confusion).
bool appendUniform(std::string& out, unsigned program,
                   const std::string& name, GLenum type) {
    GLint loc = glGetUniformLocation(program, name.c_str());
    if (loc < 0) return false;

    appendStr(out, name);
    out += ": ";
    switch (type) {
        case GL_FLOAT: {
            GLfloat v = 0;
            glGetUniformfv(program, loc, &v);
            appendFloat(out, v);
            return true;
        }
        case GL_FLOAT_VEC2: {
            GLfloat v[2] = {0,0};
            glGetUniformfv(program, loc, v);
            out += "["; appendFloat(out, v[0]);
            out += ","; appendFloat(out, v[1]);
            out += "]";
            return true;
        }
        case GL_FLOAT_VEC3: {
            GLfloat v[3] = {0,0,0};
            glGetUniformfv(program, loc, v);
            out += "["; appendFloat(out, v[0]);
            out += ","; appendFloat(out, v[1]);
            out += ","; appendFloat(out, v[2]);
            out += "]";
            return true;
        }
        case GL_FLOAT_VEC4: {
            GLfloat v[4] = {0,0,0,0};
            glGetUniformfv(program, loc, v);
            out += "["; appendFloat(out, v[0]);
            out += ","; appendFloat(out, v[1]);
            out += ","; appendFloat(out, v[2]);
            out += ","; appendFloat(out, v[3]);
            out += "]";
            return true;
        }
        case GL_INT:
        case GL_BOOL: {
            GLint v = 0;
            glGetUniformiv(program, loc, &v);
            appendInt(out, v);
            return true;
        }
        case GL_INT_VEC2: {
            GLint v[2] = {0,0};
            glGetUniformiv(program, loc, v);
            out += "["; appendInt(out, v[0]);
            out += ","; appendInt(out, v[1]);
            out += "]";
            return true;
        }
        case GL_INT_VEC3: {
            GLint v[3] = {0,0,0};
            glGetUniformiv(program, loc, v);
            out += "["; appendInt(out, v[0]);
            out += ","; appendInt(out, v[1]);
            out += ","; appendInt(out, v[2]);
            out += "]";
            return true;
        }
        case GL_INT_VEC4: {
            GLint v[4] = {0,0,0,0};
            glGetUniformiv(program, loc, v);
            out += "["; appendInt(out, v[0]);
            out += ","; appendInt(out, v[1]);
            out += ","; appendInt(out, v[2]);
            out += ","; appendInt(out, v[3]);
            out += "]";
            return true;
        }
        case GL_SAMPLER_2D:
            // MVP: skip. The texture handle would re-bound per-frame anyway,
            // and the editor has no UI to override fx-pass texture inputs.
            return false;
        default:
            // Matrices, arrays, structs, doubles — out of scope for MVP.
            return false;
    }
}

int countPasses() {
    int n = 0;
    while (true) {
        const char* nm = fx_name(n);
        if (!nm || !*nm) break;
        ++n;
    }
    return n;
}

}  // namespace

std::string snapshotEngineState() {
    std::string out;
    out.reserve(1024);
    out += "{\n  passes: [\n";

    const auto& skip = engineSlotNames();
    const int passCount = countPasses();
    char namebuf[256];

    for (int i = 0; i < passCount; ++i) {
        const char* name = fx_name(i);
        unsigned program = fx_program(i);
        const int enabled = fx_enabled(i);

        out += "    { name: ";
        appendStr(out, name ? name : "");
        out += ", enabled: ";
        out += enabled ? "true" : "false";
        out += ", uniforms: {";

        if (program != 0) {
            GLint nactive = 0;
            glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &nactive);

            bool first = true;
            for (GLint u = 0; u < nactive; ++u) {
                GLsizei len = 0;
                GLint   size = 0;
                GLenum  type = 0;
                glGetActiveUniform(program, u, sizeof(namebuf),
                                   &len, &size, &type, namebuf);
                if (len <= 0) continue;
                std::string uname(namebuf, len);
                if (skip.count(uname)) continue;
                if (size > 1) continue;   // array uniforms — MVP skip

                std::string entry;
                if (!appendUniform(entry, program, uname, type)) continue;
                if (!first) out += ", ";
                out += entry;
                first = false;
            }
        }

        out += "} }";
        if (i + 1 < passCount) out += ",";
        out += "\n";
    }

    out += "  ]\n}\n";
    return out;
}

namespace {

// Read a numeric json5 node as float — accepts INTEGER and REAL alike, so
// vec3/vec4 arrays mixing integer literals (`[1,1,0,1]`) and reals
// (`[0.5,0.7,0.3,1.0]`) both work seamlessly.
float jsonAsFloat(const json5* n) {
    if (!n) return 0.f;
    if (n->type == JSON5_INTEGER) return (float)n->integer;
    if (n->type == JSON5_REAL)    return (float)n->real;
    if (n->type == JSON5_BOOL)    return n->boolean ? 1.f : 0.f;
    return 0.f;
}

// Set one uniform on a pass based on the JSON5 value's type. Returns true if
// applied; false on unsupported shape (caller bumps `uniforms_skipped`).
bool applyOneUniform(int slot, const char* uname, const json5* val,
                     ApplyResult& r) {
    if (!val) return false;
    switch (val->type) {
        case JSON5_INTEGER:
            fx_setparami(slot, uname, (int)val->integer);
            return true;
        case JSON5_REAL:
            fx_setparam(slot, uname, (float)val->real);
            return true;
        case JSON5_BOOL:
            fx_setparami(slot, uname, val->boolean ? 1 : 0);
            return true;
        case JSON5_ARRAY: {
            if (val->count == 3) {
                vec3 v = { jsonAsFloat(&val->array[0]),
                           jsonAsFloat(&val->array[1]),
                           jsonAsFloat(&val->array[2]) };
                fx_setparam3(slot, uname, v);
                return true;
            }
            if (val->count == 4) {
                vec4 v = { jsonAsFloat(&val->array[0]),
                           jsonAsFloat(&val->array[1]),
                           jsonAsFloat(&val->array[2]),
                           jsonAsFloat(&val->array[3]) };
                fx_setparam4(slot, uname, v);
                return true;
            }
            if (val->count == 2) {
                // No public fx_setparam2 — bind the program directly and
                // call glUniform2f. Same trick as postfx_setparam (it does
                // shader_bind + shader_float + shader_bind oldprogram).
                unsigned program = fx_program(slot);
                if (!program) return false;
                GLint loc = glGetUniformLocation(program, uname);
                if (loc < 0) return false;
                GLint oldProg = 0;
                glGetIntegerv(GL_CURRENT_PROGRAM, &oldProg);
                glUseProgram(program);
                glUniform2f(loc, jsonAsFloat(&val->array[0]),
                                 jsonAsFloat(&val->array[1]));
                glUseProgram((GLuint)oldProg);
                return true;
            }
            // Other lengths — matrix-like, skip.
            r.warnings.push_back(std::string("uniform ") + uname +
                                 ": unsupported array length " +
                                 std::to_string((int)val->count));
            return false;
        }
        default:
            return false;
    }
}

}  // namespace

ApplyResult applyEngineState(const std::string& json) {
    ApplyResult r;
    if (json.empty()) return r;

    // The engine's json5_parse mutates the buffer (strchr-style scan), so
    // hand it a writable copy.
    std::string scratch = json;
    json5 root = {};
    char* err = json5_parse(&root, scratch.data(), 0);
    if (err) {
        r.warnings.push_back(std::string("JSON5 parse error: ") + err);
        json5_free(&root);
        return r;
    }
    if (root.type != JSON5_OBJECT) {
        r.warnings.push_back("expected top-level JSON5 object");
        json5_free(&root);
        return r;
    }

    // Find the `passes` child array.
    const json5* passes = nullptr;
    for (int i = 0; i < (int)root.count; ++i) {
        const json5* k = &root.nodes[i];
        if (k->name && !strcmp(k->name, "passes") &&
            k->type == JSON5_ARRAY) {
            passes = k;
            break;
        }
    }
    if (!passes) {
        // Empty snapshot (e.g. first save with no PostFXStack mutations) —
        // not an error, just nothing to do.
        json5_free(&root);
        return r;
    }

    for (int i = 0; i < (int)passes->count; ++i) {
        const json5* p = &passes->array[i];
        if (p->type != JSON5_OBJECT) continue;

        const char* name = nullptr;
        const json5* uniforms = nullptr;
        int enabled = 0;
        for (int j = 0; j < (int)p->count; ++j) {
            const json5* f = &p->nodes[j];
            if (!f->name) continue;
            if      (!strcmp(f->name, "name") && f->type == JSON5_STRING)
                name = f->string;
            else if (!strcmp(f->name, "enabled") && f->type == JSON5_BOOL)
                enabled = f->boolean;
            else if (!strcmp(f->name, "uniforms") && f->type == JSON5_OBJECT)
                uniforms = f;
        }
        if (!name) continue;

        int slot = fx_find(name);
        if (slot < 0) {
            ++r.passes_missing;
            r.warnings.push_back(std::string("pass not loaded: ") + name);
            continue;
        }

        // Restore enable-state + execution order. fx_order is a swap: if
        // the pass is already at index `i`, skip the swap to avoid a redundant
        // array_sort. Otherwise the swap puts it there (the displaced pass
        // ends up where this one was, and a later iteration will move it on).
        fx_enable(slot, enabled);
        if (slot != i) {
            (void)fx_order(slot, i);
        }
        ++r.passes_applied;

        if (uniforms) {
            for (int u = 0; u < (int)uniforms->count; ++u) {
                const json5* uval = &uniforms->nodes[u];
                if (!uval->name) continue;
                // fx_find again — the previous fx_order may have shifted
                // indices around. Cheap (linear strcmp scan, 28 items).
                int cur_slot = fx_find(name);
                if (cur_slot < 0) break;
                if (applyOneUniform(cur_slot, uval->name, uval, r)) {
                    ++r.uniforms_applied;
                } else {
                    ++r.uniforms_skipped;
                }
            }
        }
    }

    json5_free(&root);
    return r;
}

}  // namespace editor::postfx_state_io
