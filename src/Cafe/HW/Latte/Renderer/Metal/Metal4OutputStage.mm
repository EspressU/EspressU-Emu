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
#include <mutex>
#include <string>

#define ESPRESSU_METAL4 API_AVAILABLE(ios(26.0), macos(26.0))

namespace
{
	// Three slots per output: one being composed by the game, one waiting for the presenter, one on screen.
	constexpr uint32_t kSlotsPerOutput = 3;
	// How long BeginFrame waits for a free slot's GPU work before the frame takes the regular path instead.
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

	enum class SlotUse : uint8_t
	{
		Free,      // may be composed into once its GPU work is done
		Composing, // the game is drawing into it
		Queued,    // composed, waiting for the presenter
		Presenting,// the presenter is putting it on screen
		Shown,     // the last frame on screen; kept as history for frame generation
	};

	// Plain `id` fields keep the struct usable on iOS versions without Metal 4; every use casts inside an
	// availability-checked function.
	struct FrameSlot
	{
		id allocator = nil;      // id<MTL4CommandAllocator>
		id commandBuffer = nil;  // id<MTL4CommandBuffer>
		id secondCommandBuffer = nil; // id<MTL4CommandBuffer>, for the real frame after a generated one
		id arguments = nil;      // id<MTL4ArgumentTable>
		id<MTLTexture> composition = nil;
		SlotUse use = SlotUse::Free;
		uint64_t doneValue = 0;  // value of the output's event once the GPU is finished with this slot
		uint64_t sequence = 0;   // number of the game frame it holds
	};

	// A composed frame handed from the game's thread to the presenter.
	struct PendingFrame
	{
		int32_t slot = -1;
		uint64_t composedValue = 0;
		uint64_t sequence = 0;
		double producedAt = 0.0;
		double minimumDuration = 0.0;
		CAMetalLayer* layer = nil;
	};

	struct OutputSlots
	{
		FrameSlot slots[kSlotsPerOutput];
		int32_t current = -1;
		uint64_t nextSequence = 1;
		PendingFrame pending;

		// Metal 4 queue -> CPU: a slot of this output may be reused. One event per output, since each output
		// presents from its own thread and the values must rise in the order they are signalled.
		id<MTLSharedEvent> doneEvent = nil;
		uint64_t doneValue = 0;
		dispatch_queue_t presenter = nil;

		// Presenter-thread state.
		id attachedLayerSet = nil; // id<MTLResidencySet> of the CAMetalLayer this output presents to
		const void* attachedLayer = nullptr; // identity only
		int32_t shownSlot = -1;
		uint64_t shownSequence = 0;
		double lastProducedAt = 0.0;
		double smoothedInterval = 0.0;
		bool lastWentThroughGenerator = false;
	};

	// Game frames slower than this get a generated frame in between (32 fps and below), so the two presents per
	// frame still fit a 60 Hz display.
	constexpr double kGenerateAboveInterval = 1.0 / 32.0;
	// A gap this long (pause, loading) starts the frame history over.
	constexpr double kHistoryBreakInterval = 0.25;
	// Longest spacing between the two presents of a real frame.
	constexpr double kMaxPresentSpacing = 1.0 / 60.0;
}

struct Metal4OutputStage::State
{
	id<MTLDevice> device = nil;
	id queue = nil;         // id<MTL4CommandQueue>
	id compiler = nil;      // id<MTL4Compiler>
	id<MTLLibrary> library = nil;
	id residency = nil;     // id<MTLResidencySet> with every composition and frame generation image
	std::map<MTLPixelFormat, id<MTLRenderPipelineState>> pipelines;
	dispatch_queue_t feedbackQueue = nil;

	// Regular queue -> Metal 4 queue: the composition of a frame is complete.
	id<MTLSharedEvent> interopEvent = nil;
	uint64_t interopValue = 0;

	OutputSlots outputs[OutputCount];

	// Slot bookkeeping and the hand-over between the game's thread and the presenters.
	std::mutex slotLock;
	// Changes to the residency set and the pipeline map, which both threads touch.
	std::mutex residencyLock;
	std::mutex pipelineLock;
	// The submission sequence on the shared Metal 4 queue (waits, commit, signals) of one output at a time.
	std::mutex submitLock;

	// Written by commit feedback handlers, which can still run after the stage is gone, so they share ownership.
	struct GpuTimings
	{
		std::atomic<double> lastGpuMs{0.0};
		// GPU time of the last command buffer that generated a frame; decides whether there is room for the next one.
		std::atomic<double> generationGpuMs{0.0};
	};
	std::shared_ptr<GpuTimings> timings = std::make_shared<GpuTimings>();
	std::atomic<uint32_t> dropLogs{0};

