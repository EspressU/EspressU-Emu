#pragma once

namespace iosu
{
	namespace nim
	{
		struct titlePackageInfo_t
		{
			uint64 titleId;
			uint32be ukn08;
			uint8 ukn0C;
			uint8 ukn0D;
			uint8 ukn0E;
			uint8 ukn0F;
			uint8 ukn10;
			uint8 ukn11;
			uint8 ukn12;
			uint8 ukn13;
			uint8 ukn14[0xC];
			uint32be ukn20;
			uint32be ukn24;
			uint64 ukn28DLProgressRelatedMax_u64be;
			uint64 ukn30DLProgressRelatedCur_u64be;
			uint32be ukn38DLProgressRelatedMax;
			uint32be ukn3CDLProgressRelatedCur;
			uint32be ukn40;
			uint32be ukn44;
			uint8 ukn48;
			uint8 ukn49;
			uint8 ukn4A;
			uint8 ukn4B;
			uint32be ukn4C;
		};

		static_assert(sizeof(titlePackageInfo_t) == 0x50);

		struct iosuNimCemuRequest_t
		{
			uint32 requestCode;
			uint64 titleId;
			MEMPTR<void> ptr;
			MEMPTR<void> ptr2;
			sint32 maxCount;

			uint32 returnCode;
			union
			{
				struct
				{
					uint32 u32;
				}resultU32;
			};
		};

		#define IOSU_NIM_REQUEST_CEMU									(0xEE)

		#define IOSU_NIM_GET_ICON_DATABASE_ENTRY						0x01
		#define IOSU_NIM_GET_PACKAGE_COUNT								0x02
		#define IOSU_NIM_GET_PACKAGES_INFO								0x03
		#define IOSU_NIM_GET_PACKAGES_TITLEID							0x04

		void Initialize();
	}
}