/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "rtsp_camera.hpp"

namespace py = pybind11;

class PyCam {
public:
    PyCam(std::string const& url);
    py::array_t<uint8_t> read();

private:
    std::unique_ptr<rtspcam::RtspCamera> handle_;
};

PyCam::PyCam(std::string const& url)
    : handle_(rtspcam::RtspCamera::open(url))
{
    handle_->set_image_format(rtspcam::ImageFormat::BGR);
}

py::array_t<uint8_t> PyCam::read()
{
    auto image_header = handle_->read();
    int width = image_header.width_;
    int height = image_header.height_;

    // FIXME(bostjan): Check stride
    assert(image_header.size_ == width * height * 3);
    assert(image_header.stride_ == width * 3);

    // allocate and copy image data
    // py::array_t<uint8_t> image({height, width, 3});
    // std::copy(image_header.data_, image_header.data_ + image_header.size_,
    // image.mutable_data(0));

    // avoid allocation and copying
    auto buffer_info = py::buffer_info(image_header.data_, { height, width, 3 },
        { width * 3, 3, 1 } /* readonly = false */);
    py::array_t<uint8_t> image(buffer_info);

    return image;
}

static PyCam pycam_open(std::string const& url)
{
    return PyCam(url);
}

PYBIND11_MODULE(pycam, m)
{
    py::class_<PyCam>(m, "PyCam")
        //.def(py::init<const std::string &>())
        //.def("read", &PyCam::read, "read", py::return_value_policy::reference_internal);
        .def("read", &PyCam::read, "Read image from camera");

    m.def("open", &pycam_open, "Open camera stream");
}
