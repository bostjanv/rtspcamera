/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include <BasicUsageEnvironment.hh>

#include "error_slot.hpp"
#include "rtsp_camera.hpp"
#include "rtsp_camera_client.hpp"
#include "video_frame.hpp"
#include "video_scaler.hpp"

using namespace rtspcam;

struct UsageEnvironmentDeleter {
    void operator()(UsageEnvironment* p)
    {
        auto ret = p->reclaim();
        // FIXME(bostjan)
        if (ret == False) {
            std::cout << "not reclaimed" << std::endl;
        }
    }
};

class RtspCameraImpl : public RtspCamera {
public:
    RtspCameraImpl(std::string const& url);
    virtual ~RtspCameraImpl() override;
    Image read() override;
    void set_image_format(ImageFormat format) override;
    void set_size(int width, int height) override;

private:
    Swapper<VideoFramePtr> swapper_;
    ErrorSlot error_slot_;
    VideoFramePtr video_frame_;
    VideoScaler video_scaler_;
    AVPixelFormat pixel_format_;
    int width_;
    int height_;
    bool first_frame_;

    std::unique_ptr<TaskScheduler> scheduler_;
    std::unique_ptr<UsageEnvironment, UsageEnvironmentDeleter> environment_;
    std::unique_ptr<RtspCameraClient, RtspCameraClient::Deleter> client_;
};

RtspCameraImpl::RtspCameraImpl(std::string const& url)
    : swapper_(make_videoframe())
    , video_frame_(make_videoframe())
    , pixel_format_(AV_PIX_FMT_RGB24)
    , width_(0)
    , height_(0)
    , first_frame_(true)
    , scheduler_(BasicTaskScheduler::createNew())
    , environment_(BasicUsageEnvironment::createNew(*scheduler_), UsageEnvironmentDeleter())
    , client_(RtspCameraClient::create(*environment_, url, swapper_, error_slot_))
{
}

RtspCameraImpl::~RtspCameraImpl()
{
    client_->quit();
}

std::unique_ptr<RtspCamera> RtspCamera::open(std::string const& url)
{
    return std::make_unique<RtspCameraImpl>(url);
}

void RtspCameraImpl::set_image_format(ImageFormat format)
{
    if (first_frame_) {
        switch (format) {
        case ImageFormat::RGB:
            pixel_format_ = AV_PIX_FMT_RGB24;
            break;
        case ImageFormat::BGR:
            pixel_format_ = AV_PIX_FMT_BGR24;
            break;
        }
    }
}

void RtspCameraImpl::set_size(int width, int height)
{
    if (first_frame_) {
        width_ = width;
        height_ = height;
    }
}

Image RtspCameraImpl::read()
{
    for (;;) {
        auto maybe_image = swapper_.try_pop(std::move(video_frame_), std::chrono::milliseconds(100));
        if (!maybe_image) {
            auto maybe_error = error_slot_.check();
            if (maybe_error) {
                throw std::runtime_error(maybe_error.value());
            }
            continue;
        }

        video_frame_ = std::move(maybe_image.value().first);
        auto const* src_frame = video_frame_.get();

        if (first_frame_) {
            auto width = src_frame->width;
            auto height = src_frame->height;

            if (width_ == 0 || height_ == 0) {
                width_ = width;
                height_ = height;
            }

            video_scaler_.initialize(width, height, (AVPixelFormat)src_frame->format, width_, height_,
                pixel_format_);
        }

        uint64_t frame_index = maybe_image.value().second;
        return video_scaler_.convert(src_frame, frame_index);
    }
}
