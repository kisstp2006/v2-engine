#pragma once

// EngineFFI → EmmyLua stub generátor (Phase 6a).
//
// Bemenet: `code/game/embed/engine.ffi` (a motor által generált C-cdef
// szöveg, ~21k sor — GL typedef-ek, struct/enum, function-prototype-ok).
//
// Kimenet a projekt-mappába:
//   <projectPath>/.luarc/engine.d.lua  — EmmyLua stub (---@class, ---@field,
//                                         ---@meta).
//   <projectPath>/.luarc.json           — lua-language-server (sumneko)
//                                         workspace.library + LuaJIT-runtime.
//   <projectPath>/.vscode/settings.json — VSCode override (csak ha még
//                                         NINCS ott, hogy ne írjuk felül
//                                         a user-edit-jét).
//
// Az EmmyLua stub-ot a `lua-language-server` és a JetBrains EmmyLua
// plugin auto-felismeri a `.luarc.json` `workspace.library` mezője alapján.
//
// A parser NEM teljes C-parser — egy soros regex-szerű deklaráció-felismerő.
// A komplex eseteket (function-pointer-typedef, multi-line struct-tartalom)
// kihagyja. A motor-API kb. 90%-os fedettséggel megjelenik a stubban.

#include <string>

namespace editor::ffi_to_emmylua {

struct GenResult {
    bool        ok          = false;
    int         functions   = 0;
    int         classes     = 0;
    int         enums       = 0;
    std::string stubPath;        // .luarc/engine.d.lua
    std::string configPath;      // .luarc.json
    std::string vscodePath;      // .vscode/settings.json (üres, ha kihagyva)
    std::string error;
};

// Fő API. `projectPath` = abs projekt-mappa. `ffiPath` = engine.ffi abs
// vagy relatív path (a motor CWD-jéhez képest, "code/game/embed/engine.ffi").
// `force` = ha false, csak akkor regenerál, ha az engine.ffi mtime > stub mtime.
GenResult generate(const std::string& projectPath,
                   const std::string& ffiPath,
                   bool               force = true);

}  // namespace editor::ffi_to_emmylua
