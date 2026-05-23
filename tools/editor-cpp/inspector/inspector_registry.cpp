// STL ELŐSZÖR.
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "inspector_registry.h"
#include "hint_parser.h"
#include "../core/asset_path.h"
#include "../core/file_picker.h"

namespace editor {

InspectorRegistry& InspectorRegistry::instance() {
    static InspectorRegistry r;
    return r;
}

void InspectorRegistry::registerDrawer(
    const char* typeName, std::unique_ptr<IInspectorDrawer> drawer) {
    if (!typeName) return;
    drawers_[std::string(typeName)] = std::move(drawer);
}

namespace {

// 2-oszlopos label + widget layout, a motor `ui_*` widgetjei stílusához
// illesztve (motor game_ui2.h:UI_LABEL_1OF2 / 2OF2 mintára).
bool labelCol(const char* label) {
    if (!ImGui::BeginTable("##lc", 2,
                           ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
        return false;
    ImGui::TableNextColumn();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label ? label : "");
    ImGui::TableNextColumn();
    ImGui::PushItemWidth(-1);
    return true;
}
void labelColEnd() {
    ImGui::PopItemWidth();
    ImGui::EndTable();
}

// Hint-string parserek (M16a). A `STRUCT(T,t,m,"[hint] ...")` 4. paramétere
// a reflect_t.info-ba kerül "M" prefix-szel és " (FILELINE)" suffix-szel.
bool hintHas(const char* info, const char* tag) {
    return info && strstr(info, tag) != nullptr;
}

bool hintRangeFloat(const char* info, float* mn, float* mx) {
    if (!info) return false;
    const char* p = strstr(info, "[range ");
    if (!p) return false;
    return sscanf(p, "[range %f %f]", mn, mx) == 2;
}

bool hintRangeInt(const char* info, int* mn, int* mx) {
    if (!info) return false;
    const char* p = strstr(info, "[range ");
    if (!p) return false;
    return sscanf(p, "[range %d %d]", mn, mx) == 2;
}

// Phase 2c: a primary mező-értékét propagálja minden további targets[i] node
// azonos-offsetű mezőjébe. Csak HOMOGÉN selection-re hívandó.
void propagateField(const std::vector<obj*>& targets, void* p_primary,
                    const reflect_t* R) {
    if (targets.size() <= 1) return;
    bool isString = R->type && (!strcmp(R->type, "char*") ||
                                !strcmp(R->type, "string"));
    for (size_t i = 1; i < targets.size(); ++i) {
        if (!targets[i]) continue;
        void* dst = (char*)targets[i] + R->sz;
        if (isString) {
            char* old = *(char**)dst;
            if (old) FREE(old);
            char* src = *(char**)p_primary;
            *(char**)dst = (src && *src) ? STRDUP(src) : NULL;
        } else {
            memcpy(dst, p_primary, R->bytes);
        }
    }
}

// Saját replika a motor `ui_obj`-jából — hint-string-vezérelt widget-választás
// + `char*` mezőkhöz drag-drop target az ASSET_PATH payload-hoz.
// Multi-targets vector — primary = targets[0]. Egy frame-en belül a primary-n
// szerkesztett mező AUTOMATIKUSAN syncel a többi targets[i]-re (ha
// ImGui::IsItemEdited()-tel detektálható).
void drawDefaultReflection(const std::vector<obj*>& targets) {
    if (targets.empty() || !targets[0]) return;
    obj* o = targets[0];
    const char* type = obj_type(o);
    if (!type) return;
    array(reflect_t)* members = members_find(type);
    if (!members) return;

    int n = array_count(*members);
    for (int i = 0; i < n; ++i) {
        reflect_t* R = &(*members)[i];
        void*       p = (char*)o + R->sz;
        const char* mt = R->type;
        const char* mn = R->name;
        const char* info = R->info;
        if (!mt || !mn) continue;

        HintInfo h = parseHint(info);

        // [hidden] → ne rajzolja
        if (h.isHidden) continue;

        // [readonly] → minden widget disabled
        if (h.isReadonly) ImGui::BeginDisabled();

        /**/ if (!strcmp(mt, "float")) {
            if (h.hasRange) {
                if (labelCol(mn)) {
                    ImGui::SliderFloat("##v", (float*)p, h.rangeMin, h.rangeMax);
                    labelColEnd();
                }
            } else {
                ui_float(mn, (float*)p);
            }
        }
        else if (!strcmp(mt, "int")) {
            if (h.isBool) {
                bool v = *(int*)p != 0;
                if (labelCol(mn)) {
                    if (ImGui::Checkbox("##v", &v)) *(int*)p = v ? 1 : 0;
                    labelColEnd();
                }
            } else if (h.hasRange) {
                if (labelCol(mn)) {
                    ImGui::SliderInt("##v", (int*)p,
                                     (int)h.rangeMin, (int)h.rangeMax);
                    labelColEnd();
                }
            } else {
                ui_int(mn, (int*)p);
            }
        }
        else if (!strcmp(mt, "unsigned")) ui_unsigned(mn, (unsigned*)p);
        else if (!strcmp(mt, "vec2"))     ui_float2(mn, (float*)p);
        else if (!strcmp(mt, "vec3")) {
            if (h.isColor3) {
                if (labelCol(mn)) {
                    ImGui::ColorEdit3("##v", (float*)p);
                    labelColEnd();
                }
            } else {
                ui_float3(mn, (float*)p);
            }
        }
        else if (!strcmp(mt, "vec4"))     ui_float4(mn, (float*)p);
        else if (!strcmp(mt, "rgb"))      ui_color3(mn, (unsigned*)p);
        else if (!strcmp(mt, "rgba"))     ui_color4(mn, (unsigned*)p);
        else if (!strcmp(mt, "char*") || !strcmp(mt, "string")) {
            if (h.isMultiline) {
                // Multiline: char*-ot konvertáljuk fix-buffer-rel ImGui-stílusra
                // (egyszerű — a felhasználói edit visszaírja a STRDUP-pal).
                if (labelCol(mn)) {
                    static char buf[4096];
                    const char* cur = *(char**)p ? *(char**)p : "";
                    strncpy(buf, cur, sizeof(buf) - 1);
                    buf[sizeof(buf) - 1] = '\0';
                    if (ImGui::InputTextMultiline("##v", buf, sizeof(buf),
                            ImVec2(-1, ImGui::GetTextLineHeight() * 4))) {
                        char* old = *(char**)p;
                        if (old) FREE(old);
                        *(char**)p = STRDUP(buf);
                    }
                    labelColEnd();
                }
            } else {
                ui_string(mn, (char**)p);
            }
            // Phase 4a — relativize-callback. Abs path → projekt-relatív
            // string (ha az asset projekten belül van), egyébként abs marad.
            const std::string& projectPath =
                InspectorRegistry::instance().projectPath();
            auto relativize = [&projectPath](const std::string& abs) -> std::string {
                if (abs.empty() || projectPath.empty()) return abs;
                if (asset_path::isWithinProject(abs, projectPath)) {
                    return asset_path::toProjectRelative(abs, projectPath);
                }
                return abs;
            };

            // Asset-Browse button az [asset:*] hint esetén — kis "..." gomb
            // a path-mező mellett. Filter a hint-érték szerint.
            if (!h.assetType.empty()) {
                ImGui::SameLine();
                ImGui::PushID((void*)R);
                if (ImGui::SmallButton("...")) {
                    const char* ext = "*";
                    if      (h.assetType == "model")   ext = "iqm";
                    else if (h.assetType == "texture") ext = "png";
                    else if (h.assetType == "clip")    ext = "ogg";
                    else if (h.assetType == "tmx")     ext = "tmx";
                    else if (h.assetType == "script")  ext = "lua";
                    else if (h.assetType == "prefab")  ext = "json5";
                    std::string picked = pickFile("Pick asset", ext, false);
                    if (!picked.empty()) {
                        std::string rel = relativize(picked);
                        char* old = *(char**)p;
                        if (old) FREE(old);
                        *(char**)p = STRDUP(rel.c_str());
                    }
                }
                ImGui::PopID();
            }
            // Drop target: a Project panelből húzott asset path-ja felülír.
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl =
                        ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string rel = relativize(std::string((const char*)pl->Data));
                    char* old = *(char**)p;
                    if (old) FREE(old);
                    *(char**)p = STRDUP(rel.c_str());
                }
                ImGui::EndDragDropTarget();
            }
        }
        else {
            ui_label2(mn, va("(%s)", mt));
        }

        // [suffix:°] — érték mellé text-suffix
        if (!h.suffix.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", h.suffix.c_str());
        }

        if (h.isReadonly) ImGui::EndDisabled();

        // Phase 2c: multi-edit. A primary mező-értékét minden frame syncelve
        // propagáljuk a többi targets[i]-re. Single-mode (targets.size()==1)
        // esetén no-op.
        propagateField(targets, p, R);
    }
}

}  // namespace

