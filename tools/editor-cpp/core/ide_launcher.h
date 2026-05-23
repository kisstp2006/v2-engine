#pragma once

// IdeLauncher (Phase 6c). Thin wrapper around `ide_helper.hpp`:
//   - cached `detect_all()` on first use (the registration scan can take
//     50-200ms — we don't want it every frame),
//   - "preferred for Lua" priority: VSCode > VSCode Insiders > first detected,
//   - `openFile()` / `openFileAt()` simple API for the Project panel / Script
//     Inspector to call.
//
// If the user has no IDE installed, `openFile()` returns false and the
// caller logs the warning.

#include <optional>
#include <string>
#include <vector>

#include "ide_helper.hpp"

namespace editor {

class IdeLauncher {
public:
    static IdeLauncher& instance();

    // Refreshes the cached detected-list (expensive; only on demand).
    void refresh();

    // Query from the cache — runs `refresh()` automatically on first use.
    const std::vector<ide_helper::IDEInfo>& available();

    // Returns a preferred IDE for editing .lua (VSCode priority, otherwise
    // the first detected). `std::nullopt` if there is nothing.
    std::optional<ide_helper::IDEInfo> preferredForLua();

    // Opens the given abs-file in the preferred IDE. true on success.
    bool openFile(const std::string& absPath);

    // Opens a file at a specific line/column (VSCode only).
    bool openFileAt(const std::string& absPath, int line = -1, int column = -1);

private:
    IdeLauncher() = default;

    bool                                   detected_ = false;
    std::vector<ide_helper::IDEInfo>       cache_;
};

}  // namespace editor
