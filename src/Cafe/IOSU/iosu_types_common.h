#pragma once

using IOSMsgQueueId = uint32;
using IOSTimerId = uint32;

static constexpr IOSTimerId IOSInvalidTimerId = 0xFFFFFFFF;

enum IOS_ERROR : sint32
{
	IOS_ERROR_OK = 0,
	IOS_ERROR_INVALID = -4,
	IOS_ERROR_MAXIMUM_REACHED = -5,

	IOS_ERROR_NONE_AVAILABLE = -7,
	IOS_ERROR_WOULD_BLOCK = -8,

	IOS_ERROR_INVALID_ARG = -29,
};

inline bool IOS_ResultIsError(const IOS_ERROR err)
{
	return (err & 0x80000000) != 0;
}

class IOSUModule
{
  public:
	virtual void SystemLaunch() {};
	virtual void SystemExit() {};
	virtual void TitleStart() {};
	virtual void TitleStop() {};
};
