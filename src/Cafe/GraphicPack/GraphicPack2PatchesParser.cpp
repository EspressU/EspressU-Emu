#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Common/FileStream.h"
#include "util/helpers/StringParser.h"
#include "Cemu/PPCAssembler/ppcAssembler.h"
#include "Cafe/OS/RPL/rpl_structs.h"

sint32 GraphicPack2::GetLengthWithoutComment(const char* str, size_t length)
{
	sint32 index = 0;
	bool isInString = false;
	while (index < length)
	{
		const char c = str[index];
		if (c == '\"')
			isInString = !isInString;
		else if (c == '#' || c == ';')
		{
			if (!isInString)
				return index;
		}
		index++;
	}
	return (sint32)length;
}

void GraphicPack2::LogPatchesSyntaxError(sint32 lineNumber, std::string_view errorMsg)
{
	cemuLog_log(LogType::Force, "Syntax error while parsing patch for graphic pack '{}':", _pathToUtf8(this->GetRulesPath()));
	if(lineNumber >= 0)
		cemuLog_log(LogType::Force, fmt::format("Line {0}: {1}", lineNumber, errorMsg));
	else
		cemuLog_log(LogType::Force, fmt::format("{0}", errorMsg));
	list_patchGroups.clear();
}

void GraphicPack2::CancelParsingPatches()
{
	cemu_assert_debug(false);
}

void GraphicPack2::AddPatchGroup(PatchGroup* group)
{
	if (group->list_moduleMatches.empty() && !group->m_isRpxOnlyTarget)
	{
		LogPatchesSyntaxError(-1, fmt::format("Group \"{}\" has no moduleMatches definition", group->name));
		CancelParsingPatches();
		delete group;
		return;
	}
	uint32 codeCaveMaxAddr = 0;
	for (auto& itr : group->list_patches)
	{
		PatchEntryInstruction* patchData = dynamic_cast<PatchEntryInstruction*>(itr);
		if (patchData)
		{
			uint32 patchAddr = patchData->getAddr();
			if (patchAddr < 0x00100000)
			{
				codeCaveMaxAddr = std::max(codeCaveMaxAddr, patchAddr + patchData->getSize());
			}
		}

	}
	uint32 numEstimatedCodeCaveInstr = codeCaveMaxAddr / 4;
	if (group->list_patches.size() < (numEstimatedCodeCaveInstr / 8))
	{
		cemuLog_log(LogType::Force, "Graphic pack patches: Code cave for group [{}] in gfx pack \"{}\" ranges from 0 to 0x{:x} but has only few instructions. Is this intentional?", group->name, this->m_name, codeCaveMaxAddr);
	}
	group->codeCaveSize = codeCaveMaxAddr;
	list_patchGroups.emplace_back(group);
}

