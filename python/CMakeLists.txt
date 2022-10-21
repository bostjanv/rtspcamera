find_package(Python COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pycam pybind11_rtspcamera.cpp)
target_include_directories(pycam PRIVATE ../src)
target_link_libraries(pycam PRIVATE rtspcamera)

set_target_properties(pycam PROPERTIES CXX_VISIBILITY_PRESET "hidden")

install(TARGETS pycam
    CONFIGURATIONS Release
    LIBRARY DESTINATION bin
)
