/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include "queue.hpp"
#include "swapper.hpp"
#include "video_frame.hpp"

namespace rtspcam {

struct Slice {
    Slice()
        : data_(nullptr)
        , size_(0)
    {
    }

    Slice(uint8_t const* data, size_t size)
        : data_(data)
        , size_(size)
    {
    }

    uint8_t const* const data_;
    size_t const size_;
};

struct AVPacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct AVCodecContextDeleter {
    void operator()(AVCodecContext* p) const { avcodec_free_context(&p); }
};
struct AVCodecParserContextDeleter {
    void operator()(AVCodecParserContext* p) const { av_parser_close(p); }
};

class Decoder {
public:
    Decoder(Swapper<VideoFramePtr>& swapper, Slice extradata = {});
    ~Decoder();
    void send(Slice slice, uint64_t pts);

private:
    std::unique_ptr<AVCodecContext, AVCodecContextDeleter> codec_context_;
    std::unique_ptr<AVCodecParserContext, AVCodecParserContextDeleter> parser_context_;
    VideoFramePtr src_frame_;
    std::unique_ptr<AVPacket, AVPacketDeleter> packet_;
    Swapper<VideoFramePtr>& swapper_;
    bool first_frame_;
    std::thread thread_;
    Queue<std::vector<uint8_t>> queue_;

    void decode();
    void decode_loop();
};

} // namespace rtspcam
