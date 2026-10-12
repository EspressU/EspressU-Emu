#include "Cafe/OS/common/OSCommon.h"
#include "input/InputManager.h"
#include "audio/IAudioInputAPI.h"
#include "config/CemuConfig.h"
#include <algorithm>
#include <atomic>

enum class MIC_RESULT
{
	SUCCESS			=  0,
	BAD_PARAM		= -2,
	ALREADY_OPEN	= -5,
	NOT_OPEN		= -6,
	NOT_INITIALIZED	= -7,
	NOT_CONNECTED	= -8
};

#define MIC_SAMPLERATE				32000

const int MIC_SAMPLES_PER_3MS_32KHZ = (96);

enum class MIC_STATUS_FLAGS : uint32
{
	FORMAT_PCM16	= (1 << 0),
	IS_OPEN			= (1 << 1),
	IS_CONNECTED	= (1 << 2)
};
DEFINE_ENUM_FLAG_OPERATORS(MIC_STATUS_FLAGS);

#define MIC_HANDLE_DRC0				0
#define MIC_HANDLE_DRC1				1

enum class MIC_STATEID
{
	SAMPLERATE			= 0,
	GAIN_DB				= 1,
	GAIN_MIN			= 2,
	GAIN_MAX			= 3,
	GAIN_STEP			= 4,
	MUTE				= 5,
	ECHO_CANCELLATION	= 7,
	AUTO_SELECTION		= 8
};

struct
{
	struct
	{
		bool isInited;
		bool isOpen;
		void* ringbufferSampleData;
		uint32 ringbufferSize;
		uint32 readIndex;
		uint32 writeIndex;
		uint32 echoCancellation;
		uint32 autoSelection;
		uint32 gainDB;
	}drc[2];
}MICStatus = {0};

typedef struct
{
	uint32 size;
	MPTR samples;
}micRingbuffer_t;

struct micStatus_t
{
	betype<MIC_STATUS_FLAGS> flags;
	uint32be numSamplesAvailable;
	uint32be readIndex;
};

static_assert(sizeof(micStatus_t) == 0xC);

static std::atomic<bool> s_blowRequested{false};
static std::atomic<float> s_blowGain{0.0f};

void mic_setBlow(bool isBlowing)
{
	s_blowRequested.store(isBlowing, std::memory_order_relaxed);
}

bool mic_isBlowing()
{
	return s_blowRequested.load(std::memory_order_relaxed);
}

bool mic_isConnected(uint32 drcIndex)
{
	if( drcIndex != 0 )
		return false;

#if !BOOST_OS_IOS
	if (!GetConfig().microphone_enabled)
		return false;
#endif

	return InputManager::instance().get_vpad_controller(drcIndex) != nullptr;
}

sint32 mic_availableSamples(uint32 drcIndex)
{
	return (MICStatus.drc[drcIndex].ringbufferSize+MICStatus.drc[drcIndex].writeIndex-MICStatus.drc[drcIndex].readIndex) % MICStatus.drc[drcIndex].ringbufferSize;
}

bool mic_isActive(uint32 drcIndex)
{
	return MICStatus.drc[drcIndex].isOpen;
}

void mic_feedSamples(uint32 drcIndex, sint16* sampleData, sint32 numSamples)
{
	uint16* sampleDataU16 = (uint16*)sampleData;
	sint32 ringBufferSize = MICStatus.drc[0].ringbufferSize;
	sint32 writeIndex = MICStatus.drc[0].writeIndex;
	uint16* ringBufferBase = (uint16*)MICStatus.drc[0].ringbufferSampleData;
	do
	{
		ringBufferBase[writeIndex] = _swapEndianU16(*sampleDataU16);
		sampleDataU16++;
		writeIndex++;
		writeIndex %= ringBufferSize;
	}while( (--numSamples) > 0 );
	MICStatus.drc[0].writeIndex = writeIndex;
}

