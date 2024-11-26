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

if [[ "${BUILD_TARGET}" == "windows" ]]; then
    echo "===== Skipping Mesa GLU (not needed for Windows cross-compilation) ====="
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
    echo "===== Skipping Mesa GLU (not needed for MacOS) ====="
else
    echo "===== Building Mesa GLU ====="
    cd "${script_dir}/lib/mesa-glu"
    [[ -d build ]] && (rm -Rf build || die "Mesa GLU clean failed")
    mkdir build || die "Mesa GLU mkdir failed"
    cd build
    meson setup -Dgl_provider=glvnd .. || die "Mesa GLU Meson setup failed"
    meson compile || die "Mesa GLU Meson compile failed"
    old_ifs="${IFS}"
    IFS=$'\n'
    for file in $(find src/ -name \*.${BUILD_TARGET_DYNLIB_EXT}\* -and -not -name \*.p); do
        cp -a "${file}" "${script_dir}/lib/_build/" || die "Mesa GLU copy failed (1: ${file})"
    done
    IFS="${old_ifs}"
    cp -a src/libGLU.a "${script_dir}/lib/_build/" || die "Mesa GLU copy failed (2)"
    cp ../include/GL/* "${script_dir}/lib/_build/GL/" || die "Mesa GLU copy failed (3)"
fi
echo
    
if [[ "${BUILD_TARGET}" != "windows" ]]; then
    echo "===== Skipping FreeGLUT (only needed for Windows cross-compilation) ====="
else
    echo "===== Building FreeGLUT ====="
    cd "${script_dir}/lib/freeglut"
    [[ -d build ]] && (rm -Rf build || die "FreeGLUT clean failed")
    mkdir build || die "FreeGLUT mkdir failed"
    cd build
    cmake \
        -DGNU_HOST=x86_64-w64-mingw32 \
        -DCMAKE_TOOLCHAIN_FILE="${script_dir}/lib/freeglut/mingw_cross_toolchain.cmake" \
        -DOpenGL_GL_PREFERENCE=GLVND \
        -DFREEGLUT_BUILD_DEMOS=Off \
        -DFREEGLUT_BUILD_SHARED_LIBS=Off \
        -DFREEGLUT_BUILD_STATIC_LIBS=On \
        -DFREEGLUT_GLES=Off \
        -DFREEGLUT_WAYLAND=Off \
        ..
    make -j${num_jobs} || die "FreeGLUT make failed"
    mkdir -p "${script_dir}/lib/_build/GL" || die "Failed to create GL target directory"
    cp -a lib/lib*glut*.a "${script_dir}/lib/_build/" || die "FreeGLUT copy failed (1)"
    cp ../include/GL/* "${script_dir}/lib/_build/GL/" || die "FreeGLUT GLU copy failed (2)"
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
    make || die "Failed to prepare GLEW's \"auto\" directory"
cd "${script_dir}/lib/glew"
echo

echo "==== Building main directory ===="
make -j${num_jobs} || die "Failed to make GLEW"
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
cd "${script_dir}/lib/xsb_public_sk"
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
"${CPP_COMPILER}" ${CPP_COMPILER_ARGS} -c -O2 -fPIC -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} ImgFontAtlas.cpp || die "Failed to compile ImgFontAtlas.cpp"
"${CPP_COMPILER}" ${CPP_COMPILER_ARGS} -c -O2 -fPIC -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} ImgWindow.cpp || die "Failed to compile ImgWindow.cpp"
echo
echo "==== Installing ===="
cp -a "${script_dir}"/lib/xsb_public_sk/{ImgWindow,ImgFontAtlas}.{h,o} "${script_dir}/lib/_build/" || die "xsb_public_sk copy failed"
echo

echo Lib build complete.
