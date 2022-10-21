/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace rtspcam {

struct AVFrameDeleter {
    void operator()(AVFrame* p) const { av_frame_free(&p); }
};

using VideoFramePtr = std::unique_ptr<AVFrame, AVFrameDeleter>;

inline VideoFramePtr make_videoframe()
{
    return { av_frame_alloc(), AVFrameDeleter() };
}

} // namespace rtspcam
