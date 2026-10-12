#pragma once

#define PERFORMANCE_MONITOR_TRACK_CYCLES	(5)

uint64 PPCTimer_getRawTsc();
uint64 PPCTimer_tscToMicroseconds(uint64 us);

class LattePerfStatTimer
{
public:
	void beginMeasuring()
	{
		timerStart = PPCTimer_getRawTsc();
	}

	void endMeasuring()
	{
		uint64 dif = PPCTimer_getRawTsc() - timerStart;
		currentSum += dif;
	}

	void frameFinished()
	{
		previousFrame = currentSum;
		currentSum = 0;
	}

	uint64 getPreviousFrameValue()
	{
		return previousFrame;
	}

private:
	uint64 currentSum{};
	uint64 previousFrame{};
	uint64 timerStart{};
};

class LattePerfStatCounter
{
public:
	void increment()
	{
		m_value++;
	}

	void decrement()
	{
		cemu_assert_debug(m_value > 0);
		m_value--;
	}

	void decrement(uint32 count)
	{
		cemu_assert_debug(count <= m_value);
		m_value -= count;
	}

	uint32 get()
	{
		return m_value;
	}

	void reset()
	{
		m_value = 0;
	}

private:
	std::atomic_uint32_t m_value{};
};

typedef struct
{
	struct
	{
		uint64 lastCycleCount;
		uint64 skippedCycles;
		uint32 recompilerLeaveCount;
		uint32 threadLeaveCount;
		uint32 lastUpdate;
		uint32 frameCounter;
		uint32 drawCallCounter;
		uint32 fastDrawCallCounter;
		uint32 shaderBindCount;
		uint64 vertexDataUploaded;
		uint64 vertexDataCached;
		uint64 uniformBankUploadedData;
		uint64 uniformBankUploadedCount;
		uint64 indexDataUploaded;
		uint64 indexDataCached;
	}cycle[PERFORMANCE_MONITOR_TRACK_CYCLES];
	sint32 cycleIndex;
	LattePerfStatTimer gpuTime_frameTime;
	LattePerfStatTimer gpuTime_shaderCreate;
	LattePerfStatTimer gpuTime_idleTime;
	LattePerfStatTimer gpuTime_fenceTime;

	LattePerfStatTimer gpuTime_dcStageTextures;
	LattePerfStatTimer gpuTime_dcStageVertexMgr;
	LattePerfStatTimer gpuTime_dcStageShaderAndUniformMgr;
	LattePerfStatTimer gpuTime_dcStageIndexMgr;
	LattePerfStatTimer gpuTime_dcStageMRT;

	LattePerfStatTimer gpuTime_dcStageDrawcallAPI;
	LattePerfStatTimer gpuTime_waitForAsync;

	uint32 numCompiledVS;
	uint32 numCompiledGS;
	uint32 numCompiledPS;

	struct
	{
		LattePerfStatCounter numDescriptorSets;
		LattePerfStatCounter numDescriptorDynUniformBuffers;
		LattePerfStatCounter numDescriptorStorageBuffers;
		LattePerfStatCounter numDescriptorSamplerTextures;
		LattePerfStatCounter numGraphicPipelines;
		LattePerfStatCounter numImages;
		LattePerfStatCounter numImageViews;
		LattePerfStatCounter numSamplers;
		LattePerfStatCounter numRenderPass;
		LattePerfStatCounter numFramebuffer;

		LattePerfStatCounter numDrawBarriersPerFrame;
		LattePerfStatCounter numBeginRenderpassPerFrame;
	}vk;

	struct
	{
		uint32 indexDataUploadPerFrame;
	}stats;
}performanceMonitor_t;

extern performanceMonitor_t performanceMonitor;

void LattePerformanceMonitor_frameEnd();
void LattePerformanceMonitor_frameBegin();

#define beginPerfMonProfiling(__obj) if( THasProfiling ) __obj.beginMeasuring()
#define endPerfMonProfiling(__obj) if( THasProfiling ) __obj.endMeasuring()
