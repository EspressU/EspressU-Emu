#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/HW/Espresso/PPCCallback.h"
#include "Cafe/OS/libs/nn_common.h"
#include "nn_nfp.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "Common/FileStream.h"
#include "Cafe/CafeSystem.h"

std::recursive_mutex g_nfpMutex;

void nnNfpLock()
{
	g_nfpMutex.lock();
}

bool nnNfpTryLock()
{
	return g_nfpMutex.try_lock();
}

void nnNfpUnlock()
{
	g_nfpMutex.unlock();
}

struct AmiiboInternal
{
	 uint16 lockBytes;
	 uint16 staticLock;
	 uint32 cc;
	 uint8 dataHMAC[32];
	 uint8 ukn_A5;
	 uint8 writeCounterHigh;
	 uint8 writeCounterLow;
	 uint8 unk02B;
	struct
	{
		 uint8 flags;
		 uint8 countryCode;
		 uint16be crcWriteCounter;
		 uint16be date1;
		 uint16be date2;
		 uint32be crc;
		 uint16be nickname[10];
		 uint8 mii[0x60];
		 uint32be appDataTitleIdHigh;
		 uint32be appDataTitleIdLow;
		 uint16be appWriteCounter;
		 uint16be appDataIdHigh;
		 uint16be appDataIdLow;
		 uint16be ukn0BA;
		 uint32be ukn0BC;
		 uint32be ukn0C0;
		 uint32be ukn0C4;
		 uint32be ukn0C8;
		 uint32be ukn0CC;
		 uint32be ukn0D0;
		 uint32be ukn0D4;
		 uint32be ukn0D8;
		uint32 getAppDataAppId()
		{
			uint32 appId = (uint32)appDataIdHigh << 16;
			appId |= (uint32)appDataIdLow;
			return appId;
		}

		void setAppDataAppId(uint32 appId)
		{
			appDataIdHigh = (uint16)(appId >> 16);
			appDataIdLow = (uint16)(appId & 0xFFFF);
		}
	}amiiboSettings;
	 uint8 applicationData[0xD8];
	 uint8 tagHMAC[32];
	 uint8 ntagSerial[7];
	 uint8 nintendoId;
	struct
	{
		 uint8 gameAndCharacterId[2];
		 uint8 characterVariation;
		 uint8 amiiboFigureType;
		 uint8 amiiboModelNumber[2];
		 uint8 amiiboSeries;
		 uint8 ukn_02;
		 uint8 ukn5C[4];
	}amiiboIdentificationBlock;
	 uint8 keygenSalt[32];
};

static_assert(sizeof(AmiiboInternal) == 0x208);
static_assert(offsetof(AmiiboInternal, dataHMAC) == 0x8);
static_assert(offsetof(AmiiboInternal, amiiboSettings.appDataIdHigh) == 0xB6);
static_assert(offsetof(AmiiboInternal, amiiboSettings.ukn0D8) == 0xD8);
static_assert(offsetof(AmiiboInternal, tagHMAC) == 0x1B4);

union AmiiboRawNFCData
{
	struct
	{
		uint8 page0[4];
		uint8 page1[4];
		uint8 page2[4];
		uint8 page3[4];
		uint8 page4[4];
		uint8 page5[4];
		uint8 page6[4];
		uint8 page7[4];
		uint8 page8[4];
		uint8 page9[4];
		uint8 page10[4];
		uint8 page11[4];
		uint8 page12[4];
		uint8 page13[4];
		uint8 page14[4];
		uint8 page15[4];
		uint8 page16[4];
	};
	struct
	{
		uint8 rawByte[16 * 4];
	};
	struct
	{
		 uint8 ntagSerial[7];
		 uint8 nintendoId;
		 uint8 lockBytes[2];
		 uint8 staticLock[2];
		 uint8 cc[4];
		 uint8 ukn_A5;
		 uint8 writeCounter[2];
		 uint8 unk013;
		 uint8 encryptedSettings[32];
		 uint8 tagHMAC[32];
		struct
		{
			 uint8 gameAndCharacterId[2];
			 uint8 characterVariation;
			 uint8 amiiboFigureType;
			 uint8 amiiboModelNumber[2];
			 uint8 amiiboSeries;
			 uint8 ukn_02;
			 uint8 ukn5C[4];
		}amiiboIdentificationBlock;
		 uint8 keygenSalt[32];
		 uint8 dataHMAC[32];
		 uint8 encryptedMii[0x60];
		 uint8 encryptedTitleId[8];
		 uint8 encryptedApplicationWriteCounter[2];
		 uint8 encryptedApplicationAreaId[4];
		 uint8 ukn10E[2];
		 uint8 unk110[32];
		 uint8 encryptedApplicationArea[0xD8];
		 uint8 dynamicLock[4];
		 uint8 cfg0[4];
		 uint8 cfg1[4];
	};
};

