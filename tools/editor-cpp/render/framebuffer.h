#pragma once

#include "engine.h"
#ifdef obj
#undef obj
#endif

namespace editor {

// DRY-only helpers for per-panel framebuffer lifecycle. The three viewport
// panels (Scene, Game, Scene2D) used to share a verbatim copy of these.
// Kept as free functions (not a struct/class) because all panels already
// hold `fbo_t fbo_; int width_; int height_;` as private members; rewriting
// all 41 call sites would churn the panels without changing how the renderer
// uses the framebuffer.
//
// Hot path: each `ensure()` is called once per panel per frame; the no-op
// branch (size unchanged + fbo alive) is two int compares + a short-circuit.
// Retail builds (/LTCG) inline both functions, so there is zero overhead
// vs. the inline copies they replace.

// (Re)allocates `fbo` when the requested size differs from (w,h). No-op
// otherwise. `flags` is passed to the motor's `fbo()` factory (0 = default).
void framebuffer_ensure(fbo_t& fbo, int& w, int& h,
                        int newW, int newH, unsigned flags = 0);

// Destroys `fbo` (if alive) and zeros the size cache. Safe to call multiple
// times. Panel destructors call this.
void framebuffer_destroy(fbo_t& fbo, int& w, int& h);

}  // namespace editor
