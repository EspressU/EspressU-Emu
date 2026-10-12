#pragma once

namespace coreinit
{
	struct OSCoroutine
	{
		 uint32 lr;
		 uint32 cr;
		 uint32 gqr1;
		 uint32 r1;
		 uint32 r2;
		 uint32 r13;
		 uint32 gpr[18];
		uint64 fpr[18];
		uint64 psr[18];
	};

	static_assert(sizeof(OSCoroutine) == 0x180);

	void InitializeCoroutine();
}
