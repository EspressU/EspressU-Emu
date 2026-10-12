#pragma once
#include "Cafe/OS/RPL/rpl.h"
#include "Cafe/TitleList/TitleId.h"

enum class CosCapabilityBits : uint64;
enum class CosCapabilityGroup : uint32;
enum class CafeConsoleRegion;

namespace CafeSystem
{
	class SystemImplementation
	{
	public:
		virtual void CafeRecreateCanvas() = 0;
		virtual void CafePPCProcessExit() = 0;
		virtual void CafeTitleSwitchFailed(TitleId titleId) { CafePPCProcessExit(); }
		virtual void CafeTitleSwitching(TitleId titleId) {}
	};

	enum class PREPARE_STATUS_CODE
	{
		SUCCESS,
		INVALID_RPX,
		UNABLE_TO_MOUNT,
	};

	void Initialize();
	void SetImplementation(SystemImplementation* impl);
    void Shutdown();

	PREPARE_STATUS_CODE PrepareForegroundTitle(TitleId titleId);
	PREPARE_STATUS_CODE PrepareForegroundTitleFromStandaloneRPX(const fs::path& path);
	void LaunchForegroundTitle();
	bool IsTitleRunning();

	bool GetOverrideArgStr(std::vector<std::string>& args);
	void SetOverrideArgs(std::span<std::string> args);
	void UnsetOverrideArgs();

	TitleId GetForegroundTitleId();
	uint16 GetForegroundTitleVersion();
	uint32 GetForegroundTitleSDKVersion();
	CafeConsoleRegion GetForegroundTitleRegion();
	CafeConsoleRegion GetPlatformRegion();
	std::string GetForegroundTitleName();
	std::string GetForegroundTitleArgStr();
	uint32 GetForegroundTitleOlvAccesskey();
	CosCapabilityBits GetForegroundTitleCosCapabilities(CosCapabilityGroup group);
	std::optional<sint32> GetForegroundTitleReturnStatus();

	void ShutdownTitle();
	void AbortPreparedTitle();

	bool CleanSlateHasDangerousLeftover();
	std::vector<std::string> GetCleanSlateLeftovers();

	std::string GetMlcStoragePath(TitleId titleId);
	void MlcStorageMountAllTitles();

	std::string GetInternalVirtualCodeFolder();

	uint32 GetRPXHashBase();
	uint32 GetRPXHashUpdated();

	void RequestRecreateCanvas();
	void NotifyPPCProcessExit(sint32 status);

	void SetTitleSwitchInProgress(bool inProgress);
	bool IsTitleSwitchInProgress();
	void NotifyTitleSwitchFailed(TitleId titleId);
	void NotifyTitleSwitching(TitleId titleId);

};

extern RPLModule* applicationRPX;

extern std::atomic_bool g_isGPUInitFinished;
