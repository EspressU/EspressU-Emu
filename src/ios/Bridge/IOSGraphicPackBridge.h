// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

// Copyright (c) 2026 MuffinEMU Development Team.
// SPDX-License-Identifier: MPL-2.0
// This notice must be kept in any copy or derivative (MPL-2.0 §3.4).

#ifndef IOSGraphicPackBridge_h
#define IOSGraphicPackBridge_h

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool espressu_gp_title_running(void);

bool espressu_gp_reload(const char* mlcPath);

const char* espressu_gp_list(void);

const char* espressu_gp_details(const char* packPath);

bool espressu_gp_set_enabled(const char* packPath, bool enabled);

bool espressu_gp_set_preset(const char* packPath, const char* category, const char* preset);

bool espressu_gp_reset_pack(const char* packPath);

#ifdef __cplusplus
}
#endif

#endif
