// STL FIRST.
#include <optional>
#include <string>
#include <vector>

#include "ide_launcher.h"

namespace editor {

IdeLauncher& IdeLauncher::instance() {
    static IdeLauncher inst;
    return inst;
}

void IdeLauncher::refresh() {
    cache_    = ide_helper::detect_all();
    detected_ = true;
}

const std::vector<ide_helper::IDEInfo>& IdeLauncher::available() {
    if (!detected_) refresh();
    return cache_;
}

std::optional<ide_helper::IDEInfo> IdeLauncher::preferredForLua() {
    const auto& list = available();
    if (list.empty()) return std::nullopt;

    // VSCode priority (Insiders is OK if there is no plain one).
    for (const auto& i : list)
        if (i.id == ide_helper::IDE::VSCode) return i;
    for (const auto& i : list)
        if (i.id == ide_helper::IDE::VSCodeInsiders) return i;

    // Anything (first detected).
    return list.front();
}

bool IdeLauncher::openFile(const std::string& absPath) {
    auto ide = preferredForLua();
    if (!ide) return false;
    return ide_helper::open_in_ide(*ide, absPath);
}

bool IdeLauncher::openFileAt(const std::string& absPath, int line, int column) {
    auto ide = preferredForLua();
    if (!ide) return false;
    return ide_helper::open_at_location(*ide, absPath, line, column);
}

}  // namespace editor
