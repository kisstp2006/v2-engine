// STL ELŐSZÖR (motor `is(...)` macro ütközés elkerülése).
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "recent_projects.h"

namespace editor {

namespace fs = std::filesystem;

std::string RecentProjects::configDir() {
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home) home = std::getenv("HOME");
    if (!home || !*home) return std::string(".");
    return std::string(home) + "/.config/v2-editor-cpp";
}

std::string RecentProjects::filePath() {
    return configDir() + "/projects.txt";
}

void RecentProjects::load() {
    entries_.clear();
    fs::path p = filePath();
    std::error_code ec;
    if (!fs::exists(p, ec)) return;

    std::ifstream f(p);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        // egyszerű trim-szerű: CR levágás Windows EOL után
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) entries_.push_back(line);
    }
}

void RecentProjects::save() const {
    std::error_code ec;
    fs::create_directories(configDir(), ec);
    std::ofstream f(filePath(), std::ios::trunc);
    if (!f) return;
    for (const auto& e : entries_) {
        f << e << '\n';
    }
}

void RecentProjects::touch(const std::string& path) {
    if (path.empty()) return;
    entries_.erase(std::remove(entries_.begin(), entries_.end(), path),
                   entries_.end());
    entries_.insert(entries_.begin(), path);
    if (entries_.size() > kMax) entries_.resize(kMax);
}

}  // namespace editor
