/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "rtsp_camera.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <url>" << std::endl;
        return 0;
    }

    std::cout << "Connecting to " << argv[1] << "..." << std::endl;

    auto camera = rtspcam::RtspCamera::open(argv[1]);

    for (;;) {
        try {
            auto image = camera->read();
            image.save("/tmp/image.ppm");
        } catch (std::runtime_error const& e) {
            std::cout << e.what() << std::endl;
            break;
        }
    }
}
