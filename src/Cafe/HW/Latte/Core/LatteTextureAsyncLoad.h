// Copyright (c) 2026 EspressU (https://espressu.dev). EspressU Licence: non-commercial use only.
// Anything that contains or is derived from this file must be fully open source, keep this file
// under the EspressU Licence, keep this notice in every copy, and credit EspressU (https://espressu.dev).
// Full text: LICENSES/EspressU-Licence.txt. Not covered by MPL-2.0.

#pragma once

#include "Cafe/HW/Latte/Core/LatteTextureLoader.h"

class LatteTexture;
class TextureDecoder;

// Loads textures whose format the GPU cannot sample directly (BC1 to BC5 on GPUs without BC support, converted
// to ASTC on the CPU) on background threads instead of making the render thread wait.
//
// Converting such a texture takes up to several milliseconds, and a game that streams a new area needs
// thousands of them at once, which used to freeze single frames for up to a second. Now the render thread
// queues the slice and keeps drawing: a texture that is loaded again keeps its old picture until the new one is
// ready, a brand-new texture shows its average colour for a few frames. Finished slices are uploaded on the
// render thread before the next draw.
//
// Order stays correct: anything that writes into a texture or reads it on the GPU other than sampling (copies,
// clears, readbacks) first waits for that texture's pending slices, and a newer load of a slice replaces an
// older pending one. All functions are for the render thread only.
namespace LatteTextureAsync
{
	// Queues the slice if async loading is on and the texture qualifies. False: load it synchronously.
	bool TryQueue(LatteTexture* tex, const LatteTextureLoaderCtx& loader, TextureDecoder* decoder, uint32 sliceIndex, uint32 mipIndex, uint32 depth, uint32 mipLevels, uint32 imageSize);
	// The slice is being loaded synchronously: drop a pending older load so it cannot overwrite the new data.
	void CancelSlice(LatteTexture* tex, uint32 sliceIndex, uint32 mipIndex);
	// Uploads slices the workers have finished. Cheap when there are none.
	void ApplyFinished();
	// Loads all pending slices of this texture now (waits for the workers or converts them here).
	void Finish(LatteTexture* tex);
	// The texture is being deleted: forget its pending slices.
	void Forget(LatteTexture* tex);
	// Waits for the workers and drops everything (renderer shutdown).
	void Shutdown();

	// While one exists, TryQueue declines (the caller needs the data right away).
	struct SyncScope
	{
		SyncScope();
		~SyncScope();
		SyncScope(const SyncScope&) = delete;
		SyncScope& operator=(const SyncScope&) = delete;
	};
}