static_assert(sizeof(AmiiboRawNFCData) == 532);

struct AmiiboProcessedData
{
	uint8 uidLength;
	uint8 uid[7];
};

struct
{
	bool nfpIsInitialized;
	MPTR activateEvent;
	MPTR deactivateEvent;
	bool isDetecting;
	bool isMounted;
	bool isReadOnly;
	bool hasOpenApplicationArea;
	bool hasActiveAmiibo;
	fs::path amiiboPath;
	bool hasInvalidHMAC;
	uint32 amiiboTouchTime;
	AmiiboRawNFCData amiiboNFCData;
	AmiiboInternal amiiboInternal;
	AmiiboProcessedData amiiboProcessedData;
}nfp_data = { 0 };

bool nnNfp_writeCurrentAmiibo();

#include "AmiiboCrypto.h"

void nnNfpExport_SetActivateEvent(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(osEvent, coreinit::OSEvent, 0);
	ppcDefineParamMPTR(osEventMPTR, 0);

	debug_printf("nn_nfp.SetActivateEvent(0x%08x)\n", osEventMPTR);

	coreinit::OSInitEvent(osEvent, coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);

	nnNfpLock();
	nfp_data.activateEvent = osEventMPTR;
	nnNfpUnlock();

	osLib_returnFromFunction(hCPU, 0);
}

void nnNfpExport_SetDeactivateEvent(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(osEvent, coreinit::OSEvent, 0);
	ppcDefineParamMPTR(osEventMPTR, 0);

	cemuLog_log(LogType::NN_NFP, "SetDeactivateEvent(0x{:08x})", osEventMPTR);

	coreinit::OSInitEvent(osEvent, coreinit::OSEvent::EVENT_STATE::STATE_NOT_SIGNALED, coreinit::OSEvent::EVENT_MODE::MODE_AUTO);

	nnNfpLock();
	nfp_data.deactivateEvent = osEventMPTR;
	nnNfpUnlock();

	osLib_returnFromFunction(hCPU, 0);
}

void nnNfpExport_Initialize(PPCInterpreter_t* hCPU)
{
	debug_printf("Nfp Initialize()\n");
	nfp_data.nfpIsInitialized = true;
	nfp_data.isDetecting = false;
	nfp_data.hasActiveAmiibo = false;
	nfp_data.hasOpenApplicationArea = false;
	nfp_data.activateEvent = MPTR_NULL;
	nfp_data.deactivateEvent = MPTR_NULL;
	osLib_returnFromFunction(hCPU, 0);
}

void nnNfpExport_StartDetection(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "StartDetection()");
	nnNfpLock();
	nfp_data.isDetecting = true;
	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_StopDetection(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "StopDetection()");
	nnNfpLock();
	nfp_data.isDetecting = false;
	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

#define NFP_TAG_MAX_LENGTH	(10)

typedef struct
{
	 uint8 uidLength;
	 uint8 uid[0xA];
	 uint8 unused0B[0x15];
	 uint32 ukn20[4];
	uint32 ukn30[4];
	uint32 ukn40[4];
	uint32 ukn50;
}nfpTagInfo_t;

static_assert(sizeof(nfpTagInfo_t) == 0x54, "nfpTagInfo_t has invalid size");

