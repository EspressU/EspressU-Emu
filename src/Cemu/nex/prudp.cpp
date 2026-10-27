#include "prudp.h"
#include "util/crypto/md5.h"

#include <bitset>
#include <random>

#include <boost/random/uniform_int.hpp>

static void KSA(unsigned char* key, int keyLen, unsigned char* S)
{
	for (int i = 0; i < RC4_N; i++)
		S[i] = i;
	int j = 0;
	for (int i = 0; i < RC4_N; i++)
	{
		j = (j + S[i] + key[i % keyLen]) % RC4_N;
		std::swap(S[i], S[j]);
	}
}

static void PRGA(unsigned char* S, unsigned char* input, int len, unsigned char* output)
{
	for (size_t n = 0; n < len; n++)
	{
		int i = (i + 1) % RC4_N;
		int j = (j + S[i]) % RC4_N;
		std::swap(S[i], S[j]);
		int rnd = S[(S[i] + S[j]) % RC4_N];
		output[n] = rnd ^ input[n];
	}
}

static void RC4(char* key, unsigned char* input, int len, unsigned char* output)
{
	unsigned char S[RC4_N];
	KSA((unsigned char*)key, (int)strlen(key), S);
	PRGA(S, input, len, output);
}

void RC4_initCtx(RC4Ctx* rc4Ctx, const char* key)
{
	rc4Ctx->i = 0;
	rc4Ctx->j = 0;
	KSA((unsigned char*)key, (int)strlen(key), rc4Ctx->S);
}

void RC4_initCtx(RC4Ctx* rc4Ctx, unsigned char* key, int keyLen)
{
	rc4Ctx->i = 0;
	rc4Ctx->j = 0;
	KSA(key, keyLen, rc4Ctx->S);
}

void RC4_transform(RC4Ctx* rc4Ctx, unsigned char* input, int len, unsigned char* output)
{
	int i = rc4Ctx->i;
	int j = rc4Ctx->j;

	for (size_t n = 0; n < len; n++)
	{
		i = (i + 1) % RC4_N;
		j = (j + rc4Ctx->S[i]) % RC4_N;
		std::swap(rc4Ctx->S[i], rc4Ctx->S[j]);
		int rnd = rc4Ctx->S[(rc4Ctx->S[i] + rc4Ctx->S[j]) % RC4_N];
		output[n] = rnd ^ input[n];
	}
	rc4Ctx->i = i;
	rc4Ctx->j = j;
}

uint32 prudpGetMSTimestamp()
{
	return GetTickCount();
}

std::mt19937_64 prudpRG(GetTickCount());
boost::random::uniform_int_distribution<int> prudpRandomDistribution8(0, 0xFF);
boost::random::uniform_int_distribution<int> prudpRandomDistributionPortGen(0, 10000);

uint8 prudp_generateRandomU8()
{
	return prudpRandomDistribution8(prudpRG);
}

uint32 prudp_generateRandomU32()
{
	uint32 v = prudp_generateRandomU8();
	v <<= 8;
	v |= prudp_generateRandomU8();
	v <<= 8;
	v |= prudp_generateRandomU8();
	v <<= 8;
	v |= prudp_generateRandomU8();
	return v;
}

std::bitset<10000> _portUsageMask;

static uint16 AllocateRandomSrcPRUDPPort()
{
	while (true)
	{
		sint32 p = prudpRandomDistributionPortGen(prudpRG);
		if (_portUsageMask.test(p))
			continue;
		_portUsageMask.set(p);
		return 40000 + p;
	}
}

static void ReleasePRUDPSrcPort(uint16 port)
{
	cemu_assert_debug(port >= 40000);
	uint32 bitIndex = port - 40000;
	cemu_assert_debug(_portUsageMask.test(bitIndex));
	_portUsageMask.reset(bitIndex);
}

static uint8 prudp_calculateChecksum(uint8 checksumBase, uint8* data, sint32 length)
{
	uint32 checksum32 = 0;
	for (sint32 i = 0; i < length / 4; i++)
	{
		checksum32 += *(uint32*)(data + i * 4);
	}
	uint8 checksum = checksumBase;
	for (sint32 i = length & (~3); i < length; i++)
	{
		checksum += data[i];
	}
	checksum += (uint8)((checksum32 >> 0) & 0xFF);
	checksum += (uint8)((checksum32 >> 8) & 0xFF);
	checksum += (uint8)((checksum32 >> 16) & 0xFF);
	checksum += (uint8)((checksum32 >> 24) & 0xFF);
	return checksum;
}

