#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#if BOOST_OS_IOS
#include <os/proc.h>
#include <sys/sysctl.h>
#endif
#include "Cafe/HW/Latte/Renderer/Metal/MetalVoidVertexPipeline.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalMemoryManager.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureViewMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/RendererShaderMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/CachedFBOMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalOutputShaderCache.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCache.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalDepthStencilCache.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalSamplerCache.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureReadbackMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalQuery.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/UtilityShaderSource.h"

#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteTextureLoader.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Core/LatteIndices.h"
#include "Cafe/HW/Latte/Core/LatteBufferCache.h"
#include "CafeSystem.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "config/CemuConfig.h"
#include "WindowSystem.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define IMGUI_IMPL_METAL_CPP
#include "imgui/imgui_extension.h"
#include "imgui/imgui_impl_metal.h"

#define EVENT_VALUE_WRAP 4096

#if ESPRESSU_AUDIT_HOOKS
bool IOSAuditMetal_WantsCapture(bool tv);
void IOSAuditMetal_CaptureView(MetalRenderer* renderer, LatteTextureView* texView, bool padView);
#endif

extern bool hasValidFramebufferAttached;

float supportBufferData[512 * 4];

void LatteDraw_handleSpecialState8_clearAsDepth();

enum class MetalGuard : uint32
{
    UploadNoTarget,
    UploadPlaceholder,
    UploadBadLevel,
    UploadBadSize,
    UploadTooFewBytes,
    UploadDepthStencilBytes,
    UploadStagingFull,
    UploadClampedSize,
    UploadClampedRows,
    CopyBadLevel,
    CopyStartOutside,
    CopyNoSlices,
    CopyClampedRegion,
    CopyClampedSlices,
    CopyBlockMismatch,
    CopyTranscodedBlocks,
    DrawVertexBuffer,
    DrawVertexHuge,
    IndexClamped,
    ScissorClamped,
    PresentScissorClamped,
    SurfaceCopyScissorClamped,
    UploadRetryStopped,
    CopyBytesPerBlockMismatch,
    CopyRetranscoded,
    Count
};

namespace
{
    struct MetalGuardInfo
    {
        const char* name;
        const char* kind;
    };
    constexpr MetalGuardInfo kMetalGuardInfo[] = {
        {"upload-no-target", "skip"},
        {"upload-placeholder-target", "skip"},
        {"upload-bad-level", "skip"},
        {"upload-bad-size", "skip"},
        {"upload-too-few-bytes", "skip"},
        {"upload-depth-stencil-bytes", "skip"},
        {"upload-staging-full", "skip"},
        {"upload-size-clamped", "clamp"},
        {"upload-rows-clamped", "clamp"},
        {"copy-bad-level", "skip"},
        {"copy-start-outside", "skip"},
        {"copy-no-slices", "skip"},
        {"copy-region-clamped", "clamp"},
        {"copy-slices-clamped", "clamp"},
        {"copy-block-size-mismatch", "note"},
        {"copy-transcoded-blocks", "skip"},
        {"draw-vertex-buffer", "skip"},
        {"draw-vertex-range-huge", "skip"},
        {"draw-index-count-clamped", "clamp"},
        {"scissor-clamped", "clamp"},
        {"present-scissor-clamped", "clamp"},
        {"surface-copy-scissor-clamped", "clamp"},
        {"upload-retry-stopped", "note"},
        {"copy-bytes-per-block-mismatch", "skip"},
        {"copy-blocks-retranscoded", "note"},
    };
    constexpr uint32 kMetalGuardCount = (uint32)MetalGuard::Count;
    static_assert(std::size(kMetalGuardInfo) == kMetalGuardCount, "MetalGuard names out of step with the enum");

    struct MetalGuardEntry
    {
        MetalGuard reason;
        uint64 count;
        std::string text;
    };

    std::mutex s_guardMutex;
    std::unordered_map<uint64, MetalGuardEntry> s_guardEntries;
    std::atomic<uint64> s_guardTotals[kMetalGuardCount];
    std::atomic<uint64> s_guardRetriesScheduled{0};
    std::atomic<uint64> s_guardRetryStops{0};
    uint64 s_guardOverflow = 0;
    uint64 s_guardSuppressed = 0;
    double s_guardTokens = 40.0;
    std::chrono::steady_clock::time_point s_guardLastRefill{};
    constexpr size_t kMetalGuardMaxDistinct = 1024;

    template<typename Describe>
    void MetalGuardNote(MetalGuard reason, std::initializer_list<uint64> key, Describe&& describe)
    {
        s_guardTotals[(uint32)reason].fetch_add(1, std::memory_order_relaxed);
        uint64 h = 0xcbf29ce484222325ull ^ ((uint64)reason * 0x9e3779b97f4a7c15ull);
        for (uint64 v : key)
        {
            h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
            h *= 0x100000001b3ull;
        }

        std::lock_guard<std::mutex> lock(s_guardMutex);
        auto it = s_guardEntries.find(h);
        if (it == s_guardEntries.end())
        {
            if (s_guardEntries.size() >= kMetalGuardMaxDistinct)
            {
                ++s_guardOverflow;
                return;
            }
            it = s_guardEntries.emplace(h, MetalGuardEntry{reason, 0, describe()}).first;
        }
        const uint64 n = ++it->second.count;
        uint64 decade = n;
        while (decade % 10 == 0)
            decade /= 10;
        if (decade != 1)
            return;

        const auto now = std::chrono::steady_clock::now();
        if (s_guardLastRefill.time_since_epoch().count() != 0)
        {
            const double seconds = std::chrono::duration<double>(now - s_guardLastRefill).count();
            s_guardTokens = std::min(40.0, s_guardTokens + seconds * 5.0);
        }
        s_guardLastRefill = now;
        if (s_guardTokens < 1.0)
        {
            ++s_guardSuppressed;
            return;
        }
        s_guardTokens -= 1.0;
        const auto& info = kMetalGuardInfo[(uint32)reason];
        cemuLog_log(LogType::Force, "Metal guard [{} {}] {} (seen {})", info.kind, info.name, it->second.text, n);
    }

    std::string MetalGuardTotalsLine()
    {
        std::string line;
        for (uint32 i = 0; i < kMetalGuardCount; ++i)
        {
            const uint64 total = s_guardTotals[i].load(std::memory_order_relaxed);
            if (total != 0)
                line += fmt::format(" {}={}", kMetalGuardInfo[i].name, total);
        }
        return line.empty() ? std::string(" none") : line;
    }

    void MetalGuardReport(const char* when, size_t maxCases, bool reset)
    {
        std::vector<std::pair<uint64, std::string>> cases;
        uint64 suppressed, overflow;
        {
            std::lock_guard<std::mutex> lock(s_guardMutex);
            cases.reserve(s_guardEntries.size());
            for (const auto& kv : s_guardEntries)
            {
                const auto& info = kMetalGuardInfo[(uint32)kv.second.reason];
                cases.emplace_back(kv.second.count, fmt::format("[{} {}] {}", info.kind, info.name, kv.second.text));
            }
            suppressed = s_guardSuppressed;
            overflow = s_guardOverflow;
            if (reset)
            {
                s_guardEntries.clear();
                s_guardSuppressed = 0;
                s_guardOverflow = 0;
                s_guardTokens = 40.0;
            }
        }
        std::sort(cases.begin(), cases.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        cemuLog_log(LogType::Force, "Metal guard summary ({}): {} distinct cases;{}", when, cases.size(), MetalGuardTotalsLine());
        cemuLog_log(LogType::Force, "Metal guard summary ({}): upload retries scheduled {}, retry-storm stops {}", when, s_guardRetriesScheduled.load(std::memory_order_relaxed), s_guardRetryStops.load(std::memory_order_relaxed));
        if (suppressed != 0 || overflow != 0)
            cemuLog_log(LogType::Force, "Metal guard summary ({}): {} log lines were withheld by the rate limit, {} events fell outside the {} case table", when, suppressed, overflow, kMetalGuardMaxDistinct);
        for (size_t i = 0; i < cases.size() && i < maxCases; ++i)
            cemuLog_log(LogType::Force, "Metal guard case x{}: {}", cases[i].first, cases[i].second);
        if (cases.size() > maxCases)
            cemuLog_log(LogType::Force, "Metal guard summary ({}): {} less frequent cases not listed", when, cases.size() - maxCases);
        if (reset)
        {
            for (auto& total : s_guardTotals)
                total.store(0, std::memory_order_relaxed);
            s_guardRetriesScheduled.store(0, std::memory_order_relaxed);
            s_guardRetryStops.store(0, std::memory_order_relaxed);
        }
    }

    bool MetalVertexGroupIsRead(const LatteParsedFetchShaderBufferGroup_t& group, const LatteDecompilerShader* vertexShader, uint32& usedEnd)
    {
        usedEnd = 0;
        bool used = false;
        for (sint32 j = 0; j < group.attribCount; ++j)
        {
            const auto& attr = group.attrib[j];
            if ((uint32)vertexShader->resourceMapping.attributeMapping[attr.semanticId] == (uint32)-1)
                continue;
            used = true;
            usedEnd = std::max<uint32>(usedEnd, attr.offset + GetMtlVertexFormatSize(attr.format));
        }
        return used;
    }

    bool MetalPixelFormatIsNativeBC(MTL::PixelFormat pixelFormat)
    {
        switch (pixelFormat)
        {
        case MTL::PixelFormatBC1_RGBA:
        case MTL::PixelFormatBC1_RGBA_sRGB:
        case MTL::PixelFormatBC2_RGBA:
        case MTL::PixelFormatBC2_RGBA_sRGB:
        case MTL::PixelFormatBC3_RGBA:
        case MTL::PixelFormatBC3_RGBA_sRGB:
        case MTL::PixelFormatBC4_RUnorm:
        case MTL::PixelFormatBC4_RSnorm:
        case MTL::PixelFormatBC5_RGUnorm:
        case MTL::PixelFormatBC5_RGSnorm:
            return true;
        default:
            return false;
        }
    }

    void MetalUploadSkipped(LatteTextureMtl* texture, MetalGuard reason, sint32 mipIndex)
    {
        constexpr uint32 kMaxFailedFrames = 8;
        constexpr uint32 kQuietFrames = 600;
        const uint32 frame = (uint32)LatteGPUState.frameCounter;
        const bool sameRun = texture->m_retryFailedFrames != 0 && texture->m_retryReason == (uint8)reason && (uint32)(frame - texture->m_retryLastFrame) <= kQuietFrames;
        if (!sameRun)
        {
            texture->m_retryReason = (uint8)reason;
            texture->m_retryFailedFrames = 0;
            texture->m_retryStopped = false;
        }
        if (texture->m_retryFailedFrames == 0 || texture->m_retryLastFrame != frame)
        {
            if (texture->m_retryFailedFrames < 255)
                ++texture->m_retryFailedFrames;
            texture->m_retryLastFrame = frame;
        }
        if (texture->m_retryFailedFrames > kMaxFailedFrames)
        {
            if (!texture->m_retryStopped)
            {
                texture->m_retryStopped = true;
                s_guardRetryStops.fetch_add(1, std::memory_order_relaxed);
                const auto& info = kMetalGuardInfo[(uint32)reason];
                MetalGuardNote(MetalGuard::UploadRetryStopped, {(uint64)(uint32)texture->format, ((uint64)(uint32)texture->width << 32) | (uint32)texture->height, (uint64)(uint32)mipIndex, (uint64)reason}, [&] {
                    return fmt::format("format {:04x} {}x{} mip {} failed '{}' in {} frames, it is not flagged for reload any more until it has been quiet for {} frames", (uint32)texture->format, texture->width, texture->height, mipIndex, info.name, kMaxFailedFrames, kQuietFrames);
                });
            }
            return;
        }
        if (texture->m_retryHashInverted && texture->texDataHash2 == texture->m_retryInvertedHash)
            return;
        texture->texDataHash2 = ~texture->texDataHash2;
        texture->m_retryInvertedHash = texture->texDataHash2;
        texture->m_retryHashInverted = true;
        s_guardRetriesScheduled.fetch_add(1, std::memory_order_relaxed);
    }
}

std::vector<MetalRenderer::DeviceInfo> MetalRenderer::GetDevices()
{
    NS_STACK_SCOPED auto devices = MTL::CopyAllDevices();
    std::vector<MetalRenderer::DeviceInfo> result;
    result.reserve(devices->count());
    for (uint32 i = 0; i < devices->count(); i++)
    {
        MTL::Device* device = static_cast<MTL::Device*>(devices->object(i));
        result.push_back({std::string(device->name()->utf8String()), device->registryID()});
    }

    return result;
}

MetalRenderer::MetalRenderer()
{
    LatteIndices_forgetAll();
    {
        constexpr uint32 kDrawsPerCommandBuffer = 2 * 196;
        constexpr uint32 kCommandBuffersCovered = 12;
        m_breadcrumbCapacity = std::max<uint32>(1024, kDrawsPerCommandBuffer * kCommandBuffersCovered);
        m_breadcrumbs.assign(m_breadcrumbCapacity, MetalDrawBreadcrumb{});
    }
    LatteWait::ResetAll();

    switch (g_current_game_profile->GetPositionInvariance())
    {
    case PositionInvariance::Auto:
        switch (CafeSystem::GetForegroundTitleId())
        {
        case 0x0005000010157F00:
        case 0x0005000010157E00:
        case 0x000500001014DB00:
        case 0x0005000010172700:
        case 0x0005000010172600:
        case 0x0005000010136900:
        case 0x0005000010136A00:
        case 0x0005000010136B00:
        case 0x000500001011C500:
        case 0x00050000101DAA00:
        case 0x00050000101DAB00:
        case 0x000500001010ED00:
        case 0x000500001010EC00:
        case 0x000500001010EB00:
        case 0x0005000010183A00:
        case 0x000500001020A300:
        case 0x00050000101E0100:
        case 0x0005000010110B00:
        case 0x0005000010139B00:
        case 0x0005000010110A00:
        case 0x0005000010110900:
        case 0x000500001012B400:
        case 0x000500001012CF00:
        case 0x00050000101B0500:
        case 0x0005000010201C00:
        case 0x00050000101B0400:
        case 0x0005000010201B00:
        case 0x00050000101C9500:
        case 0x00050000101C9400:
        case 0x00050000101C9300:
        case 0x0005000010135300:
        case 0x000500001012DC00:
        case 0x0005000010116300:
        case 0x0005000010185600:
            m_positionInvariance = true;
            break;
        default:
            m_positionInvariance = false;
            break;
        }
        break;
    case PositionInvariance::False:
        m_positionInvariance = false;
        break;
    case PositionInvariance::True:
        m_positionInvariance = true;
        break;
    }

    auto& config = GetConfig();
    const bool hasDeviceSet = config.mtl_graphic_device_uuid != 0;

    if (hasDeviceSet)
    {
        NS_STACK_SCOPED auto devices = MTL::CopyAllDevices();
        for (uint32 i = 0; i < devices->count(); i++)
        {
            MTL::Device* device = static_cast<MTL::Device*>(devices->object(i));
            if (device->registryID() == config.mtl_graphic_device_uuid)
            {
                m_device = device;
                break;
            }
        }
    }

    if (!m_device)
    {
        if (hasDeviceSet)
        {
            cemuLog_log(LogType::Force, "The selected GPU ({}) could not be found. Using the system default device.", config.mtl_graphic_device_uuid);
            config.mtl_graphic_device_uuid = 0;
        }
        m_device = MTL::CreateSystemDefaultDevice();
    }

    const char* deviceName = m_device->name()->utf8String();
    if (memcmp(deviceName, "Apple", 5) == 0)
        m_vendor = GfxVendor::Apple;
    else if (memcmp(deviceName, "AMD", 3) == 0)
        m_vendor = GfxVendor::AMD;
    else if (memcmp(deviceName, "Intel", 5) == 0)
        m_vendor = GfxVendor::Intel;
    else if (memcmp(deviceName, "NVIDIA", 6) == 0)
        m_vendor = GfxVendor::Nvidia;
    else
        m_vendor = GfxVendor::Generic;

    m_isAppleGPU = m_device->supportsFamily(MTL::GPUFamilyApple1);
    m_supportsFramebufferFetch = GetConfig().framebuffer_fetch.GetValue() ? m_device->supportsFamily(MTL::GPUFamilyApple2) : false;
    m_hasUnifiedMemory = m_device->hasUnifiedMemory();
    m_supportsMetal3 = m_device->supportsFamily(MTL::GPUFamilyMetal3);
    m_supportsMeshShaders = (m_supportsMetal3 && (!m_isAppleGPU || m_device->supportsFamily(MTL::GPUFamilyApple7)) && (m_vendor != GfxVendor::Intel || GetConfig().force_mesh_shaders.GetValue()));
    m_argumentBufferTier = m_device->argumentBuffersSupport();
    m_maxArgumentBufferSamplerCount = static_cast<uint32>(m_device->maxArgumentBufferSamplerCount());
    cemuLog_log(LogType::Force, "Metal argument buffers: Tier {}, {} samplers", m_argumentBufferTier == MTL::ArgumentBuffersTier2 ? 2 : 1, m_maxArgumentBufferSamplerCount);
    m_recommendedMaxVRAMUsage = m_device->recommendedMaxWorkingSetSize();
    m_pixelFormatSupport = MetalPixelFormatSupport(m_device);

    CheckForPixelFormatSupport(m_pixelFormatSupport);

    m_commandQueue = m_device->newCommandQueue();

    m_event = m_device->newSharedEvent();

    NS_STACK_SCOPED MTL::SamplerDescriptor* samplerDescriptor = MTL::SamplerDescriptor::alloc()->init();
    samplerDescriptor->setSupportArgumentBuffers(true);
#ifdef CEMU_DEBUG_ASSERT
    samplerDescriptor->setLabel(GetLabel("Nearest sampler state", samplerDescriptor));
#endif
    m_nearestSampler = m_device->newSamplerState(samplerDescriptor);

    samplerDescriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    samplerDescriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
#ifdef CEMU_DEBUG_ASSERT
    samplerDescriptor->setLabel(GetLabel("Linear sampler state", samplerDescriptor));
#endif
    m_linearSampler = m_device->newSamplerState(samplerDescriptor);

    m_nullBuffer = m_device->newBuffer(64 * 1024, MTL::ResourceStorageModeShared);
    if (!m_nullBuffer)
        throw std::runtime_error("Metal: could not allocate the null buffer");
    std::memset(m_nullBuffer->contents(), 0, m_nullBuffer->length());
#ifdef CEMU_DEBUG_ASSERT
    m_nullBuffer->setLabel(GetLabel("Null buffer", m_nullBuffer));
#endif

    NS_STACK_SCOPED MTL::TextureDescriptor* textureDescriptor = MTL::TextureDescriptor::alloc()->init();
    textureDescriptor->setTextureType(MTL::TextureType1D);
    textureDescriptor->setWidth(1);
    textureDescriptor->setUsage(MTL::TextureUsageShaderRead);
    m_nullTexture1D = m_device->newTexture(textureDescriptor);
#ifdef CEMU_DEBUG_ASSERT
    m_nullTexture1D->setLabel(GetLabel("Null texture 1D", m_nullTexture1D));
#endif

    textureDescriptor->setTextureType(MTL::TextureType2D);
    textureDescriptor->setHeight(1);
    textureDescriptor->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    m_nullTexture2D = m_device->newTexture(textureDescriptor);
    if (!m_nullTexture2D)
        throw std::runtime_error("Metal: could not allocate the null 2D texture");
#ifdef CEMU_DEBUG_ASSERT
    m_nullTexture2D->setLabel(GetLabel("Null texture 2D", m_nullTexture2D));
#endif

    m_memoryManager = new MetalMemoryManager(this);
    m_outputShaderCache = new MetalOutputShaderCache(this);
    m_pipelineCache = new MetalPipelineCache(this);
    m_depthStencilCache = new MetalDepthStencilCache(this);
    m_samplerCache = new MetalSamplerCache(this);

    if (m_memoryManager->NeedsReducedLatency())
        m_defaultCommitTreshlod = 64;
    else
        m_defaultCommitTreshlod = 196;

    m_occlusionQuery.m_resultBuffer = m_device->newBuffer(OCCLUSION_QUERY_BUFFER_COUNT * OCCLUSION_QUERY_POOL_SIZE * sizeof(uint64), MTL::ResourceStorageModeShared);
    if (!m_occlusionQuery.m_resultBuffer)
        throw std::runtime_error("Metal: could not allocate the occlusion query result buffer");
#ifdef CEMU_DEBUG_ASSERT
    m_occlusionQuery.m_resultBuffer->setLabel(GetLabel("Occlusion query result buffer", m_occlusionQuery.m_resultBuffer));
#endif
    m_occlusionQuery.m_resultsPtr = (uint64*)m_occlusionQuery.m_resultBuffer->contents();
    std::fill_n(m_occlusionQuery.m_resultsPtr, OCCLUSION_QUERY_BUFFER_COUNT * OCCLUSION_QUERY_POOL_SIZE, uint64{0});

    for (uint32 i = 0; i < MAX_MTL_VERTEX_BUFFERS; i++)
    {
        m_state.m_vertexBuffers[i] = nullptr;
        m_state.m_vertexBufferOffsets[i] = INVALID_OFFSET;
        m_state.m_vertexBufferSizes[i] = 0;
    }

    for (uint32 i = 0; i < METAL_GENERAL_SHADER_TYPE_TOTAL; i++)
    {
        for (uint32 j = 0; j < MAX_MTL_BUFFERS; j++)
            m_state.m_uniformBuffers[i][j] = nullptr;
        for (uint32 j = 0; j < MAX_MTL_BUFFERS; j++)
            m_state.m_uniformBufferOffsets[i][j] = INVALID_OFFSET;
        for (uint32 j = 0; j < MAX_MTL_BUFFERS; j++)
            m_state.m_uniformBufferSizes[i][j] = 0;
    }

    NS::Error* error = nullptr;
    NS_STACK_SCOPED MTL::Library* utilityLibrary = m_device->newLibrary(ToNSString(utilityShaderSource), nullptr, &error);
    if (error)
    {
        cemuLog_log(LogType::Force, "failed to create utility library (error: {})", error->localizedDescription()->utf8String());
    }

    NS_STACK_SCOPED MTL::Function* vertexFullscreenFunction = utilityLibrary->newFunction(ToNSString("vertexFullscreen"));
    NS_STACK_SCOPED MTL::Function* fragmentCopyDepthToColorFunction = utilityLibrary->newFunction(ToNSString("fragmentCopyDepthToColor"));
    NS_STACK_SCOPED MTL::Function* fragmentCopyColorToDepthFunction = utilityLibrary->newFunction(ToNSString("fragmentCopyColorToDepth"));

    m_copyDepthToColorDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    m_copyDepthToColorDesc->setVertexFunction(vertexFullscreenFunction);
    m_copyDepthToColorDesc->setFragmentFunction(fragmentCopyDepthToColorFunction);
    m_copyDepthToColorDesc->colorAttachments()->object(0)->setWriteMask(MTL::ColorWriteMaskRed);

    m_copyColorToDepthDesc = MTL::RenderPipelineDescriptor::alloc()->init();
    m_copyColorToDepthDesc->setVertexFunction(vertexFullscreenFunction);
    m_copyColorToDepthDesc->setFragmentFunction(fragmentCopyColorToDepthFunction);

    NS_STACK_SCOPED MTL::DepthStencilDescriptor* copyColorToDepthStateDesc = MTL::DepthStencilDescriptor::alloc()->init();
    copyColorToDepthStateDesc->setDepthCompareFunction(MTL::CompareFunctionAlways);
    copyColorToDepthStateDesc->setDepthWriteEnabled(true);
    m_copyColorToDepthState = m_device->newDepthStencilState(copyColorToDepthStateDesc);

    if (m_isAppleGPU)
    {
        m_copyBufferToBufferPipeline = new MetalVoidVertexPipeline(this, utilityLibrary, "vertexCopyBufferToBuffer");
        m_copyBufferToBuffer32Pipeline = new MetalVoidVertexPipeline(this, utilityLibrary, "vertexCopyBufferToBuffer32");
    }

    m_occlusionQuery.m_lastCommandBuffer = nullptr;
    m_captureFrame = false;
}

MetalRenderer::~MetalRenderer()
{
    if (m_isAppleGPU)
    {
        delete m_copyBufferToBufferPipeline;
        delete m_copyBufferToBuffer32Pipeline;
    }

    m_copyDepthToColorDesc->release();
    for (const auto [pixelFormat, pipeline] : m_copyDepthToColorPipelines)
        pipeline->release();
    m_copyColorToDepthDesc->release();
    for (const auto [pixelFormat, pipeline] : m_copyColorToDepthPipelines)
        pipeline->release();
    m_copyColorToDepthState->release();

    if (m_commandEncoder)
    {
        m_commandEncoder->endEncoding();
        m_commandEncoder->release();
        m_commandEncoder = nullptr;
    }
    if (m_currentCommandBuffer.m_commandBuffer && !m_currentCommandBuffer.m_commited)
        m_currentCommandBuffer.m_commandBuffer->release();
    m_currentCommandBuffer = {};
    for (MTL::CommandBuffer* commandBuffer : m_executingCommandBuffers)
    {
        WaitForCommandBuffer(commandBuffer, "shutdown: waiting for a command buffer");
        commandBuffer->release();
    }
    m_executingCommandBuffers.clear();
    m_executingEventValues.clear();

    LatteIndices_forgetAll();

    delete m_outputShaderCache;
    if (!MetalPipelineCache_LoaderAbandoned())
        delete m_pipelineCache;
    delete m_depthStencilCache;
    delete m_samplerCache;
    delete m_memoryManager;

    m_nullBuffer->release();
    m_nullTexture1D->release();
    m_nullTexture2D->release();
    for (const auto& [key, texture] : m_nullSampledTextures)
    {
        if (texture)
            texture->release();
    }

    m_nearestSampler->release();
    m_linearSampler->release();

    if (m_readbackBuffer)
        m_readbackBuffer->release();

    if (m_xfbRingBuffer)
        m_xfbRingBuffer->release();
    for (MTL::Buffer* retiredBuffer : m_retiredXfbRingBuffers)
        retiredBuffer->release();

    m_occlusionQuery.m_resultBuffer->release();
    for (auto* completion : m_occlusionQuery.m_bufferCompletion)
        if (completion)
            completion->release();
    if (m_occlusionQuery.m_lastCommandBuffer)
        m_occlusionQuery.m_lastCommandBuffer->release();

    m_event->release();

    m_commandQueue->release();
    m_device->release();
}

void MetalRenderer::InitializeLayer(const Vector2i& size, bool mainWindow)
{
    auto& layer = GetLayer(mainWindow);
    layer = MetalLayerHandle(m_device, size, mainWindow);
    layer.GetLayer()->setPixelFormat(MTL::PixelFormatBGRA8Unorm);
}

void MetalRenderer::ShutdownLayer(bool mainWindow)
{
    GetLayer(mainWindow) = MetalLayerHandle();
}

void MetalRenderer::ResizeLayer(const Vector2i& size, bool mainWindow, double scale)
{
    GetLayer(mainWindow).Resize(size, scale);
}

void MetalRenderer::Initialize()
{
    Renderer::Initialize();
    RendererShaderMtl::Initialize();
}

void MetalRenderer::Shutdown()
{
    MetalPipelineCache_DrainAsyncCompiles();
    if (m_pipelineCache)
        m_pipelineCache->StopLoading(3000);
    Flush(true);
    MetalGuardReport("title stop", 200, true);
    if (ImGui::GetCurrentContext() && ImGui::GetIO().BackendRendererUserData)
        ImGui_ImplMetal_Shutdown();
    Renderer::Shutdown();
    RendererShaderMtl::Shutdown();
}

bool MetalRenderer::IsPadWindowActive()
{
    return (GetLayer(false).GetLayer() != nullptr);
}

bool MetalRenderer::GetVRAMInfo(int& usageInMB, int& totalInMB) const
{
    usageInMB = (m_device->currentAllocatedSize() - m_memoryManager->GetHostAllocationSize()) / 1024 / 1024;
    totalInMB = m_recommendedMaxVRAMUsage / 1024 / 1024;

    return true;
}

void MetalRenderer::ClearColorbuffer(bool padView)
{
    if (!AcquireDrawable(!padView))
        return;

    ClearColorTextureInternal(GetLayer(!padView).GetDrawableTexture(), 0, 0, 0.0f, 0.0f, 0.0f, 1.0f);
}

void MetalRenderer::DrawEmptyFrame(bool mainWindow)
{
    if (!BeginFrame(mainWindow))
        return;
    SwapBuffers(mainWindow, !mainWindow);
}

void MetalRenderer::SwapBuffers(bool swapTV, bool swapDRC)
{
    if (swapTV || swapDRC)
        PerfTelemetry::BeginFrameSampling();
    if (LatteWait::Get().gpuError.load(std::memory_order_relaxed))
    {
        CommitCommandBuffer();
        ProcessFinishedCommandBuffers();
        UpdateMemoryStatsAndRelievePressure();
        return;
    }

    if (swapTV)
        SwapBuffer(true);
    if (swapDRC)
        SwapBuffer(false);

    CommitCommandBuffer();

    UpdateMemoryStatsAndRelievePressure();

    m_performanceMonitor.ResetPerFrameData();

    if (m_capturing)
    {
        EndCapture();
    }
    else if (m_captureFrame)
    {
        StartCapture();
        m_captureFrame = false;
    }
}

void MetalRenderer::HandleScreenshotRequest(LatteTextureView* texView, bool padView) {
#if ESPRESSU_AUDIT_HOOKS
    if (IOSAuditMetal_WantsCapture(!padView))
        IOSAuditMetal_CaptureView(this, texView, padView);
#endif
    if (!m_screenshot_requested && m_screenshot_state == ScreenshotState::None)
        return;

    if (m_mainLayer.GetDrawable())
    {
        if (m_screenshot_state == ScreenshotState::Main && padView)
            return;

        if (m_screenshot_state == ScreenshotState::Pad && !padView)
            return;

        if (m_screenshot_state == ScreenshotState::None)
            m_screenshot_state = padView ? ScreenshotState::Main : ScreenshotState::Pad;
        else
            m_screenshot_state = ScreenshotState::None;
    }
    else
        m_screenshot_state = ScreenshotState::None;

    auto texMtl = static_cast<LatteTextureMtl*>(texView->baseTexture);

    int width, height;
    texMtl->GetEffectiveSize(width, height, 0);

    uint32 bytesPerRow = GetMtlTextureBytesPerRow(texMtl->format, texMtl->isDepth, width);
    uint32 size = GetMtlTextureBytesPerImage(texMtl->format, texMtl->isDepth, height, bytesPerRow);

    auto blitCommandEncoder = GetBlitCommandEncoder();

    auto& bufferAllocator = m_memoryManager->GetStagingAllocator();
    auto buffer = bufferAllocator.AllocateBufferMemory(size, 1);
    if (!buffer.mtlBuffer)
        return;

    blitCommandEncoder->copyFromTexture(texMtl->GetTexture(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1), buffer.mtlBuffer, buffer.bufferOffset, bytesPerRow, 0);

    bool formatValid = true;
    std::vector<uint8> rgb_data;
    rgb_data.reserve(3 * width * height);

    auto pixelFormat = texMtl->GetTexture()->pixelFormat();
    switch (pixelFormat)
    {
    case MTL::PixelFormatRGBA8Unorm:
        for (auto ptr = buffer.memPtr; ptr < buffer.memPtr + size; ptr += 4)
        {
            rgb_data.emplace_back(*ptr);
            rgb_data.emplace_back(*(ptr + 1));
            rgb_data.emplace_back(*(ptr + 2));
        }
        break;
    case MTL::PixelFormatRGBA8Unorm_sRGB:
        for (auto ptr = buffer.memPtr; ptr < buffer.memPtr + size; ptr += 4)
        {
            rgb_data.emplace_back(SRGBComponentToRGB(*ptr));
            rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 1)));
            rgb_data.emplace_back(SRGBComponentToRGB(*(ptr + 2)));
        }
        break;
    default:
        cemuLog_log(LogType::Force, "Unsupported screenshot texture pixel format {}", pixelFormat);
        formatValid = false;
        break;
    }

    if (formatValid)
        SaveScreenshot(rgb_data, width, height, !padView);
}

