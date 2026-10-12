#pragma once
#include <zlib.h>
#include "nn_olv_Common.h"

namespace nn
{
	namespace olv
	{
		struct DownloadedDataBase
		{
			enum class FLAGS : uint32
			{
				HAS_BODY_TEXT = 0x01,
				HAS_BODY_MEMO = 0x02,
				HAS_EXTERNAL_IMAGE = 0x04,
				HAS_EXTERNAL_BINARY_DATA = 0x08,
				HAS_MII_DATA = 0x10,
				HAS_EXTERNAL_URL = 0x20,
				HAS_APP_DATA = 0x40,
				HAS_EMPATHY_ADDED = 0x80,
				IS_AUTOPOST = 0x100,
				IS_SPOILER = 0x200,
				IS_NOT_AUTOPOST = 0x400,
			};

			void Reset()
			{
				memset(this, 0, sizeof(DownloadedDataBase));
			}

			void SetFlag(FLAGS flag)
			{
				flags =  (FLAGS)((uint32)flags.value() | (uint32)flag);
			}

			betype<FLAGS> flags;
			uint32be userPid;
			uint8be postId[32];
			uint64be postDate;
			sint8be feeling;
			uint8be _padding0031[3];
			uint32be regionId;
			uint8be platformId;
			uint8be languageId;
			uint8be countryId;
			uint8be _padding003B;
			uint16be bodyText[256];
			uint32be bodyTextLength;
			uint8be compressedMemoBody[40960];
			uint32be compressedMemoBodySize;
			uint16be topicTag[152];
			uint8be appData[1024];
			uint32be appDataLength;
			uint8be externalBinaryUrl[256];
			uint32be externalBinaryDataSize;
			uint8be externalImageDataUrl[256];
			uint32be externalImageDataSize;
			uint8be externalURL[256];
			uint8be miiData[96];
			uint16be miiNickname[16];
			uint32be _paddingAB00[1344];
			uint32be uknC000_someVTableMaybe;
			uint32be uknC004;

			static bool TestFlags(DownloadedDataBase* _this, DownloadedDataBase::FLAGS flag)
			{
				return HAS_FLAG((uint32)_this->flags.value(), (uint32)flag);
			}

			static uint32 GetUserPid(DownloadedDataBase* _this)
			{
				return _this->userPid;
			}

			static uint64 GetPostDate(DownloadedDataBase* _this)
			{
				return _this->postDate;
			}

			static uint32 GetFeeling(DownloadedDataBase* _this)
			{
				if(_this->feeling >= 6)
					return 0;
				return _this->feeling;
			}

			static uint32 GetRegionId(DownloadedDataBase* _this)
			{
				return _this->regionId;
			}

			static uint32 GetPlatformId(DownloadedDataBase* _this)
			{
				return _this->platformId;
			}

			static uint32 GetLanguageId(DownloadedDataBase* _this)
			{
				return _this->languageId;
			}

			static uint32 GetCountryId(DownloadedDataBase* _this)
			{
				return _this->countryId;
			}

			static uint8be* GetExternalUrl(DownloadedDataBase* _this)
			{
				if (!TestFlags(_this, FLAGS::HAS_EXTERNAL_URL))
					return nullptr;
				return _this->externalURL;
			}

			static nnResult GetMiiData1(DownloadedDataBase* _this, void* miiDataOut)
			{
				if (!TestFlags(_this, FLAGS::HAS_MII_DATA))
					return OLV_RESULT_MISSING_DATA;
				if (!miiDataOut)
					return OLV_RESULT_INVALID_PTR;
				memcpy(miiDataOut, _this->miiData, 96);
				return OLV_RESULT_SUCCESS;
			}

			static uint8be* GetMiiData2(DownloadedDataBase* _this)
			{
				if (!TestFlags(_this, FLAGS::HAS_MII_DATA))
					return nullptr;
				return _this->miiData;
			}

