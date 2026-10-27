#include "napi.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "config/ActiveSettings.h"

namespace NAPI
{
	NetworkService AuthInfo::GetService() const
	{
		return serviceOverwrite.value_or(ActiveSettings::GetNetworkService());
	}
}

// EspressU contains no online services. The interface stays so that the guest system libraries still link;
// every call fails at once and nothing is ever sent.
namespace NAPI
{
	bool NAPI_MakeAuthInfoFromCurrentAccount(AuthInfo& authInfo) { return false; }

	bool ACT_GetProfile(AuthInfo& authInfo, ACTGetProfileResult& result) { return false; }
	ACTGetNexTokenResult ACT_GetNexToken_WithCache(AuthInfo& authInfo, uint64 titleId, uint16 titleVersion, uint32 serverId) { return {}; }
	ACTGetIndependentTokenResult ACT_GetIndependentToken_WithCache(AuthInfo& authInfo, uint64 titleId, uint16 titleVersion, std::string_view clientId) { return {}; }
	ACTConvertNnidToPrincipalIdResult ACT_ACTConvertNnidToPrincipalId(AuthInfo& authInfo, std::string_view nnid) { return {}; }

	NAPI_NUSGetSystemCommonETicket_Result NUS_GetSystemCommonETicket(AuthInfo& authInfo, uint64 titleId) { return {}; }
	NAPI_IASGetChallenge_Result IAS_GetChallenge(AuthInfo& authInfo) { return {}; }
	NAPI_IASGetRegistrationInfo_Result IAS_GetRegistrationInfo_QueryInfo(AuthInfo& authInfo, std::string challenge) { return {}; }
	NAPI_ECSGetAccountStatus_Result ECS_GetAccountStatus(AuthInfo& authInfo) { return {}; }
	NAPI_ECSAccountListETicketIds_Result ECS_AccountListETicketIds(AuthInfo& authInfo) { return {}; }
	NAPI_ECSAccountGetETickets_Result ECS_AccountGetETickets(AuthInfo& authInfo, sint64 ticketId) { return {}; }

	NAPI_CCSGetTMD_Result CCS_GetTMD(AuthInfo& authInfo, uint64 titleId, uint16 titleVersion) { return {}; }
	NAPI_CCSGetTMD_Result CCS_GetTMD(AuthInfo& authInfo, uint64 titleId) { return {}; }
	NAPI_CCSGetETicket_Result CCS_GetCETK(NetworkService service, uint64 titleId, uint16 titleVersion) { return {}; }
	bool CCS_GetContentFile(NetworkService service, uint64 titleId, uint32 contentId, bool(*cbWriteCallback)(void* userData, const void* ptr, size_t len, bool isLast), void* userData) { return false; }
	NAPI_CCSGetContentH3_Result CCS_GetContentH3File(NetworkService service, uint64 titleId, uint32 contentId) { return {}; }

	std::optional<IDBEIconDataV0> IDBE_Request(NetworkService networkService, uint64 titleId) { return std::nullopt; }
	std::vector<uint8> IDBE_RequestRawEncrypted(NetworkService networkService, uint64 titleId) { return {}; }

	NAPI_VersionListVersion_Result TAG_GetVersionListVersion(AuthInfo& authInfo) { return {}; }
	NAPI_VersionList_Result TAG_GetVersionList(AuthInfo& authInfo, std::string_view fqdnURL, uint32 versionListVersion) { return {}; }
}

void NAPI_NUS_GetSystemUpdate() {}
void NAPI_NUS_GetSystemTitleHash() {}