void nnNfpExport_GetTagInfo(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "GetTagInfo(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(tagInfo, nfpTagInfo_t, 0);

	nnNfpLock();
	if (nfp_data.hasActiveAmiibo == false)
	{
		nnNfpUnlock();
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	memset(tagInfo, 0x00, sizeof(nfpTagInfo_t));

	memcpy(tagInfo->uid, nfp_data.amiiboProcessedData.uid, nfp_data.amiiboProcessedData.uidLength);
	tagInfo->uidLength = (uint8)nfp_data.amiiboProcessedData.uidLength;

	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_Mount(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "Mount()");
	nnNfpLock();
	if (nfp_data.hasActiveAmiibo == false)
	{
		nnNfpUnlock();
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	nfp_data.isMounted = true;
	nfp_data.isReadOnly = false;
	nfp_data.isDetecting = false;
	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_Unmount(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "Unmount()");
	nfp_data.hasOpenApplicationArea = false;
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_MountRom(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "MountRom()");
	nnNfpLock();
	if (nfp_data.hasActiveAmiibo == false)
	{
		nnNfpUnlock();
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	nfp_data.isMounted = true;
	nfp_data.isReadOnly = true;
	nfp_data.isDetecting = false;
	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

namespace nn::nfp
{
	struct RomInfo
	{
		 uint8 characterId[3];
		 uint8 amiiboSeries;
		 uint16be number;
		 uint8 nfpType;
		 uint8 unused[0x2F];
	};

	static_assert(offsetof(RomInfo, amiiboSeries) == 0x3);
	static_assert(offsetof(RomInfo, number) == 0x4);
	static_assert(offsetof(RomInfo, nfpType) == 0x6);
	static_assert(sizeof(RomInfo) == 0x36);

	using ReadOnlyInfo = RomInfo;

	void GetRomInfo(RomInfo* romInfo)
	{
		cemu_assert_debug(nfp_data.hasActiveAmiibo);
		memset(romInfo, 0x00, sizeof(RomInfo));
		romInfo->characterId[0] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.gameAndCharacterId[0];
		romInfo->characterId[1] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.gameAndCharacterId[1];
		romInfo->characterId[2] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.characterVariation;
		romInfo->amiiboSeries = nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboSeries;
		romInfo->number = *(uint16be*)nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboModelNumber;
		romInfo->nfpType = nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboFigureType;
		memset(romInfo->unused, 0x00, sizeof(romInfo->unused));
	}

	nnResult GetNfpRomInfo(RomInfo* romInfo)
	{
		nnNfpLock();
		if (nfp_data.hasActiveAmiibo == false)
		{
			nnNfpUnlock();
			return BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0);
		}
		GetRomInfo(romInfo);
		nnNfpUnlock();
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0);
	}

	nnResult GetNfpReadOnlyInfo(ReadOnlyInfo* readOnlyInfo)
	{
		nnNfpLock();
		if (nfp_data.hasActiveAmiibo == false)
		{
			nnNfpUnlock();
			return BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0);
		}
		GetRomInfo(readOnlyInfo);
		nnNfpUnlock();
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0);
	}
};

typedef struct
{
	uint16be year;
	uint8 month;
	uint8 day;
}nfpDate_t;

typedef struct
{
	 nfpDate_t date;
	 uint8 writeCount[2];
	 uint8 characterId[3];
	 uint8 amiiboSeries;
	 uint16be number;
	 uint8 nfpType;
	 uint8 nfpVersion;
	 uint16be applicationAreaSize;
	 uint8 unused[0x30];
}nfpCommonData_t;

static_assert(sizeof(nfpCommonData_t) == 0x40, "nfpCommonData_t has invalid size");
static_assert(offsetof(nfpCommonData_t, writeCount) == 0x4, "nfpCommonData.writeCount has invalid offset");
static_assert(offsetof(nfpCommonData_t, amiiboSeries) == 0x9, "nfpCommonData.seriesId has invalid offset");
static_assert(offsetof(nfpCommonData_t, nfpType) == 0xC, "nfpCommonData.nfpType has invalid offset");
static_assert(offsetof(nfpCommonData_t, applicationAreaSize) == 0xE, "nfpCommonData.applicationAreaSize has invalid offset");

