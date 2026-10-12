#include "Cafe/OS/common/OSCommon.h"
#include "zlib125.h"
#include "zlib.h"

typedef struct {
	 MEMPTR<uint8>		next_in;
	 uint32be			avail_in;
	 uint32be			total_in;

	 MEMPTR<uint8>		next_out;
	 uint32be			avail_out;
	 uint32be			total_out;

	 MEMPTR<char>		msg;
	 MEMPTR<void>		state;

	 MEMPTR<void>		zalloc;
	 MEMPTR<void>		zfree;
	 MEMPTR<void>		opaque;

	 uint32be			data_type;
	 uint32be			adler;
	 uint32be			reserved;
}z_stream_ppc2;

static_assert(sizeof(z_stream_ppc2) == 0x38);

voidpf zcallocWrapper(voidpf opaque, uInt items, uInt size)
{
	z_stream_ppc2* zstream = (z_stream_ppc2*)opaque;
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->gpr[3] = zstream->opaque.GetMPTR();
	hCPU->gpr[4] = items;
	hCPU->gpr[5] = size;
	PPCCore_executeCallbackInternal(zstream->zalloc.GetMPTR());
	memset(memory_getPointerFromVirtualOffset(hCPU->gpr[3]), 0, items*size);
	return memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
}

void zcfreeWrapper(voidpf opaque, voidpf baseIndex)
{
	z_stream_ppc2* zstream = (z_stream_ppc2*)opaque;
	PPCInterpreter_t* hCPU = PPCInterpreter_getCurrentInstance();
	hCPU->gpr[3] = zstream->opaque.GetMPTR();
	hCPU->gpr[4] = memory_getVirtualOffsetFromPointer(baseIndex);
	PPCCore_executeCallbackInternal(zstream->zfree.GetMPTR());
	return;
}

void zlib125_zcalloc(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32(opaque, 0);
	ppcDefineParamU32(items, 1);
	ppcDefineParamU32(size, 2);

	hCPU->gpr[3] = items*size;
	hCPU->instructionPointer = gCoreinitData->MEMAllocFromDefaultHeap.GetMPTR();
}

void zlib125_zcfree(PPCInterpreter_t* hCPU)
{
	ppcDefineParamU32(opaque, 0);
	ppcDefineParamMPTR(baseIndex, 1);

	hCPU->gpr[3] = baseIndex;
	hCPU->instructionPointer = gCoreinitData->MEMFreeToDefaultHeap.GetMPTR();
}

void zlib125_setupHostZStream(z_stream_ppc2* input, z_stream* output, bool fixInternalStreamPtr = true)
{
	output->next_in = input->next_in.GetPtr();
	output->avail_in = (uint32)input->avail_in;
	output->total_in = (uint32)input->total_in;

	output->next_out = input->next_out.GetPtr();
	output->avail_out = (uint32)input->avail_out;
	output->total_out = (uint32)input->total_out;

	output->msg = input->msg.GetPtr();
	output->state = (internal_state*)input->state.GetPtr();

	output->zalloc = zcallocWrapper;
	output->zfree = zcfreeWrapper;
	output->opaque = (void*)input;

	output->data_type = input->data_type;
	output->adler = (uint32)input->adler;
	output->reserved = (uint32)input->reserved;

	if (fixInternalStreamPtr && output->state)
	{
		(*(void**)(output->state)) = output;
	}
}

void zlib125_setupUpdateZStream(z_stream* input, z_stream_ppc2* output)
{
	output->next_in = input->next_in;
	output->avail_in = (uint32)input->avail_in;
	output->total_in = (uint32)input->total_in;

	output->next_out = input->next_out;
	output->avail_out = (uint32)input->avail_out;
	output->total_out = (uint32)input->total_out;

	output->msg = input->msg;
	output->state = (void*)input->state;

	output->data_type = input->data_type;
	output->adler = input->adler;
	output->reserved = input->reserved;

}

