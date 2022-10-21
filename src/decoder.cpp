/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decoder.hpp"
#include "video_frame.hpp"

#include <cassert>
#include <fstream>
#include <iostream>
#include <thread>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

using namespace rtspcam;

static constexpr bool be_verbose = false;

Decoder::Decoder(Swapper<VideoFramePtr>& swapper, Slice extradata)
    : src_frame_(make_videoframe())
    , packet_(av_packet_alloc(), AVPacketDeleter())
    , swapper_(swapper)
    , first_frame_(true)
{
    AVCodec const* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        throw std::runtime_error("codec h264 not found");
    }

    parser_context_ = std::unique_ptr<AVCodecParserContext, AVCodecParserContextDeleter>(
        av_parser_init(codec->id), AVCodecParserContextDeleter());
    if (!parser_context_) {
        throw std::runtime_error("failed to initialize parser");
    }

    codec_context_ = std::unique_ptr<AVCodecContext, AVCodecContextDeleter>(
        avcodec_alloc_context3(codec), AVCodecContextDeleter());
    if (!codec_context_) {
        throw std::runtime_error("failed to allocate codec context");
    }

    if (extradata.size_ != 0) {
        if constexpr (be_verbose) {
            std::cout << "setting decoder extradata" << std::endl;
        }
        codec_context_->extradata = (uint8_t*)av_mallocz(extradata.size_ + AV_INPUT_BUFFER_PADDING_SIZE);
        assert(codec_context_->extradata);
        std::copy(extradata.data_, extradata.data_ + extradata.size_, codec_context_->extradata);
        codec_context_->extradata_size = (int)extradata.size_;
    }

    // AVDictionary* options = nullptr;
    // av_dict_set(&options, "threads", "auto", 0);

    if (avcodec_open2(codec_context_.get(), codec, /*&options*/ nullptr) != 0) {
        throw std::runtime_error("failed to open codec");
    }

    thread_ = std::thread([this]() { decode_loop(); });
}

Decoder::~Decoder()
{
    queue_.push({});
    thread_.join();
}

void Decoder::send(Slice slice, uint64_t /*pts*/)
{
    // FIXME(bostjan): Avoid allocation by using memory pool
    queue_.push({ slice.data_, slice.data_ + slice.size_ });
}

void Decoder::decode()
{
    int ret;

    auto* codec_context = codec_context_.get();
    auto* packet = packet_.get();

    ret = avcodec_send_packet(codec_context, packet);
    if (ret != 0) {
        throw std::runtime_error("Error sending a packet for decoding");
    }

    while (ret >= 0) {
        auto* src_frame = src_frame_.get();

        ret = avcodec_receive_frame(codec_context, src_frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        }
        if (ret < 0) {
            throw std::runtime_error("Error during decoding");
        }

        assert(ret == 0);

        if (first_frame_) {
            first_frame_ = false;
            std::cout << "codec full name: " << codec_context->codec->long_name << "\n"
                      << "width:           " << codec_context->width << "\n"
                      << "height:          " << codec_context->height << "\n"
                      << "bit rate:        " << codec_context->bit_rate << "\n"
                      << "color range:     " << codec_context->color_range << "\n"
                      << "profile:         "
                      << avcodec_profile_name(codec_context->codec_id, codec_context->profile)
                      << "\n"
                      << "pix_fmt:         " << av_get_pix_fmt_name(codec_context->pix_fmt) << "\n"
                      << "frame number:    " << codec_context_->frame_number << "\n";

            int width = codec_context->width;
            int height = codec_context->height;

            assert(src_frame->width == width);
            assert(src_frame->height == height);
            assert(src_frame->format == codec_context_->pix_fmt);
        }

        src_frame_ = swapper_.push(std::move(src_frame_));
    }
}

void Decoder::decode_loop()
{
    for (;;) {
        // FIXME(bostjan): Prevent growing the queue too much
        auto slice = queue_.pop();
        if (slice.empty()) {
            break;
        }

        auto queue_size = queue_.size();
        // FIXME: Handle runaway queue
        if constexpr (be_verbose) {
            if (queue_size > 3) {
                std::cout << "queue size: " << queue_size << std::endl;
            }
        }

        auto* cur_ptr = slice.data();
        auto cur_size = slice.size();

        auto* parser_context = parser_context_.get();
        auto* codec_context = codec_context_.get();
        auto* packet = packet_.get();
        auto* frame = src_frame_.get();

        while (cur_size > 0) {
            int len = av_parser_parse2(parser_context, codec_context, &packet->data, &packet->size,
                cur_ptr, (int)cur_size, AV_NOPTS_VALUE /*pts*/, AV_NOPTS_VALUE,
                /*AV_NOPTS_VALUE*/ -1);

            cur_ptr += len;
            cur_size -= len;

            if (packet->size == 0) {
                continue;
            }

            if constexpr (be_verbose) {
                std::cout << "[packet] size:" << packet->size << "\t";
                switch (parser_context->pict_type) {
                case AV_PICTURE_TYPE_I:
                    std::cout << "type:I\t";
                    break;
                case AV_PICTURE_TYPE_P:
                    std::cout << "type:P\t";
                    break;
                case AV_PICTURE_TYPE_B:
                    std::cout << "type:B\t";
                    break;
                default:
                    std::cout << "type:Other\t";
                    break;
                }
                std::cout << "number:" << parser_context->output_picture_number << "\n";
            }

            decode();
        }
    }

    // FIXME(bostjan): Flush decoder
}
