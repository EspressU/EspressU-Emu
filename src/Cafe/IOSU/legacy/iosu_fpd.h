#pragma once
#include "Cafe/IOSU/iosu_types_common.h"
#include "Common/CafeString.h"

namespace iosu
{
	namespace fpd
	{
		static const int RELATIONSHIP_INVALID = 0;
		static const int RELATIONSHIP_FRIENDREQUEST_OUT = 1;
		static const int RELATIONSHIP_FRIENDREQUEST_IN = 2;
		static const int RELATIONSHIP_FRIEND = 3;

		static const int GAMEMODE_MAX_MESSAGE_LENGTH = 0x80;
		static const int MY_COMMENT_LENGTH = 0x11;

		struct FPDDate
		{
			 uint16be year;
			 uint8 month;
			 uint8 day;
			 uint8 hour;
			 uint8 minute;
			 uint8 second;
			 uint8 padding;
		};

		static_assert(sizeof(FPDDate) == 8);

		struct RecentPlayRecordEx
		{
			 uint32be pid;
			 uint8 ukn04;
			 uint8 ukn05;
			 uint8 ukn06[0x22];
			 uint8 ukn28[0x22];
			 uint8 _uknOrPadding4A[6];
			 uint32be ukn50;
			 uint32be ukn54;
			 uint16be ukn58;
			 uint8 _padding5C[4];
			 iosu::fpd::FPDDate date;
		};

		static_assert(sizeof(RecentPlayRecordEx) == 0x68, "");
		static_assert(offsetof(RecentPlayRecordEx, ukn06) == 0x06, "");
		static_assert(offsetof(RecentPlayRecordEx, ukn50) == 0x50, "");

		struct GameKey
		{
			 uint64be titleId;
			 uint16be ukn08;
			 uint8 _padding0A[6];
		};
		static_assert(sizeof(GameKey) == 0x10);

		struct Profile
		{
			uint8be region;
			uint8be regionSubcode;
			uint8be platform;
			uint8be ukn3;
		};
		static_assert(sizeof(Profile) == 0x4);

		struct GameMode
		{
			 uint32be joinFlagMask;
			 uint32be matchmakeType;
			 uint32be joinGameId;
			 uint32be joinGameMode;
			 uint32be hostPid;
			 uint32be groupId;
			 uint8    appSpecificData[0x14];
		};
		static_assert(sizeof(GameMode) == 0x2C);

		struct Comment
		{
			 uint8 unk0;
			 uint8 unk1;
			 CafeWideString<MY_COMMENT_LENGTH> commentString;
		};
		static_assert(sizeof(Comment) == 0x24);

		struct FriendData
		{
			 uint8 type;
			 uint8 _padding1[7];
			 uint32be pid;
			 char nnid[0x10 + 1];
			 uint8 ukn01D;
			 uint8 _padding1E[2];
			 uint16be screenname[10 + 1];
			 uint8 ukn036;
			 uint8 _padding037;
			 uint8 mii[0x60];
			 FPDDate uknDate;
			union
			{
				struct
				{
					 Profile profile;
					 uint32be ukn0A4;
					 GameKey gameKey;
					 GameMode gameMode;
					 uint8 unk0E4;
					 uint8 unk0E5;
					 CafeWideString<GAMEMODE_MAX_MESSAGE_LENGTH> gameModeDescription;
					 uint8 unk1E6;
					 uint8 unk1E7;
					 Profile profile1E8;
					 uint8 isOnline;
					 uint8 _padding1ED[3];
					 Comment comment;
					 uint32be _padding214;
					 FPDDate approvalTime;
					 FPDDate lastOnline;
				}friendExtraData;
				struct
				{
					 uint64be messageId;
					 uint8 ukn0A8;
					 uint8 ukn0A9;
					 uint16be comment[0x40];
					 uint8 ukn12A;
					 uint8 _padding12B;
					 uint16be uknMessage[18];
					 uint64 gameKeyTitleId;
					 uint16be gameKeyUkn;
					 uint8 _padding[6];
					 FPDDate uknData0;
					 FPDDate uknData1;
				}requestExtraData;
			};
		};
		static_assert(sizeof(FriendData) == 0x228);
		static_assert(offsetof(FriendData, friendExtraData.gameKey) == 0x0A8);
		static_assert(offsetof(FriendData, friendExtraData.gameModeDescription) == 0x0E6);
		static_assert(offsetof(FriendData, friendExtraData.comment) == 0x1F0);

		static_assert(offsetof(FriendData, requestExtraData.messageId) == 0x0A0);
		static_assert(offsetof(FriendData, requestExtraData.comment) == 0x0AA);
		static_assert(offsetof(FriendData, requestExtraData.uknMessage) == 0x12C);
		static_assert(offsetof(FriendData, requestExtraData.gameKeyTitleId) == 0x150);
		static_assert(offsetof(FriendData, requestExtraData.uknData1) == 0x168);

