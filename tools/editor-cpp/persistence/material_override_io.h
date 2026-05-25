#pragma once

// MeshRendererExtraSerializer (Blokk 2.5) — handles the
// `MeshRenderer.material_overrides` array via the extra-serializer infra
// (Fázis 2.1). Plus an `applyOverridesToModel` helper used by the
// render-walk to overlay overrides onto the runtime `model_t.materials[]`.

#include <string>

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor::material_override_io {

// One-shot registrar — call from EditorApp ctor (or anywhere before the
// first scene save/load) to plug the serializer into the
// ExtraSerializerRegistry. Idempotent.
void registerSerializer();

// Render-walk hook. Iterates `mr`'s material_overrides; for each one:
//   1) if `material_asset_path` is set → load the asset via MaterialLibrary
//      and memcpy onto the target slot of `mt->materials`,
//   2) overlay the inline_mat fields whose bit is set in `override_mask`
//      (per-layer bits in low 8, top-level bits in 8..13).
// Slot is matched by name (`MaterialOverride.name == mt->materials[i].name`).
// `projectRoot` is used to resolve relative asset paths.
void applyOverridesToModel(obj* mr, model_t* mt, const std::string& projectRoot);

}  // namespace editor::material_override_io