void MetalRenderer::DrawBackbufferQuad(LatteTextureView* texView, RendererOutputShader* shader, bool useLinearTexFilter,
                                sint32 imageX, sint32 imageY, sint32 imageWidth, sint32 imageHeight,
                                bool padView, bool clearBackground)
{
    if (!AcquireDrawable(!padView))
        return;

    MTL::Texture* presentTexture = static_cast<LatteTextureViewMtl*>(texView)->GetRGBAView();

    auto& layer = GetLayer(!padView);
    if (!layer.GetDrawableTexture() || !presentTexture)
        return;

    NS_STACK_SCOPED MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    auto colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    colorAttachment->setTexture(layer.GetDrawable()->texture());
    colorAttachment->setClearColor(MTL::ClearColor(0.0, 0.0, 0.0, 1.0));
    colorAttachment->setLoadAction(clearBackground ? MTL::LoadActionClear : MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    auto renderCommandEncoder = GetTemporaryRenderCommandEncoder(renderPassDescriptor);

    uint8 shaderIndex = 255;
    if (shader == RendererOutputShader::s_copy_shader) shaderIndex = 0;
    else if (shader == RendererOutputShader::s_bicubic_shader) shaderIndex = 1;
    else if (shader == RendererOutputShader::s_hermit_shader) shaderIndex = 2;
    else if (shader == RendererOutputShader::s_copy_shader_ud) shaderIndex = 3;
    else if (shader == RendererOutputShader::s_bicubic_shader_ud) shaderIndex = 4;
    else if (shader == RendererOutputShader::s_hermit_shader_ud) shaderIndex = 5;

    uint8 shaderType = shaderIndex % 3;

    auto renderPipelineState = m_outputShaderCache->GetPipeline(shader, shaderIndex, m_state.m_usesSRGB);

    renderCommandEncoder->setRenderPipelineState(renderPipelineState);
    renderCommandEncoder->setFragmentTexture(presentTexture, 0);
    renderCommandEncoder->setFragmentSamplerState((useLinearTexFilter ? m_linearSampler : m_nearestSampler), 0);

    float outputSize[2] = {(float)imageWidth, (float)imageHeight};
    switch (shaderType)
    {
    case 2:
        renderCommandEncoder->setFragmentBytes(outputSize, sizeof(outputSize), 0);
        break;
    default:
        break;
    }

    renderCommandEncoder->setViewport(MTL::Viewport{(double)imageX, (double)imageY, (double)imageWidth, (double)imageHeight, 0.0, 1.0});
    {
        MTL::Texture* target = layer.GetDrawable()->texture();
        const uint64 tw = target->width(), th = target->height();
        const uint64 sx = std::min<uint64>((uint32)std::max(imageX, 0), tw), sy = std::min<uint64>((uint32)std::max(imageY, 0), th);
        const uint64 sw = std::min<uint64>((uint32)std::max(imageWidth, 0), tw - sx), sh = std::min<uint64>((uint32)std::max(imageHeight, 0), th - sy);
        if (sw < (uint64)std::max(imageWidth, 0) || sh < (uint64)std::max(imageHeight, 0))
        {
            MetalGuardNote(MetalGuard::PresentScissorClamped, {(uint64)(uint32)imageX, (uint64)(uint32)imageY, (uint64)(uint32)imageWidth, (uint64)(uint32)imageHeight, tw, th}, [&] {
                return fmt::format("present image {},{} {}x{} is larger than the {}x{} drawable, scissor cut to {}x{}", imageX, imageY, imageWidth, imageHeight, tw, th, sw, sh);
            });
        }
        renderCommandEncoder->setScissorRect(MTL::ScissorRect{(NS::UInteger)sx, (NS::UInteger)sy, (NS::UInteger)sw, (NS::UInteger)sh});
    }

    renderCommandEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));

    EndEncoding();
}

bool MetalRenderer::BeginFrame(bool mainWindow)
{
    if (!AcquireDrawable(mainWindow))
        return false;

    ClearColorTextureInternal(GetLayer(mainWindow).GetDrawableTexture(), 0, 0, 0.0f, 0.0f, 0.0f, 1.0f);
    return true;
}

void MetalRenderer::Flush(bool waitIdle)
{
    if (m_recordedDrawcalls > 0 || waitIdle)
        CommitCommandBuffer();

    if (waitIdle && m_executingCommandBuffers.size() != 0)
    {
        WaitForCommandBuffer(m_executingCommandBuffers.back(), "Flush: waiting for the last command buffer");
        ProcessFinishedCommandBuffers();
    }
}

void MetalRenderer::NotifyLatteCommandProcessorIdle()
{
    constexpr auto IDLE_COMMIT_DELAY = std::chrono::milliseconds(20);

    if (!m_currentCommandBuffer.m_commandBuffer || m_currentCommandBuffer.m_commited)
    {
        m_idleCommit.m_watching = false;
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!m_idleCommit.m_watching || m_idleCommit.m_commandBuffer != m_currentCommandBuffer.m_commandBuffer || m_idleCommit.m_recordedDrawcalls != m_recordedDrawcalls)
    {
        m_idleCommit.m_watching = true;
        m_idleCommit.m_commandBuffer = m_currentCommandBuffer.m_commandBuffer;
        m_idleCommit.m_recordedDrawcalls = m_recordedDrawcalls;
        m_idleCommit.m_since = now;
        return;
    }

    if (now - m_idleCommit.m_since >= IDLE_COMMIT_DELAY)
    {
        CommitCommandBuffer();
        m_idleCommit.m_watching = false;
    }
}

bool MetalRenderer::ImguiBegin(bool mainWindow)
{
    if (!Renderer::ImguiBegin(mainWindow))
        return false;

    if (!AcquireDrawable(mainWindow))
        return false;

    EnsureImGuiBackend();

    ImGuiIO& io = ImGui::GetIO();
    if (!io.Fonts->IsBuilt())
        ImGui_ImplMetal_CreateFontsTexture(m_device);

    auto& layer = GetLayer(mainWindow);
    if (!layer.GetDrawableTexture())
        return false;

    NS_STACK_SCOPED MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    auto colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    colorAttachment->setTexture(layer.GetDrawable()->texture());
    colorAttachment->setLoadAction(MTL::LoadActionLoad);
    colorAttachment->setStoreAction(MTL::StoreActionStore);

    ImGui_ImplMetal_NewFrame(renderPassDescriptor);
    ImGui_UpdateWindowInformation(mainWindow);
    ImGui::NewFrame();

    if (m_encoderType != MetalEncoderType::Render)
        GetTemporaryRenderCommandEncoder(renderPassDescriptor);

    return true;
}

void MetalRenderer::ImguiEnd()
{
    EnsureImGuiBackend();

    if (m_encoderType != MetalEncoderType::Render)
    {
        cemuLog_logOnce(LogType::Force, "no render command encoder, cannot draw ImGui");
        return;
    }

    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), GetCurrentCommandBuffer(), (MTL::RenderCommandEncoder*)m_commandEncoder);

    EndEncoding();
}

ImTextureID MetalRenderer::GenerateTexture(const std::vector<uint8>& data, const Vector2i& size)
{
    try
    {
        std::vector <uint8> tmp(size.x * size.y * 4);
        for (size_t i = 0; i < data.size() / 3; ++i)
        {
            tmp[(i * 4) + 0] = data[(i * 3) + 0];
            tmp[(i * 4) + 1] = data[(i * 3) + 1];
            tmp[(i * 4) + 2] = data[(i * 3) + 2];
            tmp[(i * 4) + 3] = 0xFF;
        }

        NS_STACK_SCOPED MTL::TextureDescriptor* desc = MTL::TextureDescriptor::alloc()->init();
        desc->setTextureType(MTL::TextureType2D);
        desc->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
        desc->setWidth(size.x);
        desc->setHeight(size.y);
        desc->setStorageMode(m_isAppleGPU ? MTL::StorageModeShared : MTL::StorageModeManaged);
        desc->setUsage(MTL::TextureUsageShaderRead);

        MTL::Texture* texture = m_device->newTexture(desc);
        if (!texture)
            return nullptr;

        texture->replaceRegion(MTL::Region(0, 0, size.x, size.y), 0, 0, tmp.data(), size.x * 4, 0);

        return (ImTextureID)texture;
    }
    catch (const std::exception& ex)
    {
        cemuLog_log(LogType::Force, "can't generate imgui texture: {}", ex.what());
        return nullptr;
    }
}

void MetalRenderer::DeleteTexture(ImTextureID id)
{
    EnsureImGuiBackend();

    ((MTL::Texture*)id)->release();
}

void MetalRenderer::DeleteFontTextures()
{
    EnsureImGuiBackend();

    ImGui_ImplMetal_DestroyFontsTexture();
}

void MetalRenderer::AppendOverlayDebugInfo()
{
    ImGui::Text("--- GPU info ---");
    ImGui::Text("GPU                        %s", m_device->name()->utf8String());
    ImGui::Text("Is Apple GPU               %s", (m_isAppleGPU ? "yes" : "no"));
    ImGui::Text("Supports framebuffer fetch %s", (m_supportsFramebufferFetch ? "yes" : "no"));
    ImGui::Text("Has unified memory         %s", (m_hasUnifiedMemory ? "yes" : "no"));
    ImGui::Text("Supports Metal3            %s", (m_supportsMetal3 ? "yes" : "no"));

    ImGui::Text("--- Metal info ---");
    ImGui::Text("Render pipeline states     %zu", m_pipelineCache->GetPipelineCacheSize());

    ImGui::Text("--- Metal info (per frame) ---");
    ImGui::Text("Command buffers            %u", m_performanceMonitor.m_commandBuffers);
    ImGui::Text("Render passes              %u", m_performanceMonitor.m_renderPasses);
    ImGui::Text("Clears                     %u", m_performanceMonitor.m_clears);
    ImGui::Text("Manual vertex fetch draws  %u (mesh draws: %u)", m_performanceMonitor.m_manualVertexFetchDraws, m_performanceMonitor.m_meshDraws);
    ImGui::Text("Triangle fans              %u", m_performanceMonitor.m_triangleFans);
    ImGui::Text("Snapshot uploads           %llu KB (reuses: %u)", static_cast<unsigned long long>(m_performanceMonitor.m_snapshotBytes / 1024), m_performanceMonitor.m_snapshotReuses);
    ImGui::Text("Argument buffer encodes    %u (reuses: %u)", m_performanceMonitor.m_argumentBufferEncodes, m_performanceMonitor.m_argumentBufferReuses);

    ImGui::Text("--- Cache debug info ---");

    uint32 bufferCacheHeapSize = 0;
    uint32 bufferCacheAllocationSize = 0;
    uint32 bufferCacheNumAllocations = 0;

    LatteBufferCache_getStats(bufferCacheHeapSize, bufferCacheAllocationSize, bufferCacheNumAllocations);

    ImGui::Text("Buffer");
    ImGui::SameLine(60.0f);
    ImGui::Text("%06uKB / %06uKB Allocs: %u", (uint32)(bufferCacheAllocationSize + 1023) / 1024, ((uint32)bufferCacheHeapSize + 1023) / 1024, (uint32)bufferCacheNumAllocations);

    uint32 numBuffers;
    size_t totalSize, freeSize;

    m_memoryManager->GetStagingAllocator().GetStats(numBuffers, totalSize, freeSize);
    ImGui::Text("Staging");
    ImGui::SameLine(60.0f);
    ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

    m_memoryManager->GetIndexAllocator().GetStats(numBuffers, totalSize, freeSize);
    ImGui::Text("Index");
    ImGui::SameLine(60.0f);
    ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, (uint32)numBuffers);

    m_memoryManager->GetSnapshotStats(numBuffers, totalSize, freeSize);
    ImGui::Text("Snapshots");
    ImGui::SameLine(60.0f);
    ImGui::Text("%06uKB / %06uKB Buffers: %u", ((uint32)(totalSize - freeSize) + 1023) / 1024, ((uint32)totalSize + 1023) / 1024, numBuffers);
}

void MetalRenderer::renderTarget_setViewport(float x, float y, float width, float height, float nearZ, float farZ, bool halfZ)
{

    m_state.m_viewport = MTL::Viewport{x, y, width, height, nearZ, farZ};
}

void MetalRenderer::renderTarget_setScissor(sint32 scissorX, sint32 scissorY, sint32 scissorWidth, sint32 scissorHeight)
{
    m_state.m_scissor = MTL::ScissorRect{(uint32)scissorX, (uint32)scissorY, (uint32)scissorWidth, (uint32)scissorHeight};
}

LatteCachedFBO* MetalRenderer::rendertarget_createCachedFBO(uint64 key)
{
    return new CachedFBOMtl(this, key);
}

void MetalRenderer::rendertarget_deleteCachedFBO(LatteCachedFBO* cfbo)
{
    if (cfbo == (LatteCachedFBO*)m_state.m_activeFBO.m_fbo)
        m_state.m_activeFBO = {nullptr};
    if (cfbo == (LatteCachedFBO*)m_state.m_lastUsedFBO.m_fbo)
        m_state.m_lastUsedFBO.m_fbo = nullptr;
}

void MetalRenderer::rendertarget_bindFramebufferObject(LatteCachedFBO* cfbo)
{
    m_state.m_activeFBO = {(CachedFBOMtl*)cfbo, MetalAttachmentsInfo((CachedFBOMtl*)cfbo)};
    m_state.m_fboChanged = true;
}

void* MetalRenderer::texture_acquireTextureUploadBuffer(uint32 size)
{
    return m_memoryManager->AcquireTextureUploadBuffer(size);
}

void MetalRenderer::texture_uploadBufferUnavailable(LatteTexture* texture)
{
    if (texture)
        MetalUploadSkipped(static_cast<LatteTextureMtl*>(texture), MetalGuard::UploadStagingFull, 0);
}

void MetalRenderer::texture_releaseTextureUploadBuffer(uint8* mem)
{
    m_memoryManager->ReleaseTextureUploadBuffer(mem);
}

TextureDecoder* MetalRenderer::texture_chooseDecodedFormat(Latte::E_GX2SURFFMT format, bool isDepth, Latte::E_DIM dim, uint32 width, uint32 height)
{
    return GetMtlPixelFormatInfo(format, isDepth).textureDecoder;
}

void MetalRenderer::texture_clearSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex)
{
    if (hostTexture->isDepth)
    {
        texture_clearDepthSlice(hostTexture, sliceIndex, mipIndex, true, hostTexture->hasStencil, 0.0f, 0);
    }
    else
    {
        texture_clearColorSlice(hostTexture, sliceIndex, mipIndex, 0.0f, 0.0f, 0.0f, 0.0f);
    }
}

static MTL::BlitOption GetTextureUploadBlitOption(MTL::PixelFormat pixelFormat)
{
    switch (pixelFormat)
    {
    case MTL::PixelFormatDepth16Unorm:
    case MTL::PixelFormatDepth32Float:
        return MTL::BlitOptionDepthFromDepthStencil;
    case MTL::PixelFormatStencil8:
        return MTL::BlitOptionStencilFromDepthStencil;
    default:
        return MTL::BlitOptionNone;
    }
}

struct DepthStencilUploadLayout
{
    size_t sourceBytesPerTexel;
    size_t stencilOffset;
    bool maskDepthTo24Bit;
};

static DepthStencilUploadLayout GetDepthStencilUploadLayout(Latte::E_GX2SURFFMT format, const MetalPixelFormatInfo& formatInfo)
{
    switch (format)
    {
    case Latte::E_GX2SURFFMT::D24_S8_UNORM:
        if (formatInfo.pixelFormat == MTL::PixelFormatDepth24Unorm_Stencil8)
            return {4, 3, true};
        return {8, 4, false};
    case Latte::E_GX2SURFFMT::D24_S8_FLOAT:
    case Latte::E_GX2SURFFMT::D32_S8_FLOAT:
        return {8, 4, false};
    default:
        cemu_assert_suspicious();
        return {formatInfo.bytesPerBlock, formatInfo.bytesPerBlock == 4 ? (size_t)3 : (size_t)4, false};
    }
}

void MetalRenderer::texture_loadSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize)
{
    auto textureMtl = (LatteTextureMtl*)hostTexture;

    uint32 offsetZ = 0;
    if (textureMtl->Is3DTexture())
    {
        offsetZ = sliceIndex;
        sliceIndex = 0;
    }

    const auto& formatInfo = GetMtlPixelFormatInfo(textureMtl->format, textureMtl->isDepth);
    size_t bytesPerRow = GetMtlTextureBytesPerRow(textureMtl->format, textureMtl->isDepth, width);
    sint32 uploadWidth = width;
    sint32 uploadHeight = height;
    {
        MTL::Texture* target = textureMtl->GetTexture();
        const uint32 blockW = std::max<uint32>(1, formatInfo.blockTexelSize.x);
        const uint32 blockH = std::max<uint32>(1, formatInfo.blockTexelSize.y);
        const uint64 levelW = target ? std::max<uint64>(1, (uint64)target->width() >> std::min(mipIndex < 0 ? 0 : mipIndex, 63)) : 0;
        const uint64 levelH = target ? std::max<uint64>(1, (uint64)target->height() >> std::min(mipIndex < 0 ? 0 : mipIndex, 63)) : 0;
        const uint64 alignedW = (levelW + blockW - 1) / blockW * blockW;
        const uint64 alignedH = (levelH + blockH - 1) / blockH * blockH;
        const uint64 levelKey = (levelW << 32) | levelH;
        const uint64 sizeKey = ((uint64)(uint32)width << 32) | (uint32)height;
        auto describe = [&](const char* why) {
            return fmt::format("{} - format {:04x}{} {}x{} mip {} first seen at slice {} ({} data bytes, {} per row; texture {}x{} level {}x{} of {} mips, pixel format {})", why,
                (uint32)textureMtl->format, textureMtl->isDepth ? " depth" : "", width, height, mipIndex, (sint32)offsetZ + sliceIndex, compressedImageSize, bytesPerRow,
                target ? (uint64)target->width() : 0, target ? (uint64)target->height() : 0, levelW, levelH, target ? (uint64)target->mipmapLevelCount() : 0,
                target ? (uint64)target->pixelFormat() : 0);
        };
        const uint64 formatKey = ((uint64)(uint32)textureMtl->format << 1) | (textureMtl->isDepth ? 1 : 0);
        const uint64 mipKey = (uint64)(uint32)mipIndex;

        if (!target)
        {
            MetalGuardNote(MetalGuard::UploadNoTarget, {formatKey, sizeKey, mipKey}, [&] { return describe("the texture has no Metal texture"); });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadNoTarget, mipIndex);
            return;
        }
        if (target == m_nullTexture2D)
        {
            MetalGuardNote(MetalGuard::UploadPlaceholder, {formatKey, sizeKey, mipKey}, [&] { return describe("the texture is the shared placeholder"); });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadPlaceholder, mipIndex);
            return;
        }
        if (mipIndex < 0 || (NS::UInteger)mipIndex >= target->mipmapLevelCount())
        {
            MetalGuardNote(MetalGuard::UploadBadLevel, {formatKey, sizeKey, mipKey}, [&] { return describe("the texture has no such mip level"); });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadBadLevel, mipIndex);
            return;
        }
        if (width <= 0 || height <= 0)
        {
            MetalGuardNote(MetalGuard::UploadBadSize, {formatKey, sizeKey, mipKey}, [&] { return describe("empty slice"); });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadBadSize, mipIndex);
            return;
        }
        const bool packedDepthStencil = textureMtl->isDepth && formatInfo.hasStencil;
        if ((uint64)width > alignedW || (uint64)height > alignedH)
        {
            if (packedDepthStencil)
            {
                MetalGuardNote(MetalGuard::UploadBadSize, {formatKey, sizeKey, mipKey, levelKey}, [&] { return describe("depth/stencil slice larger than its level"); });
                MetalUploadSkipped(textureMtl, MetalGuard::UploadBadSize, mipIndex);
                return;
            }
            uploadWidth = (sint32)std::min<uint64>((uint64)width, alignedW);
            uploadHeight = (sint32)std::min<uint64>((uint64)height, alignedH);
            MetalGuardNote(MetalGuard::UploadClampedSize, {formatKey, sizeKey, mipKey, levelKey}, [&] { return describe("slice larger than its level, copying the part that fits"); });
        }
        if (!packedDepthStencil)
        {
            const uint64 rows = ((uint64)uploadHeight + blockH - 1) / blockH;
            const uint64 lastRowBytes = (((uint64)uploadWidth + blockW - 1) / blockW) * formatInfo.bytesPerBlock;
            if ((rows - 1) * bytesPerRow + lastRowBytes > compressedImageSize)
            {
                if (bytesPerRow == 0 || lastRowBytes > compressedImageSize)
                {
                    MetalGuardNote(MetalGuard::UploadTooFewBytes, {formatKey, sizeKey, mipKey}, [&] { return describe("not even one row of data"); });
                    MetalUploadSkipped(textureMtl, MetalGuard::UploadTooFewBytes, mipIndex);
                    return;
                }
                const uint64 rowsFit = (compressedImageSize - lastRowBytes) / bytesPerRow + 1;
                uploadHeight = (sint32)std::min<uint64>((uint64)uploadHeight, rowsFit * blockH);
                MetalGuardNote(MetalGuard::UploadClampedRows, {formatKey, sizeKey, mipKey}, [&] { return describe("less data than the slice needs, copying the rows that are there"); });
            }
        }
    }
    auto blitCommandEncoder = GetBlitCommandEncoder(PerfTelemetry::kBreakTextureUpload);

    if (textureMtl->isDepth && formatInfo.hasStencil)
    {
        const auto uploadLayout = GetDepthStencilUploadLayout(textureMtl->format, formatInfo);
        const size_t sourceBytesPerTexel = uploadLayout.sourceBytesPerTexel;
        const size_t pixelCount = (size_t)width * (size_t)height;
        const size_t expectedSourceSize = pixelCount * sourceBytesPerTexel;
        if ((sourceBytesPerTexel != 4 && sourceBytesPerTexel != 8) || uploadLayout.stencilOffset >= sourceBytesPerTexel || expectedSourceSize > compressedImageSize)
        {
            MetalGuardNote(MetalGuard::UploadDepthStencilBytes, {(uint64)(uint32)textureMtl->format, ((uint64)(uint32)width << 32) | (uint32)height}, [&] {
                return fmt::format("packed depth/stencil upload of format {:04x} {}x{} mip {} needs {} bytes ({} per texel) but has {}", (uint32)textureMtl->format, width, height, mipIndex, expectedSourceSize, sourceBytesPerTexel, compressedImageSize);
            });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadDepthStencilBytes, mipIndex);
            return;
        }

        constexpr size_t depthBytesPerTexel = sizeof(uint32);
        constexpr size_t stencilBytesPerTexel = sizeof(uint8);
        const size_t depthBytesPerRow = (size_t)width * depthBytesPerTexel;
        const size_t stencilBytesPerRow = (size_t)width * stencilBytesPerTexel;
        const size_t depthDataSize = depthBytesPerRow * (size_t)height;
        const size_t stencilDataSize = stencilBytesPerRow * (size_t)height;

        auto& bufferAllocator = m_memoryManager->GetStagingAllocator();
        auto depthAllocation = bufferAllocator.AllocateBufferMemory(depthDataSize, depthBytesPerTexel);
        auto stencilAllocation = bufferAllocator.AllocateBufferMemory(stencilDataSize, stencilBytesPerTexel);
        if (!depthAllocation.mtlBuffer || !stencilAllocation.mtlBuffer)
        {
            MetalGuardNote(MetalGuard::UploadStagingFull, {(uint64)(uint32)textureMtl->format, ((uint64)(uint32)width << 32) | (uint32)height}, [&] {
                return fmt::format("no staging memory for the depth/stencil upload of format {:04x} {}x{} mip {}", (uint32)textureMtl->format, width, height, mipIndex);
            });
            MetalUploadSkipped(textureMtl, MetalGuard::UploadStagingFull, mipIndex);
            return;
        }

        const uint8* sourceData = static_cast<const uint8*>(pixelData);
        uint8* depthData = depthAllocation.memPtr;
        uint8* stencilData = stencilAllocation.memPtr;
        for (size_t i = 0; i < pixelCount; ++i)
        {
            const uint8* sourceTexel = sourceData + i * sourceBytesPerTexel;
            uint32 depthValue;
            memcpy(&depthValue, sourceTexel, sizeof(depthValue));
            if (uploadLayout.maskDepthTo24Bit)
                depthValue &= 0x00FFFFFF;
            memcpy(depthData + i * depthBytesPerTexel, &depthValue, sizeof(depthValue));
            stencilData[i] = sourceTexel[uploadLayout.stencilOffset];
        }

        bufferAllocator.FlushReservation(depthAllocation);
        bufferAllocator.FlushReservation(stencilAllocation);

        const MTL::Size copySize(width, height, 1);
        const MTL::Origin destinationOrigin(0, 0, offsetZ);
        blitCommandEncoder->copyFromBuffer(depthAllocation.mtlBuffer, depthAllocation.bufferOffset, depthBytesPerRow, 0, copySize, textureMtl->GetTexture(), sliceIndex, mipIndex, destinationOrigin, MTL::BlitOptionDepthFromDepthStencil);
        blitCommandEncoder->copyFromBuffer(stencilAllocation.mtlBuffer, stencilAllocation.bufferOffset, stencilBytesPerRow, 0, copySize, textureMtl->GetTexture(), sliceIndex, mipIndex, destinationOrigin, MTL::BlitOptionStencilFromDepthStencil);
        return;
    }

    auto& bufferAllocator = m_memoryManager->GetStagingAllocator();
    auto allocation = bufferAllocator.AllocateBufferMemory(compressedImageSize, 1);
    if (!allocation.mtlBuffer && bytesPerRow != 0 && compressedImageSize > bytesPerRow)
    {
        const uint32 blockH = std::max<uint32>(1, formatInfo.blockTexelSize.y);
        const uint32 blockRowsTotal = ((uint32)uploadHeight + blockH - 1) / blockH;
        uint32 bandRows = std::max<uint32>(1, blockRowsTotal / 2);
        uint32 doneRows = 0;
        bool failed = false;
        while (doneRows < blockRowsTotal)
        {
            const uint32 rows = std::min(bandRows, blockRowsTotal - doneRows);
            const uint64 sourceOffset = (uint64)doneRows * bytesPerRow;
            if (sourceOffset >= compressedImageSize)
                break;
            const uint32 bandBytes = (uint32)std::min<uint64>((uint64)rows * bytesPerRow, compressedImageSize - sourceOffset);
            auto band = bufferAllocator.AllocateBufferMemory(bandBytes, 1);
            if (!band.mtlBuffer)
            {
                if (bandRows == 1)
                {
                    failed = true;
                    break;
                }
                bandRows = std::max<uint32>(1, bandRows / 2);
                continue;
            }
            memcpy(band.memPtr, static_cast<const uint8*>(pixelData) + sourceOffset, bandBytes);
            bufferAllocator.FlushReservation(band);
            const uint32 y0 = doneRows * blockH;
            const uint32 bandHeight = std::min<uint32>(rows * blockH, (uint32)uploadHeight - y0);
            blitCommandEncoder->copyFromBuffer(band.mtlBuffer, band.bufferOffset, bytesPerRow, 0, MTL::Size(uploadWidth, bandHeight, 1), textureMtl->GetTexture(), sliceIndex, mipIndex, MTL::Origin(0, y0, offsetZ), GetTextureUploadBlitOption(formatInfo.pixelFormat));
            doneRows += rows;
        }
        if (!failed)
        {
            cemuLog_logOnce(LogType::Force, "Metal: an upload of {} bytes (format {:04x} {}x{}) did not fit one staging reservation and went through in bands", compressedImageSize, (uint32)textureMtl->format, width, height);
            return;
        }
    }
    if (!allocation.mtlBuffer)
    {
        MetalGuardNote(MetalGuard::UploadStagingFull, {(uint64)(uint32)textureMtl->format, ((uint64)(uint32)width << 32) | (uint32)height}, [&] {
            return fmt::format("no staging memory for the upload of format {:04x} {}x{} mip {} ({} bytes)", (uint32)textureMtl->format, width, height, mipIndex, compressedImageSize);
        });
        MetalUploadSkipped(textureMtl, MetalGuard::UploadStagingFull, mipIndex);
        return;
    }
    memcpy(allocation.memPtr, pixelData, compressedImageSize);
    bufferAllocator.FlushReservation(allocation);

    blitCommandEncoder->copyFromBuffer(allocation.mtlBuffer, allocation.bufferOffset, bytesPerRow, 0, MTL::Size(uploadWidth, uploadHeight, 1), textureMtl->GetTexture(), sliceIndex, mipIndex, MTL::Origin(0, 0, offsetZ), GetTextureUploadBlitOption(formatInfo.pixelFormat));
}

