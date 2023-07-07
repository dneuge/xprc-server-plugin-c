#ifndef IMGWINDOW_WRAPPER_H
#define IMGWINDOW_WRAPPER_H

/* Wrapper to use ImgWindow from C instead of C++.
 * Refer to original sources from ImgWindow in xsb_public (and/or forked sources as used by this project).
 *
 * As the original interface and code documentation are mostly taken over, the original copyright applies to
 * the wrapper's interface definition:
 *
 *
 * Copyright (C) 2018,2020 Christopher Collins
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <XPLMDataAccess.h>
#include <XPLMDisplay.h>
#include <XPLMGraphics.h>

#ifdef __cplusplus
extern "C" {
    class ImgWindowWrapper;
    typedef ImgWindowWrapper* img_window;
#else
    typedef void* img_window;
#endif

    /**
     * Will be called every frame the window is drawn; used to define the ImGui interface
     * and handle events.
     * @param window reference to wrapped ImgWindow
     * @param ref reference pointer as provided during construction
     * @note You must NOT destroy the window inside this callback -
     *     use img_window_safe_delete for that.
     */
    typedef void (*img_window_build_interface_f)(img_window window, void *ref);

    /**
     * Will be called before making the window visible. It provides an
     * opportunity to prevent the window being shown.
     *
     * @note the implementation in the base-class is a null handler.  You can
     *     safely override this without chaining.
     *
     * @param window reference to wrapped ImgWindow
     * @param ref reference pointer as provided during construction
     * @return true if the window should be shown, false if the attempt to show
     *     should be suppressed
     */
    typedef bool (*img_window_on_show_f)(img_window window, void *ref);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <ImgWindow.h>

class ImgWindowWrapper : public ImgWindow {
public:
    ImgWindowWrapper(int left, int top, int right, int bottom, XPLMWindowDecoration decoration, XPLMWindowLayer layer, img_window_build_interface_f build_interface, img_window_on_show_f on_show, void *ref);
    ~ImgWindowWrapper() override;
    void _safeDelete();
    void _setWindowTitle(const std::string &title);

protected:
    void buildInterface() override;
    bool onShow() override;

private:
    img_window_build_interface_f build_interface;
    img_window_on_show_f on_show;
    void *ref;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif
    /**
     * Initializes global variables. Must be called exactly once before any other action.
     */
    void img_window_init_globals();

    /**
     * Destroys global variables. Must be called at most once after all window instances have been destroyed.
     */
    void img_window_destroy_globals();

    /**
     * Construct a window with the specified bounds
     *
     * @param left Left edge of the window's contents in global boxels.
     * @param top Top edge of the window's contents in global boxels.
     * @param right Right edge of the window's contents in global boxels.
     * @param bottom Bottom edge of the window's contents in global boxels.
     * @param decoration The decoration style to use (see notes)
     * @param layer the preferred layer to present this window in (see notes)
     * @param build_interface will be called each frame for window contents and event handling
     * @param on_show optional override to suppress opening windows; set to null if not needed
     * @param ref reference pointer provided to callbacks
     *
     * @note The decoration should generally be one presented/rendered by XP -
     *     the ImGui window decorations are very intentionally supressed by
     *     ImgWindow to allow them to fit in with the rest of the simulator.
     *
     * @note The only layers that really make sense are Floating and Modal.  Do
     *     not set VR layer here however unless the window is ONLY to be
     *     rendered in VR.
     */
    img_window img_window_create(int left, int top, int right, int bottom, XPLMWindowDecoration decoration, XPLMWindowLayer layer, img_window_build_interface_f build_interface, img_window_on_show_f on_show, void *ref);

    /**
     * Shows or hides the given window. The window's on_show callback can override the request.
     * It is also at this time that the window will be relocated onto the VR
     * display if the VR headset is in use.
     *
     * @param window window instance to manipulate
     * @param visible true to be displayed, false if the window is to be
     * hidden.
     */
    void img_window_set_visible(img_window window, bool visible);

    /**
     * Sets the title of the window both in the ImGui layer and at the XPLM layer.
     * Must be called at least once before setting the window visible.
     *
     * @param window window instance to manipulate
     * @param title the title to set; will be copied, original resource is managed by caller
     */
    void img_window_set_title(img_window window, char *title);

    /**
     * Can be used from within img_window_build_interface_f to destroy once it has finished rendering this frame.
     * @param window window instance to destroy
     */
    void img_window_safe_destroy(img_window window);

    /**
     * Immediately destroys the window. Must not be called from inside callbacks; use img_window_safe_destroy instead.
     * @param window window instance to destroy
     */
    void img_window_destroy(img_window window);

#ifdef __cplusplus
}
#endif

#endif
