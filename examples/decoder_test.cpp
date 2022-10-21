/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "decoder.hpp"
#include "video_frame.hpp"

#include <array>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace rtspcam;

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <h264 file>" << std::endl;
        return 0;
    }

    Swapper<VideoFramePtr> swapper(make_videoframe());
    Decoder decoder(swapper);

    constexpr size_t buffer_size = 4096;
    std::array<uint8_t, buffer_size + AV_INPUT_BUFFER_PADDING_SIZE> buffer;

    std::ifstream is(argv[1], std::ios::binary);
    if (!is) {
        std::ostringstream os;
        std::cerr << "failed to open h264 file `" << argv[1] << "`" << std::endl;
        return 1;
    }

    bool eof = false;

    do {
        is.read(reinterpret_cast<char*>(buffer.data()), buffer_size);
        auto bread = is.gcount();
        eof = bread == 0;

        decoder.send({buffer.data(), (size_t)bread}, 0);
    } while (!eof); 
}
