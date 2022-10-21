add_library(rtspcamera STATIC
    rtsp_camera.hpp
    rtsp_camera_impl.cpp
    rtsp_camera_client.cpp
    rtsp_camera_client.hpp
    decoder.cpp
    decoder.hpp
    video_scaler.cpp
    video_scaler.hpp
    image.cpp
    image.hpp
    swapper.hpp
    error_slot.hpp
    video_frame.hpp
)

target_include_directories(rtspcamera PRIVATE
    ${THIRD_PARTY_DIR}/include
    ${THIRD_PARTY_DIR}/include/live555
)

target_compile_definitions(rtspcamera PRIVATE
    $<$<PLATFORM_ID:Windows>:NO_OPENSSL=1;NOMINMAX>
)

target_link_libraries(rtspcamera PUBLIC
    ${LIVE555_LIBRARY}
    ${AVCODEC_LIBRARY}
    ${AVUTIL_LIBRARY}
    ${SWSCALE_LIBRARY}
)

set_target_properties(rtspcamera PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
)