void MetalRenderer::texture_clearColorSlice(LatteTexture* hostTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a)
{
    if (!FormatIsRenderable(hostTexture->format))
    {
        cemuLog_logOnce(LogType::Force, "cannot clear color texture with format {}, because it's not renderable", hostTexture->format);
        return;
    }

    auto mtlTexture = static_cast<LatteTextureMtl*>(hostTexture)->GetTexture();

    ClearColorTextureInternal(mtlTexture, sliceIndex, mipIndex, r, g, b, a);
}

void MetalRenderer::texture_clearDepthSlice(LatteTexture* hostTexture, uint32 sliceIndex, sint32 mipIndex, bool clearDepth, bool clearStencil, float depthValue, uint32 stencilValue)
{
    clearStencil = (clearStencil && GetMtlPixelFormatInfo(hostTexture->format, true).hasStencil);
    if (!clearDepth && !clearStencil)
    {
        cemuLog_logOnce(LogType::Force, "skipping depth/stencil clear");
        return;
    }

    auto mtlTexture = static_cast<LatteTextureMtl*>(hostTexture)->GetTexture();

    NS_STACK_SCOPED MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    if (clearDepth)
    {
        auto depthAttachment = renderPassDescriptor->depthAttachment();
        depthAttachment->setTexture(mtlTexture);
        depthAttachment->setClearDepth(depthValue);
        depthAttachment->setLoadAction(MTL::LoadActionClear);
        depthAttachment->setStoreAction(MTL::StoreActionStore);
        depthAttachment->setSlice(sliceIndex);
        depthAttachment->setLevel(mipIndex);
    }
    if (clearStencil)
    {
        auto stencilAttachment = renderPassDescriptor->stencilAttachment();
        stencilAttachment->setTexture(mtlTexture);
        stencilAttachment->setClearStencil(stencilValue);
        stencilAttachment->setLoadAction(MTL::LoadActionClear);
        stencilAttachment->setStoreAction(MTL::StoreActionStore);
        stencilAttachment->setSlice(sliceIndex);
        stencilAttachment->setLevel(mipIndex);
    }

    GetTemporaryRenderCommandEncoder(renderPassDescriptor);
    EndEncoding();

    m_performanceMonitor.m_clears++;
}

LatteTexture* MetalRenderer::texture_createTextureEx(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth, bool isRenderTarget)
{
    return new LatteTextureMtl(this, dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth, isRenderTarget);
}

void MetalRenderer::texture_setLatteTexture(LatteTextureView* textureView, uint32 textureUnit)
{
    m_state.m_textures[textureUnit] = static_cast<LatteTextureViewMtl*>(textureView);
}

void MetalRenderer::texture_notifyDelete(LatteTextureView* textureView)
{
    for (uint32 i = 0; i < std::size(m_state.m_textures); i++)
    {
        if (m_state.m_textures[i] == textureView)
            m_state.m_textures[i] = nullptr;
    }

    for (uint32 shaderType = 0; shaderType < METAL_SHADER_TYPE_TOTAL; shaderType++)
    {
        for (uint32 i = 0; i < MAX_MTL_TEXTURES; i++)
            m_state.m_encoderState.m_textures[shaderType][i] = nullptr;
    }
}

const char* MetalRenderer::CopyRawBlocksIntoTranscodedTexture(LatteTexture* src, sint32 srcMip, sint32 srcX, sint32 srcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 dstX, sint32 dstY, sint32 dstSlice, sint32 blocksW, sint32 blocksH, sint32 sliceCount)
{
    auto mtlSrc = static_cast<LatteTextureMtl*>(src)->GetTexture();
    auto mtlDst = static_cast<LatteTextureMtl*>(dst)->GetTexture();
    if (!mtlSrc || !mtlDst || src->isDepth || dst->isDepth)
        return "a texture is missing or is depth";
    const auto& srcInfo = GetMtlPixelFormatInfo(src->format, false);
    const auto& dstInfo = GetMtlPixelFormatInfo(dst->format, false);
    TextureDecoder* decoder = dstInfo.textureDecoder;
    if (!decoder)
        return "no decoder for the destination format";
    const uint32 latteBlockBytes = Latte::GetFormatBits(dst->format) / 8;
    if (srcInfo.blockTexelSize.x != 1 || srcInfo.blockTexelSize.y != 1 || srcInfo.bytesPerBlock != latteBlockBytes || (latteBlockBytes != 8 && latteBlockBytes != 16))
        return "the source blocks are not the size of the BC blocks";
    auto planar = [](MTL::Texture* texture) { return texture->textureType() == MTL::TextureType2D || texture->textureType() == MTL::TextureType2DArray; };
    if (!planar(mtlSrc) || !planar(mtlDst))
        return "only 2D textures are handled";
    if (srcMip < 0 || dstMip < 0 || (NS::UInteger)srcMip >= mtlSrc->mipmapLevelCount() || (NS::UInteger)dstMip >= mtlDst->mipmapLevelCount())
        return "a texture has no such mip level";
    if (srcX < 0 || srcY < 0 || dstX < 0 || dstY < 0 || (dstX & 3) || (dstY & 3) || srcSlice < 0 || dstSlice < 0 || sliceCount < 1 || blocksW < 1 || blocksH < 1)
        return "the copy is not aligned to whole blocks";

    const sint64 srcLevelW = std::max<sint64>(1, (sint64)mtlSrc->width() >> std::min(srcMip, 63));
    const sint64 srcLevelH = std::max<sint64>(1, (sint64)mtlSrc->height() >> std::min(srcMip, 63));
    const sint64 dstLevelW = std::max<sint64>(1, (sint64)mtlDst->width() >> std::min(dstMip, 63));
    const sint64 dstLevelH = std::max<sint64>(1, (sint64)mtlDst->height() >> std::min(dstMip, 63));
    if (srcX >= srcLevelW || srcY >= srcLevelH || dstX >= dstLevelW || dstY >= dstLevelH)
        return "the copy starts outside a level";
    const sint64 texelsW = std::min<sint64>((sint64)blocksW * 4, dstLevelW - dstX);
    const sint64 texelsH = std::min<sint64>((sint64)blocksH * 4, dstLevelH - dstY);
    const sint64 copyBlocksW = std::min<sint64>((texelsW + 3) / 4, srcLevelW - srcX);
    const sint64 copyBlocksH = std::min<sint64>((texelsH + 3) / 4, srcLevelH - srcY);
    if (copyBlocksW * copyBlocksH > 128 * 128)
        return "the region is too large to re-encode";
    const sint64 slices = std::min<sint64>({(sint64)sliceCount, (sint64)std::max<NS::UInteger>(1, mtlSrc->arrayLength()) - srcSlice, (sint64)std::max<NS::UInteger>(1, mtlDst->arrayLength()) - dstSlice});
    if (slices < 1)
        return "a slice the copy starts at does not exist";

    const size_t bytesPerRowIn = (size_t)copyBlocksW * latteBlockBytes;
    const size_t bytesPerImageIn = bytesPerRowIn * (size_t)copyBlocksH;
    MTL::Buffer* readback = m_device->newBuffer(bytesPerImageIn * (size_t)slices, MTL::ResourceStorageModeShared);
    if (!readback)
        return "no memory for the readback";

    {
        auto blit = GetBlitCommandEncoder(PerfTelemetry::kBreakTranscodeCopy);
        for (sint64 i = 0; i < slices; i++)
            blit->copyFromTexture(mtlSrc, (NS::UInteger)(srcSlice + i), (NS::UInteger)srcMip, MTL::Origin(srcX, srcY, 0), MTL::Size((NS::UInteger)copyBlocksW, (NS::UInteger)copyBlocksH, 1), readback, (NS::UInteger)(bytesPerImageIn * (size_t)i), bytesPerRowIn, bytesPerImageIn);
    }
    CommitCommandBuffer();
    bool finished = true;
    if (!m_executingCommandBuffers.empty())
        finished = WaitForCommandBuffer(m_executingCommandBuffers.back(), "re-encoding raw blocks into a transcoded texture");
    ProcessFinishedCommandBuffers();
    if (!finished)
    {
        readback->release();
        return "the GPU did not finish the readback";
    }

    const size_t bytesPerRowOut = GetMtlTextureBytesPerRow(dst->format, false, (uint32)texelsW);
    const size_t rowsOut = ((size_t)texelsH + std::max<uint32>(1, dstInfo.blockTexelSize.y) - 1) / std::max<uint32>(1, dstInfo.blockTexelSize.y);
    const uint8* readbackData = static_cast<const uint8*>(readback->contents());
    std::vector<std::vector<uint8>> encoded((size_t)slices);
    for (sint64 i = 0; i < slices; i++)
    {
        LatteTextureLoaderCtx ctx{};
        ctx.tileMode = Latte::E_HWTILEMODE::TM_LINEAR_GENERAL;
        ctx.bpp = latteBlockBytes * 8;
        ctx.stepX = 4;
        ctx.stepY = 4;
        ctx.width = (sint32)texelsW;
        ctx.height = (sint32)texelsH;
        ctx.pitch = (sint32)copyBlocksW;
        ctx.mipLevels = 1;
        ctx.surfaceInfoHeight = (uint32)copyBlocksH;
        ctx.surfaceInfoDepth = 1;
        ctx.inputData = const_cast<uint8*>(readbackData) + bytesPerImageIn * (size_t)i;
        const size_t outSize = (size_t)decoder->calculateImageSize(&ctx);
        if (outSize < bytesPerRowOut * rowsOut)
        {
            readback->release();
            return "the decoder output is smaller than the region";
        }
        encoded[(size_t)i].resize(outSize);
        decoder->decode(&ctx, encoded[(size_t)i].data());
    }
    readback->release();

    auto blit = GetBlitCommandEncoder(PerfTelemetry::kBreakTranscodeCopy);
    auto& bufferAllocator = m_memoryManager->GetStagingAllocator();
    for (sint64 i = 0; i < slices; i++)
    {
        auto allocation = bufferAllocator.AllocateBufferMemory((uint32)encoded[(size_t)i].size(), 1);
        if (!allocation.mtlBuffer)
            return "no staging memory for the re-encoded blocks";
        memcpy(allocation.memPtr, encoded[(size_t)i].data(), encoded[(size_t)i].size());
        bufferAllocator.FlushReservation(allocation);
        blit->copyFromBuffer(allocation.mtlBuffer, allocation.bufferOffset, bytesPerRowOut, 0, MTL::Size((NS::UInteger)texelsW, (NS::UInteger)texelsH, 1), mtlDst, (NS::UInteger)(dstSlice + i), (NS::UInteger)dstMip, MTL::Origin(dstX, dstY, 0), MTL::BlitOptionNone);
    }
    return nullptr;
}

void MetalRenderer::texture_copyImageSubData(LatteTexture* src, sint32 srcMip, sint32 effectiveSrcX, sint32 effectiveSrcY, sint32 srcSlice, LatteTexture* dst, sint32 dstMip, sint32 effectiveDstX, sint32 effectiveDstY, sint32 dstSlice, sint32 effectiveCopyWidth, sint32 effectiveCopyHeight, sint32 srcDepth_)
{
    const sint32 sourceCopyWidth = effectiveCopyWidth;
    const sint32 sourceCopyHeight = effectiveCopyHeight;

    Uvec2 srcBlockTexelSize = GetMtlPixelFormatInfo(src->format, src->isDepth).blockTexelSize;
    Uvec2 dstBlockTexelSize = GetMtlPixelFormatInfo(dst->format, dst->isDepth).blockTexelSize;
    if (srcBlockTexelSize.x != dstBlockTexelSize.x || srcBlockTexelSize.y != dstBlockTexelSize.y)
    {
        uint32 multX = (srcBlockTexelSize.x > dstBlockTexelSize.x ? srcBlockTexelSize.x / dstBlockTexelSize.x : dstBlockTexelSize.x / srcBlockTexelSize.x);
        effectiveCopyWidth *= multX;

        uint32 multY = (srcBlockTexelSize.y > dstBlockTexelSize.y ? srcBlockTexelSize.y / dstBlockTexelSize.y : dstBlockTexelSize.y / srcBlockTexelSize.y);
        effectiveCopyHeight *= multY;
    }

    auto mtlSrc = static_cast<LatteTextureMtl*>(src)->GetTexture();
    auto mtlDst = static_cast<LatteTextureMtl*>(dst)->GetTexture();

    {
        const bool blockMismatch = srcBlockTexelSize.x != dstBlockTexelSize.x || srcBlockTexelSize.y != dstBlockTexelSize.y;
        const sint64 srcLevelW = mtlSrc ? std::max<sint64>(1, (sint64)mtlSrc->width() >> std::min(std::max(srcMip, 0), 63)) : 0;
        const sint64 srcLevelH = mtlSrc ? std::max<sint64>(1, (sint64)mtlSrc->height() >> std::min(std::max(srcMip, 0), 63)) : 0;
        const sint64 dstLevelW = mtlDst ? std::max<sint64>(1, (sint64)mtlDst->width() >> std::min(std::max(dstMip, 0), 63)) : 0;
        const sint64 dstLevelH = mtlDst ? std::max<sint64>(1, (sint64)mtlDst->height() >> std::min(std::max(dstMip, 0), 63)) : 0;
        const uint64 keyFormats = ((uint64)(uint32)src->format << 32) | (uint32)dst->format;
        const uint64 keyMips = ((uint64)(uint32)srcMip << 32) | (uint32)dstMip;
        const uint64 keyLevels = ((uint64)(uint32)srcLevelW << 48) ^ ((uint64)(uint32)srcLevelH << 32) ^ ((uint64)(uint32)dstLevelW << 16) ^ (uint64)(uint32)dstLevelH;
        auto describe = [&](const char* why) {
            return fmt::format("{} - src format {:04x}{} level {}x{} mip {} slice {} at {},{} ({} block {}x{}), dst format {:04x}{} level {}x{} mip {} slice {} at {},{} ({} block {}x{}), region {}x{}, {} slices",
                why, (uint32)src->format, src->isDepth ? " depth" : "", srcLevelW, srcLevelH, srcMip, srcSlice, effectiveSrcX, effectiveSrcY,
                mtlSrc ? (uint64)mtlSrc->pixelFormat() : 0, srcBlockTexelSize.x, srcBlockTexelSize.y,
                (uint32)dst->format, dst->isDepth ? " depth" : "", dstLevelW, dstLevelH, dstMip, dstSlice, effectiveDstX, effectiveDstY,
                mtlDst ? (uint64)mtlDst->pixelFormat() : 0, dstBlockTexelSize.x, dstBlockTexelSize.y, effectiveCopyWidth, effectiveCopyHeight, srcDepth_);
        };

        if (!mtlSrc || !mtlDst || srcMip < 0 || dstMip < 0 || (NS::UInteger)srcMip >= mtlSrc->mipmapLevelCount() || (NS::UInteger)dstMip >= mtlDst->mipmapLevelCount())
        {
            MetalGuardNote(MetalGuard::CopyBadLevel, {keyFormats, keyMips, keyLevels}, [&] { return describe("a texture has no such mip level"); });
            return;
        }
        if (src->IsCompressedFormat() != dst->IsCompressedFormat())
        {
            MTL::Texture* compressedSide = src->IsCompressedFormat() ? mtlSrc : mtlDst;
            if (!MetalPixelFormatIsNativeBC(compressedSide->pixelFormat()))
            {
                const char* retranscodeFailure = "the compressed side is the source of the copy";
                if (dst->IsCompressedFormat() && !src->IsCompressedFormat())
                {
                    retranscodeFailure = CopyRawBlocksIntoTranscodedTexture(src, srcMip, effectiveSrcX, effectiveSrcY, srcSlice, dst, dstMip, effectiveDstX, effectiveDstY, dstSlice, sourceCopyWidth, sourceCopyHeight, srcDepth_);
                    if (!retranscodeFailure)
                    {
                        MetalGuardNote(MetalGuard::CopyRetranscoded, {keyFormats, keyMips, keyLevels}, [&] { return describe("raw blocks re-encoded into the transcoded compressed texture"); });
                        return;
                    }
                }
                MetalGuardNote(MetalGuard::CopyTranscodedBlocks, {keyFormats, keyMips, keyLevels}, [&] { return describe(fmt::format("raw blocks cannot be exchanged with a compressed texture this GPU stores as a transcode: {}", retranscodeFailure).c_str()); });
                return;
            }
        }
        if (effectiveSrcX < 0 || effectiveSrcY < 0 || effectiveDstX < 0 || effectiveDstY < 0 || effectiveSrcX >= srcLevelW || effectiveSrcY >= srcLevelH || effectiveDstX >= dstLevelW || effectiveDstY >= dstLevelH)
        {
            MetalGuardNote(MetalGuard::CopyStartOutside, {keyFormats, keyMips, keyLevels, (uint64)(uint32)effectiveSrcX, (uint64)(uint32)effectiveSrcY, (uint64)(uint32)effectiveDstX, (uint64)(uint32)effectiveDstY}, [&] { return describe("the copy starts outside a level"); });
            return;
        }
        {
            const uint32 srcBytesPerBlock = (uint32)GetMtlPixelFormatInfo(src->format, src->isDepth).bytesPerBlock;
            const uint32 dstBytesPerBlock = (uint32)GetMtlPixelFormatInfo(dst->format, dst->isDepth).bytesPerBlock;
            if (srcBytesPerBlock != dstBytesPerBlock)
            {
                MetalGuardNote(MetalGuard::CopyBytesPerBlockMismatch, {keyFormats, keyMips, keyLevels, ((uint64)srcBytesPerBlock << 32) | dstBytesPerBlock}, [&] {
                    return describe(fmt::format("blocks of {} bytes cannot be copied into blocks of {} bytes; skipped", srcBytesPerBlock, dstBytesPerBlock).c_str());
                });
                return;
            }
        }
        const sint64 srcRoomW = srcLevelW - effectiveSrcX, srcRoomH = srcLevelH - effectiveSrcY;
        const sint64 dstRoomW = dstLevelW - effectiveDstX, dstRoomH = dstLevelH - effectiveDstY;
        sint64 fitW, fitH;
        if (!blockMismatch)
        {
            fitW = std::min(srcRoomW, dstRoomW);
            fitH = std::min(srcRoomH, dstRoomH);
        }
        else
        {
            auto fitAxis = [](sint64 srcRoom, sint64 dstRoom, uint32 srcBlock, uint32 dstBlock) -> sint64 {
                const sint64 sb = std::max<sint64>(1, srcBlock), db = std::max<sint64>(1, dstBlock);
                const sint64 blocks = std::min((srcRoom + sb - 1) / sb, (dstRoom + db - 1) / db);
                return std::min(srcRoom, blocks * sb);
            };
            fitW = fitAxis(srcRoomW, dstRoomW, srcBlockTexelSize.x, dstBlockTexelSize.x);
            fitH = fitAxis(srcRoomH, dstRoomH, srcBlockTexelSize.y, dstBlockTexelSize.y);
            MetalGuardNote(MetalGuard::CopyBlockMismatch, {keyFormats, keyMips, keyLevels, (uint64)(uint32)effectiveCopyWidth, (uint64)(uint32)effectiveCopyHeight}, [&] { return describe("copy between different block sizes"); });
        }
        if (effectiveCopyWidth > fitW || effectiveCopyHeight > fitH)
        {
            MetalGuardNote(MetalGuard::CopyClampedRegion, {keyFormats, keyMips, keyLevels, (uint64)(uint32)effectiveCopyWidth, (uint64)(uint32)effectiveCopyHeight, (uint64)(uint32)effectiveSrcX, (uint64)(uint32)effectiveSrcY, (uint64)(uint32)effectiveDstX, (uint64)(uint32)effectiveDstY},
                [&] { return describe(fmt::format("region reaches past a level, cut to {}x{}", std::min<sint64>(effectiveCopyWidth, fitW), std::min<sint64>(effectiveCopyHeight, fitH)).c_str()); });
            effectiveCopyWidth = (sint32)std::min<sint64>(effectiveCopyWidth, fitW);
            effectiveCopyHeight = (sint32)std::min<sint64>(effectiveCopyHeight, fitH);
        }
        if (effectiveCopyWidth <= 0 || effectiveCopyHeight <= 0)
        {
            MetalGuardNote(MetalGuard::CopyStartOutside, {keyFormats, keyMips, keyLevels, (uint64)(uint32)effectiveCopyWidth, (uint64)(uint32)effectiveCopyHeight}, [&] { return describe("empty region"); });
            return;
        }

        auto slicesFrom = [](MTL::Texture* texture, sint32 level, sint64 firstSlice) -> sint64 {
            switch (texture->textureType())
            {
            case MTL::TextureType3D:
                return std::max<sint64>(1, (sint64)texture->depth() >> std::min(std::max(level, 0), 63)) - firstSlice;
            case MTL::TextureTypeCube:
                return 6 - firstSlice;
            case MTL::TextureTypeCubeArray:
                return (sint64)std::max<NS::UInteger>(1, texture->arrayLength()) * 6 - firstSlice;
            default:
                return (sint64)std::max<NS::UInteger>(1, texture->arrayLength()) - firstSlice;
            }
        };
        if (srcSlice < 0 || dstSlice < 0 || srcDepth_ < 1)
        {
            MetalGuardNote(MetalGuard::CopyNoSlices, {keyFormats, keyMips, keyLevels, (uint64)(uint32)srcSlice, (uint64)(uint32)dstSlice, (uint64)(uint32)srcDepth_}, [&] { return describe("the copy names no valid slice"); });
            return;
        }
        const sint64 slicesAvailable = std::min(slicesFrom(mtlSrc, srcMip, srcSlice), slicesFrom(mtlDst, dstMip, dstSlice));
        if (slicesAvailable < 1)
        {
            MetalGuardNote(MetalGuard::CopyNoSlices, {keyFormats, keyMips, keyLevels, (uint64)(uint32)srcSlice, (uint64)(uint32)dstSlice, (uint64)(uint32)srcDepth_}, [&] { return describe("a slice the copy starts at does not exist"); });
            return;
        }
        if (srcDepth_ > slicesAvailable)
        {
            MetalGuardNote(MetalGuard::CopyClampedSlices, {keyFormats, keyMips, keyLevels, (uint64)(uint32)srcSlice, (uint64)(uint32)dstSlice, (uint64)(uint32)srcDepth_}, [&] { return describe(fmt::format("more slices than both textures have, cut to {}", slicesAvailable).c_str()); });
            srcDepth_ = (sint32)slicesAvailable;
        }
    }

    if (m_encoderType == MetalEncoderType::Render && m_state.m_lastUsedFBO.m_fbo)
    {
        bool touches = false;
        auto* fbo = m_state.m_lastUsedFBO.m_fbo;
        for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
            if (fbo->colorBuffer[i].texture && (fbo->colorBuffer[i].texture->baseTexture == src || fbo->colorBuffer[i].texture->baseTexture == dst))
                touches = true;
        if (fbo->depthBuffer.texture && (fbo->depthBuffer.texture->baseTexture == src || fbo->depthBuffer.texture->baseTexture == dst))
            touches = true;
        if (!touches)
            PerfTelemetry::Get().imageCopyNotTouchingPass.fetch_add(1, std::memory_order_relaxed);
    }
    auto blitCommandEncoder = GetBlitCommandEncoder(PerfTelemetry::kBreakImageCopy);

    uint32 srcBaseLayer = 0;
    uint32 dstBaseLayer = 0;
    uint32 srcOffsetZ = 0;
    uint32 dstOffsetZ = 0;
    uint32 srcLayerCount = 1;
    uint32 dstLayerCount = 1;
    uint32 srcDepth = 1;
    uint32 dstDepth = 1;

    if (src->Is3DTexture())
    {
        srcOffsetZ = srcSlice;
        srcDepth = srcDepth_;
    }
    else
    {
        srcBaseLayer = srcSlice;
        srcLayerCount = srcDepth_;
    }

    if (dst->Is3DTexture())
    {
        dstOffsetZ = dstSlice;
        dstDepth = srcDepth_;
    }
    else
    {
        dstBaseLayer = dstSlice;
        dstLayerCount = srcDepth_;
    }

    if (effectiveSrcX == 0 && effectiveSrcY == 0 && effectiveDstX == 0 && effectiveDstY == 0 &&
        srcOffsetZ == 0 && dstOffsetZ == 0 &&
        effectiveCopyWidth == src->GetMipWidth(srcMip) && effectiveCopyHeight == src->GetMipHeight(srcMip) && srcDepth == src->GetMipDepth(srcMip) &&
        effectiveCopyWidth == dst->GetMipWidth(dstMip) && effectiveCopyHeight == dst->GetMipHeight(dstMip) && dstDepth == dst->GetMipDepth(dstMip) &&
        srcLayerCount == dstLayerCount)
    {
        blitCommandEncoder->copyFromTexture(mtlSrc, srcBaseLayer, srcMip, mtlDst, dstBaseLayer, dstMip, srcLayerCount, 1);
    }
    else
    {
        if (srcLayerCount == dstLayerCount)
        {
            for (uint32 i = 0; i < srcLayerCount; i++)
            {
                blitCommandEncoder->copyFromTexture(mtlSrc, srcBaseLayer + i, srcMip, MTL::Origin(effectiveSrcX, effectiveSrcY, srcOffsetZ), MTL::Size(effectiveCopyWidth, effectiveCopyHeight, srcDepth), mtlDst, dstBaseLayer + i, dstMip, MTL::Origin(effectiveDstX, effectiveDstY, dstOffsetZ));
            }
        }
        else
        {
            for (uint32 i = 0; i < std::max(srcLayerCount, dstLayerCount); i++)
            {
                const uint32 currentSrcLayer = srcBaseLayer + (srcLayerCount == 1 ? 0 : i);
                const uint32 currentDstLayer = dstBaseLayer + (dstLayerCount == 1 ? 0 : i);
                const uint32 currentSrcZ = srcOffsetZ + (srcLayerCount == 1 ? i : 0);
                const uint32 currentDstZ = dstOffsetZ + (dstLayerCount == 1 ? i : 0);

                blitCommandEncoder->copyFromTexture(mtlSrc, currentSrcLayer, srcMip, MTL::Origin(effectiveSrcX, effectiveSrcY, currentSrcZ), MTL::Size(effectiveCopyWidth, effectiveCopyHeight, 1), mtlDst, currentDstLayer, dstMip, MTL::Origin(effectiveDstX, effectiveDstY, currentDstZ));
            }
        }
    }
}