void nnNfpExport_GetNfpCommonInfo(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "GetNfpCommonInfo(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(commonInfo, nfpCommonData_t, 0);

	nnNfpLock();
	if (nfp_data.hasActiveAmiibo == false)
	{
		nnNfpUnlock();
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	if (sizeof(nfpCommonData_t) != 0x40 )
		assert_dbg();
	memset(commonInfo, 0x00, sizeof(nfpCommonData_t));

	cemuLog_logDebug(LogType::Force, "GetNfpCommonInfo(0x{:08x})");

	commonInfo->writeCount[0] = nfp_data.amiiboNFCData.writeCounter[0];
	commonInfo->writeCount[1] = nfp_data.amiiboNFCData.writeCounter[1];

	commonInfo->characterId[0] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.gameAndCharacterId[0];
	commonInfo->characterId[1] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.gameAndCharacterId[1];
	commonInfo->characterId[2] = nfp_data.amiiboNFCData.amiiboIdentificationBlock.characterVariation;

	commonInfo->number = *(uint16be*)nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboModelNumber;
	commonInfo->amiiboSeries = nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboSeries;
	commonInfo->nfpType = nfp_data.amiiboNFCData.amiiboIdentificationBlock.amiiboFigureType;

	commonInfo->applicationAreaSize = sizeof(nfp_data.amiiboInternal.applicationData);

	nnNfpUnlock();
	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

typedef struct
{
	 uint8 ownerMii[0x60];
	 uint8 nickname[0x14];
	 uint16be ukn74;
	 uint8 ukn76;
	 uint8 _padding77;
	 nfpDate_t registerDate;
	 uint8 ukn7C[0x2C];
}nfpRegisterInfo_t;

typedef struct
{
	 uint8 ownerMii[0x60];
	 uint8 nickname[0x14];
}nfpRegisterInfoSet_t;

void nnNfpExport_GetNfpRegisterInfo(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "GetNfpRegisterInfo(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(registerInfo, nfpRegisterInfo_t, 0);

	if(!registerInfo)
	{
		osLib_returnFromFunction(hCPU, 0xC1B03780);
		return;
	}

	memset(registerInfo, 0, sizeof(nfpRegisterInfo_t));
	if ((nfp_data.amiiboInternal.amiiboSettings.flags & 0x10) != 0)
	{
		memcpy(registerInfo->ownerMii, nfp_data.amiiboInternal.amiiboSettings.mii, sizeof(nfp_data.amiiboInternal.amiiboSettings.mii));
		memcpy(registerInfo->nickname, nfp_data.amiiboInternal.amiiboSettings.nickname, sizeof(nfp_data.amiiboInternal.amiiboSettings.nickname));
	}

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_InitializeRegisterInfoSet(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "InitializeRegisterInfoSet(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(registerInfoSet, nfpRegisterInfoSet_t, 0);

	memset(registerInfoSet, 0, sizeof(nfpRegisterInfoSet_t));

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_SetNfpRegisterInfo(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "SetNfpRegisterInfo(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(registerInfoSet, nfpRegisterInfoSet_t, 0);

	memcpy(nfp_data.amiiboInternal.amiiboSettings.mii, registerInfoSet->ownerMii, sizeof(nfp_data.amiiboInternal.amiiboSettings.mii));
	memcpy(nfp_data.amiiboInternal.amiiboSettings.nickname, registerInfoSet->nickname, sizeof(nfp_data.amiiboInternal.amiiboSettings.nickname));
	nfp_data.amiiboInternal.amiiboSettings.flags |= 0x10;

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_IsExistApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "IsExistApplicationArea()");
	if (!nfp_data.hasActiveAmiibo || !nfp_data.isMounted)
	{
		osLib_returnFromFunction(hCPU, 0);
		return;
	}
	bool appAreaExists = (nfp_data.amiiboInternal.amiiboSettings.flags & 0x20) != 0;
	osLib_returnFromFunction(hCPU, appAreaExists ? 1 : 0);
}

void nnNfpExport_OpenApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "OpenApplicationArea(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamU32(appAreaId, 0);

	if (!(nfp_data.amiiboInternal.amiiboSettings.flags & 0x20))
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, NN_RESULT_NFP_CODE_NOAPPAREA));
		return;
	}

	uint32 nfpAppAreaId = nfp_data.amiiboInternal.amiiboSettings.getAppDataAppId();
	if (nfpAppAreaId != appAreaId)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, NN_RESULT_NFP_CODE_APPAREAIDMISMATCH));
		return;
	}
	nfp_data.hasOpenApplicationArea = true;

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_ReadApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "ReadApplicationArea(0x{:08x}, 0x{:x})", hCPU->gpr[3], hCPU->gpr[4]);
	ppcDefineParamPtr(bufferPtr, uint8*, 0);
	ppcDefineParamU32(len, 1);

	if (!nfp_data.hasOpenApplicationArea)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	len = std::min(len, (uint32)sizeof(nfp_data.amiiboInternal.applicationData));
	memcpy(bufferPtr, nfp_data.amiiboInternal.applicationData, len);

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_WriteApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "WriteApplicationArea(0x{:08x}, 0x{:x}, 0x{:08x})", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5]);
	ppcDefineParamPtr(bufferPtr, uint8*, 0);
	ppcDefineParamU32(len, 1);

	if (!nfp_data.hasOpenApplicationArea)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	if (nfp_data.isReadOnly)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	len = std::min(len, (uint32)sizeof(nfp_data.amiiboInternal.applicationData));
	memcpy(nfp_data.amiiboInternal.applicationData, bufferPtr, len);
	for (uint32 i = len; i < sizeof(nfp_data.amiiboInternal.applicationData); i++)
		nfp_data.amiiboInternal.applicationData[i] = rand() & 0xFF;

	nfp_data.amiiboInternal.amiiboSettings.appWriteCounter = nfp_data.amiiboInternal.amiiboSettings.appWriteCounter + 1;

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

