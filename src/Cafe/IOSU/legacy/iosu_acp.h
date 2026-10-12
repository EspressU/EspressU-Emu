#pragma once

#include "Cafe/IOSU/iosu_types_common.h"
#include "Cafe/OS/libs/nn_common.h"

typedef struct
{
	 uint64 title_id;
	 uint64 boss_id;
	 uint64 os_version;
	 uint64 app_size;
	 uint64 common_save_size;
	 uint64 account_save_size;
	 uint64 common_boss_size;
	 uint64 account_boss_size;
	 uint64 join_game_mode_mask;
	 uint32be version;
	 char product_code[0x20];
	 char content_platform[0x20];
	 char company_code[8];
	 char mastering_date[0x20];
	 uint32be logo_type;
	 uint32be app_launch_type;
	 uint32be invisible_flag;
	 uint32be no_managed_flag;
	 uint32be no_event_log;
	 uint32be no_icon_database;
	 uint32be launching_flag;
	 uint32be install_flag;
	 uint32be closing_msg;
	 uint32be title_version;
	 uint32be group_id;
	 uint32be save_no_rollback;
	 uint32be bg_daemon_enable;
	 uint32be join_game_id;
	 uint32be olv_accesskey;
	 uint32be wood_tin;
	 uint32be e_manual;
	 uint32be e_manual_version;
	 uint32be region;
	 uint32be pc_cero;
	 uint32be pc_esrb;
	 uint32be pc_bbfc;
	 uint32be pc_usk;
	 uint32be pc_pegi_gen;
	 uint32be pc_pegi_fin;
	 uint32be pc_pegi_prt;
	 uint32be pc_pegi_bbfc;
	 uint32be pc_cob;
	 uint32be pc_grb;
	 uint32be pc_cgsrr;
	 uint32be pc_oflc;
	 uint32be pc_reserved0;
	 uint32be pc_reserved1;
	 uint32be pc_reserved2;
	 uint32be pc_reserved3;
	 uint32be ext_dev_nunchaku;
	 uint32be ext_dev_classic;
	 uint32be ext_dev_urcc;
	 uint32be ext_dev_board;
	 uint32be ext_dev_usb_keyboard;
	 uint32be ext_dev_etc;
	 char ext_dev_etc_name[0x200];
	 uint32be eula_version;
	 uint32be drc_use;
	 uint32be network_use;
	 uint32be online_account_use;
	 uint32be direct_boot;
	 uint32be reserved_flag[8];
	 char     longname_ja[0x200];
	 char     longname_en[0x200];
	 char     longname_fr[0x200];
	 char     longname_de[0x200];
	 char     longname_it[0x200];
	 char     longname_es[0x200];
	 char     longname_zhs[0x200];
	 char     longname_ko[0x200];
	 char     longname_nl[0x200];
	 char     longname_pt[0x200];
	 char     longname_ru[0x200];
	 char     longname_zht[0x200];
	 char     shortname_ja[0x100];
	 char     shortname_en[0x100];
	 char     shortname_fr[0x100];
	 char     shortname_de[0x100];
	 char     shortname_it[0x100];
	 char     shortname_es[0x100];
	 char     shortname_zhs[0x100];
	 char     shortname_ko[0x100];
	 char     shortname_nl[0x100];
	 char     shortname_pt[0x100];
	 char     shortname_ru[0x100];
	 char     shortname_zht[0x100];
	 char     publisher_ja[0x100];
	 char     publisher_en[0x100];
	 char     publisher_fr[0x100];
	 char     publisher_de[0x100];
	 char     publisher_it[0x100];
	 char     publisher_es[0x100];
	 char     publisher_zhs[0x100];
	 char     publisher_ko[0x100];
	 char     publisher_nl[0x100];
	 char     publisher_pt[0x100];
	 char     publisher_ru[0x100];
	 char     publisher_zht[0x100];
	 uint32be add_on_unique_id[0x20];
	 uint8    padding[0x3440 - 0x340C];
}acpMetaXml_t;

typedef struct
{
	 uint8 bootMovie[0x13B38];
	 uint8 bootLogoTex[0x6FBC];
	 uint8 ukn1AAF4[4];
	 uint8 ukn1AAF8[4];
	 uint8 ukn1AAFC[4];
}acpMetaData_t;

typedef struct
{
	uint32be titleIdHigh;
	uint32be titleIdLow;
}acpTitleId_t;

typedef struct
{

	uint32be ukn00;
	uint32be ukn04;
	uint32be persistentId;
	uint32be ukn0C;
	uint64 sizeA;
	uint64 sizeB;
	char path[0x40];
	 uint64 time;
	 uint8 padding[0x80 - 0x68];
}acpSaveDirInfo_t;

#define IOSU_ACP_REQUEST_CEMU		(0xEE)

typedef struct
{
	uint32 requestCode;
	uint8 accountSlot;
	uint64 titleId;
	sint32 type;
	MEMPTR<void> ptr;
	sint32 maxCount;
	uint32 returnCode;
	union
	{
		struct
		{
			uint32 u32;
		}resultU32;
	};
}iosuAcpCemuRequest_t;

#define IOSU_ACP_GET_SAVE_DATA_TITLE_ID_LIST				0x01
#define IOSU_ACP_GET_TITLE_SAVE_META_XML					0x02
#define IOSU_ACP_GET_TITLE_SAVE_DIR							0x03
#define IOSU_ACP_GET_TITLE_META_DATA						0x04
#define IOSU_ACP_GET_TITLE_META_XML							0x05
#define IOSU_ACP_CREATE_SAVE_DIR_EX							0x06

namespace iosu
{
	void iosuAcp_init();

	namespace acp
	{
		enum ACPDeviceType
		{
			UnknownType = 0,
			InternalDeviceType = 1,
			USBDeviceType = 3,
		};

		class IOSUModule* GetModule();

		void CreateSaveMetaFiles(uint32 persistentId, uint64 titleId);
		nnResult ACPUpdateSaveTimeStamp(uint32 persistentId, uint64 titleId, ACPDeviceType deviceType);

		nnResult ACPCreateSaveDir(uint32 persistentId, ACPDeviceType type);
		sint32 ACPCreateSaveDirEx(uint8 accountSlot, uint64 titleId);
		nnResult ACPGetOlvAccesskey(uint32be* accessKey);
	}

}