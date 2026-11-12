// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

#include "Cafe/HW/Latte/Core/LatteTextureAsyncLoad.h"
#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "util/helpers/helpers.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <unordered_map>
#include <vector>
#if __APPLE__
#include <pthread/qos.h>
#endif

void LatteTextureLoader_loadTextureDataIntoSlice(LatteTexture* hostTexture, sint32 width, sint32 height, sint32 depth, sint32 mipLevels, void* pixelData, sint32 sliceIndex, sint32 mipIndex, uint32 compressedImageSize);

namespace LatteTextureAsync
{
	namespace
	{
		// Converted data waiting for upload is held in memory. It is uploaded every frame, so this is only reached
		// when the render thread stalls; beyond it slices load synchronously as before. Queued slices that are not
		// converted yet cost no memory, only waiting time, so their number is capped much higher.
		constexpr size_t kMaxConvertedBytes = 128u * 1024u * 1024u;
		constexpr size_t kMaxQueuedSlices = 8192;
		// Small slices (the lower mips) convert in well under a millisecond: do them right away so a new texture
		// gets its real far-away look immediately.
		constexpr uint32 kMinAsyncBytes = 16u * 1024u;

		enum : uint32
		{
			kQueued = 0,
			kRunning = 1,
			kDone = 2,
		};

		struct Job
		{
			LatteTexture* tex = nullptr;
			LatteTextureLoaderCtx loader{};
			TextureDecoder* decoder = nullptr;
			uint32 slice = 0;
			uint32 mip = 0;
			uint32 depth = 0;
			uint32 mipLevels = 0;
			uint32 imageSize = 0;
			std::unique_ptr<uint8[]> data;
			std::atomic<uint32> state{kQueued};
			std::atomic<bool> cancelled{false};
			bool ok = false;
			// Render thread only
			std::atomic<bool> counted{false}; // its converted data is part of s_convertedBytes
			bool applied = false;
		};
		using JobPtr = std::shared_ptr<Job>;

		std::mutex s_mutex;
		std::condition_variable s_workCv;
		std::condition_variable s_doneCv;
		std::deque<JobPtr> s_queue;
		std::vector<JobPtr> s_done;
		std::atomic<uint32> s_doneCount{0};
		std::atomic<size_t> s_convertedBytes{0}; // converted on a worker, not uploaded yet
		std::atomic<size_t> s_queuedSlices{0};
		std::vector<std::thread> s_workers;
		bool s_stop = false;
		uint32 s_syncDepth = 0; // render thread only

		// Render thread only: the pending slices of each texture, oldest first.
		std::unordered_map<LatteTexture*, std::vector<JobPtr>> s_byTexture;

		void Decode(Job& job)
		{
			job.data.reset(new (std::nothrow) uint8[job.imageSize]);
			if (!job.data)
			{
				job.ok = false;
				return;
			}
			try
			{
				job.decoder->decode(&job.loader, job.data.get());
				job.ok = true;
			}
			catch (const std::bad_alloc&)
			{
				job.data.reset();
				job.ok = false;
			}
		}

		void WorkerMain(uint32 index)
		{
			SetThreadName("texLoad");
#if __APPLE__
			// Same class as the pipeline compilers: below the emulated CPU and render thread, above background work.
			pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif
			t_textureLoaderOnWorker = true;
			for (;;)
			{
				JobPtr job;
				{
					std::unique_lock lock(s_mutex);
					s_workCv.wait(lock, [] { return s_stop || !s_queue.empty(); });
					if (s_stop)
						return;
					job = std::move(s_queue.front());
					s_queue.pop_front();
				}
				if (job->cancelled.load(std::memory_order_acquire))
					continue;
				uint32 expected = kQueued;
				if (!job->state.compare_exchange_strong(expected, kRunning))
					continue; // the render thread needed it and converted it itself
				s_queuedSlices.fetch_sub(1, std::memory_order_relaxed);
				{
					PerfTelemetry::ScopedTimer timer(PerfTelemetry::Get().asyncTexWorkerNs);
					Decode(*job);
				}
				if (job->data)
				{
					job->counted.store(true, std::memory_order_relaxed);
					s_convertedBytes.fetch_add(job->imageSize, std::memory_order_relaxed);
				}
				{
					std::lock_guard lock(s_mutex);
					job->state.store(kDone, std::memory_order_release);
					s_done.emplace_back(job);
					s_doneCount.fetch_add(1, std::memory_order_release);
				}
				s_doneCv.notify_all();
			}
		}

