# include from build bash scripts
# first parameter passed to any script including this file is expected to be the build target name
# 
# root_dir must be set to the root directory of XPRC before including this file

function die() {
	echo $@ >&2
	exit 1
}

HOST_OS_TYPE="$(uname | cut -d' ' -f1)"
HOST_OS_ARCH="$(arch)"
HOST_OS_NAME=""
HOST_OS_VERSION=""
if [[ "${HOST_OS_TYPE}" == "Linux" ]]; then
	HOST_OS_NAME="$([[ -f /etc/os-release ]] && source /etc/os-release && echo $NAME)"
	HOST_OS_VERSION="$([[ -f /etc/os-release ]] && source /etc/os-release && [[ "$NAME" == "Ubuntu" ]] && echo $UBUNTU_CODENAME)"
elif [[ "${HOST_OS_TYPE}" == "Darwin" ]]; then
	HOST_OS_TYPE="MacOS"
	HOST_OS_NAME="MacOS"
	HOST_OS_VERSION="$(sw_vers -productVersion)"
elif [[ "${HOST_OS_TYPE}" =~ ^MSYS_NT-.* ]]; then
	HOST_OS_VERSION="${HOST_OS_TYPE/MSYS_/}"
	HOST_OS_TYPE="Windows"
	HOST_OS_NAME="Windows"
elif [[ "${HOST_OS_TYPE}" =~ ^MINGW64_NT-.* ]]; then
	HOST_OS_VERSION="${HOST_OS_TYPE/MINGW64_/}"
	HOST_OS_TYPE="Windows"
	HOST_OS_NAME="Windows"
fi
if [[ "${HOST_OS_NAME}" == "" ]]; then
	HOST_OS_NAME="unknown host system"
	echo "!!! Unknown host system: HOST_OS_NAME=${HOST_OS_NAME} / HOST_OS_TYPE=${HOST_OS_TYPE}"
fi
export HOST_OS_TYPE
export HOST_OS_ARCH
export HOST_OS_NAME
export HOST_OS_VERSION

BUILD_SYSTEM="std"
vs_product_version="unknown"
vs_root_dir="/c/vs_not_found"
if [[ "$HOST_OS_TYPE" == "Windows" ]]; then
	vswhere="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"
	if [[ ! -e "$vswhere" ]]; then
		echo "Visual Studio does not appear to be installed, assuming standard build system"
	else
		vs_product_id=$("$vswhere" -latest -property productId)
		vs_product_version=$("$vswhere" -latest -property catalog_productLineVersion)

		echo "Found Visual Studio: ${vs_product_id} ${vs_product_version}"
		BUILD_SYSTEM="vs"

		vs_root_dir="/c/Program Files (x86)/Microsoft Visual Studio/${vs_product_version}"
		if [[ ! -d "${vs_root_dir}" ]]; then
			vs_root_dir="/c/Program Files/Microsoft Visual Studio/${vs_product_version}"
		fi
		[[ -d "${vs_root_dir}" ]] || die "Visual Studio root directory could not be found"
		echo "Visual Studio root: ${vs_root_dir}"
	fi
fi
export BUILD_SYSTEM

BUILD_TARGET="$(tr '[:upper:]' '[:lower:]' <<<$HOST_OS_TYPE)"
if [[ "$#" -ge 1 ]]; then
	BUILD_TARGET="$1"
fi
export BUILD_TARGET

XPLANE_TARGET="12.04"
if [[ "$#" -ge 2 ]]; then
	XPLANE_TARGET="$2"
fi
export XPLANE_TARGET

re_supported_xp_target='^11|12\.04$'
if [[ ! "${XPLANE_TARGET}" =~ $re_supported_xp_target ]]; then
	die "Unknown X-Plane target version: ${XPLANE_TARGET}"
fi

XPLANE_TARGET_MAJOR=0
re_xp11_version='^11(\..*)?$'
re_xp12_version='^12(\..*)?$'
if [[ "${XPLANE_TARGET}" =~ $re_xp11_version ]]; then
	XPLANE_TARGET_MAJOR=11
elif [[ "${XPLANE_TARGET}" =~ $re_xp12_version ]]; then
	XPLANE_TARGET_MAJOR=12
fi

CMAKE_TOOLCHAIN_FILE=""
if [[ "${CMAKE_TOOLCHAIN_FILE:-}" != "" ]]; then
	echo "Using user-provided CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
