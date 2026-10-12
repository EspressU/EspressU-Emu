#pragma once
#include "Cafe/OS/libs/snd_core/ax.h"

namespace snd_core
{
	struct sndGeneric_t
	{
		bool isInitialized{false};
		bool isSoundCore2{false};
		struct
		{
			uint32 rendererFreq{0};
			uint32 frameLength{0};
			uint32 pipelineMode{0};
		}initParam;
	};

	extern sndGeneric_t sndGeneric;

	const uint32 AX_SYNCFLAG_SRCFILTER = 0x1;
	const uint32 AX_SYNCFLAG_DEVICEMIXMASK = 0x2;
	const uint32 AX_SYNCFLAG_PLAYBACKSTATE = 0x4;
	const uint32 AX_SYNCFLAG_VOICETYPE = 0x8;
	const uint32 AX_SYNCFLAG_DEVICEMIX = 0x10;
	const uint32 AX_SYNCFLAG_ITD20 = 0x20;
	const uint32 AX_SYNCFLAG_ITD40 = 0x40;
	const uint32 AX_SYNCFLAG_VE = 0x100;
	const uint32 AX_SYNCFLAG_VEDELTA = 0x200;
	const uint32 AX_SYNCFLAG_OFFSETS = 0x400;
	const uint32 AX_SYNCFLAG_LOOPFLAG = 0x800;
	const uint32 AX_SYNCFLAG_LOOPOFFSET = 0x1000;
	const uint32 AX_SYNCFLAG_ENDOFFSET = 0x2000;
	const uint32 AX_SYNCFLAG_CURRENTOFFSET = 0x4000;
	const uint32 AX_SYNCFLAG_ADPCMDATA = 0x8000;
	const uint32 AX_SYNCFLAG_SRCDATA = 0x10000;
	const uint32 AX_SYNCFLAG_SRCRATIO = 0x20000;
	const uint32 AX_SYNCFLAG_ADPCMLOOP = 0x40000;
	const uint32 AX_SYNCFLAG_LPFDATA = 0x80000;
	const uint32 AX_SYNCFLAG_LPFCOEF = 0x100000;
	const uint32 AX_SYNCFLAG_BIQUADDATA = 0x200000;
	const uint32 AX_SYNCFLAG_BIQUADCOEF = 0x400000;
	const uint32 AX_SYNCFLAG_VOICEREMOTEON = 0x800000;
	const uint32 AX_SYNCFLAG_4000000 = 0x4000000;
	const uint32 AX_SYNCFLAG_8000000 = 0x8000000;

	struct axADPCMInternal_t
	{
		 uint16 coef[16];
		 uint16 gain;
		 uint16 scale;
		 uint16 yn1;
		 uint16 yn2;
	};

	struct axADPCMLoopInternal_t
	{
		 uint16 loopScale;
		 uint16 loopYn1;
		 uint16 loopYn2;
	};

	struct axOffsetsInternal_t
	{
		 uint16 loopFlag;
		 uint16 format;
		 uint16 ptrHighExtension;
		 uint16 loopOffsetPtrHigh;
		 uint16 loopOffsetPtrLow;
		 uint16 endOffsetPtrHigh;
		 uint16 endOffsetPtrLow;
		 uint16 currentOffsetPtrHigh;
		 uint16 currentOffsetPtrLow;

		uint32 GetLoopOffset32() const
		{
			uint32 offset = (uint32)_swapEndianU16(loopOffsetPtrHigh) << 16;
			return offset | (uint32)_swapEndianU16(loopOffsetPtrLow);
		}

		uint32 GetEndOffset32() const
		{
			uint32 offset = (uint32)_swapEndianU16(endOffsetPtrHigh) << 16;
			return offset | (uint32)_swapEndianU16(endOffsetPtrLow);
		}

		uint32 GetCurrentOffset32() const
		{
			uint32 offset = (uint32)_swapEndianU16(currentOffsetPtrHigh) << 16;
			return offset | (uint32)_swapEndianU16(currentOffsetPtrLow);
		}
	};

	const int AX_BUS_COUNT = 4;