			static uint16be* GetMiiNickname(DownloadedDataBase* _this)
			{
				if (_this->miiNickname[0] == 0)
					return nullptr;
				return _this->miiNickname;
			}

			static nnResult GetBodyText(DownloadedDataBase* _this, uint16be* bodyTextOut, uint32 maxLength)
			{
				if (!bodyTextOut)
					return OLV_RESULT_INVALID_PTR;
				if (maxLength == 0)
					return OLV_RESULT_NOT_ENOUGH_SIZE;
				if (!TestFlags(_this, FLAGS::HAS_BODY_TEXT))
					return OLV_RESULT_MISSING_DATA;
				memset(bodyTextOut, 0, maxLength * sizeof(uint16));
				uint32 outputLength = std::min<uint32>(_this->bodyTextLength, maxLength);
				olv_wstrncpy((char16_t*)bodyTextOut, (char16_t*)_this->bodyText, outputLength);
				return OLV_RESULT_SUCCESS;
			}

			static nnResult GetBodyMemo(DownloadedDataBase* _this, uint8be* bodyMemoOut, uint32* bodyMemoSizeOut, uint32 maxSize)
			{
				if (!bodyMemoOut)
					return OLV_RESULT_INVALID_PTR;
				if (maxSize < 0x2582C)
					return OLV_RESULT_NOT_ENOUGH_SIZE;
				if (!TestFlags(_this, FLAGS::HAS_BODY_MEMO))
					return OLV_RESULT_MISSING_DATA;
				uLongf decompressedSize = maxSize;
				if (uncompress((uint8*)bodyMemoOut, &decompressedSize, (uint8*)_this->compressedMemoBody, _this->compressedMemoBodySize) != Z_OK)
				{
					cemuLog_log(LogType::Force, "DownloadedSystemTopicData::GetTitleIconData: uncompress failed");
					return OLV_RESULT_INVALID_TEXT_FIELD;
				}
				if(bodyMemoSizeOut)
					*bodyMemoSizeOut = decompressedSize;
				return OLV_RESULT_SUCCESS;
			}

			static uint16be* GetTopicTag(DownloadedDataBase* _this)
			{
				return _this->topicTag;
			}

			static nnResult GetAppData(DownloadedDataBase* _this, uint8be* appDataOut, uint32* appDataSizeOut, uint32 maxSize)
			{
				if (!appDataOut)
					return OLV_RESULT_INVALID_PTR;
				if (!TestFlags(_this, FLAGS::HAS_APP_DATA))
					return OLV_RESULT_MISSING_DATA;
				uint32 outputSize = std::min<uint32>(maxSize, _this->appDataLength);
				memcpy(appDataOut, _this->appData, outputSize);
				if(appDataSizeOut)
					*appDataSizeOut = outputSize;
				return OLV_RESULT_SUCCESS;
			}

			static uint32 GetAppDataSize(DownloadedDataBase* _this)
			{
				return _this->appDataLength;
			}

			static uint8be* GetPostId(DownloadedDataBase* _this)
			{
				return _this->postId;
			}

			static nnResult DownloadExternalImageData(DownloadedDataBase* _this, void* imageDataOut, uint32be* imageSizeOut, uint32 maxSize);

			static uint32 GetExternalImageDataSize(DownloadedDataBase* _this)
			{
				if (!TestFlags(_this, FLAGS::HAS_EXTERNAL_IMAGE))
					return 0;
				return _this->externalImageDataSize;
			}

		};

		static_assert(sizeof(DownloadedDataBase) == 0xC008);

		struct DownloadedPostData
		{
			DownloadedDataBase downloadedDataBase;
			uint32be communityId;
			uint32be empathyCount;
			uint32be commentCount;
			uint32be paddingC014[125];

			static uint32 GetCommunityId(DownloadedPostData* _this)
			{
				return _this->communityId;
			}

			static uint32 GetEmpathyCount(DownloadedPostData* _this)
			{
				return _this->empathyCount;
			}

			static uint32 GetCommentCount(DownloadedPostData* _this)
			{
				return _this->commentCount;
			}

