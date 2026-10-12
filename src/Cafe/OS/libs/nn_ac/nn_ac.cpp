#include "Cafe/OS/common/OSCommon.h"
#include "Cafe/OS/libs/nn_common.h"
#include "nn_ac.h"
#include "Common/socket.h"
#include "nn_ac_reachability.h"
#include "config/ActiveSettings.h"

#if BOOST_OS_WINDOWS
#include <iphlpapi.h>
#elif BOOST_OS_LINUX || BOOST_OS_MACOS || BOOST_OS_IOS
#include <ifaddrs.h>
#include <net/if.h>
#endif

enum class AC_STATUS : uint32
{
	FAILED = (uint32)-1,
	OK = 0,
};

static_assert(TRUE == 1, "TRUE not 1");

static std::atomic<bool> s_deviceReachable{true};

namespace nn_ac
{
	void SetDeviceReachable(bool reachable)
	{
		s_deviceReachable.store(reachable, std::memory_order_relaxed);
	}

	bool IsDeviceReachable()
	{
		return s_deviceReachable.load(std::memory_order_relaxed);
	}

	bool IsConsoleConnected()
	{
		return IsDeviceReachable();
	}
}

static constexpr uint32 AC_ERROR_NO_CONNECTION = 1022802;
static constexpr uint32 AC_RESULT_NO_CONNECTION = BUILD_NN_RESULT(NN_RESULT_LEVEL_FATAL, NN_RESULT_MODULE_NN_AC, 2802);

void _GetLocalIPAndSubnetMaskFallback(uint32& localIp, uint32& subnetMask)
{
	localIp = (192 << 24) | (168 << 16) | (0 << 8) | (100 << 0);
	subnetMask = (255 << 24) | (255 << 16) | (255 << 8) | (0 << 0);
}

#if BOOST_OS_WINDOWS
void _GetLocalIPAndSubnetMask(uint32& localIp, uint32& subnetMask)
{
	std::vector<IP_ADAPTER_ADDRESSES> buf_adapter_addresses;
	buf_adapter_addresses.resize(32);
	DWORD buf_size;
	DWORD r;

	for (uint32 i = 0; i < 6; i++)
	{
		buf_size = (uint32)(buf_adapter_addresses.size() * sizeof(IP_ADAPTER_ADDRESSES));
		r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS, nullptr, buf_adapter_addresses.data(), &buf_size);
		if (r != ERROR_BUFFER_OVERFLOW)
			break;
		buf_adapter_addresses.resize(buf_adapter_addresses.size() * 2);
	}
	if (r != ERROR_SUCCESS)
	{
		cemuLog_log(LogType::Force, "Failed to acquire local IP and subnet mask");
		_GetLocalIPAndSubnetMaskFallback(localIp, subnetMask);
		return;
	}
	IP_ADAPTER_ADDRESSES* currentAddress = buf_adapter_addresses.data();
	while (currentAddress)
	{
		if (currentAddress->OperStatus != IfOperStatusUp)
		{
			currentAddress = currentAddress->Next;
			continue;
		}
		if (!currentAddress->FirstUnicastAddress || !currentAddress->FirstUnicastAddress->Address.lpSockaddr)
		{
			currentAddress = currentAddress->Next;
			continue;
		}
		if (!currentAddress->FirstGatewayAddress)
		{
			currentAddress = currentAddress->Next;
			continue;
		}

		SOCKADDR* sockAddr = currentAddress->FirstUnicastAddress->Address.lpSockaddr;
		if (sockAddr->sa_family == AF_INET)
		{
			ULONG mask = 0;
			if (ConvertLengthToIpv4Mask(currentAddress->FirstUnicastAddress->OnLinkPrefixLength, &mask) != NO_ERROR)
				mask = 0;
			sockaddr_in* inAddr = (sockaddr_in*)sockAddr;
			localIp = _byteswap_ulong(inAddr->sin_addr.S_un.S_addr);
			subnetMask = _byteswap_ulong(mask);
			return;
		}
		currentAddress = currentAddress->Next;
	}
	cemuLog_logDebug(LogType::Force, "_GetLocalIPAndSubnetMask(): Failed to find network IP and subnet mask");
	_GetLocalIPAndSubnetMaskFallback(localIp, subnetMask);
}
#elif BOOST_OS_LINUX || BOOST_OS_MACOS || BOOST_OS_IOS
void _GetLocalIPAndSubnetMask(uint32& localIp, uint32& subnetMask)
{
	struct ifaddrs *ifaddr;
	if (getifaddrs(&ifaddr) == -1)
	{
		cemuLog_log(LogType::Force, "Failed to acquire local IP and subnet mask");
		_GetLocalIPAndSubnetMaskFallback(localIp, subnetMask);
		return;
	}
	stdx::scope_exit _ifa([&]{ freeifaddrs(ifaddr); });

	for (const struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
	{
		if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET)
			continue;

		if (!(ifa->ifa_flags & IFF_UP) || !(ifa->ifa_flags & IFF_RUNNING))
			continue;

		if (ifa->ifa_flags & IFF_LOOPBACK || ifa->ifa_flags & IFF_POINTOPOINT)
			continue;

		if (boost::starts_with(ifa->ifa_name, "br-") || boost::starts_with(ifa->ifa_name, "docker"))
			continue;

		const auto* addr_in = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
		localIp = ntohl(addr_in->sin_addr.s_addr);
		const auto* mask_in = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_netmask);
		subnetMask = ntohl(mask_in->sin_addr.s_addr);
		return;
	}
	cemuLog_logDebug(LogType::Force, "_GetLocalIPAndSubnetMask(): Failed to find network IP and subnet mask");
	_GetLocalIPAndSubnetMaskFallback(localIp, subnetMask);
}
#else
void _GetLocalIPAndSubnetMask(uint32& localIp, uint32& subnetMask)
{
	cemuLog_logDebug(LogType::Force, "_GetLocalIPAndSubnetMask(): Not implemented");
	_GetLocalIPAndSubnetMaskFallback(localIp, subnetMask);
}
#endif

