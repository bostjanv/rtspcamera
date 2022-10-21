/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <memory>
#include <string>

#include "image.hpp"

namespace rtspcam {

class RtspCamera {
public:
    static std::unique_ptr<RtspCamera> open(std::string const& url);
    virtual ~RtspCamera() = default;
    virtual Image read() = 0;
    virtual void set_image_format(ImageFormat format) = 0;
    virtual void set_size(int width, int height) = 0;
};

} // namespace rtspcam
