#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include <thread>
#include <chrono>
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

// Only decrypted content is launched: .wua archives, decrypted game folders and standalone executables.
// Encrypted dumps are recognised so that they can be refused with a message, and are never opened.
enum
{
	IOS_TITLE_LAUNCH_OK = 0,
	IOS_TITLE_LAUNCH_INVALID_RPX = 1,
	IOS_TITLE_LAUNCH_UNABLE_TO_MOUNT = 2,
	IOS_TITLE_LAUNCH_ENCRYPTED = 3,
	IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT = 5,
	IOS_TITLE_LAUNCH_BASE_NOT_FOUND = 6,
};

static thread_local std::string sLastLaunchDetail;

const char* IOSTitleLaunch_LastErrorDetail()
{
	return sLastLaunchDetail.c_str();
}

static std::atomic_bool sTitleListInitialized{false};

void IOSTitleLaunch_InitializeTitleList()
{
	if (sTitleListInitialized.exchange(true))
		return;
	cemuLog_log(LogType::Force, "iOS: title list initialized by the core");
}

static void IOSTitleLaunch_RescanInstalledContent(int maxSeconds = 10)
{
	CafeTitleList::Refresh();
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(maxSeconds);
	while (CafeTitleList::IsScanning())
	{
		if (std::chrono::steady_clock::now() >= deadline)
		{
			cemuLog_log(LogType::Force, "iOS: title rescan still running after {}s - launching with the titles found so far", maxSeconds);
			return;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
}

static int IOSTitleLaunch_ResultFromPrepare(CafeSystem::PREPARE_STATUS_CODE r)
{
	switch (r)
	{
	case CafeSystem::PREPARE_STATUS_CODE::SUCCESS:
		return IOS_TITLE_LAUNCH_OK;
	case CafeSystem::PREPARE_STATUS_CODE::INVALID_RPX:
		return IOS_TITLE_LAUNCH_INVALID_RPX;
	default:
		return IOS_TITLE_LAUNCH_UNABLE_TO_MOUNT;
	}
}

int IOSTitleLaunch_PrepareForegroundTitle(const char* pathStr)
{
	sLastLaunchDetail.clear();
	if (!pathStr || pathStr[0] == '\0')
		return IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT;
	fs::path launchPath = fs::path(pathStr);

	IOSTitleLaunch_InitializeTitleList();

	TitleInfo launchTitle{launchPath};
	if (launchTitle.IsValid())
	{
		CafeTitleList::AddTitleFromPath(launchPath);
		TitleId baseTitleId;
		if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
		{
			cemuLog_log(LogType::Force, "iOS: no base title found for {:016x}", (uint64)launchTitle.GetAppTitleId());
			return IOS_TITLE_LAUNCH_BASE_NOT_FOUND;
		}
		IOSTitleLaunch_RescanInstalledContent();
		CafeTitleList::AddTitleFromPath(launchPath);
		cemuLog_log(LogType::Force, "iOS: launching title {:016x} from {}", (uint64)baseTitleId, _pathToUtf8(launchPath));
		return IOSTitleLaunch_ResultFromPrepare(CafeSystem::PrepareForegroundTitle(baseTitleId));
	}

	CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
	if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
	{
		cemuLog_log(LogType::Force, "iOS: launching standalone executable {}", _pathToUtf8(launchPath));
		return IOSTitleLaunch_ResultFromPrepare(CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath));
	}

	switch (launchTitle.GetInvalidReason())
	{
	case TitleInfo::InvalidReason::NO_DISC_KEY:
	case TitleInfo::InvalidReason::NO_TITLE_TIK:
	case TitleInfo::InvalidReason::BAD_TITLE_TMD:
	case TitleInfo::InvalidReason::BAD_TITLE_TIK:
	case TitleInfo::InvalidReason::TITLE_KEY_INVALID:
		cemuLog_log(LogType::Force, "iOS: {} is encrypted and is not supported", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_ENCRYPTED;
	default:
		cemuLog_log(LogType::Force, "iOS: {} is not a title this build can launch (invalid reason {})", _pathToUtf8(launchPath), (int)launchTitle.GetInvalidReason());
		return IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT;
	}
}
