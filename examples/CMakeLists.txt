find_package(OpenCV COMPONENTS core highgui)

# h264decode
add_executable(h264decode
    h264decode.cpp
)
target_link_libraries(h264decode PRIVATE
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
)
target_include_directories(h264decode PRIVATE
    ${THIRD_PARTY_DIR}/include
)

# rtspdump
add_executable(rtspdump
    rtspdump.cpp
)
target_include_directories(rtspdump PRIVATE
    ${THIRD_PARTY_DIR}/include
    ${THIRD_PARTY_DIR}/include/live555
)
target_compile_definitions(rtspdump PRIVATE
    $<$<PLATFORM_ID:Windows>:NO_OPENSSL=1>
)
target_link_libraries(rtspdump PRIVATE
    ${LIVE555_LIBRARY}
    ${AVCODEC_LIBRARY}
)

# minimal
add_executable(minimal minimal.cpp)
target_link_libraries(minimal PRIVATE rtspcamera)
target_include_directories(minimal PRIVATE ../src)

# opencv_integration
if(OpenCV_FOUND)
    add_executable(opencv_integration opencv_integration.cpp)
    target_link_libraries(opencv_integration PRIVATE
        ${OpenCV_LIBS}
        rtspcamera
    )
    target_include_directories(opencv_integration PRIVATE
        ${OpenCV_INCLUDE_DIRS}
        ../src
    )
endif()

# decoder_test
add_executable(decoder_test decoder_test.cpp)
target_link_libraries(decoder_test PRIVATE rtspcamera)
target_include_directories(decoder_test PRIVATE
    ../src
    ${THIRD_PARTY_DIR}/include
)
