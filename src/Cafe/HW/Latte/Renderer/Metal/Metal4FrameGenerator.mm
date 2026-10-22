#include "Cafe/HW/Latte/Renderer/Metal/Metal4FrameGenerator.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"

#import <MetalFX/MetalFX.h>

#include <algorithm>
#include <cmath>
#if __has_include(<os/proc.h>)
#include <os/proc.h>
#define ESPRESSU_HAVE_PROC_MEMORY 1
#endif

namespace
{
	constexpr uint32_t kSlots = 3;
	constexpr uint32_t kLumaMaxWidth = 320;
	constexpr uint32_t kBlockSize = 8;
	// Kept free on top of the frame generation images, so turning it on never pushes iOS into a kill.
	constexpr uint64_t kHeadroomMargin = 300ull << 20;

	struct GeneratorParams
	{
		uint32_t lowSize[2];
		uint32_t blockCount[2];
		uint32_t outSize[2];
		int32_t searchRadius;
		float lengthPenalty;
		uint32_t usePredictor;
		uint32_t encodeSRGB;
		uint32_t padding[2];
	};

	// Compute shaders cannot write sRGB textures on every GPU, so generated images are stored in the plain
	// format with the encoding done in the shader, and read back through an sRGB view.
	MTLPixelFormat StorageFormat(MTLPixelFormat format)
	{
		switch (format)
		{
		case MTLPixelFormatBGRA8Unorm_sRGB: return MTLPixelFormatBGRA8Unorm;
		case MTLPixelFormatRGBA8Unorm_sRGB: return MTLPixelFormatRGBA8Unorm;
		default: return format;
		}
	}

