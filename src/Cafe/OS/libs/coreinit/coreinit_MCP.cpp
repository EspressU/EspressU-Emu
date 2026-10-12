#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/IOSU/legacy/iosu_ioctl.h"
#include "Cafe/IOSU/legacy/iosu_mcp.h"
#include "Cafe/OS/libs/coreinit/coreinit_IOS.h"
#include "Cafe/OS/libs/coreinit/coreinit_MCP.h"

#include "Cafe/OS/libs/nn_common.h"
#include "Cafe/OS/libs/nn_act/nn_act.h"
#include "config/CemuConfig.h"

#include "Cafe/CafeSystem.h"

#define mcpPrepareRequest() \
StackAllocator<iosuMcpCemuRequest_t> _buf_mcpRequest; \
StackAllocator<ioBufferVector_t> _buf_bufferVector; \
iosuMcpCemuRequest_t* mcpRequest = _buf_mcpRequest.GetPointer(); \
ioBufferVector_t* mcpBufferVector = _buf_bufferVector.GetPointer(); \
memset(mcpRequest, 0, sizeof(iosuMcpCemuRequest_t)); \
memset(mcpBufferVector, 0, sizeof(ioBufferVector_t)); \
mcpBufferVector->buffer = (uint8*)mcpRequest;

#define MCP_FAKE_HANDLE		(0x00000010)

typedef struct
{
	uint32be n0;
	uint32be n1;
	uint32be n2;
}mcpSystemVersion_t;

static_assert(sizeof(MCPTitleInfo) == 0x61, "title entry size is invalid");
static_assert(offsetof(MCPTitleInfo, appPath) == 0xC, "title entry appPath has invalid offset");
static_assert(offsetof(MCPTitleInfo, titleVersion) == 0x48, "title entry titleVersion has invalid offset");
static_assert(offsetof(MCPTitleInfo, osVersion) == 0x4A, "title entry osVersion has invalid offset");

MCPHANDLE MCP_Open()
{
	return MCP_FAKE_HANDLE;
}

void MCP_Close(MCPHANDLE mcpHandle)
{
}

void coreinitExport_MCP_Open(PPCInterpreter_t* hCPU)
{
	osLib_returnFromFunction(hCPU, MCP_Open());
}

void coreinitExport_MCP_Close(PPCInterpreter_t* hCPU)
{
	osLib_returnFromFunction(hCPU, 0);
}

sint32 MCP_GetSysProdSettings(MCPHANDLE mcpHandle, SysProdSettings* sysProdSettings)
{
	memset(sysProdSettings, 0x00, sizeof(SysProdSettings));

	sysProdSettings->gameRegion = (uint8)CafeSystem::GetForegroundTitleRegion();
	sysProdSettings->platformRegion = (uint8)CafeSystem::GetPlatformRegion();

	return 0;
}

void coreinitExport_MCP_GetSysProdSettings(PPCInterpreter_t* hCPU)
{
	sint32 result = MCP_GetSysProdSettings(hCPU->gpr[3], (SysProdSettings*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]));
	osLib_returnFromFunction(hCPU, result);
}

void coreinitExport_MCP_TitleListByAppType(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "MCP_TitleListByAppType(0x{:08x},0x{:08x},0x{:08x},0x{:08x},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6], hCPU->gpr[7]);

	ppcDefineParamU32(mcpHandle, 0);
	ppcDefineParamU32(appType, 1);
	ppcDefineParamU32BEPtr(countOutput, 2);
	ppcDefineParamStructPtr(titleList, MCPTitleInfo, 3);
	ppcDefineParamU32(titleListSize, 4);

	sint32 maxCount = titleListSize / sizeof(MCPTitleInfo);

	mcpPrepareRequest();
	mcpRequest->requestCode = IOSU_MCP_GET_TITLE_LIST_BY_APP_TYPE;
	mcpRequest->titleListRequest.titleCount = maxCount;
	mcpRequest->titleListRequest.titleList = titleList;
	mcpRequest->titleListRequest.titleListBufferSize = sizeof(MCPTitleInfo) * 1;
	mcpRequest->titleListRequest.appType = appType;
	__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

	*countOutput = mcpRequest->titleListRequest.titleCount;

	osLib_returnFromFunction(hCPU, 0);
}