sint32 prudpPacket::calculateSizeFromPacketData(uint8* data, sint32 length)
{
	if (length < 0xB)
		return 0;
	uint16 typeAndFlags = *(uint16*)(data + 0x02);
	uint16 type = (typeAndFlags & 0xF);
	uint16 flags = (typeAndFlags >> 4);
	if ((flags & FLAG_HAS_SIZE) == 0)
		return length;
	sint32 calculatedSize;
	if (type == TYPE_SYN)
	{
		if (length < (0xB + 0x4 + 2))
			return 0;
		uint16 payloadSize = *(uint16*)(data + 0xB + 0x4);
		calculatedSize = 0xB + 0x4 + 2 + (sint32)payloadSize + 1;
		if (calculatedSize > length)
			return 0;
		return calculatedSize;
	}
	else if (type == TYPE_CON)
	{
		if (length < (0xB + 0x4 + 2))
			return 0;
		uint16 payloadSize = *(uint16*)(data + 0xB + 0x4);
		calculatedSize = 0xB + 0x4 + 2 + (sint32)payloadSize + 1;
		if (calculatedSize > length)
			return 0;
		return calculatedSize;
	}
	else if (type == TYPE_DATA)
	{
		if (length < (0xB + 1 + 2))
			return 0;
		uint16 payloadSize = *(uint16*)(data + 0xB + 1);
		calculatedSize = 0xB + 1 + 2 + (sint32)payloadSize + 1;
		if (calculatedSize > length)
			return 0;
		return calculatedSize;
	}
	else if (type == TYPE_PING)
	{
		if (length < (0xB + 2))
			return 0;
		uint16 payloadSize = *(uint16*)(data + 0xB);
		calculatedSize = 0xB + 2 + (sint32)payloadSize + 1;
		if (calculatedSize > length)
			return 0;
		return calculatedSize;
	}
	else
		assert_dbg();
	return length;
}

prudpPacket::prudpPacket(prudpStreamSettings* streamSettings, uint8 src, uint8 dst, uint8 type, uint16 flags, uint8 sessionId, uint16 sequenceId, uint32 packetSignature)
{
	this->src = src;
	this->dst = dst;
	this->type = type;
	this->flags = flags;
	this->sessionId = sessionId;
	this->m_sequenceId = sequenceId;
	this->specifiedPacketSignature = packetSignature;
	this->streamSettings = streamSettings;
	this->fragmentIndex = 0;
	this->isEncrypted = false;
}

bool prudpPacket::requiresAck()
{
	return (flags & FLAG_NEED_ACK) != 0;
}

sint32 prudpPacket::buildData(uint8* output, sint32 maxLength)
{
	if (this->type == prudpPacket::TYPE_DATA)
	{
		if (packetData.size() > 0)
		{
			if (isEncrypted == false)
			{
				RC4_transform(&streamSettings->rc4Client, &packetData.front(), (int)packetData.size(), &packetData.front());
				isEncrypted = true;
			}
		}
	}
	uint8* packetBuffer = output;
	sint32 writeIndex = 0;
	cemu_assert(maxLength >= 0xB);
	*(uint8*)(packetBuffer + 0x00) = src;
	*(uint8*)(packetBuffer + 0x01) = dst;
	uint16 typeAndFlags = (this->flags << 4) | (this->type);
	*(uint16*)(packetBuffer + 0x02) = typeAndFlags;
	*(uint8*)(packetBuffer + 0x04) = sessionId;
	*(uint32*)(packetBuffer + 0x05) = packetSignature();
	*(uint16*)(packetBuffer + 0x09) = m_sequenceId;
	writeIndex = 0xB;
	if (this->type == TYPE_SYN)
	{
		cemu_assert(maxLength >= (0xB + 4));
		*(uint32*)(packetBuffer + writeIndex) = 0;
		writeIndex += 4;
	}
	else if (this->type == TYPE_CON)
	{
		cemu_assert(maxLength >= (0xB + packetData.size()));
		memcpy(packetBuffer + writeIndex, &packetData.front(), packetData.size());
		writeIndex += (int)packetData.size();
	}
	else if (this->type == TYPE_DATA)
	{
		cemu_assert(maxLength >= (0xB + 1 + packetData.size()));
		*(uint8*)(packetBuffer + writeIndex) = fragmentIndex;
		writeIndex += 1;
		if (packetData.empty() == false)
		{
			memcpy(packetBuffer + writeIndex, &packetData.front(), packetData.size());
			writeIndex += (int)packetData.size();
		}
	}
	else if (this->type == TYPE_PING)
	{
	}
	else
	{
		cemu_assert_suspicious();
	}
	cemu_assert(maxLength >= (writeIndex + 1));
	*(uint8*)(packetBuffer + writeIndex) = calculateChecksum(packetBuffer, writeIndex);
	writeIndex++;

	return writeIndex;
}

