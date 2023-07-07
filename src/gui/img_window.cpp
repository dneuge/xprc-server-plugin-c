#include "img_window.h"

ImgWindowWrapper::ImgWindowWrapper(int left, int top, int right, int bottom, XPLMWindowDecoration decoration,
                                   XPLMWindowLayer layer, img_window_build_interface_f build_interface,
                                   img_window_on_show_f on_show, void *ref) : ImgWindow(left, top, right, bottom,
                                                                                        decoration, layer) {
    this->build_interface = build_interface;
    this->on_show = on_show;
    this->ref = ref;

    //printf("[XPRC] ImgWindowWrapper::ImgWindowWrapper done\n"); // DEBUG
}

ImgWindowWrapper::~ImgWindowWrapper() = default;

void ImgWindowWrapper::buildInterface() {
    //printf("[XPRC] ImgWindowWrapper::buildInterface()\n"); // DEBUG

    this->build_interface(this, this->ref);
}

bool ImgWindowWrapper::onShow() {
    //printf("[XPRC] ImgWindowWrapper::onShow()\n"); // DEBUG

    if (this->on_show) {
        return this->on_show(this, this->ref);
    }
    return true;
}

void ImgWindowWrapper::_safeDelete() {
    this->SafeDelete();
}

void ImgWindowWrapper::_setWindowTitle(const std::string &title) {
    this->SetWindowTitle(title);
}

void img_window_init_globals() {
    //printf("[XPRC] img_window_init_globals()\n"); // DEBUG
    ImgWindow::sFontAtlas = std::make_shared<ImgFontAtlas>();
    //printf("[XPRC] img_window_init_globals done\n"); // DEBUG
}

void img_window_destroy_globals() {
    //printf("[XPRC] img_window_destroy_globals()\n"); // DEBUG
    ImgWindow::sFontAtlas.reset();
    //printf("[XPRC] img_window_destroy_globals done\n"); // DEBUG
}

img_window img_window_create(int left, int top, int right, int bottom, XPLMWindowDecoration decoration, XPLMWindowLayer layer, img_window_build_interface_f build_interface, img_window_on_show_f on_show, void *ref) {
    //printf("[XPRC] img_window_create(%d, %d, %d, %d, %d, %d, %p, %p, %p)\n", left, top, right, bottom, decoration, layer, build_interface, on_show, ref); // DEBUG

    if (!build_interface) {
        return nullptr;
    }

    try {
        return new ImgWindowWrapper(left, top, right, bottom, decoration, layer, build_interface, on_show, ref);
    } catch (...) {
        return nullptr;
    }
}

static std::string nonEmptyCString(char *s) {
    if (!s) {
        // null must not be provided to std::string ("undefined behaviour")
        // in case a null-pointer would get passed on, an assertion in ImGui would fail
        return "<NULL>";
    } else if (!s[0]) {
        // ImGui does not allow empty titles
        return "<EMPTY>";
    } else {
        return std::string(s);
    }
}

void img_window_set_title(img_window window, char *title) {
    try {
        window->_setWindowTitle(nonEmptyCString(title));
    } catch (...) {
        // do nothing
    }
}

void img_window_set_visible(img_window window, bool visible) {
    //printf("[XPRC] img_window_set_visible(%p, %d)\n", window, visible); // DEBUG

    if (!window) {
        return;
    }

    try {
        window->SetVisible(visible);
    } catch (...) {
        // do nothing
    }
}

void img_window_safe_destroy(img_window window) {
    //printf("[XPRC] img_window_safe_destroy(%p)\n", window); // DEBUG

    if (!window) {
        return;
    }

    try {
        window->_safeDelete();
    } catch (...) {
        // do nothing
    }

    //printf("[XPRC] img_window_safe_destroy: done\n"); // DEBUG
}

void img_window_destroy(img_window window) {
    //printf("[XPRC] img_window_destroy(%p)\n", window); // DEBUG

    if (!window) {
        return;
    }

    try {
        delete window;
    } catch (...) {
        // do nothing
    }

    //printf("[XPRC] img_window_destroy: done\n"); // DEBUG
}
