#pragma once

#include <cstdint>
#include <memory>

namespace MTL
{
	class CommandBuffer;
	class Device;
	class Texture;
}
namespace CA
{
	class MetalLayer;
}

// Runs the last step of a frame, putting the finished image on screen, on a Metal 4 command queue
// while the game itself keeps rendering on the regular queue.
//
// The regular queue composes the TV or GamePad image (output shader and overlay) into an offscreen
// texture of the stage and signals a shared event. The Metal 4 queue waits for that event on the GPU,
// draws the texture into the drawable and presents it, so neither queue waits on the CPU for the other.
// Each output cycles through a few frame slots with their own command allocator, argument table and
// target texture, so several frames can be in flight. All stage resources live in one residency set.
//
// Experimental and not recommended yet: in tests games ran no faster with it, sometimes slower, and it needs
// more work before it should be switched on by default or advised to anyone.
//
// Everything here needs iOS 26 and a GPU of the Metal 4 family. Create() returns nullptr otherwise,
// and BeginFrame() returns nullptr whenever a frame cannot go this way; the caller then uses its
// regular drawable for that frame.
class Metal4OutputStage
{
public:
	enum Output : uint32_t
	{
		TV = 0,
		GamePad = 1,
		OutputCount
	};

	static bool IsSupported(MTL::Device* device);
	static std::unique_ptr<Metal4OutputStage> Create(MTL::Device* device);
	~Metal4OutputStage();

	Metal4OutputStage(const Metal4OutputStage&) = delete;
	Metal4OutputStage& operator=(const Metal4OutputStage&) = delete;

	// The texture the regular renderer composes this frame into, sized like the drawable. On nullptr, busy says
	// whether the stage only has no slot free right now (skip the frame) rather than failing (use the drawable).
	MTL::Texture* BeginFrame(Output output, uint32_t width, uint32_t height, uint32_t pixelFormat, bool& busy);

	// Hands the composed frame over. legacyCommandBuffer is the regular command buffer that wrote it; it must
	// not be committed yet and must have no open encoder. Returns false when the frame was dropped.
	bool PresentFrame(Output output, MTL::CommandBuffer* legacyCommandBuffer, CA::MetalLayer* layer, double minimumDuration);

	// GPU time of the last presented frame on the Metal 4 queue, from commit feedback; 0 before the first one.
	double LastGpuTimeMilliseconds() const;

	// Blocks until the Metal 4 queue has finished everything committed so far, at most timeoutMs.
	bool WaitIdle(uint32_t timeoutMs);

	struct State;

private:
	Metal4OutputStage();

	std::unique_ptr<State> m_state;
};
