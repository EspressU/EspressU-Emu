
#include <TargetConditionals.h>

#if defined(__APPLE__) && TARGET_OS_IOS

#include "iOSAudioInputAPI.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#import <AVFoundation/AVFoundation.h>

IOSAudioInputAPI::IOSAudioInputAPI(uint32 samplerate,
                                   uint32 channels,
                                   uint32 samples_per_block,
                                   uint32 bits_per_sample)
	: IAudioInputAPI(samplerate, channels, samples_per_block, bits_per_sample),
	  m_buffer((size_t)samples_per_block * channels * (bits_per_sample / 8) * kBlockCount)
{
    AVAudioSession* session = [AVAudioSession sharedInstance];

    if (session.recordPermission == AVAudioSessionRecordPermissionDenied)
        throw std::runtime_error("microphone permission was denied");

    if (session.recordPermission == AVAudioSessionRecordPermissionUndetermined)
        [session requestRecordPermission:^(BOOL granted) {}];

    ConfigureSession(samplerate, samples_per_block);

    AudioComponentDescription desc{};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_RemoteIO;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent comp = AudioComponentFindNext(nullptr, &desc);
    if (!comp || AudioComponentInstanceNew(comp, &m_audioUnit) != noErr)
        throw std::runtime_error("can't initialize iOS microphone audio unit");

    auto disposeAudioUnitOnError = [this]() {
        AudioComponentInstanceDispose(m_audioUnit);
        m_audioUnit = nullptr;
    };

    UInt32 enableInput = 1;
    if (AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_EnableIO,
                             kAudioUnitScope_Input, 1, &enableInput, sizeof(enableInput)) != noErr) {
        disposeAudioUnitOnError();
        throw std::runtime_error("can't enable iOS microphone input");
    }

    const uint32 bytesPerSample = bits_per_sample / 8;

    AudioStreamBasicDescription format{};
    format.mSampleRate       = samplerate;
    format.mFormatID         = kAudioFormatLinearPCM;
    format.mFormatFlags      = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    format.mChannelsPerFrame = m_channels;
    format.mBitsPerChannel   = bits_per_sample;
    format.mFramesPerPacket  = 1;
    format.mBytesPerFrame    = m_channels * bytesPerSample;
    format.mBytesPerPacket   = format.mBytesPerFrame * format.mFramesPerPacket;

    if (AudioUnitSetProperty(m_audioUnit, kAudioUnitProperty_StreamFormat,
                             kAudioUnitScope_Output, 1, &format, sizeof(format)) != noErr) {
        disposeAudioUnitOnError();
        throw std::runtime_error("can't set iOS microphone stream format");
    }

    AURenderCallbackStruct callback{};
    callback.inputProc = InputCallback;
    callback.inputProcRefCon = this;

    if (AudioUnitSetProperty(m_audioUnit, kAudioOutputUnitProperty_SetInputCallback,
                             kAudioUnitScope_Global, 1, &callback, sizeof(callback)) != noErr) {
        disposeAudioUnitOnError();
        throw std::runtime_error("can't set iOS microphone callback");
    }

    const auto sessionFrames = static_cast<size_t>(std::ceil(session.sampleRate * session.IOBufferDuration));
    const auto captureFrames = std::max<size_t>({(size_t)samples_per_block, sessionFrames, (size_t)4096}) + 1;
    m_captureBuffer.resize(captureFrames * m_channels * (bits_per_sample / 8));

    if (AudioUnitInitialize(m_audioUnit) != noErr) {
        disposeAudioUnitOnError();
        throw std::runtime_error("can't initialize iOS microphone audio unit");
    }

    m_notificationControl = std::make_shared<NotificationControl>();
    m_notificationControl->self = this;
    auto control = m_notificationControl;
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    m_interruptionObserver = (void*)CFBridgingRetain(
        [center addObserverForName:AVAudioSessionInterruptionNotification
                            object:session
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification* note) {
            std::lock_guard<std::mutex> lock(control->mutex);
            if (!control->self)
                return;
            const NSUInteger type = [note.userInfo[AVAudioSessionInterruptionTypeKey] unsignedIntegerValue];
            control->self->HandleInterruption(type == AVAudioSessionInterruptionTypeBegan);
        }]);
    m_routeObserver = (void*)CFBridgingRetain(
        [center addObserverForName:AVAudioSessionRouteChangeNotification
                            object:session
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification* note) {
            std::lock_guard<std::mutex> lock(control->mutex);
            if (!control->self)
                return;
            control->self->HandleRouteChange();
        }]);
}

void IOSAudioInputAPI::ConfigureSession(uint32 samplerate, uint32 samplesPerBlock)
{
    NSError* error = nil;
    AVAudioSession* session = [AVAudioSession sharedInstance];
    [session setCategory:AVAudioSessionCategoryPlayAndRecord
                    mode:AVAudioSessionModeDefault
                 options:AVAudioSessionCategoryOptionMixWithOthers |
                         AVAudioSessionCategoryOptionDefaultToSpeaker |
                         AVAudioSessionCategoryOptionAllowBluetoothA2DP
                   error:&error];
    [session setPreferredSampleRate:samplerate error:&error];
    [session setPreferredIOBufferDuration:(double)samplesPerBlock / samplerate error:&error];
    [session setActive:YES error:&error];
}

