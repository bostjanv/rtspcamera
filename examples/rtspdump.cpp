#include <fstream>
#include <iomanip>
#include <iostream>

#include <BasicUsageEnvironment.hh>
#include <liveMedia.hh>

static void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString);
static void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString);
static void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString);
static void subsessionAfterPlaying(void* clientData);
static void subsessionByeHandler(void* clientData, char const* reason);
static void streamTimerHandler(void* clientData);
static void openURL(UsageEnvironment& env,
    char const* progName,
    char const* rtspURL,
    std::string const& output);
static void setupNextSubsession(RTSPClient* rtspClient);
static void shutdownStream(RTSPClient* rtspClient);
static void sessionTimeoutBrokenServerHandle(RTSPClient* rtspClient);

static UsageEnvironment& operator<<(UsageEnvironment& env, RTSPClient const& rtspClient)
{
    return env << "[URL:\"" << rtspClient.url() << "\"]: ";
}

static UsageEnvironment& operator<<(UsageEnvironment& env, MediaSubsession const& subsession)
{
    return env << subsession.mediumName() << "/" << subsession.codecName();
}

static void usage(UsageEnvironment& env, char const* progName)
{
    env << "Usage: " << progName << " <rtsp-url-1> ... <rtsp-url-N>\n";
    env << "\t(where each <rtsp-url-i> is a \"rtsp://\" URL)\n";
}

char eventLoopWatchVariable = 0;

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <url> <output>" << std::endl;
        return 0;
    }

    auto const* url = argv[1];
    auto const* output = argv[2];

    TaskScheduler* scheduler = BasicTaskScheduler::createNew();
    UsageEnvironment* env = BasicUsageEnvironment::createNew(*scheduler);

    openURL(*env, "rtspcam", url, output);

    env->taskScheduler().doEventLoop(&eventLoopWatchVariable);

    env->reclaim();
    delete scheduler;
}

class StreamClientState {
public:
    StreamClientState(std::string const& output);
    ~StreamClientState();

    std::string output_;
    MediaSubsessionIterator* iter;
    MediaSession* session;
    MediaSubsession* subsession;
    TaskToken streamTimerTask;
    TaskToken sessionTimeoutBrokenServerTask;
    double duration;
};

class ourRTSPClient : public RTSPClient {
public:
    static ourRTSPClient* createNew(UsageEnvironment& env,
        char const* rtspURL,
        std::string const& output,
        int verbosityLevel = 0,
        char const* applicationName = NULL,
        portNumBits tunnelOverHTTPPortNum = 0);

protected:
    ourRTSPClient(UsageEnvironment& env,
        char const* rtspURL,
        std::string const& output,
        int verbosityLevel,
        char const* applicationName,
        portNumBits tunnelOverHTTPPortNum);
    virtual ~ourRTSPClient();

public:
    StreamClientState scs;
};

class DummySink : public MediaSink {
public:
    static DummySink* createNew(
        UsageEnvironment& env,
        MediaSubsession& subsession, // identifies the kind of data that's being received
        std::string const& output,
        char const* streamId = NULL); // identifies the stream itself (optional)

private:
    DummySink(UsageEnvironment& env, MediaSubsession& subsession, std::string const& output, char const* streamId);
    // called only by "createNew()"
    virtual ~DummySink();

    static void afterGettingFrame(void* clientData,
        unsigned frameSize,
        unsigned numTruncatedBytes,
        struct timeval presentationTime,
        unsigned durationInMicroseconds);
    void afterGettingFrame(unsigned frameSize,
        unsigned numTruncatedBytes,
        struct timeval presentationTime,
        unsigned durationInMicroseconds);

    virtual Boolean continuePlaying();

    u_int8_t* fReceiveBuffer;
    MediaSubsession& fSubsession;
    char* fStreamId;
    std::ofstream fOutStream;
    bool fWaitingForSPSUnit;
};

#define RTSP_CLIENT_VERBOSITY_LEVEL 0

void openURL(UsageEnvironment& env,
    char const* progName,
    char const* rtspURL,
    std::string const& output)
{
    RTSPClient* rtspClient = ourRTSPClient::createNew(env, rtspURL, output, RTSP_CLIENT_VERBOSITY_LEVEL, progName);
    if (rtspClient == NULL) {
        env << "Failed to create a RTSP client for URL \"" << rtspURL
            << "\": " << env.getResultMsg() << "\n";
        return;
    }

    rtspClient->sendDescribeCommand(continueAfterDESCRIBE);
}

