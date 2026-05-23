#pragma once

// editor_style — Unity-dark + cold blue accent + compact + sharp corners.
//
// Hívd MEG egyszer `app_create()` UTÁN — felülírja a motor alapértelmezett
// `igCherryTheme`-jét és a `ImGuiStyle` minden vonatkozó mezőjét.

namespace editor {

void applyStyle();

}  // namespace editor
