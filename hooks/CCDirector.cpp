#include <Geode/modify/CCEGLView.hpp>
#include <Geode/loader/Log.hpp>
#include "../utils/FramebufferCapture.hpp"

using namespace geode::prelude;

class $modify(CaptureView, CCEGLView) {
    static void onModify(auto& self) {
        // Run at very low priority: after other mods render, right before swap.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4834)
#endif
        auto res = self.setHookPriority("cocos2d::CCEGLView::swapBuffers", -9999);
#ifdef _MSC_VER
#pragma warning(pop)
#endif
        if (res.isErr()) {
            log::warn("[CaptureView] Failed to set hook priority: {}", res.unwrapErr());
        }
    }

    void swapBuffers() {
        if (FramebufferCapture::hasPendingCapture()) {
            FramebufferCapture::processDeferredCallbacks();
            
            log::debug("[CaptureView] Executing capture in swapBuffers (low priority)");
            FramebufferCapture::executeIfPending();
        }

        CCEGLView::swapBuffers();
    }
};