			static uint8be* GetPostId(DownloadedPostData* _this)
			{
				return _this->downloadedDataBase.postId;
			}

		};

		static_assert(sizeof(DownloadedPostData) == 0xC208);

		struct DownloadedTopicData
		{
			enum class FLAGS
			{
				IS_RECOMMENDED = 0x01,
				HAS_TITLE = 0x02,
				HAS_ICON_DATA = 0x04,
			};
			betype<FLAGS> flags;
			uint32be communityId;
			int ukn[1022];

			void SetFlag(FLAGS flag)
			{
				flags = (FLAGS)((uint32)flags.value() | (uint32)flag);
			}

			static uint32 GetCommunityId(DownloadedTopicData* _this)
			{
				return _this->communityId;
			}
		};

		static_assert(sizeof(DownloadedTopicData) == 0x1000);

		namespace hidden
		{
			struct DownloadedSystemPostData
			{
				DownloadedPostData downloadedPostData;
				uint64be titleId;
				uint32be uknC210[124];
				uint32be uknC400;
				uint32be uknC404;

				static uint64 GetTitleId(DownloadedSystemPostData* _this)
				{
					return _this->titleId;
				}
			};

			static_assert(sizeof(DownloadedSystemPostData) == 0xC408);

			struct DownloadedSystemTopicData
			{
				DownloadedTopicData downloadedTopicData;
				uint64be titleId;
				uint16be titleText[128];
				uint8be ukn1108[256];
				uint8be iconData[0x1002C];
				uint32be iconDataSize;
				uint64be titleIds[32];
				uint32be titleIdsCount;
				uint32be ukn1133C[1841];

				static bool TestFlags(DownloadedSystemTopicData* _this, DownloadedTopicData::FLAGS flag)
				{
					return HAS_FLAG((uint32)_this->downloadedTopicData.flags.value(), (uint32)flag);
				}

				static uint64 GetTitleId(DownloadedSystemTopicData* _this)
				{
					return _this->titleId;
				}

				static uint32 GetTitleIdNum(DownloadedSystemTopicData* _this)
				{
					return _this->titleIdsCount;
				}

				static nnResult GetTitleText(DownloadedSystemTopicData* _this, uint16be* titleTextOut, uint32 maxLength)
				{
					if (!TestFlags(_this, DownloadedTopicData::FLAGS::HAS_TITLE))
						return OLV_RESULT_MISSING_DATA;
					if (!titleTextOut)
						return OLV_RESULT_INVALID_PTR;
					memset(titleTextOut, 0, maxLength * sizeof(uint16be));
					if (maxLength > 128)
						maxLength = 128;
					olv_wstrncpy((char16_t*)titleTextOut, (char16_t*)_this->titleText, maxLength);
					return OLV_RESULT_SUCCESS;
				}

				static nnResult GetTitleIconData(DownloadedSystemTopicData* _this, void* iconDataOut, uint32be* iconSizeOut, uint32 iconDataMaxSize)
				{
					if (!TestFlags(_this, DownloadedTopicData::FLAGS::HAS_ICON_DATA))
						return OLV_RESULT_MISSING_DATA;
					if (!iconDataOut)
						return OLV_RESULT_INVALID_PTR;
					if (iconDataMaxSize < 0x1002C)
						return OLV_RESULT_NOT_ENOUGH_SIZE;
					uLongf decompressedSize = iconDataMaxSize;
					if (uncompress((uint8*)iconDataOut, &decompressedSize, (uint8*)_this->iconData, _this->iconDataSize) != Z_OK)
					{
						cemuLog_log(LogType::Force, "DownloadedSystemTopicData::GetTitleIconData: uncompress failed");
						return OLV_RESULT_INVALID_TEXT_FIELD;
					}
					*iconSizeOut = decompressedSize;
					return OLV_RESULT_SUCCESS;
				}
			};

			static_assert(sizeof(DownloadedSystemTopicData) == 0x13000);

