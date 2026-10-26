#pragma once

#include <cstdint>
#include <memory>

namespace MTL
{
	class CommandBuffer;
	class Device;
	class Texture;
}

// Enlarges the finished TV or GamePad image with the MetalFX spatial scaler before it is drawn into
// the drawable. Each output keeps its own scaler and target texture, built again only when the
// input size, output size or pixel format changes. Returns nullptr whenever MetalFX cannot be used,
// so the caller keeps drawing with its regular output shader.
class MetalOutputUpscaler
{
public:
	enum Output : uint32_t
	{
		TV = 0,
		GamePad = 1,
		OutputCount
	};

	explicit MetalOutputUpscaler(MTL::Device* device);
	~MetalOutputUpscaler();

	MetalOutputUpscaler(const MetalOutputUpscaler&) = delete;
	MetalOutputUpscaler& operator=(const MetalOutputUpscaler&) = delete;

	static bool IsAvailable(MTL::Device* device);

	// Encodes the scaler into commandBuffer, which must have no open encoder.
	MTL::Texture* Upscale(Output output, MTL::CommandBuffer* commandBuffer, MTL::Texture* source, uint32_t outputWidth, uint32_t outputHeight);

	// Drops the scalers and their target textures; the next Upscale builds them again.
	void Release();

private:
	struct State;
	std::unique_ptr<State> m_state;
};
