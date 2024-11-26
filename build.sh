#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

function die {
    echo $@
    exit 1
}

source _build_target.sh || die "Failed to include build target script"

num_jobs=$(( $NUM_CPUS + 1 ))

[[ -d "${build_dir}" ]] && rm -Rf "${build_dir}"
mkdir -p "${build_dir}"

[[ -d "${release_dir}" ]] && rm -Rf "${release_dir}"
mkdir -p "${release_dir}"

## BUILD
cd "${build_dir}"

cmake -D XPLANE_TARGET="${XPLANE_TARGET}" .. || die "CMake failed"
make -j$num_jobs || die "make failed"

## COPY
mkdir -p "${script_dir}/release/xprc/${XPLANE_PLATFORM_ID}" || die "Failed to create release directory ${XPLANE_PLATFORM_ID}"
cp -a xprc.xpl "${script_dir}/release/xprc/${XPLANE_PLATFORM_ID}/xprc.xpl" || die "Failed to copy plugin to release directory ${XPLANE_PLATFORM_ID}"

## TEST
exec_wrapper=""
ext_executable=""
if [[ "${BUILD_TARGET}" == "windows" ]]; then
    exec_wrapper="wine"
    ext_executable=".exe"
    export WINEDEBUG="-all"
fi
${exec_wrapper} ./test-hashmap${ext_executable} || die "Failed tests for hashmaps"
${exec_wrapper} ./test-list${ext_executable} || die "Failed tests for lists"
${exec_wrapper} ./test-prealloc-list${ext_executable} || die "Failed tests for preallocated lists"
${exec_wrapper} ./test-network-addresses${ext_executable} || die "Failed tests for network addresses"

echo Build complete.
