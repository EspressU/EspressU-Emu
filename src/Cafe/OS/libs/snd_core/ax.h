#pragma once

#include "util/helpers/fspinlock.h"
#include "Cafe/OS/RPL/COSModule.h"

struct PPCInterpreter_t;

namespace snd_core
{
	const int AX_RENDERER_FREQ_32KHZ = 0;
	const int AX_RENDERER_FREQ_48KHZ = 1;
	const int AX_FRAMELENGTH_3MS = 0;

	const int AX_SAMPLES_PER_3MS_48KHZ = (144);
	const int AX_SAMPLES_PER_3MS_32KHZ = (96);
	const int AX_SAMPLES_MAX = AX_SAMPLES_PER_3MS_48KHZ;

	const int AX_DEV_TV = 0;
	const int AX_DEV_DRC = 1;
	const int AX_DEV_RMT = 2;
	const int AX_DEV_COUNT = 3;

	const int AX_UPSAMPLE_STAGE_BEFORE_FINALMIX = 0;
	const int AX_UPSAMPLE_STAGE_AFTER_FINALMIX = 1;

	const int AX_PIPELINE_SINGLE = 0;

	struct AXINITPARAM
	{
		uint32be freq;
		uint32be frameLength;
		uint32be pipelineMode;
	};

	const int AX_TV_CHANNEL_COUNT = 6;
	const int AX_DRC_CHANNEL_COUNT = 4;
	const int AX_RMT_CHANNEL_COUNT = 1;

	const int AX_APP_FRAME_CALLBACK_MAX = 64;

	const int AX_MODE_STEREO = 0;
	const int AX_MODE_SURROUND = 1;
	const int AX_MODE_DPL2 = 2;
	const int AX_MODE_6CH = 3;
	const int AX_MODE_MONO = 5;

	const int AX_PRIORITY_MAX = 32;
	const int AX_PRIORITY_FREE = 0;
	const int AX_PRIORITY_NODROP = 31;
	const int AX_PRIORITY_LOWEST = 1;
	const int AX_MAX_VOICES = 96;

	const int AX_AUX_BUS_COUNT = 3;
	const int AX_MAX_NUM_BUS = 4;

	const int AX_FORMAT_ADPCM = 0x0;
	const int AX_FORMAT_PCM16 = 0xA;
	const int AX_FORMAT_PCM8 = 0x19;

	const int AX_LPF_OFF = 0x0;

	const int AX_BIQUAD_OFF = 0x0;

	const int AX_SRC_TYPE_NONE = 0x0;
	const int AX_SRC_TYPE_LINEAR = 0x1;
	const int AX_SRC_TYPE_LOWPASS1 = 0x2;
	const int AX_SRC_TYPE_LOWPASS2 = 0x3;
	const int AX_SRC_TYPE_LOWPASS3 = 0x4;

	const int AX_FILTER_MODE_TAP = 0x0;
	const int AX_FILTER_MODE_LINEAR = 0x1;
	const int AX_FILTER_MODE_NONE = 0x2;

	const int AX_FILTER_LOWPASS_8K = 0x0;
	const int AX_FILTER_LOWPASS_12K = 0x1;
	const int AX_FILTER_LOWPASS_16K = 0x2;

	bool isInitialized();
	bool IsSndCore2();
	void reset();

	struct AXAUXCBCHANNELINFO
	{
		 uint32be numChannels;
		 uint32be numSamples;
	};

	struct AXPBOFFSET_t
	{
		 uint16		format;
		 uint16		loopFlag;
		 uint32		loopOffset;
		 uint32		endOffset;
		 uint32		currentOffset;
		 MPTR		samples;
	};

	struct AXPBVE
	{
		uint16be currentVolume;
		sint16be currentDelta;
	};

	struct AXVPBItd
	{
		uint8 ukn[64];
	};

