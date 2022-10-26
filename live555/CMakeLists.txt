cmake_minimum_required(VERSION 3.21)
project(live555)

enable_language(CXX)

include(FetchContent)
FetchContent_Declare(
  live555
  URL http://live555.com/liveMedia/public/live.2022.10.01.tar.gz
  URL_HASH SHA256=429de73061e3fc6901c4d2f0b7562ae3f6233060ca4b5e182fe555d065cbdd45
)
FetchContent_MakeAvailable(live555)

file(COPY_FILE CMakeLists-live555.txt ${live555_SOURCE_DIR}/CMakeLists.txt)

add_subdirectory(${live555_SOURCE_DIR} ${live555_BINARY_DIR})
