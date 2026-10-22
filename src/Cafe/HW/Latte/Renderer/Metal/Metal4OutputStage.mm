#include "Cafe/HW/Latte/Renderer/Metal/Metal4OutputStage.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <atomic>
#include <map>
#include <string>

#define ESPRESSU_METAL4 API_AVAILABLE(ios(26.0), macos(26.0))

namespace
{
	// Three slots per output: one being composed, one waiting for the GPU, one on screen.
	constexpr uint32_t kSlotsPerOutput = 3;
	// How long BeginFrame waits for the oldest slot before the frame takes the regular path instead.
	constexpr uint64_t kSlotWaitMs = 40;

	const char* kPresentShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct PresentVertex
{
    float4 position [[position]];
    float2 uv;
};

vertex PresentVertex espressu_present_vertex(uint vertexID [[vertex_id]])
{
    const float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    PresentVertex out;
    out.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    out.uv = uv;
    return out;
}

fragment float4 espressu_present_fragment(PresentVertex in [[stage_in]], texture2d<float> image [[texture(0)]])
{
    constexpr sampler linearClamp(filter::linear, address::clamp_to_edge);
    return image.sample(linearClamp, in.uv);
}
)";

	// Plain `id` fields keep the struct usable on iOS versions without Metal 4; every use casts inside an
	// availability-checked function.
	struct FrameSlot
	{
		id allocator = nil;      // id<MTL4CommandAllocator>
		id commandBuffer = nil;  // id<MTL4CommandBuffer>
		id arguments = nil;      // id<MTL4ArgumentTable>
		id<MTLTexture> composition = nil;
		uint64_t doneValue = 0;  // value of the stage's output event once the GPU is finished with this slot
	};

	struct OutputSlots
	{
		FrameSlot slots[kSlotsPerOutput];
		uint32_t next = 0;
		int32_t current = -1;
		id attachedLayerSet = nil; // id<MTLResidencySet> of the CAMetalLayer this output presents to
		__weak CAMetalLayer* attachedLayer = nil;
	};
}

struct Metal4OutputStage::State
{
	id<MTLDevice> device = nil;
	id queue = nil;         // id<MTL4CommandQueue>
	id compiler = nil;      // id<MTL4Compiler>
	id<MTLLibrary> library = nil;
	id residency = nil;     // id<MTLResidencySet> with every composition texture
	std::map<MTLPixelFormat, id<MTLRenderPipelineState>> pipelines;
	dispatch_queue_t feedbackQueue = nil;

	// Regular queue -> Metal 4 queue: the composition of a frame is complete.
	id<MTLSharedEvent> interopEvent = nil;
	uint64_t interopValue = 0;
	// Metal 4 queue -> CPU: a frame slot may be reused.
	id<MTLSharedEvent> outputEvent = nil;
	uint64_t outputValue = 0;

	OutputSlots outputs[OutputCount];
	std::atomic<double> lastGpuMs{0.0};
	uint32_t dropLogs = 0;
};

namespace
{
	ESPRESSU_METAL4 id<MTLRenderPipelineState> PipelineFor(Metal4OutputStage::State& state, MTLPixelFormat format)
	{
		auto it = state.pipelines.find(format);
		if (it != state.pipelines.end())
			return it->second;

		MTL4LibraryFunctionDescriptor* vertexFunction = [MTL4LibraryFunctionDescriptor new];
		vertexFunction.library = state.library;
		vertexFunction.name = @"espressu_present_vertex";
		MTL4LibraryFunctionDescriptor* fragmentFunction = [MTL4LibraryFunctionDescriptor new];
		fragmentFunction.library = state.library;
		fragmentFunction.name = @"espressu_present_fragment";

		MTL4RenderPipelineDescriptor* descriptor = [MTL4RenderPipelineDescriptor new];
		descriptor.label = @"EspressU Metal 4 present";
		descriptor.vertexFunctionDescriptor = vertexFunction;
		descriptor.fragmentFunctionDescriptor = fragmentFunction;
		descriptor.colorAttachments[0].pixelFormat = format;

		NSError* error = nil;
		id<MTL4Compiler> compiler = state.compiler;
		id<MTLRenderPipelineState> pipeline = [compiler newRenderPipelineStateWithDescriptor:descriptor compilerTaskOptions:nil error:&error];
		if (!pipeline)
			NSLog(@"EspressU: Metal 4 present pipeline for format %lu failed: %@", (unsigned long)format, error);
		state.pipelines[format] = pipeline;
		return pipeline;
	}

