#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/Filesystem/FST/KeyCache.h"
#include "Cafe/Filesystem/FST/FST.h"
#include <cctype>
#include <fstream>
#include <thread>
#include <chrono>
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"

#include <atomic>
#include <filesystem>
#include <iterator>
#include <string>
#include <vector>

enum
{
	IOS_TITLE_LAUNCH_OK = 0,
	IOS_TITLE_LAUNCH_INVALID_RPX = 1,
	IOS_TITLE_LAUNCH_UNABLE_TO_MOUNT = 2,
	IOS_TITLE_LAUNCH_NO_DISC_KEY = 3,
	IOS_TITLE_LAUNCH_NO_TITLE_TIK = 4,
	IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT = 5,
	IOS_TITLE_LAUNCH_BASE_NOT_FOUND = 6,
	IOS_TITLE_LAUNCH_BAD_TITLE_TMD = 7,
	IOS_TITLE_LAUNCH_BAD_TITLE_TIK = 8,
	IOS_TITLE_LAUNCH_TITLE_KEY_INVALID = 9,
	IOS_TITLE_LAUNCH_MISSING_CONTENT_FILE = 10,
	IOS_TITLE_LAUNCH_TITLE_NOT_INSTALLED = 11,
};

static thread_local std::string sLastLaunchDetail;

const char* IOSTitleLaunch_LastErrorDetail()
{
	return sLastLaunchDetail.c_str();
}

bool IOSDlcUpdateImport_ReadTmdTitleId(const char* tmdPath, uint64* titleIdOut);
uint64 IOSDlcUpdateImport_DeriveBaseTitleId(uint64 titleId);

static void IOSTitleLaunch_AdoptDroppedKeys();

static std::atomic_bool sTitleListInitialized{false};

