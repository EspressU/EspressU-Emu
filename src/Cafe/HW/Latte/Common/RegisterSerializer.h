#pragma once
#include "util/helpers/Serializer.h"

namespace Latte
{
	struct GPUCompactedRegisterState
	{
		static constexpr int const NUM_REGS = 1854;

		uint32 rawArray[NUM_REGS];
	};

	void StoreGPURegisterState(const LatteContextRegister& contextRegister, GPUCompactedRegisterState& registerStateOut);
	void LoadGPURegisterState(LatteContextRegister& contextRegisterOut, const GPUCompactedRegisterState& registerState);

	void SerializeRegisterState(GPUCompactedRegisterState& regState, MemStreamWriter& memWriter);
	bool DeserializeRegisterState(GPUCompactedRegisterState& regState, MemStreamReader& memReader);
}