	ESPRESSU_METAL4 bool SetUp(Metal4OutputStage::State& state)
	{
		id<MTLDevice> device = state.device;

		MTL4CommandQueueDescriptor* queueDescriptor = [MTL4CommandQueueDescriptor new];
		queueDescriptor.label = @"EspressU output";
		state.feedbackQueue = dispatch_queue_create("EspressU Metal 4 feedback", DISPATCH_QUEUE_SERIAL);
		queueDescriptor.feedbackQueue = state.feedbackQueue;
		id<MTL4CommandQueue> queue = [device newMTL4CommandQueueWithDescriptor:queueDescriptor error:nil];
		if (!queue)
			return false;
		state.queue = queue;

		NSError* error = nil;
		MTL4CompilerDescriptor* compilerDescriptor = [MTL4CompilerDescriptor new];
		compilerDescriptor.label = @"EspressU output";
		id<MTL4Compiler> compiler = [device newCompilerWithDescriptor:compilerDescriptor error:&error];
		if (!compiler)
			return false;
		state.compiler = compiler;

		MTL4LibraryDescriptor* libraryDescriptor = [MTL4LibraryDescriptor new];
		libraryDescriptor.source = [NSString stringWithUTF8String:kPresentShaderSource];
		libraryDescriptor.name = @"EspressU present";
		state.library = [compiler newLibraryWithDescriptor:libraryDescriptor error:&error];
		if (!state.library)
		{
			NSLog(@"EspressU: Metal 4 present shaders failed: %@", error);
			return false;
		}
		// Build the common formats now so a failure turns the stage off before the first frame.
		if (!PipelineFor(state, MTLPixelFormatBGRA8Unorm) || !PipelineFor(state, MTLPixelFormatBGRA8Unorm_sRGB))
			return false;

		MTLResidencySetDescriptor* residencyDescriptor = [MTLResidencySetDescriptor new];
		residencyDescriptor.label = @"EspressU output";
		residencyDescriptor.initialCapacity = Metal4OutputStage::OutputCount * kSlotsPerOutput;
		id<MTLResidencySet> residency = [device newResidencySetWithDescriptor:residencyDescriptor error:&error];
		if (!residency)
			return false;
		state.residency = residency;
		[queue addResidencySet:residency];

		state.interopEvent = [device newSharedEvent];
		state.outputEvent = [device newSharedEvent];
		if (!state.interopEvent || !state.outputEvent)
			return false;

		MTL4ArgumentTableDescriptor* tableDescriptor = [MTL4ArgumentTableDescriptor new];
		tableDescriptor.maxTextureBindCount = 1;
		for (OutputSlots& output : state.outputs)
		{
			for (FrameSlot& slot : output.slots)
			{
				id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
				id<MTL4CommandBuffer> commandBuffer = [device newCommandBuffer];
				id<MTL4ArgumentTable> arguments = [device newArgumentTableWithDescriptor:tableDescriptor error:&error];
				if (!allocator || !commandBuffer || !arguments)
					return false;
				slot.allocator = allocator;
				slot.commandBuffer = commandBuffer;
				slot.arguments = arguments;
			}
		}
		return true;
	}