uint32 prudpPacket::packetSignature()
{
	if (type == TYPE_SYN)
		return 0;
	else if (type == TYPE_CON || type == TYPE_PING)
		return specifiedPacketSignature;
	else if (type == TYPE_DATA)
	{
		if (packetData.empty())
			return 0x12345678;

		HMACMD5Ctx ctx;
		hmacMD5_init_limK_to_64(streamSettings->accessKeyDigest, 16, &ctx);
		hmacMD5_update(&packetData.front(), (int)packetData.size(), &ctx);

		uint8 digest[16];
		hmacMD5_final(digest, &ctx);
		uint32 pSig = *(uint32*)digest;
		return pSig;
	}

	assert_dbg();
	return 0;
}

void prudpPacket::setData(uint8* data, sint32 length)
{
	packetData.assign(data, data + length);
}

void prudpPacket::setFragmentIndex(uint8 fragmentIndex)
{
	this->fragmentIndex = fragmentIndex;
}

uint8 prudpPacket::calculateChecksum(uint8* data, sint32 length)
{
	return prudp_calculateChecksum(streamSettings->checksumBase, data, length);
}

prudpIncomingPacket::prudpIncomingPacket()
{
	src = 0;
	dst = 0;
	flags = 0;
	type = 0;
	sessionId = 0;
	packetSignature = 0;
	sequenceId = 0;
	fragmentIndex = 0;
	hasData = false;
	isInvalid = false;
	streamSettings = nullptr;
}

prudpIncomingPacket::prudpIncomingPacket(prudpStreamSettings* streamSettings, uint8* data, sint32 length)
	: prudpIncomingPacket()
{
	if (length < 0xB + 1)
	{
		isInvalid = true;
		return;
	}
	this->streamSettings = streamSettings;
	uint8 actualChecksum = calculateChecksum(data, length - 1);
	uint8 packetChecksum = *(uint8*)(data + length - 1);
	if (actualChecksum != packetChecksum)
	{
		isInvalid = true;
		return;
	}
	length--;
	this->src = *(uint8*)(data + 0x00);
	this->dst = *(uint8*)(data + 0x01);
	uint16 typeAndFlags = *(uint16*)(data + 0x02);
	this->flags = (typeAndFlags >> 4) & 0xFFF;
	this->type = (typeAndFlags & 0xF);
	this->sessionId = *(uint8*)(data + 0x4);
	this->packetSignature = *(uint32*)(data + 0x5);
	this->sequenceId = *(uint16*)(data + 0x9);
	sint32 readIndex = 0xB;
	if (this->type == prudpPacket::TYPE_SYN)
	{
		if ((length - readIndex) < 4)
		{
			isInvalid = true;
			return;
		}
		packetData.resize(4);
		*(uint32*)(&packetData.front()) = *(uint32*)(data + readIndex);
		hasData = true;
		readIndex += 4;
		return;
	}
	else if (this->type == prudpPacket::TYPE_CON)
	{
		if (readIndex < 4)
		{
			isInvalid = true;
			return;
		}
	}
	else if (this->type == prudpPacket::TYPE_PING)
	{
	}
	else if (this->type == prudpPacket::TYPE_DATA)
	{

		bool hasPayloadSize = (this->flags & prudpPacket::FLAG_HAS_SIZE) != 0;
		if ((length - readIndex) < 1 + (hasPayloadSize ? 2 : 0))
		{
			isInvalid = true;
			return;
		}
		this->fragmentIndex = *(uint8*)(data + readIndex);
		readIndex += sizeof(uint8);
		if (hasPayloadSize)
		{
			uint16 payloadSize = *(uint16*)(data + readIndex);
			readIndex += sizeof(uint16);
			if ((length - readIndex) != payloadSize)
			{
				assert_dbg();
			}
		}
		if (readIndex < length)
		{
			sint32 dataSize = length - readIndex;
			packetData.resize(dataSize);
			memcpy(&packetData.front(), data + readIndex, dataSize);
		}
		return;
	}
	else if (this->type == prudpPacket::TYPE_DISCONNECT)
	{
	}
	else
	{
		cemu_assert_suspicious();
	}
}