			struct DownloadedSystemTopicDataList
			{
				static constexpr size_t MAX_TOPIC_COUNT = 10;
				static constexpr size_t MAX_POSTS_PER_TOPIC = 300;

				struct DownloadedSystemTopicWrapped
				{
					DownloadedSystemTopicData downloadedSystemTopicData;
					uint32be postDataNum;
					MEMPTR<DownloadedSystemPostData> postDataList[MAX_POSTS_PER_TOPIC];
					uint32 uknPadding;
				};
				static_assert(offsetof(DownloadedSystemTopicWrapped, postDataNum) == 0x13000);
				static_assert(sizeof(DownloadedSystemTopicWrapped) == 0x134B8);

				static uint32 GetDownloadedSystemTopicDataNum(DownloadedSystemTopicDataList* _this)
				{
					return _this->topicDataNum;
				};

				static uint32 GetDownloadedSystemPostDataNum(DownloadedSystemTopicDataList* _this, uint32 topicIndex)
				{
					if(topicIndex >= MAX_TOPIC_COUNT)
						return 0;
					return _this->topicData[topicIndex].postDataNum;
				};

				static DownloadedSystemTopicData* GetDownloadedSystemTopicData(DownloadedSystemTopicDataList* _this, uint32 topicIndex)
				{
					if(topicIndex >= MAX_TOPIC_COUNT)
						return nullptr;
					return &_this->topicData[topicIndex].downloadedSystemTopicData;
				};

				static DownloadedSystemPostData* GetDownloadedSystemPostData(DownloadedSystemTopicDataList* _this, sint32 topicIndex, sint32 postIndex)
				{
					if (topicIndex >= MAX_TOPIC_COUNT || postIndex >= MAX_POSTS_PER_TOPIC)
						return nullptr;
					return _this->topicData[topicIndex].postDataList[postIndex];
				}

				uint32be topicDataNum;
				uint32be ukn4;
				DownloadedSystemTopicWrapped topicData[MAX_TOPIC_COUNT];
				uint32be uknC0F38[50];
			};

			static_assert(sizeof(DownloadedSystemTopicDataList) == 0xC1000);
		}

		struct DownloadPostDataListParam
		{
			static constexpr size_t MAX_NUM_SEARCH_PID = 12;
			static constexpr size_t MAX_NUM_SEARCH_KEY = 5;
			static constexpr size_t MAX_NUM_POST_ID = 20;

			enum class FLAGS
			{
				FRIENDS_ONLY = 0x01,
				FOLLOWERS_ONLY = 0x02,
				SELF_ONLY = 0x04,
				ONLY_TYPE_TEXT = 0x08,
				ONLY_TYPE_MEMO = 0x10,
				UKN_20 = 0x20,
				WITH_MII = 0x40,
				WITH_EMPATHY = 0x80,
				UKN_100 = 0x100,
				UKN_200 = 0x200,
				UKN_400 = 0x400,

			};

			struct SearchKey
			{
				uint16be str[152];
			};

			struct PostId
			{
				char str[32];
			};

			betype<FLAGS> flags;
			uint32be communityId;
			uint32be searchPid[MAX_NUM_SEARCH_PID];
			uint8 languageId;
			uint8 hasLanguageId_039;
			uint8 padding03A[2];
			uint32be postDataMaxNum;
			SearchKey searchKeyArray[MAX_NUM_SEARCH_KEY];
			PostId searchPostId[MAX_NUM_POST_ID];
			uint64be postDate;
			uint64be titleId;
			uint32be bodyTextMaxLength;
			uint8 padding8C4[1852];

			bool _HasFlag(FLAGS flag)
			{
				return ((uint32)flags.value() & (uint32)flag) != 0;
			}

			void _SetFlags(FLAGS flag)
			{
				flags = (FLAGS)((uint32)flags.value() | (uint32)flag);
			}

			static DownloadPostDataListParam* Construct(DownloadPostDataListParam* _this)
			{
				memset(_this, 0, sizeof(DownloadPostDataListParam));
				return _this;
			}

