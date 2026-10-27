#pragma once
#include "util/crypto/aes128.h"
#include "openssl/evp.h"
#include "openssl/hmac.h"

typedef struct
{
	char typeString[14];
	uint8 magicBytes[16];
	uint8 magicBytesLen;
	uint8 xorPad[32];
	uint8 hmacKey[16];
}amiiboInternalKeys_t;

void amiiboCalcSeed(AmiiboInternal* internalData, uint8* seed)
{
	memcpy(seed + 0x00, (uint8*)internalData + 0x029, 0x02);
	memset(seed + 0x02, 0x00, 0x0E);
	memcpy(seed + 0x10, (uint8*)internalData + 0x1D4, 0x08);
	memcpy(seed + 0x18, (uint8*)internalData + 0x1D4, 0x08);
	memcpy(seed + 0x20, (uint8*)internalData + 0x1E8, 0x20);
}

void amiiboGenKeyInternalPrepare(amiiboInternalKeys_t* keys, uint8* seed, uint8* output, sint32& outputLen)
{
	sint32 index = 0;

	sint32 typeStringLen = (sint32)strlen(keys->typeString)+1;
	memcpy(output + index, keys->typeString, typeStringLen);
	index += typeStringLen;
	sint32 seedPart1Len = 16 - keys->magicBytesLen;
	memcpy(output + index, seed, seedPart1Len);
	index += seedPart1Len;
	memcpy(output + index, keys->magicBytes, keys->magicBytesLen);
	index += keys->magicBytesLen;
	memcpy(output + index, seed + 0x10, 16);
	index += 16;
	for (sint32 i = 0; i < 32; i++)
		output[index + i] = seed[i + 32] ^ keys->xorPad[i];
	index += 32;

	outputLen = index;
}

typedef struct
{
	uint8 hmacKey[16];
	uint8 buffer[sizeof(uint16) + 480];
	sint32 bufferSize;
	uint16 counter;
}amiiboCryptCtx_t;

typedef struct
{
	uint8 raw[32];
}drgbOutput32_t;

void amiiboCryptInit(amiiboCryptCtx_t* ctx, const uint8* hmacKey, size_t hmacKeySize, const uint8* seed, sint32 seedSize)
{
	ctx->counter = 0;
	ctx->bufferSize = (sint32)sizeof(ctx->counter) + seedSize;
	memcpy(ctx->hmacKey, hmacKey, 16);

	memcpy(ctx->buffer + sizeof(uint16), seed, seedSize);
}

void amiiboCryptStep(amiiboCryptCtx_t* ctx, drgbOutput32_t* output)
{
	ctx->buffer[0] = ctx->counter >> 8;
	ctx->buffer[1] = ctx->counter >> 0;
	ctx->counter++;
	uint32 mdLen = (uint32)sizeof(drgbOutput32_t);
	HMAC(EVP_sha256(), ctx->hmacKey, sizeof(ctx->hmacKey), (const unsigned char *)ctx->buffer, ctx->bufferSize, output->raw, &mdLen);
}

typedef struct
{
	uint8 aesKey[16];
	uint8 aesIV[16];
	uint8 hmacKey[16];
}amiiboDerivedKeys_t;

static_assert(sizeof(amiiboDerivedKeys_t) == 0x30);

void amiiboCrypto_genKey(amiiboInternalKeys_t* keys, AmiiboInternal* internalData, amiiboDerivedKeys_t* derivedKeys)
{
	uint8 seed[0x40];
	amiiboCalcSeed(internalData, seed);

	uint8 internalKey[512];
	sint32 internalKeyLen = 0;
	amiiboGenKeyInternalPrepare(keys, seed, internalKey, internalKeyLen);

	drgbOutput32_t temp[2];
	amiiboCryptCtx_t rngCtx;
	amiiboCryptInit(&rngCtx, keys->hmacKey, sizeof(keys->hmacKey), internalKey, internalKeyLen);
	amiiboCryptStep(&rngCtx, temp + 0);
	amiiboCryptStep(&rngCtx, temp + 1);
	memcpy(derivedKeys, temp, sizeof(amiiboDerivedKeys_t));
}

void amiiboCrypto_nfcFormatToInternal(AmiiboRawNFCData* nfcData, AmiiboInternal* internalData)
{
	uint8* tag = (uint8*)nfcData;
	uint8* intl = (uint8*)internalData;

	memcpy(intl + 0x000, tag + 0x008, 0x008);
	memcpy(intl + 0x008, tag + 0x080, 0x020);
	memcpy(intl + 0x028, tag + 0x010, 0x024);
	memcpy(intl + 0x04C, tag + 0x0A0, 0x168);
	memcpy(intl + 0x1B4, tag + 0x034, 0x020);
	memcpy(intl + 0x1D4, tag + 0x000, 0x008);
	memcpy(intl + 0x1DC, tag + 0x054, 0x02C);
}

void amiiboCrypto_internalToNfcFormat(AmiiboInternal* internalData, AmiiboRawNFCData* nfcData)
{
	uint8* tag = (uint8*)nfcData;
	uint8* intl = (uint8*)internalData;

	memcpy(tag + 0x008, intl + 0x000, 0x008);
	memcpy(tag + 0x080, intl + 0x008, 0x020);
	memcpy(tag + 0x010, intl + 0x028, 0x024);
	memcpy(tag + 0x0A0, intl + 0x04C, 0x168);
	memcpy(tag + 0x034, intl + 0x1B4, 0x020);
	memcpy(tag + 0x000, intl + 0x1D4, 0x008);
	memcpy(tag + 0x054, intl + 0x1DC, 0x02C);
}