	ESPRESSU_METAL4 void AttachLayer(Metal4OutputStage::State& state, OutputSlots& output, CAMetalLayer* layer)
	{
		// The layer hands out a new residency set whenever its device changes.
		if (output.attachedLayer == layer && output.attachedLayerSet == layer.residencySet)
			return;
		id<MTL4CommandQueue> queue = state.queue;
		if (output.attachedLayerSet)
			[queue removeResidencySet:output.attachedLayerSet];
		output.attachedLayer = layer;
		output.attachedLayerSet = layer.residencySet;
		if (output.attachedLayerSet)
			[queue addResidencySet:output.attachedLayerSet];
	}

	ESPRESSU_METAL4 bool Present(Metal4OutputStage::State& state, Metal4OutputStage::Output outputIndex, id<MTLCommandBuffer> legacy, CAMetalLayer* layer, double minimumDuration)
	{
		OutputSlots& output = state.outputs[outputIndex];
		if (output.current < 0)
			return false;
		FrameSlot& slot = output.slots[output.current];
		output.current = -1;

		// The regular queue tells the Metal 4 queue when the composition is done. If its command buffer fails,
		// the signal is given from the CPU so the Metal 4 queue never waits forever.
		const uint64_t composed = ++state.interopValue;
		id<MTLSharedEvent> interop = state.interopEvent;
		[legacy encodeSignalEvent:interop value:composed];
		[legacy addCompletedHandler:^(id<MTLCommandBuffer> finished) {
			if (finished.status == MTLCommandBufferStatusError && interop.signaledValue < composed)
				interop.signaledValue = composed;
		}];

		AttachLayer(state, output, layer);
		id<CAMetalDrawable> drawable = [layer nextDrawable];
		id<MTLRenderPipelineState> pipeline = drawable ? PipelineFor(state, drawable.texture.pixelFormat) : nil;
		if (!drawable || !pipeline)
		{
			if (state.dropLogs++ < 8)
				NSLog(@"EspressU: Metal 4 output dropped a frame (%s)", drawable ? "no pipeline for the drawable format" : "no drawable");
			return false;
		}

		id<MTL4CommandAllocator> allocator = slot.allocator;
		id<MTL4CommandBuffer> commandBuffer = slot.commandBuffer;
		id<MTL4ArgumentTable> arguments = slot.arguments;
		[allocator reset];
		[commandBuffer beginCommandBufferWithAllocator:allocator];
		commandBuffer.label = outputIndex == Metal4OutputStage::TV ? @"EspressU TV present" : @"EspressU GamePad present";

		MTL4RenderPassDescriptor* pass = [MTL4RenderPassDescriptor new];
		pass.colorAttachments[0].texture = drawable.texture;
		pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		id<MTL4RenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
		[arguments setTexture:slot.composition.gpuResourceID atIndex:0];
		[encoder setRenderPipelineState:pipeline];
		[encoder setArgumentTable:arguments atStages:MTLRenderStageFragment];
		[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[encoder endEncoding];
		[commandBuffer endCommandBuffer];

		id<MTL4CommandQueue> queue = state.queue;
		[queue waitForEvent:interop value:composed];
		[queue waitForDrawable:drawable];

		MTL4CommitOptions* options = [MTL4CommitOptions new];
		std::atomic<double>* lastGpuMs = &state.lastGpuMs;
		[options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
			if (!feedback.error && feedback.GPUEndTime > feedback.GPUStartTime)
				lastGpuMs->store((feedback.GPUEndTime - feedback.GPUStartTime) * 1000.0, std::memory_order_relaxed);
		}];
		id<MTL4CommandBuffer> buffers[1] = {commandBuffer};
		[queue commit:buffers count:1 options:options];
		[queue signalDrawable:drawable];

		slot.doneValue = ++state.outputValue;
		[queue signalEvent:state.outputEvent value:slot.doneValue];

		if (minimumDuration > 0.0)
			[drawable presentAfterMinimumDuration:minimumDuration];
		else
			[drawable present];
		return true;
	}
}

Metal4OutputStage::Metal4OutputStage()
	: m_state(std::make_unique<State>())
{
}

