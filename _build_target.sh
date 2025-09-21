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

BUILD_TARGET="$(tr '[:upper:]' '[:lower:]' <<<$HOST_OS_TYPE)"
if [[ "$#" -ge 1 ]]; then
	BUILD_TARGET="$1"
fi
export BUILD_TARGET

CMAKE_TOOLCHAIN_FILE=""

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

if [[ "${BUILD_TARGET}" == "linux" ]]; then
	CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-generic_linux-linux-x86_64-clang.cmake"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	if [[ "${HOST_OS_NAME} ${HOST_OS_VERSION}" == "Ubuntu jammy" ]]; then
		CMAKE_TOOLCHAIN_FILE="${root_dir}/TC-ubuntu22.04-windows-x86_64-mingw.cmake"
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
if [[ "${BUILD_TARGET}" == "linux" ]]; then
	CPP_COMPILER="g++"
elif [[ "${BUILD_TARGET}" == "windows" ]]; then
	CPP_COMPILER="x86_64-w64-mingw32-g++"
elif [[ "${BUILD_TARGET}" == "macos" ]]; then
	#CPP_COMPILER="/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/c++"
	CPP_COMPILER="clang++"
	CPP_COMPILER_ARGS="-std=c++11"
else
	die "C++ compiler variable not configured for ${BUILD_TARGET}"
fi
export CPP_COMPILER
export CPP_COMPILER_ARGS

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
