#pragma once

// Editor-wide event-keys + payload-struct-ok az EventBus-hoz.
// Minden subscriber egyezzen meg az itt deklarált payload-típuson.
//
// Konvenció: `kEvtFoo` const char* string-key. A payload sztring magában
// a key (NEM std::string-műveletekhez kell), így O(1)-es string-cmp a
// hash-bucket-ben.

#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// ---- Event keys -----------------------------------------------------------

// payload: SelectionChange
constexpr const char* kEvtSelectionChanged = "selection_changed";

// payload: std::string (asset path, üres = clear). Project panel kattint-ra
// emit, az Inspector asset-preview módra vált.
constexpr const char* kEvtAssetSelectionChanged = "asset_selection_changed";

// payload: obj* newRoot (lehet nullptr)
constexpr const char* kEvtSceneReplaced    = "scene_replaced";

// payload: bool dirty
constexpr const char* kEvtSceneDirty       = "scene_dirty";

// payload: int (0=Edit, 1=Play, 2=Pause)
constexpr const char* kEvtPlayStateChanged = "play_state_changed";

// payload: obj* added/removed/renamed
constexpr const char* kEvtNodeAdded        = "node_added";
constexpr const char* kEvtNodeRemoved      = "node_removed";
constexpr const char* kEvtNodeRenamed      = "node_renamed";

// payload: std::string (log message)
constexpr const char* kEvtLogInfo          = "log";       // backward-compat
constexpr const char* kEvtLogWarn          = "log_warn";
constexpr const char* kEvtLogError         = "log_error";

// Phase 5a — cook lifecycle. Payload: CookProgress / CookResult.
constexpr const char* kEvtCookStarted      = "cook_started";    // CookProgress {0, total}
constexpr const char* kEvtCookProgress     = "cook_progress";   // CookProgress
constexpr const char* kEvtCookFinished     = "cook_finished";   // CookResult
constexpr const char* kEvtCookCancelled    = "cook_cancelled";  // CookProgress

// ---- Payload types --------------------------------------------------------

// kEvtSelectionChanged. `primary` az első kijelölt; `all` a teljes lista.
// Empty `all` esetén `primary == nullptr` (selection cleared).
struct SelectionChange {
    std::vector<obj*> all;
    obj*              primary = nullptr;
};

// Phase 5a — cook progress payload (kEvtCookStarted/Progress/Cancelled).
struct CookProgress {
    int         current = 0;       // hány file feldolgozva
    int         total   = 0;       // összes file a listában
    std::string currentFile;       // pl. "assets/audio/foo.mp3" (rel path)
};

// Phase 5a — cook eredmény (kEvtCookFinished).
struct CookResult {
    int         succeeded = 0;
    int         failed    = 0;
    int         total     = 0;
    std::string outputPath;        // cook.zip path, ha zip-mode; egyébként üres
    bool        cancelled = false;
};

}  // namespace editor
