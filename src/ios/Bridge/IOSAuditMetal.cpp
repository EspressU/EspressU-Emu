#include "IOSAuditHooks.h"

#if defined(ESPRESSU_AUDIT_HOOKS)

#include "IOSAuditFrameStats.h"

#include <algorithm>

#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureViewMtl.h"

bool IOSAuditMetal_WantsCapture(bool tv)
{
	return cemu_audit_capture_wants(tv);
}

void IOSAuditMetal_CaptureView(MetalRenderer* renderer, LatteTextureView* texView, bool padView)
{
	const bool tv = !padView;
	if (!renderer || !texView || !texView->baseTexture)
	{
		cemu_audit_capture_deliver(tv, LatteGPUState.frameCounter, 0, 0, 0, -1, nullptr, 0, true);
		return;
	}

	auto texMtl = static_cast<LatteTextureMtl*>(texView->baseTexture);
	MTL::Texture* tex = texMtl->GetTexture();
	if (!tex)
	{
		cemu_audit_capture_deliver(tv, LatteGPUState.frameCounter, 0, 0, 0, -1, nullptr, 0, true);
		return;
	}

	sint32 width = 0, height = 0;
	texMtl->GetEffectiveSize(width, height, 0);
	width = std::min<sint32>(width, (sint32)tex->width());
	height = std::min<sint32>(height, (sint32)tex->height());
	const MTL::PixelFormat pixelFormat = tex->pixelFormat();

	int layout = -1;
	switch (pixelFormat)
	{
	case MTL::PixelFormatRGBA8Unorm:
	case MTL::PixelFormatRGBA8Unorm_sRGB:
		layout = (int)ios_audit::PixelLayout::RGBA8;
		break;
	case MTL::PixelFormatBGRA8Unorm:
	case MTL::PixelFormatBGRA8Unorm_sRGB:
		layout = (int)ios_audit::PixelLayout::BGRA8;
		break;
	case MTL::PixelFormatRGB10A2Unorm:
		layout = (int)ios_audit::PixelLayout::RGB10A2;
		break;
	default:
		break;
	}

	if (layout < 0 || width <= 0 || height <= 0)
	{
		cemu_audit_capture_deliver(tv, LatteGPUState.frameCounter, (uint32_t)std::max(width, 0), (uint32_t)std::max(height, 0),
		                           (uint32_t)pixelFormat, -1, nullptr, 0, false);
		return;
	}

	const uint32_t rowBytes = (uint32_t)width * 4u;
	const size_t size = (size_t)rowBytes * (size_t)height;

	MTL::Buffer* buffer = renderer->GetDevice()->newBuffer(size, MTL::ResourceStorageModeShared);
	if (!buffer)
	{
		cemu_audit_capture_deliver(tv, LatteGPUState.frameCounter, (uint32_t)width, (uint32_t)height,
		                           (uint32_t)pixelFormat, layout, nullptr, 0, true);
		return;
	}

	auto blit = renderer->GetBlitCommandEncoder();
	blit->copyFromTexture(tex, 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1), buffer, 0, rowBytes, 0);
	renderer->Flush(true);

	cemu_audit_capture_deliver(tv, LatteGPUState.frameCounter, (uint32_t)width, (uint32_t)height, (uint32_t)pixelFormat,
	                           layout, static_cast<const uint8_t*>(buffer->contents()), rowBytes, false);
	buffer->release();
}

#endif