bool prudpIncomingPacket::hasError()
{
	return isInvalid;
}

uint8 prudpIncomingPacket::calculateChecksum(uint8* data, sint32 length)
{
	return prudp_calculateChecksum(streamSettings->checksumBase, data, length);
}

void prudpIncomingPacket::decrypt()
{
	if (packetData.empty())
		return;
	RC4_transform(&streamSettings->rc4Server, &packetData.front(), (int)packetData.size(), &packetData.front());
}

#define PRUDP_VPORT(__streamType, __port) (((__streamType) << 4) | (__port))

prudpClient::prudpClient()
{
	m_currentConnectionState = ConnectionState::Connecting;
	m_serverConnectionSignature = 0;
	m_clientConnectionSignature = 0;
	m_incomingSequenceId = 0;

	m_clientSessionId = 0;
	m_serverSessionId = 0;
}

prudpClient::prudpClient(uint32 dstIp, uint16 dstPort, const char* key)
	: prudpClient()
{
	m_dstIp = dstIp;
	m_dstPort = dstPort;
	for (sint32 tries = 0; tries < 5; tries++)
	{
		m_srcPort = AllocateRandomSrcPRUDPPort();
		m_socketUdp = (SOCKET)-1; // EspressU makes no network connections
		struct sockaddr_in udpServer;
		udpServer.sin_family = AF_INET;
		udpServer.sin_addr.s_addr = INADDR_ANY;
		udpServer.sin_port = htons(m_srcPort);
		if (bind(m_socketUdp, (struct sockaddr*)&udpServer, sizeof(udpServer)) == SOCKET_ERROR)
		{
			ReleasePRUDPSrcPort(m_srcPort);
			m_srcPort = 0;
			if (tries == 4)
			{
				cemuLog_log(LogType::Force, "PRUDP: Failed to bind UDP socket");
				m_currentConnectionState = ConnectionState::Disconnected;
				return;
			}
			closesocket(m_socketUdp);
			continue;
		}
		else
			break;
	}
#if BOOST_OS_WINDOWS
	u_long nonBlockingMode = 1;
	ioctlsocket(m_socketUdp, FIONBIO, &nonBlockingMode);
#else
	int flags = fcntl(m_socketUdp, F_GETFL);
	fcntl(m_socketUdp, F_SETFL, flags | O_NONBLOCK);
#endif
	m_srcVPort = PRUDP_VPORT(prudpPacket::STREAM_TYPE_SECURE, 0xF);
	m_dstVPort = PRUDP_VPORT(prudpPacket::STREAM_TYPE_SECURE, 0x1);
	uint8 checksumBase = 0;
	for (sint32 i = 0; key[i] != '\0'; i++)
	{
		checksumBase += key[i];
	}
	m_streamSettings.checksumBase = checksumBase;
	MD5_CTX md5Ctx;
	MD5_Init(&md5Ctx);
	MD5_Update(&md5Ctx, key, (int)strlen(key));
	MD5_Final(m_streamSettings.accessKeyDigest, &md5Ctx);
	RC4_initCtx(&m_streamSettings.rc4Server, "CD&ML");
	RC4_initCtx(&m_streamSettings.rc4Client, "CD&ML");
	SendCurrentHandshakePacket();
	m_incomingSequenceId = 1;
}

prudpClient::prudpClient(uint32 dstIp, uint16 dstPort, const char* key, prudpAuthServerInfo* authInfo)
	: prudpClient(dstIp, dstPort, key)
{
	RC4_initCtx(&m_streamSettings.rc4Server, authInfo->secureKey, 16);
	RC4_initCtx(&m_streamSettings.rc4Client, authInfo->secureKey, 16);
	m_isSecureConnection = true;
	memcpy(&m_authInfo, authInfo, sizeof(prudpAuthServerInfo));
}

prudpClient::~prudpClient()
{
	if (m_srcPort != 0)
	{
		ReleasePRUDPSrcPort(m_srcPort);
		closesocket(m_socketUdp);
	}
}

void prudpClient::AcknowledgePacket(uint16 sequenceId)
{
	auto it = std::begin(m_dataPacketsWithAckReq);
	while (it != std::end(m_dataPacketsWithAckReq))
	{
		if (it->packet->GetSequenceId() == sequenceId)
		{
			delete it->packet;
			m_dataPacketsWithAckReq.erase(it);
			return;
		}
		it++;
	}
}

