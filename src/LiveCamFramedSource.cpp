#include "LiveCamFramedSource.hpp"

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

int gettimeofday(struct timeval* tp, struct timezone* tzp)
{
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    // This magic number is the number of 100 nanosecond intervals since January 1, 1601 (UTC)
    // until 00:00:00 January 1, 1970
    static const uint64_t EPOCH = ((uint64_t)116444736000000000ULL);

    SYSTEMTIME system_time;
    FILETIME file_time;
    uint64_t time;

    GetSystemTime(&system_time);
    SystemTimeToFileTime(&system_time, &file_time);
    time = ((uint64_t)file_time.dwLowDateTime);
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec = (long)((time - EPOCH) / 10000000L);
    tp->tv_usec = (long)(system_time.wMilliseconds * 1000);
    return 0;
}
#endif
namespace LIRS {

    LiveCamFramedSource *LiveCamFramedSource::createNew(UsageEnvironment &env, Transcoder &transcoder) {
        return new LiveCamFramedSource(env, transcoder);
    }

    LiveCamFramedSource::~LiveCamFramedSource() {

        transcoder.stop();

        // delete trigger
        envir().taskScheduler().deleteEventTrigger(eventTriggerId);
        eventTriggerId = 0;

        // cleanup encoded data buffer
        encodedDataBuffer.clear();
        encodedDataBuffer.shrink_to_fit();

    }

    LiveCamFramedSource::LiveCamFramedSource(UsageEnvironment &env, Transcoder &transcoder) :
            FramedSource(env), transcoder(transcoder), eventTriggerId(0), max_nalu_size_bytes(0) {

        // create trigger invoking method which will deliver frame
        eventTriggerId = envir().taskScheduler().createEventTrigger(LiveCamFramedSource::deliverFrame0);

        encodedDataBuffer.reserve(5); // reserve enough space for handling incoming encoded data

        // set transcoder's callback indicating new encoded data availability
        transcoder.setOnEncodedDataCallback(std::bind(&LiveCamFramedSource::onEncodedData, this,
                                                      std::placeholders::_1));

        // start video data encoding/decoding in a new thread

        LOG(INFO) << "Starting to capture and encode video from the camera: "
                   << transcoder.getConfig().getName();

        std::thread([&transcoder]() {
            transcoder.run();
        }).detach();
    }

    void LiveCamFramedSource::onEncodedData(std::vector<uint8_t> &&newData) {

        if (!isCurrentlyAwaitingData()) {
            return;
        }

        encodedDataMutex.lock();

        // store encoded data to be processed later
        encodedDataBuffer.emplace_back(std::move(newData));

        encodedDataMutex.unlock();

        // publish an event to be handled by the event loop
        envir().taskScheduler().triggerEvent(eventTriggerId, this);
    }

    void LiveCamFramedSource::deliverFrame0(void *clientData) {
        ((LiveCamFramedSource *) clientData)->deliverData();
    }

    void LiveCamFramedSource::doStopGettingFrames() {

        DLOG(INFO) << "Stop getting frames from the camera: " << transcoder.getConfig().getName();

        FramedSource::doStopGettingFrames();
    }

    void LiveCamFramedSource::deliverData() {

        if (!isCurrentlyAwaitingData()) {
            return;
        }

        encodedDataMutex.lock();

        encodedData = std::move(encodedDataBuffer.back());

        encodedDataBuffer.pop_back();

        encodedDataMutex.unlock();

        if (encodedData.size() > max_nalu_size_bytes) {
            max_nalu_size_bytes = encodedData.size();
        }

        if (encodedData.size() > fMaxSize) { // truncate data

            LOG(WARNING) << "Exceeded max size, truncated: " << fNumTruncatedBytes << ", size: " << encodedData.size();

            fFrameSize = fMaxSize;

            fNumTruncatedBytes = static_cast<unsigned int>(encodedData.size() - fMaxSize);

        } else {
            fFrameSize = static_cast<unsigned int>(encodedData.size());
        }

        // can be changed to the actual frame's captured time
        gettimeofday(&fPresentationTime, nullptr);

        // DO NOT CHANGE ADDRESS, ONLY COPY (see Live555 docs)
        memcpy(fTo, encodedData.data(), fFrameSize);

        // should be invoked after successfully getting data
        FramedSource::afterGetting(this);
    }

    void LiveCamFramedSource::doGetNextFrame() {

        if (!encodedDataBuffer.empty()) {
            deliverData();
        } else {
            fFrameSize = 0;
            return;
        }
    }
}
