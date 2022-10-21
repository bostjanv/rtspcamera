/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <array>
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

static constexpr bool be_verbose = false;

static void decode(AVCodecContext* dec_ctx, AVFrame* frame, AVPacket* pkt)
{
    int ret;
    static bool first_frame = true;

    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        throw std::runtime_error("Error sending a packet for decoding");
    }

    std::cout.width(4);
    std::cout.fill('0');

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            return;
        }
        if (ret < 0) {
            throw std::runtime_error("Error during decoding");
        }

        if (first_frame) {
            first_frame = false;
            std::cout << "codec full name: " << dec_ctx->codec->long_name << "\n"
                      << "width:           " << dec_ctx->width << "\n"
                      << "height:          " << dec_ctx->height << "\n"
                      << "bit rate:        " << dec_ctx->bit_rate << "\n"
                      << "color range:     " << dec_ctx->color_range << "\n"
                      << "profile:         " << avcodec_profile_name(dec_ctx->codec_id, dec_ctx->profile) << "\n"
                      << "pix_fmt:         " << av_get_pix_fmt_name(dec_ctx->pix_fmt) << "\n";
        }

        std::cout << "frame " << dec_ctx->frame_number << "\n";
    }
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <h264 file>" << std::endl;
        return 0;
    }

    AVCodec const* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        std::cerr << "codec h264 not found" << std::endl;
        return 1;
    }

    AVCodecParserContext* parser_context = av_parser_init(codec->id);
    if (!parser_context) {
        std::cerr << "failed to initialize parser" << std::endl;
        return 1;
    }

    AVCodecContext* codec_context = avcodec_alloc_context3(codec);
    if (!codec_context) {
        std::cerr << "failed to allocate codec context" << std::endl;
        return 1;
    }

    if (avcodec_open2(codec_context, codec, nullptr) != 0) {
        std::cerr << "failed to open codec" << std::endl;
        return 1;
    }

    constexpr size_t buffer_size = 4096;
    std::array<uint8_t, buffer_size + AV_INPUT_BUFFER_PADDING_SIZE> buffer;

    std::ifstream is(argv[1], std::ios::binary);
    if (!is) {
        std::ostringstream os;
        os << "failed to open h264 file `" << argv[1] << "`";
        std::cerr << os.str() << std::endl;
        return 1;
    }

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool is_first_frame = true;
    bool eof = false;

    do {
        is.read(reinterpret_cast<char*>(buffer.data()), buffer_size);
        auto bread = is.gcount();
        eof = bread == 0;

        auto* cur_ptr = buffer.data();
        auto cur_size = bread;

        while (cur_size > 0 || eof) {
            int len = av_parser_parse2(parser_context, codec_context, &packet->data, &packet->size,
                cur_ptr, (int)cur_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);

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

            // assert(packet->size >= 4);
            // std::cout << packet->size << "\t0x" << std::setfill('0') << std::setw(2)
            //           << std::hex << (int)packet->data[4] << std::dec << "\n";

            decode(codec_context, frame, packet);

            if (eof) {
                break;
            }
        }

        assert(cur_size == 0);
    } while (!eof);

    // flush the decoder
    // packet->size = 0;
    // packet->data = nullptr;

    // flush the decoder
    decode(codec_context, frame, nullptr);

    av_frame_free(&frame);
    av_packet_free(&packet);
    av_parser_close(parser_context);
    avcodec_free_context(&codec_context);
}