LatteTextureReadbackInfo* MetalRenderer::texture_createReadback(LatteTextureView* textureView)
{
    if (!GetTextureReadbackBuffer())
    {
        cemuLog_logOnce(LogType::Force,
            "Metal: could not allocate the {} MB texture readback buffer; skipping texture readbacks",
            TextureReadbackSize() / (1024 * 1024));
        return nullptr;
    }

    MTL::Texture* mtlTexture = static_cast<LatteTextureMtl*>(textureView->baseTexture)->GetTexture();
    if (!mtlTexture)
        return nullptr;

    size_t uploadSize = mtlTexture->allocatedSize();
    if (uploadSize > TextureReadbackSize())
    {
        cemuLog_logOnce(LogType::Force, "Metal: texture is too large for the {} MB readback buffer; skipping readback", TextureReadbackSize() / (1024 * 1024));
        return nullptr;
    }

    if ((m_readbackBufferWriteOffset + uploadSize) > TextureReadbackSize())
    {
        m_readbackBufferWriteOffset = 0;
    }

    auto* result = new LatteTextureReadbackInfoMtl(this, textureView, m_readbackBufferWriteOffset);
    m_readbackBufferWriteOffset += uploadSize;

    return result;
}

void MetalRenderer::surfaceCopy_copySurfaceWithFormatConversion(LatteTexture* sourceTexture, sint32 srcMip, sint32 srcSlice, LatteTexture* destinationTexture, sint32 dstMip, sint32 dstSlice, sint32 width, sint32 height)
{
    sint32 effectiveCopyWidth = width;
    sint32 effectiveCopyHeight = height;
    LatteTexture_scaleToEffectiveSize(sourceTexture, &effectiveCopyWidth, &effectiveCopyHeight, 0);

    if (sourceTexture->isDepth == destinationTexture->isDepth)
    {
        cemu_assert_suspicious();
        return;
    }
    if (!LatteTexture_doesEffectiveRescaleRatioMatch(sourceTexture, srcMip, destinationTexture, dstMip))
    {
        cemuLog_logDebug(LogType::Force, "Metal surface copy with format conversion has mismatching dimensions");
        return;
    }
    if (sourceTexture->GetBPP() != destinationTexture->GetBPP())
    {
        cemuLog_logDebug(LogType::Force, "Metal surface copy with format conversion has mismatching BPP");
        return;
    }

    auto sourceView = static_cast<LatteTextureViewMtl*>(sourceTexture->GetOrCreateView(Latte::E_DIM::DIM_2D, sourceTexture->format, srcMip, 1, srcSlice, 1));
    auto destinationTextureMtl = static_cast<LatteTextureMtl*>(destinationTexture);
    MTL::Texture* destinationMtl = destinationTextureMtl->GetTexture();

    NS_STACK_SCOPED MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    MTL::RenderPipelineState* pipeline = nullptr;
    if (destinationTexture->isDepth)
    {
        const auto& formatInfo = GetMtlPixelFormatInfo(destinationTexture->format, true);
        auto depthAttachment = renderPassDescriptor->depthAttachment();
        depthAttachment->setTexture(destinationMtl);
        depthAttachment->setLevel(dstMip);
        depthAttachment->setSlice(dstSlice);
        depthAttachment->setLoadAction(MTL::LoadActionLoad);
        depthAttachment->setStoreAction(MTL::StoreActionStore);

        if (formatInfo.hasStencil)
        {
            auto stencilAttachment = renderPassDescriptor->stencilAttachment();
            stencilAttachment->setTexture(destinationMtl);
            stencilAttachment->setLevel(dstMip);
            stencilAttachment->setSlice(dstSlice);
            stencilAttachment->setLoadAction(MTL::LoadActionLoad);
            stencilAttachment->setStoreAction(MTL::StoreActionStore);
        }

        auto& cachedPipeline = m_copyColorToDepthPipelines[formatInfo.pixelFormat];
        if (!cachedPipeline)
        {
            m_copyColorToDepthDesc->setDepthAttachmentPixelFormat(formatInfo.pixelFormat);
            m_copyColorToDepthDesc->setStencilAttachmentPixelFormat(formatInfo.hasStencil ? formatInfo.pixelFormat : MTL::PixelFormatInvalid);
            NS::Error* error = nullptr;
            cachedPipeline = m_device->newRenderPipelineState(m_copyColorToDepthDesc, &error);
            if (error)
                cemuLog_log(LogType::Force, "Failed to create Metal color-to-depth copy pipeline: {}", error->localizedDescription()->utf8String());
        }
        pipeline = cachedPipeline;
    }
    else
    {
        const MTL::PixelFormat pixelFormat = destinationMtl->pixelFormat();
        auto colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
        colorAttachment->setTexture(destinationMtl);
        colorAttachment->setLevel(dstMip);
        colorAttachment->setSlice(dstSlice);
        colorAttachment->setLoadAction(MTL::LoadActionLoad);
        colorAttachment->setStoreAction(MTL::StoreActionStore);

        auto& cachedPipeline = m_copyDepthToColorPipelines[pixelFormat];
        if (!cachedPipeline)
        {
            m_copyDepthToColorDesc->colorAttachments()->object(0)->setPixelFormat(pixelFormat);
            NS::Error* error = nullptr;
            cachedPipeline = m_device->newRenderPipelineState(m_copyDepthToColorDesc, &error);
            if (error)
                cemuLog_log(LogType::Force, "Failed to create Metal depth-to-color copy pipeline: {}", error->localizedDescription()->utf8String());
        }
        pipeline = cachedPipeline;
    }

    if (!pipeline)
        return;

    auto renderCommandEncoder = GetTemporaryRenderCommandEncoder(renderPassDescriptor);
    renderCommandEncoder->setRenderPipelineState(pipeline);
    if (destinationTexture->isDepth)
        renderCommandEncoder->setDepthStencilState(m_copyColorToDepthState);
    renderCommandEncoder->setViewport(MTL::Viewport{0.0, 0.0, (double)effectiveCopyWidth, (double)effectiveCopyHeight, 0.0, 1.0});
    {
        const uint64 levelWidth = std::max<uint64>(1, (uint64)destinationMtl->width() >> dstMip);
        const uint64 levelHeight = std::max<uint64>(1, (uint64)destinationMtl->height() >> dstMip);
        const uint64 scissorWidth = std::min<uint64>((uint32)effectiveCopyWidth, levelWidth), scissorHeight = std::min<uint64>((uint32)effectiveCopyHeight, levelHeight);
        if (scissorWidth < (uint32)effectiveCopyWidth || scissorHeight < (uint32)effectiveCopyHeight)
        {
            MetalGuardNote(MetalGuard::SurfaceCopyScissorClamped, {(uint64)(uint32)destinationTexture->format, (uint64)(uint32)effectiveCopyWidth, (uint64)(uint32)effectiveCopyHeight, levelWidth, levelHeight}, [&] {
                return fmt::format("surface copy of {}x{} into format {:04x} mip {} ({}x{} level) cut to {}x{}", effectiveCopyWidth, effectiveCopyHeight, (uint32)destinationTexture->format, dstMip, levelWidth, levelHeight, scissorWidth, scissorHeight);
            });
        }
        renderCommandEncoder->setScissorRect(MTL::ScissorRect{0, 0, (NS::UInteger)scissorWidth, (NS::UInteger)scissorHeight});
    }
    SetTexture(renderCommandEncoder, METAL_SHADER_TYPE_FRAGMENT, sourceView->GetRGBAView(), GET_HELPER_TEXTURE_BINDING(0));
    renderCommandEncoder->drawPrimitives(MTL::PrimitiveTypeTriangle, NS::UInteger(0), NS::UInteger(3));
    EndEncoding();
}

void MetalRenderer::bufferCache_init(const sint32 bufferSize)
{
    m_memoryManager->InitBufferCache(bufferSize);
}

sint32 MetalRenderer::bufferCache_getGrantedSize(sint32 requestedSize)
{
    const size_t granted = m_memoryManager->GetBufferCacheSize();
    return granted != 0 ? static_cast<sint32>(std::min<size_t>(granted, static_cast<size_t>(requestedSize))) : requestedSize;
}

void MetalRenderer::bufferCache_upload(uint8* buffer, sint32 size, uint32 bufferOffset)
{
    m_memoryManager->UploadToBufferCache(buffer, bufferOffset, size);
}

void MetalRenderer::bufferCache_copy(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
    m_memoryManager->CopyBufferCache(srcOffset, dstOffset, size);
}

void MetalRenderer::bufferCache_copyStreamoutToMainBuffer(uint32 srcOffset, uint32 dstOffset, uint32 size)
{
    MTL::Buffer* dstBuffer = m_memoryManager->GetBufferCache();
    size_t dstBufferOffset = dstOffset;
    if (m_memoryManager->UseHostMemoryForCache())
    {
        if (m_memoryManager->IsRangeImported(dstOffset, size))
        {
            dstBuffer = m_memoryManager->GetImportedMemoryBuffer();
            dstBufferOffset = m_memoryManager->GetImportedMemoryOffset(dstOffset);
        }
        else
        {
            dstBufferOffset = LatteBufferCache_retrieveDataInCache(dstOffset, size);
        }
    }

    if (!dstBuffer)
        return;

    m_nextBufferCopyBreak = PerfTelemetry::kBreakStreamout;
    CopyBufferToBuffer(GetXfbRingBuffer(), srcOffset, dstBuffer, dstBufferOffset, size, MTL::RenderStageVertex | MTL::RenderStageMesh, ALL_MTL_RENDER_STAGES);
    m_memoryManager->TrackSharedCache(dstBuffer, dstBufferOffset, size, true);
}

MTL::Buffer* MetalRenderer::GetXfbRingBuffer(size_t minimumSize)
{
    const size_t initialCapacity = static_cast<size_t>(LatteStreamout_GetRingBufferSize());
    minimumSize = std::max(minimumSize, initialCapacity);
    if (m_xfbRingBuffer && m_xfbRingBuffer->length() >= minimumSize)
        return m_xfbRingBuffer;

    if (minimumSize > m_device->maxBufferLength())
    {
        cemuLog_logOnce(LogType::Force, "Metal streamout allocation exceeds the device buffer limit: {} bytes", minimumSize);
        return nullptr;
    }

    size_t allocationSize = m_xfbRingBuffer ? m_xfbRingBuffer->length() : initialCapacity;
    while (allocationSize < minimumSize && allocationSize <= (std::numeric_limits<size_t>::max() / 2))
        allocationSize *= 2;
    if (allocationSize < minimumSize)
        allocationSize = Align(minimumSize, 1024 * 1024);
    allocationSize = std::min<size_t>(allocationSize, m_device->maxBufferLength());

    MTL::Buffer* newBuffer = m_device->newBuffer(allocationSize, MTL::ResourceStorageModePrivate);
    if (!newBuffer)
    {
        cemuLog_logOnce(LogType::Force, "Failed to allocate {} byte Metal streamout buffer", allocationSize);
        return nullptr;
    }
#ifdef CEMU_DEBUG_ASSERT
    newBuffer->setLabel(GetLabel("Transform feedback buffer", newBuffer));
#endif
    if (m_xfbRingBuffer)
        m_retiredXfbRingBuffers.emplace_back(m_xfbRingBuffer);
    m_xfbRingBuffer = newBuffer;
    return m_xfbRingBuffer;
}

MTL::Texture* MetalRenderer::GetNullSampledTexture(Latte::E_DIM dim, bool integerFormat, bool depthFormat)
{
    const uint32 key = static_cast<uint32>(dim) | (integerFormat ? 0x100u : 0u) | (depthFormat ? 0x200u : 0u);
    auto& texture = m_nullSampledTextures[key];
    if (texture)
        return texture;

    NS_STACK_SCOPED MTL::TextureDescriptor* descriptor = MTL::TextureDescriptor::alloc()->init();
    descriptor->setWidth(1);
    descriptor->setHeight(1);
    descriptor->setDepth(1);
    descriptor->setArrayLength(1);
    descriptor->setMipmapLevelCount(1);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    descriptor->setPixelFormat(depthFormat ? MTL::PixelFormatDepth32Float : (integerFormat ? MTL::PixelFormatRGBA8Uint : MTL::PixelFormatRGBA8Unorm));

    switch (dim)
    {
        case Latte::E_DIM::DIM_1D:
            descriptor->setTextureType(MTL::TextureType1D);
            break;
        case Latte::E_DIM::DIM_2D:
        case Latte::E_DIM::DIM_2D_MSAA:
            descriptor->setTextureType(MTL::TextureType2D);
            break;
        case Latte::E_DIM::DIM_2D_ARRAY:
        case Latte::E_DIM::DIM_2D_ARRAY_MSAA:
            descriptor->setTextureType(MTL::TextureType2DArray);
            break;
        case Latte::E_DIM::DIM_CUBEMAP:
            descriptor->setTextureType(MTL::TextureTypeCubeArray);
            break;
        case Latte::E_DIM::DIM_3D:
            descriptor->setTextureType(MTL::TextureType3D);
            break;
        default:
            descriptor->setTextureType(MTL::TextureType2D);
            break;
    }

    texture = m_device->newTexture(descriptor);
#ifdef CEMU_DEBUG_ASSERT
    if (texture)
        texture->setLabel(GetLabel("Typed null sampled texture", texture));
#endif
    return texture ? texture : m_nullTexture2D;
}

void MetalRenderer::buffer_bindVertexBuffer(uint32 bufferIndex, uint32 offset, uint32 size)
{
    cemu_assert_debug(bufferIndex < LATTE_MAX_VERTEX_BUFFERS);

    MTL::Buffer* buffer = m_memoryManager->GetBufferCache();
    if (!buffer || offset >= buffer->length())
    {
        m_state.m_vertexBuffers[bufferIndex] = nullptr;
        m_state.m_vertexBufferOffsets[bufferIndex] = INVALID_OFFSET;
        m_state.m_vertexBufferSizes[bufferIndex] = 0;
        m_state.m_vertexBufferRequired[bufferIndex] = size;
        return;
    }

    m_state.m_vertexBuffers[bufferIndex] = buffer;
    m_state.m_vertexBufferOffsets[bufferIndex] = offset;
    m_state.m_vertexBufferSizes[bufferIndex] = std::min<size_t>(size, buffer->length() - offset);
    m_state.m_vertexBufferRequired[bufferIndex] = size;
}

void MetalRenderer::buffer_bindUniformBuffer(LatteConst::ShaderType shaderType, uint32 bufferIndex, uint32 offset, uint32 size)
{
    cemu_assert_debug(bufferIndex < 16);
    MetalGeneralShaderType mtlShaderType = GetMtlGeneralShaderType(shaderType);
    cemu_assert_debug(mtlShaderType < METAL_GENERAL_SHADER_TYPE_TOTAL);

    if (size == 0)
    {
        m_state.m_uniformBuffers[mtlShaderType][bufferIndex] = nullptr;
        m_state.m_uniformBufferOffsets[mtlShaderType][bufferIndex] = INVALID_OFFSET;
        m_state.m_uniformBufferSizes[mtlShaderType][bufferIndex] = 0;
        return;
    }

    m_state.m_uniformBuffers[mtlShaderType][bufferIndex] = m_memoryManager->GetBufferCache();
    m_state.m_uniformBufferOffsets[mtlShaderType][bufferIndex] = offset;
    m_state.m_uniformBufferSizes[mtlShaderType][bufferIndex] = size;
}

RendererShader* MetalRenderer::shader_create(RendererShader::ShaderType type, uint64 baseHash, uint64 auxHash, const std::string& source, bool isGameShader, bool isGfxPackShader)
{
    return new RendererShaderMtl(this, type, baseHash, auxHash, isGameShader, isGfxPackShader, source);
}

void MetalRenderer::streamout_setupXfbBuffer(uint32 bufferIndex, sint32 ringBufferOffset, uint32 rangeAddr, uint32 rangeSize)
{
    cemu_assert_debug(bufferIndex < LATTE_NUM_STREAMOUT_BUFFER);
    auto& streamoutBuffer = m_state.m_streamoutState.buffers[bufferIndex];
    streamoutBuffer = {};
    if (ringBufferOffset < 0)
        return;

    const uint64 requiredSize = static_cast<uint64>(ringBufferOffset) + rangeSize;
    if (requiredSize > std::numeric_limits<size_t>::max() || !GetXfbRingBuffer(static_cast<size_t>(requiredSize)))
        return;

    streamoutBuffer.enabled = rangeSize != 0;
    streamoutBuffer.ringBufferOffset = static_cast<uint32>(ringBufferOffset);
    streamoutBuffer.rangeSize = rangeSize;
}

void MetalRenderer::streamout_begin()
{
}

void MetalRenderer::streamout_rendererFinishDrawcall()
{
    m_state.m_streamoutState = {};
}

void MetalRenderer::draw_beginSequence()
{
    PerfTelemetry::SampledTimer beginTimer(PerfTelemetry::Get().gpuBeginNs);
    m_state.m_skipDrawSequence = false;

    if (LatteWait::Get().gpuError.load(std::memory_order_relaxed))
    {
        m_state.m_skipDrawSequence = true;
        return;
    }

    bool streamoutEnable = LatteGPUState.contextRegister[mmVGT_STRMOUT_EN] != 0;

    LatteSHRC_UpdateActiveShaders();
    if (LatteGPUState.activeShaderHasError)
    {
        cemuLog_logOnce(LogType::Force, "Skipping drawcalls due to shader error\n");
        m_state.m_skipDrawSequence = true;
        cemu_assert_debug(false);
        return;
    }

    LatteGPUState.requiresTextureBarrier = false;
    while (true)
    {
        LatteGPUState.repeatTextureInitialization = false;
        if (!LatteMRT::UpdateCurrentFBO())
        {
            cemuLog_logOnce(LogType::Force, "Rendertarget invalid\n");
            m_state.m_skipDrawSequence = true;
            return;
        }

        if (!hasValidFramebufferAttached && !streamoutEnable)
        {
            cemuLog_logOnce(LogType::Force, "Drawcall with no color buffer or depth buffer attached\n");
            m_state.m_skipDrawSequence = true;
            return;
        }
        {
            PerfTelemetry::SampledTimer texTimer(PerfTelemetry::Get().gpuTexUpdateNs);
            LatteTexture_updateTextures();
        }
        if (!LatteGPUState.repeatTextureInitialization)
            break;
    }

    LatteMRT::ApplyCurrentState();

    LatteRenderTarget_updateViewport();
    LatteRenderTarget_updateScissorBox();

    if (!LatteGPUState.contextNew.IsRasterizationEnabled() && !streamoutEnable)
        m_state.m_skipDrawSequence = true;
}