		struct FriendBasicInfo
		{
			 uint32be pid;
			 char nnid[0x11];
			 uint8 ukn15;
			 uint8 uknOrPadding[2];
			 uint16be screenname[11];
			 uint8 ukn2E;
			 uint8 ukn2F;
			 uint8 miiData[0x60];
			 FPDDate uknDate90;
		};

		static_assert(sizeof(FriendBasicInfo) == 0x98);
		static_assert(offsetof(FriendBasicInfo, nnid) == 0x04);
		static_assert(offsetof(FriendBasicInfo, ukn15) == 0x15);
		static_assert(offsetof(FriendBasicInfo, screenname) == 0x18);
		static_assert(offsetof(FriendBasicInfo, ukn2E) == 0x2E);
		static_assert(offsetof(FriendBasicInfo, miiData) == 0x30);

		struct FriendRequest
		{
			 uint32be pid;
			 uint8 nnid[17];
			 uint8 _uknOrPadding15;
			 uint8 _uknOrPadding16;
			 uint8 _uknOrPadding17;
			 uint16be screenname[11];
			 uint8 ukn2E;
			 uint8 ukn2F;
			 uint8 miiData[0x60];
			 FPDDate uknDate;
			 uint64 ukn98;
			 uint8 isMarkedAsReceived;
			 uint8 uknA1;
			 uint16be message[0x40];
			 uint8 ukn122;
			 uint8 _padding123;
			 uint16be uknString2[18];
			 uint64 gameKeyTitleId;
			 uint16be gameKeyUkn;
			 uint8 _padding152[6];
			 FPDDate uknDate2;
			 FPDDate expireDate;
		};

		static_assert(sizeof(FriendRequest) == 0x168);
		static_assert(offsetof(FriendRequest, uknDate) == 0x090);
		static_assert(offsetof(FriendRequest, message) == 0x0A2);
		static_assert(offsetof(FriendRequest, uknString2) == 0x124);
		static_assert(offsetof(FriendRequest, gameKeyTitleId) == 0x148);

		struct FriendPresence
		{
			GameMode gameMode;
			 Profile  profile;
			 uint8	 isOnline;
			 uint8    isValid;
			 uint8	 padding[2];
		};
		static_assert(sizeof(FriendPresence) == 0x34);
		static_assert(offsetof(FriendPresence, isOnline) == 0x30);

		struct FPDNotification
		{
			betype<uint32> type;
			betype<uint32> pid;
		};
		static_assert(sizeof(FPDNotification) == 8);

		struct FPDPreference
		{
			uint8be showOnline;
			uint8be showGame;
			uint8be blockFriendRequests;
			uint8be ukn;
		};
		static_assert(sizeof(FPDPreference) == 4);

		enum class FPD_REQUEST_ID
		{
			LoginAsync = 0x2775,
			HasLoggedIn = 0x2777,
			IsOnline = 0x2778,
			GetMyPrincipalId = 0x27D9,
			GetMyAccountId = 0x27DA,
			GetMyScreenName = 0x27DB,
			GetMyMii = 0x27DC,
			GetMyProfile = 0x27DD,
			GetMyPreference = 0x27DE,
			GetMyPresence = 0x27DF,
			IsPreferenceValid = 0x27E0,
			GetFriendList = 0x283D,
			GetFriendListAll = 0x283E,
			GetFriendAccountId = 0x283F,
			GetFriendScreenName = 0x2840,
			GetFriendPresence = 0x2845,
			GetFriendRelationship = 0x2846,
			GetFriendMii = 0x2841,
			GetBlackList = 0x28A1,
			GetFriendRequestList = 0x2905,
			UpdateGameModeVariation1 = 0x2969,
			UpdateGameModeVariation2 = 0x296A,
			AddFriendAsyncByPid = 0x29CD,
			AddFriendAsyncByXXX = 0x29CE,
			GetRequestBlockSettingAsync = 0x2B5D,
			GetMyComment = 0x4EE9,
			GetMyPlayingGame = 0x4EEA,
			CheckSettingStatusAsync = 0x7596,
			GetFriendListEx = 0x75F9,
			GetFriendRequestListEx = 0x76C1,
			UpdateCommentAsync = 0x7726,
			UpdatePreferenceAsync = 0x7727,
			RemoveFriendAsync = 0x7789,
			DeleteFriendFlagsAsync = 0x778A,
			AddFriendRequestByPlayRecordAsync = 0x778B,
			CancelFriendRequestAsync = 0x778C,
			AcceptFriendRequestAsync = 0x7851,
			DeleteFriendRequestAsync = 0x7852,
			MarkFriendRequestsAsReceivedAsync = 0x7854,
			GetBasicInfoAsync = 0x7919,
			SetLedEventMask = 0x9D0B,
			SetNotificationMask = 0x15ff5,
			GetNotificationAsync = 0x15FF6,
		};

		using FriendPID = uint32;

		IOSUModule* GetModule();
	}
}