void continueAfterDESCRIBE(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    do {
        UsageEnvironment& env = rtspClient->envir();                // alias
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to get a SDP description: " << resultString << "\n";
            delete[] resultString;
            break;
        }

        char* const sdpDescription = resultString;
        env << *rtspClient << "Got a SDP description:\n"
            << sdpDescription << "\n";

        // Create a media session object from this SDP description:
        scs.session = MediaSession::createNew(env, sdpDescription);
        delete[] sdpDescription; // because we don't need it anymore
        if (scs.session == nullptr) {
            env << *rtspClient
                << "Failed to create a MediaSession object from the SDP "
                   "description: "
                << env.getResultMsg() << "\n";
            break;
        }
        if (scs.session->hasSubsessions() == False) {
            env << *rtspClient
                << "This session has no media subsessions (i.e., no \"m=\" "
                   "lines)\n";
            break;
        }

        scs.iter = new MediaSubsessionIterator(*scs.session);
        setupNextSubsession(rtspClient);
        return;
    } while (false);

    // An unrecoverable error occurred with this stream.
    shutdownStream(rtspClient);
}

#define REQUEST_STREAMING_OVER_TCP False

void setupNextSubsession(RTSPClient* rtspClient)
{
    UsageEnvironment& env = rtspClient->envir();                // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    scs.subsession = scs.iter->next();
    if (scs.subsession != NULL) {
        if (scs.subsession->initiate() == False) {
            env << *rtspClient << "Failed to initiate the \"" << *scs.subsession
                << "\" subsession: " << env.getResultMsg() << "\n";
            setupNextSubsession(rtspClient); // give up on this subsession; go to the next one
        } else {
            env << *rtspClient << "Initiated the \"" << *scs.subsession << "\" subsession (";
            if (scs.subsession->rtcpIsMuxed() == True) {
                env << "client port " << scs.subsession->clientPortNum();
            } else {
                env << "client ports " << scs.subsession->clientPortNum() << "-"
                    << scs.subsession->clientPortNum() + 1;
            }
            env << ")\n";

            rtspClient->sendSetupCommand(*scs.subsession, continueAfterSETUP, False,
                REQUEST_STREAMING_OVER_TCP);
        }
        return;
    }

    if (scs.session->absStartTime() != nullptr) {
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY, scs.session->absStartTime(),
            scs.session->absEndTime());
    } else {
        scs.duration = scs.session->playEndTime() - scs.session->playStartTime();
        rtspClient->sendPlayCommand(*scs.session, continueAfterPLAY);
    }
}

void continueAfterSETUP(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    do {
        UsageEnvironment& env = rtspClient->envir();
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs;

        if (resultCode != 0) {
            env << *rtspClient << "Failed to set up the \"" << *scs.subsession
                << "\" subsession: " << resultString << "\n";
            break;
        }

        env << *rtspClient << "Set up the \"" << *scs.subsession << "\" subsession (";
        if (scs.subsession->rtcpIsMuxed() == True) {
            env << "client port " << scs.subsession->clientPortNum();
        } else {
            env << "client ports " << scs.subsession->clientPortNum() << "-"
                << scs.subsession->clientPortNum() + 1;
        }
        env << ")\n";

        scs.subsession->sink = DummySink::createNew(env, *scs.subsession, scs.output_, rtspClient->url());
        if (scs.subsession->sink == nullptr) {
            env << *rtspClient << "Failed to create a data sink for the \"" << *scs.subsession
                << "\" subsession: " << env.getResultMsg() << "\n";
            break;
        }

        env << *rtspClient << "Created a data sink for the \"" << *scs.subsession
            << "\" subsession\n";
        scs.subsession->miscPtr = rtspClient; // a hack to let subsession handler functions get the
                                              // "RTSPClient" from the subsession
        scs.subsession->sink->startPlaying(*(scs.subsession->readSource()), subsessionAfterPlaying,
            scs.subsession);
        // Also set a handler to be called if a RTCP "BYE" arrives for this
        // subsession:
        if (scs.subsession->rtcpInstance() != NULL) {
            scs.subsession->rtcpInstance()->setByeWithReasonHandler(subsessionByeHandler,
                scs.subsession);
        }
    } while (false);

    delete[] resultString;

    setupNextSubsession(rtspClient);
}

