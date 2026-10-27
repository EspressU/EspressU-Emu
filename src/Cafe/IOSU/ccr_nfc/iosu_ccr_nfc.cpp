#include "iosu_ccr_nfc.h"
#include "Cafe/IOSU/kernel/iosu_kernel.h"
#include "util/crypto/aes128.h"
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace iosu
{
	namespace ccr_nfc
	{
		IOSMsgQueueId sCCRNFCMsgQueue;
		SysAllocator<iosu::kernel::IOSMessage, 0x20> sCCRNFCMsgQueueMsgBuffer;
		std::thread sCCRNFCThread;

		constexpr uint8 sNfcKey[16] = {}; // no key material in this build
		constexpr uint8 sNfcKeyIV[16] = {}; // no key material in this build

		constexpr uint8 sUnfixedInfosMagicBytes[16] = {}; // no key material in this build
		constexpr uint8 sLockedSecretMagicBytes[16] = {}; // no key material in this build
		constexpr uint8 sUnfixedInfosString[] = { 0x75, 0x6E, 0x66, 0x69, 0x78, 0x65, 0x64, 0x20, 0x69, 0x6E, 0x66, 0x6F, 0x73, 0x00, 0x00, 0x00 };
		constexpr uint8 sLockedSecretString[] = { 0x6C, 0x6F, 0x63, 0x6B, 0x65, 0x64, 0x20, 0x73, 0x65, 0x63, 0x72, 0x65, 0x74, 0x00, 0x00, 0x00 };

		constexpr uint8 sLockedSecretHmacKey[16] = {}; // no key material in this build
		constexpr uint8 sUnfixedInfosHmacKey[16] = {}; // no key material in this build

		uint8 sLockedSecretInternalKey[0x10];
		uint8 sLockedSecretInternalNonce[0x10];
		uint8 sLockedSecretInternalHmacKey[0x10];

		uint8 sUnfixedInfosInternalKey[0x10];
		uint8 sUnfixedInfosInternalNonce[0x10];
		uint8 sUnfixedInfosInternalHmacKey[0x10];

		sint32 __CCRNFCValidateCryptData(CCRNFCCryptData* data, uint32 size, bool validateOffsets)
		{
			if (!data)
			{
				return CCR_NFC_ERROR;
			}

			if (size != sizeof(CCRNFCCryptData))
			{
				return CCR_NFC_ERROR;
			}

			if (!validateOffsets)
			{
				return 0;
			}

			if (data->version == 0)
			{
				if (data->unfixedInfosHmacOffset < 0x1C9 && data->unfixedInfosOffset < 0x1C9 &&
					data->lockedSecretHmacOffset < 0x1C9 && data->lockedSecretOffset < 0x1C9 &&
					data->lockedSecretSize < 0x1C9 && data->unfixedInfosSize < 0x1C9)
				{
					return 0;
				}
			}
			else if (data->version == 2)
			{
				if (data->unfixedInfosHmacOffset < 0x21D && data->unfixedInfosOffset < 0x21D &&
					data->lockedSecretHmacOffset < 0x21D && data->lockedSecretOffset < 0x21D &&
					data->lockedSecretSize < 0x21D && data->unfixedInfosSize < 0x21D)
				{
					return 0;
				}
			}

			return CCR_NFC_ERROR;
		}

		sint32 CCRNFCAESCTRCrypt(const uint8* key, const void* ivNonce, const void* inData, uint32 inSize, void* outData, uint32 outSize)
		{
			uint8 tmpIv[0x10];
			memcpy(tmpIv, ivNonce, sizeof(tmpIv));

			memcpy(outData, inData, inSize);
			AES128CTR_transform((uint8*)outData, outSize, (uint8*)key, tmpIv);
			return 0;
		}

		sint32 __CCRNFCGenerateKey(const uint8* hmacKey, uint32 hmacKeySize, const uint8* name, uint32 nameSize, const uint8* inData, uint32 inSize, uint8* outData, uint32 outSize)
		{
			if (nameSize != 0xe || outSize != 0x40)
			{
				return CCR_NFC_ERROR;
			}

			uint8 buffer[0x50];
			buffer[0] = 0;
			buffer[1] = 0;
			memcpy(buffer + 2, name, nameSize);
			memcpy(buffer + nameSize + 2, inData, inSize);

			uint16 counter = 0;
			while (outSize > 0)
			{
				buffer[0] = (counter >> 8) & 0xFF;
				buffer[1] = counter & 0xFF;
				counter++;

				uint32 dataSize = outSize;
				if (!HMAC(EVP_sha256(), hmacKey, hmacKeySize, buffer, sizeof(buffer), outData, &dataSize))
				{
					return CCR_NFC_ERROR;
				}

				outSize -= 0x20;
				outData += 0x20;
			}

			return 0;
		}

		sint32 __CCRNFCGenerateInternalKeys(const CCRNFCCryptData* in, const uint8* keyGenSalt)
		{
			uint8 lockedSecretBuffer[0x40] = { 0 };
			uint8 unfixedInfosBuffer[0x40] = { 0 };
			uint8 outBuffer[0x40] = { 0 };

			memcpy(lockedSecretBuffer, sLockedSecretMagicBytes, sizeof(sLockedSecretMagicBytes));
			if (in->version == 0)
			{
				memcpy(lockedSecretBuffer + 0x10, in->data + in->uuidOffset, 0x10);
			}
			else if (in->version == 2)
			{
				memcpy(lockedSecretBuffer + 0x10, in->data + in->uuidOffset, 8);
				memcpy(lockedSecretBuffer + 0x18, in->data + in->uuidOffset, 8);
			}
			else
			{
				return CCR_NFC_ERROR;
			}
			memcpy(lockedSecretBuffer + 0x20, keyGenSalt, 0x20);

			sint32 res = __CCRNFCGenerateKey(sLockedSecretHmacKey, sizeof(sLockedSecretHmacKey), sLockedSecretString, 0xe, lockedSecretBuffer, sizeof(lockedSecretBuffer), outBuffer, sizeof(outBuffer));
			if (res != 0)
			{
				return res;
			}

			memcpy(sLockedSecretInternalKey, outBuffer, 0x10);
			memcpy(sLockedSecretInternalNonce, outBuffer + 0x10, 0x10);
			memcpy(sLockedSecretInternalHmacKey, outBuffer + 0x20, 0x10);

			memcpy(unfixedInfosBuffer, in->data + in->seedOffset, 2);
			memcpy(unfixedInfosBuffer + 2, sUnfixedInfosMagicBytes + 2, 0xe);
			if (in->version == 0)
			{
				memcpy(unfixedInfosBuffer + 0x10, in->data + in->uuidOffset, 0x10);
			}
			else if (in->version == 2)
			{
				memcpy(unfixedInfosBuffer + 0x10, in->data + in->uuidOffset, 8);
				memcpy(unfixedInfosBuffer + 0x18, in->data + in->uuidOffset, 8);
			}
			else
			{
				return CCR_NFC_ERROR;
			}
			memcpy(unfixedInfosBuffer + 0x20, keyGenSalt, 0x20);

			res = __CCRNFCGenerateKey(sUnfixedInfosHmacKey, sizeof(sUnfixedInfosHmacKey), sUnfixedInfosString, 0xe, unfixedInfosBuffer, sizeof(unfixedInfosBuffer), outBuffer, sizeof(outBuffer));
			if (res != 0)
			{
				return res;
			}

			memcpy(sUnfixedInfosInternalKey, outBuffer, 0x10);
			memcpy(sUnfixedInfosInternalNonce, outBuffer + 0x10, 0x10);
			memcpy(sUnfixedInfosInternalHmacKey, outBuffer + 0x20, 0x10);

			return 0;
		}

		sint32 __CCRNFCCryptData(const CCRNFCCryptData* in, CCRNFCCryptData* out, bool decrypt)
		{
			uint8 keyGenSalt[0x20];
			sint32 res = CCRNFCAESCTRCrypt(sNfcKey, sNfcKeyIV, in->data + in->keyGenSaltOffset, 0x20, keyGenSalt, sizeof(keyGenSalt));
			if (res != 0)
			{
				return res;
			}

			res = __CCRNFCGenerateInternalKeys(in, keyGenSalt);
			if (res != 0)
			{
				return res;
			}

			if (decrypt)
			{
				if (in->version == 0)
				{
					res = CCRNFCAESCTRCrypt(sLockedSecretInternalKey, sLockedSecretInternalNonce, in->data + in->lockedSecretOffset, in->lockedSecretSize, out->data + in->lockedSecretOffset, in->lockedSecretSize);
					if (res != 0)
					{
						return res;
					}
				}

				res = CCRNFCAESCTRCrypt(sUnfixedInfosInternalKey, sUnfixedInfosInternalNonce, in->data + in->unfixedInfosOffset, in->unfixedInfosSize, out->data + in->unfixedInfosOffset, in->unfixedInfosSize);
				if (res != 0)
				{
					return res;
				}

				uint8 hmacBuffer[0x20];
				uint32 hmacLen = sizeof(hmacBuffer);

				if (!HMAC(EVP_sha256(), sLockedSecretInternalHmacKey, sizeof(sLockedSecretInternalHmacKey), out->data + in->lockedSecretHmacOffset + 0x20, (in->dataSize - in->lockedSecretHmacOffset) - 0x20, hmacBuffer, &hmacLen))
				{
					return CCR_NFC_ERROR;
				}

				if (memcmp(in->data + in->lockedSecretHmacOffset, hmacBuffer, 0x20) != 0)
				{
					return CCR_NFC_INVALID_LOCKED_SECRET;
				}

				if (in->version == 0)
				{
					hmacLen = sizeof(hmacBuffer);
					res = HMAC(EVP_sha256(), sUnfixedInfosInternalHmacKey, sizeof(sUnfixedInfosInternalHmacKey), out->data + in->unfixedInfosHmacOffset + 0x20, (in->dataSize - in->unfixedInfosHmacOffset) - 0x20, hmacBuffer, &hmacLen) ? 0 : CCR_NFC_ERROR;
				}
				else
				{
					hmacLen = sizeof(hmacBuffer);
					res = HMAC(EVP_sha256(), sUnfixedInfosInternalHmacKey, sizeof(sUnfixedInfosInternalHmacKey), out->data + in->unfixedInfosHmacOffset + 0x21, (in->dataSize - in->unfixedInfosHmacOffset) - 0x21, hmacBuffer, &hmacLen) ? 0 : CCR_NFC_ERROR;
				}

				if (memcmp(in->data + in->unfixedInfosHmacOffset, hmacBuffer, 0x20) != 0)
				{
					return CCR_NFC_INVALID_UNFIXED_INFOS;
				}
			}
			else
			{
				uint8 hmacBuffer[0x20];
				uint32 hmacLen = sizeof(hmacBuffer);

				if (!HMAC(EVP_sha256(), sLockedSecretInternalHmacKey, sizeof(sLockedSecretInternalHmacKey), out->data + in->lockedSecretHmacOffset + 0x20, (in->dataSize - in->lockedSecretHmacOffset) - 0x20, hmacBuffer, &hmacLen))
				{
					return CCR_NFC_ERROR;
				}

				if (memcmp(in->data + in->lockedSecretHmacOffset, hmacBuffer, 0x20) != 0)
				{
					return CCR_NFC_INVALID_LOCKED_SECRET;
				}

				if (in->version == 0)
				{
					uint32 hmacLen = 0x20;
					if (!HMAC(EVP_sha256(), sUnfixedInfosInternalHmacKey, sizeof(sUnfixedInfosInternalHmacKey), out->data + in->unfixedInfosHmacOffset + 0x20, (in->dataSize - in->unfixedInfosHmacOffset) - 0x20, out->data + in->unfixedInfosHmacOffset, &hmacLen))
					{
						return CCR_NFC_ERROR;
					}

					res = CCRNFCAESCTRCrypt(sLockedSecretInternalKey, sLockedSecretInternalNonce, in->data + in->lockedSecretOffset, in->lockedSecretSize, out->data + in->lockedSecretOffset, in->lockedSecretSize);
					if (res != 0)
					{
						return res;
					}
				}
				else
				{
					uint32 hmacLen = 0x20;
					if (!HMAC(EVP_sha256(), sUnfixedInfosInternalHmacKey, sizeof(sUnfixedInfosInternalHmacKey), out->data + in->unfixedInfosHmacOffset + 0x21, (in->dataSize - in->unfixedInfosHmacOffset) - 0x21, out->data + in->unfixedInfosHmacOffset, &hmacLen))
					{
						return CCR_NFC_ERROR;
					}
				}

				res = CCRNFCAESCTRCrypt(sUnfixedInfosInternalKey, sUnfixedInfosInternalNonce, in->data + in->unfixedInfosOffset, in->unfixedInfosSize, out->data + in->unfixedInfosOffset, in->unfixedInfosSize);
				if (res != 0)
				{
					return res;
				}
			}

			return res;
		}

		void CCRNFCThread()
		{
			iosu::kernel::IOSMessage msg;
			while (true)
			{
				IOS_ERROR error = iosu::kernel::IOS_ReceiveMessage(sCCRNFCMsgQueue, &msg, 0);
				cemu_assert(!IOS_ResultIsError(error));

				if (msg == 0xf00dd00d)
				{
					return;
				}

				IPCCommandBody* cmd = MEMPTR<IPCCommandBody>(msg).GetPtr();
				if (cmd->cmdId == IPCCommandId::IOS_OPEN)
				{
					iosu::kernel::IOS_ResourceReply(cmd, IOS_ERROR_OK);
				}
				else if (cmd->cmdId == IPCCommandId::IOS_CLOSE)
				{
					iosu::kernel::IOS_ResourceReply(cmd, IOS_ERROR_OK);
				}
				else if (cmd->cmdId == IPCCommandId::IOS_IOCTL)
				{
					sint32 result;
					uint32 requestId = cmd->args[0];
					void* ptrIn = MEMPTR<void>(cmd->args[1]);
					uint32 sizeIn = cmd->args[2];
					void* ptrOut = MEMPTR<void>(cmd->args[3]);
					uint32 sizeOut = cmd->args[4];

					if ((result = __CCRNFCValidateCryptData(static_cast<CCRNFCCryptData*>(ptrIn), sizeIn, true)) == 0 &&
						(result = __CCRNFCValidateCryptData(static_cast<CCRNFCCryptData*>(ptrOut), sizeOut, false)) == 0)
					{
						memcpy(ptrOut, ptrIn, sizeIn);

						switch (requestId)
						{
						case 1:
							result = __CCRNFCCryptData(static_cast<CCRNFCCryptData*>(ptrIn), static_cast<CCRNFCCryptData*>(ptrOut), false);
							break;
						case 2:
							result = __CCRNFCCryptData(static_cast<CCRNFCCryptData*>(ptrIn), static_cast<CCRNFCCryptData*>(ptrOut), true);
							break;
						default:
							cemuLog_log(LogType::Force, "/dev/ccr_nfc: Unsupported IOCTL requestId");
							cemu_assert_suspicious();
							result = IOS_ERROR_INVALID;
							break;
						}
					}

					iosu::kernel::IOS_ResourceReply(cmd, static_cast<IOS_ERROR>(result));
				}
				else
				{
					cemuLog_log(LogType::Force, "/dev/ccr_nfc: Unsupported IPC cmdId");
					cemu_assert_suspicious();
					iosu::kernel::IOS_ResourceReply(cmd, IOS_ERROR_INVALID);
				}
			}
		}

		class : public ::IOSUModule
		{
			void SystemLaunch() override
			{
				sCCRNFCMsgQueue = iosu::kernel::IOS_CreateMessageQueue(sCCRNFCMsgQueueMsgBuffer.GetPtr(), sCCRNFCMsgQueueMsgBuffer.GetCount());
				cemu_assert(!IOS_ResultIsError(static_cast<IOS_ERROR>(sCCRNFCMsgQueue)));

				IOS_ERROR error = iosu::kernel::IOS_RegisterResourceManager("/dev/ccr_nfc", sCCRNFCMsgQueue);
				cemu_assert(!IOS_ResultIsError(error));

				sCCRNFCThread = std::thread(CCRNFCThread);
			}

			void SystemExit() override
			{
				if (sCCRNFCMsgQueue < 0)
				{
					return;
				}

				iosu::kernel::IOS_SendMessage(sCCRNFCMsgQueue, 0xf00dd00d, 0);
				sCCRNFCThread.join();

				iosu::kernel::IOS_DestroyMessageQueue(sCCRNFCMsgQueue);
				sCCRNFCMsgQueue = -1;
			}
		} sIOSUModuleCCRNFC;

		IOSUModule* GetModule()
		{
			return &sIOSUModuleCCRNFC;
		}
	}
}
