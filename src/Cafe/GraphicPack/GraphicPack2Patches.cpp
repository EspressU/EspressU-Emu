#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Common/FileStream.h"
#include "WindowSystem.h"
#include "util/helpers/StringParser.h"
#include "Cemu/PPCAssembler/ppcAssembler.h"
#include "Cafe/OS/RPL/rpl_structs.h"
#include "boost/algorithm/string.hpp"

void PatchErrorHandler::printError(class PatchGroup* patchGroup, sint32 lineNumber, std::string_view errorMsg)
{
	if (m_anyErrorTriggered == false)
	{
		cemu_assert(m_gp);
		if (m_stage == STAGE::PARSER)
			cemuLog_writeLineToLog(fmt::format("An error occurred while trying to parse the patches for graphic pack \'{}\'", m_gp->GetName()), true, true);
		else if (m_stage == STAGE::APPLY)
			cemuLog_writeLineToLog(fmt::format("An error occurred while trying to apply the patches for graphic pack \'{}\'", m_gp->GetName()), true, true);
	}

	std::string msg;
	if (patchGroup == nullptr && lineNumber >= 0)
		msg.append(fmt::format("[Line {}] ", lineNumber));
	else if (patchGroup && lineNumber >= 0)
		msg.append(fmt::format("[{}, Line {}] ", patchGroup->getName(), lineNumber));
	else if (patchGroup && lineNumber < 0)
		msg.append(fmt::format("[{}] ", patchGroup->getName()));

	msg.append(errorMsg);

	cemuLog_writeLineToLog(msg, true, true);
	m_anyErrorTriggered = true;

	if (cemuLog_isLoggingEnabled(LogType::Patches))
		errorMessages.emplace_back(msg);
}

void PatchErrorHandler::showStageErrorMessageBox()
{
	std::string errorMsg;
	if (m_gp)
	{
		if (m_stage == STAGE::PARSER)
			errorMsg.assign(_tr("Failed to load patches for graphic pack \'{}\'", m_gp->GetName()));
		else
			errorMsg.assign(_tr("Failed to apply patches for graphic pack \'{}\'", m_gp->GetName()));
	}
	else
	{
		cemu_assert_debug(false);
	}
	if (cemuLog_isLoggingEnabled(LogType::Patches))
	{
		errorMsg.append("\n \n")
			.append(_tr("Details:"))
			.append("\n");
		for (auto& itr : errorMessages)
		{
			errorMsg.append(itr);
			errorMsg.append("\n");
		}
	}

	WindowSystem::ShowErrorDialog(errorMsg, _tr("Graphic pack error"), WindowSystem::ErrorCategory::GRAPHIC_PACKS);
}

bool GraphicPack2::LoadCemuPatches()
{
	bool foundPatches = false;
	fs::path path(m_rulesPath);
	path.remove_filename();
	for (auto& p : fs::directory_iterator(path))
	{
		auto& path = p.path();
		if (fs::is_regular_file(p.status()) && path.has_filename())
		{
			std::string filename = _pathToUtf8(path.filename());
			if (boost::istarts_with(filename, "patch_") && boost::iends_with(filename, ".asm"))
			{
				FileStream* patchFile = FileStream::openFile2(path);
				if (patchFile)
				{
					std::vector<uint8> fileData;
					patchFile->extract(fileData);
					delete patchFile;
					MemStreamReader patchesStream(fileData.data(), (sint32)fileData.size());
					if (!ParseCemuPatchesTxtInternal(patchesStream))
					{
						cemuLog_log(LogType::Force, "Error while processing \"{}\". No patches for this graphic pack will be applied.", _pathToUtf8(path));
						cemu_assert_debug(list_patchGroups.empty());
						return true;
					}
				}
				else
				{
					cemuLog_log(LogType::Force, "Unable to load patch file \"{}\"", _pathToUtf8(path));
				}
				foundPatches = true;
			}
		}
	}
	return foundPatches;
}

void GraphicPack2::LoadPatchFiles()
{
	if (LoadCemuPatches())
		return;
	fs::path path(m_rulesPath);
	path.remove_filename();
	path.append("patches.txt");
	FileStream* patchFile = FileStream::openFile2(path);
	if (patchFile == nullptr)
		return;
	std::vector<uint8> fileData;
	patchFile->extract(fileData);
	delete patchFile;
	cemu_assert_debug(list_patchGroups.empty());
	MemStreamReader patchesStream(fileData.data(), (sint32)fileData.size());
	ParseCemuhookPatchesTxtInternal(patchesStream);
}

void GraphicPack2::EnablePatches()
{
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	for (auto& itr : list_modules)
		ApplyPatchesForModule(itr);
}

void GraphicPack2::UnloadPatches()
{
	if (list_patchGroups.empty())
		return;
	std::lock_guard<std::recursive_mutex> lock(mtx_patches);
	for (auto& itr : list_modules)
		RevertPatchesForModule(itr);
	for (auto itr : list_patchGroups)
		delete itr;
	list_patchGroups.clear();
}

bool GraphicPack2::HasPatches()
{
	return !list_patchGroups.empty();
}

const std::vector<PatchGroup*>& GraphicPack2::GetPatchGroups() {
	return list_patchGroups;
}

void GraphicPack2::ApplyPatchesForModule(const RPLModule* rpl)
{
	if (list_patchGroups.empty())
		return;
	std::vector<PatchGroup*> list_groups;
	for (auto itr : list_patchGroups)
	{
		if (itr->matchesCRC(rpl->patchCRC) || (itr->m_isRpxOnlyTarget && rpl->IsRPX()))
			list_groups.emplace_back(itr);
	}
	if (!list_groups.empty())
		ApplyPatchGroups(list_groups, rpl);
}

void GraphicPack2::RevertPatchesForModule(const RPLModule* rpl)
{
	if (list_patchGroups.empty())
		return;
	std::vector<PatchGroup*> list_groups;
	for (auto itr : list_patchGroups)
	{
		if (itr->matchesCRC(rpl->patchCRC) || (itr->m_isRpxOnlyTarget && rpl->IsRPX()))
			list_groups.emplace_back(itr);
	}
	if (!list_groups.empty())
		UndoPatchGroups(list_groups, rpl);
}

std::recursive_mutex GraphicPack2::mtx_patches;
std::vector<const RPLModule*> GraphicPack2::list_modules;
