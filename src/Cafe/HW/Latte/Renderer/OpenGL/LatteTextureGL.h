#pragma once

#include "Cafe/HW/Latte/Core/LatteTexture.h"
#include "Common/GLInclude/GLInclude.h"

class LatteTextureGL : public LatteTexture
{
public:
	LatteTextureGL(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels,
		uint32 swizzle, Latte::E_HWTILEMODE tileMode, bool isDepth, bool isRenderTarget);

	~LatteTextureGL();

	void AllocateOnHost() override;

	static void GenerateEmptyTextureFromGX2Dim(Latte::E_DIM dim, GLuint& texId, GLint& texTarget, bool createForTargetType);

protected:
	LatteTextureView* CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount) override;

public:
	struct FormatInfoGL
	{
		sint32 glInternalFormat;
		sint32 glSuppliedFormat;
		sint32 glSuppliedFormatType;
		bool glIsCompressed;
		bool isUsingAlternativeFormat{};

		void setFormat(sint32 glInternalFormat, sint32 glSuppliedFormat, sint32 glSuppliedFormatType)
		{
			this->glInternalFormat = glInternalFormat;
			this->glSuppliedFormat = glSuppliedFormat;
			this->glSuppliedFormatType = glSuppliedFormatType;
			this->glIsCompressed = false;
		}

		void setCompressed(sint32 glInternalFormat, sint32 glSuppliedFormat, sint32 glSuppliedFormatType)
		{
			setFormat(glInternalFormat, glSuppliedFormat, glSuppliedFormatType);
			this->glIsCompressed = true;
		}

		void markAsAlternativeFormat()
		{
			this->isUsingAlternativeFormat = true;
		}
	};

	static void GetOpenGLFormatInfo(bool isDepth, Latte::E_GX2SURFFMT format, Latte::E_DIM dim, FormatInfoGL* formatInfoOut);

	GLuint glId_texture;
	GLint glTexTarget;
	GLint glInternalFormat = 0;
	bool isAlternativeFormat{};
};