	const char* kGeneratorShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct GeneratorParams
{
    uint2 lowSize;
    uint2 blockCount;
    uint2 outSize;
    int searchRadius;
    float lengthPenalty;
    uint usePredictor;
    uint encodeSRGB;
    uint2 padding;
};

static float3 linear_to_srgb(float3 c)
{
    c = saturate(c);
    return select(1.055 * pow(c, 1.0 / 2.4) - 0.055, c * 12.92, c <= 0.0031308);
}

// Small luma copy of a finished frame; four taps so large reductions do not alias.
kernel void fg_luma(texture2d<float, access::sample> source [[texture(0)]],
                    texture2d<half, access::write> luma [[texture(1)]],
                    constant GeneratorParams& params [[buffer(0)]],
                    uint2 gid [[thread_position_in_grid]])
{
    if (any(gid >= params.lowSize))
        return;
    constexpr sampler linearClamp(filter::linear, address::clamp_to_edge);
    const float2 size = float2(params.lowSize);
    const float2 uv = (float2(gid) + 0.5) / size;
    const float2 o = 0.25 / size;
    const float3 c = source.sample(linearClamp, uv + float2(-o.x, -o.y)).rgb + source.sample(linearClamp, uv + float2(o.x, -o.y)).rgb
                   + source.sample(linearClamp, uv + float2(-o.x, o.y)).rgb + source.sample(linearClamp, uv + o).rgb;
    luma.write(half4(half(dot(c * 0.25, float3(0.299, 0.587, 0.114)))), gid);
}

static float block_cost(texture2d<half, access::read> target, thread const half* reference, int2 origin, int2 offset, int2 maxPos)
{
    float sum = 0.0;
    for (int y = 0; y < 8; y++)
    {
        for (int x = 0; x < 8; x++)
        {
            const uint2 p = uint2(clamp(origin + offset + int2(x, y), int2(0), maxPos));
            sum += abs(float(target.read(p).r) - float(reference[y * 8 + x]));
        }
    }
    return sum / 64.0;
}

// One vector per 8x8 block of `from`: where its content is found in `to`. Candidates are the zero vector,
// the vectors this block and its neighbours had last frame, and a full search around zero; the best one
// is refined by one pixel. A small length penalty keeps flat areas still.
kernel void fg_motion(texture2d<half, access::read> from [[texture(0)]],
                      texture2d<half, access::read> to [[texture(1)]],
                      texture2d<half, access::read> predictor [[texture(2)]],
                      texture2d<half, access::write> field [[texture(3)]],
                      constant GeneratorParams& params [[buffer(0)]],
                      uint2 block [[thread_position_in_grid]])
{
    if (any(block >= params.blockCount))
        return;
    const int2 origin = int2(block) * 8;
    const int2 maxPos = int2(params.lowSize) - 1;
    half reference[64];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            reference[y * 8 + x] = from.read(uint2(clamp(origin + int2(x, y), int2(0), maxPos))).r;

    int2 best = int2(0);
    float bestCost = block_cost(to, reference, origin, best, maxPos) * 0.95;
    float bestScore = bestCost;

    if (params.usePredictor != 0)
    {
        const int2 maxBlock = int2(params.blockCount) - 1;
        const int2 neighbours[5] = { int2(0, 0), int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
        for (int i = 0; i < 5; i++)
        {
            const int2 b = clamp(int2(block) + neighbours[i], int2(0), maxBlock);
            const int2 candidate = int2(round(float2(predictor.read(uint2(b)).xy)));
            if (all(candidate == int2(0)))
                continue;
            const float cost = block_cost(to, reference, origin, candidate, maxPos);
            const float score = cost + params.lengthPenalty * length(float2(candidate));
            if (score < bestScore)
            {
                best = candidate;
                bestCost = cost;
                bestScore = score;
            }
        }
    }

    const int r = params.searchRadius;
    for (int dy = -r; dy <= r; dy++)
    {
        for (int dx = -r; dx <= r; dx++)
        {
            const int2 candidate = int2(dx, dy);
            if (all(candidate == int2(0)))
                continue;
            const float cost = block_cost(to, reference, origin, candidate, maxPos);
            const float score = cost + params.lengthPenalty * length(float2(candidate));
            if (score < bestScore)
            {
                best = candidate;
                bestCost = cost;
                bestScore = score;
            }
        }
    }

    const int2 centre = best;
    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            const int2 candidate = centre + int2(dx, dy);
            if (all(candidate == centre))
                continue;
            const float cost = block_cost(to, reference, origin, candidate, maxPos);
            const float score = cost + params.lengthPenalty * length(float2(candidate));
            if (score < bestScore)
            {
                best = candidate;
                bestCost = cost;
                bestScore = score;
            }
        }
    }

    field.write(half4(half2(best), half(bestCost), 1.0h), block);
}

// The in-between image. Both fields give an estimate of the motion through each pixel; where they agree
// the two frames are warped half way and blended, where the warped colours disagree the newer frame wins
// (an edge or something uncovered), and where the fields disagree the pixel falls back to the newer frame.
kernel void fg_interpolate(texture2d<float, access::sample> previous [[texture(0)]],
                           texture2d<float, access::sample> current [[texture(1)]],
                           texture2d<half, access::sample> forwardField [[texture(2)]],
                           texture2d<half, access::sample> backwardField [[texture(3)]],
                           texture2d<float, access::write> output [[texture(4)]],
                           constant GeneratorParams& params [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]])
{
    if (any(gid >= params.outSize))
        return;
    constexpr sampler linearClamp(filter::linear, address::clamp_to_edge);
    const float2 uv = (float2(gid) + 0.5) / float2(params.outSize);
    const float4 f = float4(forwardField.sample(linearClamp, uv));
    const float4 b = float4(backwardField.sample(linearClamp, uv));

    const float2 motion = 0.5 * (f.xy - b.xy) / float2(params.lowSize);
    const float disagreement = length(f.xy + b.xy);
    const float fieldConfidence = saturate(1.0 - disagreement * 0.5) * saturate(1.0 - 4.0 * max(f.z, b.z));

    const float4 warpedPrevious = previous.sample(linearClamp, uv - 0.5 * motion);
    const float4 warpedCurrent = current.sample(linearClamp, uv + 0.5 * motion);
    const float colourAgreement = saturate(1.0 - 4.0 * length(warpedPrevious.rgb - warpedCurrent.rgb));
    const float4 blended = mix(warpedCurrent, 0.5 * (warpedPrevious + warpedCurrent), colourAgreement);

    const float4 fallback = current.sample(linearClamp, uv);
    float3 colour = mix(fallback, blended, fieldConfidence).rgb;
    if (params.encodeSRGB != 0)
        colour = linear_to_srgb(colour);
    output.write(float4(colour, 1.0), gid);
}