	struct AXVPBInternal_t
	{
		 uint16be nextAddrHigh;
		 uint16be nextAddrLow;
		 uint16be selfAddrHigh;
		 uint16be selfAddrLow;
		 uint16 srcFilterMode;
		 uint16 srcTapFilter;
		 uint16be mixerSelect;
		 uint16 voiceType;
		 uint16 deviceMixMaskTV[4];
		 uint16 deviceMixMaskDRC[4 * 2];
		 AXCHMIX_DEPR deviceMixTV[AX_BUS_COUNT * AX_TV_CHANNEL_COUNT];
		 AXCHMIX_DEPR deviceMixDRC[AX_BUS_COUNT * AX_DRC_CHANNEL_COUNT * 2];
		 AXCHMIX_DEPR deviceMixRMT[0x40 / 4];
		 uint16 reserved148_voiceRmtOn;
		 uint16 deviceMixMaskRMT[0x10];
		 uint16 playbackState;
		 uint16 reserved16C;
		 uint16be itdAddrHigh;
		 uint16be itdAddrLow;
		 uint16 reserved172;
		 uint16 reserved174;
		 uint16 reserved176_itdRelated;
		 uint16 reserved178_itdRelated;
		 uint16be veVolume;
		 uint16be veDelta;
		 axOffsetsInternal_t internalOffsets;
		 axADPCMInternal_t adpcmData;
		 AXPBSRC_t src;
		 axADPCMLoopInternal_t adpcmLoop;
		struct
		{
			 uint16 on;
			 uint16 yn1;
			 uint16 a0;
			 uint16 b0;
		}lpf;
		struct
		{
			 uint16 on;
			 sint16 xn1;
			 sint16 xn2;
			 sint16 yn1;
			 sint16 yn2;
			 uint16 b0;
			 uint16 b1;
			 uint16 b2;
			 uint16 a1;
			 uint16 a2;
		}biquad;
		uint16 reserved1E8[0x18];
		uint16 reserved218[0x20];
		uint16 reserved258[0x10];
		uint16 reserved278;
		uint16 reserved27A;
		uint16 reserved27C;
		uint16 reserved27E;
		uint16 reserved280;
		uint16 reserved282_rmtIIRGuessed;
		uint32 reserved284;
		uint32 reserved288;
		uint32 reserved28C;
		uint32 reserved290;
		uint16 reserved294;
		uint16 reserved296;
		 uint16 reserved298;
		 uint16 reserved29A;
		 uint16 reserved29C;
		 uint16 reserved29E;
		 uint16be index;
		 uint16be ukn2A2;
		uint16 reserved2A4;
		uint16 reserved2A6;
		uint16 reserved2A8;
		uint16 reserved2AA;
		 MEMPTR<AXVPBInternal_t> nextToProcess;
		uint32 reserved2B0;
		uint32 reserved2B4;
		uint32 reserved2B8;
		uint32 reserved2BC;
	};

	static_assert(sizeof(AXVPBInternal_t) == 0x2C0);

	extern AXVPBInternal_t* __AXVPBInternalVoiceArray;
	extern AXVPBInternal_t* __AXVPBInternalVoiceShadowCopyArrayPtr;
	extern AXVPB* __AXVPBArrayPtr;

	void AXResetVoiceLoopCount(AXVPB* vpb);

	std::vector<AXVPB*>& AXVoiceList_GetListByPriority(uint32 priority);
	std::vector<AXVPB*>& AXVoiceList_GetFreeVoices();

	inline AXVPBInternal_t* GetInternalVoice(const AXVPB* vpb)
	{
		return __AXVPBInternalVoiceArray + (size_t)vpb->index;
	}

	inline uint32 GetVoiceIndex(const AXVPB* vpb)
	{
		return (uint32)vpb->index;
	}

    void AXVBP_Reset();

	void AXIst_InitThread();
	OSThread_t* AXIst_GetThread();
	void AXIst_StopThread();

	void AXIst_HandleFrameCallbacks();

	void AXAux_incrementBufferIndex();

	extern SysAllocator<sint32, AX_SAMPLES_MAX * AX_TV_CHANNEL_COUNT> __AXTVOutputBuffer;
	extern SysAllocator<sint32, AX_SAMPLES_MAX * AX_DRC_CHANNEL_COUNT * 2> __AXDRCOutputBuffer;

}