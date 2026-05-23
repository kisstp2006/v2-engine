#pragma once

#include <string>
#include <vector>

namespace editor {

// Persistent MRU list of opened project folders.
// Location: %USERPROFILE%/.config/v2-editor-cpp/projects.txt (one path per line).
class RecentProjects {
public:
    void load();
    void save() const;
    void touch(const std::string& path);  // bump to top, dedup, cap at 8.

    const std::vector<std::string>& entries() const { return entries_; }
    static std::string configDir();
    static std::string filePath();

private:
    static constexpr size_t kMax = 8;
    std::vector<std::string> entries_;
};

}  // namespace editor
