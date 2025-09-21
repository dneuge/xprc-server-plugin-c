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
        
        actual_size="$(wc -c download.tmp | sed -e 's#^[[:space:]]*##g' | cut -d' ' -f1)"
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
    if [[ "$(nl <<<"$subs" | tail -n1 | sed -r -e 's/[[:space:]]*([0-9]+)[[:space:]].*/\1/')" == "1" && -d "${subs}" ]]; then
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

function git_unbundle {
    description="$1"
    extract_dir="$2"
    filename="$3"
    ref="$4"

    cd "${script_dir}/lib"

    if [[ -e "${extract_dir}" ]]; then
        echo "-- ${description} is already unpacked"
        return
    fi

    echo "-- ${description} is missing"

    if [[ "${FETCHLIBS_DOWNLOAD_ONLY:-0}" == "1" ]]; then
        echo "- download only has been requested, skipping extraction..."
        return
    fi

    if [[ ! -f "_downloads/${filename}" ]]; then
        echo "Bundle is missing, Git needs to be mirrored first!"
        exit 1
    fi

    echo "- unbundling ${description} into ${extract_dir}"
    mkdir "${extract_dir}" || die "Directory ${extract_dir} already exists."

    cd "${extract_dir}" || die "Failed to change into ${extract_dir}"
    git init . || die "Failed to initialize Git repository."
    git config --local advice.detachedHead false
    git bundle unbundle "${script_dir}/lib/_downloads/${filename}" || die "git failed to unbundle"

    echo "- switching to ${ref}"
    git checkout "${ref}" || die "git failed to checkout"

    cd "${script_dir}/lib"

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


# NOTE: glew has extra pinned versions defined for its dependencies in build-libs.sh

download "X-Plane SDK" XPSDK XPSDK401.zip 500613 https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK401.zip c1104e83d9b54b03d0084c1db52ee6491e5290994503e8dd2d4a0af637e2bdd7 c5445598297a5ffa6efc9760c3f773e6
download "Mesa" mesa mesa-22.3.1.tar.xz 16972884 https://archive.mesa3d.org/older-versions/22.x/mesa-22.3.1.tar.xz 3c9cd611c0859d307aba0659833386abdca4c86162d3c275ba5be62d16cf31eb 5644bca27c9be0092c98f1ec0e1bedd5
download "GLEW" glew glew-2.2.0.zip 329458 https://github.com/nigels-com/glew/archive/refs/tags/glew-2.2.0.zip 8aaa65f4a8c0fe439b28deb2925862fcdc0549943f08af010eead2a37baa6752 f150f61074d049ff0423b09b18cd1ef6
git_mirror "GLEW glfixes" github.nigels-com.glfixes.bundle https://github.com/nigels-com/glfixes.git glew
git_mirror "Khronos Group OpenGL Registry" github.KhronosGroup.OpenGL-Registry.main.bundle https://github.com/KhronosGroup/OpenGL-Registry.git main
git_mirror "Khronos Group EGL Registry" github.KhronosGroup.EGL-Registry.main.bundle https://github.com/KhronosGroup/EGL-Registry.git main
git_mirror "XSB Public (skiselkov Fork)" github.skiselkov.xsb_public.master.bundle https://github.com/skiselkov/xsb_public.git master
git_unbundle "XSB Public (skiselkov Fork)" xsb_public_sk github.skiselkov.xsb_public.master.bundle 845522196c4fb8f9c3e3ca427bd9b4a1a7ab0b7b
download "Dear ImGui" imgui imgui-1.91.9b.zip 2021831 https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.zip fd37507c8476a6d14cc7c4b352401f31bcbd0f0d995d35390811e968c466f46e 913db10a6ef52623b118142aafba7438
download "cimgui" cimgui cimgui-1.91.9b.zip 385597 https://github.com/cimgui/cimgui/archive/refs/tags/1.91.9b.zip f7c74ef9bfe98c03056d70a0bf77588ca9d35097377d0938d1e42eecc3d86f30 1ed5e987b59ba86a962ed84c87c9d9fb