typedef struct
{
	uint32be appAreaId;
	MEMPTR<uint8> initialData;
	uint32be initialLen;
}NfpCreateInfo_t;

void nnNfpExport_CreateApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "CreateApplicationArea(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamPtr(createInfo, NfpCreateInfo_t, 0);

	if (nfp_data.hasOpenApplicationArea || (nfp_data.amiiboInternal.amiiboSettings.flags&0x20))
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	if (nfp_data.isReadOnly)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	void* writePtr = createInfo->initialData.GetPtr();
	uint32 writeSize = createInfo->initialLen;
	if (writeSize > sizeof(nfp_data.amiiboInternal.applicationData))
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	nfp_data.amiiboInternal.amiiboSettings.setAppDataAppId(createInfo->appAreaId);
	nfp_data.amiiboInternal.amiiboSettings.flags |= 0x20;
	nfp_data.amiiboInternal.amiiboSettings.appWriteCounter = nfp_data.amiiboInternal.amiiboSettings.appWriteCounter + 1;

	nfp_data.hasOpenApplicationArea = false;

	memcpy(nfp_data.amiiboInternal.applicationData, writePtr, writeSize);
	for (uint32 i = writeSize; i < sizeof(nfp_data.amiiboInternal.applicationData); i++)
		nfp_data.amiiboInternal.applicationData[i] = rand() & 0xFF;

	if (!nnNfp_writeCurrentAmiibo())
	{
		cemuLog_log(LogType::Force, "Failed to write Amiibo file data when trying to remove appArea");
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_DeleteApplicationArea(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "DeleteApplicationArea()");

	if (nfp_data.isReadOnly)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}
	if ((nfp_data.amiiboInternal.amiiboSettings.flags & 0x20) == 0)
	{
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	nfp_data.amiiboInternal.amiiboSettings.setAppDataAppId(0);
	nfp_data.amiiboInternal.amiiboSettings.flags &= ~0x20;
	nfp_data.amiiboInternal.amiiboSettings.appWriteCounter = nfp_data.amiiboInternal.amiiboSettings.appWriteCounter + 1;

	if (!nnNfp_writeCurrentAmiibo())
	{
		cemuLog_log(LogType::Force, "Failed to write Amiibo file data when trying to remove appArea");
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfpExport_Flush(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "Flush()");

	if (nfp_data.isReadOnly)
	{
		cemuLog_log(LogType::Force, "Cannot write to Amiibo when it is mounted in read-only mode");
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	if (!nnNfp_writeCurrentAmiibo())
	{
		cemuLog_log(LogType::Force, "Failed to write Amiibo data");
		osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_STATUS, NN_RESULT_MODULE_NN_NFP, 0));
		return;
	}

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

typedef struct
{
	 uint8 ukn00[0x10];
	 uint32be mode;
	 uint8 tagInfo[0x54];
	 uint8 isRegistered;
	 uint8 _padding69[3];
	 uint8 registerInfo[0xA8];
	 uint8 commonInfo[0x40];
	 uint8 ukn154[0x20];
}AmiiboSettingsArgs_t;

static_assert(sizeof(AmiiboSettingsArgs_t) == 0x174);
static_assert(offsetof(AmiiboSettingsArgs_t, mode) == 0x10);
static_assert(offsetof(AmiiboSettingsArgs_t, tagInfo) == 0x14);
static_assert(offsetof(AmiiboSettingsArgs_t, isRegistered) == 0x68);
static_assert(offsetof(AmiiboSettingsArgs_t, registerInfo) == 0x6C);
static_assert(offsetof(AmiiboSettingsArgs_t, commonInfo) == 0x114);

void nnNfpExport_GetAmiiboSettingsArgs(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "GetAmiiboSettingsArgs(0x{:08x})", hCPU->gpr[3]);
	ppcDefineParamStructPtr(settingsArg, AmiiboSettingsArgs_t, 0);

	memset(settingsArg, 0, sizeof(AmiiboSettingsArgs_t));

	settingsArg->mode = 0x64;

	osLib_returnFromFunction(hCPU, BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_NFP, 0));
}

void nnNfp_unloadAmiibo()
{
	nnNfpLock();
	nfp_data.isMounted = false;
	nfp_data.hasActiveAmiibo = false;
	nnNfpUnlock();
}

bool nnNfp_isInitialized()
{
	return nfp_data.nfpIsInitialized;
}

#define NFC_ERROR_NONE					(0)
#define NFC_ERROR_NO_ACCESS				(1)
#define NFC_ERROR_INVALID_FILE_FORMAT	(2)