void continueAfterPLAY(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    Boolean success = False;

    do {
        UsageEnvironment& env = rtspClient->envir();                // alias
        StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

        if (resultCode != 0) {
            env << *rtspClient << "Failed to start playing session: " << resultString << "\n";
            break;
        }

        // Set a timer to be handled at the end of the stream's expected
        // duration (if the stream does not already signal its end using a RTCP
        // "BYE").  This is optional.  If, instead, you want to keep the stream
        // active - e.g., so you can later 'seek' back within it and do another
        // RTSP "PLAY" - then you can omit this code. (Alternatively, if you
        // don't want to receive the entire stream, you could set this timer for
        // some shorter value.)
        if (scs.duration > 0) {
            unsigned const delaySlop = 2; // number of seconds extra to delay, after the stream's
                                          // expected duration.  (This is optional.)
            scs.duration += delaySlop;
            unsigned uSecsToDelay = (unsigned)(scs.duration * 1000000);
            scs.streamTimerTask = env.taskScheduler().scheduleDelayedTask(
                uSecsToDelay, (TaskFunc*)streamTimerHandler, rtspClient);
        }

        env << *rtspClient << "Started playing session";
        if (scs.duration > 0) {
            env << " (for up to " << scs.duration << " seconds)";
        }
        env << "...\n";

        scs.sessionTimeoutBrokenServerTask = env.taskScheduler().scheduleDelayedTask(
            55UL * 1'000'000, (TaskFunc*)sessionTimeoutBrokenServerHandle, rtspClient);

        success = True;
    } while (false);

    delete[] resultString;

    if (success == False) {
        // An unrecoverable error occurred with this stream.
        shutdownStream(rtspClient);
    }
}

static void subsessionAfterPlaying(void* clientData)
{
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    RTSPClient* rtspClient = (RTSPClient*)(subsession->miscPtr);

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
    shutdownStream(rtspClient);
}

static void subsessionByeHandler(void* clientData, char const* reason)
{
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    RTSPClient* rtspClient = (RTSPClient*)subsession->miscPtr;
    UsageEnvironment& env = rtspClient->envir(); // alias

    env << *rtspClient << "Received RTCP \"BYE\"";
    if (reason != NULL) {
        env << " (reason:\"" << reason << "\")";
        delete[] (char*)reason;
    }
    env << " on \"" << *subsession << "\" subsession\n";

    // Now act as if the subsession had closed:
    subsessionAfterPlaying(subsession);
}

static void streamTimerHandler(void* clientData)
{
    ourRTSPClient* rtspClient = (ourRTSPClient*)clientData;
    StreamClientState& scs = rtspClient->scs; // alias

    scs.streamTimerTask = NULL;

    // Shut down the stream:
    shutdownStream(rtspClient);
}

static void sessionTimeoutBrokenServerHandle(RTSPClient* rtspClient)
{
    // if (!sendKeepAlivesToBrokenServers) return; // we're not checking

    // Send an "OPTIONS" request, starting with the second call
    // rtspClient->sendOptionsCommand(nullptr);
    ourRTSPClient* ourRtspClient = (ourRTSPClient*)rtspClient;
    StreamClientState& scs = ourRtspClient->scs; // alias
    rtspClient->sendGetParameterCommand(*scs.session, nullptr, nullptr);

    unsigned sessionTimeout = rtspClient->sessionTimeoutParameter();
    sessionTimeout = sessionTimeout == 0 ? 60 : sessionTimeout;
    int64_t secondsUntilNextKeepAlive = sessionTimeout <= 5 ? 1 : sessionTimeout - 5;

    rtspClient->envir().taskScheduler().rescheduleDelayedTask(
        scs.sessionTimeoutBrokenServerTask, secondsUntilNextKeepAlive * 1'000'000,
        (TaskFunc*)sessionTimeoutBrokenServerHandle, rtspClient);
}

static void shutdownStream(RTSPClient* rtspClient)
{
    UsageEnvironment& env = rtspClient->envir();                // alias
    StreamClientState& scs = ((ourRTSPClient*)rtspClient)->scs; // alias

    // First, check whether any subsessions have still to be closed:
    if (scs.session != NULL) {
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator iter(*scs.session);
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
            rtspClient->sendTeardownCommand(*scs.session, NULL);
        }
    }

    env << *rtspClient << "Closing the stream.\n";
    Medium::close(rtspClient);
    // Note that this will also cause this stream's "StreamClientState"
    // structure to get reclaimed.

    eventLoopWatchVariable = 1;
}