	// Metal4FrameGenerator*, owned and used only on the TV presenter; untyped so the struct stays usable before iOS 26.
	void* generator = nullptr;
	uint8_t generatorMode = 0;
	// The mode that failed for good, so it is not set up again every frame until the setting changes.
	uint8_t refusedGeneratorMode = 0;
};

namespace
{
	ESPRESSU_METAL4 id<MTLRenderPipelineState> PipelineFor(Metal4OutputStage::State& state, MTLPixelFormat format)
	{
		std::lock_guard lock(state.pipelineLock);
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
		if (!state.interopEvent)
			return false;

		MTL4ArgumentTableDescriptor* tableDescriptor = [MTL4ArgumentTableDescriptor new];
		tableDescriptor.maxTextureBindCount = 1;
		for (uint32_t index = 0; index < Metal4OutputStage::OutputCount; index++)
		{
			OutputSlots& output = state.outputs[index];
			output.doneEvent = [device newSharedEvent];
			// Drawable waits block this thread, never the game's.
			dispatch_queue_attr_t attributes = dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INTERACTIVE, 0);
			output.presenter = dispatch_queue_create(index == Metal4OutputStage::TV ? "EspressU TV presenter" : "EspressU GamePad presenter", attributes);
			if (!output.doneEvent || !output.presenter)
				return false;
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
		// Metal 4 command buffers do not keep their resources alive, so wait for the TV output's GPU work first.
		OutputSlots& tv = state.outputs[Metal4OutputStage::TV];
		if (tv.doneValue)
			[tv.doneEvent waitUntilSignaledValue:tv.doneValue timeoutMS:500];
		std::lock_guard lock(state.residencyLock);
		delete Generator(state);
		state.generator = nullptr;
		state.generatorMode = 0;
	}

	// Brings the frame generator in line with the setting; returns it when it should run. TV presenter only.
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
		std::unique_ptr<Metal4FrameGenerator> generator;
		{
			std::lock_guard lock(state.residencyLock);
			generator = Metal4FrameGenerator::Create(state.device, state.compiler, state.residency, backend, whyNot);
		}
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

	// Game's thread: hands a composed frame to the output's presenter. A frame the presenter has not picked up
	// yet is replaced, so a slow display costs frames on screen, never time on the game's thread.
	ESPRESSU_METAL4 bool Enqueue(Metal4OutputStage::State& state, Metal4OutputStage::Output outputIndex, id<MTLCommandBuffer> legacy, CAMetalLayer* layer,
		double minimumDuration, void (^present)(void))
	{
		OutputSlots& output = state.outputs[outputIndex];
		// The regular queue tells the Metal 4 queue when the composition is done. If its command buffer fails,
		// the signal is given from the CPU so the Metal 4 queue never waits forever.
		const uint64_t composed = ++state.interopValue;
		id<MTLSharedEvent> interop = state.interopEvent;
		[legacy encodeSignalEvent:interop value:composed];
		[legacy addCompletedHandler:^(id<MTLCommandBuffer> finished) {
			if (finished.status == MTLCommandBufferStatusError && interop.signaledValue < composed)
				interop.signaledValue = composed;
		}];

		{
			std::lock_guard lock(state.slotLock);
			if (output.current < 0)
				return false;
			FrameSlot& slot = output.slots[output.current];
			if (output.pending.slot >= 0)
			{
				output.slots[output.pending.slot].use = SlotUse::Free;
				HostTuning::Count(HostTuning::GetStats().m4Dropped);
			}
			slot.use = SlotUse::Queued;
			slot.sequence = output.nextSequence++;
			output.pending = PendingFrame{output.current, composed, slot.sequence, CACurrentMediaTime(), minimumDuration, layer};
			output.current = -1;
		}
		dispatch_async(output.presenter, present);
		return true;
	}

