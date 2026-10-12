#pragma once

void KeyCache_Prepare();

void KeyCache_ResetForNewPaths();

void KeyCache_SetPreInitKeyFiles(const fs::path& preferred, const fs::path& fallback);

uint8* KeyCache_GetAES128(sint32 index);

uint64 KeyCache_GetFingerprint();
