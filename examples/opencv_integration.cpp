/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <chrono>
#include <iostream>
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "image.hpp"
#include "rtsp_camera.hpp"

static cv::Mat convert(rtspcam::Image&& image)
{
    return cv::Mat(image.height_, image.width_, CV_8UC3, image.data_);
}

int main(int argc, char* argv[])
{
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <url>" << std::endl;
        return 0;
    }

    std::cout << "Connecting to " << argv[1] << "..." << std::endl;

    auto camera = rtspcam::RtspCamera::open(argv[1]);
    camera->set_image_format(rtspcam::ImageFormat::BGR);
    camera->set_size(1920 / 2, 1080 / 2);

    bool pause = false;

    try {
        bool quit = false;
        for (;;) {
            auto image = convert(camera->read());
            cv::imshow("rtspcamera", image);
            int key;
            if (pause) {
                key = cv::waitKey();
            } else {
                key = cv::pollKey();
            }
            switch (key) {
            case 'q':
                quit = true;
                break;
            case ' ':
                pause = !pause;
                break;
            }
            if (quit) {
                break;
            }
        }
    } catch (std::runtime_error const& e) {
        std::cout << e.what() << std::endl;
    }
}
