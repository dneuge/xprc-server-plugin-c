((c-mode . (
    (indent-tabs-mode . nil)
    (c-basic-offset . 4)
    (tab-width . 4)
    
    ; TODO: set up for and switch to GCC if somehow possible... plugin is expected to be compiled with GCC, not clang
    (flycheck-clang-definitions . ("APL=0" "IBM=0" "LIN=1"
                                   "BUILD_PLUGIN=1"
                                   "XPLM200=1" "XPLM210=1" "XPLM300=1" "XPLM301=1" "XPLM303=1" "XPLM400=1"
                                   ))
    (flycheck-clang-include-path . ("../lib/XPSDK/CHeaders/Widgets" "../lib/XPSDK/CHeaders/Wrappers" "../lib/XPSDK/CHeaders/XPLM"))
)))
