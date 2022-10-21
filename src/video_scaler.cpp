/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "video_scaler.hpp"
#include "video_frame.hpp"

#include <stdexcept>

extern "C" {
#include <libavutil/imgutils.h>
}

using namespace rtspcam;

static std::pair<AVPixelFormat, bool> maybe_change_pixel_format(AVPixelFormat pixfmt);

void VideoScaler::initialize(int src_width,
    int src_height,
    AVPixelFormat src_pixfmt,
    int dst_width,
    int dst_height,
    AVPixelFormat dst_pixfmt)
{
    bool should_change_colorspace_details;
    std::tie(src_pixfmt, should_change_colorspace_details) = maybe_change_pixel_format(src_pixfmt);

    sws_context_ = std::unique_ptr<SwsContext, SwsContextDeleter>(
        sws_getContext(src_width, src_height, src_pixfmt, dst_width, dst_height, dst_pixfmt,
            SWS_BILINEAR, nullptr, nullptr, nullptr),
        SwsContextDeleter());
    if (!sws_context_) {
        throw std::runtime_error("Failed to initialize video scaler");
    }

    if (should_change_colorspace_details) {
        // change the range of input data by first reading the current color space and then setting
        // it's range as yuvj.
        // http://ffmpeg.org/doxygen/trunk/vf__scale_8c_source.html#l00677
        int const* inv_table;
        int const* table;
        int src_range;
        int dst_range;
        int brightness;
        int contrast;
        int saturation;

        // FIXME(bostjan): check if using inv_table and table instead of coefs is correct
        if (sws_getColorspaceDetails(sws_context_.get(), (int**)&inv_table, &src_range,
                (int**)&table, &dst_range, &brightness, &contrast,
                &saturation)
            < 0) {
            throw std::runtime_error("Failed to get colorspace details");
        }

        // const int* coefs = sws_getCoefficients(SWS_CS_DEFAULT);
        src_range = 1; // this marks that values are according to yuvj

        if (sws_setColorspaceDetails(sws_context_.get(), inv_table, src_range, table, dst_range,
                brightness, contrast, saturation)
            < 0) {
            throw std::runtime_error("Failed to set colorspace details");
        }
    }

    dst_frame_ = make_videoframe();
    auto* dst_frame = dst_frame_.get();

    // allocate destination buffer
    // int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, dst_width, dst_height, 0);
    dst_frame->format = AV_PIX_FMT_RGB24;
    dst_frame->width = dst_width;
    dst_frame->height = dst_height;
    if (av_frame_get_buffer(dst_frame, 0) != 0) {
        throw std::runtime_error("Failed to allocate buffer for frame");
    }
}

Image VideoScaler::convert(AVFrame const* src_frame, uint64_t frame_index)
{
    auto* dst_frame = dst_frame_.get();

    if (sws_scale(sws_context_.get(), src_frame->data, src_frame->linesize, 0, src_frame->height,
            dst_frame->data, dst_frame->linesize)
        != dst_frame->height) {
        // char err[AV_ERROR_MAX_STRING_SIZE];
        // av_strerror(ret, err, AV_ERROR_MAX_STRING_SIZE);
        // std::cout << "sws_scale_frame error: " << err << std::endl;
        throw std::runtime_error("Failed to scale video frame");
    }

    return Image(dst_frame->data[0], (size_t)dst_frame->linesize[0] * dst_frame->height, frame_index,
        dst_frame->width, dst_frame->height, dst_frame->linesize[0]);
}

static std::pair<AVPixelFormat, bool> maybe_change_pixel_format(AVPixelFormat pixfmt)
{
    // https://stackoverflow.com/questions/23067722/swscaler-warning-deprecated-pixel-format-used/23216860
    AVPixelFormat dst_pixfmt;
    bool change_colorspace_details;

    switch (pixfmt) {
    case AV_PIX_FMT_YUVJ420P:
        dst_pixfmt = AV_PIX_FMT_YUV420P;
        change_colorspace_details = true;
        break;
    case AV_PIX_FMT_YUVJ422P:
        dst_pixfmt = AV_PIX_FMT_YUV422P;
        change_colorspace_details = true;
        break;
    case AV_PIX_FMT_YUVJ444P:
        dst_pixfmt = AV_PIX_FMT_YUV444P;
        change_colorspace_details = true;
        break;
    case AV_PIX_FMT_YUVJ440P:
        dst_pixfmt = AV_PIX_FMT_YUV440P;
        change_colorspace_details = true;
        break;
    default:
        dst_pixfmt = pixfmt;
        change_colorspace_details = false;
    }

    return { dst_pixfmt, change_colorspace_details };
}
