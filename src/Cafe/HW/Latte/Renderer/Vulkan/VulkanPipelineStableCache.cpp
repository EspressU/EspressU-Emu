#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineCompiler.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineStableCache.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LattePerformanceMonitor.h"
#include "Cafe/HW/Latte/Core/LatteCachedFBO.h"
#include "Cafe/OS/libs/gx2/GX2.h"
#include "config/ActiveSettings.h"
#include "util/helpers/Serializer.h"
#include "Cafe/HW/Latte/Common/RegisterSerializer.h"
#include "Cemu/FileCache/FileCache.h"
#include "Cafe/HW/Latte/Core/LatteShaderCache.h"
#include "util/helpers/helpers.h"
#include <openssl/sha.h>

struct
{
	uint32 pipelineLoadIndex;
	uint32 pipelineMaxFileIndex;

	std::atomic_uint32_t pipelinesQueued;
	std::atomic_uint32_t pipelinesLoaded;
}g_vkCacheState;

VulkanPipelineStableCache g_vkPipelineStableCacheInstance;

static std::vector<LatteShaderCachePipelineRekey> s_pendingRekeys;

static std::mutex s_cacheLifetimeMutex;

VulkanPipelineStableCache& VulkanPipelineStableCache::GetInstance()
{
	return g_vkPipelineStableCacheInstance;
}

uint32 VulkanPipelineStableCache::BeginLoading(uint64 cacheTitleId)
{
	std::error_code ec;
	fs::create_directories(ActiveSettings::GetCachePath("shaderCache/transferable"), ec);
	const auto pathCacheFile = ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_vkpipeline.bin", cacheTitleId);

	g_vkCacheState.pipelineLoadIndex = 0;
	g_vkCacheState.pipelineMaxFileIndex = 0;
	g_vkCacheState.pipelinesLoaded = 0;
	g_vkCacheState.pipelinesQueued = 0;
	s_pendingRekeys.clear();

	m_compilationCount.store(0);
	m_compilationQueue.clear();

	uint32 cpuCoreCount = GetPhysicalCoreCount();
	m_numCompilationThreads = std::clamp(cpuCoreCount, 1u, 8u);
	if (VulkanRenderer::GetInstance()->GetDisableMultithreadedCompilation())
		m_numCompilationThreads = 1;

	for (uint32 i = 0; i < m_numCompilationThreads; i++)
	{
		std::thread compileThread(&VulkanPipelineStableCache::CompilerThread, this);
		compileThread.detach();
	}

	cemu_assert_debug(s_cache == nullptr);
	s_cache = FileCache::Open(pathCacheFile, true, LatteShaderCache_getPipelineCacheExtraVersion(cacheTitleId));
	if (!s_cache)
	{
		cemuLog_log(LogType::Force, "Failed to open or create Vulkan pipeline cache file: {}", _pathToUtf8(pathCacheFile));
		return 0;
	}
	else
	{
		s_cache->UseCompression(false);
		g_vkCacheState.pipelineMaxFileIndex = s_cache->GetMaximumFileIndex();
	}
	return s_cache->GetFileCount();
}

bool VulkanPipelineStableCache::UpdateLoading(uint32& pipelinesLoadedTotal, uint32& pipelinesMissingShaders)
{
	pipelinesLoadedTotal = g_vkCacheState.pipelinesLoaded;
	pipelinesMissingShaders = 0;
	while (g_vkCacheState.pipelineLoadIndex <= g_vkCacheState.pipelineMaxFileIndex)
	{
		if (m_compilationQueue.size() >= 50)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			return true;
		}

		uint64 fileNameA, fileNameB;
		std::vector<uint8> fileData;
		if (s_cache->GetFileByIndex(g_vkCacheState.pipelineLoadIndex, &fileNameA, &fileNameB, fileData))
		{
			LatteShaderCache_TranslatePipelineEntry(fileNameA, fileNameB, fileData, s_pendingRekeys);
			g_vkCacheState.pipelinesQueued++;
			m_compilationQueue.push(std::move(fileData));
			g_vkCacheState.pipelineLoadIndex++;
			return true;
		}
		g_vkCacheState.pipelineLoadIndex++;
	}
	if (g_vkCacheState.pipelinesLoaded != g_vkCacheState.pipelinesQueued)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		return true;
	}
	LatteShaderCache_ApplyPipelineRekeys(s_cache, s_pendingRekeys);
	return false;
}

