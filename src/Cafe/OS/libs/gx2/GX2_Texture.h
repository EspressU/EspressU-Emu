#pragma once
#include "Cafe/HW/Latte/ISA/LatteReg.h"
#include "GX2_Surface.h"

namespace GX2
{
	struct GX2Texture
	{
		 GX2Surface surface;
		 uint32be viewFirstMip;
		 uint32be viewNumMips;
		 uint32be viewFirstSlice;
		 uint32be viewNumSlices;
		 uint32be compSel;
		 betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD0_N> regTexWord0;
		 betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD1_N> regTexWord1;
		 betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N> regTexWord4;
		 betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD5_N> regTexWord5;
		 betype<Latte::LATTE_SQ_TEX_RESOURCE_WORD6_N> regTexWord6;
	};

	static_assert(sizeof(GX2Texture) == 0x9C);

	struct GX2Sampler
	{
		betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD0_0> word0;
		betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD1_0> word1;
		betype<Latte::LATTE_SQ_TEX_SAMPLER_WORD2_0> word2;
	};

	static_assert(sizeof(GX2Sampler) == 12);

	void GX2InitTextureRegs(GX2Texture* texture);

	void GX2TextureInit();
};