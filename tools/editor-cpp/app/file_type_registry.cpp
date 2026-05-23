// STL ELŐSZÖR.
#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include "file_type_registry.h"

namespace editor {

namespace {

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool endsWith(const std::string& path, const std::string& ext) {
    if (path.size() < ext.size()) return false;
    return path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
}

}  // namespace

FileTypeRegistry& FileTypeRegistry::instance() {
    static FileTypeRegistry r;
    return r;
}

void FileTypeRegistry::registerHandler(FileTypeHandler h) {
    handlers_.push_back(std::move(h));
}

const FileTypeHandler* FileTypeRegistry::handlerFor(const std::string& path) const {
    const std::string p = toLower(path);
    const FileTypeHandler* best = nullptr;
    size_t                 bestLen = 0;
    for (const auto& h : handlers_) {
        for (const auto& ext : h.extensions) {
            const std::string lext = toLower(ext);
            if (endsWith(p, lext) && lext.size() > bestLen) {
                best = &h;
                bestLen = lext.size();
            }
        }
    }
    return best;
}

FileTypeRegistrar::FileTypeRegistrar(FileTypeHandler h) {
    FileTypeRegistry::instance().registerHandler(std::move(h));
}

}  // namespace editor
