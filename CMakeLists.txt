cmake_minimum_required(VERSION 3.21)
project(rtspcamera)

enable_language(CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

find_library(LIVE555_LIBRARY
    NAMES liblive555.so live555.lib
    PATHS ${THIRD_PARTY_DIR}/lib
    REQUIRED
)

find_library(AVCODEC_LIBRARY
    NAMES avcodec
    REQUIRED
)

find_library(AVUTIL_LIBRARY
    NAMES avutil
)

find_library(SWSCALE_LIBRARY
    NAMES swscale
)

add_subdirectory(src)
add_subdirectory(python)
add_subdirectory(examples)
