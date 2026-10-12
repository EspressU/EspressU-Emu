#pragma once
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"

namespace coreinit
{
	enum class SysMessageId : uint32
	{
		MsgAcquireForeground = 0xFACEF000,
		MsgReleaseForeground = 0xFACEBACC,
		MsgExit = 0xD1E0D1E0,
		HomeButtonDenied = 0xCCC0FFEE,
		NetIoStartOrStop = 0xAAC0FFEE,
	};

	struct OSMessage
	{
		uint32be		message;
		uint32be		data0;
		uint32be		data1;
		uint32be		data2;
	};

	struct OSMessageQueue
	{
		 uint32be				magic;
		 MEMPTR<void>			userData;
		 uint32be				ukn08;
		 OSThreadQueue			threadQueueSend;
		 OSThreadQueue			threadQueueReceive;
		 MEMPTR<OSMessage>		msgArray;
		 uint32be				msgCount;
		 uint32be				firstIndex;
		 uint32be				usedCount;
	};

	static_assert(sizeof(OSMessageQueue) == 0x3C);

	#define OS_MESSAGE_BLOCK				1
	#define OS_MESSAGE_HIGH_PRIORITY		2

	void OSInitMessageQueueEx(OSMessageQueue* msgQueue, OSMessage* msgArray, uint32 msgCount, void* userData);
	void OSInitMessageQueue(OSMessageQueue* msgQueue, OSMessage* msgArray, uint32 msgCount);
	bool OSReceiveMessage(OSMessageQueue* msgQueue, OSMessage* msg, uint32 flags);
	bool OSPeekMessage(OSMessageQueue* msgQueue, OSMessage* msg);
	sint32 OSSendMessage(OSMessageQueue* msgQueue, OSMessage* msg, uint32 flags);

	OSMessageQueue* OSGetSystemMessageQueue();

	void InitializeMessageQueue();
};