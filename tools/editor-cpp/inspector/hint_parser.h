#pragma once

#include <string>

namespace editor {

// Hint-string parser (Phase 2d). A STRUCT(T,t,m,"[hint] desc") 4. paramétere
// a reflect_t.info-ba kerül "M[hint] desc (FILELINE)" formátumban. Innen
// parsoljuk a tag-eket egy HintInfo struktúrába.
//
// Támogatott tag-ek:
//   [bool]             — int 0/1 → ImGui::Checkbox
//   [hidden]           — ne rajzolja
//   [readonly]         — disabled widget (csak megjelenít, nem editálható)
//   [multiline]        — char* InputTextMultiline (3-soros)
//   [range MIN MAX]    — float/int slider min/max
//   [color3]           — vec3 ColorEdit3 (RGB picker)
//   [suffix:°]         — érték mellé text-suffix (pl. "fov 60°")
//   [asset:model]      — char* + Browse-button .iqm filter
//   [asset:texture]    — Browse-button .png/.jpg/.bmp/.tga
//   [asset:clip]       — Browse-button .ogg/.wav/.mp3/.flac
//   [asset:tmx]        — Browse-button .tmx
//   [asset:script]     — Browse-button .lua
//   [asset:prefab]     — Browse-button .json5
//
// A `[group:Name]` tag (collapsing-header köré) Phase 3+-ra halasztva.
struct HintInfo {
    bool        isBool = false;
    bool        isHidden = false;
    bool        isReadonly = false;
    bool        isMultiline = false;
    bool        hasRange = false;
    float       rangeMin = 0, rangeMax = 0;
    bool        isColor3 = false;
    std::string suffix;      // pl. "°", "cd"
    std::string assetType;   // "model", "texture", "clip", "tmx", "script", "prefab"
};

HintInfo parseHint(const char* info);

}  // namespace editor
