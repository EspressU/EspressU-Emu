#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "util/highresolutiontimer/HighResolutionTimer.h"

class LatteTextureReadbackInfo
{
public:
	LatteTextureReadbackInfo(LatteTextureView* textureView, sint32 mipIndex = 0)
		: hostTextureCopy(textureView->baseTexture), m_firstSlice(textureView->firstSlice), m_firstMip(mipIndex), m_textureView(textureView)
	{}

	virtual ~LatteTextureReadbackInfo() = default;

	virtual void StartTransfer() = 0;
	virtual bool IsFinished() = 0;
	virtual void ForceFinish() {};

	virtual uint8* GetData() = 0;
	virtual void ReleaseData() {};

	HRTick transferStartTime;
	HRTick waitStartTime;
	bool forceFinish{ false };
	LatteTextureDefinition hostTextureCopy{};
	sint32 m_firstSlice{};
	sint32 m_firstMip{};
	uint32 m_rowPitch = 0;

protected:
	LatteTextureView* m_textureView;
	uint32 m_image_size = 0;
};
