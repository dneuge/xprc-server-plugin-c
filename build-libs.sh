#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
download_dir="${root_dir}/lib/_downloads"
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

function cd_real_path {
    cd "$(realpath -e "$1")" || die "Failed to change into real path of $1"
}

source ./_build_target.sh || die "Failed to include build target script"

[[ -d lib/_build ]] && rm -Rf lib/_build
mkdir -p lib/_build
mkdir -p "${script_dir}/lib/_build/GL" || die "Failed to create GL target directory"

num_jobs=$(( $NUM_CPUS + 1 ))

MULTIARCH_FLAGS=""
if [[ "${BUILD_TARGET}" == "macos" ]]; then
    MULTIARCH_FLAGS="-arch x86_64 -arch arm64"
fi

# set locale to default to silence Perl warnings
export LANG="C"
export LC_ALL="C"

# MinGW misses some C11 compatibility, most notably threads.h which is extensively used throughout XPRC
# Mesa seems to have the best maintained C11 compatibility wrapper available, so we borrow that but only if we
# compile for Windows - other systems/environments should hopefully support C11 by now...
if [[ "${BUILD_TARGET}" != "windows" ]]; then
    echo "==== Skipping Mesa (only C11 compatibility) ===="
else
    echo "==== Building Mesa (only C11 compatibility) ===="
    cd_real_path "${script_dir}/lib/mesa/_c11_only"
    [[ -d build ]] && (rm -Rf build || die "Mesa (only C11 compatibility) clean failed")
    mkdir build || die "Mesa (only C11 compatibility) mkdir failed"
    cd build
    cmake .. || die "Mesa (only C11 compatibility) CMake failed"
    make -j$num_jobs || die "Mesa (only C11 compatibility) make failed"
    mkdir -p "${script_dir}/lib/_build/c11" || die "Mesa (only C11 compatibility) mkdir failed"
    cp -a libmesa_c11*.${BUILD_TARGET_DYNLIB_EXT}* "${script_dir}/lib/_build/" || die "Mesa (only C11 compatibility) copy failed (1)"
    cp -a libmesa_c11*.a "${script_dir}/lib/_build/" || die "Mesa (only C11 compatibility) copy failed (2)"
    cp -a ../../src/c11/*.h "${script_dir}/lib/_build/c11" || die "Mesa (only C11 compatibility) copy failed (3)"
    
    if ! grep C_DEFINES CMakeFiles/mesa_c11.dir/flags.make | grep -- -DHAVE_STRUCT_TIMESPEC; then
        echo "- CMake appears NOT to have used -DHAVE_STRUCT_TIMESPEC, keeping headers unmodified"
    else
        echo "- CMake appears to have used -DHAVE_STRUCT_TIMESPEC, persisting in time.h"
        
        cd "${script_dir}/lib/_build/c11"
        patch -p1 <<'EOF'
--- a/time.h  2022-12-19 14:45:40.920933949 +0000
+++ b/time.h  2022-12-19 14:33:26.694721638 +0000
@@ -27,6 +27,7 @@
  * On MSVC `struct timespec` and `timespec_get` present at the same time;
  * So detecting `HAVE_STRUCT_TIMESPEC` in meson script dynamically.
  */