// Full-size motion for MetalFX: pixels from the current frame back to the previous one.
kernel void fg_motion_fullsize(texture2d<half, access::sample> backwardField [[texture(0)]],
                               texture2d<half, access::write> motion [[texture(1)]],
                               constant GeneratorParams& params [[buffer(0)]],
                               uint2 gid [[thread_position_in_grid]])
{
    if (any(gid >= params.outSize))
        return;
    constexpr sampler linearClamp(filter::linear, address::clamp_to_edge);
    const float2 uv = (float2(gid) + 0.5) / float2(params.outSize);
    const float2 scale = float2(params.outSize) / float2(params.lowSize);
    const float2 v = float2(backwardField.sample(linearClamp, uv).xy) * scale;
    motion.write(half4(half2(v), 0.0h, 0.0h), gid);
}
)";

	API_AVAILABLE(ios(26.0), macos(26.0))
	id<MTLComputePipelineState> MakeComputePipeline(id<MTL4Compiler> compiler, id<MTLLibrary> library, NSString* name)
	{
		MTL4LibraryFunctionDescriptor* function = [MTL4LibraryFunctionDescriptor new];
		function.library = library;
		function.name = name;
		MTL4ComputePipelineDescriptor* descriptor = [MTL4ComputePipelineDescriptor new];
		descriptor.computeFunctionDescriptor = function;
		descriptor.label = name;
		NSError* error = nil;
		id<MTLComputePipelineState> pipeline = [compiler newComputePipelineStateWithDescriptor:descriptor compilerTaskOptions:nil error:&error];
		if (!pipeline)
			NSLog(@"EspressU: frame generation pipeline %@ failed: %@", name, error);
		return pipeline;
	}

	uint64_t AvailableMemory()
	{
#ifdef ESPRESSU_HAVE_PROC_MEMORY
		return (uint64_t)os_proc_available_memory();
#else
		return UINT64_MAX;
#endif
	}
}

struct Metal4FrameGenerator::Resources
{
	id<MTLDevice> device = nil;
	id compiler = nil;     // id<MTL4Compiler>
	id residency = nil;    // id<MTLResidencySet>
	Backend backend = Backend::EspressU;

	id<MTLComputePipelineState> lumaPipeline = nil;
	id<MTLComputePipelineState> motionPipeline = nil;
	id<MTLComputePipelineState> interpolatePipeline = nil;
	id<MTLComputePipelineState> fullMotionPipeline = nil;
	id interpolator = nil; // id<MTL4FXFrameInterpolator>

	uint32_t width = 0, height = 0;
	MTLPixelFormat format = MTLPixelFormatInvalid;
	uint32_t lowWidth = 0, lowHeight = 0, blocksX = 0, blocksY = 0;

	id<MTLTexture> luma[2] = {nil, nil};
	__weak id<MTLTexture> lumaSource[2] = {nil, nil};
	id<MTLTexture> forwardField[2] = {nil, nil};
	id<MTLTexture> backwardField[2] = {nil, nil};
	uint32_t fieldIndex = 0;
	bool fieldsValid = false;
	id<MTLTexture> generated[kSlots] = {nil, nil, nil};
	id<MTLTexture> generatedView[kSlots] = {nil, nil, nil};
	id<MTLTexture> fullMotion = nil;
	id<MTLTexture> depth = nil;
	bool depthCleared = false;
	id<MTLBuffer> params[kSlots] = {nil, nil, nil};
	id tables[kSlots] = {nil, nil, nil}; // id<MTL4ArgumentTable>

