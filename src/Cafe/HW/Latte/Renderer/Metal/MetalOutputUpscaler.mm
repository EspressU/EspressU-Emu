#include "Cafe/HW/Latte/Renderer/Metal/MetalOutputUpscaler.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalFaultHistory.h"
#include "Cafe/HW/Latte/Core/HostTuning.h"

#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

#include <string>

namespace
{
	struct Target
	{
		id scaler = nil;
		id<MTLTexture> texture = nil;
		NSUInteger inputWidth = 0;
		NSUInteger inputHeight = 0;
		NSUInteger outputWidth = 0;
		NSUInteger outputHeight = 0;
		MTLPixelFormat format = MTLPixelFormatInvalid;
		// Set when the scaler could not be built for this exact configuration, so it is not retried every frame.
		bool unusable = false;

		bool Matches(id<MTLTexture> source, NSUInteger width, NSUInteger height) const
		{
			return inputWidth == source.width && inputHeight == source.height && outputWidth == width && outputHeight == height && format == source.pixelFormat;
		}
	};

	bool IsScalableFormat(MTLPixelFormat format)
	{
		switch (format)
		{
		case MTLPixelFormatRGBA8Unorm:
		case MTLPixelFormatRGBA8Unorm_sRGB:
		case MTLPixelFormatBGRA8Unorm:
		case MTLPixelFormatBGRA8Unorm_sRGB:
		case MTLPixelFormatRGB10A2Unorm:
		case MTLPixelFormatRGBA16Float:
			return true;
		default:
			return false;
		}
	}

	bool IsFloatFormat(MTLPixelFormat format)
	{
		return format == MTLPixelFormatRGBA16Float;
	}
}

struct MetalOutputUpscaler::State
{
	id<MTLDevice> device = nil;
	Target targets[OutputCount];
};

MetalOutputUpscaler::MetalOutputUpscaler(MTL::Device* device)
	: m_state(std::make_unique<State>())
{
	m_state->device = (__bridge id<MTLDevice>)device;
}

MetalOutputUpscaler::~MetalOutputUpscaler() = default;

bool MetalOutputUpscaler::IsAvailable(MTL::Device* device)
{
	if (@available(iOS 16.0, macOS 13.0, *))
		return device && [MTLFXSpatialScalerDescriptor supportsDevice:(__bridge id<MTLDevice>)device];
	return false;
}

void MetalOutputUpscaler::Release()
{
	for (Target& target : m_state->targets)
		target = Target{};
}

MTL::Texture* MetalOutputUpscaler::Upscale(Output output, MTL::CommandBuffer* commandBuffer, MTL::Texture* source, uint32_t outputWidth, uint32_t outputHeight)
{
	if (@available(iOS 16.0, macOS 13.0, *))
	{
		id<MTLTexture> input = (__bridge id<MTLTexture>)source;
		id<MTLCommandBuffer> commands = (__bridge id<MTLCommandBuffer>)commandBuffer;
		if (output >= OutputCount || !input || !commands || input.textureType != MTLTextureType2D || !IsScalableFormat(input.pixelFormat))
			return nullptr;
		// Only enlarging is worth a pass; shrinking stays with the regular output filter.
		if (outputWidth <= input.width || outputHeight <= input.height || outputWidth > 16384 || outputHeight > 16384)
			return nullptr;

		Target& target = m_state->targets[output];
		if (!target.Matches(input, outputWidth, outputHeight))
		{
			target = Target{};
			target.inputWidth = input.width;
			target.inputHeight = input.height;
			target.outputWidth = outputWidth;
			target.outputHeight = outputHeight;
			target.format = input.pixelFormat;

			MTLFXSpatialScalerDescriptor* descriptor = [MTLFXSpatialScalerDescriptor new];
			descriptor.colorTextureFormat = input.pixelFormat;
			descriptor.outputTextureFormat = input.pixelFormat;
			descriptor.inputWidth = input.width;
			descriptor.inputHeight = input.height;
			descriptor.outputWidth = outputWidth;
			descriptor.outputHeight = outputHeight;
			descriptor.colorProcessingMode = IsFloatFormat(input.pixelFormat) ? MTLFXSpatialScalerColorProcessingModeLinear
			                                                                  : MTLFXSpatialScalerColorProcessingModePerceptual;
			id<MTLFXSpatialScaler> scaler = [descriptor newSpatialScalerWithDevice:m_state->device];
			if (scaler)
			{
				MTLTextureDescriptor* textureDescriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:input.pixelFormat
				                                                                                             width:outputWidth
				                                                                                            height:outputHeight
				                                                                                         mipmapped:NO];
				textureDescriptor.usage = scaler.outputTextureUsage | MTLTextureUsageShaderRead;
				textureDescriptor.storageMode = MTLStorageModePrivate;
				target.texture = [m_state->device newTextureWithDescriptor:textureDescriptor];
				if (!target.texture)
				{
					MetalFaultHistory::Record(MetalFaultHistory::Kind::Texture, (uint64_t)outputWidth * outputHeight * 4,
						std::string("MetalFX output for the ") + (output == TV ? "TV" : "GamePad"));
					scaler = nil;
				}
			}
			target.scaler = scaler;
			target.unusable = (scaler == nil);
			HostTuning::Count(target.unusable ? HostTuning::GetStats().fxFailures : HostTuning::GetStats().fxRebuilds);
		}
		if (target.unusable)
			return nullptr;

		id<MTLFXSpatialScaler> scaler = target.scaler;
		scaler.colorTexture = input;
		scaler.outputTexture = target.texture;
		scaler.inputContentWidth = input.width;
		scaler.inputContentHeight = input.height;
		[scaler encodeToCommandBuffer:commands];
		return (__bridge MTL::Texture*)target.texture;
	}
	return nullptr;
}