void MetalRenderer::draw_execute(uint32 baseVertex, uint32 baseInstance, uint32 instanceCount, uint32 count, MPTR indexDataMPTR, Latte::LATTE_VGT_DMA_INDEX_TYPE::E_INDEX_TYPE indexType, bool isFirst)
{
    PerfTelemetry::SampledTimer execTimer(PerfTelemetry::Get().gpuExecNs);
    PerfTelemetry::Get().gpuDraws.fetch_add(1, std::memory_order_relaxed);
    if (m_state.m_skipDrawSequence)
    {
        LatteGPUState.drawCallCounter++;
        return;
    }

    if (LatteGPUState.contextNew.GetSpecialStateValues()[8] != 0)
    {
        LatteDraw_handleSpecialState8_clearAsDepth();
        LatteGPUState.drawCallCounter++;
        return;
    }
    else if (LatteGPUState.contextNew.GetSpecialStateValues()[5] != 0)
    {
        draw_handleSpecialState5();
        LatteGPUState.drawCallCounter++;
        return;
    }

    auto& encoderState = m_state.m_encoderState;
    m_crumb = nullptr;
    uint32 suspectFlags = 0;

    LatteDecompilerShader* vertexShader = LatteSHRC_GetActiveVertexShader();
    LatteDecompilerShader* geometryShader = LatteSHRC_GetActiveGeometryShader();
    LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
    const auto fetchShader = LatteSHRC_GetActiveFetchShader();

    if (!m_state.m_isFirstDrawInRenderPass)
    {
        bool endRenderPass = CheckIfRenderPassNeedsFlush(pixelShader);
        if (!endRenderPass)
            endRenderPass = CheckIfRenderPassNeedsFlush(vertexShader);
        if (!endRenderPass && geometryShader)
            endRenderPass = CheckIfRenderPassNeedsFlush(geometryShader);

        if (endRenderPass)
        {
            EndEncoding();
            cemuLog_logOnce(LogType::Force, "Ending Metal render pass due to render target self-dependency");
        }
    }

    const LattePrimitiveMode primitiveMode = LatteGPUState.contextNew.VGT_PRIMITIVE_TYPE.get_PRIMITIVE_MODE();
    auto mtlPrimitiveType = GetMtlPrimitiveType(primitiveMode);

    bool usesGeometryShader = UseGeometryShader(LatteGPUState.contextNew, geometryShader != nullptr);
    if (usesGeometryShader && !m_supportsMeshShaders)
        return;

    const bool usesVertexStreamout = !usesGeometryShader && vertexShader->hasStreamoutBufferWrite;
    bool fetchVertexManually = usesGeometryShader || usesVertexStreamout || fetchShader->mtlFetchVertexManually;

    PrepareOcclusionQueryDraw();

    Renderer::INDEX_TYPE hostIndexType;
    uint32 hostIndexCount;
    uint32 indexMin = 0;
    uint32 indexMax = 0;
    Renderer::IndexAllocation indexAllocation;
    {
        PerfTelemetry::SampledTimer indexTimer(PerfTelemetry::Get().gpuIndexNs);
        LatteIndices_decode(memory_getPointerFromVirtualOffset(indexDataMPTR), indexType, count, primitiveMode, indexMin, indexMax, hostIndexType, hostIndexCount, indexAllocation);
    }
    auto indexAllocationMtl = static_cast<MetalSynchronizedHeapAllocator::AllocatorReservation*>(indexAllocation.rendererInternal);
    const sint32 signedBaseVertex = static_cast<sint32>(baseVertex);
    if (indexAllocationMtl && hostIndexType != INDEX_TYPE::NONE)
    {
        const uint64 indexBytes = hostIndexType == INDEX_TYPE::U16 ? 2 : 4;
        const uint64 bufferLength = indexAllocationMtl->mtlBuffer ? indexAllocationMtl->mtlBuffer->length() : 0;
        const uint64 reachable = indexAllocationMtl->bufferOffset < bufferLength ? bufferLength - indexAllocationMtl->bufferOffset : 0;
        const uint64 capacity = std::min<uint64>(indexAllocationMtl->size, reachable);
        if ((uint64)hostIndexCount * indexBytes > capacity)
        {
            MetalGuardNote(MetalGuard::IndexClamped, {hostIndexCount, indexBytes, capacity}, [&] {
                return fmt::format("index count {} ({} bytes each) does not fit its {} byte allocation (offset {}, buffer length {}); drawing {} indices instead", hostIndexCount, indexBytes, capacity, (uint64)indexAllocationMtl->bufferOffset, bufferLength, capacity / indexBytes);
            });
            hostIndexCount = static_cast<uint32>(capacity / indexBytes);
            suspectFlags |= MetalDrawBreadcrumb::SUSPECT_INDEX_BUFFER;
        }
    }
    m_state.m_drawResources.indexBuffer = indexAllocationMtl ? indexAllocationMtl->mtlBuffer : nullptr;
    m_state.m_drawResources.indexBufferOffset = indexAllocationMtl ? indexAllocationMtl->bufferOffset : 0;
    m_state.m_drawResources.indexBufferSize = indexAllocationMtl ? indexAllocationMtl->size : 0;
    m_state.m_drawResources.indexType = static_cast<uint32>(hostIndexType);
    m_state.m_drawResources.baseVertex = signedBaseVertex;
    m_state.m_drawResources.baseInstance = baseInstance;

    uint32 minVertexIndex = baseVertex;
    uint32 maxVertexIndex = count > 0 ? baseVertex + count - 1 : baseVertex;
    if (hostIndexType != INDEX_TYPE::NONE)
    {
        sint64 signedMinVertexIndex = (sint64)indexMin + signedBaseVertex;
        sint64 signedMaxVertexIndex = (sint64)indexMax + signedBaseVertex;
        minVertexIndex = signedMinVertexIndex <= 0 ? 0 : (uint32)std::min<sint64>(signedMinVertexIndex, std::numeric_limits<uint32>::max());
        maxVertexIndex = signedMaxVertexIndex <= 0 ? 0 : (uint32)std::min<sint64>(signedMaxVertexIndex, std::numeric_limits<uint32>::max());
    }

    const uint64 bufferStart = PerfTelemetry::SampleStart();
    if (m_memoryManager->UseHostMemoryForCache())
    {
        {
            PerfTelemetry::SampledTimer flushTimer(PerfTelemetry::Get().gpuFlushNs);
            LatteBufferCache_processDCFlushQueue();
            LatteBufferCache_processDeallocations();
        }
        draw_updateVertexBuffersDirectAccess(minVertexIndex, maxVertexIndex, baseInstance, instanceCount, fetchVertexManually);
        if (vertexShader)
            draw_updateUniformBuffersDirectAccess(vertexShader, mmSQ_VTX_UNIFORM_BLOCK_START);
        if (geometryShader)
            draw_updateUniformBuffersDirectAccess(geometryShader, mmSQ_GS_UNIFORM_BLOCK_START);
        if (pixelShader)
            draw_updateUniformBuffersDirectAccess(pixelShader, mmSQ_PS_UNIFORM_BLOCK_START);
    }
    else
    {
        LatteBufferCache_Sync(minVertexIndex, maxVertexIndex, baseInstance, instanceCount);
    }

    PrepareUniformBufferSizes(vertexShader);
    if (usesGeometryShader)
        PrepareUniformBufferSizes(geometryShader);
    PrepareUniformBufferSizes(pixelShader);
    PerfTelemetry::SampleEnd(PerfTelemetry::Get().gpuBufferNs, bufferStart);

    const uint64 passStart = PerfTelemetry::SampleStart();
    auto renderCommandEncoder = GetRenderCommandEncoder();
    PerfTelemetry::SampleEnd(PerfTelemetry::Get().gpuPassNs, passStart);

    const uint64 pipelineStart = PerfTelemetry::SampleStart();
    PipelineObject* pipelineObj = m_pipelineCache->GetRenderPipelineState(fetchShader, vertexShader, geometryShader, pixelShader, m_state.m_lastUsedFBO.m_attachmentsInfo, m_state.m_activeFBO.m_attachmentsInfo, m_state.m_activeFBO.m_fbo->m_size, count, LatteGPUState.contextNew);
    PerfTelemetry::SampleEnd(PerfTelemetry::Get().gpuPipelineNs, pipelineStart);
    if (!pipelineObj->m_pipeline)
        return;

    if (pipelineObj->m_pipeline != encoderState.m_renderPipelineState)
       {
        renderCommandEncoder->setRenderPipelineState(pipelineObj->m_pipeline);
          encoderState.m_renderPipelineState = pipelineObj->m_pipeline;
       }

    const bool hasDepthStencilAttachment = m_state.m_activeFBO.m_fbo->depthBuffer.texture != nullptr;
    MTL::DepthStencilState* depthStencilState = m_depthStencilCache->GetDepthStencilState(LatteGPUState.contextNew, hasDepthStencilAttachment);
    if (depthStencilState != encoderState.m_depthStencilState)
    {
        renderCommandEncoder->setDepthStencilState(depthStencilState);
        encoderState.m_depthStencilState = depthStencilState;
    }

    bool stencilEnable = hasDepthStencilAttachment && LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_STENCIL_ENABLE();
    if (stencilEnable)
    {
        bool backStencilEnable = LatteGPUState.contextNew.DB_DEPTH_CONTROL.get_BACK_STENCIL_ENABLE();
        uint32 stencilRefFront = LatteGPUState.contextNew.DB_STENCILREFMASK.get_STENCILREF_F();
        uint32 stencilRefBack;
        if (backStencilEnable)
            stencilRefBack = LatteGPUState.contextNew.DB_STENCILREFMASK_BF.get_STENCILREF_B();
        else
            stencilRefBack = stencilRefFront;

        if (stencilRefFront != encoderState.m_stencilRefFront || stencilRefBack != encoderState.m_stencilRefBack)
        {
            renderCommandEncoder->setStencilReferenceValues(stencilRefFront, stencilRefBack);

            encoderState.m_stencilRefFront = stencilRefFront;
            encoderState.m_stencilRefBack = stencilRefBack;
        }
    }

    uint32* blendColorConstantU32 = LatteGPUState.contextRegister + Latte::REGADDR::CB_BLEND_RED;

    if (blendColorConstantU32[0] != encoderState.m_blendColor[0] || blendColorConstantU32[1] != encoderState.m_blendColor[1] || blendColorConstantU32[2] != encoderState.m_blendColor[2] || blendColorConstantU32[3] != encoderState.m_blendColor[3])
    {
        float* blendColorConstant = (float*)LatteGPUState.contextRegister + Latte::REGADDR::CB_BLEND_RED;
        renderCommandEncoder->setBlendColor(blendColorConstant[0], blendColorConstant[1], blendColorConstant[2], blendColorConstant[3]);

        encoderState.m_blendColor[0] = blendColorConstantU32[0];
        encoderState.m_blendColor[1] = blendColorConstantU32[1];
        encoderState.m_blendColor[2] = blendColorConstantU32[2];
        encoderState.m_blendColor[3] = blendColorConstantU32[3];
    }

    const auto& polygonControlReg = LatteGPUState.contextNew.PA_SU_SC_MODE_CNTL;
    const auto frontFace = polygonControlReg.get_FRONT_FACE();
    uint32 cullFront = polygonControlReg.get_CULL_FRONT();
    uint32 cullBack = polygonControlReg.get_CULL_BACK();
    uint32 polyOffsetFrontEnable = polygonControlReg.get_OFFSET_FRONT_ENABLED();

    if (polyOffsetFrontEnable)
    {
        uint32 frontScaleU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.getRawValue();
        uint32 frontOffsetU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.getRawValue();
        uint32 offsetClampU32 = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.getRawValue();

        if (frontOffsetU32 != encoderState.m_depthBias || frontScaleU32 != encoderState.m_depthSlope || offsetClampU32 != encoderState.m_depthClamp)
        {
               float frontScale = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_SCALE.get_SCALE();
               float frontOffset = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_FRONT_OFFSET.get_OFFSET();
               float offsetClamp = LatteGPUState.contextNew.PA_SU_POLY_OFFSET_CLAMP.get_CLAMP();

               frontScale /= 16.0f;

            renderCommandEncoder->setDepthBias(frontOffset, frontScale, offsetClamp);

            encoderState.m_depthBias = frontOffsetU32;
            encoderState.m_depthSlope = frontScaleU32;
            encoderState.m_depthClamp = offsetClampU32;
        }
    }
    else
    {
        if (0 != encoderState.m_depthBias || 0 != encoderState.m_depthSlope || 0 != encoderState.m_depthClamp)
        {
            renderCommandEncoder->setDepthBias(0.0f, 0.0f, 0.0f);

            encoderState.m_depthBias = 0;
            encoderState.m_depthSlope = 0;
            encoderState.m_depthClamp = 0;
        }
    }

    cemu_assert_debug(LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_NEAR_DISABLE() == LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE());
    bool zClipEnable = LatteGPUState.contextNew.PA_CL_CLIP_CNTL.get_ZCLIP_FAR_DISABLE() == false;

    if (zClipEnable != encoderState.m_depthClipEnable)
    {
        renderCommandEncoder->setDepthClipMode(zClipEnable ? MTL::DepthClipModeClip : MTL::DepthClipModeClamp);
        encoderState.m_depthClipEnable = zClipEnable;
    }

    MTL::VisibilityResultMode visibilityMode = MTL::VisibilityResultModeDisabled;
    NS::UInteger visibilityOffset = 0;
    if (m_occlusionQuery.m_active)
    {
        visibilityMode = MTL::VisibilityResultModeCounting;
        visibilityOffset = (m_occlusionQuery.m_currentBuffer * OCCLUSION_QUERY_POOL_SIZE + m_occlusionQuery.m_currentIndex) * sizeof(uint64);
    }
    if (visibilityMode != encoderState.m_visibilityMode || visibilityOffset != encoderState.m_visibilityOffset)
    {
        renderCommandEncoder->setVisibilityResultMode(visibilityMode, visibilityOffset);
        encoderState.m_visibilityMode = visibilityMode;
        encoderState.m_visibilityOffset = visibilityOffset;
    }

    if (primitiveMode == Latte::LATTE_VGT_PRIMITIVE_TYPE::E_PRIMITIVE_TYPE::RECTS)
    {
        if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CW)
            cullFront = cullBack;
        else
            cullBack = cullFront;
    }

    if (!(cullFront && cullBack))
    {
        MTL::CullMode cullMode;
           if (cullFront)
              cullMode = MTL::CullModeFront;
           else if (cullBack)
              cullMode = MTL::CullModeBack;
           else
              cullMode = MTL::CullModeNone;

        if (cullMode != encoderState.m_cullMode)
           {
               renderCommandEncoder->setCullMode(cullMode);
              encoderState.m_cullMode = cullMode;
           }
    }

    MTL::Winding frontFaceWinding;
    if (frontFace == Latte::LATTE_PA_SU_SC_MODE_CNTL::E_FRONTFACE::CCW)
        frontFaceWinding = MTL::WindingCounterClockwise;
    else
        frontFaceWinding = MTL::WindingClockwise;

    if (frontFaceWinding != encoderState.m_frontFaceWinding)
       {
           renderCommandEncoder->setFrontFacingWinding(frontFaceWinding);
          encoderState.m_frontFaceWinding = frontFaceWinding;
       }

    if (m_state.m_viewport.originX != encoderState.m_viewport.originX ||
        m_state.m_viewport.originY != encoderState.m_viewport.originY ||
        m_state.m_viewport.width != encoderState.m_viewport.width ||
        m_state.m_viewport.height != encoderState.m_viewport.height ||
        m_state.m_viewport.znear != encoderState.m_viewport.znear ||
        m_state.m_viewport.zfar != encoderState.m_viewport.zfar)
    {
        renderCommandEncoder->setViewport(m_state.m_viewport);

        encoderState.m_viewport = m_state.m_viewport;
    }

    MTL::ScissorRect scissorToSend = m_state.m_scissor;
    const uint32 renderAreaWidth = m_state.m_activeFBO.m_fbo->GetRenderAreaWidth();
    const uint32 renderAreaHeight = m_state.m_activeFBO.m_fbo->GetRenderAreaHeight();
    if (renderAreaWidth != 0 && renderAreaHeight != 0)
    {
        const uint64 x = std::min<uint64>(scissorToSend.x, renderAreaWidth);
        const uint64 y = std::min<uint64>(scissorToSend.y, renderAreaHeight);
        const uint64 w = std::min<uint64>(scissorToSend.width, renderAreaWidth - x);
        const uint64 h = std::min<uint64>(scissorToSend.height, renderAreaHeight - y);
        if (x != scissorToSend.x || y != scissorToSend.y || w != scissorToSend.width || h != scissorToSend.height)
        {
            suspectFlags |= MetalDrawBreadcrumb::SUSPECT_SCISSOR;
            MetalGuardNote(MetalGuard::ScissorClamped, {(uint64)scissorToSend.x, (uint64)scissorToSend.y, (uint64)scissorToSend.width, (uint64)scissorToSend.height, renderAreaWidth, renderAreaHeight}, [&] {
                return fmt::format("scissor {},{} {}x{} cut to {},{} {}x{} for a {}x{} render area", (uint64)scissorToSend.x, (uint64)scissorToSend.y, (uint64)scissorToSend.width, (uint64)scissorToSend.height, x, y, w, h, renderAreaWidth, renderAreaHeight);
            });
        }
        scissorToSend = MTL::ScissorRect{(NS::UInteger)x, (NS::UInteger)y, (NS::UInteger)w, (NS::UInteger)h};
    }
    if (scissorToSend.x != encoderState.m_scissor.x ||
        scissorToSend.y != encoderState.m_scissor.y ||
        scissorToSend.width != encoderState.m_scissor.width ||
        scissorToSend.height != encoderState.m_scissor.height)
    {
        encoderState.m_scissor = scissorToSend;
        renderCommandEncoder->setScissorRect(encoderState.m_scissor);
    }

    if (PerfTelemetry::DrawBreadcrumbsEnabled().load(std::memory_order_relaxed))
    {
        auto* crumb = BeginDrawBreadcrumb();
        crumb->count = count;
        crumb->hostIndexCount = hostIndexCount;
        crumb->instanceCount = instanceCount;
        crumb->baseInstance = baseInstance;
        crumb->baseVertex = signedBaseVertex;
        crumb->minVertex = minVertexIndex;
        crumb->maxVertex = maxVertexIndex;
        crumb->primitive = static_cast<uint8>(primitiveMode);
        crumb->indexType = static_cast<uint8>(hostIndexType);
        crumb->flags = (fetchVertexManually ? 1 : 0) | (usesGeometryShader ? 2 : 0) | (usesVertexStreamout ? 4 : 0);
        crumb->indexOffset = indexAllocationMtl ? indexAllocationMtl->bufferOffset : 0;
        crumb->indexAllocSize = indexAllocationMtl ? indexAllocationMtl->size : 0;
        crumb->indexBufferLength = indexAllocationMtl && indexAllocationMtl->mtlBuffer ? indexAllocationMtl->mtlBuffer->length() : 0;
        crumb->renderAreaWidth = renderAreaWidth;
        crumb->renderAreaHeight = renderAreaHeight;
        crumb->scissor[0] = (uint32)m_state.m_scissor.x;
        crumb->scissor[1] = (uint32)m_state.m_scissor.y;
        crumb->scissor[2] = (uint32)m_state.m_scissor.width;
        crumb->scissor[3] = (uint32)m_state.m_scissor.height;
        crumb->scissorSent[0] = (uint32)scissorToSend.x;
        crumb->scissorSent[1] = (uint32)scissorToSend.y;
        crumb->scissorSent[2] = (uint32)scissorToSend.width;
        crumb->scissorSent[3] = (uint32)scissorToSend.height;
        crumb->viewport[0] = (float)m_state.m_viewport.originX;
        crumb->viewport[1] = (float)m_state.m_viewport.originY;
        crumb->viewport[2] = (float)m_state.m_viewport.width;
        crumb->viewport[3] = (float)m_state.m_viewport.height;
        crumb->vertexShaderHash = vertexShader ? vertexShader->baseHash : 0;
        crumb->pixelShaderHash = pixelShader ? pixelShader->baseHash : 0;
        crumb->suspect = suspectFlags;
        for (const auto& group : fetchShader->bufferGroups)
        {
            const uint32 i = group.attributeBufferIndex;
            if (i >= MAX_MTL_VERTEX_BUFFERS || crumb->numVertexBuffers >= MetalDrawBreadcrumb::MAX_VERTEX_BUFFERS)
                continue;
            auto& entry = crumb->vertexBuffers[crumb->numVertexBuffers++];
            entry.slot = static_cast<uint8>(i);
            entry.stride = (LatteGPUState.contextRegister[mmSQ_VTX_ATTRIBUTE_BLOCK_START + i * 7 + 2] >> 11) & 0xFFFF;
            entry.offset = m_state.m_vertexBufferOffsets[i] == INVALID_OFFSET ? 0 : m_state.m_vertexBufferOffsets[i];
            entry.size = m_state.m_vertexBufferSizes[i];
            entry.required = m_state.m_vertexBufferRequired[i];
            entry.bufferLength = m_state.m_vertexBuffers[i] ? m_state.m_vertexBuffers[i]->length() : 0;
        }
    }

    if (!fetchVertexManually)
    {
        bool vertexBuffersUsable = true;
        const uint64 vertexShaderHash = vertexShader ? vertexShader->baseHash : 0;
        for (const auto& group : fetchShader->bufferGroups)
        {
            const uint32 i = group.attributeBufferIndex;
            if (i >= MAX_MTL_VERTEX_BUFFERS)
            {
                vertexBuffersUsable = false;
                MetalGuardNote(MetalGuard::DrawVertexBuffer, {vertexShaderHash, i, 1}, [&] { return fmt::format("vertex buffer slot {} is out of range (vs {:016x})", i, vertexShaderHash); });
                continue;
            }
            uint32 usedEnd = 0;
            if (!MetalVertexGroupIsRead(group, vertexShader, usedEnd))
                continue;
            MTL::Buffer* buffer = m_state.m_vertexBuffers[i];
            const size_t offset = m_state.m_vertexBufferOffsets[i];
            const size_t bound = m_state.m_vertexBufferSizes[i];
            const uint64 stride = (LatteGPUState.contextRegister[mmSQ_VTX_ATTRIBUTE_BLOCK_START + i * 7 + 2] >> 11) & 0xFFFF;
            uint64 reach = 0;
            if (group.hasVtxIndexAccess)
                reach = stride * (uint64)maxVertexIndex + usedEnd;
            if (group.hasInstanceIndexAccess)
                reach = std::max<uint64>(reach, stride * ((uint64)baseInstance + std::max<uint32>(instanceCount, 1) - 1) + usedEnd);
            if (stride == 0)
                reach = usedEnd;
            const uint64 needed = std::min<uint64>(m_state.m_vertexBufferRequired[i], reach);
            if (!buffer || offset == INVALID_OFFSET || offset >= buffer->length() || bound < needed)
            {
                vertexBuffersUsable = false;
                const uint64 failure = !buffer ? 2 : (offset == INVALID_OFFSET ? 3 : (offset >= buffer->length() ? 4 : 5));
                MetalGuardNote(MetalGuard::DrawVertexBuffer, {vertexShaderHash, i, failure}, [&] {
                    return fmt::format("vertex buffer {} {} (vs {:016x}): stride {}, offset {}, buffer length {}, bound {} bytes, conservative size {}, draw reaches {}, max vertex {}, base vertex {}, instances {}+{}",
                        i, failure == 2 ? "is not bound" : (failure == 3 ? "has no valid offset" : (failure == 4 ? "starts past its buffer" : "is smaller than what the draw reads")),
                        vertexShaderHash, stride, offset == INVALID_OFFSET ? (sint64)-1 : (sint64)offset, buffer ? (uint64)buffer->length() : (uint64)0, (uint64)bound, (uint64)m_state.m_vertexBufferRequired[i], reach,
                        maxVertexIndex, signedBaseVertex, baseInstance, instanceCount);
                });
            }
        }
        if (!vertexBuffersUsable)
        {
            if (m_crumb)
                m_crumb->suspect |= MetalDrawBreadcrumb::SUSPECT_VERTEX_BUFFER | MetalDrawBreadcrumb::SUSPECT_SKIPPED;
            streamout_rendererFinishDrawcall();
            LatteGPUState.drawCallCounter++;
            return;
        }
    }

    if (!fetchVertexManually || vertexShader->resourceMapping.argumentBufferBindingPoint < 0)
    {
        for (uint8 i = 0; i < MAX_MTL_VERTEX_BUFFERS; i++)
        {
            MTL::Buffer* buffer = m_state.m_vertexBuffers[i];
            size_t offset = m_state.m_vertexBufferOffsets[i];
            if (buffer && offset != INVALID_OFFSET)
            {
                SetBuffer(renderCommandEncoder, GetMtlShaderType(vertexShader->shaderType, usesGeometryShader), buffer, offset, GET_MTL_VERTEX_BUFFER_INDEX(i));
            }
        }
    }

    const uint32 streamoutVertexCount = usesVertexStreamout && hostIndexType != INDEX_TYPE::NONE ? hostIndexCount : count;
    m_state.m_streamoutState.verticesPerInstance = streamoutVertexCount;
    LatteStreamout_PrepareDrawcall(streamoutVertexCount, instanceCount);

    const uint64 bindStart = PerfTelemetry::SampleStart();
    const bool stageResourcesBound = BindStageResources(renderCommandEncoder, vertexShader, usesGeometryShader) &&
        (!(usesGeometryShader && geometryShader) || BindStageResources(renderCommandEncoder, geometryShader, usesGeometryShader)) &&
        BindStageResources(renderCommandEncoder, pixelShader, usesGeometryShader);
    PerfTelemetry::SampleEnd(PerfTelemetry::Get().gpuBindNs, bindStart);
    if (!stageResourcesBound)
    {
        streamout_rendererFinishDrawcall();
        LatteGPUState.drawCallCounter++;
        return;
    }

    for (const auto& group : fetchShader->bufferGroups)
    {
        const uint32 i = group.attributeBufferIndex;
        if (i < MAX_MTL_VERTEX_BUFFERS)
            m_memoryManager->TrackSharedCache(m_state.m_vertexBuffers[i], m_state.m_vertexBufferOffsets[i], m_state.m_vertexBufferSizes[i]);
    }

    if (usesGeometryShader)
    {
        const bool indexMemoryMissing = hostIndexType != INDEX_TYPE::NONE && !indexAllocationMtl;
        if (hostIndexType != INDEX_TYPE::NONE && !indexMemoryMissing && vertexShader->resourceMapping.argumentBufferBindingPoint < 0)
            SetBuffer(renderCommandEncoder, METAL_SHADER_TYPE_OBJECT, indexAllocationMtl->mtlBuffer, indexAllocationMtl->bufferOffset, vertexShader->resourceMapping.indexBufferBinding);

        uint8 hostIndexTypeU8 = (uint8)hostIndexType;
        if (vertexShader->resourceMapping.argumentBufferBindingPoint < 0 &&
            vertexShader->resourceMapping.indexTypeBinding < MAX_MTL_BUFFERS)
        {
            renderCommandEncoder->setObjectBytes(&hostIndexTypeU8, sizeof(hostIndexTypeU8), vertexShader->resourceMapping.indexTypeBinding);
            encoderState.m_buffers[METAL_SHADER_TYPE_OBJECT][vertexShader->resourceMapping.indexTypeBinding] = {nullptr};
        }
        else if (vertexShader->resourceMapping.argumentBufferBindingPoint < 0)
        {
            cemuLog_logOnce(LogType::Force, "invalid Metal index type binding {}", (uint32)vertexShader->resourceMapping.indexTypeBinding);
        }

        uint32 verticesPerPrimitive = GetVerticesPerPrimitive(primitiveMode);
        uint64 primitivesPerInstance = 0;
        if (verticesPerPrimitive != 0)
        {
            if (PrimitiveRequiresConnection(primitiveMode))
            {
                if (count >= verticesPerPrimitive)
                    primitivesPerInstance = (uint64)count - verticesPerPrimitive + 1;
            }
            else
            {
                primitivesPerInstance = count / verticesPerPrimitive;
            }
        }
        else
        {
            cemuLog_logOnce(LogType::Force, "invalid Metal mesh primitive mode {}", (uint32)primitiveMode);
        }

        uint64 threadgroupCount = primitivesPerInstance * instanceCount;
        if (threadgroupCount > 0 && !indexMemoryMissing)
            renderCommandEncoder->drawMeshThreadgroups(MTL::Size(threadgroupCount, 1, 1), MTL::Size(verticesPerPrimitive, 1, 1), MTL::Size(1, 1, 1));
    }
    else if (usesVertexStreamout)
    {
        renderCommandEncoder->drawPrimitives(mtlPrimitiveType, 0, streamoutVertexCount, instanceCount, 0);
    }
    else
    {
        if (hostIndexType != INDEX_TYPE::NONE)
        {
            if (indexAllocationMtl)
            {
                auto mtlIndexType = GetMtlIndexType(hostIndexType);
                renderCommandEncoder->drawIndexedPrimitives(mtlPrimitiveType, hostIndexCount, mtlIndexType, indexAllocationMtl->mtlBuffer, indexAllocationMtl->bufferOffset, instanceCount, static_cast<NS::Integer>(signedBaseVertex), baseInstance);
            }
        }
        else
        {
            renderCommandEncoder->drawPrimitives(mtlPrimitiveType, baseVertex, count, instanceCount, baseInstance);
        }
    }

    m_state.m_isFirstDrawInRenderPass = false;

    if (m_occlusionQuery.m_active)
        ++m_occlusionQuery.m_currentIndex;

    LatteStreamout_FinishDrawcall(m_memoryManager->UseHostMemoryForCache());

    if (fetchVertexManually)
        m_performanceMonitor.m_manualVertexFetchDraws++;
    if (usesGeometryShader)
        m_performanceMonitor.m_meshDraws++;
    if (primitiveMode == LattePrimitiveMode::TRIANGLE_FAN)
        m_performanceMonitor.m_triangleFans++;

    LatteGPUState.drawCallCounter++;
}

void MetalRenderer::draw_endSequence()
{
    PerfTelemetry::SampledTimer endTimer(PerfTelemetry::Get().gpuEndNs);
    LatteDecompilerShader* pixelShader = LatteSHRC_GetActivePixelShader();
    if (pixelShader)
        LatteRenderTarget_trackUpdates();
    bool hasReadback = LatteTextureReadback_Update();
    m_recordedDrawcalls++;
    if (m_recordedDrawcalls >= m_commitTreshold * 2 )
    {
        CommitCommandBuffer();

        LatteTextureReadback_UpdateFinishedTransfers(false);
    }
}