void prudpClient::SortIncomingDataPacket(std::unique_ptr<prudpIncomingPacket> incomingPacket)
{
	uint16 sequenceIdIncomingPacket = incomingPacket->sequenceId;
	sint32 insertIndex = 0;
	while (insertIndex < m_incomingPacketQueue.size())
	{
		uint16 seqDif = sequenceIdIncomingPacket - m_incomingPacketQueue[insertIndex]->sequenceId;
		if (seqDif & 0x8000)
			break;
#ifdef CEMU_DEBUG_ASSERT
		if (seqDif == 0)
			assert_dbg();
#endif
		insertIndex++;
	}
	m_incomingPacketQueue.insert(m_incomingPacketQueue.begin() + insertIndex, std::move(incomingPacket));
#ifdef CEMU_DEBUG_ASSERT
	for (sint32 i = 1; i < m_incomingPacketQueue.size(); i++)
	{
		uint16 seqDif = m_incomingPacketQueue[i]->sequenceId - m_incomingPacketQueue[i - 1]->sequenceId;
		if (seqDif & 0x8000)
			seqDif = -seqDif;
		if (seqDif >= 0x8000)
			assert_dbg();
	}
#endif
}

sint32 prudpClient::KerberosEncryptData(uint8* input, sint32 length, uint8* output)
{
	RC4Ctx rc4Kerberos;
	RC4_initCtx(&rc4Kerberos, m_authInfo.secureKey, 16);
	memcpy(output, input, length);
	RC4_transform(&rc4Kerberos, output, length, output);
	hmacMD5(this->m_authInfo.secureKey, 16, output, length, output + length);
	return length + 16;
}

void prudpClient::SendCurrentHandshakePacket()
{
	if (!m_hasSynAck)
	{
		prudpPacket synPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_SYN, prudpPacket::FLAG_NEED_ACK, 0, 0, 0);
		DirectSendPacket(&synPacket);
	}
	else
	{
		prudpPacket conPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_CON, prudpPacket::FLAG_NEED_ACK | prudpPacket::FLAG_RELIABLE, this->m_clientSessionId, 1, m_serverConnectionSignature);
		if (this->m_isSecureConnection)
		{
			uint8 tempBuffer[512];
			nexPacketBuffer conData(tempBuffer, sizeof(tempBuffer), true);
			conData.writeU32(m_clientConnectionSignature);
			conData.writeBuffer(m_authInfo.secureTicket, m_authInfo.secureTicketLength);
			uint8 requestData[4 * 3];
			uint8 requestDataEncrypted[4 * 3 + 0x10];
			*(uint32*)(requestData + 0x0) = m_authInfo.userPid;
			*(uint32*)(requestData + 0x4) = m_authInfo.server.cid;
			*(uint32*)(requestData + 0x8) = prudp_generateRandomU32();
			sint32 encryptedSize = KerberosEncryptData(requestData, sizeof(requestData), requestDataEncrypted);
			conData.writeBuffer(requestDataEncrypted, encryptedSize);
			conPacket.setData(conData.getDataPtr(), conData.getWriteIndex());
		}
		else
		{
			conPacket.setData((uint8*)&m_clientConnectionSignature, sizeof(uint32));
		}
		DirectSendPacket(&conPacket);
	}
	m_lastHandshakeTimestamp = prudpGetMSTimestamp();
	m_handshakeRetryCount++;
}

