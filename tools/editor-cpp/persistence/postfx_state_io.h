#pragma once

// PostFX engine-state snapshot + apply. The engine's per-pass `enabled`,
// `priority`, and user-uniform values are runtime-only state — they don't
// persist across editor restarts. To survive scene round-trips we capture
// the state into a JSON5 blob on Save Scene (`snapshotEngineState`) and
// re-apply it on Open Scene (`applyEngineState`, added in Fázis 1.3).
//
// The blob is stored in the PostFXStack node's `state_json` field, which
// is invisible to the default Inspector (`[hidden]` hint) and rides along
// the reflection-driven scene-IO automatically.
//
// Format (one entry per loaded pass, IN CURRENT EXECUTION ORDER):
// {
//   passes: [
//     { name: "fxBloom.glsl", enabled: true,
//       uniforms: { threshold: 0.8, tint_color: [1,0.5,0.5] } },
//     ...
//   ]
// }
//
// No explicit `priority` field — the engine's `passfx.priority` has no public
// getter, so we use the iteration index as the implicit priority. Apply re-
// applies the order with `fx_order(slot, target_index)` swaps (Fázis 1.3).

#include <string>
#include <vector>

namespace editor::postfx_state_io {

// Snapshot the engine's current PostFX pipeline state into a JSON5 string.
// Skipped uniforms (matrix / array / engine-defined slot like iTime) are
// silently dropped — only `fx_setparam`-compatible types are persisted.
std::string snapshotEngineState();

// Result counts + warnings from applyEngineState. The caller decides where
// to surface them (Console-log, dialog, etc.).
struct ApplyResult {
    int passes_applied   = 0;
    int passes_missing   = 0;     // fx_find returned -1 (shader not loaded)
    int uniforms_applied = 0;
    int uniforms_skipped = 0;     // unsupported type or unknown name
    std::vector<std::string> warnings;
};

// Re-apply a previously snapshotted state onto the engine. Must be called
// AFTER the corresponding FX shaders have been loaded (`fx_load*`) — pass
// lookup goes through `fx_find(name)` and missing passes are reported in
// `passes_missing`, not hard-failed.
//
// Order is restored by re-sorting via `fx_order(slot, target_index)` swaps:
// each pass in the JSON `passes[]` array maps to its iteration index as the
// target priority (the engine's `passfx.priority` has no public getter).
ApplyResult applyEngineState(const std::string& json);

}  // namespace editor::postfx_state_io