	// Presenter thread: puts the newest queued frame of one output on screen, with a generated frame before it
	// when frame generation runs.
	ESPRESSU_METAL4 void PresentQueued(Metal4OutputStage::State& state, Metal4OutputStage::Output outputIndex)
	{
		OutputSlots& output = state.outputs[outputIndex];
		PendingFrame frame;
		int32_t previousSlot = -1;
		{
			std::lock_guard lock(state.slotLock);
			if (output.pending.slot < 0)
				return;
			frame = output.pending;
			output.pending = PendingFrame{};
			output.slots[frame.slot].use = SlotUse::Presenting;
			previousSlot = output.shownSlot;
		}
		FrameSlot& slot = output.slots[frame.slot];

		// Pacing from when the game produced its frames, not from when the display took them, so a busy
		// display cannot stretch the timing it is paced by.
		const double interval = output.lastProducedAt > 0.0 ? frame.producedAt - output.lastProducedAt : 0.0;
		output.lastProducedAt = frame.producedAt;
		const bool historyBreak = interval <= 0.0 || interval > kHistoryBreakInterval;
		if (historyBreak)
			output.smoothedInterval = 0.0;
		else
			output.smoothedInterval = output.smoothedInterval > 0.0 ? output.smoothedInterval * 0.9 + interval * 0.1 : interval;
		const double frameInterval = frame.minimumDuration > 0.0 ? frame.minimumDuration : output.smoothedInterval;
		const bool consecutive = previousSlot >= 0 && output.shownSequence + 1 == frame.sequence;

		Metal4FrameGenerator* generator = outputIndex == Metal4OutputStage::TV ? UpdateGenerator(state) : nullptr;
		const double generationMs = state.timings->generationGpuMs.load(std::memory_order_relaxed);
		const bool gpuKeepsUp = generationMs < frameInterval * 1000.0 * 0.4;
		if (!gpuKeepsUp)
			state.timings->generationGpuMs.store(generationMs * 0.95, std::memory_order_relaxed); // try again after a while
		bool generate = generator && !historyBreak && consecutive && frameInterval > kGenerateAboveInterval && gpuKeepsUp;
		if (generator)
		{
			// Why a frame got no generated partner, first reason only.
			auto& stats = HostTuning::GetStats();
			stats.fgIntervalMicros.store((uint64_t)(frameInterval * 1e6), std::memory_order_relaxed);
			stats.fgGpuMicros.store((uint64_t)(generationMs * 1000.0), std::memory_order_relaxed);
			if (historyBreak || !consecutive)
				HostTuning::Count(stats.fgSkippedHistory);
			else if (frameInterval <= kGenerateAboveInterval)
				HostTuning::Count(stats.fgSkippedFastEnough);
			else if (!gpuKeepsUp)
				HostTuning::Count(stats.fgSkippedGpu);
		}

		CAMetalLayer* layer = frame.layer;
		AttachLayer(state, output, layer);
		id<CAMetalDrawable> generatedDrawable = generate ? [layer nextDrawable] : nil;
		id<CAMetalDrawable> drawable = [layer nextDrawable];
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
			std::lock_guard lock(state.slotLock);
			slot.use = SlotUse::Free;
			return;
		}

		id<MTL4CommandAllocator> allocator = slot.allocator;
		id<MTL4ArgumentTable> arguments = slot.arguments;
		[allocator reset];

		id<MTL4CommandBuffer> generatedCommands = nil;
		if (generate)
		{
			generatedCommands = slot.commandBuffer;
			[generatedCommands beginCommandBufferWithAllocator:allocator];
			generatedCommands.label = @"EspressU generated frame";
			std::string whyDisabled;
			id<MTLTexture> generatedImage = nil;
			{
				std::lock_guard lock(state.residencyLock);
				generatedImage = generator->Encode(generatedCommands, output.slots[previousSlot].composition, slot.composition, (uint32_t)frame.slot,
					(float)frameInterval, output.lastWentThroughGenerator, whyDisabled);
			}
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
				generate = false; // the compute work still runs below so the generator's history stays consistent
		}

		id<MTL4CommandBuffer> commandBuffer = generatedCommands ? slot.secondCommandBuffer : slot.commandBuffer;
		[commandBuffer beginCommandBufferWithAllocator:allocator];
		commandBuffer.label = outputIndex == Metal4OutputStage::TV ? @"EspressU TV present" : @"EspressU GamePad present";
		id<CAMetalDrawable> realDrawable = generate ? drawable : (generatedDrawable ? generatedDrawable : drawable);
		EncodeBlit(commandBuffer, arguments, pipeline, slot.composition, realDrawable.texture, generatedCommands != nil);
		[commandBuffer endCommandBuffer];

		uint64_t done = 0;
		{
			std::lock_guard lock(state.submitLock);
			id<MTL4CommandQueue> queue = state.queue;
			[queue waitForEvent:state.interopEvent value:frame.composedValue];
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
			done = ++output.doneValue;
			[queue signalEvent:output.doneEvent value:done];
		}