void VulkanPipelineStableCache::EndLoading()
{
	uint32 threadCount = m_numCompilationThreads;
	m_numCompilationThreads = 0;
	for (uint32 i = 0; i < threadCount; i++)
	{
		m_compilationQueue.push({});
	}
}

void VulkanPipelineStableCache::Close()
{
    {
        std::lock_guard lock(s_cacheLifetimeMutex);
        if(s_cache)
        {
            delete s_cache;
            s_cache = nullptr;
        }
    }
    m_pipelineIsCachedLock.lock();
    m_pipelineIsCached.clear();
    m_pipelineIsCachedLock.unlock();
}

struct CachedPipeline
{
	struct ShaderHash
	{
		uint64 baseHash;
		uint64 auxHash;
		bool isPresent{};

		void set(uint64 baseHash, uint64 auxHash)
		{
			this->baseHash = baseHash;
			this->auxHash = auxHash;
			this->isPresent = true;
		}
	};

	ShaderHash vsHash;
	ShaderHash gsHash;
	ShaderHash psHash;

	Latte::GPUCompactedRegisterState gpuState;
};

VkFormat __getColorBufferVkFormat(const uint32 index, const LatteContextRegister& lcr)
{
	Latte::E_GX2SURFFMT colorBufferFormat = LatteMRT::GetColorBufferFormat(index, lcr);
	VulkanRenderer::FormatInfoVK texFormatInfo;
	VulkanRenderer::GetInstance()->GetTextureFormatInfoVK(colorBufferFormat, false, Latte::E_DIM::DIM_2D, 1280, 720, &texFormatInfo);
	return texFormatInfo.vkImageFormat;
}

void __getDepthBufferVkFormat(const LatteContextRegister& lcr, VkFormat& dbFormat, bool& hasStencil)
{
	Latte::E_GX2SURFFMT format = LatteMRT::GetDepthBufferFormat(lcr);
	VulkanRenderer::FormatInfoVK texFormatInfo;
	VulkanRenderer::GetInstance()->GetTextureFormatInfoVK(format, true, Latte::E_DIM::DIM_2D, 1280, 720, &texFormatInfo);
	dbFormat = texFormatInfo.vkImageFormat;
	hasStencil = (texFormatInfo.vkImageAspect & VK_IMAGE_ASPECT_STENCIL_BIT) != 0;
}

VKRObjectRenderPass* __CreateTemporaryRenderPass(const LatteDecompilerShader* pixelShader, const LatteContextRegister& lcr)
{
	VKRObjectRenderPass::AttachmentInfo_t attachmentInfo;

	uint8 cbMask = LatteMRT::GetActiveColorBufferMask(pixelShader, lcr);
	bool dbMask = LatteMRT::GetActiveDepthBufferMask(lcr);

	for (int i = 0; i < 8; ++i)
	{
		if ((cbMask & (1 << i)) == 0)
		{
			attachmentInfo.colorAttachment[i].viewObj = nullptr;
			continue;
		}
		attachmentInfo.colorAttachment[i].viewObj = nullptr;
		attachmentInfo.colorAttachment[i].isPresent = true;
		attachmentInfo.colorAttachment[i].format = __getColorBufferVkFormat(i, lcr);
	}

	if (dbMask)
	{
		attachmentInfo.depthAttachment.viewObj = nullptr;
		attachmentInfo.depthAttachment.isPresent = true;
		VkFormat dbFormat;
		bool hasStencil;
		__getDepthBufferVkFormat(lcr, dbFormat, hasStencil);
		attachmentInfo.depthAttachment.format = dbFormat;
		attachmentInfo.depthAttachment.hasStencil = hasStencil;
	}
	else
	{
		attachmentInfo.depthAttachment.viewObj = nullptr;
		attachmentInfo.depthAttachment.isPresent = false;
	}

	return new VKRObjectRenderPass(attachmentInfo);
}

