#pragma once

enum class CafeConsoleRegion;

typedef struct ec_key_st EC_KEY;
typedef struct ec_point_st EC_POINT;
typedef struct ECDSA_SIG_st ECDSA_SIG;

namespace NCrypto
{
	std::string base64Encode(const void* inputMem, size_t inputLen);
	std::vector<uint8> base64Decode(std::string_view inputStr);

	struct AesKey
	{
		static constexpr size_t SIZE = 16;
		uint8 b[SIZE];
	};

	struct AesIv
	{
		static constexpr size_t SIZE = 16;
		uint8 iv[SIZE];
	};

	struct ECCPrivKey
	{
		uint8 keyData[30];

		void setPrivateKey(EC_KEY* key);
		EC_KEY* getPrivateKey() const;

		static ECCPrivKey getDeviceCertPrivateKey();
	};

	struct ECCPubKey
	{
		uint8 x[30];
		uint8 y[30];

		EC_KEY* getPublicKey();
		EC_POINT* getPublicKeyAsPoint();

		[[nodiscard]] static ECCPubKey generateFromPrivateKey(ECCPrivKey& privKey);
	};

	struct ECCSig
	{
		uint8 r[30];
		uint8 s[30];

		ECDSA_SIG* getSignature();
		void setSignature(ECDSA_SIG* sig);

		bool doesMatch(ECCSig& otherSig) const
		{
			return memcmp(this->r, otherSig.r, sizeof(this->r)) == 0 && memcmp(this->s, otherSig.s, sizeof(this->s)) == 0;
		}

		std::string encodeToBase64()
		{
			return base64Encode(this, 60);
		}
	};

	struct CHash256
	{
		uint8 b[32];
	};

	struct CHash160
	{
		uint8 b[20];
	};

	struct CertECC
	{
		enum class SIGTYPE : uint32
		{
			ECC_SHA1   = 0x00010002,
			ECC_SHA256 = 0x00010005
		};

		 betype<SIGTYPE>	signatureType;
		 ECCSig				signature;
		 uint8				ukn040[0x40];
		 char				issuer[0x40];
		 char				ukn0C0[0x4];
		 char				ngName[0x40];
		 uint32				date;
		 ECCPubKey			publicKey;
		 uint8				padding[0x180 - 0x144];

		bool decodeFromBase64(std::string_view input);
		std::string encodeToBase64();

		bool verifySignatureViaPubKey(ECCPubKey& signerPubKey);
		void sign(ECCPrivKey& signerPrivKey);

		static CertECC GetDeviceCertificate();
		static CertECC generateCertificate(uint32 signerTitleIdHigh, uint32 signerTitleIdLow, ECCPrivKey& privKeySigner, ECCPrivKey& privKeyIn, ECCPubKey& pubKeyOut);
	};

	static_assert(sizeof(CertECC) == 0x180);

	class ETicketParser
	{
	public:
		bool parse(const uint8* data, size_t size);
		static bool Depersonalize(uint8* ticketData, size_t ticketSize, uint32 deviceId, const ECCPrivKey& devicePrivKey);

		uint64 GetTicketId() const
		{
			return m_ticketId;
		}

		uint64 GetTitleId() const
		{
			return m_titleId;
		}

		uint16 GetTicketVersion() const
		{
			return m_titleVersion;
		}

		void GetTitleKey(AesKey& key);

		bool IsPersonalized()
		{
			return m_isPersonalized;
		}

		bool CheckRight(size_t index)
		{
			if (index >= m_contentRights.size())
				return false;
			return m_contentRights[index];
		}

	private:
		uint64 m_titleId;
		uint16 m_titleVersion;
		uint64 m_ticketId;
		uint8 m_ticketFormatVersion;
		uint8 m_encryptedTitleKey[16];
		bool m_isPersonalized;
		uint32 m_deviceId;
		ECCPubKey m_publicKey;
		std::vector<bool> m_contentRights;
	};

	class TMDParser
	{
	public:
		enum class TMDContentFlags : uint16
		{
			FLAG_SHA1 = 0x2000,
			FLAG_HASHED_CONTENT = 0x0002,
		};

		struct ContentEntry
		{
			uint32 contentId;
			uint16 index;
			TMDContentFlags contentFlags;
			uint64 size;
			uint8 hash32[32];
		};

		uint64 getTitleId() const
		{
			return m_titleId;
		}

		uint16 getTitleVersion() const
		{
			return m_titleVersion;
		}

		bool parse(const uint8* data, size_t size);
		std::vector<ContentEntry>& GetContentList()
		{
			return m_content;
		}

	private:
		uint64 m_titleId;
		uint16 m_titleVersion;
		std::vector<ContentEntry> m_content;
	};

	DEFINE_ENUM_FLAG_OPERATORS(TMDParser::TMDContentFlags);

	void GenerateHashSHA256(const void* data, size_t len, CHash256& hashOut);
	ECCSig signHash(uint32 signerTitleIdHigh, uint32 signerTitleIdLow, uint8* hash, sint32 hashLen, CertECC& certChainOut);

	uint32 GetDeviceId();
	std::string GetSerial();
	CafeConsoleRegion SEEPROM_GetRegion();
	std::string GetRegionAsString(CafeConsoleRegion regionCode);
	const char* GetCountryAsString(sint32 index);
	size_t GetCountryCount();
	bool SEEPROM_IsPresent();
	bool OTP_IsPresent();

	bool HasDataForConsoleCert();

	void unitTests();
}