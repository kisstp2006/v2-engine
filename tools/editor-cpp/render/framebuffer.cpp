#include "framebuffer.h"

namespace editor {

void framebuffer_ensure(fbo_t& fbo, int& w, int& h,
                        int newW, int newH, unsigned flags) {
    if (newW == w && newH == h && fbo.id) return;
    if (fbo.id) fbo_destroy(fbo);
    // `::fbo(...)` — global-namespace call disambiguates from the local
    // `fbo` reference (motor's factory function vs. our parameter).
    fbo = ::fbo((unsigned)newW, (unsigned)newH, flags, 0);
    w = newW;
    h = newH;
}

void framebuffer_destroy(fbo_t& fbo, int& w, int& h) {
    if (fbo.id) {
        ::fbo_destroy(fbo);
        fbo = fbo_t{};
    }
    w = 0;
    h = 0;
}

}  // namespace editor