+#define HAVE_STRUCT_TIMESPEC /* set by Frame Buffet during build-libs.sh */
 #ifndef HAVE_STRUCT_TIMESPEC
 struct timespec
 {
EOF
    fi
fi
echo

echo "===== Building GLEW ====="
echo "==== Cleaning main build ===="
cd "${script_dir}/lib/glew"
make clean || die "cleaning main build directory failed"
echo

echo "==== Cleaning \"auto\" ===="
cd "${script_dir}/lib/glew/auto"
make clean || die "cleaning \"auto\" directory failed"
echo

echo "==== Preparing \"auto\" ===="
cd "${script_dir}/lib/glew/auto"
# reference date for chosen commit refs is successful build from 28 Oct 2023
REPO_OPENGL="${download_dir}/github.KhronosGroup.OpenGL-Registry.main.bundle" \
    REF_OPENGL="784b8b340e0429da3be2378bd5217d3c5530b9e5" \
    REPO_EGL="${download_dir}/github.KhronosGroup.EGL-Registry.main.bundle" \
    REF_EGL="b055c9b483e70ecd57b3cf7204db21f5a06f9ffe" \
    REPO_GLFIXES="${download_dir}/github.nigels-com.glfixes.bundle" \
    REF_GLFIXES="b63c8d3e676097d610efda64870cbe6f10543bd3" \
    make -j1 || die "Failed to prepare GLEW's \"auto\" directory"
cd "${script_dir}/lib/glew"
echo

echo "==== Building main directory ===="
if [[ "${BUILD_TARGET}" != "macos" ]]; then
    make -j${num_jobs} glew.lib || die "Failed to make GLEW (regular call)"
else
    # GLEW's Makefile does not handle multi-architecture binaries, so we cannot use ar and strip and instead need to do some "post-processing" on our own
    # also silence OpenGL deprecation warnings
    make -j${num_jobs} CFLAGS.EXTRA="${MULTIARCH_FLAGS} -DGL_SILENCE_DEPRECATION" LDFLAGS.EXTRA="${MULTIARCH_FLAGS}" AR= STRIP= glew.lib || die "Failed to make GLEW (MacOS call)"
    
    # we cannot create an actual .a archive but we can create a universal binary
    
    # "strip" would normally be called to strip the .a archive which only contains glew.o, so we simply strip the object file
    strip -x tmp/darwin/default/static/glew.o || die "Failed to strip glew object file"

    # now we can package it to a universal binary and give it a misleading .a extension
    # note that this is NOT an archive but CMake and Apple compiler toolchain will treat it correctly nevertheless
    lipo tmp/darwin/default/static/glew.o -create -output lib/libGLEW.a || die "Failed to package glew object file to universal binary (fake .a)"
fi
echo

echo "==== Installing ===="
mkdir -p "${script_dir}/lib/_build/GL" || die "Failed to create GL target directory"
cp "${script_dir}/lib/glew/include/GL/"* "${script_dir}/lib/_build/GL/" || die "Failed to copy GLEW includes"
cp "${script_dir}/lib/glew/lib/"* "${script_dir}/lib/_build/" || die "Failed to copy GLEW lib"
echo

echo "===== Building (c)imgui ====="
cd "${script_dir}/lib/cimgui"

echo "==== Symlinking imgui directory into cimgui ===="
([[ -e "imgui" ]] && rm -Rf "imgui") || die "Failed to remove previous imgui from cimgui"
ln -s "../imgui" "imgui" || die "Failed to create symlink"

echo "==== Building cimgui ===="
[[ -d build ]] && (rm -Rf build || die "cimgui clean failed")
mkdir build || die "cimgui mkdir failed"
cd build
cmake \
    -D IMGUI_STATIC=yes \
    -D IMGUI_FREETYPE=no \
    -D CMAKE_POSITION_INDEPENDENT_CODE=TRUE \
    -D CMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
    ..
make -j${num_jobs} || die "cimgui make failed"
cp -a cimgui.a "${script_dir}/lib/_build/" || die "cimgui copy (1) failed"
cp -a ../cimgui.h "${script_dir}/lib/_build/" || die "cimgui copy (2) failed"
echo


echo "==== Installing ===="
cp -a "${script_dir}/lib/imgui/imgui.h" "${script_dir}/lib/_build/" || die "imgui header copy (1) failed"
cp -a "${script_dir}/lib/imgui/imconfig.h" "${script_dir}/lib/_build/" || die "imgui header copy (2) failed"
echo


echo "===== Building XSB Public (forked) / ImgWindow ====="
echo "==== Compiling ===="
cd "${script_dir}/lib/xsb_public_fork"
XPLM400=0
if [[ "${XPLANE_TARGET}" == "12.04" ]]; then
    XPLM400=1
fi
TARGET_OSX=0
TARGET_LIN=0
TARGET_WIN=0
if [[ "${BUILD_TARGET}" == "linux" ]]; then
    TARGET_LIN=1
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
    TARGET_WIN=1
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
    TARGET_OSX=1
else
    die "Unknown target system: ${BUILD_TARGET}"
fi
"${CPP_COMPILER}" ${CPP_COMPILER_ARGS} -c -O2 -fPIC ${MULTIARCH_FLAGS} -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} -DGLEW_NO_GLU ImgFontAtlas.cpp || die "Failed to compile ImgFontAtlas.cpp"
"${CPP_COMPILER}" ${CPP_COMPILER_ARGS} -c -O2 -fPIC ${MULTIARCH_FLAGS} -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} -DGLEW_NO_GLU ImgWindow.cpp || die "Failed to compile ImgWindow.cpp"
echo
echo "==== Installing ===="
cp -a "${script_dir}"/lib/xsb_public_fork/{ImgWindow,ImgFontAtlas}.{h,o} "${script_dir}/lib/_build/" || die "xsb_public_fork copy failed"
echo

echo Lib build complete.
