#pragma once

// MainThreadQueue — background-thread → main-thread callback executor
// (Phase 5a). A worker-thread `enqueue(fn)`-nel adja fel a UI-érintő
// műveleteket; a main-thread minden frame-ben `drainOnMainThread()`-tel
// futtatja őket sorban. Így nem kell ImGui-t / EventBus-t direkt thread-
// safe-vé tenni — minden mutáció a main-thread-en történik.

#include <functional>
#include <mutex>
#include <vector>

namespace editor {

class MainThreadQueue {
public:
    // Background-thread-ből hívható. A `fn` a következő drain-kor fut.
    void enqueue(std::function<void()> fn);

    // Main-thread-en hívandó (EditorApp::drawFrame elején). Az összes
    // pending callback-et lefuttatja sorrendben. Új enqueue() közben is
    // biztonságos — a snapshot-listát mutex alatt vesszük ki, és üres
    // queue-n hívunk a callback-eket (más szálak közben tovább adhatnak fel).
    void drainOnMainThread();

private:
    std::mutex                          mtx_;
    std::vector<std::function<void()>>  pending_;
};

}  // namespace editor