bool nnNfp_touchNfcTagFromFile(const fs::path& filePath, uint32* nfcError)
{
	AmiiboRawNFCData rawData = { 0 };
	auto nfcData = FileStream::LoadIntoMemory(filePath);
	if (!nfcData)
	{
		*nfcError = NFC_ERROR_NO_ACCESS;
		return false;
	}
	if (nfcData->size() < sizeof(AmiiboRawNFCData))
	{
		*nfcError = NFC_ERROR_INVALID_FILE_FORMAT;
		return false;
	}
	memcpy(&rawData, nfcData->data(), sizeof(AmiiboRawNFCData));

	if (rawData.dynamicLock[0] != 0x01 || rawData.dynamicLock[1] != 0x00 || rawData.dynamicLock[2] != 0x0F || rawData.dynamicLock[3] != 0xBD)
	{
		rawData.dynamicLock[0] = 0x01;
		rawData.dynamicLock[1] = 0x00;
		rawData.dynamicLock[2] = 0x0F;
		rawData.dynamicLock[3] = 0xBD;

	}
	if (rawData.cfg0[0] != 0x00 || rawData.cfg0[1] != 0x00 || rawData.cfg0[2] != 0x00 || rawData.cfg0[3] != 0x04)
	{
		*nfcError = NFC_ERROR_INVALID_FILE_FORMAT;
		return false;
	}
	if (rawData.cfg1[0] != 0x5F || rawData.cfg1[1] != 0x00 || rawData.cfg1[2] != 0x00 || rawData.cfg1[3] != 0x00)
	{
		*nfcError = NFC_ERROR_INVALID_FILE_FORMAT;
		return false;
	}
	if (rawData.staticLock[0] != 0x0F || rawData.staticLock[1] != 0xE0)
	{
		*nfcError = NFC_ERROR_INVALID_FILE_FORMAT;
		return false;
	}
	if (rawData.cc[0] != 0xF1 || rawData.cc[1] != 0x10 || rawData.cc[2] != 0xFF || rawData.cc[3] != 0xEE)
	{
		*nfcError = NFC_ERROR_INVALID_FILE_FORMAT;
		return false;
	}

	uint8 serialNumber[7];
	serialNumber[0] = rawData.ntagSerial[0];
	serialNumber[1] = rawData.ntagSerial[1];
	serialNumber[2] = rawData.ntagSerial[2];
	serialNumber[3] = rawData.ntagSerial[4];
	serialNumber[4] = rawData.ntagSerial[5];
	serialNumber[5] = rawData.ntagSerial[6];
	serialNumber[6] = rawData.nintendoId;

	uint8 serialCheckByte0 = rawData.ntagSerial[3];
	uint8 serialCheckByte1 = rawData.lockBytes[0];

	uint8 bcc0 = serialNumber[0] ^ serialNumber[1] ^ serialNumber[2] ^ 0x88;
	uint8 bcc1 = serialNumber[3] ^ serialNumber[4] ^ serialNumber[5] ^ serialNumber[6];

	if (serialCheckByte0 != bcc0 || serialCheckByte1 != bcc1)
	{
		cemuLog_log(LogType::Force, "nn_nfp: Mismatch in serial checksum of scanned NFC tag");
	}
	nfp_data.amiiboProcessedData.uidLength = 7;
	memcpy(nfp_data.amiiboProcessedData.uid, serialNumber, 7);
	nnNfp_unloadAmiibo();
	nnNfpLock();
	memcpy(&nfp_data.amiiboNFCData, &rawData, sizeof(AmiiboRawNFCData));
	amiiboDecrypt();
	nfp_data.amiiboPath = filePath;
	nfp_data.hasActiveAmiibo = true;
	if (nfp_data.activateEvent)
	{
		MEMPTR<coreinit::OSEvent> osEvent(nfp_data.activateEvent);
		coreinit::OSSignalEvent(osEvent);
	}
	nfp_data.amiiboTouchTime = GetTickCount();
	nnNfpUnlock();
	*nfcError = NFC_ERROR_NO_ACCESS;
	return true;
}

bool nnNfp_writeCurrentAmiibo()
{
	nnNfpLock();
	if (!nfp_data.hasActiveAmiibo)
	{
		nnNfpUnlock();
		return false;
	}

	uint16 writeCounter = nfp_data.amiiboInternal.writeCounterLow + (nfp_data.amiiboInternal.writeCounterHigh << 8);
	writeCounter++;
	nfp_data.amiiboInternal.writeCounterLow = writeCounter & 0xFF;
	nfp_data.amiiboInternal.writeCounterHigh = (writeCounter >> 8) & 0xFF;

	FileStream* fs = FileStream::openFile2(nfp_data.amiiboPath, true);
	if (!fs)
	{
		nnNfpUnlock();
		return false;
	}
	AmiiboRawNFCData nfcData;
	amiiboEncrypt(&nfcData);
	fs->writeData(&nfcData, sizeof(AmiiboRawNFCData));
	delete fs;

	nnNfpUnlock();
	return true;
}

