#include "Cafe/HW/Latte/Renderer/Metal/Metal4OutputStage.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"
#include "Cafe/HW/Latte/Renderer/Metal/Metal4FrameGenerator.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <memory>
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
		id secondCommandBuffer = nil; // id<MTL4CommandBuffer>, for the real frame after a generated one
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
		const void* attachedLayer = nullptr; // identity only

		// Frame pacing of the real frames, for frame generation.
		int32_t lastPresented = -1;
		double lastPresentTime = 0.0;
		double smoothedInterval = 0.0;
		bool lastWentThroughGenerator = false;
		// Frames left before generation may run again after its drawables made the emulator thread wait.
		uint32_t generationCooldown = 0;
	};

	// Game frames slower than this get a generated frame in between (below 40 fps).
	constexpr double kGenerateAboveInterval = 1.0 / 40.0;
	// A gap this long (pause, loading) starts the frame history over.
	constexpr double kHistoryBreakInterval = 0.25;
	// Longest spacing between the two presents of a real frame. Pacing by the measured interval alone feeds
	// back on itself: a late frame stretches the spacing, which holds the drawables longer, which makes the next
	// frame later still. Capping at one 60 Hz refresh breaks that loop.
	constexpr double kMaxPresentSpacing = 1.0 / 60.0;
	// Waiting this long for the two drawables of a generated frame pauses generation for a while.
	constexpr double kDrawableWaitLimit = 0.006;
	constexpr uint32_t kGenerationCooldownFrames = 120;
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
	// Written by commit feedback handlers, which can still run after the stage is gone, so they share ownership.
	struct GpuTimings
	{
		std::atomic<double> lastGpuMs{0.0};
		// GPU time of the last command buffer that generated a frame; decides whether there is room for the next one.
		std::atomic<double> generationGpuMs{0.0};
	};
	std::shared_ptr<GpuTimings> timings = std::make_shared<GpuTimings>();
	uint32_t dropLogs = 0;

	// Metal4FrameGenerator*, owned; untyped so the struct stays usable before iOS 26.
	void* generator = nullptr;
	uint8_t generatorMode = 0;
	// The mode that failed for good, so it is not set up again every frame until the setting changes.
	uint8_t refusedGeneratorMode = 0;
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
				id<MTL4CommandBuffer> secondCommandBuffer = [device newCommandBuffer];
				id<MTL4ArgumentTable> arguments = [device newArgumentTableWithDescriptor:tableDescriptor error:&error];
				if (!allocator || !commandBuffer || !secondCommandBuffer || !arguments)
					return false;
				slot.allocator = allocator;
				slot.commandBuffer = commandBuffer;
				slot.secondCommandBuffer = secondCommandBuffer;
				slot.arguments = arguments;
			}
		}
		return true;
	}

	ESPRESSU_METAL4 void AttachLayer(Metal4OutputStage::State& state, OutputSlots& output, CAMetalLayer* layer)
	{
		// The layer hands out a new residency set whenever its device changes.
		if (output.attachedLayer == (__bridge const void*)layer && output.attachedLayerSet == layer.residencySet)
			return;
		id<MTL4CommandQueue> queue = state.queue;
		if (output.attachedLayerSet)
			[queue removeResidencySet:output.attachedLayerSet];
		output.attachedLayer = (__bridge const void*)layer;
		output.attachedLayerSet = layer.residencySet;
		if (output.attachedLayerSet)
			[queue addResidencySet:output.attachedLayerSet];
	}

	ESPRESSU_METAL4 Metal4FrameGenerator* Generator(Metal4OutputStage::State& state)
	{
		return static_cast<Metal4FrameGenerator*>(state.generator);
	}

	ESPRESSU_METAL4 void DropGenerator(Metal4OutputStage::State& state)
	{
		if (!state.generator)
			return;
		// Metal 4 command buffers do not keep their resources alive, so wait for the queue first.
		if (state.outputValue)
			[state.outputEvent waitUntilSignaledValue:state.outputValue timeoutMS:500];
		delete Generator(state);
		state.generator = nullptr;
		state.generatorMode = 0;
	}

	// Brings the frame generator in line with the setting; returns it when it should run.
	ESPRESSU_METAL4 Metal4FrameGenerator* UpdateGenerator(Metal4OutputStage::State& state)
	{
		const uint8_t mode = HostTuning::Get().frameGeneration.load(std::memory_order_relaxed);
		if (mode != state.refusedGeneratorMode)
			state.refusedGeneratorMode = 0;
		auto& stats = HostTuning::GetStats();
		stats.fgDisabled.store(mode != 0 && mode == state.refusedGeneratorMode, std::memory_order_relaxed);
		if (mode == 0 || mode == state.refusedGeneratorMode)
		{
			DropGenerator(state);
			stats.fgBackend.store(0, std::memory_order_relaxed);
			return nullptr;
		}
		if (state.generator && state.generatorMode == mode)
			return Generator(state);
		DropGenerator(state);

		std::string whyNot;
		const auto backend = mode == 2 ? Metal4FrameGenerator::Backend::MetalFX : Metal4FrameGenerator::Backend::EspressU;
		auto generator = Metal4FrameGenerator::Create(state.device, state.compiler, state.residency, backend, whyNot);
		if (!generator)
		{
			NSLog(@"EspressU: frame generation is off because %s", whyNot.c_str());
			state.refusedGeneratorMode = mode;
			return nullptr;
		}
		NSLog(@"EspressU: frame generation on (%s)", generator->ActiveBackend() == Metal4FrameGenerator::Backend::MetalFX ? "MetalFX" : "EspressU");
		stats.fgBackend.store(generator->ActiveBackend() == Metal4FrameGenerator::Backend::MetalFX ? 2 : 1, std::memory_order_relaxed);
		state.generator = generator.release();
		state.generatorMode = mode;
		state.timings->generationGpuMs.store(0.0, std::memory_order_relaxed);
		return Generator(state);
	}

	ESPRESSU_METAL4 void EncodeBlit(id<MTL4CommandBuffer> commandBuffer, id<MTL4ArgumentTable> arguments, id<MTLRenderPipelineState> pipeline,
		id<MTLTexture> source, id<MTLTexture> target, bool afterCompute)
	{
		MTL4RenderPassDescriptor* pass = [MTL4RenderPassDescriptor new];
		pass.colorAttachments[0].texture = target;
		pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		id<MTL4RenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
		if (afterCompute)
			[encoder barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageFragment visibilityOptions:MTL4VisibilityOptionDevice];
		[arguments setTexture:source.gpuResourceID atIndex:0];
		[encoder setRenderPipelineState:pipeline];
		[encoder setArgumentTable:arguments atStages:MTLRenderStageFragment];
		[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[encoder endEncoding];
	}

	ESPRESSU_METAL4 MTL4CommitOptions* FeedbackOptions(const std::shared_ptr<Metal4OutputStage::State::GpuTimings>& timings, bool generation)
	{
		MTL4CommitOptions* options = [MTL4CommitOptions new];
		std::shared_ptr<Metal4OutputStage::State::GpuTimings> keep = timings;
		[options addFeedbackHandler:^(id<MTL4CommitFeedback> feedback) {
			if (!feedback.error && feedback.GPUEndTime > feedback.GPUStartTime)
				(generation ? keep->generationGpuMs : keep->lastGpuMs).store((feedback.GPUEndTime - feedback.GPUStartTime) * 1000.0, std::memory_order_relaxed);
		}];
		return options;
	}

	ESPRESSU_METAL4 bool Present(Metal4OutputStage::State& state, Metal4OutputStage::Output outputIndex, id<MTLCommandBuffer> legacy, CAMetalLayer* layer, double minimumDuration)
	{
		OutputSlots& output = state.outputs[outputIndex];
		if (output.current < 0)
			return false;
		const int32_t slotIndex = output.current;
		FrameSlot& slot = output.slots[slotIndex];
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

		// Pacing of the real frames: a smoothed interval, started over after a long gap.
		const double now = CACurrentMediaTime();
		const double interval = output.lastPresentTime > 0.0 ? now - output.lastPresentTime : 0.0;
		output.lastPresentTime = now;
		const bool historyBreak = interval <= 0.0 || interval > kHistoryBreakInterval;
		if (historyBreak)
			output.smoothedInterval = 0.0;
		else
			output.smoothedInterval = output.smoothedInterval > 0.0 ? output.smoothedInterval * 0.9 + interval * 0.1 : interval;
		const double frameInterval = minimumDuration > 0.0 ? minimumDuration : output.smoothedInterval;

		const int32_t previousIndex = (slotIndex + kSlotsPerOutput - 1) % kSlotsPerOutput;
		const bool previousIsLastFrame = output.lastPresented == previousIndex;
		output.lastPresented = -1;

		Metal4FrameGenerator* generator = outputIndex == Metal4OutputStage::TV ? UpdateGenerator(state) : nullptr;
		// Present credit: at most two frames waiting on the output queue, and enough GPU time left for the extra one.
		const bool queueKeepsUp = state.outputValue <= state.outputEvent.signaledValue + 2;
		const double generationMs = state.timings->generationGpuMs.load(std::memory_order_relaxed);
		const bool gpuKeepsUp = generationMs < frameInterval * 1000.0 * 0.4;
		if (!gpuKeepsUp)
			state.timings->generationGpuMs.store(generationMs * 0.95, std::memory_order_relaxed); // try again after a while
		if (output.generationCooldown > 0)
			output.generationCooldown--;
		bool generate = generator && !historyBreak && previousIsLastFrame && frameInterval > kGenerateAboveInterval && queueKeepsUp && gpuKeepsUp
			&& output.generationCooldown == 0;
		if (generator)
		{
			// Why a frame got no generated partner, first reason only.
			auto& stats = HostTuning::GetStats();
			stats.fgIntervalMicros.store((uint64_t)(frameInterval * 1e6), std::memory_order_relaxed);
			stats.fgGpuMicros.store((uint64_t)(generationMs * 1000.0), std::memory_order_relaxed);
			if (historyBreak || !previousIsLastFrame)
				HostTuning::Count(stats.fgSkippedHistory);
			else if (frameInterval <= kGenerateAboveInterval)
				HostTuning::Count(stats.fgSkippedFastEnough);
			else if (output.generationCooldown > 0)
				HostTuning::Count(stats.fgSkippedCooldown);
			else if (!queueKeepsUp)
				HostTuning::Count(stats.fgSkippedQueue);
			else if (!gpuKeepsUp)
				HostTuning::Count(stats.fgSkippedGpu);
		}

		AttachLayer(state, output, layer);
		const double acquireStart = CACurrentMediaTime();
		id<CAMetalDrawable> generatedDrawable = generate ? [layer nextDrawable] : nil;
		id<CAMetalDrawable> drawable = [layer nextDrawable];
		if (generate && CACurrentMediaTime() - acquireStart > kDrawableWaitLimit)
		{
			// The display still holds the last two presents; generating more would only slow the game down.
			output.generationCooldown = kGenerationCooldownFrames;
			HostTuning::Count(HostTuning::GetStats().fgCooldowns);
			if (state.dropLogs++ < 8)
				NSLog(@"EspressU: frame generation paused, the drawables made the game wait %.1f ms", (CACurrentMediaTime() - acquireStart) * 1000.0);
		}
		if (!drawable && generatedDrawable)
		{
			drawable = generatedDrawable;
			generatedDrawable = nil;
		}
		generate = generate && generatedDrawable;
		id<MTLRenderPipelineState> pipeline = drawable ? PipelineFor(state, drawable.texture.pixelFormat) : nil;
		if (!drawable || !pipeline)
		{
			if (state.dropLogs++ < 8)
				NSLog(@"EspressU: Metal 4 output dropped a frame (%s)", drawable ? "no pipeline for the drawable format" : "no drawable");
			output.lastWentThroughGenerator = false;
			HostTuning::Count(HostTuning::GetStats().m4Dropped);
			return false;
		}

		id<MTL4CommandAllocator> allocator = slot.allocator;
		id<MTL4ArgumentTable> arguments = slot.arguments;
		id<MTL4CommandQueue> queue = state.queue;
		[allocator reset];

		id<MTL4CommandBuffer> generatedCommands = nil;
		if (generate)
		{
			generatedCommands = slot.commandBuffer;
			[generatedCommands beginCommandBufferWithAllocator:allocator];
			generatedCommands.label = @"EspressU generated frame";
			std::string whyDisabled;
			id<MTLTexture> generatedImage = generator->Encode(generatedCommands, output.slots[previousIndex].composition, slot.composition, (uint32_t)slotIndex,
				(float)frameInterval, output.lastWentThroughGenerator, whyDisabled);
			if (generatedImage)
				EncodeBlit(generatedCommands, arguments, pipeline, generatedImage, generatedDrawable.texture, true);
			[generatedCommands endCommandBuffer];
			if (!whyDisabled.empty())
			{
				NSLog(@"EspressU: frame generation is off because of %s", whyDisabled.c_str());
				state.refusedGeneratorMode = state.generatorMode;
			}
			HostTuning::Count(generatedImage ? HostTuning::GetStats().fgGenerated : HostTuning::GetStats().fgEncodeFailed);
			if (!generatedImage)
			{
				// Nothing was drawn into the drawable; it is still presented (as the real frame) below.
				generate = false;
			}
		}

		id<MTL4CommandBuffer> commandBuffer = generate ? slot.secondCommandBuffer : slot.commandBuffer;
		if (!generate && generatedCommands)
		{
			// The command buffer started above holds only compute work that fed nothing; it still runs so the
			// generator's history stays consistent, and the real frame follows in the second buffer.
			commandBuffer = slot.secondCommandBuffer;
		}
		[commandBuffer beginCommandBufferWithAllocator:allocator];
		commandBuffer.label = outputIndex == Metal4OutputStage::TV ? @"EspressU TV present" : @"EspressU GamePad present";
		id<CAMetalDrawable> realDrawable = generate ? drawable : (generatedDrawable ? generatedDrawable : drawable);
		EncodeBlit(commandBuffer, arguments, pipeline, slot.composition, realDrawable.texture, generatedCommands != nil);
		[commandBuffer endCommandBuffer];

		[queue waitForEvent:interop value:composed];
		if (generate)
			[queue waitForDrawable:generatedDrawable];
		[queue waitForDrawable:realDrawable];
		if (generatedCommands)
		{
			id<MTL4CommandBuffer> first[1] = {generatedCommands};
			[queue commit:first count:1 options:FeedbackOptions(state.timings, true)];
			if (generate)
				[queue signalDrawable:generatedDrawable];
		}
		id<MTL4CommandBuffer> second[1] = {commandBuffer};
		[queue commit:second count:1 options:FeedbackOptions(state.timings, false)];
		[queue signalDrawable:realDrawable];

		slot.doneValue = ++state.outputValue;
		if (generatedCommands)
			output.slots[previousIndex].doneValue = slot.doneValue; // the generator read the previous composition too
		[queue signalEvent:state.outputEvent value:slot.doneValue];

		if (generate)
		{
			// Two presents for one real frame, half an interval apart: the display paces them, not the CPU.
			const double half = std::min(frameInterval * 0.5, kMaxPresentSpacing);
			[generatedDrawable presentAfterMinimumDuration:half];
			[realDrawable presentAfterMinimumDuration:half];
		}
		else if (minimumDuration > 0.0)
			[realDrawable presentAfterMinimumDuration:minimumDuration];
		else
			[realDrawable present];
		// A drawable taken for a generated frame that did not happen is simply not used.

		output.lastPresented = slotIndex;
		output.lastWentThroughGenerator = generatedCommands != nil;
		HostTuning::Count(HostTuning::GetStats().m4Frames);
		HostTuning::GetStats().m4GpuMicros.store((uint64_t)(state.timings->lastGpuMs.load(std::memory_order_relaxed) * 1000.0), std::memory_order_relaxed);
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
	// Work still waiting for a composition that will never be signalled (the game stopped mid-frame) is let go,
	// so the queue can drain before it is released.
	if (m_state->interopEvent && m_state->interopEvent.signaledValue < m_state->interopValue)
		m_state->interopEvent.signaledValue = m_state->interopValue;
	WaitIdle(1000);
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		DropGenerator(*m_state);
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
		{
			HostTuning::Count(HostTuning::GetStats().m4SlotTimeouts);
			return nullptr;
		}

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
	return m_state->timings->lastGpuMs.load(std::memory_order_relaxed);
}

bool Metal4OutputStage::WaitIdle(uint32_t timeoutMs)
{
	if (!m_state->outputEvent || m_state->outputValue == 0)
		return true;
	return [m_state->outputEvent waitUntilSignaledValue:m_state->outputValue timeoutMS:timeoutMs];
}