void coreinitExport_MCP_TitleList(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32(mcpHandle, 0);
	ppcDefineParamU32BEPtr(countOutput, 1);
	ppcDefineParamStructPtr(titleList, MCPTitleInfo, 2);
	ppcDefineParamU32(titleListBufferSize, 3);

	mcpPrepareRequest();
	mcpRequest->requestCode = IOSU_MCP_GET_TITLE_LIST;
	mcpRequest->titleListRequest.titleCount = *countOutput;
	mcpRequest->titleListRequest.titleList = titleList;
	mcpRequest->titleListRequest.titleListBufferSize = titleListBufferSize;
	__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

	*countOutput = mcpRequest->titleListRequest.titleCount;

	cemuLog_logDebug(LogType::Force, "MCP_TitleList(...) returned {} titles", (uint32)mcpRequest->titleListRequest.titleCount);

	osLib_returnFromFunction(hCPU, mcpRequest->returnCode);
}

void coreinitExport_MCP_TitleCount(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "MCP_TitleCount(): Untested");
	ppcDefineParamU32(mcpHandle, 0);

	mcpPrepareRequest();
	mcpRequest->requestCode = IOSU_MCP_GET_TITLE_COUNT;
	mcpRequest->titleListRequest.titleCount = 0;
	__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

	osLib_returnFromFunction(hCPU, mcpRequest->titleListRequest.titleCount);
}

void coreinitExport_MCP_GetTitleInfo(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32(mcpHandle, 0);
	ppcDefineParamU64(titleId, 2);
	ppcDefineParamStructPtr(titleList, MCPTitleInfo, 4);

	cemuLog_logDebug(LogType::Force, "MCP_GetTitleInfo() called");

	mcpPrepareRequest();
	mcpRequest->requestCode = IOSU_MCP_GET_TITLE_INFO;
	mcpRequest->titleListRequest.titleCount = 1;
	mcpRequest->titleListRequest.titleList = titleList;
	mcpRequest->titleListRequest.titleListBufferSize = sizeof(MCPTitleInfo);
	mcpRequest->titleListRequest.titleId = titleId;
	__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

	if (mcpRequest->titleListRequest.titleCount == 0)
	{
		cemuLog_log(LogType::Force, "MCP_GetTitleInfo() failed to get title info");
	}

	osLib_returnFromFunction(hCPU, mcpRequest->returnCode);

}

void coreinitExport_MCP_GetTitleInfoByTitleAndDevice(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32(mcpHandle, 0);
	ppcDefineParamU64(titleId, 2);
	ppcDefineParamStr(device, 4);
	ppcDefineParamStructPtr(titleList, MCPTitleInfo, 5);

	cemuLog_logDebug(LogType::Force, "MCP_GetTitleInfoByTitleAndDevice() called (todo - device type support)");

	mcpPrepareRequest();
	mcpRequest->requestCode = IOSU_MCP_GET_TITLE_INFO;
	mcpRequest->titleListRequest.titleCount = 1;
	mcpRequest->titleListRequest.titleList = titleList;
	mcpRequest->titleListRequest.titleListBufferSize = sizeof(MCPTitleInfo);
	mcpRequest->titleListRequest.titleId = titleId;
	__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

	if (mcpRequest->titleListRequest.titleCount == 0)
	{
		cemuLog_logDebug(LogType::Force, "MCP_GetTitleInfoByTitleAndDevice() no title found");
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_MCP, 0));
		return;
	}

	osLib_returnFromFunction(hCPU, mcpRequest->returnCode);
}

namespace coreinit
{