IOSAudioInputAPI::~IOSAudioInputAPI()
{
    if (m_notificationControl) {
        std::lock_guard<std::mutex> lock(m_notificationControl->mutex);
        m_notificationControl->self = nullptr;
    }
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    if (m_interruptionObserver) {
        [center removeObserver:(__bridge id)m_interruptionObserver];
        CFRelease(m_interruptionObserver);
    }
    if (m_routeObserver) {
        [center removeObserver:(__bridge id)m_routeObserver];
        CFRelease(m_routeObserver);
    }
    if (m_audioUnit) {
        m_isPlaying = false;
        AudioOutputUnitStop(m_audioUnit);
        AudioUnitUninitialize(m_audioUnit);
        AudioComponentInstanceDispose(m_audioUnit);
        m_audioUnit = nullptr;
    }
}

bool IOSAudioInputAPI::ConsumeBlock(sint16* data)
{
    std::uint8_t discard[256];
    if (m_flushRequested.exchange(false)) {
        while (m_buffer.read(discard, sizeof(discard)) != 0) {}
    }
    else {
        const size_t backlog = m_buffer.size();
        if (backlog > (size_t)m_bytesPerBlock * 6) {
            size_t toDrop = backlog - (size_t)m_bytesPerBlock * 2;
            while (toDrop > 0) {
                const size_t n = m_buffer.read(discard, std::min(toDrop, sizeof(discard)));
                if (n == 0)
                    break;
                toDrop -= n;
            }
        }
    }

    const auto copied = m_buffer.read(reinterpret_cast<std::uint8_t*>(data), m_bytesPerBlock);
    if (copied != m_bytesPerBlock)
        std::memset(reinterpret_cast<std::uint8_t*>(data) + copied, 0, m_bytesPerBlock - copied);

    if (m_volume != 50 && m_bitsPerSample == 16) {
        const float gain = std::clamp(m_volume, 0, 100) / 50.0f;
        const size_t count = m_bytesPerBlock / sizeof(sint16);
        for (size_t i = 0; i < count; i++) {
            const float v = data[i] * gain;
            data[i] = (sint16)std::clamp(v, -32768.0f, 32767.0f);
        }
    }

    return true;
}

bool IOSAudioInputAPI::Play()
{
    m_wantPlaying = true;
    if (!m_audioUnit)
        return false;
    if (m_isPlaying)
        return true;
    if (m_interrupted)
        return false;

    m_flushRequested = true;
    if (AudioOutputUnitStart(m_audioUnit) != noErr)
        return false;

    m_isPlaying = true;
    return true;
}

bool IOSAudioInputAPI::Stop()
{
    m_wantPlaying = false;
    if (!m_audioUnit)
        return false;
    if (!m_isPlaying)
        return true;

    if (AudioOutputUnitStop(m_audioUnit) != noErr)
        return false;

    m_isPlaying = false;
    return true;
}

void IOSAudioInputAPI::HandleInterruption(bool began)
{
    if (began) {
        m_interrupted = true;
        m_isPlaying = false;
        return;
    }

    m_interrupted = false;
    NSError* error = nil;
    [[AVAudioSession sharedInstance] setActive:YES error:&error];
    m_flushRequested = true;
    if (m_wantPlaying && m_audioUnit && AudioOutputUnitStart(m_audioUnit) == noErr)
        m_isPlaying = true;
}

void IOSAudioInputAPI::HandleRouteChange()
{
    AVAudioSession* session = [AVAudioSession sharedInstance];
    if (![session.category isEqualToString:AVAudioSessionCategoryPlayAndRecord])
        ConfigureSession(m_samplerate, m_samplesPerBlock);

    m_flushRequested = true;
    if (!m_wantPlaying || !m_audioUnit || m_interrupted)
        return;

    UInt32 running = 0;
    UInt32 size = sizeof(running);
    AudioUnitGetProperty(m_audioUnit, kAudioOutputUnitProperty_IsRunning, kAudioUnitScope_Global, 0, &running, &size);
    if (!running)
        m_isPlaying = (AudioOutputUnitStart(m_audioUnit) == noErr);
}

OSStatus IOSAudioInputAPI::InputCallback(void* inRefCon,
                                         AudioUnitRenderActionFlags* ioActionFlags,
                                         const AudioTimeStamp* inTimeStamp,
                                         UInt32 inBusNumber,
                                         UInt32 inNumberFrames,
                                         AudioBufferList* ioData)
{
    auto* self = reinterpret_cast<IOSAudioInputAPI*>(inRefCon);
    if (!self || !self->m_audioUnit)
        return noErr;

    const auto bytesNeeded = (size_t)inNumberFrames * self->m_channels * (self->m_bitsPerSample / 8);
    if (bytesNeeded > self->m_captureBuffer.size())
        return noErr;

    AudioBufferList bufferList{};
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = self->m_channels;
    bufferList.mBuffers[0].mDataByteSize = static_cast<UInt32>(bytesNeeded);
    bufferList.mBuffers[0].mData = self->m_captureBuffer.data();

    OSStatus status = AudioUnitRender(self->m_audioUnit, ioActionFlags, inTimeStamp, inBusNumber, inNumberFrames, &bufferList);
    if (status != noErr)
        return status;

    self->m_buffer.write(self->m_captureBuffer.data(), bytesNeeded);
    return noErr;
}

std::vector<IAudioInputAPI::DeviceDescriptionPtr> IOSAudioInputAPI::GetDevices()
{
    std::vector<DeviceDescriptionPtr> devices;
    devices.push_back(std::make_shared<IOSAudioInputDeviceDescription>());
    return devices;
}

#endif