void nnNfp_update()
{
	if (nfp_data.hasActiveAmiibo == false)
		return;
	if (!nnNfpTryLock())
		return;
	if (nfp_data.hasActiveAmiibo == false)
		return;
	uint32 amiiboElapsedTouchTime = GetTickCount() - nfp_data.amiiboTouchTime;
	if (amiiboElapsedTouchTime >= 1500)
	{
		nnNfp_unloadAmiibo();
		if (nfp_data.deactivateEvent)
		{
			coreinit::OSEvent* osEvent = (coreinit::OSEvent*)memory_getPointerFromVirtualOffset(nfp_data.deactivateEvent);
			coreinit::OSSignalEvent(osEvent);
		}
	}
	nnNfpUnlock();
}

void nnNfpExport_GetNfpState(PPCInterpreter_t* hCPU)
{
	cemuLog_log(LogType::NN_NFP, "GetNfpState()");

	uint64 titleId = CafeSystem::GetForegroundTitleId();
	if (titleId == 0x0005000010162d00 || titleId == 0x0005000010162e00)
	{
		coreinit::OSSleepTicks(ESPRESSO_TIMER_CLOCK / 100);
	}

	uint32 nfpState;
	if (nfp_data.nfpIsInitialized == false)
	{
		nfpState = NFP_STATE_NONE;
	}
	else
	{
		if (nfp_data.isMounted && nfp_data.hasActiveAmiibo)
		{
			if (nfp_data.isReadOnly)
				nfpState = NFP_STATE_RW_MOUNT_ROM;
			else
				nfpState = NFP_STATE_RW_MOUNT;
		}
		else if (nfp_data.isDetecting)
		{
			if (nfp_data.hasActiveAmiibo)
			{
				nfpState = NFP_STATE_RW_ACTIVE;
			}
			else
				nfpState = NFP_STATE_RW_SEARCH;
		}
		else
		{
			nfpState = NFP_STATE_INIT;
		}
	}
	osLib_returnFromFunction(hCPU, nfpState);
}

namespace nn::nfp
{
	typedef struct
	{
		 uint8 uidLength;
		 uint8 uid[0xA];
		 uint8 ukn0B;
		 uint8 ukn0C;
		 uint8 ukn0D;
	}NFCTagInfoCallbackParam_t;

	uint32 NFCGetTagInfo(uint32 index, uint32 timeout, MPTR functionPtr, void* userParam)
	{
		cemuLog_log(LogType::NN_NFP, "NFCGetTagInfo({},{},0x{:08x},0x{:08x})", index, timeout, functionPtr, userParam ? memory_getVirtualOffsetFromPointer(userParam) : 0);

		cemu_assert(index == 0);

		nnNfpLock();

		StackAllocator<NFCTagInfoCallbackParam_t> _callbackParam;
		NFCTagInfoCallbackParam_t* callbackParam = _callbackParam.GetPointer();

		memset(callbackParam, 0x00, sizeof(NFCTagInfoCallbackParam_t));

		memcpy(callbackParam->uid, nfp_data.amiiboProcessedData.uid, nfp_data.amiiboProcessedData.uidLength);
		callbackParam->uidLength = (uint8)nfp_data.amiiboProcessedData.uidLength;

		PPCCoreCallback(functionPtr, index, 0, _callbackParam.GetPointer(), userParam);

		nnNfpUnlock();

		return 0;
	}

	uint32 GetErrorCode(uint32 result)
	{
		uint32 level = (result >> 0x1b) & 3;
		uint32 mask = 0x7f00000;
		if (level != 3)
		{
			mask = 0x1ff00000;
		}
		if (((result & mask) == 0x1b00000) && ((result & 0x80000000) != 0))
		{
			mask = 0x3ff;
			if (level != 3)
			{
				mask = 0xfffff;
			}
			return ((result & mask) >> 7) + 1680000;
		}
		return 1680000;
	}