	void ReleaseImages()
	{
		NSMutableArray* gone = [NSMutableArray array];
		auto drop = [&](__strong id<MTLTexture>& texture) {
			if (texture)
				[gone addObject:texture];
			texture = nil;
		};
		for (auto& t : luma) drop(t);
		for (auto& t : forwardField) drop(t);
		for (auto& t : backwardField) drop(t);
		for (auto& t : generated) drop(t);
		for (auto& t : generatedView) t = nil;
		drop(fullMotion);
		drop(depth);
		if (@available(iOS 26.0, macOS 26.0, *))
		{
			id<MTLResidencySet> set = residency;
			for (id<MTLAllocation> allocation in gone)
				[set removeAllocation:allocation];
			[set commit];
		}
		lumaSource[0] = lumaSource[1] = nil;
		fieldsValid = false;
		depthCleared = false;
		width = height = 0;
	}
};

Metal4FrameGenerator::Metal4FrameGenerator()
	: m_res(std::make_unique<Resources>())
{
}

Metal4FrameGenerator::~Metal4FrameGenerator()
{
	m_res->ReleaseImages();
	id<MTLResidencySet> set = m_res->residency;
	for (id<MTLBuffer> buffer : m_res->params)
		if (buffer)
			[set removeAllocation:buffer];
	[set commit];
}

Metal4FrameGenerator::Backend Metal4FrameGenerator::ActiveBackend() const
{
	return m_res->backend;
}

std::unique_ptr<Metal4FrameGenerator> Metal4FrameGenerator::Create(id<MTLDevice> device, id<MTL4Compiler> compiler, id<MTLResidencySet> residency,
	Backend backend, std::string& whyNot)
{
	std::unique_ptr<Metal4FrameGenerator> generator(new Metal4FrameGenerator());
	Resources& res = *generator->m_res;
	res.device = device;
	res.compiler = compiler;
	res.residency = residency;
	res.backend = backend;

	NSError* error = nil;
	MTL4LibraryDescriptor* libraryDescriptor = [MTL4LibraryDescriptor new];
	libraryDescriptor.source = [NSString stringWithUTF8String:kGeneratorShaderSource];
	libraryDescriptor.name = @"EspressU frame generation";
	id<MTLLibrary> library = [compiler newLibraryWithDescriptor:libraryDescriptor error:&error];
	if (!library)
	{
		NSLog(@"EspressU: frame generation shaders failed: %@", error);
		whyNot = "its shaders did not compile";
		return nullptr;
	}
	res.lumaPipeline = MakeComputePipeline(compiler, library, @"fg_luma");
	res.motionPipeline = MakeComputePipeline(compiler, library, @"fg_motion");
	res.interpolatePipeline = MakeComputePipeline(compiler, library, @"fg_interpolate");
	res.fullMotionPipeline = MakeComputePipeline(compiler, library, @"fg_motion_fullsize");
	if (!res.lumaPipeline || !res.motionPipeline || !res.interpolatePipeline || !res.fullMotionPipeline)
	{
		whyNot = "a compute pipeline could not be built";
		return nullptr;
	}

	MTL4ArgumentTableDescriptor* tableDescriptor = [MTL4ArgumentTableDescriptor new];
	tableDescriptor.maxTextureBindCount = 5;
	tableDescriptor.maxBufferBindCount = 1;
	for (uint32_t i = 0; i < kSlots; i++)
	{
		res.params[i] = [device newBufferWithLength:sizeof(GeneratorParams) options:MTLResourceStorageModeShared];
		res.tables[i] = [device newArgumentTableWithDescriptor:tableDescriptor error:&error];
		if (!res.params[i] || !res.tables[i])
		{
			whyNot = "its parameter buffers could not be created";
			return nullptr;
		}
		[residency addAllocation:res.params[i]];
	}
	[residency commit];

	if (backend == Backend::MetalFX && ![MTLFXFrameInterpolatorDescriptor supportsMetal4FX:device])
	{
		NSLog(@"EspressU: MetalFX frame interpolation is not supported here, using EspressU's own");
		res.backend = Backend::EspressU;
	}
	return generator;
}

