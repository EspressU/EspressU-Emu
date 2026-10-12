#include "util/crypto/aes128.h"
#include "WindowSystem.h"
#include "Common/FileStream.h"

void CemuCommonInit();

typedef struct
{
	 uint32be magic;
}ppcAncastHeader_t;

void loadEncryptedPPCAncastKernel()
{
	auto kernelData = FileStream::LoadIntoMemory("kernel.img");
	if (!kernelData)
		exit(-1);
	ppcAncastHeader_t* ancastHeader = (ppcAncastHeader_t*)kernelData->data();
	if(ancastHeader->magic != (uint32be)0xEFA282D9)
		assert_dbg();
	memcpy(memory_getPointerFromPhysicalOffset(0x08000000), kernelData->data(), kernelData->size());
}

void loadPPCBootrom()
{
	auto bootromData = FileStream::LoadIntoMemory("bootrom.bin");
	if (!bootromData)
		exit(-1);
	memcpy(memory_getPointerFromPhysicalOffset(0x00000000), bootromData->data(), bootromData->size());
}

void mainEmulatorLLE()
{
	CemuCommonInit();
	memory_initPhysicalLayout();

	WindowSystem::Create();
	loadPPCBootrom();
	loadEncryptedPPCAncastKernel();

	PPCTimer_waitForInit();
	PPCCoreLLE_startSingleCoreScheduler(0x00000100);
}