void GraphicPack2::ParseCemuhookPatchesTxtInternal(MemStreamReader& patchesStream)
{
	sint32 lineNumber = 0;
	PatchGroup* currentGroup = nullptr;
	while (true)
	{
		auto lineStr = patchesStream.readLine();
		lineNumber++;
		if (patchesStream.hasError())
			break;
		size_t lineLength = GetLengthWithoutComment(lineStr.data(), lineStr.size());

		StringTokenParser parser(lineStr.data(), (sint32)lineLength);

		parser.skipWhitespaces();
		if (parser.isEndOfString())
			continue;
		if (parser.compareCharacter(0, '['))
		{
			parser.skipCharacters(1);
			const char* groupNameStr = parser.getCurrentPtr();
			sint32 groupNameLength = parser.skipToCharacter(']');
			if (groupNameLength < 0)
			{

				LogPatchesSyntaxError(lineNumber, "Expected ']'");
				CancelParsingPatches();
				return;
			}
			parser.skipCharacters(1);
			parser.skipWhitespaces();
			if (!parser.isEndOfString())
			{
				LogPatchesSyntaxError(lineNumber, "Unexpected characters after ']'");
				CancelParsingPatches();
				return;
			}
			if (currentGroup)
			{
				AddPatchGroup(currentGroup);
			}
			currentGroup = new PatchGroup(this, groupNameStr, groupNameLength);
		}
		else if (parser.compareCharacter(0, '0') && parser.compareCharacterI(1, 'x'))
		{
			uint32 patchedAddress;
			if (!parser.parseU32(patchedAddress))
			{
				LogPatchesSyntaxError(lineNumber, "Malformed address");
				CancelParsingPatches();
				return;
			}
			if (parser.matchWordI("=") == false)
			{
				LogPatchesSyntaxError(lineNumber, "Expected '=' after address");
				CancelParsingPatches();
				return;
			}
			parser.skipWhitespaces();
			parser.trimWhitespaces();
			std::string instrText(parser.getCurrentPtr(), parser.getCurrentLen());
			PPCAssemblerInOut ctx{};
			ctx.virtualAddress = patchedAddress;
			if (!ppcAssembler_assembleSingleInstruction(instrText.c_str(), &ctx))
			{
				LogPatchesSyntaxError(lineNumber, fmt::format("Error in assembler: {}", ctx.errorMsg));
				CancelParsingPatches();
				return;
			}
			currentGroup->list_patches.emplace_back(new PatchEntryInstruction(lineNumber, patchedAddress, { ctx.outputData.data(), ctx.outputData.size() }, ctx.list_relocs));
		}
		else if (parser.matchWordI("moduleMatches"))
		{
			if (currentGroup == nullptr)
			{
				LogPatchesSyntaxError(lineNumber, "Specified 'ModuleMatches' outside of a group");
				CancelParsingPatches();
				return;
			}
			if (parser.matchWordI("=") == false)
			{
				LogPatchesSyntaxError(lineNumber, "Expected '=' after ModuleMatches");
				CancelParsingPatches();
				return;
			}
			while (true)
			{
				uint32 checksum = 0;
				if (parser.parseU32(checksum) == false)
				{
					LogPatchesSyntaxError(lineNumber, "Invalid value for ModuleMatches");
					CancelParsingPatches();
					return;
				}
				currentGroup->list_moduleMatches.emplace_back(checksum);
				if (parser.matchWordI(",") == false)
					break;
			}
			parser.skipWhitespaces();
			if (!parser.isEndOfString())
			{
				LogPatchesSyntaxError(lineNumber, "Unexpected character in line");
				CancelParsingPatches();
				return;
			}
			continue;
		}
		else
		{

			StringTokenParser bakParser;
			const char* symbolStr;
			sint32 symbolLen;
			parser.storeParserState(&bakParser);
			if (parser.parseSymbolName(symbolStr, symbolLen) && parser.matchWordI("="))
			{
				parser.skipWhitespaces();
				parser.trimWhitespaces();
				const char* expressionStr = parser.getCurrentPtr();
				sint32 expressionLen = parser.getCurrentLen();
				currentGroup->list_patches.emplace_back(new PatchEntryCemuhookSymbolValue(lineNumber, symbolStr, symbolLen, expressionStr, expressionLen));
				continue;
			}
			else
			{
				LogPatchesSyntaxError(lineNumber, fmt::format("Invalid syntax"));
				CancelParsingPatches();
				return;
			}
		}
	}
	if (currentGroup)
		AddPatchGroup(currentGroup);
}

static inline uint32 INVALID_ORIGIN = 0xFFFFFFFF;

