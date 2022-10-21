/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "image.hpp"

#include <fstream>

using namespace rtspcam;

void Image::save(std::string const& filename) const
{
    std::ofstream os(filename.c_str(), std::ios::binary);
    if (!os) {
        throw std::runtime_error("Failed to save image");
    }

    // FIXME(bostjan)
    int wrap = width_ * 3;

    os << "P6\n"
       << width_ << " " << height_ << "\n"
       << 255 << "\n";

    for (size_t i = 0; i < height_; i++) {
        os.write(reinterpret_cast<char const*>(data_ + i * wrap), (int64_t)width_ * 3);
    }
}