// Implementation of "ourRTSPClient":

ourRTSPClient* ourRTSPClient::createNew(UsageEnvironment& env,
    char const* rtspURL,
    std::string const& output,
    int verbosityLevel,
    char const* applicationName,
    portNumBits tunnelOverHTTPPortNum)
{
    return new ourRTSPClient(env, rtspURL, output, verbosityLevel, applicationName,
        tunnelOverHTTPPortNum);
}

ourRTSPClient::ourRTSPClient(UsageEnvironment& env,
    char const* rtspURL,
    std::string const& output,
    int verbosityLevel,
    char const* applicationName,
    portNumBits tunnelOverHTTPPortNum)
    : RTSPClient(env, rtspURL, verbosityLevel, applicationName, tunnelOverHTTPPortNum, -1)
    , scs(output)
{
}

ourRTSPClient::~ourRTSPClient() { }

StreamClientState::StreamClientState(std::string const& output)
    : output_(output)
    , iter(NULL)
    , session(NULL)
    , subsession(NULL)
    , streamTimerTask(NULL)
    , sessionTimeoutBrokenServerTask(nullptr)
    , duration(0.0)
{
}

StreamClientState::~StreamClientState()
{
    delete iter;
    if (session != NULL) {
        // We also need to delete "session", and unschedule "streamTimerTask"
        // (if set)
        UsageEnvironment& env = session->envir(); // alias

        env.taskScheduler().unscheduleDelayedTask(sessionTimeoutBrokenServerTask);
        env.taskScheduler().unscheduleDelayedTask(streamTimerTask);
        Medium::close(session);
    }
}

#define DUMMY_SINK_RECEIVE_BUFFER_SIZE 2'000'000

DummySink* DummySink::createNew(UsageEnvironment& env,
    MediaSubsession& subsession,
    std::string const& output,
    char const* streamId)
{
    return new DummySink(env, subsession, output, streamId);
}

DummySink::DummySink(UsageEnvironment& env, MediaSubsession& subsession, std::string const& output, char const* streamId)
    : MediaSink(env)
    , fSubsession(subsession)
{
    fStreamId = strDup(streamId);
    fReceiveBuffer = new u_int8_t[DUMMY_SINK_RECEIVE_BUFFER_SIZE + 4];
    static constexpr uint8_t start_marker[] { 0x00, 0x00, 0x00, 0x01 };
    memcpy(fReceiveBuffer, start_marker, 4);
    fOutStream.open(output.c_str(), std::ios::binary);
    fWaitingForSPSUnit = true;
}

DummySink::~DummySink()
{
    delete[] fReceiveBuffer;
    delete[] fStreamId;
}

void DummySink::afterGettingFrame(void* clientData,
    unsigned frameSize,
    unsigned numTruncatedBytes,
    struct timeval presentationTime,
    unsigned durationInMicroseconds)
{
    DummySink* sink = (DummySink*)clientData;
    sink->afterGettingFrame(frameSize, numTruncatedBytes, presentationTime, durationInMicroseconds);
}

// #define DEBUG_PRINT_EACH_RECEIVED_FRAME 1

void DummySink::afterGettingFrame(unsigned frameSize,
    unsigned /*numTruncatedBytes*/,
    struct timeval /*presentationTime*/,
    unsigned /*durationInMicroseconds*/)
{
    // We've just received a frame of data.  (Optionally) print out information
    // about it:
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

    if (fWaitingForSPSUnit && fReceiveBuffer[4] == 0x67) {
        fWaitingForSPSUnit = false;
    }

    if (!fWaitingForSPSUnit) {
        std::cout << std::dec << frameSize << "\t0x" << std::setfill('0')
                  << std::setw(2) << std::hex << (int)fReceiveBuffer[4] << "\n";
        fOutStream.write(reinterpret_cast<char const*>(fReceiveBuffer), frameSize + 4);
    }

    continuePlaying();
}

Boolean DummySink::continuePlaying()
{
    if (fSource == NULL) {
        return False; // sanity check (should not happen)
    }

    fSource->getNextFrame(fReceiveBuffer + 4, DUMMY_SINK_RECEIVE_BUFFER_SIZE, afterGettingFrame,
        this, onSourceClosure, this);

    return True;
}
