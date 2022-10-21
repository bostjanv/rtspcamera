/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rtspcam {

struct Image {
    Image(uint8_t* data, size_t size, uint64_t frame_index, int width, int height, int stride)
        : data_(data)
        , size_(size)
        , frame_index_(frame_index)
        , width_(width)
        , height_(height)
        , stride_(stride)
    {
    }

    void save(std::string const& filename) const;

    uint8_t* data_;
    size_t size_;
    uint64_t frame_index_;
    int width_;
    int height_;
    int stride_;
};

enum class ImageFormat {
    RGB,
    BGR,
};

} // namespace rtspcam