void zlib125Export_inflateInit_(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamStr(version, 1);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs, false);

	if (zstream->zalloc == nullptr)
		zstream->zalloc = PPCInterpreter_makeCallableExportDepr(zlib125_zcalloc);
	if (zstream->zfree == nullptr)
		zstream->zfree = PPCInterpreter_makeCallableExportDepr(zlib125_zcfree);

	sint32 r = inflateInit_(&hzs, version, sizeof(z_stream));

	zlib125_setupUpdateZStream(&hzs, zstream);

	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_inflateInit2_(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(windowBits, 1);
	ppcDefineParamStr(version, 2);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs, false);

	if (zstream->zalloc == nullptr)
		zstream->zalloc = PPCInterpreter_makeCallableExportDepr(zlib125_zcalloc);
	if (zstream->zfree == nullptr)
		zstream->zfree = PPCInterpreter_makeCallableExportDepr(zlib125_zcfree);

	sint32 r = inflateInit2_(&hzs, windowBits, version, sizeof(z_stream));

	zlib125_setupUpdateZStream(&hzs, zstream);

	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_inflate(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(flushParam, 1);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = inflate(&hzs, flushParam);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_inflateEnd(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = inflateEnd(&hzs);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_inflateReset(PPCInterpreter_t* hCPU)
{
	debug_printf("inflateReset(0x%08x)\n", hCPU->gpr[3]);
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = inflateReset(&hzs);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_inflateReset2(PPCInterpreter_t* hCPU)
{
	debug_printf("inflateReset2(0x%08x,0x%08x)\n", hCPU->gpr[3], hCPU->gpr[4]);
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(windowBits, 1);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = inflateReset2(&hzs, windowBits);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_deflateInit_(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(level, 1);
	ppcDefineParamStr(version, 2);
	ppcDefineParamS32(streamsize, 3);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs, false);

	if (zstream->zalloc == nullptr)
		zstream->zalloc = PPCInterpreter_makeCallableExportDepr(zlib125_zcalloc);
	if (zstream->zfree == nullptr)
		zstream->zfree = PPCInterpreter_makeCallableExportDepr(zlib125_zcfree);

	if (streamsize != sizeof(z_stream_ppc2))
		assert_dbg();

	sint32 r = deflateInit_(&hzs, level, version, sizeof(z_stream));

	zlib125_setupUpdateZStream(&hzs, zstream);

	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_deflateInit2_(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(level, 1);
	ppcDefineParamS32(method, 2);
	ppcDefineParamS32(windowBits, 3);
	ppcDefineParamS32(memLevel, 4);
	ppcDefineParamS32(strategy, 5);
	ppcDefineParamStr(version, 6);
	ppcDefineParamS32(streamsize, 7);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs, false);

	if (zstream->zalloc == nullptr)
		zstream->zalloc = PPCInterpreter_makeCallableExportDepr(zlib125_zcalloc);
	if (zstream->zfree == nullptr)
		zstream->zfree = PPCInterpreter_makeCallableExportDepr(zlib125_zcfree);

	zstream->zalloc = PPCInterpreter_makeCallableExportDepr(zlib125_zcalloc);
	zstream->zfree = PPCInterpreter_makeCallableExportDepr(zlib125_zcfree);

	if (streamsize != sizeof(z_stream_ppc2))
		assert_dbg();

	sint32 r = deflateInit2_(&hzs, level, method, windowBits, memLevel, strategy, version, sizeof(z_stream));

	zlib125_setupUpdateZStream(&hzs, zstream);

	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_deflateBound(PPCInterpreter_t* hCPU)
{
	debug_printf("deflateBound(0x%08x,0x%08x)\n", hCPU->gpr[3], hCPU->gpr[4]);
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(sourceLen, 1);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = deflateBound(&hzs, sourceLen);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_deflate(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);
	ppcDefineParamS32(flushParam, 1);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = deflate(&hzs, flushParam);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_deflateEnd(PPCInterpreter_t* hCPU)
{
	ppcDefineParamStructPtr(zstream, z_stream_ppc2, 0);

	z_stream hzs;
	zlib125_setupHostZStream(zstream, &hzs);

	sint32 r = deflateEnd(&hzs);

	zlib125_setupUpdateZStream(&hzs, zstream);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_uncompress(PPCInterpreter_t* hCPU)
{
	uint8* memDst = memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint8* memSrc = memory_getPointerFromVirtualOffset(hCPU->gpr[5]);
	uint32* pDestLenBE = (uint32*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
	uint32 sourceLen = hCPU->gpr[6];
	uLong destLen = _swapEndianU32(*pDestLenBE);
	sint32 r = uncompress(memDst, &destLen, memSrc, sourceLen);
	*pDestLenBE = _swapEndianU32(destLen);

	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_compress(PPCInterpreter_t* hCPU)
{
	uint8* memDst = memory_getPointerFromVirtualOffset(hCPU->gpr[3]);
	uint8* memSrc = memory_getPointerFromVirtualOffset(hCPU->gpr[5]);
	uint32* pDestLenBE = (uint32*)memory_getPointerFromVirtualOffset(hCPU->gpr[4]);
	uint32 sourceLen = hCPU->gpr[6];
	uLong destLen = _swapEndianU32(*pDestLenBE);
	sint32 r = compress(memDst, &destLen, memSrc, sourceLen);
	*pDestLenBE = _swapEndianU32(destLen);
	osLib_returnFromFunction(hCPU, r);
}

void zlib125Export_crc32(PPCInterpreter_t* hCPU)
{
	uint32 crc = hCPU->gpr[3];
	uint8* buf = (uint8*)memory_getPointerFromVirtualOffsetAllowNull(hCPU->gpr[4]);
	uint32 len = hCPU->gpr[5];

	uint32 crcResult = crc32(crc, buf, len);

	osLib_returnFromFunction(hCPU, crcResult);
}

void zlib125Export_compressBound(PPCInterpreter_t* hCPU)
{
	uint32 result = compressBound(hCPU->gpr[3]);
	osLib_returnFromFunction(hCPU, result);
}

namespace zlib
{
	void load()
	{

	}

	class : public COSModule
	{
		public:
		std::string_view GetName() override
		{
			return "zlib125";
		}

		void RPLMapped() override
		{
			osLib_addFunction("zlib125", "inflateInit2_", zlib125Export_inflateInit2_);
			osLib_addFunction("zlib125", "inflateInit_", zlib125Export_inflateInit_);
			osLib_addFunction("zlib125", "inflateEnd", zlib125Export_inflateEnd);
			osLib_addFunction("zlib125", "inflate", zlib125Export_inflate);
			osLib_addFunction("zlib125", "inflateReset", zlib125Export_inflateReset);
			osLib_addFunction("zlib125", "inflateReset2", zlib125Export_inflateReset2);

			osLib_addFunction("zlib125", "deflateInit_", zlib125Export_deflateInit_);
			osLib_addFunction("zlib125", "deflateInit2_", zlib125Export_deflateInit2_);
			osLib_addFunction("zlib125", "deflateBound", zlib125Export_deflateBound);
			osLib_addFunction("zlib125", "deflate", zlib125Export_deflate);
			osLib_addFunction("zlib125", "deflateEnd", zlib125Export_deflateEnd);

			osLib_addFunction("zlib125", "uncompress", zlib125Export_uncompress);
			osLib_addFunction("zlib125", "compress", zlib125Export_compress);

			osLib_addFunction("zlib125", "crc32", zlib125Export_crc32);
			osLib_addFunction("zlib125", "compressBound", zlib125Export_compressBound);
		};

	}s_COSZlib125Module;

	COSModule* GetModule()
	{
		return &s_COSZlib125Module;
	}
}