		void EnsureWorkers()
		{
			if (!s_workers.empty())
				return;
			// The emulated CPU uses up to three cores and the render thread one more. Two workers on small chips,
			// three when there is room.
			const uint32 cores = std::max(1u, std::thread::hardware_concurrency());
			// Three on the 5-core A16 too: at an area change the two workers were busy for 3.8 of 5 seconds.
			const uint32 count = cores >= 5 ? 3 : 2;
			s_stop = false;
			for (uint32 i = 0; i < count; i++)
				s_workers.emplace_back(WorkerMain, i);
			cemuLog_log(LogType::Force, "Textures: {} background threads convert BC textures to ASTC (async texture loading on)", count);
		}

		// Every job a worker converted ends up in s_done, so its memory is uncounted exactly once, there.
		void Uncount(Job& job)
		{
			if (job.counted.exchange(false, std::memory_order_relaxed))
				s_convertedBytes.fetch_sub(job.imageSize, std::memory_order_relaxed);
		}

		void RemoveFromTexture(const JobPtr& job)
		{
			auto it = s_byTexture.find(job->tex);
			if (it == s_byTexture.end())
				return;
			auto& list = it->second;
			list.erase(std::remove(list.begin(), list.end(), job), list.end());
			if (list.empty())
				s_byTexture.erase(it);
		}

		void Cancel(const JobPtr& job)
		{
			job->cancelled.store(true, std::memory_order_release);
			// Claim it if no worker has started it, so none will.
			uint32 expected = kQueued;
			if (job->state.compare_exchange_strong(expected, kDone))
				s_queuedSlices.fetch_sub(1, std::memory_order_relaxed);
		}

		void Apply(const JobPtr& job)
		{
			job->applied = true;
			if (job->ok && job->data)
			{
				LatteTextureLoader_loadTextureDataIntoSlice(job->tex, job->loader.width, job->loader.height, job->depth, job->mipLevels,
					job->data.get(), job->slice, job->mip, job->imageSize);
				PerfTelemetry::Get().asyncTexApplied.fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				cemuLog_logOnce(LogType::Force, "Textures: a background texture conversion ran out of memory; that texture will be loaded again");
				g_renderer->texture_uploadBufferUnavailable(job->tex);
			}
			job->data.reset();
		}

