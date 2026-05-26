// STL FIRST — engine.h macros (`set`, `obj`, `is`, ...) collide with
// <xlocale> internals if STL is included after it.
#include <chrono>
#include <filesystem>
#include <system_error>

#include "asset_cache.h"

namespace editor {

uint64_t mtimeNs(const std::string& path) {
    std::error_code ec;
    auto t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
        t.time_since_epoch()).count();
}

double nowSeconds() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()).count();
}

void FailedPathSet::insert(const std::string& path) {
    entries_[path] = nowSeconds();
}

void FailedPathSet::erase(const std::string& path) {
    entries_.erase(path);
}

bool FailedPathSet::isFresh(const std::string& path) const {
    auto it = entries_.find(path);
    if (it == entries_.end()) return false;
    return (nowSeconds() - it->second) < retry_after;
}

bool FailedPathSet::shouldRetry(const std::string& path) const {
    return !isFresh(path);
}

}  // namespace editor
