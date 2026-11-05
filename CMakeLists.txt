# bfbc - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(bfbc LANGUAGES CXX)

# The Windows shell icon lives in a .rc resource; llvm-rc compiles it.
enable_language(RC)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(BFBC_SOURCES
    src/main.cpp
    src/bfbc_frame.cpp
    src/bfbc_mouse.cpp
    src/bfbc_patches.cpp
    src/bfbc.rc
)

if(WIN32)
    add_executable(bfbc WIN32 ${BFBC_SOURCES})
else()
    add_executable(bfbc ${BFBC_SOURCES})
endif()

# GPU_PLUGINS xenos stages rexgpu-xenos.dll next to the exe. The plugin is
# runtime-loaded by --gpu_plugin=xenos; without it the runtime boots with no
# graphics system ("native rendering mode") and the guest never renders.
rexglue_setup_target(bfbc GPU_PLUGINS xenos)

# Emit a linker map so host addresses in crash dumps can be mapped back to the
# recompiled guest functions (sub_XXXXXXXX). Harmless for normal runs.
if(WIN32)
    target_link_options(bfbc PRIVATE "-Xlinker" "/MAP:${CMAKE_BINARY_DIR}/bfbc.map")
endif()