		// A brand-new texture has no picture yet. Fill the slice with its average colour (ASTC void-extent blocks)
		// so it does not flash black or show garbage until the real data arrives.
		void UploadPlaceholder(LatteTexture* tex, const LatteTextureLoaderCtx& source, TextureDecoder* decoder, uint32 slice, uint32 mip, uint32 depth, uint32 mipLevels, uint32 imageSize)
		{
			if ((imageSize % 16) != 0)
				return;
			LatteTextureLoaderCtx loader = source;
			const sint32 stepX = std::max(1, loader.stepX);
			const sint32 stepY = std::max(1, loader.stepY);
			const sint32 blocksX = std::max(1, (loader.width + stepX - 1) / stepX);
			const sint32 blocksY = std::max(1, (loader.height + stepY - 1) / stepY);
			const sint32 samplesX = std::min(8, blocksX);
			const sint32 samplesY = std::min(8, blocksY);
			uint32 sum[4] = {};
			uint32 count = 0;
			for (sint32 sy = 0; sy < samplesY; sy++)
			{
				for (sint32 sx = 0; sx < samplesX; sx++)
				{
					const sint32 bx = (sx * 2 + 1) * blocksX / (samplesX * 2);
					const sint32 by = (sy * 2 + 1) * blocksY / (samplesY * 2);
					uint8* block = LatteTextureLoader_GetInput(&loader, bx * stepX, by * stepY);
					uint8 px[4];
					decoder->decodePixelToRGBA(block, px, 1, 1);
					for (int c = 0; c < 4; c++)
						sum[c] += px[c];
					count++;
				}
			}
			if (count == 0)
				return;
			uint8 blockBytes[16];
			// Void-extent block: block mode 0x1FC, LDR, both reserved bits set, all extent coordinates 1 (no extent),
			// then RGBA as UNORM16.
			const uint64 header = 0xFFFFFFFFFFFFFDFCull;
			memcpy(blockBytes, &header, 8);
			for (int c = 0; c < 4; c++)
			{
				const uint16 v = (uint16)((sum[c] + count / 2) / count) * 257;
				blockBytes[8 + c * 2 + 0] = (uint8)(v & 0xFF);
				blockBytes[8 + c * 2 + 1] = (uint8)(v >> 8);
			}
			std::unique_ptr<uint8[]> fill(new (std::nothrow) uint8[imageSize]);
			if (!fill)
				return;
			for (uint32 off = 0; off < imageSize; off += 16)
				memcpy(fill.get() + off, blockBytes, 16);
			LatteTextureLoader_loadTextureDataIntoSlice(tex, loader.width, loader.height, depth, mipLevels, fill.get(), slice, mip, imageSize);
			PerfTelemetry::Get().asyncTexPlaceholders.fetch_add(1, std::memory_order_relaxed);
		}
	}

	bool TryQueue(LatteTexture* tex, const LatteTextureLoaderCtx& loader, TextureDecoder* decoder, uint32 sliceIndex, uint32 mipIndex, uint32 depth, uint32 mipLevels, uint32 imageSize)
	{
		if (s_syncDepth != 0 || !HostTuning::Get().asyncTextureLoading.load(std::memory_order_relaxed))
			return false;
		if (!decoder || !decoder->isSlowTranscodeToASTC())
			return false;
		// Only plain sampled textures: anything the GPU renders into or that is depth keeps the synchronous path.
		if (tex->isDepth || tex->isRenderTarget || tex->isUpdatedOnGPU)
			return false;
		if (imageSize < kMinAsyncBytes)
			return false;
		if (s_convertedBytes.load(std::memory_order_relaxed) + imageSize > kMaxConvertedBytes ||
			s_queuedSlices.load(std::memory_order_relaxed) >= kMaxQueuedSlices)
		{
			PerfTelemetry::Get().asyncTexFallbacks.fetch_add(1, std::memory_order_relaxed);
			return false;
		}
		EnsureWorkers();

		// A newer load of the same slice replaces the pending one.
		CancelSlice(tex, sliceIndex, mipIndex);

		// reloadCount is 1 during the first load of a texture: it has no picture yet.
		if (tex->reloadCount <= 1)
			UploadPlaceholder(tex, loader, decoder, sliceIndex, mipIndex, depth, mipLevels, imageSize);

		auto job = std::make_shared<Job>();
		job->tex = tex;
		job->loader = loader;
		job->loader.dump = false;
		job->loader.dumpRGBA = nullptr;
		job->decoder = decoder;
		job->slice = sliceIndex;
		job->mip = mipIndex;
		job->depth = depth;
		job->mipLevels = mipLevels;
		job->imageSize = imageSize;
		s_byTexture[tex].emplace_back(job);
		s_queuedSlices.fetch_add(1, std::memory_order_relaxed);
		{
			std::lock_guard lock(s_mutex);
			s_queue.emplace_back(std::move(job));
		}
		s_workCv.notify_one();
		PerfTelemetry::Get().asyncTexQueued.fetch_add(1, std::memory_order_relaxed);
		return true;
	}