			static nnResult SetFlags(DownloadPostDataListParam* _this, FLAGS flags)
			{
				_this->flags = flags;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetLanguageId(DownloadPostDataListParam* _this, uint8 languageId)
			{
				_this->languageId = languageId;
				_this->hasLanguageId_039 = 1;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetCommunityId(DownloadPostDataListParam* _this, uint32 communityId)
			{
				_this->communityId = communityId;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetSearchKey(DownloadPostDataListParam* _this, const uint16be* searchKey, uint8 searchKeyIndex)
			{
				if( !searchKey )
				{
					memset(&_this->searchKeyArray[searchKeyIndex], 0, sizeof(SearchKey));
					return OLV_RESULT_SUCCESS;
				}
				if (searchKeyIndex >= MAX_NUM_SEARCH_KEY)
					return OLV_RESULT_INVALID_PARAMETER;
				memset(&_this->searchKeyArray[searchKeyIndex], 0, sizeof(SearchKey));
				if(olv_wstrnlen((const char16_t*)searchKey, 152) > 50)
				{
					cemuLog_log(LogType::Force, "DownloadPostDataListParam::SetSearchKey: searchKey is too long\n");
					return OLV_RESULT_INVALID_PARAMETER;
				}
				SetStringUC2(_this->searchKeyArray[searchKeyIndex].str, searchKey);
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetSearchKeySingle(DownloadPostDataListParam* _this, const uint16be* searchKey)
			{
				if (searchKey == nullptr)
				{
					cemuLog_logDebug(LogType::NN_OLV, "DownloadPostDataListParam::SetSearchKeySingle: searchKeySingle is Null\n");
					return OLV_RESULT_INVALID_PARAMETER;
				}
				return SetSearchKey(_this, searchKey, 0);
			}

			static nnResult SetSearchPid(DownloadPostDataListParam* _this, uint32 searchPid)
			{
				if(_this->_HasFlag(FLAGS::FRIENDS_ONLY) || _this->_HasFlag(FLAGS::FOLLOWERS_ONLY) || _this->_HasFlag(FLAGS::SELF_ONLY))
					return OLV_RESULT_INVALID_PARAMETER;
				_this->searchPid[0] = searchPid;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetPostId(DownloadPostDataListParam* _this, const char* postId, uint32 postIdIndex)
			{
				if (postIdIndex >= MAX_NUM_POST_ID)
					return OLV_RESULT_INVALID_PARAMETER;
				memset(&_this->searchPostId[postIdIndex], 0, sizeof(PostId));
				if (strlen(postId) > 22)
				{
					cemuLog_log(LogType::Force, "DownloadPostDataListParam::SetPostId: postId is too long\n");
					return OLV_RESULT_INVALID_PARAMETER;
				}
				strcpy(_this->searchPostId[postIdIndex].str, postId);
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetPostDate(DownloadPostDataListParam* _this, uint64 postDate)
			{
				_this->postDate = postDate;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetPostDataMaxNum(DownloadPostDataListParam* _this, uint32 postDataMaxNum)
			{
				if(postDataMaxNum == 0)
					return OLV_RESULT_INVALID_PARAMETER;
				_this->postDataMaxNum = postDataMaxNum;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult SetBodyTextMaxLength(DownloadPostDataListParam* _this, uint32 bodyTextMaxLength)
			{
				if(bodyTextMaxLength >= 256)
					return OLV_RESULT_INVALID_PARAMETER;
				_this->bodyTextMaxLength = bodyTextMaxLength;
				return OLV_RESULT_SUCCESS;
			}

			static nnResult GetRawDataUrl(DownloadPostDataListParam* _this, char* urlOut, uint32 urlMaxSize);
		};

		static_assert(sizeof(DownloadPostDataListParam) == 0x1000);

		bool ParseXML_DownloadedPostData(DownloadedPostData& obj, pugi::xml_node& xmlNode);

		void loadOlivePostAndTopicTypes();
	}
}