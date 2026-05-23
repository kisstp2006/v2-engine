#pragma once

// Asset path-segéd (Phase 4a). Az editor-ben minden tárolt asset-path
// projekt-relatív forma (pl. "assets/models/foo.iqm"), a render/load
// időben pedig abszolútra resolve-elve adjuk át a motornak (`model()`,
// `texture()`, `file_read()`, stb.).
//
// Cél: a projekt-mappa hordozható legyen — másold másik gépre/diszkre,
// minden asset relatívan megtalálható.

#include <string>

namespace editor::asset_path {

// Abs → rel. Pl. ("C:\proj\assets\models\foo.iqm", "C:\proj")
// → "assets/models/foo.iqm". Forward-slash normalizált.
// Ha az `abs` NEM projekt-en belül van, az `abs`-t adja vissza változatlan
// (forward-slash normalize-zal).
std::string toProjectRelative(const std::string& abs,
                              const std::string& projectRoot);

// Rel → abs. Pl. ("assets/models/foo.iqm", "C:\proj")
// → "C:/proj/assets/models/foo.iqm". Ha az input már abs, változatlan.
std::string toAbsolute(const std::string& rel,
                       const std::string& projectRoot);

// true ha az `abs` path a projekt-mappa alatt van. Lexically-normal
// összehasonlítás (`/foo/../bar` → `/bar`).
bool isWithinProject(const std::string& abs,
                     const std::string& projectRoot);

// true ha a path abszolút (Windows: drive-letter vagy UNC; POSIX: '/').
bool isAbsolute(const std::string& path);

}  // namespace editor::asset_path