id<MTLTexture> Metal4FrameGenerator::Encode(id<MTL4CommandBuffer> commandBuffer, id<MTLTexture> previous, id<MTLTexture> current,
	uint32_t slot, float frameSeconds, bool historyValid, std::string& whyDisabled)
{
	Resources& res = *m_res;
	if (!previous || !current || slot >= kSlots || previous.width != current.width || previous.height != current.height || previous.pixelFormat != current.pixelFormat)
		return nil;

	const uint32_t width = (uint32_t)current.width;
	const uint32_t height = (uint32_t)current.height;
	if (width != res.width || height != res.height || current.pixelFormat != res.format)
	{
		res.ReleaseImages();

		const uint64_t frameBytes = (uint64_t)width * height * 4;
		const uint64_t needed = frameBytes * (kSlots + (res.backend == Backend::MetalFX ? 2 : 0));
		if (AvailableMemory() < needed + kHeadroomMargin)
		{
			whyDisabled = "insufficient memory headroom for its images";
			return nil;
		}

		res.lowWidth = std::min(width, kLumaMaxWidth);
		res.lowHeight = std::max<uint32_t>(1, (uint32_t)std::lround((double)height * res.lowWidth / width));
		res.blocksX = (res.lowWidth + kBlockSize - 1) / kBlockSize;
		res.blocksY = (res.lowHeight + kBlockSize - 1) / kBlockSize;

		auto make = [&](MTLPixelFormat format, uint32_t w, uint32_t h, MTLTextureUsage usage) -> id<MTLTexture> {
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:w height:h mipmapped:NO];
			descriptor.usage = usage;
			descriptor.storageMode = MTLStorageModePrivate;
			id<MTLTexture> texture = [res.device newTextureWithDescriptor:descriptor];
			if (texture)
				[(id<MTLResidencySet>)res.residency addAllocation:texture];
			return texture;
		};
		bool ok = true;
		for (uint32_t i = 0; i < 2; i++)
		{
			res.luma[i] = make(MTLPixelFormatR16Float, res.lowWidth, res.lowHeight, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
			res.forwardField[i] = make(MTLPixelFormatRGBA16Float, res.blocksX, res.blocksY, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
			res.backwardField[i] = make(MTLPixelFormatRGBA16Float, res.blocksX, res.blocksY, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
			ok = ok && res.luma[i] && res.forwardField[i] && res.backwardField[i];
		}
		if (ok && res.backend == Backend::MetalFX)
		{
			res.fullMotion = make(MTLPixelFormatRG16Float, width, height, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
			res.depth = make(MTLPixelFormatDepth32Float, width, height, MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget);
			MTLFXFrameInterpolatorDescriptor* descriptor = [MTLFXFrameInterpolatorDescriptor new];
			descriptor.colorTextureFormat = current.pixelFormat;
			descriptor.outputTextureFormat = current.pixelFormat;
			descriptor.depthTextureFormat = MTLPixelFormatDepth32Float;
			descriptor.motionTextureFormat = MTLPixelFormatRG16Float;
			descriptor.inputWidth = width;
			descriptor.inputHeight = height;
			descriptor.outputWidth = width;
			descriptor.outputHeight = height;
			res.interpolator = (res.fullMotion && res.depth) ? [descriptor newFrameInterpolatorWithDevice:res.device compiler:res.compiler] : nil;
			if (res.interpolator)
			{
				id<MTL4FXFrameInterpolator> interpolator = res.interpolator;
				for (uint32_t i = 0; i < kSlots && ok; i++)
				{
					res.generated[i] = make(current.pixelFormat, width, height, interpolator.outputTextureUsage | MTLTextureUsageShaderRead);
					res.generatedView[i] = res.generated[i];
					ok = res.generated[i] != nil;
				}
			}
			else
			{
				NSLog(@"EspressU: MetalFX frame interpolation could not be set up for %ux%u, using EspressU's own", width, height);
				res.backend = Backend::EspressU;
			}
		}
		if (ok && res.backend == Backend::EspressU)
		{
			const MTLPixelFormat storage = StorageFormat(current.pixelFormat);
			for (uint32_t i = 0; i < kSlots && ok; i++)
			{
				res.generated[i] = make(storage, width, height, MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsagePixelFormatView);
				res.generatedView[i] = (res.generated[i] && storage != current.pixelFormat) ? [res.generated[i] newTextureViewWithPixelFormat:current.pixelFormat] : res.generated[i];
				ok = res.generatedView[i] != nil;
			}
		}
		[(id<MTLResidencySet>)res.residency commit];
		if (!ok)
		{
			MetalFaultHistory::Record(MetalFaultHistory::Kind::Texture, (uint64_t)width * height * 4 * kSlots, "frame generation images");
			res.ReleaseImages();
			whyDisabled = "its images could not be allocated";
			return nil;
		}
		res.width = width;
		res.height = height;
		res.format = current.pixelFormat;
	}

	// Luma of the previous frame is reused when it is still the one computed last time. The composition
	// textures are recycled, so identity only means anything while frames arrive back to back.
	if (!historyValid)
		res.lumaSource[0] = res.lumaSource[1] = nil;
	const uint32_t currentLuma = (res.lumaSource[0] == previous) ? 1 : 0;
	const uint32_t previousLuma = currentLuma ^ 1;
	const bool previousLumaValid = res.lumaSource[previousLuma] == previous;
	const bool usePredictor = historyValid && res.fieldsValid;

	GeneratorParams* params = (GeneratorParams*)[res.params[slot] contents];
	params->lowSize[0] = res.lowWidth;
	params->lowSize[1] = res.lowHeight;
	params->blockCount[0] = res.blocksX;
	params->blockCount[1] = res.blocksY;
	params->outSize[0] = width;
	params->outSize[1] = height;
	params->searchRadius = 6;
	params->lengthPenalty = 0.002f;
	params->usePredictor = usePredictor ? 1 : 0;
	params->encodeSRGB = StorageFormat(current.pixelFormat) != current.pixelFormat ? 1 : 0;

	id<MTL4ArgumentTable> table = res.tables[slot];
	[table setAddress:res.params[slot].gpuAddress atIndex:0];

	const MTLSize group = MTLSizeMake(8, 8, 1);
	const MTLSize lowGrid = MTLSizeMake(res.lowWidth, res.lowHeight, 1);
	const MTLSize blockGrid = MTLSizeMake(res.blocksX, res.blocksY, 1);
	const MTLSize fullGrid = MTLSizeMake(width, height, 1);

	id<MTL4ComputeCommandEncoder> encoder = [commandBuffer computeCommandEncoder];
	encoder.label = @"EspressU frame generation";
	// Earlier frames on this queue may still read the images written here.
	[encoder barrierAfterQueueStages:MTLStageDispatch | MTLStageFragment | MTLStageBlit beforeStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
	[encoder setArgumentTable:table];

	[encoder setComputePipelineState:res.lumaPipeline];
	if (!previousLumaValid)
	{
		[table setTexture:previous.gpuResourceID atIndex:0];
		[table setTexture:res.luma[previousLuma].gpuResourceID atIndex:1];
		[encoder dispatchThreads:lowGrid threadsPerThreadgroup:group];
		res.lumaSource[previousLuma] = previous;
	}
	[table setTexture:current.gpuResourceID atIndex:0];
	[table setTexture:res.luma[currentLuma].gpuResourceID atIndex:1];
	[encoder dispatchThreads:lowGrid threadsPerThreadgroup:group];
	res.lumaSource[currentLuma] = current;
	[encoder barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];

	const uint32_t writeField = res.fieldIndex ^ 1;
	[encoder setComputePipelineState:res.motionPipeline];
	[table setTexture:res.luma[previousLuma].gpuResourceID atIndex:0];
	[table setTexture:res.luma[currentLuma].gpuResourceID atIndex:1];
	[table setTexture:res.forwardField[res.fieldIndex].gpuResourceID atIndex:2];
	[table setTexture:res.forwardField[writeField].gpuResourceID atIndex:3];
	[encoder dispatchThreads:blockGrid threadsPerThreadgroup:group];
	[table setTexture:res.luma[currentLuma].gpuResourceID atIndex:0];
	[table setTexture:res.luma[previousLuma].gpuResourceID atIndex:1];
	[table setTexture:res.backwardField[res.fieldIndex].gpuResourceID atIndex:2];
	[table setTexture:res.backwardField[writeField].gpuResourceID atIndex:3];
	[encoder dispatchThreads:blockGrid threadsPerThreadgroup:group];
	[encoder barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
	res.fieldIndex = writeField;
	res.fieldsValid = true;

	id<MTLTexture> output = res.generated[slot];
	id<MTLTexture> outputView = res.generatedView[slot];
	if (res.backend == Backend::MetalFX)
	{
		[encoder setComputePipelineState:res.fullMotionPipeline];
		[table setTexture:res.backwardField[writeField].gpuResourceID atIndex:0];
		[table setTexture:res.fullMotion.gpuResourceID atIndex:1];
		[encoder dispatchThreads:fullGrid threadsPerThreadgroup:group];
		[encoder endEncoding];

		if (!res.depthCleared)
		{
			// MetalFX gets a flat depth buffer: the frames carry no depth of their own.
			MTL4RenderPassDescriptor* pass = [MTL4RenderPassDescriptor new];
			pass.depthAttachment.texture = res.depth;
			pass.depthAttachment.loadAction = MTLLoadActionClear;
			pass.depthAttachment.storeAction = MTLStoreActionStore;
			pass.depthAttachment.clearDepth = 1.0;
			pass.renderTargetWidth = width;
			pass.renderTargetHeight = height;
			id<MTL4RenderCommandEncoder> clear = [commandBuffer renderCommandEncoderWithDescriptor:pass];
			[clear endEncoding];
			res.depthCleared = true;
		}

		id<MTL4FXFrameInterpolator> interpolator = res.interpolator;
		interpolator.prevColorTexture = previous;
		interpolator.colorTexture = current;
		interpolator.motionTexture = res.fullMotion;
		interpolator.depthTexture = res.depth;
		interpolator.outputTexture = output;
		interpolator.motionVectorScaleX = 1.0f;
		interpolator.motionVectorScaleY = 1.0f;
		interpolator.deltaTime = std::max(frameSeconds, 1.0f / 240.0f);
		interpolator.nearPlane = 0.1f;
		interpolator.farPlane = 1000.0f;
		interpolator.fieldOfView = 60.0f;
		interpolator.aspectRatio = (float)width / (float)height;
		interpolator.shouldResetHistory = !historyValid;
		[interpolator encodeToCommandBuffer:commandBuffer];
		return outputView;
	}

	[encoder setComputePipelineState:res.interpolatePipeline];
	[table setTexture:previous.gpuResourceID atIndex:0];
	[table setTexture:current.gpuResourceID atIndex:1];
	[table setTexture:res.forwardField[writeField].gpuResourceID atIndex:2];
	[table setTexture:res.backwardField[writeField].gpuResourceID atIndex:3];
	[table setTexture:output.gpuResourceID atIndex:4];
	[encoder dispatchThreads:fullGrid threadsPerThreadgroup:group];
	[encoder endEncoding];
	return outputView;
}
