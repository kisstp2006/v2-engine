#pragma once

// IdeLauncher (Phase 6c). Vékony wrapper a `ide_helper.hpp` köré:
//   - első használatkor cache-elt `detect_all()` (a regisztrációs scan
//     50-200ms is lehet — nem akarjuk minden frame-ben),
//   - "preferred for Lua" prioritás: VSCode > VSCode Insiders > első detected,
//   - `openFile()` / `openFileAt()` egyszerű API a Project panel / Script
//     Inspector hívásához.
//
// Ha a felhasználónak nincs telepített IDE-je, az `openFile()` false-t
// ad vissza, és a hívó loggolja a warning-ot.

#include <optional>
#include <string>
#include <vector>

#include "ide_helper.hpp"

namespace editor {

class IdeLauncher {
public:
    static IdeLauncher& instance();

    // Frissíti a cache-elt detected-listát (drága; csak igény szerint).
    void refresh();

    // Kérdezés a cache-ből — első használatkor automatikusan futtat
    // `refresh()`-t.
    const std::vector<ide_helper::IDEInfo>& available();

    // Visszaad egy preferált IDE-t .lua szerkesztéshez (VSCode prioritás,
    // egyébként az első detected). `std::nullopt` ha semmi sincs.
    std::optional<ide_helper::IDEInfo> preferredForLua();

    // Megnyitja a megadott absz-fájlt a preferált IDE-ben. true ha sikerült.
    bool openFile(const std::string& absPath);

    // Megnyit egy fájlt egy konkrét sor/oszlopra (csak VSCode támogatja).
    bool openFileAt(const std::string& absPath, int line = -1, int column = -1);

private:
    IdeLauncher() = default;

    bool                                   detected_ = false;
    std::vector<ide_helper::IDEInfo>       cache_;
};

}  // namespace editor