void prudpClient::HandleIncomingPacket(std::unique_ptr<prudpIncomingPacket> incomingPacket)
{
	if (incomingPacket->type == prudpPacket::TYPE_PING)
	{
		if (incomingPacket->flags & prudpPacket::FLAG_ACK)
		{
			if (incomingPacket->flags & prudpPacket::FLAG_NEED_ACK)
				cemuLog_log(LogType::PRUDP, "[PRUDP] Received unexpected ping packet with both ACK and NEED_ACK set");
			if (m_unacknowledgedPingCount > 0)
			{
				if (incomingPacket->sequenceId == m_outgoingSequenceId_ping)
				{
					cemuLog_log(LogType::PRUDP, "[PRUDP] Received ping packet ACK (unacknowledged count: {})", m_unacknowledgedPingCount);
					m_unacknowledgedPingCount = 0;
				}
				else
				{
					cemuLog_log(LogType::PRUDP, "[PRUDP] Received ping packet ACK with wrong sequenceId (expected: {}, received: {})", m_outgoingSequenceId_ping, incomingPacket->sequenceId);
				}
			}
			else
			{
				cemuLog_log(LogType::PRUDP, "[PRUDP] Received ping packet ACK which we dont need");
			}
		}
		else if (incomingPacket->flags & prudpPacket::FLAG_NEED_ACK)
		{
			cemuLog_log(LogType::PRUDP, "[PRUDP] Received ping packet with NEED_ACK set. Sending ACK back");
			prudpPacket ackPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_PING, prudpPacket::FLAG_ACK, this->m_clientSessionId, incomingPacket->sequenceId, 0);
			if(!incomingPacket->packetData.empty())
				ackPacket.setData(incomingPacket->packetData.data(), incomingPacket->packetData.size());
			DirectSendPacket(&ackPacket);
		}
		return;
	}
	else if (incomingPacket->type == prudpPacket::TYPE_SYN)
	{
		if (!(incomingPacket->flags & prudpPacket::FLAG_ACK))
		{
			cemuLog_log(LogType::Force, "[PRUDP] Received SYN packet without ACK flag set");
			return;
		}
		if (m_hasSynAck || !incomingPacket->hasData || incomingPacket->packetData.size() != 4)
		{
			cemuLog_log(LogType::PRUDP, "[PRUDP] Received unexpected SYN packet");
			return;
		}
		m_hasSynAck = true;
		this->m_serverConnectionSignature = *(uint32*)&incomingPacket->packetData.front();
		this->m_clientSessionId = prudp_generateRandomU8();
		this->m_clientConnectionSignature = prudp_generateRandomU32();
		m_handshakeRetryCount = 0;
		SendCurrentHandshakePacket();
		return;
	}
	else if (incomingPacket->type == prudpPacket::TYPE_CON)
	{
		if (!m_hasSynAck || m_hasConAck)
		{
			cemuLog_log(LogType::PRUDP, "[PRUDP] Received unexpected CON packet");
			return;
		}
		if (!(incomingPacket->flags & prudpPacket::FLAG_ACK))
		{
			cemuLog_log(LogType::Force, "[PRUDP] Received CON packet without ACK flag set");
			return;
		}
		m_hasConAck = true;
		m_handshakeRetryCount = 0;
		cemu_assert_debug(m_currentConnectionState == ConnectionState::Connecting);
		m_lastPingTimestamp = prudpGetMSTimestamp();
		cemu_assert_debug(m_serverSessionId == 0);
		m_serverSessionId = incomingPacket->sessionId;
		m_currentConnectionState = ConnectionState::Connected;
		cemuLog_log(LogType::PRUDP, "[PRUDP] Connection established. ClientSession {:02x} ServerSession {:02x}", m_clientSessionId, m_serverSessionId);
		return;
	}
	else if (incomingPacket->type == prudpPacket::TYPE_DATA)
	{
		if (incomingPacket->flags & prudpPacket::FLAG_ACK)
		{
			AcknowledgePacket(incomingPacket->sequenceId);
			if(!incomingPacket->packetData.empty())
				cemuLog_log(LogType::PRUDP, "[PRUDP] Received ACK data packet with payload");
			return;
		}
		if (incomingPacket->flags & prudpPacket::FLAG_NEED_ACK)
		{
			prudpPacket ackPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_DATA, prudpPacket::FLAG_ACK, this->m_clientSessionId, incomingPacket->sequenceId, 0);
			DirectSendPacket(&ackPacket);
		}
		if (incomingPacket->packetData.empty())
			return;
		uint16 seqDist = incomingPacket->sequenceId - m_incomingSequenceId;
		if (seqDist >= 0xC000)
		{
			return;
		}
		for (auto& it : m_incomingPacketQueue)
		{
			if (it->sequenceId == incomingPacket->sequenceId)
			{
				cemuLog_log(LogType::PRUDP, "Duplicate PRUDP packet received");
				return;
			}
		}
		SortIncomingDataPacket(std::move(incomingPacket));
	}
	else if (incomingPacket->type == prudpPacket::TYPE_DISCONNECT)
	{
		m_currentConnectionState = ConnectionState::Disconnected;
		return;
	}
	else
	{
		cemuLog_log(LogType::PRUDP, "[PRUDP] Received unknown packet type");
	}
}

