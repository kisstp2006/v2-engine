#pragma once

// MainThreadQueue — background-thread → main-thread callback executor
// (Phase 5a). The worker-thread posts UI-touching operations with
// `enqueue(fn)`; the main-thread runs them in order each frame via
// `drainOnMainThread()`. This way we don't have to make ImGui / EventBus
// directly thread-safe — every mutation happens on the main-thread.

#include <functional>
#include <mutex>
#include <vector>

namespace editor {

class MainThreadQueue {
public:
    // Callable from background-thread. `fn` runs at the next drain.
    void enqueue(std::function<void()> fn);

    // To be called on the main-thread (at the start of EditorApp::drawFrame).
    // Runs every pending callback in order. Safe while new enqueue() calls
    // happen — the snapshot-list is taken under mutex, and callbacks are
    // invoked on an empty queue (other threads can keep posting in the meantime).
    void drainOnMainThread();

private:
    std::mutex                          mtx_;
    std::vector<std::function<void()>>  pending_;
};

}  // namespace editor
