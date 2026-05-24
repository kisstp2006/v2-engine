#pragma once

// Bundled PostFX shader helper. The editor ships 28 GLSL files under
// `tools/editor-cpp/embed/fx/` and copies them into a project's
// `<projectRoot>/assets/fx/` so the runtime `fx_load()` can pick them up.
//
// Two call-sites:
//   - NewProjectDialog (one-time, project creation) — overwrite_existing OK
//     since the dest is freshly created.
//   - EditorApp::importDefaultFXShaders (Tools menu, existing projects) —
//     skip_existing so user-edited variants are NOT clobbered.

#include <string>

namespace editor::fx_bundle {

// Result of a copy operation.
struct CopyResult {
    int copied  = 0;   // # of files that were actually written
    int skipped = 0;   // # of files that already existed (skip-mode only)
    int total   = 0;   // # of .glsl files found in the embed dir
    bool source_dir_missing = false;
};

// Copy `tools/editor-cpp/embed/fx/*.glsl` into `<projectRoot>/assets/fx/`.
// `overwrite` = true: clobber any existing same-named file (Wizard mode).
// `overwrite` = false: leave any existing same-named file alone (Tools mode).
// Creates the destination dir on demand. Returns counts (see CopyResult).
CopyResult copyBundledShaders(const std::string& projectRoot, bool overwrite);

}  // namespace editor::fx_bundle
