#pragma once

// MeshRendererDrawer (GLTF Step 6). Custom Inspector drawer for MeshRenderer:
//   - Default reflection (model_path / cast_shadows / tint).
//   - Read-only "Model Info" panel with mesh / triangle / joint / anim / frame
//     counts and a format chip (IQM / glTF / GLB).
//
// Cheap: model() hits the engine's model_cache so the per-frame call is
// effectively a hash-map lookup.
//
// Registration: bottom of the .cpp file static-inits with
//   InspectorRegistry::instance().registerDrawer("MeshRenderer", ...)

#include "inspector_registry.h"

namespace editor {

class MeshRendererDrawer : public IInspectorDrawer {
public:
    void draw(obj* o) override;
};

}  // namespace editor
