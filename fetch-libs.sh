#!/bin/bash

# FETCHLIBS_DOWNLOAD_ONLY=1 can be set to only download missing dependencies
# FETCHLIBS_UPDATE_MIRROR=0 can be set to skip updates of existing Git mirror bundles

set -Eeuo pipefail

script_path=$(readlink -f "$0")
script_dir=$(dirname "${script_path}")
cd "${script_dir}"

mkdir -p lib/_downloads
cd lib

function die {
    echo $@
    exit 1
}

function download {
    description="$1"
    extract_dir="$2"
    filename="$3"
    expected_size="$4"
    url="$5"
    expected_shasum="$6"
    expected_mdsum="$7"

    if [[ -e "${extract_dir}" ]]; then
        echo "-- ${description} is already unpacked"
        return
    fi
    
    echo "-- ${description} is missing"

    if [[ -f "_downloads/${filename}" ]]; then
        echo "- using cached ${description} archive"
    else
        echo "- fetching ${description} from ${url}"

        [[ -e download.tmp ]] && (rm download.tmp || die "deleting temporary file download.tmp failed")
        curl "${url}" -L -o download.tmp || die "download failed"
        
        actual_size="$(wc -c download.tmp | cut -d' ' -f1)"
        actual_shasum="$(sha256sum download.tmp | cut -d' ' -f1)"
        actual_mdsum="$(md5sum download.tmp | cut -d' ' -f1)"
        
        if [[ "${actual_size}" != "${expected_size}" || "${actual_shasum}" != "${expected_shasum}" || "${actual_mdsum}" != "${expected_mdsum}" ]]; then
            echo "Download verification failed, check download.tmp:"
            echo " expected size ${expected_size}, got ${actual_size}"
            echo " expected SHA256 ${expected_shasum}, got ${actual_shasum}"
            echo " expected MD5 ${expected_mdsum}, got ${actual_mdsum}"
            exit 1
        fi
        
        mv download.tmp "_downloads/${filename}" || die "Failed to move downloaded file to cache"
    fi
    
    if [[ "${FETCHLIBS_DOWNLOAD_ONLY:-0}" == "1" ]]; then
        echo "- download only has been requested, skipping extraction..."
        return
    fi
    
    echo "- extracting ${description} from ${filename} to ${extract_dir}"
    mkdir "${extract_dir}" || die "Failed to create extraction directory ${extract_dir}"
    
    if [[ "${filename}" =~ \.zip$ ]]; then
        unzip -d "${extract_dir}" "_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
    elif [[ "${filename}" =~ \.7z$ ]]; then
        7z x -o"${extract_dir}" "_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
    elif [[ "${filename}" =~ \.tar\.gz$ ]]; then
        previous_dir="$(pwd)"
        cd "${extract_dir}"
        tar -x -z -f "${previous_dir}/_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
        cd "${previous_dir}"
    elif [[ "${filename}" =~ \.tar\.xz$ ]]; then
        previous_dir="$(pwd)"
        cd "${extract_dir}"
        tar -x -J -f "${previous_dir}/_downloads/${filename}" || die "Failed to unpack ${filename} to ${extract_dir}"
        cd "${previous_dir}"
    else
        rmdir "${extract_dir}"
        die "Unhandled file suffix: ${filename}"
    fi
    
    # if there was a single directory on root level, pull it up
    subs="$(find "${extract_dir}" -mindepth 1 -maxdepth 1)"
    if [[ "$(nl <<<"$subs" | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')" == "1" && -d "${subs}" ]]; then
        [[ ! -e "_tmp" ]] || die "unable to pull up directory, _tmp exists already"
        mv "${subs}" _tmp || die "failed to pull directory ${subs} up (1)"
        rmdir "${extract_dir}" || die "failed to pull directory ${subs} up (2)"
        mv _tmp "${extract_dir}" || die "failed to pull directory ${subs} up (3)"
    fi
    
    echo "- ${description} has been unpacked"
    
    echo "- applying patches"
    if ! ls ${extract_dir}-*.patch >/dev/null 2>&1; then
        echo "no patches found"
    else
        for patch_file in ${extract_dir}-*.patch; do
            echo "applying ${patch_file}"
            patch -p1 <"${patch_file}" || die "patch failed"
        done
    fi
}

function git_mirror {
    description="$1"
    filename="$2"
    url="$3"
    branch="$4"
    
    echo "-- mirroring Git repository for ${description}"

    bundle_file="${script_dir}/lib/_downloads/${filename}"
    mirror_workdir="${script_dir}/lib/_downloads/.git_mirror_work"
    
    if [[ -e "${bundle_file}" ]] && [[ "${FETCHLIBS_UPDATE_MIRROR:-1}" -eq 0 ]]; then
	echo "- bundle already exists and updates have been disabled, skipping..."
        return
    fi

    if [[ -d "${mirror_workdir}" ]]; then
        rm -Rf "${mirror_workdir}" || die "failed to delete git mirror working directory ${mirror_workdir}"
    fi
    
    mkdir -p "${mirror_workdir}" || die "failed to create git mirror working directory ${mirror_workdir}"
    cd "${mirror_workdir}" || die "failed to change into git mirror working directory ${mirror_workdir}"
    
    if [[ -e "${bundle_file}" ]]; then
        echo "- restoring previous state from bundle ${bundle_file}"
        git clone --branch ${branch} "${bundle_file}" . || die "git clone from ${bundle_file} failed for branch ${branch}"
    else
        echo "- initializing repository with branch ${branch}"
        git init . || die "git init failed"
        git checkout -b ${branch} || die "branch ${branch} could not be created"
    fi
    
    echo "- updating from ${url}"
    git pull --tags "${url}" ${branch} || die "pulling ${branch} from ${url} failed"
    
    echo "- archiving to ${bundle_file}"
    git bundle create "${bundle_file}" --branches --tags || die "archival to ${bundle_file} failed"
    
    rm -Rf "${mirror_workdir}" || die "failed to delete git mirror working directory ${mirror_workdir}"
    cd "${script_dir}/lib"
}

download "X-Plane SDK" XPSDK XPSDK401.zip 500613 https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sample_templates/XPSDK401.zip c1104e83d9b54b03d0084c1db52ee6491e5290994503e8dd2d4a0af637e2bdd7 c5445598297a5ffa6efc9760c3f773e6
