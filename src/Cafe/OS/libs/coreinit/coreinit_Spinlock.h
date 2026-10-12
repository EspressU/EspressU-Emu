#pragma once

namespace coreinit
{
	struct OSSpinLock
	{
		 MEMPTR<struct OSThread_t> ownerThread;
		 MEMPTR<void> userData;
		 uint32be count;
		 uint32be interruptMask;
	};

	static_assert(sizeof(OSSpinLock) == 0x10);

	void InitializeSpinlock();

	void OSInitSpinLock(OSSpinLock* spinlock);

	bool OSAcquireSpinLock(OSSpinLock* spinlock);
	bool OSTryAcquireSpinLock(OSSpinLock* spinlock);
	bool OSTryAcquireSpinLockWithTimeout(OSSpinLock* spinlock, uint64 timeout);
	bool OSReleaseSpinLock(OSSpinLock* spinlock);

	bool OSUninterruptibleSpinLock_Acquire(OSSpinLock* spinlock);
	bool OSUninterruptibleSpinLock_TryAcquire(OSSpinLock* spinlock);
	bool OSUninterruptibleSpinLock_TryAcquireWithTimeout(OSSpinLock* spinlock, uint64 timeout);
	bool OSUninterruptibleSpinLock_Release(OSSpinLock* spinlock);
}