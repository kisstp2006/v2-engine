#pragma once

// Per-channel texture row for a material editor — `[label]: [path-input]`
// with a drag-drop target accepting the Project panel's "ASSET_PATH" payload.
// Used in two places (consistent UX):
//   - asset_preview.cpp — when a `.mat.json5` is selected in the Project
//     panel, the Inspector shows the material editor (Blokk 2.3)
//   - mesh_renderer_drawer.cpp — Materials array-UI on a MeshRenderer node
//     edits inline-overlay material_t fields (Blokk 2.6)
//
// On commit (Enter in InputText OR drop), the function calls `colormap(...)`
// to load the texture so that ui_material()'s thumbnail updates in-place.
// Returns `true` when the texname changed (caller bumps dirty / emits event).

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor::material_drop {

// Draw a single channel row. `channelLabel` is the human-readable name
// ("Albedo", "Normals", etc.). `projectRoot` is needed for relative→absolute
// path conversion before colormap() reload.
bool drawTextureChannel(material_t* m,
                        int channelIdx,
                        const std::string& projectRoot,
                        const char* channelLabel);

}  // namespace editor::material_drop
