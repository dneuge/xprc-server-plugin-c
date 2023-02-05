#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
root_dir="${script_dir}/.."
cd "${script_dir}"

src_dir="${script_dir}/src"
build_dir="${script_dir}/build"
release_dir="${script_dir}/release"

function die {
    echo $@
    exit 1
}

source _build_target.sh || die "Failed to include build target script"

num_cpus=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
num_jobs=$(( $num_cpus + 1 ))

[[ -d "${build_dir}" ]] && rm -Rf "${build_dir}"
mkdir -p "${build_dir}"

[[ -d "${release_dir}" ]] && rm -Rf "${release_dir}"
mkdir -p "${release_dir}"

## BUILD
cd "${build_dir}"

cmake .. || die "CMake failed"
make -j$num_jobs || die "make failed"

## COPY
#mkdir -p "${script_dir}/release/psxvc/lin_x64" || die "Failed to create release directory lin_64"
#cp -a lin.xpl "${script_dir}/release/psxvc/lin_x64/psxvc.xpl" || die "Failed to copy Linux plugin to release directory"

echo Build complete.
