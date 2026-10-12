#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureReadbackMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteTextureMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/LatteToMtl.h"

LatteTextureReadbackInfoMtl::~LatteTextureReadbackInfoMtl()
{
    if (m_commandBuffer)
        m_commandBuffer->release();
}

void LatteTextureReadbackInfoMtl::StartTransfer()
{
	cemu_assert(m_textureView);

	auto* baseTexture = (LatteTextureMtl*)m_textureView->baseTexture;

	cemu_assert_debug(m_textureView->firstMip == 0);
	cemu_assert_debug(m_textureView->baseTexture->dim != Latte::E_DIM::DIM_3D);

	size_t bytesPerRow = GetMtlTextureBytesPerRow(baseTexture->format, baseTexture->isDepth, baseTexture->width);
	m_rowPitch = static_cast<uint32>(bytesPerRow);
	size_t bytesPerImage = GetMtlTextureBytesPerImage(baseTexture->format, baseTexture->isDepth, baseTexture->height, bytesPerRow);

	auto blitCommandEncoder = m_mtlr->GetBlitCommandEncoder(PerfTelemetry::kBreakReadback);

	blitCommandEncoder->copyFromTexture(baseTexture->GetTexture(), m_firstSlice, 0, MTL::Origin{0, 0, 0}, MTL::Size{(uint32)baseTexture->width, (uint32)baseTexture->height, 1}, m_mtlr->GetTextureReadbackBuffer(), m_bufferOffset, bytesPerRow, bytesPerImage);

	m_commandBuffer = m_mtlr->GetCurrentCommandBuffer()->retain();
	m_mtlr->CommitCommandBuffer();
}

bool LatteTextureReadbackInfoMtl::IsFinished()
{

    return CommandBufferCompleted(m_commandBuffer);
}

void LatteTextureReadbackInfoMtl::ForceFinish()
{
    WaitForCommandBuffer(m_commandBuffer, "texture readback: waiting for the copy to finish");
}

uint8* LatteTextureReadbackInfoMtl::GetData()
{
	return (uint8*)m_mtlr->GetTextureReadbackBuffer()->contents() + m_bufferOffset;
}