elif [[ "${BUILD_TARGET}" == "linux" ]]; then
	CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-generic_linux-linux-x86_64-clang.cmake"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	if [[ "${HOST_OS_NAME} ${HOST_OS_VERSION}" == "Ubuntu jammy" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-ubuntu22.04-windows-x86_64-mingw.cmake"
	elif [[ "${HOST_OS_NAME}" == "Windows" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-windows_github-windows-x86_64-msvc_clang.cmake"
	else
		die "Missing CMake toolchain for ${HOST_OS_NAME} ${HOST_OS_VERSION} (target: ${BUILD_TARGET})"
	fi
fi

BUILD_TARGET_DYNLIB_EXT="so"
if [[ "${BUILD_TARGET}" == "windows" ]]; then
	BUILD_TARGET_DYNLIB_EXT="dll"
elif [[ "${BUILD_TARGET}" != "linux" ]] && [[ "${BUILD_TARGET}" != "macos" ]]; then
	die "Unknown build target: ${BUILD_TARGET}"
fi
export BUILD_TARGET_DYNLIB_EXT

if [[ "${BUILD_TARGET}" == "linux" ]]; then
	XPLANE_PLATFORM_ID="lin_x64"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	XPLANE_PLATFORM_ID="win_x64"
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
	XPLANE_PLATFORM_ID="mac_x64"
else
	die "X-Plane platform ID not set up for ${BUILD_TARGET}"
fi
export XPLANE_PLATFORM_ID

CPP_COMPILER_ARGS=""
CPP_COMPILER="clang++"
if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
	if [[ "Microsoft.VisualStudio.Product.Community" ]]; then
		if [[ "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" != "1" && "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" != "True" ]]; then
			echo "!!! ENABLING COMPILATION WITH VS COMMUNITY EDITION WHICH VOIDS LICENSE CONFORMITY; DO NOT DISTRIBUTE BUILD RESULTS !!!"
		else
			die "Detected Visual Studio Community edition which unfortunately cannot be used for distribution builds due to dependencies using licenses which are not OSI-approved."
		fi
	fi

	CPP_COMPILER="${vs_root_dir}/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"
	if [[ ! -e "${CPP_COMPILER}" ]]; then
		CPP_COMPILER="${vs_root_dir}/Enterprise/VC/Tools/Llvm/x64/bin/clang-cl.exe"
	fi
	[[ -e "${CPP_COMPILER}" ]] || die "Visual Studio Clang could not be found"

	# For options see:
	#   https://github.com/MicrosoftDocs/cpp-docs/blob/main/docs/build/reference/md-mt-ld-use-run-time-library.md
	#
	# /MD selects a runtime library suitable for creating DLLs which is necessary because by default MT_StaticRelease
	# will be selected which is incompatible to linking the main plugin. This can be checked via
	#   "c:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433\bin\Hostx86\x64\dumpbin.exe" /DIRECTIVES ...
	# which must not list
	#   /FAILIFMISMATCH:RuntimeLibrary=MT_StaticRelease
	CPP_COMPILER_ARGS="//MD"
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
	CPP_COMPILER_ARGS="-std=c++11"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
  if [[ "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" != "1" && "${I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS:-False}" != "True" ]]; then
    echo "!!! ENABLING MINGW COMPILATION WHICH VOIDS LICENSE CONFORMITY; DO NOT DISTRIBUTE BUILD RESULTS !!!"
    CPP_COMPILER="x86_64-w64-mingw32-g++"
  fi
fi
export CPP_COMPILER
export CPP_COMPILER_ARGS

if [[ "${BUILD_SYSTEM}" == "vs" ]]; then
	if [[ ! "$(which msbuild.exe)" ]]; then
		echo "MSBuild is not on path, trying known locations..."
		vs_msbuild_dir="${vs_root_dir}/Enterprise/MSBuild/Current/Bin"
		[[ -e "${vs_msbuild_dir}/MSBuild.exe" ]] || die "MSBuild.exe could not be located"
		export PATH="${vs_msbuild_dir}:$PATH"
		echo "MSBuild found in ${vs_msbuild_dir} (added to PATH)"
	fi
fi

if [[ "${HOST_OS_TYPE}" == "MacOS" ]]; then
	NUM_CPUS=$(sysctl -n hw.ncpu)
	cmake_app_bin_path="/Applications/CMake.app/Contents/bin"
	if which cmake; then
		echo "CMake binary found on PATH already..."
	elif [[ -d "${cmake_app_bin_path}" ]]; then
		echo "CMake binary does not exist on PATH, extending by ${cmake_app_bin_path}"
		export PATH="${cmake_app_bin_path}:${PATH}"
	else
		echo "WARNING: CMake binary not found on PATH and also not in ${cmake_bin_path}"
	fi
elif [[ "${HOST_OS_TYPE}" == "Windows" ]]; then
	NUM_CPUS=$NUMBER_OF_PROCESSORS
else
	NUM_CPUS=$(cat /proc/cpuinfo | grep -E 'processor\s*:' | nl | tail -n1 | sed -e 's/\s*\([0-9]\+\)\s.*/\1/')
fi

# normalize paths
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	CMAKE_TOOLCHAIN_FILE=$(realpath "${CMAKE_TOOLCHAIN_FILE}")
fi

echo "Build target: X-Plane ${XPLANE_TARGET} on ${BUILD_TARGET}"
if [[ "${CMAKE_TOOLCHAIN_FILE}" != "" ]]; then
	echo "  applying CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}"
	export CMAKE_TOOLCHAIN_FILE
else
	echo "  no CMake toolchain"
fi
echo
