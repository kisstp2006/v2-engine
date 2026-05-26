#pragma once

#include <string>

// editor::texture_loader — single entry-point for ALL editor-side texture
// loads. Built on editor::file_io::readBytes + motor texture_compressed_from_mem
// so the load works on every Windows path editor::file_io can reach (OneDrive
// online-only, mixed slashes, spaces — all the cases where motor `texture()`/
// `texture_compressed()` chains to a failing `file_read()`).
//
// Replaces direct use of:
//   - motor `texture(path, flags)`             — uses file_read internally
//   - motor `texture_compressed(path, flags)`  — uses file_read internally
//   - motor `colormap(cm, path, srgb)`         — chains to texture_compressed
//
// Callers responsible for caching (AssetManager, MaterialLibrary, etc.).
//
// Header kept STL-light (no motor include) so this can be pulled in from any
// spot in the include chain. The motor types (texture_t / colormap_t) are
// forward-declared via the engine headers in the .cpp.

// Forward-declares are tricky with engine.h macros, so we just include the
// engine header. NOTE: callers of this header MUST also #include "engine.h"
// before any STL header (see editor-cpp macro-clash convention).
#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor::texture_loader {

// Read a texture file (any motor-supported format: png/jpg/dds/ktx/basisu)
// and upload to GPU. Returns a texture_t with `id == 0` on failure (caller
// should treat 0 as "not loaded"). On success the returned texture_t owns a
// GPU texture object; caller is responsible for STRDUP'ing filename if it
// wants to track it.
//
// `absPath` must be an absolute path. `flags` are the motor's TEXTURE_* bits
// (TEXTURE_SRGB / TEXTURE_LINEAR / TEXTURE_MIPMAPS / TEXTURE_REPEAT etc.).
texture_t loadCompressed(const std::string& absPath, unsigned flags);

// colormap()-style helper: drop-in replacement for motor `colormap(cm, path,
// srgb)`. Allocates and fills `cm->texture` exactly like the motor would,
// but routes through editor::file_io. Returns true on success, false if the
// file couldn't be read or decoded.
//
// Pre-condition: `cm` is non-null. `cm->no_mipmaps` is respected.
bool loadIntoColormap(colormap_t* cm,
                      const std::string& absPath,
                      bool load_as_srgb);

}  // namespace editor::texture_loader
