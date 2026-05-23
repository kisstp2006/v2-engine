#pragma once

#include <string>
#include <vector>

namespace editor {

// Persistent MRU lista a megnyitott projekt-mappákról.
// Hely: %USERPROFILE%/.config/v2-editor-cpp/projects.txt (egy path per sor).
class RecentProjects {
public:
    void load();
    void save() const;
    void touch(const std::string& path);  // top-ra emeli, dedup, cap 8-ra.

    const std::vector<std::string>& entries() const { return entries_; }
    static std::string configDir();
    static std::string filePath();

private:
    static constexpr size_t kMax = 8;
    std::vector<std::string> entries_;
};

}  // namespace editor
