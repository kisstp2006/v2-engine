#pragma once

#include <any>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace editor {

// Simple in-process event bus.
// on("foo", h) → every emit("foo", data) calls every subscribed h.
// Typing is loose (std::any) — subscribers must agree on the data type.
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