	void CancelSlice(LatteTexture* tex, uint32 sliceIndex, uint32 mipIndex)
	{
		if (s_byTexture.empty())
			return;
		auto it = s_byTexture.find(tex);
		if (it == s_byTexture.end())
			return;
		auto& list = it->second;
		for (auto& job : list)
		{
			if (job->slice == sliceIndex && job->mip == mipIndex)
				Cancel(job);
		}
		list.erase(std::remove_if(list.begin(), list.end(), [](const JobPtr& j) { return j->cancelled.load(std::memory_order_relaxed); }), list.end());
		if (list.empty())
			s_byTexture.erase(it);
	}

	void ApplyFinished()
	{
		if (s_doneCount.load(std::memory_order_acquire) == 0)
			return;
		std::vector<JobPtr> done;
		{
			std::lock_guard lock(s_mutex);
			done.swap(s_done);
			s_doneCount.store(0, std::memory_order_relaxed);
		}
		for (auto& job : done)
		{
			Uncount(*job);
			if (job->applied || job->cancelled.load(std::memory_order_acquire))
			{
				job->data.reset();
				continue;
			}
			RemoveFromTexture(job);
			Apply(job);
		}
	}

	void Finish(LatteTexture* tex)
	{
		if (s_byTexture.empty())
			return;
		auto it = s_byTexture.find(tex);
		if (it == s_byTexture.end())
			return;
		std::vector<JobPtr> jobs = std::move(it->second);
		s_byTexture.erase(it);
		PerfTelemetry::ScopedTimer timer(PerfTelemetry::Get().rtAsyncTexWaitNs);
		PerfTelemetry::Get().asyncTexWaits.fetch_add(1, std::memory_order_relaxed);
		for (auto& job : jobs)
		{
			if (job->cancelled.load(std::memory_order_acquire))
				continue;
			uint32 expected = kQueued;
			if (job->state.compare_exchange_strong(expected, kRunning))
			{
				// Not started yet: convert it here instead of waiting for a free worker.
				s_queuedSlices.fetch_sub(1, std::memory_order_relaxed);
				Decode(*job);
				job->state.store(kDone, std::memory_order_release);
			}
			else
			{
				std::unique_lock lock(s_mutex);
				s_doneCv.wait(lock, [&] { return job->state.load(std::memory_order_acquire) == kDone; });
			}
			Apply(job);
		}
	}

	void Forget(LatteTexture* tex)
	{
		if (s_byTexture.empty())
			return;
		auto it = s_byTexture.find(tex);
		if (it == s_byTexture.end())
			return;
		std::vector<JobPtr> jobs = std::move(it->second);
		s_byTexture.erase(it);
		for (auto& job : jobs)
		{
			Cancel(job);
			// A worker may be reading the texture's guest memory right now. The memory can go away with the
			// texture (title stop), so wait for that one conversion to end.
			if (job->state.load(std::memory_order_acquire) == kRunning)
			{
				std::unique_lock lock(s_mutex);
				s_doneCv.wait(lock, [&] { return job->state.load(std::memory_order_acquire) != kRunning; });
			}
		}
	}

	SyncScope::SyncScope() { s_syncDepth++; }
	SyncScope::~SyncScope() { s_syncDepth--; }

	void Shutdown()
	{
		{
			std::lock_guard lock(s_mutex);
			s_stop = true;
			s_queue.clear();
		}
		s_workCv.notify_all();
		for (auto& worker : s_workers)
			worker.join();
		s_workers.clear();
		{
			std::lock_guard lock(s_mutex);
			s_done.clear();
			s_doneCount.store(0);
			s_stop = false;
		}
		s_byTexture.clear();
		s_convertedBytes.store(0);
		s_queuedSlices.store(0);
	}
}