void IOSTitleLaunch_InitializeTitleList()
{
	if (sTitleListInitialized.exchange(true))
		return;
	cemuLog_log(LogType::Force, "iOS: title list already initialized by the core (mlc: {})", _pathToUtf8(ActiveSettings::GetMlcPath()));
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

static bool IOSTitleLaunch_IsWiiUMenuTitleId(uint64 titleId)
{
	return titleId == 0x0005001010040000ull || titleId == 0x0005001010040100ull || titleId == 0x0005001010040200ull;
}

static bool sMenuScanPathActive{false};

static void IOSTitleLaunch_ExposeRomsToTheMenu()
{
	if (sMenuScanPathActive)
		return;
	const fs::path userData = ActiveSettings::GetUserDataPath();
	if (userData.empty())
		return;
	const fs::path roms = userData.parent_path() / "Roms";
	CafeTitleList::AddScanPath(roms);
	sMenuScanPathActive = true;
	cemuLog_log(LogType::Force, "iOS: Wii U Menu - {} added to the title list scan, so the Menu can list and launch those games", _pathToUtf8(roms));
}

static void IOSTitleLaunch_DropMenuScanPath()
{
	if (!sMenuScanPathActive)
		return;
	CafeTitleList::ClearScanPaths();
	sMenuScanPathActive = false;
}

static void IOSTitleLaunch_LogMenuPreflight()
{
	std::error_code ec;
	const fs::path mlc = ActiveSettings::GetMlcPath();
	const fs::path userData = ActiveSettings::GetUserDataPath();
	if (!fs::exists(userData / "otp.bin", ec))
		cemuLog_log(LogType::Force, "iOS: Wii U Menu - otp.bin is missing (Documents/mlc/otp.bin); online features will not work, the Menu may still start");
	if (!fs::exists(userData / "seeprom.bin", ec))
		cemuLog_log(LogType::Force, "iOS: Wii U Menu - seeprom.bin is missing (Documents/mlc/seeprom.bin); online features will not work, the Menu may still start");
	if (!fs::is_directory(mlc / "sys/title/0005001b", ec))
		cemuLog_log(LogType::Force, "iOS: Wii U Menu - sys/title/0005001b (shared data: fonts, Mii) is missing from the MLC; the Menu is likely to crash without it");
	static const char* const cafeLibs[] = {"drmapp", "erreula", "nn_sl", "nsyskbd", "snd_user", "snduser2", "swkbd"};
	int missingLibs = 0;
	for (const char* lib : cafeLibs)
	{
		if (!fs::exists(userData / "cafeLibs" / (std::string(lib) + ".rpl"), ec))
			missingLibs++;
	}
	int incomplete = 0;
	for (auto& group : fs::directory_iterator(mlc / "sys/title", ec))
	{
		for (auto& title : fs::directory_iterator(group.path(), ec))
		{
			const fs::path dir = title.path();
			if (!fs::is_directory(dir / "code", ec) || !fs::is_directory(dir / "meta", ec))
				continue;
			if (fs::exists(dir / "code/app.xml", ec) && fs::exists(dir / "code/cos.xml", ec) && fs::exists(dir / "meta/meta.xml", ec))
				continue;
			if (++incomplete <= 8)
				cemuLog_log(LogType::Force, "iOS: Wii U Menu - system title {} is missing app.xml, cos.xml or meta.xml", _pathToUtf8(dir.lexically_relative(mlc)));
		}
	}
	if (incomplete > 8)
		cemuLog_log(LogType::Force, "iOS: Wii U Menu - and {} more incomplete system titles", incomplete - 8);
	if (missingLibs > 0)
		cemuLog_log(LogType::Force, "iOS: Wii U Menu - {} of {} cafeLibs .rpl files are missing from Documents/mlc/cafeLibs; the Menu will not work without them", missingLibs, (int)std::size(cafeLibs));
}

static std::vector<fs::path> IOSTitleLaunch_FindWuaCompanions(TitleId baseTitleId);

int IOSTitleLaunch_PrepareForegroundTitleById(uint64 titleId)
{
	IOSTitleLaunch_AdoptDroppedKeys();
	KeyCache_Prepare();
	IOSTitleLaunch_InitializeTitleList();

	const bool isMenu = IOSTitleLaunch_IsWiiUMenuTitleId(titleId);
	if (isMenu)
	{
		IOSTitleLaunch_LogMenuPreflight();
		IOSTitleLaunch_ExposeRomsToTheMenu();
	}
	else
		IOSTitleLaunch_DropMenuScanPath();

	const std::string high = fmt::format("{:08x}", (uint32)(titleId >> 32));
	const std::string low = fmt::format("{:08x}", (uint32)(titleId & 0xFFFFFFFFull));
	const fs::path mlc = ActiveSettings::GetMlcPath();
	std::error_code ec;
	fs::path titleDir;
	for (const char* group : {"sys/title", "usr/title"})
	{
		const fs::path candidate = mlc / group / high / low;
		if (fs::is_directory(candidate / "code", ec))
		{
			titleDir = candidate;
			break;
		}
	}
	if (titleDir.empty())
	{
		cemuLog_log(LogType::Force, "iOS: title {:016x} is not installed in the MLC ({}/{{sys,usr}}/title/{}/{})", titleId, _pathToUtf8(mlc), high, low);
		return IOS_TITLE_LAUNCH_TITLE_NOT_INSTALLED;
	}

	CafeTitleList::AddTitleFromPath(titleDir);
	IOSTitleLaunch_RescanInstalledContent(isMenu ? 30 : 10);
	CafeTitleList::AddTitleFromPath(titleDir);

	TitleId baseTitleId;
	if (!CafeTitleList::FindBaseTitleId(titleId, baseTitleId))
		return IOS_TITLE_LAUNCH_BASE_NOT_FOUND;
	for (const fs::path& wua : IOSTitleLaunch_FindWuaCompanions(baseTitleId))
		CafeTitleList::AddTitleFromPath(wua);
	cemuLog_log(LogType::Force, "iOS: launching installed title {:016x} from {}", (uint64)baseTitleId, _pathToUtf8(titleDir));
	CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitle(baseTitleId);
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

static std::vector<fs::path> IOSTitleLaunch_FindWuaCompanions(TitleId baseTitleId)
{
	std::vector<fs::path> found;
	const fs::path dir = ActiveSettings::GetMlcPath() / "wua-content" / fmt::format("{:016x}", (uint64)baseTitleId);
	std::error_code ec;
	for (const char* name : {"update.wua", "dlc.wua"})
	{
		const fs::path candidate = dir / name;
		if (fs::is_regular_file(candidate, ec))
			found.push_back(candidate);
	}
	return found;
}

static std::vector<fs::path> IOSTitleLaunch_FindCompanionTitles(const TitleInfo& base, TitleId baseTitleId)
{
	std::vector<fs::path> found;
	if (base.GetFormat() != TitleInfo::TitleDataFormat::NUS)
		return found;
	const fs::path baseFolder = base.GetPath().parent_path();
	std::vector<fs::path> searchRoots{baseFolder};
	if (baseFolder.has_parent_path() && baseFolder.parent_path() != baseFolder)
		searchRoots.push_back(baseFolder.parent_path());
	std::error_code ec;
	for (const fs::path& root : searchRoots)
	{
		for (auto& dir : fs::directory_iterator(root, ec))
		{
			if (!dir.is_directory(ec) || dir.path() == baseFolder)
				continue;
			fs::path tmdPath;
			for (auto& file : fs::directory_iterator(dir.path(), ec))
			{
				std::string name = _pathToUtf8(file.path().filename());
				for (auto& c : name)
					c = (char)std::tolower((unsigned char)c);
				if (name == "title.tmd")
				{
					tmdPath = file.path();
					break;
				}
			}
			if (tmdPath.empty())
				continue;
			uint64 tmdTitleId = 0;
			if (!IOSDlcUpdateImport_ReadTmdTitleId(_pathToUtf8(tmdPath).c_str(), &tmdTitleId))
				continue;
			const uint32 high = (uint32)(tmdTitleId >> 32);
			if (high != 0x0005000E && high != 0x0005000C)
				continue;
			if (IOSDlcUpdateImport_DeriveBaseTitleId(tmdTitleId) != (uint64)baseTitleId)
				continue;
			found.push_back(tmdPath);
		}
	}
	return found;
}

int IOSTitleLaunch_PrepareForegroundTitle(const char* pathStr)
{
	sLastLaunchDetail.clear();
	if (!pathStr || pathStr[0] == '\0')
		return IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT;
	fs::path launchPath = fs::path(pathStr);

	IOSTitleLaunch_AdoptDroppedKeys();
	KeyCache_Prepare();
	IOSTitleLaunch_InitializeTitleList();

	TitleInfo launchTitle{launchPath};
	if (launchTitle.IsValid())
	{
		CafeTitleList::AddTitleFromPath(launchPath);
		TitleId baseTitleId;
		if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
		{
			cemuLog_log(LogType::Force, "iOS: no base title found for {:016x} - an update or DLC was launched without its base game", (uint64)launchTitle.GetAppTitleId());
			return IOS_TITLE_LAUNCH_BASE_NOT_FOUND;
		}
		if (IOSTitleLaunch_IsWiiUMenuTitleId((uint64)baseTitleId))
		{
			IOSTitleLaunch_LogMenuPreflight();
			IOSTitleLaunch_ExposeRomsToTheMenu();
		}
		else
			IOSTitleLaunch_DropMenuScanPath();
		std::vector<fs::path> companionTitles = IOSTitleLaunch_FindCompanionTitles(launchTitle, baseTitleId);
		for (const fs::path& wua : IOSTitleLaunch_FindWuaCompanions(baseTitleId))
			companionTitles.push_back(wua);
		for (const fs::path& companion : companionTitles)
		{
			cemuLog_log(LogType::Force, "iOS: adding update/DLC folder {} next to the game", _pathToUtf8(companion));
			CafeTitleList::AddTitleFromPath(companion);
		}
		IOSTitleLaunch_RescanInstalledContent();
		CafeTitleList::AddTitleFromPath(launchPath);
		for (const fs::path& companion : companionTitles)
			CafeTitleList::AddTitleFromPath(companion);
		cemuLog_log(LogType::Force, "iOS: launching real title {:016x} from {}", (uint64)baseTitleId, _pathToUtf8(launchPath));
		CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitle(baseTitleId);
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

	CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
	if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
	{
		cemuLog_log(LogType::Force, "iOS: launching standalone executable {}", _pathToUtf8(launchPath));
		CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath);
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

	switch (launchTitle.GetInvalidReason())
	{
	case TitleInfo::InvalidReason::NO_DISC_KEY:
		cemuLog_log(LogType::Force, "iOS: {} is an encrypted disc image and no key in keys.txt decrypts it", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_NO_DISC_KEY;
	case TitleInfo::InvalidReason::NO_TITLE_TIK:
		cemuLog_log(LogType::Force, "iOS: {} has no usable title.tik", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_NO_TITLE_TIK;
	case TitleInfo::InvalidReason::BAD_TITLE_TMD:
		cemuLog_log(LogType::Force, "iOS: {} has a title.tmd that could not be read", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_BAD_TITLE_TMD;
	case TitleInfo::InvalidReason::BAD_TITLE_TIK:
		cemuLog_log(LogType::Force, "iOS: {} has a title.tik that could not be read", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_BAD_TITLE_TIK;
	case TitleInfo::InvalidReason::TITLE_KEY_INVALID:
		cemuLog_log(LogType::Force, "iOS: {} could not be decrypted with its ticket or any key in keys.txt", _pathToUtf8(launchPath));
		return IOS_TITLE_LAUNCH_TITLE_KEY_INVALID;
	case TitleInfo::InvalidReason::MISSING_CONTENT_FILE:
		sLastLaunchDetail = FSTVolume::GetLastMissingContentFile();
		cemuLog_log(LogType::Force, "iOS: {} is missing content file {}", _pathToUtf8(launchPath), sLastLaunchDetail);
		return IOS_TITLE_LAUNCH_MISSING_CONTENT_FILE;
	default:
		cemuLog_log(LogType::Force, "iOS: {} is not a title this build can launch (invalid reason {})", _pathToUtf8(launchPath), (int)launchTitle.GetInvalidReason());
		return IOS_TITLE_LAUNCH_UNSUPPORTED_FORMAT;
	}
}

static void IOSTitleLaunch_AdoptDroppedKeys()
{
	std::error_code ec;

	const fs::path engineKeys = ActiveSettings::GetUserDataPath("keys.txt");
	const fs::path userDataRoot = ActiveSettings::GetUserDataPath();
	if (userDataRoot.empty())
		return;
	const fs::path dropDir = userDataRoot.parent_path() / "keys";
	const fs::path dropKeys = dropDir / "keys.txt";

	fs::create_directories(dropDir, ec);
	if (ec)
	{
		cemuLog_log(LogType::Force, "iOS: could not create the keys drop folder ({})", ec.message());
		return;
	}

	std::error_code dropEc, engineEc;
	const bool haveDrop = fs::is_regular_file(dropKeys, dropEc);
	const bool haveEngine = fs::is_regular_file(engineKeys, engineEc);
	if (!haveDrop && !haveEngine)
		return;

	const fs::path& from = haveDrop ? dropKeys : engineKeys;
	const fs::path& to = haveDrop ? engineKeys : dropKeys;

	if (haveDrop && haveEngine)
	{
		std::error_code fromSizeEc, toSizeEc, fromTimeEc, toTimeEc;
		const auto fromSize = fs::file_size(from, fromSizeEc);
		const auto toSize = fs::file_size(to, toSizeEc);
		const auto fromTime = fs::last_write_time(from, fromTimeEc);
		const auto toTime = fs::last_write_time(to, toTimeEc);
		if (!fromSizeEc && !toSizeEc && !fromTimeEc && !toTimeEc
			&& fromSize == toSize && fromTime <= toTime)
			return;
	}

	ec.clear();
	fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
	if (ec)
		cemuLog_log(LogType::Force, "iOS: could not adopt keys.txt from {} ({})", _pathToUtf8(from), ec.message());
	else
		cemuLog_log(LogType::Force, "iOS: adopted keys.txt from {}", _pathToUtf8(from));
}

int IOSTitleLaunch_ReloadAndCountKeys()
{
	IOSTitleLaunch_AdoptDroppedKeys();
	KeyCache_Prepare();
	sint32 cached = 0;
	while (KeyCache_GetAES128(cached) != nullptr)
		cached++;

	sint32 inFile = 0;
	std::ifstream keys(ActiveSettings::GetUserDataPath("keys.txt"));
	for (std::string line; std::getline(keys, line);)
	{
		size_t hex = 0;
		while (hex < line.size() && std::isxdigit((unsigned char)line[hex]))
			hex++;
		if (hex >= 32)
			inFile++;
	}
	if (inFile != cached)
		cemuLog_log(LogType::Force, "iOS: keys.txt has {} key(s), {} loaded this session - relaunch EspressU to use the new ones", inFile, cached);
	else
		cemuLog_log(LogType::Force, "iOS: keys.txt checked, {} key(s) available", inFile);
	return (int)inFile;
}