bool GraphicPack2::ParseCemuPatchesTxtInternal(MemStreamReader& patchesStream)
{
	sint32 lineNumber = 0;
	PatchGroup* currentGroup = nullptr;

	struct
	{
		void reset()
		{
			currentOrigin = INVALID_ORIGIN;
			codeCaveOrigin = 0;
		}

		void setOrigin(uint32 origin)
		{
			currentOrigin = origin;
		}

		void setOriginCodeCave()
		{
			currentOrigin = codeCaveOrigin;
		}

		bool isValidOrigin()
		{
			return currentOrigin != INVALID_ORIGIN;
		}

		void incrementOrigin(uint32 size)
		{
			currentOrigin += size;
			if (currentOrigin <= 32 * 1024 * 1024)
				codeCaveOrigin = std::max(codeCaveOrigin, currentOrigin);
		}

		uint32 currentOrigin{};
		uint32 codeCaveOrigin{};
	}originInfo;
	std::vector<PatchEntryLabel*> scheduledLabels;

	auto flushLabels = [&]()
	{
		for (auto& itr : scheduledLabels)
		{
			itr->setAssignedVA(originInfo.currentOrigin);
			currentGroup->list_patches.emplace_back(itr);
		}
		scheduledLabels.clear();
	};

	while (true)
	{
		size_t lineLength;
		auto lineStr = patchesStream.readLine();
		lineNumber++;
		if (patchesStream.hasError())
			break;
		lineLength = GetLengthWithoutComment(lineStr.data(), lineStr.size());

		StringTokenParser parser(lineStr.data(), (sint32)lineLength);

		parser.skipWhitespaces();
		if (parser.isEndOfString())
			continue;
		if (parser.compareCharacter(0, '['))
		{
			parser.skipCharacters(1);
			const char* groupNameStr = parser.getCurrentPtr();
			sint32 groupNameLength = parser.skipToCharacter(']');
			if (groupNameLength < 0)
			{
				LogPatchesSyntaxError(lineNumber, "Expected ']'");
				CancelParsingPatches();
				return false;
			}
			parser.skipCharacters(1);
			parser.skipWhitespaces();
			if (!parser.isEndOfString())
			{
				LogPatchesSyntaxError(lineNumber, "Unexpected characters after ']'");
				CancelParsingPatches();
				return false;
			}
			if (currentGroup)
			{
				flushLabels();
				AddPatchGroup(currentGroup);
			}
			currentGroup = new PatchGroup(this, groupNameStr, groupNameLength);
			originInfo.reset();
			continue;
		}
		else if (parser.matchWordI("moduleMatches"))
		{
			if (currentGroup == nullptr)
			{
				LogPatchesSyntaxError(lineNumber, "Specified 'ModuleMatches' outside of a group");
				CancelParsingPatches();
				return false;
			}
			if (parser.matchWordI("=") == false)
			{
				LogPatchesSyntaxError(lineNumber, "Expected '=' after ModuleMatches");
				CancelParsingPatches();
				return false;
			}
			while (true)
			{
                if (parser.matchWordI("rpx"))
                {
                   	currentGroup->m_isRpxOnlyTarget = true;
                   	break;
                }

				uint32 checksum = 0;
				if (parser.parseU32(checksum) == false)
				{
					LogPatchesSyntaxError(lineNumber, "Invalid value for ModuleMatches");
					CancelParsingPatches();
					return false;
				}
				currentGroup->list_moduleMatches.emplace_back(checksum);
				if (parser.matchWordI(",") == false)
					break;
			}
			parser.skipWhitespaces();
			if (!parser.isEndOfString())
			{
				LogPatchesSyntaxError(lineNumber, "Unexpected character");
				CancelParsingPatches();
				return false;
			}
			continue;
		}

		uint32 overwriteOrigin = INVALID_ORIGIN;
		if (parser.compareCharacter(0, '0') && parser.compareCharacterI(1, 'x'))
		{
			uint32 patchedAddress;
			if (!parser.parseU32(patchedAddress))
			{
				LogPatchesSyntaxError(lineNumber, "Malformed address");
				CancelParsingPatches();
				return false;
			}
			if (parser.matchWordI("=") == false)
			{
				LogPatchesSyntaxError(lineNumber, "Expected '=' after address");
				CancelParsingPatches();
				return false;
			}
			parser.skipWhitespaces();
			parser.trimWhitespaces();
			overwriteOrigin = patchedAddress;
		}
		if (parser.matchWordI(".origin"))
		{
			if (overwriteOrigin != INVALID_ORIGIN)
			{
				LogPatchesSyntaxError(lineNumber, fmt::format(".origin directive must appear alone without <address> = prefix."));
				CancelParsingPatches();
				return false;
			}
			if (!parser.matchWordI("="))
			{
				LogPatchesSyntaxError(lineNumber, fmt::format("Missing '=' after .origin"));
				CancelParsingPatches();
				return false;
			}
			uint32 originAddress;
			if (parser.matchWordI("codecave"))
			{
				originInfo.setOriginCodeCave();
			}
			else if(parser.parseU32(originAddress))
			{
				originInfo.setOrigin(originAddress);
			}
			else
			{
				LogPatchesSyntaxError(lineNumber, fmt::format("\'.origin =\' must be followed by the keyword codecave or a valid address"));
				CancelParsingPatches();
				return false;
			}
			continue;
		}
		else if (parser.matchWordI(".callback"))
		{
		    if (parser.matchWordI("entry"))
    		{
                const char* symbolStr;
    			sint32 symbolLen;
    		    if (parser.parseSymbolName(symbolStr, symbolLen))
    		    {
    				currentGroup->list_callbacks.push_back(std::make_pair(std::string(symbolStr, static_cast<size_t>(symbolLen)), GPCallbackType::Entry));
    				continue;
    		    }
    		    else
    		    {
                    LogPatchesSyntaxError(lineNumber, "'.callback' must reference a symbol after the type");
                    CancelParsingPatches();
                    return false;
    		    }
    		}
		    else
		    {
				LogPatchesSyntaxError(lineNumber, "Unrecognized type for '.callback'");
				CancelParsingPatches();
				return false;
			}
		}

		StringTokenParser bakParser;
		const char* symbolStr;
		sint32 symbolLen;
		parser.storeParserState(&bakParser);

		if (parser.parseSymbolName(symbolStr, symbolLen) && parser.matchWordI(":"))
		{
			parser.skipWhitespaces();
			if (!parser.isEndOfString())
			{
				LogPatchesSyntaxError(lineNumber, fmt::format("Unexpected characters after label"));
				CancelParsingPatches();
				return false;
			}
			uint32 labelAddress;
			if (overwriteOrigin != INVALID_ORIGIN)
				labelAddress = overwriteOrigin;
			else
			{
				if (!originInfo.isValidOrigin())
				{
					LogPatchesSyntaxError(lineNumber, fmt::format("Defined label has no address assigned or there is no active .origin"));
					CancelParsingPatches();
					return false;
				}
				labelAddress = originInfo.currentOrigin;
			}
			if (overwriteOrigin == INVALID_ORIGIN)
			{
				scheduledLabels.emplace_back(new PatchEntryLabel(lineNumber, symbolStr, symbolLen));
			}
			else
			{
				PatchEntryLabel* patchLabel = new PatchEntryLabel(lineNumber, symbolStr, symbolLen);
				patchLabel->setAssignedVA(labelAddress);
				currentGroup->list_patches.emplace_back(patchLabel);
			}
			continue;
		}
		parser.restoreParserState(&bakParser);
		if (parser.parseSymbolName(symbolStr, symbolLen) && parser.matchWordI("="))
		{
			parser.skipWhitespaces();
			parser.trimWhitespaces();
			const char* expressionStr = parser.getCurrentPtr();
			sint32 expressionLen = parser.getCurrentLen();
			currentGroup->list_patches.emplace_back(new PatchEntryVariableValue(lineNumber, symbolStr, symbolLen, PATCHVARTYPE::UINT, expressionStr, expressionLen));
			continue;
		}
		parser.restoreParserState(&bakParser);
		std::string instrText(parser.getCurrentPtr(), parser.getCurrentLen());
		PPCAssemblerInOut ctx{};
		ctx.forceNoAlignment = overwriteOrigin != INVALID_ORIGIN;
		if (overwriteOrigin != INVALID_ORIGIN)
			ctx.virtualAddress = overwriteOrigin;
		else if(originInfo.isValidOrigin())
			ctx.virtualAddress = originInfo.currentOrigin;
		else
		{
			LogPatchesSyntaxError(lineNumber, fmt::format("Trying to assemble line but no address specified. (Declare .origin or prefix line with <address> = )"));
			CancelParsingPatches();
			return false;
		}
		if (!ppcAssembler_assembleSingleInstruction(instrText.c_str(), &ctx))
		{
			LogPatchesSyntaxError(lineNumber, fmt::format("Error in assembler: {}", ctx.errorMsg));
			CancelParsingPatches();
			return false;
		}
		cemu_assert_debug(ctx.alignmentRequirement != 0);
		if (overwriteOrigin == INVALID_ORIGIN)
		{
			originInfo.incrementOrigin((sint32)ctx.alignmentPaddingSize);
			originInfo.incrementOrigin((sint32)ctx.outputData.size());
		}
		for (auto& itr : scheduledLabels)
		{
			itr->setAssignedVA(ctx.virtualAddressAligned);
			currentGroup->list_patches.emplace_back(itr);
		}
		scheduledLabels.clear();
		currentGroup->list_patches.emplace_back(new PatchEntryInstruction(lineNumber, ctx.virtualAddressAligned, { ctx.outputData.data(), ctx.outputData.size() }, ctx.list_relocs));
	}
	flushLabels();

	if (currentGroup)
		AddPatchGroup(currentGroup);
	return true;
}
