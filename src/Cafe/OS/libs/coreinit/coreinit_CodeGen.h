#pragma once

struct PPCInterpreter_t;

namespace coreinit
{
	void OSGetCodegenVirtAddrRangeInternal(uint32& rangeStart, uint32& rangeSize);
	void codeGenHandleICBI(uint32 ea);
	void codeGenHandleICBI(PPCInterpreter_t* hCPU, uint32 ea);
	bool codeGenShouldAvoid();

	void InitializeCodeGen();
}
