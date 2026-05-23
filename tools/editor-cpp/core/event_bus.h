#pragma once

#include <any>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

// Egyszerű in-process event bus.
// on("foo", h) → minden emit("foo", data) hív minden feliratkozó h-t.
// A typing laza (std::any) — a feliratkozók egyezzenek meg az adattípuson.
class EventBus {
public:
    using Handler = std::function<void(const std::any&)>;

    void on(const std::string& evt, Handler h) {
        subs_[evt].push_back(std::move(h));
    }

    void emit(const std::string& evt, std::any data = {}) const {
        auto it = subs_.find(evt);
        if (it == subs_.end()) return;
        for (const auto& h : it->second) h(data);
    }

private:
    std::unordered_map<std::string, std::vector<Handler>> subs_;
};

}  // namespace editor
