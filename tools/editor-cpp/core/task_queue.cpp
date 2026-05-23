// STL ELŐSZÖR.
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "task_queue.h"

namespace editor {

void MainThreadQueue::enqueue(std::function<void()> fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> g(mtx_);
    pending_.push_back(std::move(fn));
}

void MainThreadQueue::drainOnMainThread() {
    std::vector<std::function<void()>> snapshot;
    {
        std::lock_guard<std::mutex> g(mtx_);
        snapshot.swap(pending_);   // O(1), kiüríti pending_-t
    }
    // Mutex elengedve — a callback-ek belül enqueue()-zhatnak újra.
    for (auto& fn : snapshot) {
        if (fn) fn();
    }
}

}  // namespace editor