void nnAcExport_GetAssignedAddress(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "GetAssignedAddress() called");
	ppcDefineParamU32BEPtr(ipAddrOut, 0);

	uint32 localIp;
	uint32 subnetMask;
	_GetLocalIPAndSubnetMask(localIp, subnetMask);

	*ipAddrOut = localIp;

	const uint32 nnResultCode = nn_ac::IsConsoleConnected() ? BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0) : AC_RESULT_NO_CONNECTION;
	osLib_returnFromFunction(hCPU, nnResultCode);
}

void nnAcExport_GetAssignedSubnet(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "GetAssignedSubnet() called");

	ppcDefineParamU32BEPtr(subnetMaskOut, 0);

	uint32 localIp;
	uint32 subnetMask;
	_GetLocalIPAndSubnetMask(localIp, subnetMask);

	*subnetMaskOut = subnetMask;

	const uint32 nnResultCode = nn_ac::IsConsoleConnected() ? BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0) : AC_RESULT_NO_CONNECTION;
	osLib_returnFromFunction(hCPU, nnResultCode);
}

void nnAcExport_ACGetAssignedAddress(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32BEPtr(ipAddrOut, 0);

	uint32 localIp;
	uint32 subnetMask;
	_GetLocalIPAndSubnetMask(localIp, subnetMask);
	*ipAddrOut = localIp;

	const uint32 nnResultCode = nn_ac::IsConsoleConnected() ? BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0) : AC_RESULT_NO_CONNECTION;
	osLib_returnFromFunction(hCPU, nnResultCode);
}

void nnAcExport_IsSystemConnected(PPCInterpreter_t* hCPU)
{
	ppcDefineParamTypePtr(isConnectedOut, uint8, 0);
	ppcDefineParamTypePtr(apTypeOut, uint32be, 1);

	cemuLog_logDebug(LogType::Force, "nn_ac.IsSystemConnected() - placeholder");
	*apTypeOut = 0;
	*isConnectedOut = nn_ac::IsConsoleConnected() ? 1 : 0;

	osLib_returnFromFunction(hCPU, 0);
}

void nnAcExport_IsConfigExisting(PPCInterpreter_t* hCPU)
{
	cemuLog_logDebug(LogType::Force, "nn_ac.IsConfigExisting() - placeholder");

	ppcDefineParamU32(configId, 0);
	ppcDefineParamTypePtr(isConfigExisting, uint8, 1);

	*isConfigExisting = ActiveSettings::IsOnlineEnabled() ? 1 : 0;

	osLib_returnFromFunction(hCPU, 0);
}

