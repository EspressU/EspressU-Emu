#pragma once

void iosuCrypto_init();

bool iosuCrypto_getDeviceId(uint32* deviceId);
void iosuCrypto_getDeviceSerialString(char* serialString);

struct ECCPrivKey
{
	uint8 keyData[30];
};

struct ECCPubKey
{
	uint8 keyData[60];
};

struct ECCSig
{
	uint8 keyData[60];
};

struct CHash256
{
	uint8 b[32];
};

struct CertECC_t
{
	enum class SIGTYPE : uint32
	{
		ECC_SHA256 = 0x00010005
	};

	 betype<SIGTYPE>	sigType;
	 uint8				signature[0x3C];
	 uint8				ukn040[0x40];
	 char				certificateSubject[0x40];
	 char				ukn0C0[0x4];
	 char				ngName[0x40];
	 uint32				ngKeyId;
	 uint8				publicKey[0x3C];
	 uint8				padding[0x180 - 0x144];

};

static_assert(sizeof(CertECC_t) == 0x180);

sint32 iosuCrypto_getDeviceCertificateBase64Encoded(char* output);
bool iosuCrypto_verifyCert(CertECC_t& cert);

std::string iosuCrypto_base64Encode(unsigned char const* bytes_to_encode, unsigned int inputLen);

bool iosuCrypto_addClientCertificate(void* sslctx, sint32 certificateId);
bool iosuCrypto_addCACertificate(void* sslctx, sint32 certificateId);
bool iosuCrypto_addCustomCACertificate(void* sslctx, uint8* certData, sint32 certLength);

uint8* iosuCrypto_getCertificateDataById(sint32 certificateId, sint32* certificateSize);
uint8* iosuCrypto_getCertificatePrivateKeyById(sint32 certificateId, sint32* certificateSize);

enum
{
	IOS_CRYPTO_ONLINE_REQ_OK,
	IOS_CRYPTO_ONLINE_REQ_OTP_MISSING,
	IOS_CRYPTO_ONLINE_REQ_OTP_CORRUPTED,
	IOS_CRYPTO_ONLINE_REQ_SEEPROM_MISSING,
	IOS_CRYPTO_ONLINE_REQ_SEEPROM_CORRUPTED,
	IOS_CRYPTO_ONLINE_REQ_MISSING_FILE
};

sint32 iosuCrypt_checkRequirementsForOnlineMode(std::string& additionalErrorInfo);
void iosuCrypto_readOtpData(void* output, sint32 wordIndex, sint32 size);

std::vector<const wchar_t*> iosuCrypt_getCertificateKeys();
std::vector<const wchar_t*> iosuCrypt_getCertificateNames();