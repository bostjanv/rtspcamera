/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <liveMedia.hh>

#include "decoder.hpp"
#include "error_slot.hpp"
#include "rtsp_camera.hpp"
#include "swapper.hpp"
#include "video_frame.hpp"

namespace rtspcam {

static void shutdownStream(RTSPClient* rtsp_client);
static void continueAfterDESCRIBE(RTSPClient* rtsp_client, int result_code, char* result_string);
static void setupNextSubsession(RTSPClient* rtsp_client);
static void continueAfterSETUP(RTSPClient* rtsp_client, int result_code, char* result_string);
static void continueAfterPLAY(RTSPClient* rtsp_client, int result_code, char* result_string);
static void streamTimerHandler(void* client_data);
static void sessionTimeoutBrokenServerHandle(RTSPClient* rtsp_client);

class RtspCameraClient : public RTSPClient {
public:
    virtual ~RtspCameraClient() override;

    void quit();
    void on_quit();

    struct Deleter {
        void operator()(RtspCameraClient* p) { Medium::close(p); }
    };

    static std::unique_ptr<RtspCameraClient, RtspCameraClient::Deleter> create(
        UsageEnvironment& environment,
        std::string const& rtsp_url,
        Swapper<VideoFramePtr>& swapper,
        ErrorSlot& error_slot);

    struct StreamState {
        ~StreamState();

        MediaSubsessionIterator* subsession_iterator_;
        MediaSession* session_;
        MediaSubsession* subsession_;
        TaskToken stream_timer_task_;
        TaskToken session_timeout_broken_server_task_;
        double duration_;
    };

private:
    RtspCameraClient(UsageEnvironment& environment,
        std::string const& rtsp_url,
        Swapper<VideoFramePtr>& swapper,
        ErrorSlot& error_slot_);

    std::thread thread_;
    Swapper<VideoFramePtr>& swapper_;
    ErrorSlot& error_slot_;
    std::string error_message_;
    EventTriggerId quit_trigger_;
    char quit_flag_;
    bool already_shutteddown_;
    StreamState stream_state_;

    friend void shutdownStream(RTSPClient*);
    friend void continueAfterDESCRIBE(RTSPClient*, int, char*);
    friend void setupNextSubsession(RTSPClient*);
    friend void continueAfterSETUP(RTSPClient*, int, char*);
    friend void continueAfterPLAY(RTSPClient*, int, char*);
    friend void streamTimerHandler(void*);
    friend void sessionTimeoutBrokenServerHandle(RTSPClient*);
};

} // namespace rtspcam