void MetalRenderer::draw_updateVertexBuffersDirectAccess(uint32 minIndex, uint32 maxIndex, uint32 baseInstance, uint32 instanceCount, bool fetchVertexManually)
{
    LatteFetchShader* parsedFetchShader = LatteSHRC_GetActiveFetchShader();
    if (!parsedFetchShader)
        return;

    for (auto& bufferGroup : parsedFetchShader->bufferGroups)
    {
        uint32 bufferIndex = bufferGroup.attributeBufferIndex;
        uint32 bufferBaseRegisterIndex = mmSQ_VTX_ATTRIBUTE_BLOCK_START + bufferIndex * 7;
        MPTR bufferAddress = LatteGPUState.contextRegister[bufferBaseRegisterIndex + 0];
        uint32 bufferStride = (LatteGPUState.contextRegister[bufferBaseRegisterIndex + 2] >> 11) & 0xFFFF;

        if (bufferAddress == MPTR_NULL) [[unlikely]]
            bufferAddress = m_memoryManager->GetImportedMemBaseAddress();

        uint64 bufferSize64 = 0;
        if (bufferGroup.hasVtxIndexAccess)
            bufferSize64 = (uint64)bufferStride * ((uint64)maxIndex + 1) + bufferGroup.maxOffset;
        if (bufferGroup.hasInstanceIndexAccess)
        {
            const uint64 instanceBufferSize = (uint64)bufferStride * ((uint64)baseInstance + instanceCount + 1) + bufferGroup.maxOffset;
            bufferSize64 = std::max(bufferSize64, instanceBufferSize);
        }
        if (bufferSize64 == 0 || bufferStride == 0)
            bufferSize64 += 128;
        if (bufferSize64 > 0x7FFFFFFFull)
        {
            {
                uint32 usedEnd = 0;
                LatteDecompilerShader* activeVertexShader = LatteSHRC_GetActiveVertexShader();
                if (fetchVertexManually || !activeVertexShader || MetalVertexGroupIsRead(bufferGroup, activeVertexShader, usedEnd))
                    MetalGuardNote(MetalGuard::DrawVertexHuge, {activeVertexShader ? activeVertexShader->baseHash : 0, bufferIndex, bufferStride}, [&] {
                        return fmt::format("vertex buffer {} would need {} bytes (stride {}, max index {}, instances {}+{}); its draws are skipped (vs {:016x})", bufferIndex, bufferSize64, bufferStride, maxIndex, baseInstance, instanceCount, activeVertexShader ? activeVertexShader->baseHash : (uint64)0);
                    });
            }
            m_state.m_vertexBuffers[bufferIndex] = nullptr;
            m_state.m_vertexBufferOffsets[bufferIndex] = INVALID_OFFSET;
            m_state.m_vertexBufferSizes[bufferIndex] = 0;
            m_state.m_vertexBufferRequired[bufferIndex] = std::numeric_limits<uint32>::max();
            continue;
        }
        const uint32 bufferSize = static_cast<uint32>(bufferSize64);
        m_state.m_vertexBufferRequired[bufferIndex] = bufferSize;

        if (m_memoryManager->IsRangeImported(bufferAddress, bufferSize))
        {
            uint32 firstByte = 0;
            if (!fetchVertexManually && bufferGroup.hasVtxIndexAccess && !bufferGroup.hasInstanceIndexAccess)
                firstByte = static_cast<uint32>(std::min<uint64>((uint64)bufferStride * minIndex, bufferSize));
            if (LatteBufferCache_hostIsRangeVolatile(bufferAddress + firstByte, bufferSize - firstByte))
            {
                auto* allocation = m_memoryManager->GetCachedSnapshot(MetalMemoryManager::VertexSnapshotBase + bufferIndex,
                    memory_getPointerFromVirtualOffset(bufferAddress), bufferSize, firstByte);

                if (!allocation)
                {
                    m_state.m_vertexBuffers[bufferIndex] = nullptr;
                    m_state.m_vertexBufferOffsets[bufferIndex] = INVALID_OFFSET;
                    m_state.m_vertexBufferSizes[bufferIndex] = 0;
                    continue;
                }

                m_state.m_vertexBuffers[bufferIndex] = allocation->mtlBuffer;
                m_state.m_vertexBufferOffsets[bufferIndex] = allocation->bufferOffset;
                m_state.m_vertexBufferSizes[bufferIndex] = bufferSize;
            }
            else
            {
                size_t bufferOffset = m_memoryManager->GetImportedMemoryOffset(bufferAddress);
                m_state.m_vertexBuffers[bufferIndex] = m_memoryManager->GetImportedMemoryBuffer();
                m_state.m_vertexBufferOffsets[bufferIndex] = bufferOffset;
                m_state.m_vertexBufferSizes[bufferIndex] = m_state.m_vertexBuffers[bufferIndex] && bufferOffset < m_state.m_vertexBuffers[bufferIndex]->length() ? std::min<size_t>(bufferSize, m_state.m_vertexBuffers[bufferIndex]->length() - bufferOffset) : 0;
                m_memoryManager->NotifyImportedMemoryRangeModified(bufferOffset, bufferSize);
            }
        }
        else
        {
            uint32 bindOffset = LatteBufferCache_retrieveDataInCache(bufferAddress, bufferSize);
            m_state.m_vertexBuffers[bufferIndex] = m_memoryManager->GetBufferCache();
            m_state.m_vertexBufferOffsets[bufferIndex] = bindOffset;
            m_state.m_vertexBufferSizes[bufferIndex] = m_state.m_vertexBuffers[bufferIndex] && bindOffset < m_state.m_vertexBuffers[bufferIndex]->length() ? std::min<size_t>(bufferSize, m_state.m_vertexBuffers[bufferIndex]->length() - bindOffset) : 0;
        }
    }
}

void MetalRenderer::draw_updateUniformBuffersDirectAccess(LatteDecompilerShader* shader, const uint32 uniformBufferRegOffset)
{
    if (shader->uniformMode == LATTE_DECOMPILER_UNIFORM_MODE_FULL_CBANK)
    {
        for (const auto& buf : shader->list_quickBufferList)
        {
            sint32 i = buf.index;
            MPTR physicalAddr = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 0];
            uint32 uniformSize = LatteGPUState.contextRegister[uniformBufferRegOffset + i * 7 + 1] + 1;

            if (physicalAddr == MPTR_NULL) [[unlikely]]
            {
                cemu_assert_unimplemented();
                MetalGeneralShaderType shaderType = GetMtlGeneralShaderType(shader->shaderType);
                m_state.m_uniformBuffers[shaderType][i] = nullptr;
                m_state.m_uniformBufferOffsets[shaderType][i] = INVALID_OFFSET;
                m_state.m_uniformBufferSizes[shaderType][i] = 0;
                continue;
            }
            uniformSize = std::min<uint32>(uniformSize, buf.size);

            cemu_assert_debug(physicalAddr < 0x50000000);

            uint32 bufferIndex = i;
            cemu_assert_debug(bufferIndex < 16);

            MetalGeneralShaderType shaderType = GetMtlGeneralShaderType(shader->shaderType);
            if (m_memoryManager->IsRangeImported(physicalAddr, uniformSize))
            {
                auto* allocation = m_memoryManager->GetCachedSnapshot(MetalMemoryManager::UniformSnapshotBase + shaderType * MAX_MTL_BUFFERS + bufferIndex,
                    memory_getPointerFromVirtualOffset(physicalAddr), uniformSize);

                if (!allocation)
                {
                    m_state.m_uniformBuffers[shaderType][bufferIndex] = nullptr;
                    m_state.m_uniformBufferOffsets[shaderType][bufferIndex] = INVALID_OFFSET;
                    m_state.m_uniformBufferSizes[shaderType][bufferIndex] = 0;
                    continue;
                }

                m_state.m_uniformBuffers[shaderType][bufferIndex] = allocation->mtlBuffer;
                m_state.m_uniformBufferOffsets[shaderType][bufferIndex] = allocation->bufferOffset;
                m_state.m_uniformBufferSizes[shaderType][bufferIndex] = uniformSize;
            }
            else
            {
                uint32 bindOffset = LatteBufferCache_retrieveDataInCache(physicalAddr, uniformSize);
                m_state.m_uniformBuffers[shaderType][bufferIndex] = m_memoryManager->GetBufferCache();
                m_state.m_uniformBufferOffsets[shaderType][bufferIndex] = bindOffset;
                m_state.m_uniformBufferSizes[shaderType][bufferIndex] = uniformSize;
            }
        }
    }
}

void MetalRenderer::draw_handleSpecialState5()
{
    LatteMRT::UpdateCurrentFBO();
    LatteRenderTarget_updateViewport();

    LatteTextureView* colorBuffer = LatteMRT::GetColorAttachment(0);
    LatteTextureView* depthBuffer = LatteMRT::GetDepthAttachment();
    sint32 vpWidth, vpHeight;
    LatteMRT::GetVirtualViewportDimensions(vpWidth, vpHeight);

    surfaceCopy_copySurfaceWithFormatConversion(
        depthBuffer->baseTexture, depthBuffer->firstMip, depthBuffer->firstSlice,
        colorBuffer->baseTexture, colorBuffer->firstMip, colorBuffer->firstSlice,
        vpWidth, vpHeight);
}

Renderer::IndexAllocation MetalRenderer::indexData_reserveIndexMemory(uint32 size)
{
    auto allocation = m_memoryManager->GetIndexAllocator().AllocateBufferMemory(size, 128);
    if (!allocation)
        return {nullptr, nullptr};

    return {allocation->memPtr, allocation};
}

void MetalRenderer::indexData_releaseIndexMemory(IndexAllocation& allocation)
{
    m_memoryManager->GetIndexAllocator().FreeReservation(static_cast<MetalSynchronizedHeapAllocator::AllocatorReservation*>(allocation.rendererInternal));
}

void MetalRenderer::indexData_uploadIndexMemory(IndexAllocation& allocation)
{
    m_memoryManager->GetIndexAllocator().FlushReservation(static_cast<MetalSynchronizedHeapAllocator::AllocatorReservation*>(allocation.rendererInternal));
}

LatteQueryObject* MetalRenderer::occlusionQuery_create() {
    auto* query = new LatteQueryObjectMtl(this);
    m_occlusionQuery.m_queries.push_back(query);
    return query;
}

void MetalRenderer::occlusionQuery_destroy(LatteQueryObject* queryObj) {
    auto queryObjMtl = static_cast<LatteQueryObjectMtl*>(queryObj);
    std::erase(m_occlusionQuery.m_queries, queryObjMtl);
    delete queryObjMtl;
}

void MetalRenderer::PrepareOcclusionQueryDraw()
{
    if (!m_occlusionQuery.m_active || m_occlusionQuery.m_currentIndex < OCCLUSION_QUERY_POOL_SIZE)
        return;

    const uint32 previousBuffer = m_occlusionQuery.m_currentBuffer;
    m_occlusionQuery.m_bufferCompletion[previousBuffer] = GetCommandBuffer()->retain();
    CommitCommandBuffer();
    for (auto* query : m_occlusionQuery.m_queries)
        query->SealCurrentRange(previousBuffer);

    const uint32 nextBuffer = (previousBuffer + 1) % OCCLUSION_QUERY_BUFFER_COUNT;
    auto*& completion = m_occlusionQuery.m_bufferCompletion[nextBuffer];
    if (completion)
    {

        WaitForCommandBuffer(completion, "occlusion query buffer reuse: waiting for an older command buffer");

        for (auto* query : m_occlusionQuery.m_queries)
            query->AccumulateBuffer(nextBuffer);

        completion->release();
        completion = nullptr;
    }
    m_occlusionQuery.m_currentBuffer = nextBuffer;
    std::fill_n(GetOcclusionQueryResultsPtr(), OCCLUSION_QUERY_POOL_SIZE, uint64{0});
    m_occlusionQuery.m_currentIndex = 0;
}

void MetalRenderer::occlusionQuery_flush() {
    CommitCommandBuffer();
    if (m_occlusionQuery.m_lastCommandBuffer)
        WaitForCommandBuffer(m_occlusionQuery.m_lastCommandBuffer, "occlusion query flush: waiting for the query command buffer");
}

void MetalRenderer::occlusionQuery_updateState() {
    ProcessFinishedCommandBuffers();
}

namespace
{
    constexpr size_t kBytesBoundOffset = std::numeric_limits<size_t>::max() - 1;

    struct BoundBytes
    {
        uint32 length = 0;
        uint8 data[MTL_MAX_SET_BYTES];
    };
    BoundBytes s_boundBytes[METAL_SHADER_TYPE_TOTAL];
    uint32 s_boundBytesIndex[METAL_SHADER_TYPE_TOTAL];
}

void MetalRenderer::SetBytes(MTL::RenderCommandEncoder* renderCommandEncoder, MetalShaderType shaderType, const void* bytes, uint32 length, uint32 index)
{
    if (index >= MAX_MTL_BUFFERS || length > MTL_MAX_SET_BYTES)
    {
        cemuLog_logOnce(LogType::Force, "invalid Metal bytes binding {} ({} bytes)", index, length);
        return;
    }

    auto& boundBuffer = m_state.m_encoderState.m_buffers[shaderType][index];
    auto& shadow = s_boundBytes[shaderType];
    if (boundBuffer.m_buffer == nullptr && boundBuffer.m_offset == kBytesBoundOffset && s_boundBytesIndex[shaderType] == index
        && shadow.length == length && std::memcmp(shadow.data, bytes, length) == 0)
        return;

    switch (shaderType)
    {
    case METAL_SHADER_TYPE_VERTEX:
        renderCommandEncoder->setVertexBytes(bytes, length, index);
        break;
    case METAL_SHADER_TYPE_OBJECT:
        renderCommandEncoder->setObjectBytes(bytes, length, index);
        break;
    case METAL_SHADER_TYPE_MESH:
        renderCommandEncoder->setMeshBytes(bytes, length, index);
        break;
    case METAL_SHADER_TYPE_FRAGMENT:
        renderCommandEncoder->setFragmentBytes(bytes, length, index);
        break;
    default:
        return;
    }
    boundBuffer = {nullptr, kBytesBoundOffset};
    s_boundBytesIndex[shaderType] = index;
    shadow.length = length;
    std::memcpy(shadow.data, bytes, length);
}

void MetalRenderer::SetBuffer(MTL::RenderCommandEncoder* renderCommandEncoder, MetalShaderType shaderType, MTL::Buffer* buffer, size_t offset, uint32 index)
{
    if (index >= MAX_MTL_BUFFERS)
    {
        cemuLog_logOnce(LogType::Force, "invalid Metal buffer binding {}", index);
        return;
    }

    auto& boundBuffer = m_state.m_encoderState.m_buffers[shaderType][index];
    if (buffer == boundBuffer.m_buffer && offset == boundBuffer.m_offset)
        return;

    if (buffer == boundBuffer.m_buffer)
    {
        boundBuffer.m_offset = offset;

        switch (shaderType)
        {
        case METAL_SHADER_TYPE_VERTEX:
            renderCommandEncoder->setVertexBufferOffset(offset, index);
            break;
        case METAL_SHADER_TYPE_OBJECT:
            renderCommandEncoder->setObjectBufferOffset(offset, index);
            break;
        case METAL_SHADER_TYPE_MESH:
            renderCommandEncoder->setMeshBufferOffset(offset, index);
            break;
        case METAL_SHADER_TYPE_FRAGMENT:
            renderCommandEncoder->setFragmentBufferOffset(offset, index);
            break;
        }

        return;
    }

    boundBuffer = {buffer, offset};

    switch (shaderType)
    {
    case METAL_SHADER_TYPE_VERTEX:
        renderCommandEncoder->setVertexBuffer(buffer, offset, index);
        break;
    case METAL_SHADER_TYPE_OBJECT:
        renderCommandEncoder->setObjectBuffer(buffer, offset, index);
        break;
    case METAL_SHADER_TYPE_MESH:
        renderCommandEncoder->setMeshBuffer(buffer, offset, index);
        break;
    case METAL_SHADER_TYPE_FRAGMENT:
        renderCommandEncoder->setFragmentBuffer(buffer, offset, index);
        break;
    }
}

void MetalRenderer::SetTexture(MTL::RenderCommandEncoder* renderCommandEncoder, MetalShaderType shaderType, MTL::Texture* texture, uint32 index)
{
    if (index >= MAX_MTL_TEXTURES)
    {
        cemuLog_logOnce(LogType::Force, "invalid Metal texture binding {}", index);
        return;
    }

    auto& boundTexture = m_state.m_encoderState.m_textures[shaderType][index];
    if (texture == boundTexture)
        return;

    boundTexture = texture;

    switch (shaderType)
    {
    case METAL_SHADER_TYPE_VERTEX:
        renderCommandEncoder->setVertexTexture(texture, index);
        break;
    case METAL_SHADER_TYPE_OBJECT:
        renderCommandEncoder->setObjectTexture(texture, index);
        break;
    case METAL_SHADER_TYPE_MESH:
        renderCommandEncoder->setMeshTexture(texture, index);
        break;
    case METAL_SHADER_TYPE_FRAGMENT:
        renderCommandEncoder->setFragmentTexture(texture, index);
        break;
    }
}

void MetalRenderer::SetSamplerState(MTL::RenderCommandEncoder* renderCommandEncoder, MetalShaderType shaderType, MTL::SamplerState* samplerState, uint32 index)
{
    if (index >= MAX_MTL_SAMPLERS)
    {
        cemuLog_logOnce(LogType::Force, "invalid Metal sampler binding {}", index);
        return;
    }

    auto& boundSamplerState = m_state.m_encoderState.m_samplers[shaderType][index];
    if (samplerState == boundSamplerState)
        return;

    boundSamplerState = samplerState;

    switch (shaderType)
    {
    case METAL_SHADER_TYPE_VERTEX:
        renderCommandEncoder->setVertexSamplerState(samplerState, index);
        break;
    case METAL_SHADER_TYPE_OBJECT:
        renderCommandEncoder->setObjectSamplerState(samplerState, index);
        break;
    case METAL_SHADER_TYPE_MESH:
        renderCommandEncoder->setMeshSamplerState(samplerState, index);
        break;
    case METAL_SHADER_TYPE_FRAGMENT:
        renderCommandEncoder->setFragmentSamplerState(samplerState, index);
        break;
    }
}

MTL::CommandBuffer* MetalRenderer::GetCommandBuffer()
{
    bool needsNewCommandBuffer = (!m_currentCommandBuffer.m_commandBuffer || m_currentCommandBuffer.m_commited);
    if (needsNewCommandBuffer)
    {
        const bool replacingQueue = m_gpuRecoveryPending && !m_gpuRecovering;
        if (replacingQueue)
            RecoverFromGpuFault();

        if (m_memoryManager)
        {
            if (replacingQueue)
                m_memoryManager->ReleaseArgumentCache();
            else
                m_memoryManager->AgeArgumentCache();
        }

        auto pool = NS::AutoreleasePool::alloc()->init();
        auto* commandBufferDescriptor = MTL::CommandBufferDescriptor::alloc()->init();
        if (PerfTelemetry::DrawBreadcrumbsEnabled().load(std::memory_order_relaxed))
            commandBufferDescriptor->setErrorOptions(MTL::CommandBufferErrorOptionEncoderExecutionStatus);
        MTL::CommandBuffer* mtlCommandBuffer = m_commandQueue->commandBuffer(commandBufferDescriptor)->retain();
        commandBufferDescriptor->release();
        pool->release();
        m_currentCommandBuffer = {mtlCommandBuffer};

        if (m_eventValue != -1)
            mtlCommandBuffer->encodeWait(m_event, m_eventValue);

        m_recordedDrawcalls = 0;
        m_commitTreshold = m_defaultCommitTreshlod;

        m_performanceMonitor.m_commandBuffers++;

        return mtlCommandBuffer;
    }
    else
    {
        return m_currentCommandBuffer.m_commandBuffer;
    }
}

MTL::RenderCommandEncoder* MetalRenderer::GetTemporaryRenderCommandEncoder(MTL::RenderPassDescriptor* renderPassDescriptor)
{
    if (m_commandEncoder && m_encoderType == MetalEncoderType::Render)
        PerfTelemetry::Get().renderPassBreaks[PerfTelemetry::kBreakTemporaryPass].fetch_add(1, std::memory_order_relaxed);
    EndEncoding();

    auto commandBuffer = GetCommandBuffer();

    auto pool = NS::AutoreleasePool::alloc()->init();
    auto renderCommandEncoder = commandBuffer->renderCommandEncoder(renderPassDescriptor)->retain();
    pool->release();
    LabelEncoder(renderCommandEncoder, "temporary render");
    m_commandEncoder = renderCommandEncoder;
    m_encoderType = MetalEncoderType::Render;

    ResetEncoderState();

    m_performanceMonitor.m_renderPasses++;

    return renderCommandEncoder;
}

MTL::RenderCommandEncoder* MetalRenderer::GetRenderCommandEncoder(bool forceRecreate)
{
    bool fboChanged = m_state.m_fboChanged;
    m_state.m_fboChanged = false;

    if (m_commandEncoder)
    {
        if (!forceRecreate)
        {
            if (m_encoderType == MetalEncoderType::Render)
            {
                bool needsNewRenderPass = false;
                if (fboChanged)
                {
                    needsNewRenderPass = (m_state.m_lastUsedFBO.m_fbo == nullptr);
                    if (!needsNewRenderPass)
                    {
                        for (uint8 i = 0; i < 8; i++)
                        {
                            if (m_state.m_activeFBO.m_fbo->colorBuffer[i].texture && m_state.m_activeFBO.m_fbo->colorBuffer[i].texture != m_state.m_lastUsedFBO.m_fbo->colorBuffer[i].texture)
                            {
                                needsNewRenderPass = true;
                                break;
                            }
                        }
                    }

                    if (!needsNewRenderPass)
                    {
                        if (m_state.m_activeFBO.m_fbo->depthBuffer.texture && (m_state.m_activeFBO.m_fbo->depthBuffer.texture != m_state.m_lastUsedFBO.m_fbo->depthBuffer.texture || ( m_state.m_activeFBO.m_fbo->depthBuffer.hasStencil && !m_state.m_lastUsedFBO.m_fbo->depthBuffer.hasStencil)))
                        {
                            needsNewRenderPass = true;
                        }
                    }
                }

                if (!needsNewRenderPass)
                {
                    return (MTL::RenderCommandEncoder*)m_commandEncoder;
                }
            }
        }

        if (m_encoderType == MetalEncoderType::Render)
            PerfTelemetry::Get().renderPassBreaks[forceRecreate ? PerfTelemetry::kBreakForced : PerfTelemetry::kBreakFboChange].fetch_add(1, std::memory_order_relaxed);
        EndEncoding();
    }

    auto commandBuffer = GetCommandBuffer();

    auto pool = NS::AutoreleasePool::alloc()->init();
    auto renderCommandEncoder = commandBuffer->renderCommandEncoder(m_state.m_activeFBO.m_fbo->GetRenderPassDescriptor())->retain();
    pool->release();
    LabelEncoder(renderCommandEncoder, "render pass");
    PerfTelemetry::Get().renderPassesStarted.fetch_add(1, std::memory_order_relaxed);
    {
        static const void* s_prevFbo = nullptr;
        static const void* s_prevPrevFbo = nullptr;
        const void* fbo = m_state.m_activeFBO.m_fbo;
        if (fbo && fbo == s_prevPrevFbo && fbo != s_prevFbo)
            PerfTelemetry::Get().passReturnsToPrevious.fetch_add(1, std::memory_order_relaxed);
        s_prevPrevFbo = s_prevFbo;
        s_prevFbo = fbo;
    }
    m_commandEncoder = renderCommandEncoder;
    m_encoderType = MetalEncoderType::Render;

    m_state.m_lastUsedFBO = m_state.m_activeFBO;
    m_state.m_isFirstDrawInRenderPass = true;

    ResetEncoderState();

    m_performanceMonitor.m_renderPasses++;

    return renderCommandEncoder;
}

MTL::ComputeCommandEncoder* MetalRenderer::GetComputeCommandEncoder()
{
    if (m_commandEncoder)
    {
        if (m_encoderType == MetalEncoderType::Compute)
        {
            return (MTL::ComputeCommandEncoder*)m_commandEncoder;
        }

        if (m_encoderType == MetalEncoderType::Render)
            PerfTelemetry::Get().renderPassBreaks[PerfTelemetry::kBreakCompute].fetch_add(1, std::memory_order_relaxed);
        EndEncoding();
    }

    auto commandBuffer = GetCommandBuffer();

    auto pool = NS::AutoreleasePool::alloc()->init();
    auto computeCommandEncoder = commandBuffer->computeCommandEncoder()->retain();
    pool->release();
    LabelEncoder(computeCommandEncoder, "compute");
    m_commandEncoder = computeCommandEncoder;
    m_encoderType = MetalEncoderType::Compute;

    ResetEncoderState();

    return computeCommandEncoder;
}

MTL::BlitCommandEncoder* MetalRenderer::GetBlitCommandEncoder(int passBreakReason)
{
    if (m_commandEncoder)
    {
        if (m_encoderType == MetalEncoderType::Blit)
        {
            return (MTL::BlitCommandEncoder*)m_commandEncoder;
        }

        if (m_encoderType == MetalEncoderType::Render)
            PerfTelemetry::Get().renderPassBreaks[passBreakReason & 15].fetch_add(1, std::memory_order_relaxed);
        EndEncoding();
    }

    auto commandBuffer = GetCommandBuffer();

    auto pool = NS::AutoreleasePool::alloc()->init();
    auto blitCommandEncoder = commandBuffer->blitCommandEncoder()->retain();
    pool->release();
    LabelEncoder(blitCommandEncoder, "blit");
    m_commandEncoder = blitCommandEncoder;
    m_encoderType = MetalEncoderType::Blit;

    ResetEncoderState();

    return blitCommandEncoder;
}

void MetalRenderer::EndEncoding()
{
    if (m_commandEncoder)
    {
        m_commandEncoder->endEncoding();
        m_commandEncoder->release();
        m_commandEncoder = nullptr;
        m_encoderType = MetalEncoderType::None;

        if (m_recordedDrawcalls >= m_commitTreshold)
            CommitCommandBuffer();
    }
}

void MetalRenderer::CommitCommandBuffer()
{
    if (!m_currentCommandBuffer.m_commandBuffer)
        return;

    if (m_commandEncoder && m_encoderType == MetalEncoderType::Render)
        PerfTelemetry::Get().renderPassBreaks[PerfTelemetry::kBreakCommit].fetch_add(1, std::memory_order_relaxed);
    EndEncoding();

    ProcessFinishedCommandBuffers();

    if (!m_currentCommandBuffer.m_commited)
    {
        constexpr size_t MAX_COMMAND_BUFFERS_IN_FLIGHT = 10;
        for (int guard = 0; guard < 4 && m_executingCommandBuffers.size() >= MAX_COMMAND_BUFFERS_IN_FLIGHT; ++guard)
        {
            static uint32 s_throttleLogs = 0;
            if (s_throttleLogs++ < 4)
                cemuLog_log(LogType::Force, "Metal: {} command buffers are still on the GPU, waiting for the oldest before submitting more", m_executingCommandBuffers.size());
            const bool finished = WaitForCommandBuffer(m_executingCommandBuffers.front(), "waiting for the GPU to catch up (too many command buffers in flight)");
            ProcessFinishedCommandBuffers();
            if (!finished)
                break;
        }

        m_eventValue = (m_eventValue + 1) % EVENT_VALUE_WRAP;
        auto mtlCommandBuffer = m_currentCommandBuffer.m_commandBuffer;
        mtlCommandBuffer->encodeSignalEvent(m_event, m_eventValue);

        mtlCommandBuffer->commit();
        m_currentCommandBuffer.m_commited = true;

        m_executingCommandBuffers.push_back(mtlCommandBuffer);
        m_executingEventValues.push_back(m_eventValue);
        m_executingQueueGenerations.push_back(m_queueGeneration);
        LatteWait::Get().executingCommandBuffers.store((uint32)m_executingCommandBuffers.size(), std::memory_order_relaxed);
        LatteWait::Get().cbSubmitted.fetch_add(1, std::memory_order_relaxed);

    }
}

void MetalRenderer::LabelEncoder(MTL::CommandEncoder* encoder, const char* kind)
{
    if (!PerfTelemetry::DrawBreadcrumbsEnabled().load(std::memory_order_relaxed))
        return;
    char text[96];
    snprintf(text, sizeof(text), "%s frame %u draw %u", kind, (uint32)LatteGPUState.frameCounter, (uint32)LatteGPUState.drawCallCounter);
    auto pool = NS::AutoreleasePool::alloc()->init();
    encoder->setLabel(NS::String::string(text, NS::UTF8StringEncoding));
    pool->release();
}

MetalDrawBreadcrumb* MetalRenderer::BeginDrawBreadcrumb()
{
    MetalDrawBreadcrumb& crumb = m_breadcrumbs[m_breadcrumbNext];
    m_breadcrumbNext = (m_breadcrumbNext + 1) % m_breadcrumbCapacity;
    ++m_breadcrumbsWritten;
    crumb.numVertexBuffers = 0;
    crumb.numUniformBuffers = 0;
    crumb.numTextures = 0;
    crumb.suspect = 0;
    crumb.frame = (uint32)LatteGPUState.frameCounter;
    crumb.draw = (uint32)LatteGPUState.drawCallCounter;
    m_crumb = &crumb;
    return m_crumb;
}

