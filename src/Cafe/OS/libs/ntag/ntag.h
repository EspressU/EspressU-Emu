#pragma once
#include "Cafe/OS/libs/nfc/nfc.h"
#include "Cafe/OS/RPL/COSModule.h"

#define NTAG_RESULT_SUCCESS		(0)
#define NTAG_RESULT_UNINITIALIZED	(-0x3E7)
#define NTAG_RESULT_INVALID_STATE	(-0x3E6)
#define NTAG_RESULT_NO_TAG		(-0x3E5)
#define NTAG_RESULT_INVALID		(-0x3E1)
#define NTAG_RESULT_UID_MISMATCH	(-0x3DB)

namespace ntag
{
	struct NTAGFormatSettings
	{
		 uint8 version;
		 uint32 makerCode;
		 uint32 identifyCode;
		 uint8 reserved[0x1C];
	};
	static_assert(sizeof(NTAGFormatSettings) == 0x28);

#pragma pack(1)
	struct NTAGNoftHeader
	{
		 uint32 magic;
		 uint8 version;
		 uint16 writeCount;
		 uint8 unknown;
	};
	static_assert(sizeof(NTAGNoftHeader) == 0x8);
#pragma pack()

	struct NTAGInfoHeader
	{
		 uint16 rwHeaderOffset;
		 uint16 rwSize;
		 uint16 roHeaderOffset;
		 uint16 roSize;
		 nfc::NFCUid uid;
		 uint8 formatVersion;
	};
	static_assert(sizeof(NTAGInfoHeader) == 0x10);

	struct NTAGAreaHeader
	{
		 uint16 magic;
		 uint16 offset;
		 uint16 size;
		 uint16 padding;
		 uint32 makerCode;
		 uint32 identifyCode;
	};
	static_assert(sizeof(NTAGAreaHeader) == 0x10);

	struct NTAGAreaInfo
	{
		 MPTR data;
		 uint16 size;
		 uint16 padding;
		 uint32 makerCode;
		 uint32 identifyCode;
		 uint8 reserved[0x20];
	};
	static_assert(sizeof(NTAGAreaInfo) == 0x30);

	struct NTAGData
	{
		 nfc::NFCUid uid;
		 uint8 readOnly;
		 uint8 formatVersion;
		 uint8 padding[3];
		 NTAGAreaInfo rwInfo;
		 NTAGAreaInfo roInfo;
		 uint8 reserved[0x20];
	};
	static_assert(sizeof(NTAGData) == 0x8C);

	sint32 NTAGInit(uint32 chan);

	sint32 NTAGInitEx(uint32 chan);

	sint32 NTAGShutdown(uint32 chan);

	bool NTAGIsInit(uint32 chan);

	void NTAGProc(uint32 chan);

	void NTAGSetFormatSettings(NTAGFormatSettings* formatSettings);

	void NTAGSetTagDetectCallback(uint32 chan, MPTR callback, void* context);

	sint32 NTAGAbort(uint32 chan, MPTR callback, void* context);

	sint32 NTAGRead(uint32 chan, uint32 timeout, nfc::NFCUid* uid, nfc::NFCUid* uidMask, MPTR callback, void* context);

	sint32 NTAGWrite(uint32 chan, uint32 timeout, nfc::NFCUid* uid, uint32 rwSize, void* rwData, MPTR callback, void* context);

	sint32 NTAGFormat(uint32 chan, uint32 timeout, nfc::NFCUid* uid, uint32 rwSize, void* rwData, MPTR callback, void* context);

	COSModule* GetModule();
}
