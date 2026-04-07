#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}"
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

build_info_path="${src_dir}/_buildinfo.h"

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

## GENERATE BUILD INFO FILE
if [[ "${XPRC_SERVER_BUILD_REF:-}" != "" ]]; then
    echo "!! Build version reference has been overridden to: ${XPRC_SERVER_BUILD_REF}"
elif [[ ! -d .git ]]; then
    echo "!! Building without git repository, build version reference will be missing"
else
    XPRC_SERVER_BUILD_REF="$(git rev-parse HEAD)"
    
    tag="$(git describe --tags --exact-match 2>/dev/null || echo -n)"
    if [[ "$tag" != "" ]]; then
        XPRC_SERVER_BUILD_REF="${tag}@${XPRC_SERVER_BUILD_REF}"
    fi
    
    if [[ "$(git status --porcelain)" != "" ]]; then
        XPRC_SERVER_BUILD_REF="${XPRC_SERVER_BUILD_REF}(modified)"
    fi
fi

cat >"${build_info_path}" <<EOF
#ifndef XPRC__BUILDINFO_H
#define XPRC__BUILDINFO_H

#define XPRC_SERVER_ID "${XPRC_SERVER_ID:-de.energiequant.xprc}"
#define XPRC_SERVER_NAME "${XPRC_SERVER_NAME:-XPRC}"
#define XPRC_SERVER_VERSION "${XPRC_SERVER_VERSION:-dev}"
#define XPRC_SERVER_WEBSITE "${XPRC_SERVER_WEBSITE:-}"
#define XPRC_SERVER_BUILD_ID "${XPRC_SERVER_BUILD_ID:-}"
#define XPRC_SERVER_BUILD_REF "${XPRC_SERVER_BUILD_REF:-}"
#define XPRC_SERVER_BUILD_TARGET "${XPLANE_TARGET} ${BUILD_TARGET} ${BUILD_SYSTEM}"
#define XPRC_SERVER_BUILD_TIME "$(date -u +'%Y-%m-%dT%H:%M:%SZ')"

#endif //XPRC__BUILDINFO_H
EOF

echo
echo "------ BUILD INFO FILE: ${build_info_path}"
cat "${build_info_path}"
echo "------ END OF BUILD INFO FILE"
echo

echo
echo "------ BUILD LICENSE SOURCE FILES"
if [[ "${XPRC_SKIP_LICENSE_REGEN:-0}" == "1" ]]; then
  echo "skipped"
else
  python3 tools/license-writer/main.py ${XPRC_LICENSE_PARAMS:-} || die "Failed to generate license source files"
fi
echo "------ END OF BUILD LICENSE SOURCE FILES"
echo

## BUILD
cd "${build_dir}"

cmake -D XPLANE_TARGET="${XPLANE_TARGET}" -D I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS="${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" .. || die "CMake failed"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
    MSYS_NO_PATHCONV=1 msbuild.exe xprc.vcxproj /t:Build /p:Configuration=Release || die "msbuild xprc failed"
    MSYS_NO_PATHCONV=1 msbuild.exe test-hashmap.vcxproj /t:Build /p:Configuration=Release || die "msbuild test-hashmap failed"
    MSYS_NO_PATHCONV=1 msbuild.exe test-list.vcxproj /t:Build /p:Configuration=Release || die "msbuild test-list failed"
    MSYS_NO_PATHCONV=1 msbuild.exe test-prealloc-list.vcxproj /t:Build /p:Configuration=Release || die "msbuild test-prealloc-list failed"
    MSYS_NO_PATHCONV=1 msbuild.exe test-network-addresses.vcxproj /t:Build /p:Configuration=Release || die "msbuild test-network-addresses failed"
    MSYS_NO_PATHCONV=1 msbuild.exe manualtest-licenses.vcxproj /t:Build /p:Configuration=Release || die "msbuild manualtest-licenses failed"
    MSYS_NO_PATHCONV=1 msbuild.exe manualtest-dependencies.vcxproj /t:Build /p:Configuration=Release || die "msbuild manualtest-dependencies failed"
else
    make -j$num_jobs || die "make failed"
fi

## MSVC: change directory
if [[ "$BUILD_SYSTEM" == "vs" ]]; then
    cd Release
fi

## COPY
mkdir -p "${script_dir}/release/xprc/${XPLANE_PLATFORM_ID}" || die "Failed to create release directory ${XPLANE_PLATFORM_ID}"
cp -a xprc.xpl "${script_dir}/release/xprc/${XPLANE_PLATFORM_ID}/xprc.xpl" || die "Failed to copy plugin to release directory ${XPLANE_PLATFORM_ID}"

## TEST
exec_wrapper=""
ext_executable=""
if [[ "${BUILD_TARGET}" == "windows" ]]; then
    ext_executable=".exe"

    if [[ "$HOST_OS_TYPE" != "Windows" ]]; then
        exec_wrapper="wine"
        export WINEDEBUG="-all"
    fi
fi
${exec_wrapper} ./test-hashmap${ext_executable} || die "Failed tests for hashmaps"
${exec_wrapper} ./test-list${ext_executable} || die "Failed tests for lists"
${exec_wrapper} ./test-prealloc-list${ext_executable} || die "Failed tests for preallocated lists"
${exec_wrapper} ./test-network-addresses${ext_executable} || die "Failed tests for network addresses"

${exec_wrapper} ./manualtest-licenses${ext_executable} >/dev/null || die "License information could not be retrieved, check manualtest-licenses"
${exec_wrapper} ./manualtest-dependencies${ext_executable} || die "Dependency information could not be retrieved"

echo Build complete.
