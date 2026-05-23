#pragma once

// Editor-wide event-keys + payload-structs for the EventBus.
// Every subscriber must agree on the payload-type declared here.
//
// Convention: `kEvtFoo` const char* string-key. The payload string is
// the key itself (NOT for std::string operations), giving O(1) string-cmp
// in the hash-bucket.

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

// payload: std::string (asset path, empty = clear). Project panel emits
// on click, the Inspector switches to asset-preview mode.
constexpr const char* kEvtAssetSelectionChanged = "asset_selection_changed";

// payload: obj* newRoot (may be nullptr)
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

// kEvtSelectionChanged. `primary` is the first selected; `all` is the full list.
// When `all` is empty, `primary == nullptr` (selection cleared).
struct SelectionChange {
    std::vector<obj*> all;
    obj*              primary = nullptr;
};

// Phase 5a — cook progress payload (kEvtCookStarted/Progress/Cancelled).
struct CookProgress {
    int         current = 0;       // how many files processed
    int         total   = 0;       // total files in the list
    std::string currentFile;       // e.g. "assets/audio/foo.mp3" (rel path)
};

// Phase 5a — cook result (kEvtCookFinished).
struct CookResult {
    int         succeeded = 0;
    int         failed    = 0;
    int         total     = 0;
    std::string outputPath;        // cook.zip path if zip-mode; otherwise empty
    bool        cancelled = false;
};

}  // namespace editor