Metal4OutputStage::~Metal4OutputStage()
{
	if (!m_state->queue)
		return;
	WaitIdle(1000);
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		id<MTL4CommandQueue> queue = m_state->queue;
		for (OutputSlots& output : m_state->outputs)
		{
			if (output.attachedLayerSet)
				[queue removeResidencySet:output.attachedLayerSet];
		}
		if (m_state->residency)
			[queue removeResidencySet:m_state->residency];
	}
}

bool Metal4OutputStage::IsSupported(MTL::Device* device)
{
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		id<MTLDevice> mtlDevice = (__bridge id<MTLDevice>)device;
		return mtlDevice && [mtlDevice supportsFamily:MTLGPUFamilyMetal4];
	}
	return false;
}

std::unique_ptr<Metal4OutputStage> Metal4OutputStage::Create(MTL::Device* device)
{
	if (!IsSupported(device))
		return nullptr;
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		std::unique_ptr<Metal4OutputStage> stage(new Metal4OutputStage());
		stage->m_state->device = (__bridge id<MTLDevice>)device;
		if (SetUp(*stage->m_state))
			return stage;
		stage->m_state->queue = nil;
	}
	return nullptr;
}

MTL::Texture* Metal4OutputStage::BeginFrame(Output outputIndex, uint32_t width, uint32_t height, uint32_t pixelFormat)
{
	if (outputIndex >= OutputCount || width == 0 || height == 0)
		return nullptr;
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		State& state = *m_state;
		OutputSlots& output = state.outputs[outputIndex];
		if (output.current >= 0)
			return (__bridge MTL::Texture*)output.slots[output.current].composition;

		FrameSlot& slot = output.slots[output.next];
		if (slot.doneValue && ![state.outputEvent waitUntilSignaledValue:slot.doneValue timeoutMS:kSlotWaitMs])
			return nullptr;

		const MTLPixelFormat format = (MTLPixelFormat)pixelFormat;
		if (!slot.composition || slot.composition.width != width || slot.composition.height != height || slot.composition.pixelFormat != format)
		{
			id<MTLResidencySet> residency = state.residency;
			if (slot.composition)
				[residency removeAllocation:slot.composition];
			MTLTextureDescriptor* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
			descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
			descriptor.storageMode = MTLStorageModePrivate;
			slot.composition = [state.device newTextureWithDescriptor:descriptor];
			if (slot.composition)
				[residency addAllocation:slot.composition];
			[residency commit];
			if (!slot.composition)
			{
				MetalFaultHistory::Record(MetalFaultHistory::Kind::Texture, (uint64_t)width * height * 4,
					std::string("Metal 4 output composition for the ") + (outputIndex == TV ? "TV" : "GamePad"));
				return nullptr;
			}
		}

		output.current = (int32_t)output.next;
		output.next = (output.next + 1) % kSlotsPerOutput;
		return (__bridge MTL::Texture*)slot.composition;
	}
	return nullptr;
}

bool Metal4OutputStage::PresentFrame(Output output, MTL::CommandBuffer* legacyCommandBuffer, CA::MetalLayer* layer, double minimumDuration)
{
	if (output >= OutputCount || !legacyCommandBuffer || !layer)
		return false;
	if (@available(iOS 26.0, macOS 26.0, *))
		return Present(*m_state, output, (__bridge id<MTLCommandBuffer>)legacyCommandBuffer, (__bridge CAMetalLayer*)layer, minimumDuration);
	return false;
}

double Metal4OutputStage::LastGpuTimeMilliseconds() const
{
	return m_state->lastGpuMs.load(std::memory_order_relaxed);
}

bool Metal4OutputStage::WaitIdle(uint32_t timeoutMs)
{
	if (!m_state->outputEvent || m_state->outputValue == 0)
		return true;
	return [m_state->outputEvent waitUntilSignaledValue:m_state->outputValue timeoutMS:timeoutMs];
}
