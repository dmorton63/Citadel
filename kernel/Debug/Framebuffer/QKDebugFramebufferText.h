#pragma once

#include "QCTypes.h"

namespace QK::Debug::FramebufferText
{
    // Initialize a tiny framebuffer text renderer from Limine's framebuffer request.
    // Returns false if no framebuffer response is available.
    bool InitFromLimineRequest(QC::u64 FramebufferRequest[]);

    bool IsReady();

    // Enable/disable rendering output (useful when the desktop takes over the framebuffer).
    void SetEnabled(bool Enabled);

    // Scrollback controls (512-line ring buffer).
    // When scrolled up, new output continues buffering but the on-screen viewport is frozen.
    void PageUp();
    void PageDown();
    void FollowTail();
    bool IsViewingHistory();

    // Render text to the framebuffer (best-effort). Safe to call even if not ready.
    void Write(const char *Message);
}
