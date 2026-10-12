
#pragma once

#include "IAudioAPI.h"
#include "AudioRingBuffer.h"
#include "iOSDeviceDescription.h"
#include <AudioUnit/AudioUnit.h>
#include <TargetConditionals.h>
#include <atomic>
#include <vector>

#if defined(__APPLE__) && TARGET_OS_IOS

class IOSAudioAPI : public IAudioAPI
{
public:
    IOSAudioAPI(uint32 samplerate,
                uint32 channels,
                uint32 samples_per_block,
                uint32 bits_per_sample);

    virtual ~IOSAudioAPI();

    virtual AudioAPI GetType() const override { return IOSAudio; }
    virtual bool Play() override;
    virtual bool Stop() override;
    virtual bool NeedAdditionalBlocks() const override;
    virtual bool FeedBlock(sint16* data) override;

    static std::vector<IAudioAPI::DeviceDescriptionPtr> GetDevices();

private:
    static OSStatus RenderCallback(
        void* inRefCon,
        AudioUnitRenderActionFlags* ioActionFlags,
        const AudioTimeStamp* inTimeStamp,
        UInt32 inBusNumber,
        UInt32 inNumberFrames,
        AudioBufferList* ioData);

    AudioUnit m_audioUnit = nullptr;

    void RestartAfterInterruption(const char* why, bool onlyIfStopped = false);
    void* m_interruptionObserver = nullptr;
    void* m_becameActiveObserver = nullptr;
    void StopForBackground();
    void* m_enteredBackgroundObserver = nullptr;

    AudioRingBuffer m_buffer;
    std::atomic_bool m_isPlaying = false;
};

#endif