void micExport_MICInit(PPCInterpreter_t* hCPU)
{
	debug_printf("MICInit(%d,%d,0x%08x,0x%08x)\n", hCPU->gpr[3], hCPU->gpr[4], hCPU->gpr[5], hCPU->gpr[6]);
	uint32 drcIndex = hCPU->gpr[3];
	if( drcIndex > 1 || (drcIndex == 0 && mic_isConnected(drcIndex) == false) )
	{
		memory_writeU32(hCPU->gpr[6], (uint32)MIC_RESULT::NOT_CONNECTED);
		osLib_returnFromFunction(hCPU, -1);
		return;
	}
	if( drcIndex != 0 )
	{
		memory_writeU32(hCPU->gpr[6], (uint32)MIC_RESULT::NOT_CONNECTED);
		osLib_returnFromFunction(hCPU, -1);
		return;
	}
	if( MICStatus.drc[drcIndex].isInited )
	{
		memory_writeU32(hCPU->gpr[6], (uint32)MIC_RESULT::ALREADY_OPEN);
		osLib_returnFromFunction(hCPU, -1);
		return;
	}
	micRingbuffer_t* micRingbuffer = (micRingbuffer_t*)memory_getPointerFromVirtualOffset(hCPU->gpr[5]);
	MICStatus.drc[drcIndex].ringbufferSampleData = memory_getPointerFromVirtualOffset(_swapEndianU32(micRingbuffer->samples));
	MICStatus.drc[drcIndex].ringbufferSize = _swapEndianU32(micRingbuffer->size);
	MICStatus.drc[drcIndex].readIndex = 0;
	MICStatus.drc[drcIndex].writeIndex = 0;
	MICStatus.drc[drcIndex].isInited = true;
	MICStatus.drc[drcIndex].echoCancellation = 1;
	MICStatus.drc[drcIndex].autoSelection = 1;
	memory_writeU32(hCPU->gpr[6], 0);
	osLib_returnFromFunction(hCPU, (drcIndex==0)?MIC_HANDLE_DRC0:MIC_HANDLE_DRC1);

	auto& config = GetConfig();
#if BOOST_OS_IOS
	const auto audio_api = IAudioInputAPI::IOSAudio;
#else
	const auto audio_api = IAudioInputAPI::Cubeb;
#endif

#if BOOST_OS_IOS
	const bool useRealMic = config.microphone_enabled;
#else
	const bool useRealMic = true;
#endif

	std::unique_lock lock(g_audioInputMutex);
	if (useRealMic && !g_inputAudio)
	{
		IAudioInputAPI::DeviceDescriptionPtr device_description;
		if (IAudioInputAPI::IsAudioInputAPIAvailable(audio_api))
		{
			auto devices = IAudioInputAPI::GetDevices(audio_api);
			const auto it = std::find_if(devices.begin(), devices.end(), [&config](const auto& d) {return d->GetIdentifier() == config.input_device; });
			if (it != devices.end())
				device_description = *it;
			else if (!devices.empty())
				device_description = devices.front();
		}

		if (device_description)
		{
			try
			{
				g_inputAudio = IAudioInputAPI::CreateDevice(audio_api, device_description, MIC_SAMPLERATE, 1, MIC_SAMPLES_PER_3MS_32KHZ, 16);
				g_inputAudio->SetVolume(config.input_volume);
			}
			catch (std::runtime_error& ex)
			{
				cemuLog_log(LogType::Force, "can't initialize audio input: {}", ex.what());
#if BOOST_OS_IOS
				g_inputAudio.reset();
#else
				exit(0);
#endif
			}
		}
	}
}

void micExport_MICOpen(PPCInterpreter_t* hCPU)
{
	debug_printf("MICOpen(%d)\n", hCPU->gpr[3]);
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	if( MICStatus.drc[drcIndex].isOpen )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::ALREADY_OPEN);
		return;
	}
	bool hasDRCConnected = InputManager::instance().get_vpad_controller(drcIndex) != nullptr;
	if( hasDRCConnected == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_CONNECTED);
		return;
	}
	MICStatus.drc[drcIndex].isOpen = true;
	osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::SUCCESS);

	std::shared_lock lock(g_audioInputMutex);
	if(g_inputAudio)
		g_inputAudio->Play();
}

void micExport_MICClose(PPCInterpreter_t* hCPU)
{
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	if( MICStatus.drc[drcIndex].isOpen == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::ALREADY_OPEN);
		return;
	}
	MICStatus.drc[drcIndex].isOpen = false;
	osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::SUCCESS);

	std::shared_lock lock(g_audioInputMutex);
	if (g_inputAudio)
		g_inputAudio->Stop();
}