bool prudpClient::Update()
{
	if (m_currentConnectionState == ConnectionState::Disconnected)
		return false;
	uint32 currentTimestamp = prudpGetMSTimestamp();
	uint8 receiveBuffer[4096];
	while (true)
	{
		sockaddr receiveFrom = {0};
		socklen_t receiveFromLen = sizeof(receiveFrom);
		sint32 r = recvfrom(m_socketUdp, (char*)receiveBuffer, sizeof(receiveBuffer), 0, &receiveFrom, &receiveFromLen);
		if (r >= 0)
		{
			sint32 pIdx = 0;
			while (pIdx < r)
			{
				sint32 packetLength = prudpPacket::calculateSizeFromPacketData(receiveBuffer + pIdx, r - pIdx);
				if (packetLength <= 0 || (pIdx + packetLength) > r)
				{
					cemuLog_log(LogType::Force, "[PRUDP] Invalid packet length");
					break;
				}
				auto incomingPacket = std::make_unique<prudpIncomingPacket>(&m_streamSettings, receiveBuffer + pIdx, packetLength);
				pIdx += packetLength;
				if (incomingPacket->hasError())
				{
					cemuLog_log(LogType::Force, "[PRUDP] Packet error");
					break;
				}
				if (incomingPacket->type != prudpPacket::TYPE_CON && incomingPacket->sessionId != m_serverSessionId)
				{
					cemuLog_log(LogType::PRUDP, "[PRUDP] Invalid session id");
					continue;
				}
				HandleIncomingPacket(std::move(incomingPacket));
			}
		}
		else
			break;
	}
	for (auto& it : m_dataPacketsWithAckReq)
	{
		if ((currentTimestamp - it.lastRetryTimestamp) >= 2300)
		{
			if (it.retryCount >= 7)
			{
				m_currentConnectionState = ConnectionState::Disconnected;
			}
			DirectSendPacket(it.packet);
			it.lastRetryTimestamp = currentTimestamp;
			it.retryCount++;
		}
	}
	if (m_currentConnectionState == ConnectionState::Connecting)
	{
		uint32 timeSinceLastHandshake = currentTimestamp - m_lastHandshakeTimestamp;
		if (timeSinceLastHandshake >= 1200)
		{
			if (m_handshakeRetryCount >= 5)
			{
				m_currentConnectionState = ConnectionState::Disconnected;
				cemuLog_log(LogType::PRUDP, "PRUDP: Failed to connect");
				return false;
			}
			SendCurrentHandshakePacket();
		}
	}
	else if (m_currentConnectionState == ConnectionState::Connected)
	{
		if (m_unacknowledgedPingCount != 0)
		{
			if ((currentTimestamp - m_lastPingTimestamp) >= 1500)
			{
				cemuLog_log(LogType::PRUDP, "[PRUDP] Resending ping packet (no ack received)");
				if (m_unacknowledgedPingCount >= 10)
				{
					m_currentConnectionState = ConnectionState::Disconnected;
					cemuLog_log(LogType::PRUDP, "PRUDP: Connection did not receive a ping response in a while. Assuming disconnect");
					return false;
				}
				prudpPacket pingPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_PING, prudpPacket::FLAG_NEED_ACK, this->m_clientSessionId, this->m_outgoingSequenceId_ping, m_serverConnectionSignature);
				DirectSendPacket(&pingPacket);
				m_unacknowledgedPingCount++;
				m_lastPingTimestamp = currentTimestamp;
			}
		}
		else
		{
			if ((currentTimestamp - m_lastPingTimestamp) >= 20000)
			{
				cemuLog_log(LogType::PRUDP, "[PRUDP] Sending new ping packet with sequenceId {}", this->m_outgoingSequenceId_ping + 1);
				m_outgoingSequenceId_ping++;
				prudpPacket pingPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_PING, prudpPacket::FLAG_NEED_ACK, this->m_clientSessionId, this->m_outgoingSequenceId_ping, m_serverConnectionSignature);
				DirectSendPacket(&pingPacket);
				m_unacknowledgedPingCount++;
				m_lastPingTimestamp = currentTimestamp;
			}
		}
	}
	return false;
}