		if (generate)
		{
			// Two presents for one real frame, half an interval apart: the display paces them.
			const double half = std::min(frameInterval * 0.5, kMaxPresentSpacing);
			[generatedDrawable presentAfterMinimumDuration:half];
			[realDrawable presentAfterMinimumDuration:half];
		}
		else if (frame.minimumDuration > 0.0)
			[realDrawable presentAfterMinimumDuration:frame.minimumDuration];
		else
			[realDrawable present];
		// A drawable taken for a generated frame that did not happen is simply not used.

		{
			std::lock_guard lock(state.slotLock);
			slot.doneValue = done;
			slot.use = SlotUse::Shown;
			if (previousSlot >= 0 && previousSlot != frame.slot)
			{
				FrameSlot& previous = output.slots[previousSlot];
				if (generatedCommands)
					previous.doneValue = done; // the generator read it too
				previous.use = SlotUse::Free;
			}
			output.shownSlot = frame.slot;
			output.shownSequence = frame.sequence;
		}
		output.lastWentThroughGenerator = generatedCommands != nil;
		HostTuning::Count(HostTuning::GetStats().m4Frames);
		HostTuning::GetStats().m4GpuMicros.store((uint64_t)(state.timings->lastGpuMs.load(std::memory_order_relaxed) * 1000.0), std::memory_order_relaxed);
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
	// Let the presenters finish what they hold; queued blocks reference this stage.
	for (OutputSlots& output : m_state->outputs)
	{
		if (output.presenter)
			dispatch_sync(output.presenter, ^{});
	}
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
		int32_t index = -1;
		uint64_t waitFor = 0;
		{
			std::lock_guard lock(state.slotLock);
			if (output.current >= 0)
				return (__bridge MTL::Texture*)output.slots[output.current].composition;
			for (uint32_t i = 0; i < kSlotsPerOutput; i++)
			{
				if (output.slots[i].use == SlotUse::Free)
				{
					index = (int32_t)i;
					break;
				}
			}
			if (index < 0 && output.pending.slot >= 0)
			{
				// The presenter is still busy with an older frame; the one waiting behind it would be replaced by
				// this frame anyway, so its slot is taken back now.
				index = output.pending.slot;
				output.pending = PendingFrame{};
				HostTuning::Count(HostTuning::GetStats().m4Dropped);
			}
			if (index < 0)
			{
				HostTuning::Count(HostTuning::GetStats().m4SlotTimeouts);
				return nullptr;
			}
			output.slots[index].use = SlotUse::Composing;
			output.current = index;
			waitFor = output.slots[index].doneValue;
		}
		FrameSlot& slot = output.slots[index];

		auto giveBack = [&]() {
			std::lock_guard lock(state.slotLock);
			slot.use = SlotUse::Free;
			output.current = -1;
		};
		if (waitFor && ![output.doneEvent waitUntilSignaledValue:waitFor timeoutMS:kSlotWaitMs])
		{
			HostTuning::Count(HostTuning::GetStats().m4SlotTimeouts);
			giveBack();
			return nullptr;
		}

		const MTLPixelFormat format = (MTLPixelFormat)pixelFormat;
		if (!slot.composition || slot.composition.width != width || slot.composition.height != height || slot.composition.pixelFormat != format)
		{
			std::lock_guard lock(state.residencyLock);
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
				giveBack();
				return nullptr;
			}
		}
		return (__bridge MTL::Texture*)slot.composition;
	}
	return nullptr;
}

bool Metal4OutputStage::PresentFrame(Output output, MTL::CommandBuffer* legacyCommandBuffer, CA::MetalLayer* layer, double minimumDuration)
{
	if (output >= OutputCount || !legacyCommandBuffer || !layer)
		return false;
	if (@available(iOS 26.0, macOS 26.0, *))
	{
		State* state = m_state.get();
		return Enqueue(*state, output, (__bridge id<MTLCommandBuffer>)legacyCommandBuffer, (__bridge CAMetalLayer*)layer, minimumDuration, ^{
			PresentQueued(*state, output);
		});
	}
	return false;
}

double Metal4OutputStage::LastGpuTimeMilliseconds() const
{
	return m_state->timings->lastGpuMs.load(std::memory_order_relaxed);
}

bool Metal4OutputStage::WaitIdle(uint32_t timeoutMs)
{
	bool idle = true;
	for (OutputSlots& output : m_state->outputs)
	{
		if (output.doneEvent && output.doneValue)
			idle = [output.doneEvent waitUntilSignaledValue:output.doneValue timeoutMS:timeoutMs] && idle;
	}
	return idle;
}