void micExport_MICGetStatus(PPCInterpreter_t* hCPU)
{
	debug_printf("MICGetStatus(%d,0x%08x)\n", hCPU->gpr[3], hCPU->gpr[4]);
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	micStatus_t* micStatus = (micStatus_t*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
	MIC_STATUS_FLAGS micFlags = MIC_STATUS_FLAGS::FORMAT_PCM16;
	if( mic_isConnected(drcIndex) )
		micFlags |= MIC_STATUS_FLAGS::IS_CONNECTED;
	if( MICStatus.drc[drcIndex].isOpen )
		micFlags |= MIC_STATUS_FLAGS::IS_OPEN;
	micStatus->flags = micFlags;
	micStatus->numSamplesAvailable = mic_availableSamples(drcIndex);
	micStatus->readIndex = MICStatus.drc[drcIndex].readIndex;
	osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::SUCCESS);
}

void micExport_MICGetState(PPCInterpreter_t* hCPU)
{
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	MIC_STATEID stateId = (MIC_STATEID)hCPU->gpr[4];
	if (stateId == MIC_STATEID::SAMPLERATE)
	{
		memory_writeU32(hCPU->gpr[5], MIC_SAMPLERATE);
	}
	else if (stateId == MIC_STATEID::AUTO_SELECTION)
	{
		memory_writeU32(hCPU->gpr[5], MICStatus.drc[drcIndex].autoSelection);
	}
	else if (stateId == MIC_STATEID::GAIN_MIN)
	{
		memory_writeU32(hCPU->gpr[5], 0x0000);
	}
	else if (stateId == MIC_STATEID::GAIN_MAX)
	{
		memory_writeU32(hCPU->gpr[5], 0x0200);
	}
	else if (stateId == MIC_STATEID::GAIN_STEP)
	{
		memory_writeU32(hCPU->gpr[5], 0x0001);
	}
	else if (stateId == MIC_STATEID::ECHO_CANCELLATION)
	{
		memory_writeU32(hCPU->gpr[5], MICStatus.drc[drcIndex].echoCancellation);
	}
	else if (stateId == MIC_STATEID::GAIN_DB)
	{
		memory_writeU32(hCPU->gpr[5], MICStatus.drc[drcIndex].gainDB);
	}
	else
		cemu_assert_unimplemented();
	osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::SUCCESS);
	return;
}

void micExport_MICSetState(PPCInterpreter_t* hCPU)
{
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	MIC_STATEID stateId = (MIC_STATEID)hCPU->gpr[4];
	uint32 newValue = hCPU->gpr[5];
	if( stateId == MIC_STATEID::ECHO_CANCELLATION )
	{
		MICStatus.drc[drcIndex].echoCancellation = (newValue!=0)?1:0;
	}
	else if( stateId == MIC_STATEID::AUTO_SELECTION )
	{
		MICStatus.drc[drcIndex].autoSelection = (newValue!=0)?1:0;
	}
	else if( stateId == MIC_STATEID::GAIN_DB )
	{
		MICStatus.drc[drcIndex].gainDB = newValue;
	}
	else
		assert_dbg();
	osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::SUCCESS);
	return;
}

void micExport_MICSetDataConsumed(PPCInterpreter_t* hCPU)
{
	uint32 micHandle = hCPU->gpr[3];
	if( micHandle != MIC_HANDLE_DRC0 && micHandle != MIC_HANDLE_DRC1 )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::BAD_PARAM);
		return;
	}
	uint32 drcIndex = (micHandle==MIC_HANDLE_DRC0)?0:1;
	if( MICStatus.drc[drcIndex].isInited == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_INITIALIZED);
		return;
	}
	if( MICStatus.drc[drcIndex].isOpen == false )
	{
		osLib_returnFromFunction(hCPU, (uint32)MIC_RESULT::NOT_OPEN);
		return;
	}
	sint32 numConsumedSamples = (sint32)hCPU->gpr[4];
	if( mic_availableSamples(drcIndex) < numConsumedSamples )
	{
		MICStatus.drc[drcIndex].readIndex = MICStatus.drc[drcIndex].writeIndex;
		osLib_returnFromFunction(hCPU, -81);
	}
	else
	{
		MICStatus.drc[drcIndex].readIndex += numConsumedSamples;
		MICStatus.drc[drcIndex].readIndex %= MICStatus.drc[drcIndex].ringbufferSize;
		osLib_returnFromFunction(hCPU, 0);
	}
	return;
}

