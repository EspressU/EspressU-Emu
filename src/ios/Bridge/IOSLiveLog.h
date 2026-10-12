#ifndef CEMU_IOS_LIVE_LOG_H
#define CEMU_IOS_LIVE_LOG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ios_live_log_set_enabled(bool enabled);
bool ios_live_log_is_enabled(void);

void ios_live_log_push(const char* line);

void ios_live_log_begin_run(void);

const char* ios_live_log_drain(uint64_t* inoutCursor, uint64_t* outDropped);

#ifdef __cplusplus
}
#endif

#endif
