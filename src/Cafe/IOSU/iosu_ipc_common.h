#pragma once

using IOSDevHandle = uint32;

enum class IPCDriverState : uint32
{
	CLOSED = 1,
	INITIALIZED = 2,
	READY = 3,
	SUBMITTING = 4
};

enum class IPCCommandId : uint32
{
	IOS_OPEN = 1,
	IOS_CLOSE = 2,

	IOS_IOCTL = 6,
	IOS_IOCTLV = 7,
};

struct IPCIoctlVector
{
	MEMPTR<void> baseVirt;
	uint32be size;
	MEMPTR<void> basePhys;
};

static_assert(sizeof(IPCIoctlVector) == 0xC);

struct IPCCommandBody
{
	 betype<IPCCommandId> cmdId;
	 uint32be result;
	 betype<IOSDevHandle> devHandle;
	 uint32be ukn0C;
	 uint32be ukn10;
	 uint32be ukn14;
	 uint32be ukn18;
	 uint32be ukn1C;
	 uint32be ukn20;
	 uint32be args[5];
	 betype<IPCCommandId> prev_cmdId;
	 betype<IOSDevHandle> prev_devHandle;
	 MEMPTR<void> ppcVirt0;
	 MEMPTR<void> ppcVirt1;

};
