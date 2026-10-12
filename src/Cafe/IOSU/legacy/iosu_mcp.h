
#pragma pack(1)

struct MCPTitleInfo
{

	 uint32be titleIdHigh;
	 uint32be titleIdLow;
	 uint32be appGroup;
	 char appPath[0x38];
	 uint32be appType;
	 uint16be titleVersion;
	 uint64be osVersion;
	 uint32be sdkVersion;
	 uint8 deviceName[10];
	 uint8 uknPadding;
};

static_assert(sizeof(MCPTitleInfo) == 0x61);

#pragma pack()

#define IOSU_MCP_REQUEST_CEMU (0xEE)

typedef struct
{
	uint32 requestCode;
	struct
	{
		MEMPTR<MCPTitleInfo> titleList;
		uint32be titleCount;
		uint32be titleListBufferSize;
		uint64 titleId;
		uint32be appType;
	}titleListRequest;

	uint32 returnCode;
	union
	{
		struct
		{
			uint32 u32;
		}resultU32;
	};
}iosuMcpCemuRequest_t;

#define IOSU_MCP_GET_TITLE_LIST								0x01
#define IOSU_MCP_GET_TITLE_LIST_BY_APP_TYPE					0x02
#define IOSU_MCP_GET_TITLE_INFO								0x03
#define IOSU_MCP_GET_TITLE_COUNT							0x04

namespace iosu
{
	sint32 mcpGetTitleCount();
	sint32 mcpGetTitleList(MCPTitleInfo* titleList, uint32 titleListBufferSize, uint32be* titleCount);
	void mcpResetTitleMounts();

	void iosuMcp_init();

	namespace mcp
	{
		void Init();
		void Shutdown();
	};
}
