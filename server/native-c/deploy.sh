#!/bin/bash

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

function die {
    echo $@
    exit 1
}

if [[ ! -e user.cfg ]]; then
    echo "Missing user.cfg, skipping deployment."
    exit 0
fi

. user.cfg


mkdir -p "${DEPLOYDIR}/lin_x64" || die "Failed to make sure deployment directory exists"
cp release/xprc/lin_x64/* "${DEPLOYDIR}/lin_x64/" || die "Failed to deploy Linux plugin to ${DEPLOYDIR}"

echo Deployed.
