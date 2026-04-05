# Development Guide

## General Development Guidelines / "Golden Rules"

* maintain/continue existing coding style/formatting for consistent readability
    * do not use tabs, only spaces for indention
* coordinate major changes with the maintainer/main author ahead of implementation
* any change to dependencies or patches needs to be recorded in the [SBOM](sbom.xml)
* do not use "AI" (LLMs), only submit original work - see the [ReadMe](README.md) for an explanation

## Build prerequisites

Quick checklist:

* Bash to run build scripts
  * generally needed CLI tools commonly available on GNU environments (e.g. `patch`)
* Python 3 (reasonably recent version, at least 3.11, tested with 3.13)
* a supported compiler for the target platform

More details follow below. 

## Recommended Environment

* Linux
* [CLion](https://www.jetbrains.com/clion/) IDE (free for non-commercial use as of 2025)
* Clang (see below for other compilers)

## Build Process

The project comes with several build scripts; [`do_all.sh`](do_all.sh) can be used to call them in order:

1. [`fetch-libs.sh`](fetch-libs.sh) downloads all dependencies into `lib` and patches them, if needed
2. [`build-libs.sh`](build-libs.sh) builds all dependencies in `lib` to `lib/_build`
3. [`build.sh`](build.sh)
   - dumps build version information to `src/_buildinfo.h`
   - generates `src/_licenses.c` containing all license texts that need to be available within the build artifact
     (via `tools/license-writer`)
   - builds the plugin itself in `build` and packages it to `release`
4. [`deploy.sh`](deploy.sh) copies the plugin from `release` to X-Plane, if configured in `user.cfg` (see [template](user.cfg.template))

Most scripts accept two optional parameters, target platform (`linux`, `windows`, `macos`) and X-Plane target version
(`11` or `12.04`) which are evaluated to further build configuration by [`_build_target.sh`](_build_target.sh).
If called without those parameters, the current platform and the latest X-Plane version are being
targeted by default.

Compilation with GCC, MinGW or Visual Studio Community is blocked due to possible license violations/uncertainty.
For local development only (keeping the compilation result to yourself), you may choose to override that safety lockout
by declaring `I_WILL_NOT_DISTRIBUTE_BUILD_RESULTS` to `True` or `1`; either as an environment variable (when using the
included build scripts) or in your CMake toolchain configuration (when using an IDE).

In case Python is not available (or only an unsupported correct version) environment variable `XPRC_SKIP_LICENSE_REGEN`
can be set to `1` in order to skip generation of `src/_licenses.c` during `build.sh`. The file must still be present.
Since it is not being committed to the repository, you must supply it from another system/environment instead, if
skipped.

Depending on the target platform...

* when targeting Linux, please build on Linux with standard build tools, CMake and Clang
* when targeting macOS®, please build on a Mac® computer using the latest stable release of the Xcode® development suite
  and any compatible version of CMake
  * cross-building from other platforms requires original Apple SDKs/files and would violate licenses,
    no support or advice will be given and cross-builds should in general not be discussed (public discussions will be
    deleted)
* when targeting Windows:
  * install Git
  * Visual Studio 2022 should be used for compilation
    * When using VS under the free Community license, carefully read the license agreement. Some
      dependencies use licenses not approved by OSI, which is one of the key requirements to be able to use the
      Community edition at least in an "organizational" context. It should probably be okay to use the
      Community edition for local development but redistributable releases would probably violate the license agreement.
  * Cross-compilation is only possible via MinGW which is based on GCC and thus creates licensing issues
    for redistribution in addition to other legal questions regarding the use of Windows APIs without a
    Microsoft-supplied toolchain. \
    Cross-compilation from Linux using MinGW is supported for development but not for distribution (see the
    paragraph above on how to bypass the GCC lockout).

## Protocol Changes

Server plugins need to follow the official protocol specification for compatibility with all clients. Changes to the
general protocol, commands or arguments need to be coordinated through the separate
[specification](https://github.com/dneuge/xprc-protocol) project.

The specification may contain further information on a standard way of adding experimental commands/arguments.

## Releases, Forks

Releases should only be created and published by the [mainline project](https://github.com/dneuge/xprc-server-plugin-c).

The release process involves building the plugin for all platforms on trusted CI nodes/services, packaging it for
redistribution (incl. necessary documentation) and uploading the resulting archive to official project locations.
Build results are reviewed and undergo quick testing before being publicly released.

In case a project fork (not just for contributions but releasing its own artifacts by a new maintainer) should become
necessary in the future:

* please contact the maintainers beforehand to check if a fork is really necessary
* in case the project seems dead, please confirm through public issue trackers, email etc.
* coordinate with other interested parties to avoid multiple concurrent forks
* unless the project turns out to really be dead and you take over mainline maintenance (coordinated with other
  interested parties), please change the project name to avoid confusion/incompatibility

## Acknowledgments

[Git](https://git-scm.com/) and the Git logo are either registered trademarks or trademarks of
[Software Freedom Conservancy, Inc.](https://sfconservancy.org/), corporate home of the Git Project, in the
United States and/or other countries.

Mac, macOS and Xcode are trademarks of Apple Inc., registered in the U.S. and other countries and regions.

Microsoft, Visual Studio and Windows are trademarks of the Microsoft group of companies. 
