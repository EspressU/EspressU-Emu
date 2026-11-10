#include "Common/precompiled.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFrameGen.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFrameGenShaders.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "gui/interface/WindowSystem.h"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#include <os/proc.h>
#include <pthread/qos.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace
{
	constexpr size_t kHistory = 4;
	constexpr size_t kCaptureSlots = kHistory + 2;
	constexpr uint32_t kMatchingExtent = 320;
	constexpr double kHistoryGap = 0.25;

	struct Grid
	{
		uint32_t width, height;
		float phase;
	};

	id<MTLTexture> makeTexture(id<MTLDevice> device, MTLPixelFormat format, uint32_t width, uint32_t height, MTLTextureUsage usage)
	{
		auto descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format width:width height:height mipmapped:NO];
		descriptor.storageMode = MTLStorageModePrivate;
		descriptor.usage = usage;
		return [device newTextureWithDescriptor:descriptor];
	}

	struct Pipelines
	{
		id<MTLComputePipelineState> regions, camera, refine, project;
		std::array<id<MTLRenderPipelineState>, 2> copy{}, interpolate{};

		bool Build(id<MTLDevice> device)
		{
			NSError* error = nil;
			auto library = [device newLibraryWithSource:[NSString stringWithUTF8String:kFrameGenShaderSource] options:nil error:&error];
			if (!library)
			{
				NSLog(@"EspressU frame generation shaders: %@", error);
				return false;
			}
			auto compute = [&](NSString* name) {
				NSError* failure = nil;
				auto pipeline = [device newComputePipelineStateWithFunction:[library newFunctionWithName:name] error:&failure];
				if (!pipeline)
					NSLog(@"EspressU frame generation %@: %@", name, failure);
				return pipeline;
			};
			regions = compute(@"fg_regions");
			camera = compute(@"fg_camera");
			refine = compute(@"fg_refine");
			project = compute(@"fg_project");

			auto vertex = [library newFunctionWithName:@"fg_vertex"];
			auto makeRender = [&](NSString* name, MTLPixelFormat format) {
				auto descriptor = [MTLRenderPipelineDescriptor new];
				descriptor.vertexFunction = vertex;
				descriptor.fragmentFunction = [library newFunctionWithName:name];
				descriptor.colorAttachments[0].pixelFormat = format;
				NSError* failure = nil;
				auto pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:&failure];
				if (!pipeline)
					NSLog(@"EspressU frame generation %@: %@", name, failure);
				return pipeline;
			};
			for (size_t i = 0; i < copy.size(); ++i)
			{
				auto format = i == 0 ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatBGRA8Unorm_sRGB;
				copy[i] = makeRender(@"fg_copy", format);
				interpolate[i] = makeRender(@"fg_present", format);
			}
			return regions && camera && refine && project && copy[0] && copy[1] && interpolate[0] && interpolate[1];
		}
	};

	void dispatchGrid(id<MTLComputeCommandEncoder> encoder, id<MTLComputePipelineState> pipeline, uint32_t width, uint32_t height)
	{
		const NSUInteger x = pipeline.threadExecutionWidth;
		const NSUInteger y = std::min<NSUInteger>(4, pipeline.maxTotalThreadsPerThreadgroup / x);
		[encoder dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(x, y, 1)];
	}

	struct Motion
	{
		struct Direction
		{
			id<MTLTexture> regions, field;
			id<MTLBuffer> model, projection;
		};
		std::array<Direction, 2> directions{};
		Grid grid{};
		uint64_t first = 0, second = 0;

		bool Prepare(id<MTLDevice> device, id<MTLTexture> source)
		{
			const double scale = std::min(1.0, double(kMatchingExtent) / double(std::max(source.width, source.height)));
			const uint32_t width = std::max(1u, uint32_t(source.width * scale));
			const uint32_t height = std::max(1u, uint32_t(source.height * scale));
			if (grid.width == width && grid.height == height && directions[1].projection)
				return true;
			*this = {};
			grid = {width, height, 0.0f};
			for (auto& direction : directions)
			{
				constexpr auto usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
				direction.regions = makeTexture(device, MTLPixelFormatRGBA16Float, (width + 7) / 8, (height + 7) / 8, usage);
				direction.field = makeTexture(device, MTLPixelFormatRGBA16Float, (width + 3) / 4, (height + 3) / 4, usage);
				direction.model = [device newBufferWithLength:24 options:MTLResourceStorageModePrivate];
				direction.projection = [device newBufferWithLength:size_t(width) * height * sizeof(uint32_t) options:MTLResourceStorageModePrivate];
				if (!direction.regions || !direction.field || !direction.model || !direction.projection)
				{
					*this = {};
					return false;
				}
			}
			return true;
		}

		void Analyze(id<MTLCommandBuffer> command, const Pipelines& pipelines, id<MTLTexture> a, id<MTLTexture> b, uint64_t idA, uint64_t idB)
		{
			if (first == idA && second == idB)
				return;
			first = idA;
			second = idB;
			for (auto& direction : directions)
			{
				auto regionEncoder = [command computeCommandEncoder];
				[regionEncoder setComputePipelineState:pipelines.regions];
				[regionEncoder setTexture:a atIndex:0];
				[regionEncoder setTexture:b atIndex:1];
				[regionEncoder setTexture:direction.regions atIndex:2];
				[regionEncoder setBytes:&grid length:sizeof(grid) atIndex:0];
				dispatchGrid(regionEncoder, pipelines.regions, (uint32_t)direction.regions.width, (uint32_t)direction.regions.height);
				[regionEncoder endEncoding];

				auto cameraEncoder = [command computeCommandEncoder];
				[cameraEncoder setComputePipelineState:pipelines.camera];
				[cameraEncoder setTexture:direction.regions atIndex:0];
				[cameraEncoder setBuffer:direction.model offset:0 atIndex:0];
				[cameraEncoder dispatchThreadgroups:MTLSizeMake(1, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)];
				[cameraEncoder endEncoding];

				auto refineEncoder = [command computeCommandEncoder];
				[refineEncoder setComputePipelineState:pipelines.refine];
				[refineEncoder setTexture:a atIndex:0];
				[refineEncoder setTexture:b atIndex:1];
				[refineEncoder setTexture:direction.regions atIndex:2];
				[refineEncoder setTexture:direction.field atIndex:3];
				[refineEncoder setBytes:&grid length:sizeof(grid) atIndex:0];
				[refineEncoder setBuffer:direction.model offset:0 atIndex:1];
				dispatchGrid(refineEncoder, pipelines.refine, (uint32_t)direction.field.width, (uint32_t)direction.field.height);
				[refineEncoder endEncoding];
				std::swap(a, b);
			}
		}

		void Project(id<MTLCommandBuffer> command, const Pipelines& pipelines, id<MTLTexture> a, id<MTLTexture> b, float phase)
		{
			auto clear = [command blitCommandEncoder];
			for (const auto& direction : directions)
				[clear fillBuffer:direction.projection range:NSMakeRange(0, direction.projection.length) value:0];
			[clear endEncoding];

			Grid params = grid;
			for (size_t i = 0; i < directions.size(); ++i)
			{
				auto encoder = [command computeCommandEncoder];
				params.phase = i == 0 ? phase : 1.0f - phase;
				[encoder setComputePipelineState:pipelines.project];
				[encoder setTexture:a atIndex:0];
				[encoder setTexture:b atIndex:1];
				[encoder setTexture:directions[i].field atIndex:2];
				[encoder setTexture:directions[1 - i].field atIndex:3];
				[encoder setBytes:&params length:sizeof(params) atIndex:0];
				[encoder setBuffer:directions[i].projection offset:0 atIndex:1];
				dispatchGrid(encoder, pipelines.project, grid.width, grid.height);
				[encoder endEncoding];
				std::swap(a, b);
			}
		}
	};

	struct Image
	{
		id<MTLTexture> texture;
		uint64_t id = 0;
		double time = 0.0;
	};

	struct DisplayMeter
	{
		std::mutex mutex;
		uint64_t session = 0;
		std::deque<std::pair<double, bool>> presentations;

		uint64_t Reset()
		{
			std::lock_guard lock(mutex);
			presentations.clear();
			return ++session;
		}

		void Record(uint64_t epoch, double time, bool generated)
		{
			if (!(time > 0.0))
				return;
			std::lock_guard lock(mutex);
			if (session != epoch)
				return;
			if (!presentations.empty() && time <= presentations.back().first)
				return;
			while (!presentations.empty() && time - presentations.front().first > 1.0)
				presentations.pop_front();
			presentations.emplace_back(time, generated);
		}

		MetalFrameGen::Rates Read()
		{
			std::lock_guard lock(mutex);
			const double now = CACurrentMediaTime();
			while (!presentations.empty() && now - presentations.front().first > 1.0)
				presentations.pop_front();
			if (presentations.size() < 2 || now - presentations.back().first > kHistoryGap)
				return {};
			const double span = presentations.back().first - presentations.front().first;
			if (span < 0.1)
				return {};
			const size_t generated = std::count_if(std::next(presentations.begin()), presentations.end(),
				[](const auto& entry) { return entry.second; });
			return {double(presentations.size() - 1) / span, double(generated) / span};
		}
	};

	DisplayMeter& meter()
	{
		static DisplayMeter value;
		return value;
	}

	struct Presenter
	{
		id<MTLDevice> device;
		CAMetalLayer* layer;
		id<MTLCommandQueue> queue;
		Pipelines pipelines;
		Motion motion;
		std::mutex mutex;
		std::condition_variable wake;
		std::deque<std::shared_ptr<Image>> history;
		std::atomic<bool> ready{false}, failed{false}, stopped{false};
		std::atomic<unsigned> inFlight{0};
		std::atomic<double> gpuCooldownUntil{0.0};
		uint64_t generation = 0, meterSession = 0, tick = 0;
		double target = 0.0, refresh = 1.0 / 60.0, interval = 0.0, gpuLag = 0.0;
		uint64_t lastReal = 0, presentedGeneration = 0;
		double lastSourceTime = 0.0;

		void Publish(const std::shared_ptr<Image>& image, uint64_t epoch, bool success)
		{
			std::lock_guard lock(mutex);
			if (stopped.load() || generation != epoch)
				return;
			if (!success)
			{
				failed.store(true);
				HostTuning::Count(HostTuning::GetStats().fgFailures);
				return;
			}
			if (!history.empty())
			{
				const double delta = image->time - history.back()->time;
				if (delta <= 0.0)
					return;
				if (delta > kHistoryGap)
				{
					history.clear();
					interval = 0.0;
					++generation;
					meterSession = meter().Reset();
				}
				else
					interval = interval == 0.0 ? delta : 0.9 * interval + 0.1 * delta;
			}
			const double latency = std::clamp(CACurrentMediaTime() - image->time, 0.0, 0.15);
			gpuLag = gpuLag == 0.0 ? latency : 0.9 * gpuLag + 0.1 * latency;
			history.push_back(image);
			if (history.size() > kHistory)
				history.pop_front();
		}

		void Tick(double displayTime, double displayPeriod)
		{
			if (!ready.load() || !std::isfinite(displayTime) || !(displayPeriod > 0.0))
				return;
			{
				std::lock_guard lock(mutex);
				target = displayTime;
				refresh = displayPeriod;
				++tick;
			}
			wake.notify_one();
		}
	};
}

