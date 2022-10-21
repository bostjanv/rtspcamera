/*
 * Copyright (c) 2022, Bostjan Vesnicer
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <array>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <H264VideoRTPSource.hh>
#include <liveMedia.hh>

#include "decoder.hpp"
#include "rtsp_camera_client.hpp"
#include "video_frame.hpp"

using namespace rtspcam;

static void on_quit_event(void* data);
static std::vector<uint8_t> decode_sprop_parameters(char const* sprop_parameters);

static void subsessionAfterPlaying(void* client_data);
static void subsessionByeHandler(void* client_data, char const* reason);

static UsageEnvironment& operator<<(UsageEnvironment& env, RTSPClient const& rtsp_client);
static UsageEnvironment& operator<<(UsageEnvironment& env, MediaSubsession const& subsession);

// VideoSink

static constexpr size_t receive_buffer_size = 2'000'000;
static constexpr bool be_verbose = false;

class VideoSink : public MediaSink {
public:
    static VideoSink* create(
        UsageEnvironment& env,
        MediaSubsession& subsession, // identifies the kind of data that's being received
        Swapper<VideoFramePtr>& swapper,
        Slice extradata,
        char const* stream_id = nullptr); // identifies the stream itself (optional)

private:
    VideoSink(UsageEnvironment& env,
        MediaSubsession& subsession,
        Swapper<VideoFramePtr>& swapper,
        Slice extradata,
        char const* stream_id);

    static void afterGettingFrame(void* clientData,
        unsigned frameSize,
        unsigned numTruncatedBytes,
        struct timeval presentationTime,
        unsigned durationInMicroseconds);

    void afterGettingFrame(unsigned frameSize,
        unsigned numTruncatedBytes,
        struct timeval presentationTime,
        unsigned durationInMicroseconds);

    virtual Boolean continuePlaying() override;

    MediaSubsession& subsession_;
    std::vector<uint8_t> receive_buffer_;
    std::string stream_id_;
    bool waiting_for_sps_unit_;
    Decoder decoder_;
};

std::unique_ptr<RtspCameraClient, RtspCameraClient::Deleter> RtspCameraClient::create(
    UsageEnvironment& environment,
    std::string const& rtsp_url,
    Swapper<VideoFramePtr>& swapper,
    ErrorSlot& error_slot)
{
    return std::unique_ptr<RtspCameraClient, RtspCameraClient::Deleter>(
        new RtspCameraClient(environment, rtsp_url, swapper, error_slot),
        RtspCameraClient::Deleter());
}

static constexpr int verbosity_level = 0;

RtspCameraClient::RtspCameraClient(UsageEnvironment& environment,
    std::string const& rtsp_url,
    Swapper<VideoFramePtr>& swapper,
    ErrorSlot& error_slot)
    : RTSPClient(environment, rtsp_url.c_str(), verbosity_level, "rtspcam", 0, -1)
    , swapper_(swapper)
    , error_slot_(error_slot)
    , quit_flag_(0)
    , already_shutteddown_(false)
    , stream_state_ {}
{
    quit_trigger_ = envir().taskScheduler().createEventTrigger(on_quit_event);
    sendDescribeCommand(continueAfterDESCRIBE);

    thread_ = std::thread([&env = envir(), quit_flag = &quit_flag_] {
        // env->taskScheduler().scheduleDelayedTask(1'000'000, task, nullptr);
        env.taskScheduler().doEventLoop(quit_flag);
    });
}

RtspCameraClient::~RtspCameraClient() { }

void RtspCameraClient::quit()
{
    envir().taskScheduler().triggerEvent(quit_trigger_, this);
    thread_.join();
}

void RtspCameraClient::on_quit()
{
    shutdownStream(this);
    quit_flag_ = 1;
}

// RtspCameraClient::StreamState::StreamState() {}
RtspCameraClient::StreamState::~StreamState()
{
    delete subsession_iterator_;
    if (session_ != NULL) {
        // We also need to delete "session", and unschedule "streamTimerTask" (if set)
        UsageEnvironment& env = session_->envir();

        env.taskScheduler().unscheduleDelayedTask(session_timeout_broken_server_task_);
        env.taskScheduler().unscheduleDelayedTask(stream_timer_task_);
        Medium::close(session_);
    }
}

static void on_quit_event(void* data)
{
    auto* self = static_cast<RtspCameraClient*>(data);
    self->on_quit();
}

static void rtspcam::continueAfterDESCRIBE(RTSPClient* rtsp_client,
    int result_code,
    char* result_string)
{
    do {
        UsageEnvironment& env = rtsp_client->envir();
        RtspCameraClient& client = *static_cast<RtspCameraClient*>(rtsp_client);
        RtspCameraClient::StreamState& state = client.stream_state_;

        if (result_code != 0) {
            env << client << "Failed to get a SDP description: " << result_string << "\n";
            std::ostringstream os;
            os << "Failed to get a SDP description: " << result_string;
            client.error_message_ = os.str();
            delete[] result_string;
            break;
        }

        char* const sdpDescription = result_string;
        env << *rtsp_client << "Got a SDP description:\n"
            << sdpDescription << "\n";

        // Create a media session object from this SDP description:
        state.session_ = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription; // because we don't need it anymore
        if (state.session_ == nullptr) {
            env << *rtsp_client
                << "Failed to create a MediaSession object from the SDP "
                   "description: "
                << env.getResultMsg() << "\n";
            break;
        }
        if (state.session_->hasSubsessions() == False) {
            env << *rtsp_client
                << "This session has no media subsessions (i.e., no \"m=\" "
                   "lines)\n";
            break;
        }

        // Then, create and set up our data source objects for the session.  We
        // do this by iterating over the session's 'subsessions', calling
        // "MediaSubsession::initiate()", and then sending a RTSP "SETUP"
        // command, on each one. (Each 'subsession' will have its own data
        // source.)
        state.subsession_iterator_ = new MediaSubsessionIterator(*state.session_);
        setupNextSubsession(rtsp_client);
        return;
    } while (false);

    // An unrecoverable error occurred with this stream.
    shutdownStream(rtsp_client);
}

// By default, we request that the server stream its data using RTP/UDP.
// If, instead, you want to request that the server stream via RTP-over-TCP,
// change the following to True:
#define REQUEST_STREAMING_OVER_TCP False

// FIXME(bostjan): Refactor this recursive mess
static void rtspcam::setupNextSubsession(RTSPClient* rtsp_client)
{
    UsageEnvironment& env = rtsp_client->envir();
    RtspCameraClient::StreamState& state = (static_cast<RtspCameraClient*>(rtsp_client))->stream_state_;

    state.subsession_ = state.subsession_iterator_->next();
    if (state.subsession_ != NULL) {
        if (strcmp(state.subsession_->codecName(), "H264") != 0) {
            setupNextSubsession(rtsp_client);
            return;
        }
        if (state.subsession_->initiate() == False) {
            env << *rtsp_client << "Failed to initiate the \"" << *state.subsession_
                << "\" subsession: " << env.getResultMsg() << "\n";
            setupNextSubsession(rtsp_client); // give up on this subsession; go to the next one
        } else {
            env << *rtsp_client << "Initiated the \"" << *state.subsession_ << "\" subsession (";
            if (state.subsession_->rtcpIsMuxed() == True) {
                env << "client port " << state.subsession_->clientPortNum();
            } else {
                env << "client ports " << state.subsession_->clientPortNum() << "-"
                    << state.subsession_->clientPortNum() + 1;
            }
            env << ")\n";

            // Continue setting up this subsession, by sending a RTSP "SETUP"
            // command:
            rtsp_client->sendSetupCommand(*state.subsession_, continueAfterSETUP, False,
                REQUEST_STREAMING_OVER_TCP);
        }
        return;
    }

    // We've finished setting up all of the subsessions.  Now, send a RTSP
    // "PLAY" command to start the streaming:
    if (state.session_->absStartTime() != NULL) {
        // Special case: The stream is indexed by 'absolute' time, so send an
        // appropriate "PLAY" command:
        rtsp_client->sendPlayCommand(*state.session_, continueAfterPLAY,
            state.session_->absStartTime(), state.session_->absEndTime());
    } else {
        state.duration_ = state.session_->playEndTime() - state.session_->playStartTime();
        rtsp_client->sendPlayCommand(*state.session_, continueAfterPLAY);
    }
}

static void rtspcam::continueAfterSETUP(RTSPClient* rtsp_client, int result_code, char* result_string)
{
    do {
        UsageEnvironment& env = rtsp_client->envir();
        RtspCameraClient& client = *static_cast<RtspCameraClient*>(rtsp_client);
        RtspCameraClient::StreamState& state = client.stream_state_;

        if (result_code != 0) {
            env << *rtsp_client << "Failed to set up the \"" << *state.subsession_
                << "\" subsession: " << result_string << "\n";
            break;
        }

        env << *rtsp_client << "Set up the \"" << *state.subsession_ << "\" subsession (";
        if (state.subsession_->rtcpIsMuxed() == True) {
            env << "client port " << state.subsession_->clientPortNum();
        } else {
            env << "client ports " << state.subsession_->clientPortNum() << "-"
                << state.subsession_->clientPortNum() + 1;
        }
        env << ")\n";

        // Having successfully setup the subsession, create a data sink for it,
        // and call "startPlaying()" on it. (This will prepare the data sink to
        // receive data; the actual flow of data from the client won't start
        // happening until later, after we've sent a RTSP "PLAY" command.)

        // FIXME(bostjan): We handle only video/h264 sessions currently.

        if constexpr (be_verbose) {
            std::cout << "&state:             " << &state << "\n"
                      << "state.subsession:   " << state.subsession_ << "\n"
                      << "codec name:         " << state.subsession_->codecName() << "\n";

            std::cout << "spropparametersets: " << state.subsession_->fmtp_spropparametersets()
                      << "\n"
                      << "sprosps:            " << state.subsession_->fmtp_spropsps() << "\n"
                      << "spropps:            " << state.subsession_->fmtp_sproppps() << "\n"
                      << "sprovps:            " << state.subsession_->fmtp_spropvps() << "\n";
        }

        if (strcmp(state.subsession_->codecName(), "H264") == 0) {
            char const* sprop_parameters = state.subsession_->fmtp_spropparametersets();
            std::vector<uint8_t> extradata;
            if (sprop_parameters) {
                extradata = decode_sprop_parameters(sprop_parameters);
            }

            state.subsession_->sink = VideoSink::create(env, *state.subsession_, client.swapper_,
                { extradata.data(), extradata.size() }, rtsp_client->url());
            if (state.subsession_->sink == nullptr) {
                env << *rtsp_client << "Failed to create a data sink for the \""
                    << *state.subsession_ << "\" subsession: " << env.getResultMsg() << "\n";
                break;
            }

            env << *rtsp_client << "Created a data sink for the \"" << *state.subsession_
                << "\" subsession\n";
            state.subsession_->miscPtr = rtsp_client; // a hack to let subsession handler functions
                                                      // get the "RTSPClient" from the subsession
            state.subsession_->sink->startPlaying(*(state.subsession_->readSource()),
                subsessionAfterPlaying, state.subsession_);

            // Also set a handler to be called if a RTCP "BYE" arrives for this
            // subsession:
            if (state.subsession_->rtcpInstance() != NULL) {
                state.subsession_->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler,
                    state.subsession_);
            }
        } else {
            state.subsession_->sink = nullptr;
        }

    } while (false);

    delete[] result_string;

    // Set up the next subsession, if any:
    setupNextSubsession(rtsp_client);
}

static void rtspcam::continueAfterPLAY(RTSPClient* rtsp_client, int result_code, char* result_string)
{
    Boolean success = False;

    do {
        UsageEnvironment& env = rtsp_client->envir();
        RtspCameraClient::StreamState& state = (static_cast<RtspCameraClient*>(rtsp_client))->stream_state_;

        if (result_code != 0) {
            env << *rtsp_client << "Failed to start playing session: " << result_string << "\n";
            break;
        }

        // Set a timer to be handled at the end of the stream's expected
        // duration (if the stream does not already signal its end using a RTCP
        // "BYE").  This is optional.  If, instead, you want to keep the stream
        // active - e.g., so you can later 'seek' back within it and do another
        // RTSP "PLAY" - then you can omit this code. (Alternatively, if you
        // don't want to receive the entire stream, you could set this timer for
        // some shorter value.)
        if (state.duration_ > 0) {
            unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's
                                          // expected duration.  (This is optional.)
            state.duration_ += delaySlop;
            unsigned uSecsToDelay = (unsigned)(state.duration_ * 1000000);
            state.stream_timer_task_ = env.taskScheduler().scheduleDelayedTask(
                uSecsToDelay, (TaskFunc*)streamTimerHandler, rtsp_client);
        }

        env << *rtsp_client << "Started playing session";
        if (state.duration_ > 0) {
            env << " (for up to " << state.duration_ << " seconds)";
        }
        env << "...\n";

        // FIXME(bostjan)
        state.session_timeout_broken_server_task_ = env.taskScheduler().scheduleDelayedTask(
            55UL * 1'000'000, (TaskFunc*)sessionTimeoutBrokenServerHandle, rtsp_client);

        success = True;
    } while (false);

    delete[] result_string;

    if (success == False) {
        // An unrecoverable error occurred with this stream.
        shutdownStream(rtsp_client);
    }
}

static void subsessionAfterPlaying(void* client_data)
{
    MediaSubsession* subsession = (MediaSubsession*)client_data;
    RTSPClient* rtsp_client = (RTSPClient*)(subsession->miscPtr);

    // Begin by closing this subsession's stream:
    Medium::close(subsession->sink);
    subsession->sink = NULL;

    // Next, check whether *all* subsessions' streams have now been closed:
    MediaSession& session = subsession->parentSession();
    MediaSubsessionIterator iter(session);
    while ((subsession = iter.next()) != NULL) {
        if (subsession->sink != NULL)
            return; // this subsession is still active
    }

    // All subsessions' streams have now been closed, so shutdown the client:
    shutdownStream(rtsp_client);
}

static void subsessionByeHandler(void* client_data, char const* reason)
{
    MediaSubsession* subsession = (MediaSubsession*)client_data;
    RTSPClient* rtsp_client = (RTSPClient*)subsession->miscPtr;
    UsageEnvironment& env = rtsp_client->envir(); // alias

    env << *rtsp_client << "Received RTCP \"BYE\"";
    if (reason != NULL) {
        env << " (reason:\"" << reason << "\")";
        delete[] (char*)reason;
    }
    env << " on \"" << *subsession << "\" subsession\n";

    // Now act as if the subsession had closed:
    subsessionAfterPlaying(subsession);
}

static void rtspcam::streamTimerHandler(void* client_data)
{
    RtspCameraClient& client = *static_cast<RtspCameraClient*>(client_data);
    RtspCameraClient::StreamState& state = client.stream_state_;

    state.stream_timer_task_ = NULL;

    shutdownStream(&client);
}

static void rtspcam::sessionTimeoutBrokenServerHandle(RTSPClient* rtsp_client)
{
    // Send an "OPTIONS" request, starting with the second call
    // rtspClient->sendOptionsCommand(nullptr);
    RtspCameraClient& client = *static_cast<RtspCameraClient*>(rtsp_client);
    RtspCameraClient::StreamState& state = client.stream_state_;
    rtsp_client->sendGetParameterCommand(*state.session_, nullptr, nullptr);

    unsigned sessionTimeout = rtsp_client->sessionTimeoutParameter();
    sessionTimeout = sessionTimeout == 0 ? 60 : sessionTimeout;
    int64_t secondsUntilNextKeepAlive = sessionTimeout <= 5 ? 1 : sessionTimeout - 5;

    rtsp_client->envir().taskScheduler().rescheduleDelayedTask(
        state.session_timeout_broken_server_task_, secondsUntilNextKeepAlive * 1'000'000,
        (TaskFunc*)sessionTimeoutBrokenServerHandle, rtsp_client);
}

static void rtspcam::shutdownStream(RTSPClient* rtsp_client)
{
    UsageEnvironment& env = rtsp_client->envir();
    auto& client = *static_cast<RtspCameraClient*>(rtsp_client);
    RtspCameraClient::StreamState& state = client.stream_state_;

    if (client.already_shutteddown_) {
        return;
    }

    // First, check whether any subsessions have still to be closed:
    if (state.session_ != NULL) {
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator iter(*state.session_);
        MediaSubsession* subsession;

        while ((subsession = iter.next()) != NULL) {
            if (subsession->sink != NULL) {
                Medium::close(subsession->sink);
                subsession->sink = NULL;

                if (subsession->rtcpInstance() != NULL) {
                    subsession->rtcpInstance()->setByeHandler(
                        NULL, NULL); // in case the server sends a RTCP "BYE"
                                     // while handling "TEARDOWN"
                }

                someSubsessionsWereActive = True;
            }
        }

        if (someSubsessionsWereActive == True) {
            // Send a RTSP "TEARDOWN" command, to tell the server to shutdown
            // the stream. Don't bother handling the response to the "TEARDOWN".
            rtsp_client->sendTeardownCommand(*state.session_, NULL);
        }
    }

    env << *rtsp_client << "Closing the stream.\n";
    client.already_shutteddown_ = true;
    client.error_slot_.set(client.error_message_);
}

static UsageEnvironment& operator<<(UsageEnvironment& env, RTSPClient const& rtsp_client)
{
    return env << "[URL:\"" << rtsp_client.url() << "\"]: ";
}

static UsageEnvironment& operator<<(UsageEnvironment& env, MediaSubsession const& subsession)
{
    return env << subsession.mediumName() << "/" << subsession.codecName();
}

VideoSink* VideoSink::create(UsageEnvironment& env,
    MediaSubsession& subsession,
    Swapper<VideoFramePtr>& swapper,
    Slice extradata,
    char const* stream_id)
{
    return new VideoSink(env, subsession, swapper, extradata, stream_id);
}

VideoSink::VideoSink(UsageEnvironment& env,
    MediaSubsession& subsession,
    Swapper<VideoFramePtr>& swapper,
    Slice extradata,
    char const* stream_id)
    : MediaSink(env)
    , subsession_(subsession)
    , receive_buffer_(receive_buffer_size + 4)
    , stream_id_(stream_id)
    , waiting_for_sps_unit_(true)
    , decoder_(swapper, extradata)
{
    static constexpr std::array<uint8_t, 4> start_marker { 0x00, 0x00, 0x00, 0x01 };
    std::copy(start_marker.begin(), start_marker.end(), receive_buffer_.begin());
    if (extradata.size_ > 0) {
        // FIXME(bostjan): Not working as intended
        // waiting_for_sps_unit_ = false;
    }
}

void VideoSink::afterGettingFrame(void* clientData,
    unsigned frameSize,
    unsigned numTruncatedBytes,
    struct timeval presentationTime,
    unsigned durationInMicroseconds)
{
    VideoSink& sink = *static_cast<VideoSink*>(clientData);
    sink.afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// #define DEBUG_PRINT_EACH_RECEIVED_FRAME 1

void VideoSink::afterGettingFrame(unsigned frameSize,
    unsigned numTruncatedBytes,
    struct timeval presentationTime,
    unsigned /*duration_in_microseconds*/)
{
    // We've just received a frame of data.  (Optionally) print out information about it:
#ifdef DEBUG_PRINT_EACH_RECEIVED_FRAME
    if (fStreamId != NULL)
        envir() << "Stream \"" << fStreamId << "\"; ";
    envir() << fSubsession.mediumName() << "/" << fSubsession.codecName() << ":\tReceived "
            << frameSize << " bytes";
    if (numTruncatedBytes > 0)
        envir() << " (with " << numTruncatedBytes << " bytes truncated)";
    char uSecsStr[6 + 1]; // used to output the 'microseconds' part of the
                          // presentation time
    sprintf(uSecsStr, "%06u", (unsigned)presentationTime.tv_usec);
    envir() << ".\tPresentation time: " << (int)presentationTime.tv_sec << "." << uSecsStr;
    if (fSubsession.rtpSource() != NULL && !fSubsession.rtpSource()->hasBeenSynchronizedUsingRTCP()) {
        envir() << "!"; // mark the debugging output to indicate that this
                        // presentation time is not RTCP-synchronized
    }
#    ifdef DEBUG_PRINT_NPT
    envir() << "\tNPT: " << fSubsession.getNormalPlayTime(presentationTime);
#    endif
    envir() << "\n";
#endif

#if 0
    envir() << "codec_name:        " << subsession_.codecName() << "\n"
            << "medium name:       " << subsession_.mediumName() << "\n"
            << "frame size:        " << frame_size << "\n"
            << "stream id:         " << stream_id_.c_str() << "\n"
            << "stream id:         " << stream_id_.c_str() << "\n"
            << "presentation time: " << subsession_.getNormalPlayTime(presentationTime) << "\n"
            << "width:             " << subsession_.videoWidth() << "\n"
            << "height:            " << subsession_.videoHeight() << "\n"
            << "FPS:               " << subsession_.videoFPS() << "\n"
            << "session_id:        " << subsession_.sessionId() << "\n"
            << "\n";
#endif

    if (numTruncatedBytes != 0) {
        envir() << "num. truncated bytes: " << numTruncatedBytes << "\n";
    }

    if constexpr (be_verbose) {
        std::cout << std::setfill('0');
        if (receive_buffer_[4] == 0x67 || receive_buffer_[4] == 0x68) {
            for (size_t i = 0; i < frameSize; i++) {
                std::cout << " " << std::hex << std::setw(2) << (int)receive_buffer_[i + 4];
            }
            std::cout << std::endl;
        }
    }

    if (waiting_for_sps_unit_ && receive_buffer_[4] == 0x67) {
        // got sps unit
        waiting_for_sps_unit_ = false;
    }

    if (!waiting_for_sps_unit_) {
        decoder_.send({ receive_buffer_.data(), frameSize + 4 }, presentationTime.tv_usec);
    }

    // Then continue, to request the next frame of data:
    continuePlaying();
}

