#pragma once

#include <cstdint>
#include <memory>

namespace CA { class MetalLayer; }
namespace MTL
{
	class CommandBuffer;
	class Device;
	class Texture;
}

class MetalFrameGen
{
public:
	MetalFrameGen(MTL::Device* device, CA::MetalLayer* layer);
	~MetalFrameGen();

	MetalFrameGen(const MetalFrameGen&) = delete;
	MetalFrameGen& operator=(const MetalFrameGen&) = delete;

	// The render thread writes a composed TV image into this texture. When the
	// presenter owns the layer, a null texture means to skip this capture.
	MTL::Texture* BeginCapture(uint32_t width, uint32_t height, bool srgb);
	bool OwnsLayer() const;
	void SubmitCapture(MTL::CommandBuffer* commandBuffer);
	void RelieveMemoryPressure();

	struct Rates
	{
		double output = 0.0;
		double generated = 0.0;
	};
	static Rates DisplayRates();

private:
	void InvalidateHistory();
	struct State;
	std::unique_ptr<State> m_state;
};
