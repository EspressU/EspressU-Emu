#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"
#include "Cafe/HW/Latte/Core/Latte.h"

#include "config/LaunchSettings.h"

LatteTextureGL::LatteTextureGL(Latte::E_DIM dim, MPTR physAddress, MPTR physMipAddress, Latte::E_GX2SURFFMT format, uint32 width, uint32 height, uint32 depth, uint32 pitch, uint32 mipLevels, uint32 swizzle,
	Latte::E_HWTILEMODE tileMode, bool isDepth, bool isRenderTarget)
	: LatteTexture(dim, physAddress, physMipAddress, format, width, height, depth, pitch, mipLevels, swizzle, tileMode, isDepth, isRenderTarget)
{
	GenerateEmptyTextureFromGX2Dim(dim, this->glId_texture, this->glTexTarget, true);
	FormatInfoGL glFormatInfo;
	GetOpenGLFormatInfo(isDepth, overwriteInfo.hasFormatOverwrite ? (Latte::E_GX2SURFFMT)overwriteInfo.format : format, dim, &glFormatInfo);
	this->glInternalFormat = glFormatInfo.glInternalFormat;
	this->isAlternativeFormat = glFormatInfo.isUsingAlternativeFormat;
	bool useGLDebugNames = false;
#ifdef CEMU_DEBUG_ASSERT
	useGLDebugNames = true;
#endif
	if (LaunchSettings::NSightModeEnabled())
		useGLDebugNames = true;
	if (useGLDebugNames)
	{
		char textureDebugLabel[512];
		sprintf(textureDebugLabel, "%08x_f%04x%s_p%04x_%dx%d", physAddress, (uint32)format, this->isDepth ? "_d" : "", pitch, width, height);
		glObjectLabel(GL_TEXTURE, this->glId_texture, -1, textureDebugLabel);
	}
}

LatteTextureGL::~LatteTextureGL()
{
	glDeleteTextures(1, &glId_texture);
	catchOpenGLError();
}

void LatteTextureGL::GenerateEmptyTextureFromGX2Dim(Latte::E_DIM dim, GLuint& texId, GLint& texTarget, bool createForTargetType)
{
	if (dim == Latte::E_DIM::DIM_2D)
		texTarget = GL_TEXTURE_2D;
	else if (dim == Latte::E_DIM::DIM_1D)
		texTarget = GL_TEXTURE_1D;
	else if (dim == Latte::E_DIM::DIM_3D)
		texTarget = GL_TEXTURE_3D;
	else if (dim == Latte::E_DIM::DIM_2D_ARRAY)
		texTarget = GL_TEXTURE_2D_ARRAY;
	else if (dim == Latte::E_DIM::DIM_CUBEMAP)
		texTarget = GL_TEXTURE_CUBE_MAP_ARRAY;
	else if (dim == Latte::E_DIM::DIM_2D_MSAA)
		texTarget = GL_TEXTURE_2D;
	else
	{
		cemu_assert_unimplemented();
	}
	if(createForTargetType)
		texId = glCreateTextureWrapper(texTarget);
	else
		glGenTextures(1, &texId);
}

LatteTextureView* LatteTextureGL::CreateView(Latte::E_DIM dim, Latte::E_GX2SURFFMT format, sint32 firstMip, sint32 mipCount, sint32 firstSlice, sint32 sliceCount)
{
	return new LatteTextureViewGL(this, dim, format, firstMip, mipCount, firstSlice, sliceCount);
}