void VulkanPipelineStableCache::LoadPipelineFromCache(std::span<uint8> fileData)
{
	static FSpinlock s_spinlockSharedInternal;

	LatteContextRegister* lcr = new LatteContextRegister();
	s_spinlockSharedInternal.lock();
	CachedPipeline* cachedPipeline = new CachedPipeline();
	s_spinlockSharedInternal.unlock();

	MemStreamReader streamReader(fileData.data(), fileData.size());
	if (!DeserializePipeline(streamReader, *cachedPipeline))
	{
		s_spinlockSharedInternal.lock();
		delete lcr;
		delete cachedPipeline;
		s_spinlockSharedInternal.unlock();
		return;
	}
	Latte::LoadGPURegisterState(*lcr, cachedPipeline->gpuState);

	LatteDecompilerShader* vertexShader = nullptr;
	LatteDecompilerShader* geometryShader = nullptr;
	LatteDecompilerShader* pixelShader = nullptr;
	if (cachedPipeline->vsHash.isPresent)
	{
		vertexShader = LatteSHRC_FindVertexShader(cachedPipeline->vsHash.baseHash, cachedPipeline->vsHash.auxHash);
		if (!vertexShader)
		{
			cemuLog_logDebug(LogType::Force, "Vertex shader not found in cache");
			return;
		}
	}
	if (cachedPipeline->gsHash.isPresent)
	{
		geometryShader = LatteSHRC_FindGeometryShader(cachedPipeline->gsHash.baseHash, cachedPipeline->gsHash.auxHash);
		if (!geometryShader)
		{
			cemuLog_logDebug(LogType::Force, "Geometry shader not found in cache");
			return;
		}
	}
	if (cachedPipeline->psHash.isPresent)
	{
		pixelShader = LatteSHRC_FindPixelShader(cachedPipeline->psHash.baseHash, cachedPipeline->psHash.auxHash);
		if (!pixelShader)
		{
			cemuLog_logDebug(LogType::Force, "Pixel shader not found in cache");
			return;
		}
	}
	if (!pixelShader)
	{
		cemu_assert_debug(false);
		return;
	}
	auto renderPass = __CreateTemporaryRenderPass(pixelShader, *lcr);
	m_pipelineIsCachedLock.lock();
	PipelineInfo* pipelineInfo = new PipelineInfo(0, 0, vertexShader->compatibleFetchShader, vertexShader, pixelShader, geometryShader);
	m_pipelineIsCachedLock.unlock();
	{
		PipelineCompiler pipelineCompiler;
		bool requiresRobustBufferAccess = PipelineCompiler::CalcRobustBufferAccessRequirement(vertexShader, pixelShader, geometryShader);
		if (!pipelineCompiler.InitFromCurrentGPUState(pipelineInfo, *lcr, renderPass, requiresRobustBufferAccess))
		{
			s_spinlockSharedInternal.lock();
			delete lcr;
			delete cachedPipeline;
			s_spinlockSharedInternal.unlock();
			return;
		}
		pipelineCompiler.Compile(true, true, false);
	}
	uint64 pipelineBaseHash = vertexShader->baseHash;
	uint64 pipelineStateHash = VulkanRenderer::draw_calculateGraphicsPipelineHash(vertexShader->compatibleFetchShader, vertexShader, geometryShader, pixelShader, renderPass, *lcr);
	m_pipelineIsCachedLock.lock();
	m_pipelineIsCached.emplace(pipelineBaseHash, pipelineStateHash);
	m_pipelineIsCachedLock.unlock();
	s_spinlockSharedInternal.lock();
	delete pipelineInfo;
	delete lcr;
	delete cachedPipeline;
	VulkanRenderer::GetInstance()->ReleaseDestructibleObject(renderPass);
	s_spinlockSharedInternal.unlock();
}

bool VulkanPipelineStableCache::HasPipelineCached(uint64 baseHash, uint64 pipelineStateHash)
{
	PipelineHash ph(baseHash, pipelineStateHash);
	return m_pipelineIsCached.find(ph) != m_pipelineIsCached.end();
}

ConcurrentQueue<CachedPipeline*> g_pipelineCachingQueue;