void mic_updateDevicePlayState(bool isPlaying)
{
	if (g_inputAudio)
	{
		if (isPlaying)
			g_inputAudio->Play();
		else
			g_inputAudio->Stop();
	}
}

static bool mic_mixBlowNoise(sint16* data, sint32 numSamples, bool blowing)
{
	float gain = s_blowGain.load(std::memory_order_relaxed);
	if (!blowing && gain <= 0.0f)
		return false;

	static uint32 rng = 0x2545F491u;
	static float lowpass = 0.0f;
	const float target = blowing ? 1.0f : 0.0f;
	const float rampStep = 1.0f / (MIC_SAMPLERATE * 0.02f);
	for (sint32 i = 0; i < numSamples; i++)
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		const float white = (float)(sint32)rng * (1.0f / 2147483648.0f);
		lowpass += 0.35f * (white - lowpass);
		if (gain < target)
			gain = std::min(gain + rampStep, target);
		else if (gain > target)
			gain = std::max(gain - rampStep, target);
		const float v = std::clamp(lowpass * gain * 1.6f, -0.95f, 0.95f);
		data[i] = (sint16)(v * 32767.0f);
	}
	s_blowGain.store(gain, std::memory_order_relaxed);
	return true;
}

void mic_updateOnAXFrame()
{
	sint32 drcIndex = 0;
	if( mic_isActive(0) == false )
	{
		drcIndex = 1;
		if (mic_isActive(1) == false)
		{
			std::shared_lock lock(g_audioInputMutex);
			mic_updateDevicePlayState(false);
			return;
		}
	}

	std::shared_lock lock(g_audioInputMutex);
	mic_updateDevicePlayState(true);

	sint16 micSampleData[MIC_SAMPLES_PER_3MS_32KHZ];
	if (g_inputAudio)
		g_inputAudio->ConsumeBlock(micSampleData);
	else
		memset(micSampleData, 0x00, sizeof(micSampleData));

	bool blowing = mic_isBlowing();
	if (!blowing)
	{
		auto controller = InputManager::instance().get_vpad_controller(drcIndex);
		blowing = controller && controller->is_mic_active();
	}
	mic_mixBlowNoise(micSampleData, MIC_SAMPLES_PER_3MS_32KHZ, blowing);

	mic_feedSamples(0, micSampleData, MIC_SAMPLES_PER_3MS_32KHZ);
}

static void mic_resetForTitleStop()
{
	s_blowRequested.store(false, std::memory_order_relaxed);
	s_blowGain.store(0.0f, std::memory_order_relaxed);
	std::unique_lock lock(g_audioInputMutex);
	memset(&MICStatus, 0, sizeof(MICStatus));
	if (g_inputAudio)
	{
		g_inputAudio->Stop();
		g_inputAudio.reset();
	}
}

namespace mic
{
	class : public COSModule
	{
		public:
		std::string_view GetName() override
		{
			return "mic";
		}

		void RPLMapped() override
		{
			osLib_addFunction("mic", "MICInit", micExport_MICInit);
			osLib_addFunction("mic", "MICOpen", micExport_MICOpen);
			osLib_addFunction("mic", "MICClose", micExport_MICClose);
			osLib_addFunction("mic", "MICGetStatus", micExport_MICGetStatus);
			osLib_addFunction("mic", "MICGetState", micExport_MICGetState);
			osLib_addFunction("mic", "MICSetState", micExport_MICSetState);
			osLib_addFunction("mic", "MICSetDataConsumed", micExport_MICSetDataConsumed);
		}

		void RPLUnmapped() override
		{
			mic_resetForTitleStop();
		}
	}s_COSmicModule;

	COSModule* GetModule()
	{
		return &s_COSmicModule;
	}

};