void InspectorRegistry::drawDefaults(const std::vector<obj*>& targets) {
    drawDefaultReflection(targets);
}

void InspectorRegistry::drawComponents(obj* o) {
    if (!o) return;

    const char* type = obj_type(o);
    if (type) {
        auto it = drawers_.find(type);
        if (it != drawers_.end()) {
            it->second->draw(o);
            return;
        }
    }

    // Fallback: saját reflection-render (mint a motor ui_obj, plusz drag-drop
    // target a char* mezőkön).
    drawDefaultReflection({o});
}

void InspectorRegistry::drawComponentsMulti(const std::vector<obj*>& targets) {
    if (targets.empty()) return;
    if (targets.size() == 1) { drawComponents(targets[0]); return; }

    // Csak homogén selection: minden node-ra ugyanaz az obj_type. Heterogén
    // esetén csak a primary-t mutatjuk (single-mode fallback).
    const char* primaryType = obj_type(targets[0]);
    if (!primaryType) { drawComponents(targets[0]); return; }
    for (size_t i = 1; i < targets.size(); ++i) {
        const char* t = targets[i] ? obj_type(targets[i]) : nullptr;
        if (!t || strcmp(t, primaryType) != 0) {
            drawComponents(targets[0]);  // heterogén → single
            return;
        }
    }
    // Custom drawer? Single-only — a multi-edit-hez fallback drawDefaultReflection.
    auto it = drawers_.find(primaryType);
    if (it != drawers_.end()) {
        it->second->draw(targets[0]);   // custom drawer marad single
        return;
    }
    drawDefaultReflection(targets);
}

}  // namespace editor
