#pragma once

// Profile-scope — RAII timer that records into the motor's `profiler` map
// via the editor_profile_record_us helper. Build a per-section sample on
// scope exit, name-keyed (the panel groups + sorts by these names).
//
// Usage:
//     void renderScene(...) {
//         EDITOR_PROFILE("Editor.RenderScene");      // whole-function timer
//         // ...
//         {
//             EDITOR_PROFILE("Editor.RenderScene.ShadowPass");
//             // ...
//         }
//     }
//
// The macro takes a string literal and stamps in a unique scope-variable
// name from __LINE__, so multiple EDITOR_PROFILE blocks in one function
// don't shadow each other.
//
// Name convention: dot-separated, top prefix = "Editor." for our additions.
// The motor's own samples use "Render." and "Sprite." prefixes.

#include "engine.h"

namespace editor {

class ProfileScope {
public:
    explicit ProfileScope(const char* name) noexcept
        : name_(name), start_us_(time_us()) {}

    ~ProfileScope() noexcept {
        double dt = (double)(time_us() - start_us_);
        editor_profile_record_us(name_, dt);
    }

    ProfileScope(const ProfileScope&)            = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    const char* name_;
    uint64_t    start_us_;
};

}  // namespace editor

#define EDITOR_PROFILE_CAT2(a, b) a##b
#define EDITOR_PROFILE_CAT(a, b)  EDITOR_PROFILE_CAT2(a, b)
#define EDITOR_PROFILE(name) \
    ::editor::ProfileScope EDITOR_PROFILE_CAT(_profile_scope_, __LINE__)(name)