Boolean VideoSink::continuePlaying()
{
    if (fSource == NULL)
        return False; // sanity check (should not happen)

    // Request the next frame of data from our input source.
    // "afterGettingFrame()" will get called later, when it arrives:
    fSource->getNextFrame(&receive_buffer_[4], receive_buffer_size, afterGettingFrame, this,
        onSourceClosure, this);
    return True;
}

static std::vector<uint8_t> decode_sprop_parameters(char const* sprop_parameters)
{
    // feed decoder the sprop parameters
    unsigned int num_sprop_records = 0;
    auto const* sprop_records = parseSPropParameterSets(sprop_parameters, num_sprop_records);

    size_t size = 0;
    for (int i = 0; i < num_sprop_records; i++) {
        size += 4 + sprop_records[i].sPropLength;
    }

    std::array<uint8_t, 4> delimiter { 0x00, 0x00, 0x00, 0x01 };
    std::vector<uint8_t> buffer(size);

    int64_t pos = 0;
    for (size_t i = 0; i < num_sprop_records; i++) {
        std::copy(delimiter.begin(), delimiter.end(), buffer.begin() + pos);
        std::copy(sprop_records[i].sPropBytes,
            sprop_records[i].sPropBytes + sprop_records[i].sPropLength,
            buffer.begin() + pos + 4);
        pos += 4 + sprop_records[i].sPropLength;
    }

    delete[] sprop_records;

    if (buffer.size() == 4) {
        buffer.clear();
    }

    if constexpr (be_verbose) {
        std::cout << "extradata:" << std::setfill('0');
        for (auto b : buffer) {
            std::cout << " " << std::hex << std::setw(2) << (int)b;
        }
        std::cout << std::endl;
    }

    return buffer;
}
