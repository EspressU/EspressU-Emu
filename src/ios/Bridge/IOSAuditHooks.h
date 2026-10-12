#ifndef CEMU_IOS_AUDIT_HOOKS_H
#define CEMU_IOS_AUDIT_HOOKS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CEMU_AUDIT_API_VERSION 1

int cemu_audit_api_version(void);

const char* cemu_audit_build_info(void);

uint64_t cemu_audit_now_ns(void);

void cemu_audit_set_log_profile(int profile);

bool cemu_audit_guest_read(uint32_t guestAddress, void* out, uint32_t length);
bool cemu_audit_guest_write(uint32_t guestAddress, const void* data, uint32_t length);

const char* cemu_audit_snapshot_json(void);

#define CEMU_AUDIT_VIEW_TV  1u
#define CEMU_AUDIT_VIEW_PAD 2u

#define CEMU_AUDIT_FRAME_OK                 0u
#define CEMU_AUDIT_FRAME_UNSUPPORTED_FORMAT 1u
#define CEMU_AUDIT_FRAME_READBACK_FAILED    2u

typedef struct CemuAuditFrame {
    uint32_t seq;
    uint32_t view;
    uint32_t status;
    uint32_t latteFrame;
    uint64_t timestampNs;
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t pixelFormat;
    uint32_t thumbWidth;
    uint32_t thumbHeight;
    float    meanR;
    float    meanG;
    float    meanB;
    float    blackFraction;
    float    whiteFraction;
    uint32_t minLuma;
    uint32_t maxLuma;
    uint64_t hash;
} CemuAuditFrame;

void cemu_audit_capture_arm(uint32_t viewMask, uint32_t count, uint32_t thumbWidth, uint32_t thumbHeight);

void cemu_audit_capture_cancel(void);

uint32_t cemu_audit_capture_pending(void);

bool cemu_audit_capture_pop(CemuAuditFrame* header, uint8_t* thumbRGB, uint32_t thumbCapacity);

void cemu_audit_frame_timing_enable(bool enabled);

uint32_t cemu_audit_frame_timing_drain(uint64_t* outNs, uint32_t capacity, uint32_t* dropped);

typedef struct CemuAuditAudioStats {
    uint64_t callbacks;
    uint64_t frames;
    uint64_t validFrames;
    uint64_t underrunCallbacks;
    uint64_t underrunFrames;
    uint64_t feedRejects;
    uint64_t silentCallbacks;
    uint64_t discontinuities;
    uint32_t maxStep;
    uint32_t peak;
    uint32_t longestZeroRun;
    uint32_t channels;
    double   rms;
} CemuAuditAudioStats;

void cemu_audit_audio_reset(void);
void cemu_audit_audio_get(CemuAuditAudioStats* out);

void cemu_audit_audio_note_render(const void* device, const int16_t* samples, uint32_t bytesValid,
                                  uint32_t bytesRequested, uint32_t channels, uint32_t bitsPerSample);
void cemu_audit_audio_note_feed_reject(void);

#ifdef __cplusplus
}

bool cemu_audit_capture_wants(bool tv);
void cemu_audit_capture_deliver(bool tv, uint32_t latteFrame, uint32_t srcWidth, uint32_t srcHeight,
                                uint32_t rawPixelFormat, int layout  ,
                                const uint8_t* pixels, uint32_t rowBytes, bool readbackFailed);
#endif

#endif
