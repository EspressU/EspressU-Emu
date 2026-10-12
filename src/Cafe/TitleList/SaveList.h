#pragma once

#include "SaveInfo.h"

struct CafeSaveListCallbackEvent
{
	enum class TYPE
	{
		SAVE_DISCOVERED,
		SAVE_REMOVED,
		SCAN_FINISHED,
	};
	TYPE eventType;
	SaveInfo* saveInfo;
};

class CafeSaveList
{
public:
	static void Initialize();
	static void SetMLCPath(fs::path mlcPath);
	static void Refresh();

	static SaveInfo GetSaveByTitleId(TitleId titleId);

	static uint64 RegisterCallback(void(*cb)(CafeSaveListCallbackEvent* evt, void* ctx), void* ctx);
	static void UnregisterCallback(uint64 id);

private:
	static void RefreshThreadWorker();
	static void DiscoveredSave(SaveInfo* saveInfo);
};