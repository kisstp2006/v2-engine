// STL FIRST.
#include <filesystem>
#include <string>
#include <system_error>

#include "fx_bundle.h"

namespace editor::fx_bundle {

namespace fs = std::filesystem;

namespace {

// CWD-relative source dir. The editor is launched from the v2 repo root
// (the working-dir convention used by `code/game/embed/engine.ffi`,
// `tools/editor-cpp/themes/default.json`, etc.).
constexpr const char* kEmbedFXDir = "tools/editor-cpp/embed/fx";

}  // namespace

CopyResult copyBundledShaders(const std::string& projectRoot, bool overwrite) {
    CopyResult r;
    std::error_code ec;
    fs::path src(kEmbedFXDir);

    if (!fs::is_directory(src, ec)) {
        r.source_dir_missing = true;
        return r;
    }

    fs::path dst = fs::path(projectRoot) / "assets" / "fx";
    fs::create_directories(dst, ec);
    if (ec) return r;     // can't create dest — caller will see counts=0

    for (auto& e : fs::directory_iterator(src, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().extension() != ".glsl") continue;
        ++r.total;

        fs::path target = dst / e.path().filename();

        if (!overwrite && fs::exists(target, ec)) {
            ++r.skipped;
            continue;
        }

        std::error_code copy_ec;
        fs::copy_file(e.path(), target,
                      overwrite ? fs::copy_options::overwrite_existing
                                : fs::copy_options::none,
                      copy_ec);
        if (!copy_ec) ++r.copied;
    }
    return r;
}

}  // namespace editor::fx_bundle