	void nnNfp_load()
	{
		osLib_addFunction("nn_nfp", "SetActivateEvent__Q2_2nn3nfpFP7OSEvent", nnNfpExport_SetActivateEvent);
		osLib_addFunction("nn_nfp", "SetDeactivateEvent__Q2_2nn3nfpFP7OSEvent", nnNfpExport_SetDeactivateEvent);
		osLib_addFunction("nn_nfp", "StartDetection__Q2_2nn3nfpFv", nnNfpExport_StartDetection);
		osLib_addFunction("nn_nfp", "StopDetection__Q2_2nn3nfpFv", nnNfpExport_StopDetection);

		osLib_addFunction("nn_nfp", "GetTagInfo__Q2_2nn3nfpFPQ3_2nn3nfp7TagInfo", nnNfpExport_GetTagInfo);
		osLib_addFunction("nn_nfp", "Mount__Q2_2nn3nfpFv", nnNfpExport_Mount);
		osLib_addFunction("nn_nfp", "MountRom__Q2_2nn3nfpFv", nnNfpExport_MountRom);
		osLib_addFunction("nn_nfp", "Unmount__Q2_2nn3nfpFv", nnNfpExport_Unmount);
		osLib_addFunction("nn_nfp", "GetNfpCommonInfo__Q2_2nn3nfpFPQ3_2nn3nfp10CommonInfo", nnNfpExport_GetNfpCommonInfo);
		osLib_addFunction("nn_nfp", "GetNfpRegisterInfo__Q2_2nn3nfpFPQ3_2nn3nfp12RegisterInfo", nnNfpExport_GetNfpRegisterInfo);

		osLib_addFunction("nn_nfp", "InitializeRegisterInfoSet__Q2_2nn3nfpFPQ3_2nn3nfp15RegisterInfoSet", nnNfpExport_InitializeRegisterInfoSet);
		osLib_addFunction("nn_nfp", "SetNfpRegisterInfo__Q2_2nn3nfpFRCQ3_2nn3nfp15RegisterInfoSet", nnNfpExport_SetNfpRegisterInfo);

		osLib_addFunction("nn_nfp", "IsExistApplicationArea__Q2_2nn3nfpFv", nnNfpExport_IsExistApplicationArea);
		osLib_addFunction("nn_nfp", "OpenApplicationArea__Q2_2nn3nfpFUi", nnNfpExport_OpenApplicationArea);
		osLib_addFunction("nn_nfp", "CreateApplicationArea__Q2_2nn3nfpFRCQ3_2nn3nfp25ApplicationAreaCreateInfo", nnNfpExport_CreateApplicationArea);
		osLib_addFunction("nn_nfp", "DeleteApplicationArea__Q2_2nn3nfpFv", nnNfpExport_DeleteApplicationArea);
		osLib_addFunction("nn_nfp", "ReadApplicationArea__Q2_2nn3nfpFPvUi", nnNfpExport_ReadApplicationArea);
		osLib_addFunction("nn_nfp", "WriteApplicationArea__Q2_2nn3nfpFPCvUiRCQ3_2nn3nfp5TagId", nnNfpExport_WriteApplicationArea);

		osLib_addFunction("nn_nfp", "Flush__Q2_2nn3nfpFv", nnNfpExport_Flush);

		osLib_addFunction("nn_nfp", "Initialize__Q2_2nn3nfpFv", nnNfpExport_Initialize);
		osLib_addFunction("nn_nfp", "GetNfpState__Q2_2nn3nfpFv", nnNfpExport_GetNfpState);

		osLib_addFunction("nn_nfp", "GetAmiiboSettingsArgs__Q2_2nn3nfpFPQ3_2nn3nfp18AmiiboSettingsArgs", nnNfpExport_GetAmiiboSettingsArgs);
	}

	class : public COSModule
	{
		public:
		std::string_view GetName() override
		{
			return "nn_nfp";
		}

		void RPLMapped() override
		{
			nnNfp_load();

			cafeExportRegisterFunc(nn::nfp::GetErrorCode, "nn_nfp", "GetErrorCode__Q2_2nn3nfpFRCQ2_2nn6Result", LogType::NN_NFP);
			cafeExportRegisterFunc(nn::nfp::GetNfpRomInfo, "nn_nfp", "GetNfpRomInfo__Q2_2nn3nfpFPQ3_2nn3nfp7RomInfo", LogType::NN_NFP);
			cafeExportRegisterFunc(nn::nfp::GetNfpReadOnlyInfo, "nn_nfp", "GetNfpReadOnlyInfo__Q2_2nn3nfpFPQ3_2nn3nfp12ReadOnlyInfo", LogType::NN_NFP);
		};

	}s_COSnnNfpModule;

	COSModule* GetModule()
	{
		return &s_COSnnNfpModule;
	}
}