void amiiboCrypto_transform(const amiiboDerivedKeys_t * keys, uint8 * in, uint8 * out)
{
	uint8 nonce[16];

	memcpy(nonce, keys->aesIV, sizeof(nonce));

	AmiiboInternal internalCopy;
	memcpy(&internalCopy, in, sizeof(internalCopy));

	if (out != in)
		memcpy(out, in, 0x188 + 0x2C);
	AES128CTR_transform(out + 0x2C, 0x188, (uint8*)keys->aesKey, nonce);

	memcpy(out + 0x000, ((uint8*)&internalCopy) + 0x000, 0x008);
	memcpy(out + 0x028, ((uint8*)&internalCopy) + 0x028, 0x004);
	memcpy(out + 0x1D4, ((uint8*)&internalCopy) + 0x1D4, 0x034);
}

void AES128_init();

void amiiboInitMasterKeys(amiiboInternalKeys_t& kData, amiiboInternalKeys_t& kTag)
{
	// EspressU ships no key material: the key sets stay empty, so tag data can not be processed.
	memset(&kData, 0, sizeof(kData));
	memset(&kTag, 0, sizeof(kTag));
}

void amiiboDecrypt()
{
	amiiboInternalKeys_t kData{};
	amiiboInternalKeys_t kTag{};
	amiiboInitMasterKeys(kData, kTag);

	amiiboDerivedKeys_t derivedKeysData{};
	amiiboDerivedKeys_t derivedKeysTag{};

	amiiboCrypto_nfcFormatToInternal(&nfp_data.amiiboNFCData, &nfp_data.amiiboInternal);

	amiiboCrypto_genKey(&kData, &nfp_data.amiiboInternal, &derivedKeysData);
	amiiboCrypto_genKey(&kTag, &nfp_data.amiiboInternal, &derivedKeysTag);

	amiiboCrypto_transform(&derivedKeysData, (uint8*)&nfp_data.amiiboInternal, (uint8*)&nfp_data.amiiboInternal);

	uint32 mdLen = 32;
	HMAC(EVP_sha256(), derivedKeysTag.hmacKey, 16, (((uint8*)&nfp_data.amiiboInternal) + 0x1D4), 0x34,
		nfp_data.amiiboInternal.tagHMAC, &mdLen);
	mdLen = 32;
	HMAC(EVP_sha256(), derivedKeysData.hmacKey, 16, (((uint8*)&nfp_data.amiiboInternal) + 0x29), 0x1DF,
		((uint8*)&nfp_data.amiiboInternal) + 0x8, &mdLen);

	bool isValidTagHMAC = memcmp(nfp_data.amiiboNFCData.tagHMAC, nfp_data.amiiboInternal.tagHMAC, 32) == 0;
	bool isValidDataHMAC = memcmp(nfp_data.amiiboNFCData.dataHMAC, nfp_data.amiiboInternal.dataHMAC, 32) == 0;
	if (!isValidTagHMAC)
		cemuLog_log(LogType::Force, "Decrypt amiibo has invalid tag HMAC");
	if (!isValidDataHMAC)
		cemuLog_log(LogType::Force, "Decrypt amiibo has invalid data HMAC");
	nfp_data.hasInvalidHMAC = !isValidTagHMAC || !isValidDataHMAC;
}

void amiiboEncrypt(AmiiboRawNFCData* nfcOutput)
{
	amiiboInternalKeys_t kData{};
	amiiboInternalKeys_t kTag{};
	amiiboInitMasterKeys(kData, kTag);

	amiiboDerivedKeys_t derivedKeysData{};
	amiiboDerivedKeys_t derivedKeysTag{};

	AmiiboInternal internalCopy;
	memcpy(&internalCopy, &nfp_data.amiiboInternal, sizeof(AmiiboInternal));

	amiiboCrypto_genKey(&kData, &internalCopy, &derivedKeysData);
	amiiboCrypto_genKey(&kTag, &internalCopy, &derivedKeysTag);

	uint8 tagHMAC[32];
	uint32 mdLen = 32;
	HMAC(EVP_sha256(), derivedKeysTag.hmacKey, 16, (((uint8*)&internalCopy) + 0x1D4), 0x34,
		tagHMAC, &mdLen);
	mdLen = 32;
	uint8 dataHMAC[32];
	HMAC(EVP_sha256(), derivedKeysData.hmacKey, 16, (((uint8*)&internalCopy) + 0x29), 0x1DF,
		dataHMAC, &mdLen);

	memset(internalCopy.tagHMAC, 0, 32);
	memset(internalCopy.dataHMAC, 0, 32);

	amiiboCrypto_transform(&derivedKeysData, (uint8*)&internalCopy, (uint8*)&internalCopy);

	memcpy(internalCopy.tagHMAC, tagHMAC, 32);
	memcpy(internalCopy.dataHMAC, dataHMAC, 32);

	amiiboCrypto_internalToNfcFormat(&internalCopy, nfcOutput);

	memcpy(nfcOutput->dynamicLock, nfp_data.amiiboNFCData.dynamicLock, 4);
	memcpy(nfcOutput->cfg0, nfp_data.amiiboNFCData.cfg0, 4);
	memcpy(nfcOutput->cfg1, nfp_data.amiiboNFCData.cfg1, 4);
}
