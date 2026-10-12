#include "Cafe/CafeSystem.h"
#include "Cemu/Logging/CemuLogging.h"

#include <atomic>

bool IOSBridge_RecreateRenderSurface();
void IOSBridge_TitleSwitching(uint64 titleId);

static std::atomic_bool sTitleExitedItself{false};
static std::atomic<sint32> sTitleExitStatus{0};
static std::atomic_bool sTitleSwitchFailed{false};
static std::atomic_bool sSystemAppletRequested{false};

bool _SYSIsSystemApplicationTitleId(uint64 titleId);

class IOSSystemImplementation final : public CafeSystem::SystemImplementation
{
public:
	void CafeRecreateCanvas() override
	{
		cemuLog_log(LogType::Force, "iOS: title switch - rebuilding the renderer on the existing UIKit surface");
		if (!IOSBridge_RecreateRenderSurface())
			cemuLog_log(LogType::Force, "iOS: title switch - the renderer could not be rebuilt, the next title may not draw");
	}

	void CafePPCProcessExit() override
	{
		if (CafeSystem::IsTitleSwitchInProgress())
		{
			cemuLog_log(LogType::Force, "iOS: ignoring a process exit that arrived during a title switch");
			return;
		}
		const sint32 status = CafeSystem::GetForegroundTitleReturnStatus().value_or(0);
		sTitleExitStatus.store(status);
		sTitleExitedItself.store(true);
		cemuLog_log(LogType::Force, "iOS: the title exited on its own (status {})", status);
	}

	void CafeTitleSwitching(TitleId titleId) override
	{
		IOSBridge_TitleSwitching((uint64)titleId);
	}

	void CafeTitleSwitchFailed(TitleId titleId) override
	{
		cemuLog_log(LogType::Force, "iOS: the title switch to {:016x} failed - the previous title is already shut down", (uint64)titleId);
		sTitleSwitchFailed.store(true);
		sSystemAppletRequested.store(_SYSIsSystemApplicationTitleId((uint64)titleId));
		sTitleExitStatus.store(-1);
		sTitleExitedItself.store(true);
	}
};

void IOSSystemImplementation_Install()
{
	static IOSSystemImplementation implementation;
	CafeSystem::SetImplementation(&implementation);
}

bool IOSSystemImplementation_TitleExited(int* statusOut)
{
	if (statusOut)
		*statusOut = sTitleExitStatus.load();
	return sTitleExitedItself.load();
}

bool IOSSystemImplementation_SystemAppletRequested()
{
	return sSystemAppletRequested.load();
}

bool IOSSystemImplementation_TitleSwitchFailed()
{
	return sTitleSwitchFailed.load();
}

static std::atomic<const char*> sFatalReason{nullptr};

void IOSSystemImplementation_ReportFatal(const char* reason)
{
	sFatalReason.store(reason);
	sTitleExitStatus.store(-1);
	sTitleExitedItself.store(true);
}

const char* IOSSystemImplementation_FatalReason()
{
	return sFatalReason.load();
}

void IOSSystemImplementation_ResetExit()
{
	sFatalReason.store(nullptr);
	sTitleExitedItself.store(false);
	sTitleExitStatus.store(0);
	sTitleSwitchFailed.store(false);
	sSystemAppletRequested.store(false);
}
