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

source ./_build_target.sh || die "Failed to include build target script"

[[ -d lib/_build ]] && rm -Rf lib/_build
mkdir -p lib/_build

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

echo "===== Building Mesa GLU ====="
cd "${script_dir}/lib/mesa-glu"
[[ -d build ]] && (rm -Rf build || die "Mesa GLU clean failed")
mkdir build || die "Mesa GLU mkdir failed"
cd build
meson setup -Dgl_provider=glvnd .. || die "Mesa GLU Meson setup failed"
meson compile || die "Mesa GLU Meson compile failed"
mkdir -p "${script_dir}/lib/_build/GL" || die "Failed to create GL target directory"
old_ifs="${IFS}"
IFS=$'\n'
for file in $(find src/ -name \*.${BUILD_TARGET_DYNLIB_EXT}\* -and -not -name \*.p); do
    cp -a "${file}" "${script_dir}/lib/_build/" || die "Mesa GLU copy failed (1: ${file})"
done
IFS="${old_ifs}"
cp -a src/libGLU.a "${script_dir}/lib/_build/" || die "Mesa GLU copy failed (2)"
cp ../include/GL/* "${script_dir}/lib/_build/GL/" || die "Mesa GLU copy failed (3)"
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
REPO_OPENGL="${download_dir}/github.KhronosGroup.OpenGL-Registry.main.bundle" REPO_EGL="${download_dir}/github.KhronosGroup.EGL-Registry.main.bundle" REPO_GLFIXES="${download_dir}/github.nigels-com.glfixes.bundle" make || die "Failed to prepare GLEW's \"auto\" directory"
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
fi
if [[ "${BUILD_TARGET}" == "windows" ]]; then
    TARGET_WIN=1
fi
g++ -c -O2 -fPIC -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} ImgFontAtlas.cpp || die "Failed to compile ImgFontAtlas.cpp"
g++ -c -O2 -fPIC -I../_build/ -I../XPSDK/CHeaders/XPLM -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM303=1 -DXPLM400=${XPLM400} -DAPL=${TARGET_OSX} -DIBM=${TARGET_WIN} -DLIN=${TARGET_LIN} ImgWindow.cpp || die "Failed to compile ImgWindow.cpp"
echo
echo "==== Installing ===="
cp -a "${script_dir}"/lib/xsb_public_sk/{ImgWindow,ImgFontAtlas}.{h,o} "${script_dir}/lib/_build/" || die "xsb_public_sk copy failed"
echo

echo Lib build complete.
