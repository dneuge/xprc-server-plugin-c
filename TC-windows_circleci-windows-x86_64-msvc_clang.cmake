# Host system: Windows (Server 2022, CircleCI)
# Target OS:   Windows
# Target CPU:  x86, 64 Bit
# Compiler:    Visual Studio 17 2022 Community Edition with Clang 18

set(CMAKE_GENERATOR_TOOLSET ClangCL)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_C_COMPILER   clang.exe)
set(CMAKE_CXX_COMPILER clang++.exe)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSTEM_VERSION 10.0.26624)
set(CMAKE_C_STANDARD 11)
