// STL FIRST.
#include <cstring>
#include <string>
#include <vector>

#include "engine.h"
#ifdef obj
#undef obj
#endif

#include "texture_loader.h"
#include "file_io.h"

namespace editor::texture_loader {

texture_t loadCompressed(const std::string& absPath, unsigned flags) {
    texture_t t = {};
    if (absPath.empty()) return t;

    // editor::file_io::readBytes — STL-based, robust against the Windows
    // path edge-cases that break motor `file_read()` (OneDrive online-only,
    // spaces in path, etc.).
    std::vector<uint8_t> bytes = editor::file_io::readBytes(absPath);
    if (bytes.empty()) return t;

    // GPU upload happens before texture_compressed_from_mem returns, so the
    // local std::vector scope is sufficient (no need to STRDUP the bytes).
    t = texture_compressed_from_mem(bytes.data(), (int)bytes.size(), flags);
    if (t.id == 0 || t.id == texture_checker().id) {
        t.id = 0;
        return t;
    }

    // Mirror motor `texture()`/`texture_compressed()`: STRDUP the filename so
    // downstream debug-logs / cache-key comparisons get the expected string.
    t.filename = STRDUP(absPath.c_str());
    return t;
}

bool loadIntoColormap(colormap_t* cm,
                      const std::string& absPath,
                      bool load_as_srgb) {
    if (!cm) return false;
    if (absPath.empty()) return false;

    // Replicates motor `colormap()` flag composition (render_colormap.h:19-22).
    int srgb      = load_as_srgb ? TEXTURE_SRGB : 0;
    int mipmapped = !cm->no_mipmaps ? (TEXTURE_MIPMAPS | TEXTURE_ANISOTROPY) : 0;
    int hdr       = (absPath.size() >= 4 &&
                     (absPath.compare(absPath.size() - 4, 4, ".hdr") == 0 ||
                      absPath.compare(absPath.size() - 4, 4, ".HDR") == 0))
                    ? (TEXTURE_FLOAT | TEXTURE_RGBA) : 0;
    unsigned flags = TEXTURE_LINEAR | TEXTURE_REPEAT | mipmapped | hdr | srgb;

    texture_t t = loadCompressed(absPath, flags);
    if (t.id == 0) {
        cm->texture = nullptr;
        return false;
    }
    // Motor `colormap()` CALLOCs a heap texture_t and copies the value in.
    // We do the same so existing free-paths (if any) still work.
    cm->texture  = (texture_t*)CALLOC(1, sizeof(texture_t));
    *cm->texture = t;
    return true;
}

}  // namespace editor::texture_loader
