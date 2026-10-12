#pragma once

#include "Cafe/OS/libs/padscore/padscore.h"
#include "Cafe/OS/RPL/COSModule.h"

namespace vpad
{
	void load();
	void start();
}

#define VPAD_MAX_CONTROLLERS (2)

struct BtnRepeat
{
	sint32 delay, pulse;
};

enum VPADTouchValidity
{
	kTpValid = 0,
	kTpInvalidX = 1,
	kTpInvalidY = 2,
	kTpInvalid = kTpInvalidX | kTpInvalidY,
};

enum VPADTouchState
{
	kTpTouchOff = 0,
	kTpTouchOn = 1,
};

struct VPADDir
{
	beVec3D_t x;
	beVec3D_t y;
	beVec3D_t z;

	VPADDir() = default;
	VPADDir(const beVec3D_t& x, const beVec3D_t& y, const beVec3D_t& z)
		: x(x), y(y), z(z) {}

};

static_assert(sizeof(VPADDir) == 0x24);

struct VPADTPData_t
{
	uint16be x;
	uint16be y;
	uint16be touch;
	uint16be validity;
};

static_assert(sizeof(VPADTPData_t) == 8);

typedef struct VPADStatus
{
	 uint32be hold;
	 uint32be trig;
	 uint32be release;
	 beVec2D_t leftStick;
	 beVec2D_t rightStick;
	 beVec3D_t acc;
	 float32be accMagnitude;
	 float32be accAcceleration;
	 beVec2D_t accXY;
	 beVec3D_t gyroChange;
	 beVec3D_t gyroOrientation;
	 sint8 vpadErr;
	 uint8 padding1[1];
	 VPADTPData_t tpData;
	 VPADTPData_t tpProcessed1;
	 VPADTPData_t tpProcessed2;
		uint8 padding2[2];
	 VPADDir dir;
	 uint8 headphoneStatus;
	 uint8 padding3[3];
	 beVec3D_t magnet;
	 uint8 slideVolume;
	 uint8 batteryLevel;
	 uint8 micStatus;
	 uint8 slideVolume2;
	 uint8 padding4[8];
}VPADStatus_t;

static_assert(sizeof(VPADStatus) == 0xAC);

namespace vpad
{
	COSModule* GetModule();
}
