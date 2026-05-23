// STL FIRST.
#include <cstdio>
#include <cstring>
#include <string>

#include "hint_parser.h"

namespace editor {

namespace {

bool tagPresent(const char* info, const char* tag) {
    return info && strstr(info, tag) != nullptr;
}

// Simple key-prefix search: reads after "[key" until "]".
// E.g. "[asset:model]" → tag="[asset:", out_val="model".
bool extractTag(const char* info, const char* prefix, std::string& out_val) {
    if (!info) return false;
    const char* p = strstr(info, prefix);
    if (!p) return false;
    p += strlen(prefix);
    const char* end = strchr(p, ']');
    if (!end) return false;
    out_val.assign(p, end - p);
    return true;
}

}  // namespace

HintInfo parseHint(const char* info) {
    HintInfo h;
    if (!info) return h;
    h.isBool      = tagPresent(info, "[bool]");
    h.isHidden    = tagPresent(info, "[hidden]");
    h.isReadonly  = tagPresent(info, "[readonly]");
    h.isMultiline = tagPresent(info, "[multiline]");
    h.isColor3    = tagPresent(info, "[color3]");

    // [range MIN MAX]
    const char* r = strstr(info, "[range ");
    if (r) {
        float mn, mx;
        if (sscanf(r, "[range %f %f]", &mn, &mx) == 2) {
            h.hasRange = true;
            h.rangeMin = mn;
            h.rangeMax = mx;
        }
    }

    extractTag(info, "[suffix:", h.suffix);
    extractTag(info, "[asset:", h.assetType);
    return h;
}

}  // namespace editor