void LatteTextureGL::GetOpenGLFormatInfo(bool isDepth, Latte::E_GX2SURFFMT format, Latte::E_DIM dim, FormatInfoGL* formatInfoOut)
{
	formatInfoOut->isUsingAlternativeFormat = false;

	if (isDepth)
	{
		if (format == Latte::E_GX2SURFFMT::D24_S8_UNORM)
		{
			formatInfoOut->setFormat(GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D24_S8_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV);
			formatInfoOut->markAsAlternativeFormat();
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D32_S8_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D32_FLOAT)
		{
			formatInfoOut->setFormat(GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT);
			return;
		}
		else if (format == Latte::E_GX2SURFFMT::D16_UNORM)
		{
			formatInfoOut->setFormat(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);
			return;
		}
		cemuLog_log(LogType::Force, "OpenGL: Unsupported texture depth format 0x{:04x}", (uint32)format);
		formatInfoOut->setFormat(GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT);
		formatInfoOut->markAsAlternativeFormat();
		return;
	}

	bool glIsCompressed = false;
	bool isUsingAlternativeFormat = false;
	sint32 glInternalFormat;
	sint32 glSuppliedFormat;
	sint32 glSuppliedFormatType;
	if (format == Latte::E_GX2SURFFMT::R4_G4_UNORM)
	{
		formatInfoOut->setFormat(GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
		formatInfoOut->markAsAlternativeFormat();
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R4_G4_B4_A4_UNORM)
	{
		formatInfoOut->setFormat(GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_FLOAT)
	{
		formatInfoOut->setFormat(GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_FLOAT)
	{
		formatInfoOut->setFormat(GL_RG16F, GL_RG, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_SNORM)
	{
		formatInfoOut->setFormat(GL_R16_SNORM, GL_RED, GL_SHORT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_FLOAT)
	{
		formatInfoOut->setFormat(GL_R16F, GL_RED, GL_HALF_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC1_UNORM ||
		format == Latte::E_GX2SURFFMT::BC1_SRGB)
	{
		if (format == Latte::E_GX2SURFFMT::BC1_SRGB)
			formatInfoOut->setCompressed(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT, -1, -1);
		else
			formatInfoOut->setCompressed(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, -1, -1);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC2_UNORM || format == Latte::E_GX2SURFFMT::BC2_SRGB)
	{
		formatInfoOut->setFormat(GL_RGBA16F, GL_RGBA, GL_FLOAT);
		formatInfoOut->markAsAlternativeFormat();
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC3_UNORM || format == Latte::E_GX2SURFFMT::BC3_SRGB)
	{
		if (format == Latte::E_GX2SURFFMT::BC3_SRGB)
			formatInfoOut->setCompressed(GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, -1, -1);
		else
			formatInfoOut->setCompressed(GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, -1, -1);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::BC4_UNORM || format == Latte::E_GX2SURFFMT::BC4_SNORM)
	{
		bool allowCompressed = true;
		if (dim != Latte::E_DIM::DIM_2D && dim != Latte::E_DIM::DIM_2D_ARRAY)
			allowCompressed = false;
		if (allowCompressed)
		{
			if (format == Latte::E_GX2SURFFMT::BC4_UNORM)
				formatInfoOut->setCompressed(GL_COMPRESSED_RED_RGTC1, -1, -1);
			else
				formatInfoOut->setCompressed(GL_COMPRESSED_SIGNED_RED_RGTC1, -1, -1);
			return;
		}
		else
		{
			formatInfoOut->setFormat(GL_RG16F, GL_RG, GL_FLOAT);
			formatInfoOut->markAsAlternativeFormat();
			return;
		}
	}
	else if (format == Latte::E_GX2SURFFMT::BC5_UNORM || format == Latte::E_GX2SURFFMT::BC5_SNORM)
	{
		if (format == Latte::E_GX2SURFFMT::BC5_SNORM)
			formatInfoOut->setCompressed(GL_COMPRESSED_SIGNED_RG_RGTC2, -1, -1);
		else
			formatInfoOut->setCompressed(GL_COMPRESSED_RG_RGTC2, -1, -1);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_FLOAT)
	{
		formatInfoOut->setFormat(GL_R32F, GL_RED, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_FLOAT)
	{
		formatInfoOut->setFormat(GL_RG32F, GL_RG, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_UINT)
	{
		formatInfoOut->setFormat(GL_RG32UI, GL_RG_INTEGER, GL_UNSIGNED_INT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_UINT)
	{
		formatInfoOut->setFormat(GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_UINT)
	{
		formatInfoOut->setFormat(GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_UINT)
	{
		formatInfoOut->setFormat(GL_R8UI, GL_RED_INTEGER, GL_UNSIGNED_BYTE);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_FLOAT)
	{
		formatInfoOut->setFormat(GL_RGBA32F, GL_RGBA, GL_FLOAT);
		return;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UNORM || format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
	{
		if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SRGB)
			glInternalFormat = GL_SRGB8_ALPHA8;
		else
			glInternalFormat = GL_RGBA8;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_SNORM)
	{
		glInternalFormat = GL_RGBA8_SNORM;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_UNORM)
	{
		glInternalFormat = GL_R8;
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_SNORM)
	{
		glInternalFormat = GL_R8_SNORM;
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_UNORM)
	{
		glInternalFormat = GL_RG8;
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_SNORM)
	{
		glInternalFormat = GL_RG8_SNORM;
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_UNORM)
	{
		glInternalFormat = GL_R16;
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UNORM)
	{
		glInternalFormat = GL_RGBA16;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_SNORM)
	{
		glInternalFormat = GL_RGBA16_SNORM;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_UNORM)
	{
		glInternalFormat = GL_RG16;
		glSuppliedFormat = GL_RG;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R5_G6_B5_UNORM)
	{
		glInternalFormat = GL_RGB565;
		glSuppliedFormat = GL_RGB;
		glSuppliedFormatType = GL_UNSIGNED_SHORT_5_6_5_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R5_G5_B5_A1_UNORM)
	{
		glInternalFormat = GL_RGB5_A1;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT_5_5_5_1;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::A1_B5_G5_R5_UNORM)
	{
		glInternalFormat = GL_RGB5_A1;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_SHORT_5_5_5_1;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_UNORM)
	{
		glInternalFormat = GL_RGB10_A2;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_2_10_10_10_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SRGB)
	{
		glInternalFormat = GL_RGB10_A2;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_2_10_10_10_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::A2_B10_G10_R10_UNORM)
	{
		glInternalFormat = GL_RGB10_A2;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_UNSIGNED_INT_10_10_10_2;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R10_G10_B10_A2_SNORM)
	{
		glInternalFormat = GL_RGBA16_SNORM;
		isUsingAlternativeFormat = true;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R11_G11_B10_FLOAT)
	{
		glInternalFormat = GL_R11F_G11F_B10F;
		glSuppliedFormat = GL_RGB;
		glSuppliedFormatType = GL_UNSIGNED_INT_10F_11F_11F_REV;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_G32_B32_A32_UINT)
	{
		glInternalFormat = GL_RGBA32UI;
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_INT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R16_G16_B16_A16_UINT)
	{
		glInternalFormat = GL_RGBA16UI;
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_SHORT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R8_G8_B8_A8_UINT)
	{
		glInternalFormat = GL_RGBA8UI;
		glSuppliedFormat = GL_RGBA_INTEGER;
		glSuppliedFormatType = GL_UNSIGNED_BYTE;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R24_X8_UNORM)
	{
		glInternalFormat = GL_R32F;
		isUsingAlternativeFormat = true;
		glSuppliedFormat = GL_RED;
		glSuppliedFormatType = GL_FLOAT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::X24_G8_UINT)
	{

		glInternalFormat = GL_RGBA8UI;
		isUsingAlternativeFormat = true;
		glSuppliedFormat = GL_RGBA;
		glSuppliedFormatType = GL_FLOAT;
		glIsCompressed = false;
	}
	else if (format == Latte::E_GX2SURFFMT::R32_X8_FLOAT)
	{
		glInternalFormat = GL_DEPTH32F_STENCIL8;
		isUsingAlternativeFormat = false;
		glSuppliedFormat = GL_DEPTH_STENCIL;
		glSuppliedFormatType = GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
		glIsCompressed = false;
		cemu_assert_debug(false);
	}
	else
	{
		cemuLog_log(LogType::Force, "OpenGL: Unsupported texture format 0x{:04x}", (uint32)format);
		cemu_assert_unimplemented();
	}
	formatInfoOut->glInternalFormat = glInternalFormat;
	formatInfoOut->glSuppliedFormat = glSuppliedFormat;
	formatInfoOut->glSuppliedFormatType = glSuppliedFormatType;
	formatInfoOut->glIsCompressed = glIsCompressed;
	formatInfoOut->isUsingAlternativeFormat = isUsingAlternativeFormat;
}

void LatteTextureGL::AllocateOnHost()
{
	auto hostTexture = this;
	cemu_assert_debug(hostTexture->isDataDefined == false);
	sint32 effectiveBaseWidth = hostTexture->width;
	sint32 effectiveBaseHeight = hostTexture->height;
	sint32 effectiveBaseDepth = hostTexture->depth;
	if (hostTexture->overwriteInfo.hasResolutionOverwrite)
	{
		effectiveBaseWidth = hostTexture->overwriteInfo.width;
		effectiveBaseHeight = hostTexture->overwriteInfo.height;
		effectiveBaseDepth = hostTexture->overwriteInfo.depth;
	}
	sint32 mipLevels = std::min(hostTexture->mipLevels, hostTexture->maxPossibleMipLevels);
	mipLevels = std::max(mipLevels, 1);
	if (hostTexture->dim == Latte::E_DIM::DIM_2D || hostTexture->dim == Latte::E_DIM::DIM_2D_MSAA)
	{
		cemu_assert_debug(effectiveBaseDepth == 1);
		glTextureStorage2DWrapper(GL_TEXTURE_2D, hostTexture->glId_texture, mipLevels, hostTexture->glInternalFormat, effectiveBaseWidth, effectiveBaseHeight);
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_1D)
	{
		cemu_assert_debug(effectiveBaseHeight == 1);
		cemu_assert_debug(effectiveBaseDepth == 1);
		glTextureStorage1DWrapper(GL_TEXTURE_1D, hostTexture->glId_texture, mipLevels, hostTexture->glInternalFormat, effectiveBaseWidth);
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_2D_ARRAY || hostTexture->dim == Latte::E_DIM::DIM_2D_ARRAY_MSAA)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_2D_ARRAY, hostTexture->glId_texture, mipLevels, hostTexture->glInternalFormat, effectiveBaseWidth, effectiveBaseHeight, std::max(1, effectiveBaseDepth));
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_3D)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_3D, hostTexture->glId_texture, mipLevels, hostTexture->glInternalFormat, effectiveBaseWidth, effectiveBaseHeight, std::max(1, effectiveBaseDepth));
	}
	else if (hostTexture->dim == Latte::E_DIM::DIM_CUBEMAP)
	{
		glTextureStorage3DWrapper(GL_TEXTURE_CUBE_MAP_ARRAY, hostTexture->glId_texture, mipLevels, hostTexture->glInternalFormat, effectiveBaseWidth, effectiveBaseHeight, effectiveBaseDepth);
	}
	else
	{
		cemu_assert_unimplemented();
	}
}
