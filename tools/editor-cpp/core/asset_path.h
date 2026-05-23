#pragma once

// Asset path helper (Phase 4a). In the editor every stored asset-path
// is in project-relative form (e.g. "assets/models/foo.iqm"); at
// render/load time we resolve to absolute and pass to the engine (`model()`,
// `texture()`, `file_read()`, etc.).
//
// Goal: the project folder should be portable — copy to another machine/disk,
// every asset is findable via relative path.

#include <string>

namespace editor::asset_path {

// Abs → rel. E.g. ("C:\proj\assets\models\foo.iqm", "C:\proj")
// → "assets/models/foo.iqm". Forward-slash normalized.
// If `abs` is NOT inside the project, returns `abs` unchanged
// (with forward-slash normalize).
std::string toProjectRelative(const std::string& abs,
                              const std::string& projectRoot);

// Rel → abs. E.g. ("assets/models/foo.iqm", "C:\proj")
// → "C:/proj/assets/models/foo.iqm". If input is already abs, unchanged.
std::string toAbsolute(const std::string& rel,
                       const std::string& projectRoot);

// true if `abs` path is under the project folder. Lexically-normal
// comparison (`/foo/../bar` → `/bar`).
bool isWithinProject(const std::string& abs,
                     const std::string& projectRoot);

// true if the path is absolute (Windows: drive-letter or UNC; POSIX: '/').
bool isAbsolute(const std::string& path);

}  // namespace editor::asset_path