void prudpClient::DirectSendPacket(prudpPacket* packet)
{
	uint8 packetBuffer[prudpPacket::PACKET_RAW_SIZE_MAX];
	sint32 len = packet->buildData(packetBuffer, prudpPacket::PACKET_RAW_SIZE_MAX);
	if (len > prudpPacket::PACKET_RAW_SIZE_MAX)
	{
		cemuLog_log(LogType::Force, "PRUDP: Packet exceeds maximum size");
		return;
	}
	sockaddr_in destAddr;
	destAddr.sin_family = AF_INET;
	destAddr.sin_port = htons(m_dstPort);
	destAddr.sin_addr.s_addr = m_dstIp;
	sendto(m_socketUdp, (const char*)packetBuffer, len, 0, (const sockaddr*)&destAddr, sizeof(destAddr));
}

void prudpClient::QueuePacket(prudpPacket* packet)
{
	cemu_assert_debug(packet->GetType() == prudpPacket::TYPE_DATA);
	if (packet->requiresAck())
	{
		m_dataPacketsWithAckReq.emplace_back(packet, prudpGetMSTimestamp());
		DirectSendPacket(packet);
	}
	else
	{
		DirectSendPacket(packet);
		delete packet;
	}
}

void prudpClient::SendDatagram(uint8* input, sint32 length, bool reliable)
{
	cemu_assert_debug(reliable);
	cemu_assert_debug(m_hasSynAck && m_hasConAck);
	if (length > prudpPacket::PACKET_RAW_SIZE_MAX - 13)
	{
		cemuLog_logOnce(LogType::Force, "PRUDP: Datagram too long. Fragmentation not implemented yet");
		return;
	}
	uint16 flags = prudpPacket::FLAG_NEED_ACK;
	if (reliable)
		flags |= prudpPacket::FLAG_RELIABLE;
	prudpPacket* packet = new prudpPacket(&m_streamSettings, m_srcVPort, m_dstVPort, prudpPacket::TYPE_DATA, flags, m_clientSessionId, m_outgoingReliableSequenceId, 0);
	if (reliable)
		m_outgoingReliableSequenceId++;
	packet->setFragmentIndex(0);
	packet->setData(input, length);
	QueuePacket(packet);
}

sint32 prudpClient::ReceiveDatagram(std::vector<uint8>& outputBuffer)
{
	outputBuffer.clear();
	if (m_incomingPacketQueue.empty())
		return -1;
	prudpIncomingPacket* frontPacket = m_incomingPacketQueue[0].get();
	if (frontPacket->sequenceId != this->m_incomingSequenceId)
		return -1;
	if (frontPacket->fragmentIndex == 0)
	{
		frontPacket->decrypt();
		if (!frontPacket->packetData.empty())
		{
			constexpr size_t BUFFER_TARGET_SIZE = 1024 * 32;
			if (frontPacket->packetData.size() < BUFFER_TARGET_SIZE && outputBuffer.capacity() > BUFFER_TARGET_SIZE)
			{
				outputBuffer.resize(BUFFER_TARGET_SIZE);
				outputBuffer.shrink_to_fit();
				outputBuffer.clear();
			}
			cemu_assert_debug(outputBuffer.empty());
			outputBuffer.insert(outputBuffer.end(), frontPacket->packetData.begin(), frontPacket->packetData.end());
		}
		m_incomingPacketQueue.erase(m_incomingPacketQueue.begin());
		this->m_incomingSequenceId++;
		return (sint32)outputBuffer.size();
	}
	else
	{
		if (frontPacket->fragmentIndex != 1)
			return -1;
		sint32 packetIndex = 1;
		sint32 chainLength = -1;
		for (sint32 i = 1; i < m_incomingPacketQueue.size(); i++)
		{
			uint8 itFragmentIndex = m_incomingPacketQueue[packetIndex]->fragmentIndex;
			if (m_incomingPacketQueue[packetIndex]->sequenceId != (m_incomingSequenceId + i))
				return -1;
			if (itFragmentIndex == 0)
			{
				chainLength = i + 1;
				break;
			}
			packetIndex++;
		}
		if (chainLength < 1)
			return -1;
		cemu_assert_debug(outputBuffer.empty());
		for (sint32 i = 0; i < chainLength; i++)
		{
			prudpIncomingPacket* incomingPacket = m_incomingPacketQueue[i].get();
			incomingPacket->decrypt();
			outputBuffer.insert(outputBuffer.end(), incomingPacket->packetData.begin(), incomingPacket->packetData.end());
		}
		m_incomingPacketQueue.erase(m_incomingPacketQueue.begin(), m_incomingPacketQueue.begin() + chainLength);
		m_incomingSequenceId += chainLength;
		return (sint32)outputBuffer.size();
	}
	return -1;
}