#if TARGET_OS_IPHONE
@interface EspressUFGDisplayClock : NSObject
- (instancetype)initWithPresenter:(std::weak_ptr<Presenter>)presenter;
- (void)start;
- (void)stop;
@end

@implementation EspressUFGDisplayClock
{
	std::weak_ptr<Presenter> _presenter;
	CADisplayLink* _link;
}

- (instancetype)initWithPresenter:(std::weak_ptr<Presenter>)presenter
{
	if ((self = [super init]))
		_presenter = presenter;
	return self;
}

- (void)start
{
	auto presenter = _presenter.lock();
	if (!presenter || presenter->stopped.load())
		return;
	id delegate = presenter->layer.delegate;
	UIView* view = [delegate isKindOfClass:UIView.class] ? (UIView*)delegate : nil;
	UIWindowScene* scene = view.window.windowScene;
	if (@available(iOS 27.0, *))
		_link = [scene displayLinkWithTarget:self selector:@selector(tick:)];
	else
		_link = [scene.screen displayLinkWithTarget:self selector:@selector(tick:)];
	if (!_link)
		_link = [CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)];
	const float maximum = scene ? float(scene.screen.maximumFramesPerSecond) : 120.0f;
	_link.preferredFrameRateRange = CAFrameRateRangeMake(std::min(60.0f, maximum), maximum, maximum);
	[_link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
}