namespace nn_ac
{
	nnResult Initialize()
	{
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0);
	}

	nnResult ConnectAsync()
	{
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0);
	}

	nnResult IsApplicationConnected(uint8be* connected)
	{
		if (connected)
			*connected = IsConsoleConnected() ? TRUE : FALSE;
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0);
	}

	uint32 Connect()
	{
		if (!IsConsoleConnected())
			return AC_RESULT_NO_CONNECTION;
		uint32 nnResultCode = 0;
		return nnResultCode;
	}

	nnResult GetConnectStatus(betype<AC_STATUS>* status)
	{
		if (status)
			*status = IsConsoleConnected() ? AC_STATUS::OK : AC_STATUS::FAILED;
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0);
	}

	nnResult GetStatus(betype<AC_STATUS>* status)
	{
		return GetConnectStatus(status);
	}

	nnResult GetLastErrorCode(uint32be* errorCode)
	{
		if (errorCode)
			*errorCode = IsConsoleConnected() ? 0 : AC_ERROR_NO_CONNECTION;
		return BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0);
	}

	nnResult GetConnectResult(uint32be* connectResult)
	{
		const uint32 nnResultCode = IsConsoleConnected() ? BUILD_NN_RESULT(NN_RESULT_LEVEL_SUCCESS, NN_RESULT_MODULE_NN_AC, 0) : AC_RESULT_NO_CONNECTION;
		if (connectResult)
			*connectResult = nnResultCode;
		return nnResultCode;
	}

	static_assert(sizeof(betype<AC_STATUS>) == 4);
	static_assert(sizeof(betype<nnResult>) == 4);

	nnResult ACInitialize()
	{
		return Initialize();
	}

	bool ACIsSuccess(betype<nnResult>* r)
	{
		return NN_RESULT_IS_SUCCESS(*r) ? 1 : 0;
	}

	bool ACIsFailure(betype<nnResult>* r)
	{
		return NN_RESULT_IS_FAILURE(*r) ? 1 : 0;
	}

	nnResult ACGetConnectStatus(betype<AC_STATUS>* connectionStatus)
	{
		return GetConnectStatus(connectionStatus);
	}

	nnResult ACGetStatus(betype<AC_STATUS>* connectionStatus)
	{
		return GetStatus(connectionStatus);
	}

	nnResult ACConnectAsync()
	{
		return ConnectAsync();
	}

	nnResult ACIsApplicationConnected(uint32be* connectedU32)
	{
		uint8be connected = 0;
		nnResult r = IsApplicationConnected(&connected);
		*connectedU32 = connected;
		return r;
	}

	void load()
	{
		cafeExportRegisterFunc(Initialize, "nn_ac", "Initialize__Q2_2nn2acFv", LogType::Placeholder);

		cafeExportRegisterFunc(Connect, "nn_ac", "Connect__Q2_2nn2acFv", LogType::Placeholder);
		cafeExportRegisterFunc(ConnectAsync, "nn_ac", "ConnectAsync__Q2_2nn2acFv", LogType::Placeholder);

		cafeExportRegisterFunc(GetConnectResult, "nn_ac", "GetConnectResult__Q2_2nn2acFPQ2_2nn6Result", LogType::Placeholder);
		cafeExportRegisterFunc(GetLastErrorCode, "nn_ac", "GetLastErrorCode__Q2_2nn2acFPUi", LogType::Placeholder);
		cafeExportRegisterFunc(GetConnectStatus, "nn_ac", "GetConnectStatus__Q2_2nn2acFPQ3_2nn2ac6Status", LogType::Placeholder);
		cafeExportRegisterFunc(GetStatus, "nn_ac", "GetStatus__Q2_2nn2acFPQ3_2nn2ac6Status", LogType::Placeholder);
		cafeExportRegisterFunc(IsApplicationConnected, "nn_ac", "IsApplicationConnected__Q2_2nn2acFPb", LogType::Placeholder);

		cafeExportRegister("nn_ac", ACInitialize, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACIsSuccess, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACIsFailure, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACGetConnectStatus, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACGetStatus, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACConnectAsync, LogType::Placeholder);
		cafeExportRegister("nn_ac", ACIsApplicationConnected, LogType::Placeholder);
	}

}

void nnAc_load()
{

}

namespace nn::ac
{
	class : public COSModule
	{
		public:
		std::string_view GetName() override
		{
			return "nn_ac";
		}

		void RPLMapped() override
		{
			osLib_addFunction("nn_ac", "GetAssignedAddress__Q2_2nn2acFPUl", nnAcExport_GetAssignedAddress);
			osLib_addFunction("nn_ac", "GetAssignedSubnet__Q2_2nn2acFPUl", nnAcExport_GetAssignedSubnet);

			osLib_addFunction("nn_ac", "IsSystemConnected__Q2_2nn2acFPbPQ3_2nn2ac6ApType", nnAcExport_IsSystemConnected);

			osLib_addFunction("nn_ac", "IsConfigExisting__Q2_2nn2acFQ3_2nn2ac11ConfigIdNumPb", nnAcExport_IsConfigExisting);

			osLib_addFunction("nn_ac", "ACGetAssignedAddress", nnAcExport_ACGetAssignedAddress);

			nn_ac::load();
		};
	}s_COSnnAcModule;

	COSModule* GetModule()
	{
		return &s_COSnnAcModule;
	}
}
