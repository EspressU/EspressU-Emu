#pragma once

// Objective-C++ only; used by Metal4OutputStage.mm.

#import <Metal/Metal.h>

#include <cstdint>
#include <memory>
#include <string>

// Builds the in-between image of two finished TV frames on the Metal 4 output queue.
//
// Motion is estimated on small luma copies of both frames (at most 320 pixels wide), one vector per
// 8x8 block, searched in both directions so each pixel can check the two estimates against each
// other. The in-between image is then either warped and blended by EspressU's own compute pass, or
// handed to Apple's MTL4FXFrameInterpolator together with the estimated motion.
class API_AVAILABLE(ios(26.0), macos(26.0)) Metal4FrameGenerator
{
public:
	enum class Backend : uint8_t
	{
		EspressU = 1,
		MetalFX = 2,
	};

	static std::unique_ptr<Metal4FrameGenerator> Create(id<MTLDevice> device, id<MTL4Compiler> compiler, id<MTLResidencySet> residency,
		Backend backend, std::string& whyNot);
	~Metal4FrameGenerator();

	Backend ActiveBackend() const;

	// Encodes everything needed for the image halfway between previous and current into commandBuffer and
	// returns it, or nil when this frame cannot get one. A non-empty whyDisabled means it never will again.
	id<MTLTexture> Encode(id<MTL4CommandBuffer> commandBuffer, id<MTLTexture> previous, id<MTLTexture> current,
		uint32_t slot, float frameSeconds, bool historyValid, std::string& whyDisabled);

private:
	Metal4FrameGenerator();

	struct Resources;
	std::unique_ptr<Resources> m_res;
};
