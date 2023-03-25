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

source _build_target.sh || die "Failed to include build target script"

. user.cfg

DEPLOYDIR=""
if [[ "${XPLANE_TARGET_MAJOR}" == "11" ]]; then
    DEPLOYDIR="${DEPLOYDIR_XP11:-}"
elif [[ "${XPLANE_TARGET_MAJOR}" == "12" ]]; then
    DEPLOYDIR="${DEPLOYDIR_XP12:-}"
else
    die "unhandled X-Plane major version ${XPLANE_TARGET_MAJOR}"
fi

if [[ "${DEPLOYDIR}" == "" ]]; then
    echo "No deployment directory configured for X-Plane ${XPLANE_TARGET_MAJOR}, skipping deployment."
    exit 0
fi

DEPLOYSUBDIR="${DEPLOYDIR}/${XPLANE_PLATFORM_ID}"
mkdir -p "${DEPLOYSUBDIR}" || die "Failed to make sure deployment directory ${DEPLOYSUBDIR} exists"
cp release/xprc/${XPLANE_PLATFORM_ID}/* "${DEPLOYSUBDIR}/" || die "Failed to deploy plugin to ${DEPLOYSUBDIR}"

echo "Deployed to ${DEPLOYSUBDIR}"