- (void)tick:(CADisplayLink*)link
{
	if (auto presenter = _presenter.lock())
		presenter->Tick(link.targetTimestamp, link.targetTimestamp - link.timestamp);
}

- (void)stop
{
	[_link invalidate];
	_link = nil;
}
@end
#endif

namespace
{
	void Present(const std::shared_ptr<Presenter>& state)
	{
		std::shared_ptr<Image> a, b;
		double phase = 0.0, target = 0.0, refresh = 0.0, sourceTime = 0.0;
		uint64_t generation = 0, epoch = 0;
		bool interpolate = false;
		{
			std::lock_guard lock(state->mutex);
			if (state->history.empty() || state->failed.load() ||
				CACurrentMediaTime() - state->history.back()->time > kHistoryGap)
				return;

			target = state->target;
			refresh = state->refresh;
			generation = state->generation;
			epoch = state->meterSession;
			if (generation != state->presentedGeneration)
			{
				state->presentedGeneration = generation;
				state->lastReal = 0;
				state->lastSourceTime = 0.0;
			}
			b = state->history.back();
			sourceTime = b->time;
			if (state->interval > state->refresh * 1.15 && state->history.size() >= 2 &&
				target >= state->gpuCooldownUntil.load() &&
				NSProcessInfo.processInfo.thermalState < NSProcessInfoThermalStateSerious)
			{
				const double sample = target - state->refresh - state->interval - std::min(state->gpuLag, state->interval);
				for (size_t i = 1; i < state->history.size(); ++i)
				{
					const auto& first = state->history[i - 1];
					const auto& second = state->history[i];
					if (sample < first->time || sample > second->time)
						continue;
					phase = (sample - first->time) / (second->time - first->time);
					a = first;
					b = second;
					sourceTime = sample;
					// Give each source image its nearest display slot. At
					// 30 -> 60 the other slot is the interpolated picture.
					const double snap = refresh * 0.5;
					const bool showFirst = sample - first->time <= snap;
					interpolate = !showFirst && second->time - sample > snap;
					if (showFirst)
						b = a;
					break;
				}
				// Once playing on the delayed timeline, an underrun holds the
				// previous picture rather than jumping ahead to the newest one.
				if (!a && state->lastSourceTime > 0.0 && sample < b->time)
					return;
			}
			if (sourceTime <= state->lastSourceTime || (!interpolate && b->id == state->lastReal))
				return;
		}

		if (state->inFlight.load() >= 2 || CACurrentMediaTime() > target)
		{
			HostTuning::Count(HostTuning::GetStats().fgBacklog);
			return;
		}
		auto drawable = [state->layer nextDrawable];
		if (!drawable)
		{
			HostTuning::Count(HostTuning::GetStats().fgDrawableWaits);
			return;
		}
		if (state->stopped.load() || state->failed.load() || PerfTelemetry::AppInBackground().load() ||
			!(WindowSystem::GetWindowInfo().visible_outputs.load() & 1u))
			return;
		if (CACurrentMediaTime() > target + refresh)
		{
			HostTuning::Count(HostTuning::GetStats().fgBacklog);
			return;
		}

		auto output = drawable.texture;
		if (output.width != b->texture.width || output.height != b->texture.height ||
			output.pixelFormat != b->texture.pixelFormat)
			return;
		{
			std::lock_guard lock(state->mutex);
			if (generation != state->generation)
				return;
		}

		auto command = [state->queue commandBuffer];
		if (!command)
			return;
		const size_t format = output.pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB ? 1 : 0;
		interpolate = interpolate && state->motion.Prepare(state->device, b->texture);
		if (interpolate)
		{
			state->motion.Analyze(command, state->pipelines, a->texture, b->texture, a->id, b->id);
			state->motion.Project(command, state->pipelines, a->texture, b->texture, float(phase));
		}

		auto pass = [MTLRenderPassDescriptor renderPassDescriptor];
		pass.colorAttachments[0].texture = output;
		pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
		pass.colorAttachments[0].storeAction = MTLStoreActionStore;
		auto render = [command renderCommandEncoderWithDescriptor:pass];
		if (!render)
		{
			state->failed.store(true);
			HostTuning::Count(HostTuning::GetStats().fgFailures);
			return;
		}
		[render setRenderPipelineState:interpolate ? state->pipelines.interpolate[format] : state->pipelines.copy[format]];
		if (interpolate)
		{
			Grid params = state->motion.grid;
			params.phase = float(phase);
			[render setFragmentTexture:a->texture atIndex:0];
			[render setFragmentTexture:b->texture atIndex:1];
			[render setFragmentTexture:state->motion.directions[0].field atIndex:2];
			[render setFragmentTexture:state->motion.directions[1].field atIndex:3];
			[render setFragmentBytes:&params length:sizeof(params) atIndex:0];
			[render setFragmentBuffer:state->motion.directions[0].projection offset:0 atIndex:1];
			[render setFragmentBuffer:state->motion.directions[1].projection offset:0 atIndex:2];
		}
		else
			[render setFragmentTexture:b->texture atIndex:0];
		[render drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
		[render endEncoding];

		// Keep the pool entries leased until this GPU read has finished.
		[command addCompletedHandler:^(id<MTLCommandBuffer> finished) {
			(void)a;
			(void)b;
			state->inFlight.fetch_sub(1);
			if (finished.status == MTLCommandBufferStatusError)
			{
				state->failed.store(true);
				HostTuning::Count(HostTuning::GetStats().fgFailures);
			}
			else if (interpolate && finished.GPUEndTime - finished.GPUStartTime > refresh * 0.8)
				state->gpuCooldownUntil.store(CACurrentMediaTime() + 0.5);
		}];
		[drawable addPresentedHandler:^(id<MTLDrawable> shown) {
			const double time = shown.presentedTime;
			if (time > 0.0 && !state->stopped.load())
			{
				meter().Record(epoch, time, interpolate);
				PerfTelemetry::Get().tvPresents.fetch_add(1, std::memory_order_relaxed);
				LatteWait::Get().presentedFrames.fetch_add(1, std::memory_order_relaxed);
				if (interpolate)
					HostTuning::Count(HostTuning::GetStats().fgGenerated);
			}
		}];

		{
			std::lock_guard lock(state->mutex);
			if (state->stopped.load() || generation != state->generation)
				return;
		}
		state->inFlight.fetch_add(1);
		state->lastSourceTime = sourceTime;
		if (!interpolate)
			state->lastReal = b->id;
		[command presentDrawable:drawable atTime:std::max(target, CACurrentMediaTime())];
		[command commit];
	}

	void RunPresenter(const std::shared_ptr<Presenter>& state)
	{
		@autoreleasepool
		{
#if TARGET_OS_IPHONE
			pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
			state->queue = [state->device newCommandQueue];
			if (!state->queue || !state->pipelines.Build(state->device))
			{
				state->failed.store(true);
				return;
			}
			state->ready.store(true);
		}
		uint64_t seen = 0;
		std::unique_lock lock(state->mutex);
		for (;;)
		{
			state->wake.wait(lock, [&] { return state->stopped.load() || seen != state->tick; });
			if (state->stopped.load())
				break;
			seen = state->tick;
			lock.unlock();
			@autoreleasepool
			{
				if (!PerfTelemetry::AppInBackground().load() &&
					(WindowSystem::GetWindowInfo().visible_outputs.load() & 1u))
					Present(state);
			}
			lock.lock();
		}
	}
}

struct MetalFrameGen::State
{
	std::shared_ptr<Presenter> presenter = std::make_shared<Presenter>();
	std::array<std::shared_ptr<Image>, kCaptureSlots> slots{};
	std::shared_ptr<Image> capture;
	std::thread worker;
#if TARGET_OS_IPHONE
	EspressUFGDisplayClock* clock = nil;
#endif
	uint32_t width = 0, height = 0;
	MTLPixelFormat format = MTLPixelFormatInvalid;
	uint64_t nextID = 0;
	bool active = false;
	double retryAt = 0.0;
};

MetalFrameGen::MetalFrameGen(MTL::Device* device, CA::MetalLayer* layer) : m_state(std::make_unique<State>())
{
#if TARGET_OS_IPHONE
	auto& state = *m_state;
	state.presenter->device = (__bridge id<MTLDevice>)(void*)device;
	state.presenter->layer = (__bridge CAMetalLayer*)(void*)layer;
	state.presenter->meterSession = meter().Reset();
	state.clock = [[EspressUFGDisplayClock alloc] initWithPresenter:state.presenter];
	state.worker = std::thread(RunPresenter, state.presenter);
	EspressUFGDisplayClock* clock = state.clock;
	dispatch_async(dispatch_get_main_queue(), ^{ [clock start]; });
#else
	(void)device;
	(void)layer;
#endif
}

MetalFrameGen::~MetalFrameGen()
{
	auto& state = *m_state;
	{
		std::lock_guard lock(state.presenter->mutex);
		state.presenter->stopped.store(true);
		state.presenter->history.clear();
	}
	state.presenter->wake.notify_one();
#if TARGET_OS_IPHONE
	EspressUFGDisplayClock* clock = state.clock;
	dispatch_async(dispatch_get_main_queue(), ^{ [clock stop]; });
#endif
	if (state.worker.joinable())
		state.worker.join();
	if (state.active)
		meter().Reset();
}

MTL::Texture* MetalFrameGen::BeginCapture(uint32_t width, uint32_t height, bool srgb)
{
	auto& state = *m_state;
	if (!state.presenter->ready.load() || state.presenter->failed.load() || width == 0 || height == 0)
		return nullptr;
	const auto format = srgb ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatBGRA8Unorm;
	if (state.width != width || state.height != height || state.format != format)
	{
		InvalidateHistory();
		state.width = width;
		state.height = height;
		state.format = format;
		state.active = false;
		state.retryAt = 0.0;
	}
	if (state.capture)
		return (MTL::Texture*)(__bridge void*)state.capture->texture;
	const double now = CACurrentMediaTime();
	if (now < state.retryAt)
		return nullptr;
#if TARGET_OS_IPHONE
	if (!state.active)
	{
		const uint64_t needed = (64ull << 20) + 24ull * width * height;
		if (os_proc_available_memory() < needed)
		{
			state.retryAt = now + 1.0;
			return nullptr;
		}
	}
#endif
	for (auto& slot : state.slots)
	{
		if (slot && slot.use_count() != 1)
			continue;
		if (!slot)
		{
			auto texture = makeTexture(state.presenter->device, format, width, height, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
			if (!texture)
			{
				state.retryAt = now + 1.0;
				return nullptr;
			}
			slot = std::make_shared<Image>();
			slot->texture = texture;
		}
		state.capture = slot;
		state.active = true;
		return (MTL::Texture*)(__bridge void*)slot->texture;
	}
	HostTuning::Count(HostTuning::GetStats().fgBacklog);
	return nullptr;
}

bool MetalFrameGen::OwnsLayer() const
{
	return m_state->active && !m_state->presenter->failed.load();
}

void MetalFrameGen::SubmitCapture(MTL::CommandBuffer* commandBuffer)
{
	auto& state = *m_state;
	auto image = std::move(state.capture);
	if (!image)
		return;
	image->id = ++state.nextID;
	image->time = CACurrentMediaTime();
	auto presenter = state.presenter;
	uint64_t generation;
	{
		std::lock_guard lock(presenter->mutex);
		generation = presenter->generation;
	}
	auto command = (__bridge id<MTLCommandBuffer>)(void*)commandBuffer;
	[command addCompletedHandler:^(id<MTLCommandBuffer> done) {
		presenter->Publish(image, generation, done.status == MTLCommandBufferStatusCompleted);
	}];
}

void MetalFrameGen::RelieveMemoryPressure()
{
	InvalidateHistory();
	m_state->active = false;
	m_state->retryAt = CACurrentMediaTime() + 1.0;
}

void MetalFrameGen::InvalidateHistory()
{
	auto& state = *m_state;
	state.capture.reset();
	state.slots = {};
	std::lock_guard lock(state.presenter->mutex);
	++state.presenter->generation;
	state.presenter->history.clear();
	state.presenter->interval = 0.0;
	state.presenter->gpuLag = 0.0;
	state.presenter->meterSession = meter().Reset();
}

MetalFrameGen::Rates MetalFrameGen::DisplayRates()
{
	return meter().Read();
}