	void export_MCP_GetSystemVersion(PPCInterpreter_t* hCPU)
	{
		cemuLog_logDebug(LogType::Force, "MCP_GetSystemVersion({},0x{:08x})", hCPU->gpr[3], hCPU->gpr[4]);
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamStructPtr(systemVersion, mcpSystemVersion_t, 1);

		systemVersion->n0 = 0x5;
		systemVersion->n1 = 0x5;
		systemVersion->n2 = 0x5;

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_Get4SecondOffStatus(PPCInterpreter_t* hCPU)
	{
		cemuLog_logDebug(LogType::Force, "MCP_Get4SecondOffStatus(...) placeholder");
		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_TitleListByDevice(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamStr(deviceName, 1);
		ppcDefineParamU32BEPtr(titleCount, 2);
		ppcDefineParamStructPtr(titleList, MCPTitleInfo, 3);

		cemu_assert_unimplemented();

		titleList[0].titleIdHigh = 0x00050000;
		titleList[0].titleIdLow = 0x11223344;
		strcpy(titleList[0].appPath, "titlePathTest");

		*titleCount = 1;

		cemuLog_logDebug(LogType::Force, "MCP_TitleListByDevice() - placeholder");

		osLib_returnFromFunction(hCPU, 0);
	}

#pragma pack(1)
	struct MCPDevice_t
	{
		 char storageName[0x8];
		 char volumeId[16];
		 char ukn[0x90 - 0x18];
		 char storagePath[0x280 - 1];
		 uint32be flags;
		uint8 ukn313[4];
		uint8 ukn317[4];
	};
#pragma pack()

	static_assert(sizeof(MCPDevice_t) == 0x31B);

	static_assert(sizeof(MCPDevice_t) == 0x31B);
	static_assert(offsetof(MCPDevice_t, storagePath) == 0x90);
	static_assert(offsetof(MCPDevice_t, flags) == 0x30F);
	static_assert(offsetof(MCPDevice_t, ukn313) == 0x313);
	static_assert(offsetof(MCPDevice_t, ukn317) == 0x317);

	void MCP_DeviceListEx(uint32 mcpHandle, uint32be* deviceCount, MCPDevice_t* deviceList, uint32 deviceListSize, bool returnFullList)
	{
		sint32 maxDeviceCount = deviceListSize / sizeof(MCPDevice_t);

		cemu_assert(maxDeviceCount >= 2);

		memset(deviceList, 0, deviceListSize);
		sint32 index = 0;

		uint32 flags = 2 | 8;
		strcpy(deviceList[index].storageName, "slc");
		strcpy(deviceList[index].volumeId, "VOLID_SLC");
		deviceList[index].flags = flags;
		strcpy(deviceList[index].storagePath, "/vol/system_slc");
		index++;
		strcpy(deviceList[index].storageName, "mlc");
		strcpy(deviceList[index].volumeId, "VOLID_MLC");
		deviceList[index].flags = flags;
		sprintf(deviceList[index].storagePath, "/vol/storage_mlc01");
		index++;

		*deviceCount = index;
	}

	void export_MCP_DeviceList(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamU32BEPtr(deviceCount, 1);
		ppcDefineParamStructPtr(deviceList, MCPDevice_t, 2);
		ppcDefineParamU32(deviceListSize, 3);

		cemuLog_logDebug(LogType::Force, "MCP_DeviceList()");

		sint32 maxDeviceCount = deviceListSize / sizeof(MCPDevice_t);

		if (maxDeviceCount < 2)
			assert_dbg();

		memset(deviceList, 0, sizeof(MCPDevice_t) * 1);
		strcpy(deviceList[0].storageName, "mlc");
		deviceList[0].flags = (0x01);
		sprintf(deviceList[0].storagePath, "/vol/storage_%s%02x", deviceList[0].storageName, (sint32)deviceList[0].flags);
		strcpy(deviceList[1].storageName, "slc");
		deviceList[1].flags = (0x01);
		sprintf(deviceList[1].storagePath, "/vol/storage_%s%02x", deviceList[1].storageName, (sint32)deviceList[1].flags);

		*deviceCount = 2;

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_FullDeviceList(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamU32BEPtr(deviceCount, 1);
		ppcDefineParamStructPtr(deviceList, MCPDevice_t, 2);
		ppcDefineParamU32(deviceListSize, 3);

		cemuLog_logDebug(LogType::Force, "MCP_FullDeviceList()");

		MCP_DeviceListEx(mcpHandle, deviceCount, deviceList, deviceListSize, true);

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_UpdateCheckContext(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamU32BEPtr(unknownParam, 1);

		cemuLog_logDebug(LogType::Force, "MCP_UpdateCheckContext() - placeholder (might be wrong)");

		*unknownParam = 1;

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_TitleListUpdateGetNext(PPCInterpreter_t* hCPU)
	{
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamMPTR(callbackMPTR, 1);

		cemuLog_logDebug(LogType::Force, "MCP_TitleListUpdateGetNext() - placeholder/unimplemented");

		osLib_returnFromFunction(hCPU, 0);
	}

	void export_MCP_GetOverlayAppInfo(PPCInterpreter_t* hCPU)
	{
		cemuLog_logDebug(LogType::Force, "MCP_GetOverlayAppInfo(...) placeholder");
		ppcDefineParamU32(mcpHandle, 0);
		ppcDefineParamU32(appType, 1);
		ppcDefineParamU32(uknR5, 2);
		ppcDefineParamStructPtr(titleList, MCPTitleInfo, 3);

		mcpPrepareRequest();
		mcpRequest->requestCode = IOSU_MCP_GET_TITLE_LIST_BY_APP_TYPE;
		mcpRequest->titleListRequest.titleCount = 1;
		mcpRequest->titleListRequest.titleList = titleList;
		mcpRequest->titleListRequest.titleListBufferSize = sizeof(MCPTitleInfo)*1;
		mcpRequest->titleListRequest.appType = appType;
		__depr__IOS_Ioctlv(IOS_DEVICE_MCP, IOSU_MCP_REQUEST_CEMU, 1, 1, mcpBufferVector);

		if (mcpRequest->titleListRequest.titleCount != 1)
			assert_dbg();

		osLib_returnFromFunction(hCPU, 0);
	}

	uint32 MCP_UpdateClearContextAsync(uint32 mcpHandle, betype<MPTR>* callbackPtr)
	{
		cemuLog_logDebug(LogType::Force, "MCP_UpdateClearContextAsync() - stubbed");
		uint32 clearContextResult = 0;
		PPCCoreCallback(*callbackPtr, clearContextResult);
		return 0;
	}

	uint32 MCP_InstallUtilGetTitleEnability(uint32 mcpHandle, uint32be* enabilityOutput, MCPTitleInfo* title)
	{
		*enabilityOutput = 1;
		return 0;
	}

	uint32 MCP_GetEcoSettings(uint32 mcpHandle, uint32be* flagCaffeineEnable, uint32be* uknFlag2, uint32be* uknFlag3)
	{
		*flagCaffeineEnable = 1;
		*uknFlag2 = 0;
		*uknFlag3 = 0;
		return 0;
	}

	uint32 MCP_RightCheckLaunchable(uint32 mcpHandle, uint64 titleId, uint32be* launchableOut)
	{
		*launchableOut = 1;
		return 0;
	}

	uint32 MCP_GetTitleId(uint32 mcpHandle, uint64be* outTitleId)
	{
		*outTitleId = CafeSystem::GetForegroundTitleId();
		return 0;
	}

	uint32 MCP_DemoLaunchGetRemainder(uint32 mcpHandle, uint64 titleId, uint32be* outRemainder)
	{
		if (outRemainder)
			*outRemainder = 99;
		return 0;
	}

	void InitializeMCP()
	{
		osLib_addFunction("coreinit", "MCP_Open", coreinitExport_MCP_Open);
		osLib_addFunction("coreinit", "MCP_Close", coreinitExport_MCP_Close);
		osLib_addFunction("coreinit", "MCP_GetSysProdSettings", coreinitExport_MCP_GetSysProdSettings);
		osLib_addFunction("coreinit", "MCP_TitleListByAppType", coreinitExport_MCP_TitleListByAppType);
		osLib_addFunction("coreinit", "MCP_TitleList", coreinitExport_MCP_TitleList);
		osLib_addFunction("coreinit", "MCP_TitleCount", coreinitExport_MCP_TitleCount);
		osLib_addFunction("coreinit", "MCP_GetTitleInfo", coreinitExport_MCP_GetTitleInfo);
		osLib_addFunction("coreinit", "MCP_GetTitleInfoByTitleAndDevice", coreinitExport_MCP_GetTitleInfoByTitleAndDevice);

		osLib_addFunction("coreinit", "MCP_TitleListByDevice", export_MCP_TitleListByDevice);
		osLib_addFunction("coreinit", "MCP_GetSystemVersion", export_MCP_GetSystemVersion);
		osLib_addFunction("coreinit", "MCP_Get4SecondOffStatus", export_MCP_Get4SecondOffStatus);

		osLib_addFunction("coreinit", "MCP_DeviceList", export_MCP_DeviceList);
		osLib_addFunction("coreinit", "MCP_FullDeviceList", export_MCP_FullDeviceList);

		osLib_addFunction("coreinit", "MCP_UpdateCheckContext", export_MCP_UpdateCheckContext);
		osLib_addFunction("coreinit", "MCP_TitleListUpdateGetNext", export_MCP_TitleListUpdateGetNext);
		osLib_addFunction("coreinit", "MCP_GetOverlayAppInfo", export_MCP_GetOverlayAppInfo);
		cafeExportRegister("coreinit", MCP_UpdateClearContextAsync, LogType::Placeholder);

		cafeExportRegister("coreinit", MCP_InstallUtilGetTitleEnability, LogType::Placeholder);
		cafeExportRegister("coreinit", MCP_RightCheckLaunchable, LogType::Placeholder);

		cafeExportRegister("coreinit", MCP_GetEcoSettings, LogType::Placeholder);

		cafeExportRegister("coreinit", MCP_GetTitleId, LogType::Placeholder);
		cafeExportRegister("coreinit", MCP_DemoLaunchGetRemainder, LogType::Placeholder);
	}

}

typedef struct
{
	char settingName[0x40];
	uint32 ukn1;
	uint32 ukn2;
	uint32 ukn3;
	uint32 ukn4_size;
	MPTR resultPtr;
}UCParamStruct_t;

static_assert(sizeof(UCParamStruct_t) == 0x54);

#if BOOST_OS_LINUX || BOOST_OS_MACOS || BOOST_OS_IOS || BOOST_OS_BSD
#define _strcmpi strcasecmp
#endif

void coreinitExport_UCReadSysConfig(PPCInterpreter_t* hCPU)
{
	UCParamStruct_t* ucParamBase = (UCParamStruct_t*)memory_getPointerFromVirtualOffset(hCPU->gpr[5]);
	uint32 ucParamCount = hCPU->gpr[4];
	for(uint32 i=0; i<ucParamCount; i++)
	{
		UCParamStruct_t* ucParam = ucParamBase+i;
		if(_strcmpi(ucParam->settingName, "cafe.cntry_reg") == 0)
		{
			uint32be simpleAddress = 0;
			nn::act::GetSimpleAddressIdEx(&simpleAddress, nn::act::ACT_SLOT_CURRENT);

			uint32 countryCode = nn::act::getCountryCodeFromSimpleAddress(simpleAddress);

			if( ucParam->resultPtr != _swapEndianU32(MPTR_NULL) )
				memory_writeU32(_swapEndianU32(ucParam->resultPtr), countryCode);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.language") == 0)
		{
			uint32 languageId = (uint32)GetConfig().console_language.GetValue();
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU32(_swapEndianU32(ucParam->resultPtr), languageId);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.initial_launch") == 0)
		{
			memory_writeU8(_swapEndianU32(ucParam->resultPtr), 2);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.eula_version") == 0)
		{
			memory_writeU32(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.eula_agree") == 0)
		{
			memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.version") == 0)
		{
			memory_writeU16(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.eco") == 0)
		{
			memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "cafe.fast_boot") == 0)
		{
			memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "parent.enable") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU32(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "nn.act.account_repaired") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "p_acct1.net_communication_on_game") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "p_acct1.int_movie") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "p_acct1.network_launcher") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "s_acct01.uuid") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL) && ucParam->ukn4_size >= 37)
			{
				StackAllocator<uint8, 16> _uuid;
				uint8* uuid = _uuid.GetPointer();
				nn::act::GetUuidEx(uuid, 1, 0);
				char tempStr[64];
				sprintf(tempStr, "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X", uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
					uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
				strcpy((char*)memory_getPointerFromVirtualOffset(_swapEndianU32(ucParam->resultPtr)), tempStr);
			}
		}
		else if (_strcmpi(ucParam->settingName, "s_acct01.nn.ec.eshop_initialized") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 1);
		}
		else if (_strcmpi(ucParam->settingName, "p_acct1.int_browser") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "caffeine.enable") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 1);
		}
		else if (_strcmpi(ucParam->settingName, "caffeine.ad_enable") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "caffeine.push_enable") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else if (_strcmpi(ucParam->settingName, "caffeine.drcled_enable") == 0)
		{
			if (ucParam->resultPtr != _swapEndianU32(MPTR_NULL))
				memory_writeU8(_swapEndianU32(ucParam->resultPtr), 0);
		}
		else
		{
			cemuLog_logDebug(LogType::Force, "Unsupported SCI value: {} Size {:08x}", ucParam->settingName, ucParam->ukn4_size);
		}
	}
	osLib_returnFromFunction(hCPU, 0);
}
