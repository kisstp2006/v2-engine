#pragma once

// Editor-side material UI — replaces the engine's `ui_material()` because
// the latter only DISPLAYS textures (via ui_texture) but doesn't provide UI
// to ASSIGN them. Our drawer integrates the texture-path input + drop-target
// (material_drop_target.h) INSIDE each layer's collapsible, alongside the
// color/value widgets — UX-coherent with the rest of the Inspector.
//
// Used in:
//   - asset_preview.cpp — when a `.mat.json5` is selected (Blokk 2.3)
//   - mesh_renderer_drawer.cpp — Materials array-UI on MeshRenderer (Blokk 2.6)
//
// Returns true if the material was edited this frame (caller may want to bump
// dirty / commit a transaction; the asset-preview just leaves it for the
// explicit Save button).

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

bool drawMaterial(material_t* m, const std::string& projectRoot);

}  // namespace editor
