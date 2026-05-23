#pragma once

#include <string>

namespace editor {

// Hint-string parser (Phase 2d). The 4th parameter of STRUCT(T,t,m,"[hint] desc")
// goes into reflect_t.info in the "M[hint] desc (FILELINE)" format. From there
// we parse the tags into a HintInfo struct.
//
// Supported tags:
//   [bool]             — int 0/1 → ImGui::Checkbox
//   [hidden]           — don't draw it
//   [readonly]         — disabled widget (display only, not editable)
//   [multiline]        — char* InputTextMultiline (3 lines)
//   [range MIN MAX]    — float/int slider min/max
//   [color3]           — vec3 ColorEdit3 (RGB picker)
//   [suffix:°]         — text-suffix next to the value (e.g. "fov 60°")
//   [asset:model]      — char* + Browse-button .iqm filter
//   [asset:texture]    — Browse-button .png/.jpg/.bmp/.tga
//   [asset:clip]       — Browse-button .ogg/.wav/.mp3/.flac
//   [asset:tmx]        — Browse-button .tmx
//   [asset:script]     — Browse-button .lua
//   [asset:prefab]     — Browse-button .json5
//
// The `[group:Name]` tag (wrapping in a collapsing-header) is deferred to Phase 3+.
struct HintInfo {
    bool        isBool = false;
    bool        isHidden = false;
    bool        isReadonly = false;
    bool        isMultiline = false;
    bool        hasRange = false;
    float       rangeMin = 0, rangeMax = 0;
    bool        isColor3 = false;
    std::string suffix;      // e.g. "°", "cd"
    std::string assetType;   // "model", "texture", "clip", "tmx", "script", "prefab"
};

HintInfo parseHint(const char* info);

}  // namespace editor
