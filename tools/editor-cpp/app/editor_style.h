#pragma once

// editor_style — dark theme + cold blue accent + compact + sharp corners.
//
// Call once AFTER `app_create()` — overrides the engine's default
// `igCherryTheme` and every relevant field of `ImGuiStyle`.

namespace editor {

void applyStyle();

}  // namespace editor
