/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <memory>

extern "C" {
#include <libswscale/swscale.h>
}

#include "image.hpp"
#include "video_frame.hpp"

namespace rtspcam {

struct SwsContextDeleter {
    void operator()(SwsContext* p) const { sws_freeContext(p); }
};

class VideoScaler {
public:
    void initialize(int src_width, int src_height, AVPixelFormat src_pixfmt,
        int dst_width, int dst_height, AVPixelFormat dst_pixfmt);
    Image convert(AVFrame const* src_frame, uint64_t frame_index);

private:
    std::unique_ptr<SwsContext, SwsContextDeleter> sws_context_;
    VideoFramePtr dst_frame_;
};

} // namespace rtspcam
