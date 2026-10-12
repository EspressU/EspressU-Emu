#include "Cafe/OS/common/OSCommon.h"
#include "coreinit_HWInterface.h"

namespace coreinit
{
	enum class RegisterInterfaceId : uint32
	{
		INTERFACE_VI_UKN = 0,

		INTERFACE_VI2_UKN = 3,

	};

	enum class SysRegisterInterfaceId : uint32
	{
		INTERFACE_UKN = 0,

		INTERFACE_3_ACR_VI = 3,

		INTERFACE_6_SI = 6,
		INTERFACE_7_AI_PROBABLY = 7,

	};

	PAddr _GetRegisterPhysicalAddress(RegisterInterfaceId interfaceId, uint32 offset)
	{
		PAddr base = 0;
		switch (interfaceId)
		{
		case RegisterInterfaceId::INTERFACE_VI_UKN:
			base = 0x0C1E0000;
			break;
		default:
			cemu_assert_debug(false);
			return 0;
		}
		return base + offset;
	}

	PAddr _GetSysRegisterPhysicalAddress(SysRegisterInterfaceId interfaceId, uint32 offset)
	{
		PAddr base = 0;
		switch (interfaceId)
		{
		case SysRegisterInterfaceId::INTERFACE_3_ACR_VI:
			base = 0x0D00021C;
			break;
		case SysRegisterInterfaceId::INTERFACE_6_SI:
			base = 0x0D006400;
			break;
		default:
			cemu_assert_debug(false);
			return 0;
		}
		return base + offset;
	}

	uint32 OSReadRegister32(RegisterInterfaceId interfaceId, uint32 offset)
	{
		PAddr regAddr = _GetRegisterPhysicalAddress(interfaceId, offset);
		cemu_assert_debug(regAddr);
		return MMU::ReadMMIO_32(regAddr);
	}

	uint16 OSReadRegister16(RegisterInterfaceId interfaceId, uint32 offset)
	{
		PAddr regAddr = _GetRegisterPhysicalAddress(interfaceId, offset);
		cemu_assert_debug(regAddr);
		return MMU::ReadMMIO_16(regAddr);
	}

	void OSWriteRegister16(uint16 newValue, RegisterInterfaceId interfaceId, uint32 offset)
	{
		static bool s_dbg = false;
		if (!s_dbg)
		{
			cemu_assert_debug(false);
			s_dbg = true;
		}
	}

	void OSWriteRegister32(uint16 newValue, RegisterInterfaceId interfaceId, uint32 offset)
	{
		static bool s_dbg = false;
		if (!s_dbg)
		{
			cemu_assert_debug(false);
			s_dbg = true;
		}
	}

	void OSModifyRegister16(RegisterInterfaceId interfaceId, uint32 uknR4, uint32 uknR5, uint32 uknR6)
	{
		static bool s_dbg = false;
		if (!s_dbg)
		{
			cemu_assert_debug(false);
			s_dbg = true;
		}
	}

	uint32 __OSReadRegister32Ex(SysRegisterInterfaceId interfaceId, uint32 registerId)
	{
		uint32 offset = registerId * 4;
		cemu_assert_debug(offset < 0x40);
		PAddr regAddr = _GetSysRegisterPhysicalAddress(interfaceId, offset);
		cemu_assert_debug(regAddr);
		return MMU::ReadMMIO_32(regAddr);
	}

	void __OSWriteRegister32Ex(SysRegisterInterfaceId interfaceId, uint32 registerId, uint32 newValue)
	{
		uint32 offset = registerId * 4;
		cemu_assert_debug(offset < 0x40);
		PAddr regAddr = _GetSysRegisterPhysicalAddress(interfaceId, offset);
		cemu_assert_debug(regAddr);
		MMU::WriteMMIO_32(regAddr, newValue);
	}

	void InitializeHWInterface()
	{
		cafeExportRegister("coreinit", OSReadRegister32, LogType::Placeholder);
		cafeExportRegister("coreinit", OSReadRegister16, LogType::Placeholder);
		cafeExportRegister("coreinit", OSWriteRegister16, LogType::Placeholder);
		cafeExportRegister("coreinit", OSWriteRegister32, LogType::Placeholder);
		cafeExportRegister("coreinit", OSModifyRegister16, LogType::Placeholder);

		cafeExportRegister("coreinit", __OSReadRegister32Ex, LogType::Placeholder);
		cafeExportRegister("coreinit", __OSWriteRegister32Ex, LogType::Placeholder);
	};
};