	struct AXVPB
	{
		 uint32be			index;
		 uint32be			playbackState;
		 uint32be			ukn08;
		 uint32be			mixerSelect;
		 MEMPTR<AXVPB>		next;
		 MEMPTR<AXVPB>		prev;
		 uint32be			ukn18;
		 uint32be			priority;
		 uint32be			callback;
		 uint32be			userParam;
		 uint32be			sync;
		 uint32be			depop;
		 MEMPTR<AXVPBItd>	itd;
		 AXPBOFFSET_t		offsets;
		 uint32be			callbackEx;
		 uint32be			ukn4C_dropReason;
		 float32be			dspLoad;
		 float32be			ppcLoad;
	};

	struct AXPBLPF_t
	{
		 uint16 on;
		 sint16 yn1;
		 sint16 a0;
		 sint16 b0;
	};

	struct AXPBBIQUAD_t
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
	};

	struct AXRemixMatrix_t
	{
		 uint32be channelIn;
		 uint32be channelOut;
		 MEMPTR<float32be> matrix;
	};

	struct AXRemixMatrices_t
	{
		 AXRemixMatrix_t deviceEntry[3];
	};

	struct AXPBADPCM_t
	{
		uint16 a[16];
		uint16 gain;
		uint16 scale;
		uint16 yn1;
		uint16 yn2;
	};

	struct AXPBADPCMLOOP_t
	{
		uint16 loopScale;
		uint16 loopYn1;
		uint16 loopYn2;
	};

	struct AXPBSRC_t
	{
		 uint16 ratioHigh;
		 uint16 ratioLow;
		 uint16 currentFrac;
		 uint16 historySamples[4];

		uint32 GetSrcRatio32() const
		{
			uint32 offset = (uint32)_swapEndianU16(ratioHigh) << 16;
			return offset | (uint32)_swapEndianU16(ratioLow);
		}
	};

	struct AXCHMIX_DEPR
	{
		uint16 vol;
		sint16 delta;
	};

	struct AXCHMIX2
	{
		uint16be vol;
		sint16be delta;
	};

	void AXVPB_Init();
    void AXResetToDefaultState();

	sint32 AXIsValidDevice(sint32 device, sint32 deviceIndex);

	AXVPB* AXAcquireVoiceEx(uint32 priority, MPTR callbackEx, MPTR userParam);
	void AXFreeVoice(AXVPB* vpb);

	bool AXUserIsProtected();
	sint32 AXUserBegin();
	sint32 AXUserEnd();

	bool AXVoiceProtection_IsProtectedByAnyThread(AXVPB* vpb);
	bool AXVoiceProtection_IsProtectedByCurrentThread(AXVPB* vpb);

	sint32 AXVoiceBegin(AXVPB* voice);
	sint32 AXVoiceEnd(AXVPB* voice);
	sint32 AXSetVoiceDeviceMix(AXVPB* vpb, sint32 device, sint32 deviceIndex, AXCHMIX_DEPR* mix);
	void AXSetVoiceState(AXVPB* vpb, sint32 voiceState);
	sint32 AXIsVoiceRunning(AXVPB* vpb);
	void AXSetVoiceType(AXVPB* vpb, uint16 voiceType);
	void AXSetVoiceAdpcm(AXVPB* vpb, AXPBADPCM_t* adpcm);
	void AXSetVoiceAdpcmLoop(AXVPB* vpb, AXPBADPCMLOOP_t* adpcmLoop);
	void AXSetVoiceSrc(AXVPB* vpb, AXPBSRC_t* src);
	void AXSetVoiceSrcType(AXVPB* vpb, uint32 srcType);
	sint32 AXSetVoiceSrcRatio(AXVPB* vpb, float ratio);
	void AXSetVoiceVe(AXVPB* vpb, AXPBVE* ve);
	void AXComputeLpfCoefs(uint32 freq, uint16be* a0, uint16be* b0);
	void AXSetVoiceLpf(AXVPB* vpb, AXPBLPF_t* lpf);
	void AXSetVoiceLpfCoefs(AXVPB* vpb, uint16 a0, uint16 b0);
	void AXSetVoiceBiquad(AXVPB* vpb, AXPBBIQUAD_t* biquad);
	void AXSetVoiceBiquadCoefs(AXVPB* vpb, uint16 b0, uint16 b1, uint16 b2, uint16 a1, uint16 a2);
	void AXSetVoiceOffsets(AXVPB* vpb, AXPBOFFSET_t* pbOffset);
	void AXSetVoiceOffsetsEx(AXVPB* vpb, AXPBOFFSET_t* pbOffset, void* sampleBase);
	void AXGetVoiceOffsets(AXVPB* vpb, AXPBOFFSET_t* pbOffset);
	void AXGetVoiceOffsetsEx(AXVPB* vpb, AXPBOFFSET_t* pbOffset, MPTR sampleBase);
	void AXSetVoiceSamplesAddr(AXVPB* vpb, void* sampleBase);
	void AXSetVoiceCurrentOffset(AXVPB* vpb, uint32 currentOffset);
	void AXSetVoiceLoopOffset(AXVPB* vpb, uint32 loopOffset);
	void AXSetVoiceEndOffset(AXVPB* vpb, uint32 endOffset);
	void AXSetVoiceCurrentOffsetEx(AXVPB* vpb, uint32 currentOffset, MPTR sampleBase);
	void AXSetVoiceLoopOffsetEx(AXVPB* vpb, uint32 loopOffset, MPTR sampleBase);
	void AXSetVoiceEndOffsetEx(AXVPB* vpb, uint32 endOffset, MPTR sampleBase);
	uint32 AXGetVoiceCurrentOffsetEx(AXVPB* vpb, MPTR sampleBase);
	void AXSetVoiceLoop(AXVPB* vpb, uint16 loopState);
	sint32 AXGetVoiceLoopCount(AXVPB* vpb);

	void AXIst_Init();
	void AXIst_ThreadEntry(PPCInterpreter_t* hCPU);
	void AXIst_QueueFrame();

	void AXResetCallbacks();

	bool AXIst_IsFrameBeingProcessed();

	sint32 AXSetDeviceUpsampleStage(sint32 device, int upsampleStage);
	sint32 AXGetDeviceUpsampleStage(sint32 device, uint32be* upsampleStage);

	sint32 AXGetDeviceFinalMixCallback(sint32 device, uint32be* funcAddrPtr);
	sint32 AXRegisterDeviceFinalMixCallback(sint32 device, MPTR funcAddr);

	sint32 AXSetDeviceRemixMatrix(sint32 deviceId, uint32 inputChannelCount, uint32 outputChannelCount, const MEMPTR<float32be>& matrix);
	sint32 AXGetDeviceRemixMatrix(uint32 deviceId, uint32 inputChannelCount, uint32 outputChannelCount, MEMPTR<MEMPTR<float32be>>& matrix);

	sint32 AXRegisterAppFrameCallback(MPTR funcAddr);
	sint32 AXDeregisterAppFrameCallback(MPTR funcAddr);
	MPTR AXRegisterFrameCallback(MPTR funcAddr);

	sint32 AXGetInputSamplesPerFrame();
	sint32 AXGetInputSamplesPerSec();

	void resetNumProcessedFrames();
	uint32 getNumProcessedFrames();

	void AXOut_Init();

	sint32 AIGetSamplesPerChannel(uint32 device);
	sint32 AIGetChannelCount(uint32 device);
	sint16* AIGetCurrentDMABuffer(uint32 device);

	void AXOut_SubmitTVFrame(sint32 frameIndex);
	void AXOut_SubmitDRCFrame(sint32 frameIndex);

	sint32 AXGetDeviceMode(sint32 device);

	extern uint32 numProcessedFrames;

	void AXAux_Init();

	void AXAux_Process();

	sint32be* AXAux_GetInputBuffer(sint32 device, sint32 deviceIndex, sint32 auxBus);
	sint32be* AXAux_GetOutputBuffer(sint32 device, sint32 deviceIndex, sint32 auxBus);

	sint32 AXGetAuxCallback(sint32 device, sint32 deviceIndex, uint32 auxBusIndex, MEMPTR<uint32be> funcPtrOut, MEMPTR<uint32be> contextPtrOut);
	sint32 AXRegisterAuxCallback(sint32 device, sint32 deviceIndex, uint32 auxBusIndex, MPTR funcMPTR, MPTR userParam);

	sint32 AXSetAuxReturnVolume(uint32 device, uint32 deviceIndex, uint32 auxBus, uint16 volume);

	extern uint16 __AXTVAuxReturnVolume[AX_AUX_BUS_COUNT];

	const int AX_MIXER_SELECT_DSP = (0);
	const int AX_MIXER_SELECT_PPC = (1);
	const int AX_MIXER_SELECT_BOTH = (2);

	void AXMix_Init();
	void AXSetDefaultMixerSelect(uint32 mixerSelect);
	uint32 AXGetDefaultMixerSelect();

	void AXMix_DepopVoice(struct AXVPBInternal_t* internalShadowCopy);

	void AXMix_process(struct AXVPBInternal_t* internalShadowCopyHead);

	extern FSpinlock __AXVoiceListSpinlock;

	struct AXVPBMULTI
	{
		 betype<uint32> isUsed;
		 betype<uint32> channelCount;
		 MEMPTR<AXVPB> voice[6];
	};
	static_assert(sizeof(AXVPBMULTI) == 0x20);

	struct AXMULTIVOICEUKNSTRUCT
	{
		uint8 ukn[0x4A];
		betype<sint16> channelCount;
	};

	struct AXDSPADPCM
	{
		 uint32be numSamples;
		 uint32be ukn04;
		 uint32be sampleRate;
		 uint16be isLooped;
		 uint16be format;
		 uint32be ukn10;
		 uint32be ukn14;
		 uint32be ukn18;
		 uint16be coef[16];
		 uint16be gain;
		 uint16be scale;
		 uint16be yn1;
		 uint16be yn2;
		 uint16be ukn44;
		 uint16be ukn46;
		 uint16be ukn48;
		 uint16be channelCount;
		 uint16be ukn4C;
		 uint8 _padding4E[0x12];
	};
	static_assert(sizeof(AXDSPADPCM) == 0x60);

	void AXMultiVoice_Init();

	sint32 AXAcquireMultiVoice(sint32 voicePriority, void* cbFunc, void* cbData, AXMULTIVOICEUKNSTRUCT* uknR6, MEMPTR<AXVPBMULTI>* multiVoiceOut);
	void AXFreeMultiVoice(AXVPBMULTI* multiVoice);
	sint32 AXGetMultiVoiceReformatBufferSize(sint32 voiceFormat, uint32 channelCount, uint32 sizeInBytes, uint32be* sizeOutput);
	void AXSetMultiVoiceType(AXVPBMULTI* mv, uint16 type);
	void AXSetMultiVoiceAdpcm(AXVPBMULTI* mv, AXDSPADPCM* data);
	void AXSetMultiVoiceSrcType(AXVPBMULTI* mv, uint32 type);
	void AXSetMultiVoiceOffsets(AXVPBMULTI* mv, AXPBOFFSET_t* offsets);
	void AXSetMultiVoiceVe(AXVPBMULTI* mv, AXPBVE* ve);
	void AXSetMultiVoiceSrcRatio(AXVPBMULTI* mv, float ratio);
	void AXSetMultiVoiceSrc(AXVPBMULTI* mv, AXPBSRC_t* src);
	void AXSetMultiVoiceLoop(AXVPBMULTI* mv, uint16 loop);
	void AXSetMultiVoiceState(AXVPBMULTI* mv, uint16 state);
	void AXSetMultiVoiceAdpcmLoop(AXVPBMULTI* mv, AXPBADPCMLOOP_t* loops);
	sint32 AXIsMultiVoiceRunning(AXVPBMULTI* mv);

	void AXOut_init();
	void AXOut_reset();
	void AXOut_update();

	COSModule* GetModuleSndCore1();
	COSModule* GetModuleSndCore2();
}