void MetalRenderer::RecordBreadcrumbTexture(LatteConst::ShaderType shaderType, uint32 unit, uint32 dim, MTL::Texture* texture, bool fallback)
{
    if (!m_crumb)
        return;
    if (fallback)
        m_crumb->suspect |= MetalDrawBreadcrumb::SUSPECT_TEXTURE;
    if (!texture || m_crumb->numTextures >= MetalDrawBreadcrumb::MAX_TEXTURES)
        return;
    BreadcrumbTextureInfo& info = m_breadcrumbTextureInfo[(uint32)(((uintptr_t)texture >> 4) * 2654435761u) % 4096];
    if (info.texture != texture)
    {
        info = {};
        info.texture = texture;
        info.width = (uint32)texture->width();
        info.height = (uint32)texture->height();
        info.depth = (uint32)std::max<NS::UInteger>(texture->depth(), texture->arrayLength());
        info.mips = (uint16)texture->mipmapLevelCount();
        info.pixelFormat = (uint16)texture->pixelFormat();
        if (auto* parent = texture->parentTexture())
        {
            info.hasParent = true;
            info.parentLevel = (uint16)texture->parentRelativeLevel();
            info.parentSlice = (uint16)texture->parentRelativeSlice();
            info.parentMips = (uint16)parent->mipmapLevelCount();
            info.parentLayers = (uint16)parent->arrayLength();
            info.suspect = (NS::UInteger)info.parentLevel + texture->mipmapLevelCount() > parent->mipmapLevelCount() ||
                (NS::UInteger)info.parentSlice + texture->arrayLength() > std::max<NS::UInteger>(1, parent->arrayLength()) * (parent->textureType() == MTL::TextureTypeCubeArray ? 6 : 1);
        }
    }
    auto& entry = m_crumb->textures[m_crumb->numTextures++];
    entry.stage = static_cast<uint8>(shaderType);
    entry.unit = static_cast<uint8>(unit);
    entry.type = static_cast<uint8>(dim);
    entry.fallback = fallback ? 1 : 0;
    entry.width = info.width;
    entry.height = info.height;
    entry.depth = info.depth;
    entry.mips = info.mips;
    entry.pixelFormat = info.pixelFormat;
    if (info.hasParent)
    {
        entry.parentLevel = info.parentLevel;
        entry.parentSlice = info.parentSlice;
        entry.parentMips = info.parentMips;
        entry.parentLayers = info.parentLayers;
        if (info.suspect)
            m_crumb->suspect |= MetalDrawBreadcrumb::SUSPECT_TEXTURE;
    }
}

void MetalRenderer::UseResource(MTL::RenderCommandEncoder* encoder, MTL::Resource* resource, MTL::ResourceUsage usage, MTL::RenderStages stages)
{
    if (!resource)
        return;
    uint32 slot = (uint32)(((uintptr_t)resource >> 4) * 2654435761u) % kUsedResourceSlots;
    for (uint32 probe = 0; probe < 8; probe++, slot = (slot + 1) % kUsedResourceSlots)
    {
        UsedResource& entry = m_usedResources[slot];
        if (entry.generation != m_usedResourceGeneration)
        {
            entry = {resource, m_usedResourceGeneration, (uint32)usage, (uint32)stages};
            encoder->useResource(resource, usage, stages);
            return;
        }
        if (entry.resource == resource)
        {
            if ((usage & ~entry.usage) == 0 && (stages & ~entry.stages) == 0)
                return;
            entry.usage |= (uint32)usage;
            entry.stages |= (uint32)stages;
            encoder->useResource(resource, (MTL::ResourceUsage)entry.usage, (MTL::RenderStages)entry.stages);
            return;
        }
    }
    encoder->useResource(resource, usage, stages);
}

void MetalRenderer::DumpDrawBreadcrumbs(uint32 firstDraw, uint32 lastDraw, bool haveRange)
{
    MetalGuardReport("breadcrumb dump", 24, false);
    const uint32 stored = std::min(m_breadcrumbsWritten, m_breadcrumbCapacity);
    if (stored == 0)
    {
        cemuLog_log(LogType::Force, "Metal: no draw breadcrumbs were recorded");
        return;
    }
    const uint32 start = (m_breadcrumbNext + m_breadcrumbCapacity - stored) % m_breadcrumbCapacity;
    auto at = [&](uint32 n) -> const MetalDrawBreadcrumb& { return m_breadcrumbs[(start + n) % m_breadcrumbCapacity]; };
    if (haveRange)
        cemuLog_log(LogType::Force, "Metal: draw breadcrumbs: {} draws kept (draw {} to {}), unfinished encoders start at draws {} to {}", stored, at(0).draw, at(stored - 1).draw, firstDraw, lastDraw);
    else
        cemuLog_log(LogType::Force, "Metal: draw breadcrumbs: {} draws kept (draw {} to {}), the failed command buffer named no encoders", stored, at(0).draw, at(stored - 1).draw);
    if (haveRange && firstDraw < at(0).draw)
        cemuLog_log(LogType::Force, "Metal: the failed encoder starts at draw {}, {} draws older than the oldest breadcrumb kept (draw {}); its early draws are not in the ring", firstDraw, at(0).draw - firstDraw, at(0).draw);

    auto print = [&](const MetalDrawBreadcrumb& d) {
        cemuLog_log(LogType::Force, "Metal: crumb draw {} frame {} prim {} count {} indexed {} (type {}, offset {}, alloc {}, buffer {}) inst {}+{} baseVertex {} vertices {}..{} flags {:#x} suspect {:#x} vs {:016x} ps {:016x}",
            d.draw, d.frame, (uint32)d.primitive, d.count, d.hostIndexCount, (uint32)d.indexType, d.indexOffset, d.indexAllocSize, d.indexBufferLength,
            d.baseInstance, d.instanceCount, d.baseVertex, d.minVertex, d.maxVertex, (uint32)d.flags, d.suspect, d.vertexShaderHash, d.pixelShaderHash);
        cemuLog_log(LogType::Force, "Metal:   target {}x{} scissor {},{} {}x{} sent {},{} {}x{} viewport {:.0f},{:.0f} {:.0f}x{:.0f}",
            d.renderAreaWidth, d.renderAreaHeight, d.scissor[0], d.scissor[1], d.scissor[2], d.scissor[3], d.scissorSent[0], d.scissorSent[1], d.scissorSent[2], d.scissorSent[3],
            d.viewport[0], d.viewport[1], d.viewport[2], d.viewport[3]);
        std::string buffers;
        for (uint32 i = 0; i < d.numVertexBuffers; ++i)
        {
            const auto& b = d.vertexBuffers[i];
            buffers += fmt::format(" vb{}[stride {} off {} size {} need {} len {}]", (uint32)b.slot, b.stride, b.offset, b.size, b.required, b.bufferLength);
        }
        for (uint32 i = 0; i < d.numUniformBuffers; ++i)
        {
            const auto& b = d.uniformBuffers[i];
            buffers += fmt::format(" ub{}.{}[off {} size {} need {} len {}]", (uint32)b.stage, (uint32)b.index, b.offset, b.size, b.required, b.bufferLength);
        }
        cemuLog_log(LogType::Force, "Metal:   buffers{}", buffers);
        std::string textures;
        for (uint32 i = 0; i < d.numTextures; ++i)
        {
            const auto& t = d.textures[i];
            textures += fmt::format(" tex{}.{}[{}x{}x{} mips {} fmt {} dim {}{} view of level {} slice {} in {} mips {} layers]", (uint32)t.stage, (uint32)t.unit, t.width, t.height, t.depth, (uint32)t.mips, (uint32)t.pixelFormat, (uint32)t.type, t.fallback ? " FALLBACK" : "", (uint32)t.parentLevel, (uint32)t.parentSlice, (uint32)t.parentMips, (uint32)t.parentLayers);
        }
        cemuLog_log(LogType::Force, "Metal:   textures{}", textures);
    };

    uint32 suspects = 0;
    for (uint32 n = 0; n < stored; ++n)
        if (at(n).suspect != 0)
            ++suspects;
    cemuLog_log(LogType::Force, "Metal: {} of the kept draws were flagged when recorded (1 vertex buffer, 2 uniform buffer, 4 index buffer, 8 scissor clamped, 16 texture, 32 skipped)", suspects);

    uint32 printed = 0;
    for (uint32 n = 0; n < stored && printed < 12; ++n)
    {
        if (at(n).suspect != 0)
        {
            print(at(n));
            ++printed;
        }
    }

    if (haveRange)
    {
        printed = 0;
        for (uint32 n = 0; n < stored && printed < 80; ++n)
        {
            const auto& d = at(n);
            if (d.draw >= firstDraw && d.draw <= lastDraw + 48)
            {
                print(d);
                ++printed;
            }
        }
    }
    else
    {
        for (uint32 n = stored > 24 ? stored - 24 : 0; n < stored; ++n)
            print(at(n));
    }
}

static void LogFailedEncoders(MTL::CommandBuffer* commandBuffer, NS::Error* error, uint32& firstDraw, uint32& lastDraw, bool& haveRange)
{
    firstDraw = 0xFFFFFFFF;
    lastDraw = 0;
    haveRange = false;
    if (!error || !error->userInfo())
        return;

    auto* encoderInfos = static_cast<NS::Array*>(error->userInfo()->object(MTL::CommandBufferEncoderInfoErrorKey));
    if (!encoderInfos)
        return;
    uint32 logged = 0;
    for (NS::UInteger i = 0; i < encoderInfos->count() && logged < 6; ++i)
    {
        auto* info = static_cast<MTL::CommandBufferEncoderInfo*>(encoderInfos->object(i));
        if (!info || info->errorState() == MTL::CommandEncoderErrorStateCompleted)
            continue;
        const char* label = info->label() ? info->label()->utf8String() : "(no label)";
        cemuLog_log(LogType::Force, "Metal:   encoder \"{}\" state {} (0 unknown, 2 affected, 3 pending, 4 faulted)", label, (int)info->errorState());
        if (const char* drawText = std::strstr(label, " draw "))
        {
            const uint32 draw = (uint32)std::strtoul(drawText + 6, nullptr, 10);
            firstDraw = std::min(firstDraw, draw);
            lastDraw = std::max(lastDraw, draw);
            haveRange = true;
        }
        ++logged;
    }
}