void VulkanPipelineStableCache::AddCurrentStateToCache(uint64 baseHash, uint64 pipelineStateHash)
{
	m_pipelineIsCached.emplace(baseHash, pipelineStateHash);
	if (!m_pipelineCacheStoreThread)
	{
		m_pipelineCacheStoreThread = new std::thread(&VulkanPipelineStableCache::WorkerThread, this);
		m_pipelineCacheStoreThread->detach();
	}
	CachedPipeline* job = new CachedPipeline();
	auto vs = LatteSHRC_GetActiveVertexShader();
	auto gs = LatteSHRC_GetActiveGeometryShader();
	auto ps = LatteSHRC_GetActivePixelShader();
	if (vs)
		job->vsHash.set(vs->baseHash, vs->auxHash);
	if (gs)
		job->gsHash.set(gs->baseHash, gs->auxHash);
	if (ps)
		job->psHash.set(ps->baseHash, ps->auxHash);
	Latte::StoreGPURegisterState(LatteGPUState.contextNew, job->gpuState);
	g_pipelineCachingQueue.push(job);
}

bool VulkanPipelineStableCache::SerializePipeline(MemStreamWriter& memWriter, CachedPipeline& cachedPipeline)
{
	memWriter.writeBE<uint8>(0x01);
	uint8 presentMask = 0;
	if (cachedPipeline.vsHash.isPresent)
		presentMask |= 1;
	if (cachedPipeline.gsHash.isPresent)
		presentMask |= 2;
	if (cachedPipeline.psHash.isPresent)
		presentMask |= 4;
	memWriter.writeBE<uint8>(presentMask);
	if (cachedPipeline.vsHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.vsHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.vsHash.auxHash);
	}
	if (cachedPipeline.gsHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.gsHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.gsHash.auxHash);
	}
	if (cachedPipeline.psHash.isPresent)
	{
		memWriter.writeBE<uint64>(cachedPipeline.psHash.baseHash);
		memWriter.writeBE<uint64>(cachedPipeline.psHash.auxHash);
	}
	Latte::SerializeRegisterState(cachedPipeline.gpuState, memWriter);
	return true;
}

bool VulkanPipelineStableCache::DeserializePipeline(MemStreamReader& memReader, CachedPipeline& cachedPipeline)
{
	if (memReader.readBE<uint8>() != 1)
	{
		cemuLog_log(LogType::Force, "Cached Vulkan pipeline corrupted or has unknown version");
		return false;
	}
	uint8 presentMask = memReader.readBE<uint8>();
	if (presentMask & 1)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.vsHash.set(baseHash, auxHash);
	}
	if (presentMask & 2)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.gsHash.set(baseHash, auxHash);
	}
	if (presentMask & 4)
	{
		uint64 baseHash = memReader.readBE<uint64>();
		uint64 auxHash = memReader.readBE<uint64>();
		cachedPipeline.psHash.set(baseHash, auxHash);
	}
	if (!Latte::DeserializeRegisterState(cachedPipeline.gpuState, memReader))
	{
		return false;
	}
	cemu_assert_debug(!memReader.hasError());
	return true;
}

int VulkanPipelineStableCache::CompilerThread()
{
	SetThreadName("plCacheCompiler");
	while (m_numCompilationThreads != 0)
	{
		std::vector<uint8> pipelineData = m_compilationQueue.pop();
		if(pipelineData.empty())
			continue;
		LoadPipelineFromCache(pipelineData);
		++g_vkCacheState.pipelinesLoaded;
	}
	return 0;
}

void VulkanPipelineStableCache::WorkerThread()
{
	SetThreadName("plCacheWriter");
	while (true)
	{
		CachedPipeline* job;
		g_pipelineCachingQueue.pop(job);
		{
			std::lock_guard lock(s_cacheLifetimeMutex);
			if (!s_cache)
			{
				delete job;
				continue;
			}
		}
		MemStreamWriter memWriter(1024 * 4);
		SerializePipeline(memWriter, *job);
		auto blob = memWriter.getResult();
		uint8 hash[SHA256_DIGEST_LENGTH];
		SHA256(blob.data(), blob.size(), hash);
		uint64 nameA = *(uint64be*)(hash + 0);
		uint64 nameB = *(uint64be*)(hash + 8);
		{
			std::lock_guard lock(s_cacheLifetimeMutex);
			if (s_cache)
				s_cache->AddFileAsync({ nameA, nameB }, blob.data(), blob.size());
		}
		delete job;
	}
}