void MetalRenderer::UpdateMemoryStatsAndRelievePressure()
{
    auto& w = LatteWait::Get();
    const bool evictionRequested = w.evictionRequested.exchange(false);
    const auto now = std::chrono::steady_clock::now();
    if (!evictionRequested && now - m_lastMemoryCheck < std::chrono::milliseconds(500))
        return;
    m_lastMemoryCheck = now;
    constexpr uint64 MB = 1024 * 1024;

    {
        std::vector<LatteTexture*> substitutes;
        for (LatteTexture* texture : LatteTexture::GetAllTextures())
        {
            if (texture && static_cast<LatteTextureMtl*>(texture)->IsNullSubstitute())
                substitutes.push_back(texture);
        }
        uint32 replaced = 0;
        for (LatteTexture* texture : substitutes)
        {
            const auto& live = LatteTexture::GetAllTextures();
            if (std::find(live.begin(), live.end(), texture) == live.end())
                continue;
            LatteTexture_Delete(texture);
            ++replaced;
        }
        if (replaced > 0)
        {
            static uint32 s_substituteLogs = 0;
            if (s_substituteLogs++ < 8)
                cemuLog_log(LogType::Force, "Metal: dropped {} textures that had no GPU memory so they are allocated again on their next use", replaced);
        }
    }

    uint32 numBuffers;
    size_t totalSize, freeSize;
    m_memoryManager->GetStagingAllocator().GetStats(numBuffers, totalSize, freeSize);
    w.memStagingMB.store((uint32)(totalSize / MB), std::memory_order_relaxed);
    m_memoryManager->GetIndexAllocator().GetStats(numBuffers, totalSize, freeSize);
    w.memIndexMB.store((uint32)(totalSize / MB), std::memory_order_relaxed);
    m_memoryManager->GetSnapshotStats(numBuffers, totalSize, freeSize);
    w.memSnapshotMB.store((uint32)(totalSize / MB), std::memory_order_relaxed);
    w.memBufferCacheMB.store(m_memoryManager->GetBufferCache() ? (uint32)(m_memoryManager->GetBufferCache()->length() / MB) : 0, std::memory_order_relaxed);
    w.memXfbMB.store(m_xfbRingBuffer ? (uint32)(m_xfbRingBuffer->length() / MB) : 0, std::memory_order_relaxed);
    w.memReadbackMB.store(m_readbackBuffer ? (uint32)(m_readbackBuffer->length() / MB) : 0, std::memory_order_relaxed);
    w.memDeviceMB.store((uint32)(m_device->currentAllocatedSize() / MB), std::memory_order_relaxed);
    w.memHostMappedMB.store((uint32)(m_memoryManager->GetHostAllocationSize() / MB), std::memory_order_relaxed);

    static std::chrono::steady_clock::time_point s_lastTextureStats;
    if (evictionRequested || now - s_lastTextureStats >= std::chrono::seconds(2))
    {
        s_lastTextureStats = now;
        uint64 textureBytes = 0;
        uint32 textureCount = 0;
        for (LatteTexture* texture : LatteTexture::GetAllTextures())
        {
            if (!texture)
                continue;
            ++textureCount;
            if (auto* mtlTexture = static_cast<LatteTextureMtl*>(texture)->GetTexture())
                textureBytes += mtlTexture->allocatedSize();
        }
        w.memTextureCount.store(textureCount, std::memory_order_relaxed);
        w.memTextureMB.store((uint32)(textureBytes / MB), std::memory_order_relaxed);
    }
    w.memStatsValid.store(true, std::memory_order_relaxed);

#if BOOST_OS_IOS
    const uint64 available = os_proc_available_memory();
    if (m_startAvailableMemory == 0)
        m_startAvailableMemory = available;
    uint64 lowMark, criticalMark;
    const bool smallHeadroom = DeviceCaps::EvictionMarks(DeviceCaps::GetBudgets(), m_startAvailableMemory, lowMark, criticalMark);
    static bool s_loggedMemoryMarks = false;
    if (!s_loggedMemoryMarks && m_startAvailableMemory != 0)
    {
        s_loggedMemoryMarks = true;
        cemuLog_log(LogType::Force, "Metal: {} memory headroom ({} MB free at the first frame): dropping cheap textures below {} MB free, all idle ones below {} MB",
            smallHeadroom ? "small" : "standard", m_startAvailableMemory / MB, lowMark / MB, criticalMark / MB);
    }
    if (!evictionRequested && available >= lowMark)
        return;

    std::vector<LatteTexture*> candidates = LatteTC_GetDeleteableTextures();
    uint32 deleted = 0;
    uint64 freedBytes = 0;
    for (LatteTexture* texture : candidates)
    {
        if (deleted >= 200)
            break;
        const auto& live = LatteTexture::GetAllTextures();
        if (std::find(live.begin(), live.end(), texture) == live.end())
            continue;
        if (auto* mtlTexture = static_cast<LatteTextureMtl*>(texture)->GetTexture())
            freedBytes += mtlTexture->allocatedSize();
        LatteTexture_Delete(texture);
        ++deleted;
    }
    if (available < criticalMark)
    {
        const uint64 needed = lowMark > available ? lowMark - available : 0;
        if (freedBytes < needed)
        {
            const uint32 currentTick = GetTickCount();
            const uint32 currentFrame = LatteGPUState.frameCounter;
            std::vector<std::pair<uint64, LatteTexture*>> idleGpuWritten;
            for (LatteTexture* texture : LatteTexture::GetAllTextures())
            {
                if (!texture || texture->lastAccessFrameCount == 0)
                    continue;
                if ((currentTick - texture->lastAccessTick) < 30000 || (currentFrame - texture->lastAccessFrameCount) < 300)
                    continue;
                auto* mtlTexture = static_cast<LatteTextureMtl*>(texture)->GetTexture();
                idleGpuWritten.emplace_back(mtlTexture ? (uint64)mtlTexture->allocatedSize() : 0, texture);
            }
            std::sort(idleGpuWritten.begin(), idleGpuWritten.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
            uint32 lost = 0;
            for (const auto& [size, texture] : idleGpuWritten)
            {
                if (freedBytes >= needed || lost >= 16)
                    break;
                const auto& live = LatteTexture::GetAllTextures();
                if (std::find(live.begin(), live.end(), texture) == live.end())
                    continue;
                freedBytes += size;
                LatteTexture_Delete(texture);
                ++deleted;
                ++lost;
            }
        }
    }
    if (deleted > 0)
    {
        w.texturesEvicted.fetch_add(deleted, std::memory_order_relaxed);
        if (m_memoryPressureLogs++ < 12)
            cemuLog_log(LogType::Force, "Metal: memory is low ({} MB left), deleted {} unused textures (about {} MB)", os_proc_available_memory() / MB, deleted, freedBytes / MB);
    }
    if (evictionRequested)
        w.evictionPasses.fetch_add(1);
#endif
}

void MetalRenderer::ProcessFinishedCommandBuffers()
{
    for (size_t i = 0; i < m_executingCommandBuffers.size();)
    {
        auto commandBuffer = m_executingCommandBuffers[i];
        if (CommandBufferCompleted(commandBuffer))
        {
            const uint32 generation = i < m_executingQueueGenerations.size() ? m_executingQueueGenerations[i] : m_queueGeneration;
            const bool staleQueue = generation != m_queueGeneration || m_gpuRecovering;
            if (commandBuffer->status() == MTL::CommandBufferStatusError)
            {
                auto& waitState = LatteWait::Get();
                const uint32 errorCount = waitState.erroredCommandBuffers.fetch_add(1) + 1;
                if (errorCount <= 8)
                {
                    NS::Error* error = commandBuffer->error();
                    cemuLog_log(LogType::Force, "Metal: command buffer failed (#{}): code {} {}{}", errorCount,
                        error ? (long)error->code() : 0L,
                        (error && error->localizedDescription()) ? error->localizedDescription()->utf8String() : "",
                        staleQueue ? " (command queue that was replaced)" : "");
                    uint32 firstDraw, lastDraw;
                    bool haveRange;
                    LogFailedEncoders(commandBuffer, error, firstDraw, lastDraw, haveRange);
                    if (!m_breadcrumbsDumped && !staleQueue)
                    {
                        m_breadcrumbsDumped = true;
                        DumpDrawBreadcrumbs(firstDraw, lastDraw, haveRange);
                    }
                }
                const long errorCode = commandBuffer->error() ? (long)commandBuffer->error()->code() : 0L;
                if (!staleQueue)
                {
                    waitState.cbErrorStreak.fetch_add(1, std::memory_order_relaxed);
                    waitState.cbLastErrorCode.store((int32_t)errorCode, std::memory_order_relaxed);
                }
                if (errorCode == 3 || errorCode == 4 || errorCode == 8 || errorCode == 11)
                {
                    if (staleQueue)
                    {
                    }
                    else if (m_gpuRecoveryPending)
                    {
                    }
                    else if (errorCode == 3 && m_gpuRecoveryCount < 3 && (m_gpuRecoveryCount == 0 || m_gpuRecoveryLogged))
                    {
                        m_gpuRecoveryPending = true;
                        cemuLog_log(LogType::Force, "Metal: GPU page fault, will replace the command queue and try to carry on (attempt {} of 3)", m_gpuRecoveryCount + 1);
                    }
                    else if (!waitState.gpuError.load())
                    {
                        cemuLog_log(LogType::Force, "Metal: stopping after GPU error code {} ({} command queue replacement(s) tried)", errorCode, m_gpuRecoveryCount);
                        waitState.gpuErrorCode.store((int32_t)errorCode);
                        waitState.gpuError.store(true);
                    }
                }
                static_cast<MTL::SharedEvent*>(m_event)->setSignaledValue((uint64_t)m_executingEventValues[i]);
            }
            else
            {
                auto& waitState = LatteWait::Get();
                waitState.cbRetired.fetch_add(1, std::memory_order_relaxed);
                if (!staleQueue)
                    waitState.cbErrorStreak.store(0, std::memory_order_relaxed);
                if (m_gpuRecoveryCount > 0 && !staleQueue && !m_gpuRecoveryLogged)
                {
                    m_gpuRecoveryLogged = true;
                    cemuLog_log(LogType::Force, "Metal: the first command buffer on the new command queue completed without error");
                }
            }
            if (commandBuffer->status() == MTL::CommandBufferStatusCompleted)
            {
                const double gpuStart = commandBuffer->GPUStartTime();
                const double gpuEnd = commandBuffer->GPUEndTime();
                if (gpuStart > 0.0 && gpuEnd > gpuStart)
                {
                    PerfTelemetry::Get().mtlGpuNs.fetch_add((uint64)((gpuEnd - gpuStart) * 1e9), std::memory_order_relaxed);
                    PerfTelemetry::Get().mtlCommandBuffers.fetch_add(1, std::memory_order_relaxed);
                }
            }
            m_memoryManager->CleanupBuffers(commandBuffer);
            commandBuffer->release();
            m_executingCommandBuffers.erase(m_executingCommandBuffers.begin() + i);
            m_executingEventValues.erase(m_executingEventValues.begin() + i);
            if (i < m_executingQueueGenerations.size())
                m_executingQueueGenerations.erase(m_executingQueueGenerations.begin() + i);
        }
        else
        {
            ++i;
        }
    }
    LatteWait::Get().executingCommandBuffers.store((uint32)m_executingCommandBuffers.size(), std::memory_order_relaxed);
}

bool MetalRenderer::RecoverFromGpuFault()
{
    m_gpuRecoveryPending = false;
    m_gpuRecovering = true;
    m_gpuRecoveryLogged = false;
    auto fail = [&](const char* why) {
        m_gpuRecovering = false;
        m_gpuRecoveryCount++;
        cemuLog_log(LogType::Force, "Metal: could not replace the command queue ({}); stopping", why);
        auto& waitState = LatteWait::Get();
        waitState.gpuErrorCode.store(3);
        waitState.gpuError.store(true);
        return false;
    };

    for (int guard = 0; guard < 64 && !m_executingCommandBuffers.empty(); ++guard)
    {
        const bool finished = WaitForCommandBuffer(m_executingCommandBuffers.front(), "GPU recovery: waiting for the faulted command queue to drain");
        ProcessFinishedCommandBuffers();
        if (!finished)
            return fail("the old command buffers did not finish");
    }
    if (!m_executingCommandBuffers.empty())
        return fail("the old command buffers did not finish");

    MTL::CommandQueue* newQueue = m_device->newCommandQueue();
    if (!newQueue)
        return fail("newCommandQueue returned nothing");

    auto* sharedEvent = static_cast<MTL::SharedEvent*>(m_event);
    if (m_eventValue >= 0 && sharedEvent->signaledValue() < (uint64_t)m_eventValue)
        sharedEvent->setSignaledValue((uint64_t)m_eventValue);

    m_commandQueue->release();
    m_commandQueue = newQueue;
    m_queueGeneration++;
    m_gpuRecoveryCount++;
    m_gpuRecovering = false;
    cemuLog_log(LogType::Force, "Metal: command queue replaced after the page fault (generation {}); rendering continues if the new queue is accepted", m_queueGeneration);
    return true;
}

bool MetalRenderer::AcquireDrawable(bool mainWindow)
{
    auto& layer = GetLayer(mainWindow);
    if (!layer.GetLayer())
        return false;

#if BOOST_OS_IOS
    if (!layer.GetDrawable() && PerfTelemetry::AppInBackground().load(std::memory_order_relaxed))
        return false;
    const auto outputBit = mainWindow ? 1u : 2u;
    if (!layer.GetDrawable() && !(WindowSystem::GetWindowInfo().visible_outputs.load() & outputBit))
        return false;
#endif

    const bool latteBufferUsesSRGB = mainWindow ? LatteGPUState.tvBufferUsesSRGB : LatteGPUState.drcBufferUsesSRGB;
    const auto pixelFormat = latteBufferUsesSRGB ? MTL::PixelFormatBGRA8Unorm_sRGB : MTL::PixelFormatBGRA8Unorm;
    if (layer.GetLayer()->pixelFormat() != pixelFormat)
        layer.GetLayer()->setPixelFormat(pixelFormat);
    m_state.m_usesSRGB = latteBufferUsesSRGB;

    const uint64 acquireStart = PerfTelemetry::NowNs();
    const bool acquired = layer.AcquireDrawable();
    const uint64 acquireNs = PerfTelemetry::NowNs() - acquireStart;
    PerfTelemetry::Get().drawableWaitNs.fetch_add(acquireNs, std::memory_order_relaxed);
    PerfTelemetry::Get().gpuSyncNs.fetch_add(acquireNs, std::memory_order_relaxed);
    return acquired;
}

bool MetalRenderer::CheckIfRenderPassNeedsFlush(LatteDecompilerShader* shader)
{
    if (!shader)
        return false;

    sint32 textureCount = shader->resourceMapping.getTextureCount();
    for (int i = 0; i < textureCount; ++i)
    {
        const auto relative_textureUnit = shader->resourceMapping.getTextureUnitFromBindingPoint(i);
        auto hostTextureUnit = relative_textureUnit;
        auto textureDim = shader->textureUnitDim[relative_textureUnit];

        uint8 renderTargetIndex = shader->textureRenderTargetIndex[relative_textureUnit];
        if (m_supportsFramebufferFetch && renderTargetIndex != 255)
        {
            auto format = LatteMRT::GetColorBufferFormat(renderTargetIndex, LatteGPUState.contextNew);
            if (GetMtlPixelFormat(format, false) != MTL::PixelFormatInvalid)
                continue;
        }

        auto texUnitRegIndex = hostTextureUnit * 7;
        switch (shader->shaderType)
        {
        case LatteConst::ShaderType::Vertex:
            hostTextureUnit += LATTE_CEMU_VS_TEX_UNIT_BASE;
            texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
            break;
        case LatteConst::ShaderType::Pixel:
            hostTextureUnit += LATTE_CEMU_PS_TEX_UNIT_BASE;
            texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
            break;
        case LatteConst::ShaderType::Geometry:
            hostTextureUnit += LATTE_CEMU_GS_TEX_UNIT_BASE;
            texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
            break;
        default:
            UNREACHABLE;
        }

        auto textureView = m_state.m_textures[hostTextureUnit];
        if (!textureView)
            continue;

        LatteTexture* baseTexture = textureView->baseTexture;

        for (uint8 i = 0; i < LATTE_NUM_COLOR_TARGET; i++)
        {
            auto colorTarget = m_state.m_activeFBO.m_fbo->colorBuffer[i].texture;
            if (colorTarget && colorTarget->baseTexture == baseTexture)
                return true;
        }
        auto depthTarget = m_state.m_activeFBO.m_fbo->depthBuffer.texture;
        if (depthTarget && depthTarget->baseTexture == baseTexture)
            return true;
    }

    return false;
}

void MetalRenderer::PrepareUniformBufferSizes(LatteDecompilerShader* shader)
{
    if (!shader)
        return;
    const auto stage = GetMtlGeneralShaderType(shader->shaderType);
    for (const auto& required : shader->list_quickBufferList)
    {
        const auto i = required.index;
        if (i >= LATTE_NUM_MAX_UNIFORM_BUFFERS || shader->resourceMapping.uniformBuffersBindingPoint[i] < 0)
            continue;
        auto*& buffer = m_state.m_uniformBuffers[stage][i];
        auto& offset = m_state.m_uniformBufferOffsets[stage][i];
        auto& size = m_state.m_uniformBufferSizes[stage][i];
        if (!buffer || offset >= buffer->length())
            continue;
        size = std::min(size, buffer->length() - offset);
        if (required.size <= size)
            continue;
        const bool gpuCopy = buffer->storageMode() == MTL::StorageModePrivate ||
            (buffer == m_memoryManager->GetBufferCache() && m_memoryManager->SharedCacheBusy(offset, size, true));
        if (gpuCopy)
            GetBlitCommandEncoder(PerfTelemetry::kBreakUniformResize);
        else
            GetCommandBuffer();
        auto& allocator = m_memoryManager->GetStagingAllocator();
        auto allocation = allocator.AllocateBufferMemory(required.size, 16);
        if (!allocation.mtlBuffer)
            continue;
        std::memset(allocation.memPtr, 0, allocation.size);
        if (!gpuCopy)
            std::memcpy(allocation.memPtr, static_cast<uint8*>(buffer->contents()) + offset, size);
        allocator.FlushReservation(allocation);
        if (gpuCopy && size)
        {
            CopyBufferToBuffer(buffer, offset, allocation.mtlBuffer, allocation.bufferOffset, size, ALL_MTL_RENDER_STAGES, ALL_MTL_RENDER_STAGES);
            m_memoryManager->TrackSharedCache(buffer, offset, size);
        }
        buffer = allocation.mtlBuffer;
        offset = allocation.bufferOffset;
        size = required.size;
    }
}

bool MetalRenderer::BindStageResources(MTL::RenderCommandEncoder* renderCommandEncoder, LatteDecompilerShader* shader, bool usesGeometryShader)
{
    auto mtlShaderType = GetMtlShaderType(shader->shaderType, usesGeometryShader);
    auto* rendererShader = static_cast<RendererShaderMtl*>(shader->shader);
    MTL::ArgumentEncoder* argumentEncoder = nullptr;
    MetalArgumentBindings argumentBindings{};
    const bool shaderUsesArgumentBuffer = shader->resourceMapping.argumentBufferBindingPoint >= 0;
    if (shaderUsesArgumentBuffer)
    {
        argumentEncoder = rendererShader->GetArgumentEncoder();
        if (!argumentEncoder)
        {
            cemuLog_logOnce(LogType::Force, "Metal shader {:016x} has no argument encoder", shader->baseHash);
            return false;
        }

        const uint32 encodedLength = rendererShader->GetArgumentBufferEncodedLength();
        if (encodedLength == 0)
        {
            cemuLog_logOnce(LogType::Force, "Metal shader {:016x} has an empty argument-buffer layout", shader->baseHash);
            return false;
        }

        argumentBindings[MetalArgumentBuffer::Dummy] = {MetalArgumentBinding::Type::Constant, nullptr, 0};
    }

    MTL::RenderStages renderStage = MTL::RenderStageVertex;
    switch (mtlShaderType)
    {
        case METAL_SHADER_TYPE_VERTEX:
            renderStage = MTL::RenderStageVertex;
            break;
        case METAL_SHADER_TYPE_OBJECT:
            renderStage = MTL::RenderStageObject;
            break;
        case METAL_SHADER_TYPE_MESH:
            renderStage = MTL::RenderStageMesh;
            break;
        case METAL_SHADER_TYPE_FRAGMENT:
            renderStage = MTL::RenderStageFragment;
            break;
        default:
            UNREACHABLE;
    }

    for (sint32 relative_textureUnit = 0; relative_textureUnit < LATTE_NUM_MAX_TEX_UNITS; relative_textureUnit++)
    {
        if (shader->resourceMapping.textureUnitToBindingPoint[relative_textureUnit] < 0)
            continue;

        auto hostTextureUnit = relative_textureUnit;

        uint8 renderTargetIndex = shader->textureRenderTargetIndex[relative_textureUnit];
        if (m_supportsFramebufferFetch && renderTargetIndex != 255)
        {
            auto format = LatteMRT::GetColorBufferFormat(renderTargetIndex, LatteGPUState.contextNew);
            if (GetMtlPixelFormat(format, false) != MTL::PixelFormatInvalid)
                continue;
        }

        auto textureDim = shader->textureUnitDim[relative_textureUnit];
        auto texUnitRegIndex = hostTextureUnit * 7;
        switch (shader->shaderType)
        {
            case LatteConst::ShaderType::Vertex:
                hostTextureUnit += LATTE_CEMU_VS_TEX_UNIT_BASE;
                texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_VS;
                break;
            case LatteConst::ShaderType::Pixel:
                hostTextureUnit += LATTE_CEMU_PS_TEX_UNIT_BASE;
                texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_PS;
                break;
            case LatteConst::ShaderType::Geometry:
                hostTextureUnit += LATTE_CEMU_GS_TEX_UNIT_BASE;
                texUnitRegIndex += Latte::REGADDR::SQ_TEX_RESOURCE_WORD0_N_GS;
                break;
            default:
                UNREACHABLE;
        }

        uint32 binding = shader->resourceMapping.textureUnitToBindingPoint[relative_textureUnit];
        if (binding >= MAX_MTL_TEXTURES)
        {
            cemuLog_logOnce(LogType::Force, "invalid texture binding {}", binding);
            continue;
        }
        sint32 samplerBinding = shader->resourceMapping.textureUnitToSamplerBindingPoint[relative_textureUnit];

        auto textureView = m_state.m_textures[hostTextureUnit];
        const bool depthTexture = shader->textureUsesDepthCompare[relative_textureUnit] && IsValidDepthTextureType(textureDim);
        MTL::SamplerState* sampler = m_nearestSampler;
        uint32 stageSamplerIndex = shader->textureUnitSamplerAssignment[relative_textureUnit];
        if (samplerBinding >= 0 && stageSamplerIndex != LATTE_DECOMPILER_SAMPLER_NONE)
        {
            uint32 samplerIndex = stageSamplerIndex + LatteDecompiler_getTextureSamplerBaseIndex(shader->shaderType);
            _LatteRegisterSetSampler* samplerWords = LatteGPUState.contextNew.SQ_TEX_SAMPLER + samplerIndex;
            if (textureView && textureView->baseTexture->overwriteInfo.anisotropicLevel >= 0)
                samplerWords->WORD0.set_MAX_ANISO_RATIO(textureView->baseTexture->overwriteInfo.anisotropicLevel);
            sampler = m_samplerCache->GetSamplerState(LatteGPUState.contextNew, shader->shaderType, stageSamplerIndex, samplerWords, depthTexture);
        }
        if (samplerBinding >= 0)
        {
            if (!sampler)
            {
                cemuLog_logOnce(LogType::Force, "Metal shader {:016x} could not allocate a sampler", shader->baseHash);
                return false;
            }
            if (argumentEncoder)
                argumentBindings[MetalArgumentBuffer::SamplerBase + samplerBinding] = {MetalArgumentBinding::Type::Sampler, sampler, 0};
            else
                SetSamplerState(renderCommandEncoder, mtlShaderType, sampler, samplerBinding);
        }

        MTL::Texture* mtlTexture = nullptr;
        bool textureViewFailed = false;
        const auto nullTexture = [&]() { return GetNullSampledTexture(textureDim, shader->textureIsIntegerFormat[relative_textureUnit], depthTexture); };
        if (!textureView)
        {
            mtlTexture = nullTexture();
        }
        else if (textureDim == Latte::E_DIM::DIM_1D && (textureView->dim != Latte::E_DIM::DIM_1D))
        {
            mtlTexture = nullTexture();
        }
        else if (textureDim == Latte::E_DIM::DIM_2D && (textureView->dim != Latte::E_DIM::DIM_2D && textureView->dim != Latte::E_DIM::DIM_2D_MSAA))
        {
            mtlTexture = nullTexture();
        }
        else if (textureDim != Latte::E_DIM::DIM_1D &&
                 textureDim != Latte::E_DIM::DIM_2D &&
                 textureView->dim != textureDim)
        {
            mtlTexture = nullTexture();
        }
        else
        {
            uint32 word4 = LatteGPUState.contextRegister[texUnitRegIndex + 4];
            mtlTexture = textureView->GetSwizzledView(word4);
            if (!mtlTexture)
            {
                mtlTexture = nullTexture();
                textureViewFailed = true;
            }
            else if (depthTexture && !textureView->baseTexture->isDepth)
            {
                cemuLog_logOnce(LogType::Force, "Metal shader {:016x} compares against texture unit {} (format {:04x}), which is a colour texture, not a depth texture; the result of that compare is undefined on Metal", shader->baseHash, relative_textureUnit, (uint32)textureView->format);
            }
        }

        RecordBreadcrumbTexture(shader->shaderType, relative_textureUnit, (uint32)textureDim, mtlTexture, textureViewFailed);

        if (argumentEncoder)
        {
            argumentBindings[MetalArgumentBuffer::TextureBase + relative_textureUnit] = {MetalArgumentBinding::Type::Texture, mtlTexture, 0};
            UseResource(renderCommandEncoder, mtlTexture, MTL::ResourceUsageRead | MTL::ResourceUsageSample, renderStage);
        }
        else
            SetTexture(renderCommandEncoder, mtlShaderType, mtlTexture, binding);
    }

    auto GET_UNIFORM_DATA_PTR = [&](size_t index) { return supportBufferData + (index / 4); };

    sint32 shaderAluConst;
    sint32 shaderUniformRegisterOffset;

    switch (shader->shaderType)
    {
        case LatteConst::ShaderType::Vertex:
            shaderAluConst = 0x400;
            shaderUniformRegisterOffset = mmSQ_VTX_UNIFORM_BLOCK_START;
            break;
        case LatteConst::ShaderType::Pixel:
            shaderAluConst = 0;
            shaderUniformRegisterOffset = mmSQ_PS_UNIFORM_BLOCK_START;
            break;
        case LatteConst::ShaderType::Geometry:
            shaderAluConst = 0;
            shaderUniformRegisterOffset = mmSQ_GS_UNIFORM_BLOCK_START;
            break;
        default:
            UNREACHABLE;
    }

    if (shader->resourceMapping.uniformVarsBufferBindingPoint >= 0)
    {
        if (shader->uniform.uniformRangeSize > sizeof(supportBufferData))
        {
            cemuLog_logOnce(LogType::Force, "Metal shader {:016x} exceeds the support buffer capacity", shader->baseHash);
            return false;
        }
        if (shader->uniform.list_ufTexRescale.empty() == false)
        {
            for (auto& entry : shader->uniform.list_ufTexRescale)
            {
                float* xyScale = LatteTexture_getEffectiveTextureScale(shader->shaderType, entry.texUnit);
                memcpy(entry.currentValue, xyScale, sizeof(float) * 2);
                memcpy(GET_UNIFORM_DATA_PTR(entry.uniformLocation), xyScale, sizeof(float) * 2);
            }
        }
        if (shader->uniform.loc_alphaTestRef >= 0)
        {
            *GET_UNIFORM_DATA_PTR(shader->uniform.loc_alphaTestRef) = LatteGPUState.contextNew.SX_ALPHA_REF.get_ALPHA_TEST_REF();
        }
        if (shader->uniform.loc_pointSize >= 0)
        {
            const auto& pointSizeReg = LatteGPUState.contextNew.PA_SU_POINT_SIZE;
            float pointWidth = (float)pointSizeReg.get_WIDTH() / 8.0f;
            if (pointWidth == 0.0f)
                pointWidth = 1.0f / 8.0f;
            *GET_UNIFORM_DATA_PTR(shader->uniform.loc_pointSize) = pointWidth;
        }
        if (shader->uniform.loc_remapped >= 0)
        {
            LatteBufferCache_LoadRemappedUniforms(shader, GET_UNIFORM_DATA_PTR(shader->uniform.loc_remapped));
        }
        if (shader->uniform.loc_uniformRegister >= 0)
        {
            uint32* uniformRegData = (uint32*)(LatteGPUState.contextRegister + mmSQ_ALU_CONSTANT0_0 + shaderAluConst);
            memcpy(GET_UNIFORM_DATA_PTR(shader->uniform.loc_uniformRegister), uniformRegData, shader->uniform.count_uniformRegister * 16);
        }
        if (shader->uniform.loc_windowSpaceToClipSpaceTransform >= 0)
        {
            sint32 viewportWidth;
            sint32 viewportHeight;
            LatteRenderTarget_GetCurrentVirtualViewportSize(&viewportWidth, &viewportHeight);
            float* v = GET_UNIFORM_DATA_PTR(shader->uniform.loc_windowSpaceToClipSpaceTransform);
            v[0] = 2.0f / (float)viewportWidth;
            v[1] = 2.0f / (float)viewportHeight;
        }
        if (shader->uniform.loc_fragCoordScale >= 0)
        {
            LatteMRT::GetCurrentFragCoordScale(GET_UNIFORM_DATA_PTR(shader->uniform.loc_fragCoordScale));
        }
        if (shader->uniform.loc_baseVertex >= 0)
            *reinterpret_cast<sint32*>(GET_UNIFORM_DATA_PTR(shader->uniform.loc_baseVertex)) = m_state.m_drawResources.baseVertex;
        if (shader->uniform.loc_baseInstance >= 0)
            *reinterpret_cast<uint32*>(GET_UNIFORM_DATA_PTR(shader->uniform.loc_baseInstance)) = m_state.m_drawResources.baseInstance;
        if (shader->uniform.loc_verticesPerInstance >= 0)
        {
            *reinterpret_cast<uint32*>(GET_UNIFORM_DATA_PTR(shader->uniform.loc_verticesPerInstance)) = m_state.m_streamoutState.verticesPerInstance;
            for (sint32 b = 0; b < LATTE_NUM_STREAMOUT_BUFFER; b++)
            {
                if (shader->uniform.loc_streamoutBufferBase[b] >= 0)
                {
                    *reinterpret_cast<uint32*>(GET_UNIFORM_DATA_PTR(shader->uniform.loc_streamoutBufferBase[b])) = m_state.m_streamoutState.buffers[b].ringBufferOffset;
                }
                if (shader->uniform.loc_streamoutBufferSize[b] >= 0)
                    *reinterpret_cast<uint32*>(GET_UNIFORM_DATA_PTR(shader->uniform.loc_streamoutBufferSize[b])) = m_state.m_streamoutState.buffers[b].rangeSize;
            }
        }

        size_t size = shader->uniform.uniformRangeSize;
        const uint32 supportBinding = argumentEncoder ? MTL_SUPPORT_BUFFER_BINDING : (uint32)shader->resourceMapping.uniformVarsBufferBindingPoint;
        if (argumentEncoder && size <= MTL_MAX_SET_BYTES)
        {
            const uint32 length = (uint32)std::min<size_t>(Align(std::max<size_t>(size, 16), 16), MTL_MAX_SET_BYTES);
            SetBytes(renderCommandEncoder, mtlShaderType, supportBufferData, length, supportBinding);
        }
        else
        {
            auto* allocation = m_memoryManager->GetCachedSnapshot(MetalMemoryManager::SupportSnapshotBase + mtlShaderType, supportBufferData, size);
            if (!allocation)
                return false;
            SetBuffer(renderCommandEncoder, mtlShaderType, allocation->mtlBuffer, allocation->bufferOffset, supportBinding);
        }
    }

    for (sint32 i = 0; i < LATTE_NUM_MAX_UNIFORM_BUFFERS; i++)
    {
        if (shader->resourceMapping.uniformBuffersBindingPoint[i] >= 0)
        {
            uint32 binding = shader->resourceMapping.uniformBuffersBindingPoint[i];
            if (binding >= MAX_MTL_BUFFERS)
            {
                cemuLog_logOnce(LogType::Force, "invalid buffer binding {}", binding);
                continue;
            }

            MetalGeneralShaderType shaderType = GetMtlGeneralShaderType(shader->shaderType);
            MTL::Buffer* buffer = m_state.m_uniformBuffers[shaderType][i];
            size_t offset = m_state.m_uniformBufferOffsets[shaderType][i];
            size_t size = m_state.m_uniformBufferSizes[shaderType][i];
            if (!buffer || offset == INVALID_OFFSET || offset >= buffer->length())
            {
                buffer = m_nullBuffer;
                offset = 0;
            }
            else
            {
                size = std::min(size, buffer->length() - offset);
            }

            m_memoryManager->TrackSharedCache(buffer, offset, size);

            if (m_crumb && m_crumb->numUniformBuffers < MetalDrawBreadcrumb::MAX_UNIFORM_BUFFERS)
            {
                uint64 requiredSize = 0;
                for (const auto& quick : shader->list_quickBufferList)
                    if (quick.index == i)
                        requiredSize = quick.size;
                const uint64 reach = buffer == m_nullBuffer ? buffer->length() : size;
                auto& entry = m_crumb->uniformBuffers[m_crumb->numUniformBuffers++];
                entry.stage = static_cast<uint8>(shader->shaderType);
                entry.index = static_cast<uint8>(i);
                entry.offset = offset;
                entry.size = reach;
                entry.required = requiredSize;
                entry.bufferLength = buffer->length();
                if (requiredSize > reach)
                    m_crumb->suspect |= MetalDrawBreadcrumb::SUSPECT_UNIFORM_BUFFER;
            }

            if (argumentEncoder)
            {
                argumentBindings[MetalArgumentBuffer::UniformBufferBase + i] = {MetalArgumentBinding::Type::Buffer, buffer, offset};
                UseResource(renderCommandEncoder, buffer, MTL::ResourceUsageRead, renderStage);
            }
            else
                SetBuffer(renderCommandEncoder, mtlShaderType, buffer, offset, binding);
        }
    }

    if (shader->resourceMapping.tfStorageBindingPoint >= 0)
    {
        MTL::Buffer* xfbRingBuffer = GetXfbRingBuffer() ? GetXfbRingBuffer() : m_nullBuffer;
        if (argumentEncoder)
        {
            argumentBindings[MetalArgumentBuffer::StreamoutBuffer] = {MetalArgumentBinding::Type::Buffer, xfbRingBuffer, 0};
            UseResource(renderCommandEncoder, xfbRingBuffer, MTL::ResourceUsageWrite, renderStage);
        }
        else
            SetBuffer(renderCommandEncoder, mtlShaderType, xfbRingBuffer, 0, shader->resourceMapping.tfStorageBindingPoint);
    }

    if (argumentEncoder && shader->shaderType == LatteConst::ShaderType::Vertex)
    {
        const LatteFetchShader* fetchShader = LatteSHRC_GetActiveFetchShader();
        const bool fetchVertexManually =
        usesGeometryShader ||
        (fetchShader && fetchShader->mtlFetchVertexManually) ||
        (shader->hasStreamoutBufferWrite && !usesGeometryShader);
        if (fetchVertexManually && fetchShader)
        {
            bool encodedVertexBuffers[LATTE_MAX_VERTEX_BUFFERS]{};
            for (const auto& bufferGroup : fetchShader->bufferGroups)
            {
                const uint32 bufferIndex = bufferGroup.attributeBufferIndex;
                if (bufferIndex >= LATTE_MAX_VERTEX_BUFFERS || encodedVertexBuffers[bufferIndex])
                    continue;

                MTL::Buffer* vertexBuffer = m_state.m_vertexBuffers[bufferIndex];
                size_t vertexBufferOffset = m_state.m_vertexBufferOffsets[bufferIndex];
                size_t vertexBufferSize = m_state.m_vertexBufferSizes[bufferIndex];
                if (!vertexBuffer || vertexBufferOffset == INVALID_OFFSET || vertexBufferOffset >= vertexBuffer->length())
                {
                    vertexBuffer = m_nullBuffer;
                    vertexBufferOffset = 0;
                    vertexBufferSize = 0;
                }
                argumentBindings[MetalArgumentBuffer::VertexBufferBase + bufferIndex] = {MetalArgumentBinding::Type::Buffer, vertexBuffer, vertexBufferOffset};
                UseResource(renderCommandEncoder, vertexBuffer, MTL::ResourceUsageRead, renderStage);
                vertexBufferSize = std::min<size_t>(vertexBufferSize, vertexBuffer->length() - vertexBufferOffset);
                argumentBindings[MetalArgumentBuffer::VertexBufferSizeBase + bufferIndex] = {MetalArgumentBinding::Type::Constant, nullptr,
                    static_cast<uint32>(std::min<size_t>(vertexBufferSize, std::numeric_limits<uint32>::max()))};
                encodedVertexBuffers[bufferIndex] = true;
            }
        }

        const bool usesLogicalVertexIds = usesGeometryShader || shader->hasStreamoutBufferWrite;
        if (usesLogicalVertexIds)
        {
            MTL::Buffer* indexBuffer = m_state.m_drawResources.indexBuffer;
            size_t indexBufferOffset = m_state.m_drawResources.indexBufferOffset;
            size_t indexBufferSize = m_state.m_drawResources.indexBufferSize;
            if (!indexBuffer || indexBufferOffset >= indexBuffer->length())
            {
                indexBuffer = m_nullBuffer;
                indexBufferOffset = 0;
                indexBufferSize = 0;
            }
            argumentBindings[MetalArgumentBuffer::IndexBuffer] = {MetalArgumentBinding::Type::Buffer, indexBuffer, indexBufferOffset};
            UseResource(renderCommandEncoder, indexBuffer, MTL::ResourceUsageRead, renderStage);
            indexBufferSize = std::min<size_t>(indexBufferSize, indexBuffer->length() - indexBufferOffset);
            argumentBindings[MetalArgumentBuffer::IndexBufferSize] = {MetalArgumentBinding::Type::Constant, nullptr,
                static_cast<uint32>(std::min<size_t>(indexBufferSize, std::numeric_limits<uint32>::max()))};
            argumentBindings[MetalArgumentBuffer::IndexType] = {MetalArgumentBinding::Type::Constant, nullptr, m_state.m_drawResources.indexType};
        }
    }

    if (argumentEncoder)
    {
        auto* allocation = m_memoryManager->GetCachedArgumentBuffer(mtlShaderType, argumentEncoder, argumentBindings);
        if (!allocation)
            return false;
        SetBuffer(renderCommandEncoder, mtlShaderType, allocation->mtlBuffer, allocation->bufferOffset, shader->resourceMapping.argumentBufferBindingPoint);
    }
    return true;
}

void MetalRenderer::ClearColorTextureInternal(MTL::Texture* mtlTexture, sint32 sliceIndex, sint32 mipIndex, float r, float g, float b, float a)
{
    if (!mtlTexture)
        return;
    NS_STACK_SCOPED MTL::RenderPassDescriptor* renderPassDescriptor = MTL::RenderPassDescriptor::alloc()->init();
    auto colorAttachment = renderPassDescriptor->colorAttachments()->object(0);
    colorAttachment->setTexture(mtlTexture);
    colorAttachment->setClearColor(MTL::ClearColor(r, g, b, a));
    colorAttachment->setLoadAction(MTL::LoadActionClear);
    colorAttachment->setStoreAction(MTL::StoreActionStore);
    colorAttachment->setSlice(sliceIndex);
    colorAttachment->setLevel(mipIndex);

    GetTemporaryRenderCommandEncoder(renderPassDescriptor);
    EndEncoding();

    m_performanceMonitor.m_clears++;
}

void MetalRenderer::CopyBufferToBuffer(MTL::Buffer* src, uint32 srcOffset, MTL::Buffer* dst, uint32 dstOffset, uint32 size, MTL::RenderStages after, MTL::RenderStages before)
{
#if BOOST_OS_IOS
    if (m_isAppleGPU && m_copyBufferToBuffer32Pipeline && m_copyBufferToBuffer32Pipeline->GetRenderPipelineState()
        && m_encoderType == MetalEncoderType::Render && m_nextBufferCopyBreak == PerfTelemetry::kBreakStreamout
        && size > 0 && (size & 3) == 0 && (srcOffset & 3) == 0 && (dstOffset & 3) == 0)
    {
        m_nextBufferCopyBreak = PerfTelemetry::kBreakBufferCopy;
        auto renderCommandEncoder = static_cast<MTL::RenderCommandEncoder*>(m_commandEncoder);

        MTL::Resource* barrierBuffers[] = {src};
        renderCommandEncoder->memoryBarrier(barrierBuffers, 1, after, after | MTL::RenderStageVertex);

        renderCommandEncoder->setRenderPipelineState(m_copyBufferToBuffer32Pipeline->GetRenderPipelineState());
        m_state.m_encoderState.m_renderPipelineState = m_copyBufferToBuffer32Pipeline->GetRenderPipelineState();

        SetBuffer(renderCommandEncoder, METAL_SHADER_TYPE_VERTEX, src, srcOffset, GET_HELPER_BUFFER_BINDING(0));
        SetBuffer(renderCommandEncoder, METAL_SHADER_TYPE_VERTEX, dst, dstOffset, GET_HELPER_BUFFER_BINDING(1));

        renderCommandEncoder->drawPrimitives(MTL::PrimitiveTypePoint, NS::UInteger(0), NS::UInteger(size / 4));

        barrierBuffers[0] = dst;
        renderCommandEncoder->memoryBarrier(barrierBuffers, 1, before | MTL::RenderStageVertex, before);
        return;
    }
#endif
        auto blitCommandEncoder = GetBlitCommandEncoder(m_nextBufferCopyBreak);
        m_nextBufferCopyBreak = PerfTelemetry::kBreakBufferCopy;

        blitCommandEncoder->copyFromBuffer(src, srcOffset, dst, dstOffset, size);
}

void MetalRenderer::SwapBuffer(bool mainWindow)
{
    auto& layer = GetLayer(mainWindow);
    const bool drawableAlreadyAcquired = layer.GetDrawable() != nullptr;

    if (!AcquireDrawable(mainWindow))
        return;

    if (!drawableAlreadyAcquired)
        ClearColorTextureInternal(layer.GetDrawableTexture(), 0, 0, 0.0f, 0.0f, 0.0f, 1.0f);

    auto commandBuffer = GetCommandBuffer();
    layer.PresentDrawable(commandBuffer);
    (mainWindow ? PerfTelemetry::Get().tvPresents : PerfTelemetry::Get().padPresents).fetch_add(1, std::memory_order_relaxed);
}

void MetalRenderer::EnsureImGuiBackend()
{
    if (!ImGui::GetIO().BackendRendererUserData)
    {
        ImGui_ImplMetal_Init(m_device);
    }
}

void MetalRenderer::StartCapture()
{
    auto captureManager = MTL::CaptureManager::sharedCaptureManager();
    auto desc = MTL::CaptureDescriptor::alloc()->init();
    desc->setCaptureObject(m_device);

    if (captureManager->supportsDestination(MTL::CaptureDestinationDeveloperTools))
    {
        desc->setDestination(MTL::CaptureDestinationDeveloperTools);
    }
    else
    {
        if (GetConfig().gpu_capture_dir.GetValue().empty())
        {
            cemuLog_log(LogType::Force, "No GPU capture directory specified, cannot do a GPU capture");
            return;
        }

        if (!captureManager->supportsDestination(MTL::CaptureDestinationGPUTraceDocument))
        {
            cemuLog_log(LogType::Force, "GPU trace document destination is not available, cannot do a GPU capture");
            return;
        }

        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&now_time), "%Y-%m-%d_%H-%M-%S");
        std::string now_str = oss.str();

        std::string capturePath = fmt::format("{}/cemu_{}.gputrace", GetConfig().gpu_capture_dir.GetValue(), now_str);
        desc->setDestination(MTL::CaptureDestinationGPUTraceDocument);
        desc->setOutputURL(ToNSURL(capturePath));
    }

    NS::Error* error = nullptr;
    captureManager->startCapture(desc, &error);
    if (error)
    {
        cemuLog_log(LogType::Force, "Failed to start GPU capture: {}", error->localizedDescription()->utf8String());
    }

    m_capturing = true;
}

void MetalRenderer::EndCapture()
{
    auto captureManager = MTL::CaptureManager::sharedCaptureManager();
    captureManager->stopCapture();

    m_capturing = false;
}
