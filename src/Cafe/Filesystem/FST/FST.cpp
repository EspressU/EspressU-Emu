#include "Common/precompiled.h"
#include "Common/FileStream.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "Cafe/Filesystem/WUD/wud.h"
#include "util/crypto/aes128.h"
#include "util/helpers/helpers.h"
#include "openssl/sha.h"
#include "fstUtil.h"

#include "FST.h"
#include "KeyCache.h"

#include "boost/range/adaptor/reversed.hpp"

#define SET_FST_ERROR(__code) 	if (errorCodeOut) *errorCodeOut = ErrorCode::__code

static_assert(sizeof(NCrypto::AesIv) == 16);

#if BOOST_OS_MACOS || BOOST_OS_IOS
#include <sys/sysctl.h>
#endif

static size_t FSTReadCacheBudgetBytes()
{
	static const size_t budget = []() -> size_t
	{
		constexpr size_t MB = 1024ULL * 1024ULL;
		uint64 physicalMemory = 0;
#if BOOST_OS_MACOS || BOOST_OS_IOS
		size_t len = sizeof(physicalMemory);
		if (sysctlbyname("hw.memsize", &physicalMemory, &len, nullptr, 0) != 0)
			physicalMemory = 0;
#endif
		if (physicalMemory == 0)
			return 128 * MB;
		return (size_t)std::clamp<uint64>(physicalMemory / 48, 24 * MB, 128 * MB);
	}();
	return budget;
}

class FSTDataSource
{
public:
	virtual uint64 readData(uint16 clusterIndex, uint64 clusterOffset, uint64 offset, void* data, uint64 size) = 0;
	virtual ~FSTDataSource() {};

protected:
	FSTDataSource() {};

	bool m_isOpen;
};

class FSTDataSourceWUD : public FSTDataSource
{
public:

    static FSTDataSourceWUD* Open(const fs::path& path)
    {
        wud_t* wudFile = wud_open(path);
        if (!wudFile)
            return nullptr;
        FSTDataSourceWUD* ds = new FSTDataSourceWUD();
        ds->m_wudFile = wudFile;
        ds->m_directWudFile = wud_open(path);
        ds->m_path = path;
        ds->m_prefetchThread = std::thread(&FSTDataSourceWUD::PrefetchThread, ds);
        return ds;
    }

    void SetBaseOffset(uint64 baseOffset)
    {
        m_baseOffset = baseOffset;
    }

    uint64 GetBaseOffset() const
    {
        return m_baseOffset;
    }

    uint64 readData(uint16 clusterIndex, uint64 clusterOffset, uint64 offset, void* data, uint64 size) override
    {
        cemu_assert_debug(size <= 0xFFFFFFFF);
        return ReadCached(clusterOffset + offset + m_baseOffset, data, size);
    }

    ~FSTDataSourceWUD() override
    {
        {
            std::lock_guard lock(m_prefetchMutex);
            m_prefetchShutdown = true;
        }
        m_prefetchCV.notify_one();
        if (m_prefetchThread.joinable())
            m_prefetchThread.join();
        if (m_directWudFile)
            wud_close(m_directWudFile);
        if(m_wudFile)
            wud_close(m_wudFile);
    }

protected:
    static constexpr uint64 WUD_CACHE_BLOCK_SIZE = 2ULL * 1024ULL * 1024ULL;
    static constexpr uint64 WUD_CACHE_MIN_FOREGROUND_FILL_SIZE = WUD_CACHE_BLOCK_SIZE;
    static constexpr uint32 WUD_READAHEAD_BLOCK_COUNT = 4;
    static constexpr size_t WUD_READAHEAD_MAX_QUEUE = 32;

    struct WUDCacheBlock
    {
        std::vector<uint8> data;
        uint64 lastAccess{};
    };

    FSTDataSourceWUD() {}

    uint64 ReadDirect(uint64 offset, void* data, uint64 size)
    {
        if (m_directWudFile)
        {
            std::lock_guard lock(m_directWudMutex);
            return wud_readData(m_directWudFile, data, (uint32)size, offset);
        }
        std::lock_guard lock(m_wudMutex);
        return wud_readData(m_wudFile, data, (uint32)size, offset);
    }

    WUDCacheBlock* FindCachedBlock(uint64 blockIndex)
    {
        auto itr = m_cache.find(blockIndex);
        if (itr == m_cache.end())
            return nullptr;
        itr->second->lastAccess = ++m_cacheAccessCounter;
        return itr->second.get();
    }

    void TrimCacheLocked()
    {
        while (m_cacheBytes > FSTReadCacheBudgetBytes() && !m_cache.empty())
        {
            auto dropItr = std::min_element(m_cache.begin(), m_cache.end(), [](const auto& a, const auto& b) {
                return a.second->lastAccess < b.second->lastAccess;
            });
            if (dropItr == m_cache.end())
                return;
            m_cacheBytes -= dropItr->second->data.size();
            m_cache.erase(dropItr);
        }
    }

    bool StoreCacheBlock(uint64 blockIndex, std::vector<uint8>&& data)
    {
        if (data.empty())
            return false;
        std::lock_guard lock(m_cacheMutex);
        if (m_cache.find(blockIndex) != m_cache.end())
            return true;
        auto block = std::make_unique<WUDCacheBlock>();
        block->lastAccess = ++m_cacheAccessCounter;
        block->data = std::move(data);
        m_cacheBytes += block->data.size();
        m_cache.emplace(blockIndex, std::move(block));
        TrimCacheLocked();
        return true;
    }

    bool LoadBlock(wud_t* wudFile, uint64 blockIndex)
    {
        uint64 blockOffset = blockIndex * WUD_CACHE_BLOCK_SIZE;
        long long bytesLeft = wud_getWUDSize(wudFile) - (long long)blockOffset;
        if (bytesLeft <= 0)
            return false;
        uint32 bytesToRead = (uint32)std::min<uint64>((uint64)bytesLeft, WUD_CACHE_BLOCK_SIZE);
        std::vector<uint8> data(bytesToRead);
        uint32 bytesRead = wud_readData(wudFile, data.data(), bytesToRead, blockOffset);
        if (bytesRead != bytesToRead)
            data.resize(bytesRead);
        return StoreCacheBlock(blockIndex, std::move(data));
    }

    bool LoadBlockForeground(uint64 blockIndex)
    {
        std::lock_guard lock(m_wudMutex);
        return LoadBlock(m_wudFile, blockIndex);
    }

    void QueueReadAhead(uint64 firstBlockIndex)
    {
        std::lock_guard prefetchLock(m_prefetchMutex);
        for (uint32 i = 0; i < WUD_READAHEAD_BLOCK_COUNT; i++)
        {
            uint64 blockIndex = firstBlockIndex + i;
            {
                std::lock_guard cacheLock(m_cacheMutex);
                if (m_cache.find(blockIndex) != m_cache.end())
                    continue;
            }
            if (m_prefetchQueuedBlocks.find(blockIndex) != m_prefetchQueuedBlocks.end())
                continue;
            if (m_prefetchQueue.size() >= WUD_READAHEAD_MAX_QUEUE)
            {
                m_prefetchQueuedBlocks.erase(m_prefetchQueue.front());
                m_prefetchQueue.pop_front();
            }
            m_prefetchQueue.emplace_back(blockIndex);
            m_prefetchQueuedBlocks.emplace(blockIndex);
        }
        m_prefetchCV.notify_one();
    }

    void PrefetchThread()
    {
        SetThreadName("WUD ReadAhead");
        wud_t* prefetchWud = wud_open(m_path);
        if (!prefetchWud)
            return;
        while (true)
        {
            uint64 blockIndex = 0;
            {
                std::unique_lock lock(m_prefetchMutex);
                m_prefetchCV.wait(lock, [this]() {
                    return m_prefetchShutdown || !m_prefetchQueue.empty();
                });
                if (m_prefetchShutdown)
                    break;
                blockIndex = m_prefetchQueue.front();
                m_prefetchQueue.pop_front();
                m_prefetchQueuedBlocks.erase(blockIndex);
            }
            {
                std::lock_guard cacheLock(m_cacheMutex);
                if (m_cache.find(blockIndex) != m_cache.end())
                    continue;
            }
            LoadBlock(prefetchWud, blockIndex);
        }
        wud_close(prefetchWud);
    }

    bool TryReadCachedOnly(uint64 offset, void* data, uint64 size)
    {
        uint8* dataOut = (uint8*)data;
        uint64 remaining = size;
        uint64 currentOffset = offset;
        std::lock_guard lock(m_cacheMutex);
        while (remaining > 0)
        {
            uint64 blockIndex = currentOffset / WUD_CACHE_BLOCK_SIZE;
            uint64 blockOffset = currentOffset % WUD_CACHE_BLOCK_SIZE;
            WUDCacheBlock* block = FindCachedBlock(blockIndex);
            if (!block)
                return false;
            if (blockOffset >= block->data.size())
                return false;

            uint32 bytesToCopy = (uint32)std::min<uint64>(remaining, block->data.size() - blockOffset);
            std::memcpy(dataOut, block->data.data() + blockOffset, bytesToCopy);
            dataOut += bytesToCopy;
            currentOffset += bytesToCopy;
            remaining -= bytesToCopy;
        }
        return true;
    }

    uint64 ReadCached(uint64 offset, void* data, uint64 size)
    {
        if (size < WUD_CACHE_MIN_FOREGROUND_FILL_SIZE)
        {
            if (TryReadCachedOnly(offset, data, size))
                return size;
            return ReadDirect(offset, data, size);
        }

        uint8* dataOut = (uint8*)data;
        uint64 remaining = size;
        uint64 bytesReadTotal = 0;
        uint64 currentOffset = offset;
        while (remaining > 0)
        {
            uint64 blockIndex = currentOffset / WUD_CACHE_BLOCK_SIZE;
            uint64 blockOffset = currentOffset % WUD_CACHE_BLOCK_SIZE;
            bool blockCached = false;
            {
                std::lock_guard lock(m_cacheMutex);
                blockCached = FindCachedBlock(blockIndex) != nullptr;
            }
            if (!blockCached)
            {
                QueueReadAhead(blockIndex + 1);
                if (!LoadBlockForeground(blockIndex))
                    break;
            }

            {
                std::lock_guard lock(m_cacheMutex);
                WUDCacheBlock* block = FindCachedBlock(blockIndex);
                if (!block)
                    break;
                if (blockOffset >= block->data.size())
                    break;

                uint32 bytesToCopy = (uint32)std::min<uint64>(remaining, block->data.size() - blockOffset);
                std::memcpy(dataOut, block->data.data() + blockOffset, bytesToCopy);
                dataOut += bytesToCopy;
                currentOffset += bytesToCopy;
                remaining -= bytesToCopy;
                bytesReadTotal += bytesToCopy;
            }
            QueueReadAhead(blockIndex + 1);
        }
        return bytesReadTotal;
    }

    fs::path m_path;
    wud_t* m_wudFile{};
    wud_t* m_directWudFile{};
    uint64 m_baseOffset{};
    std::vector<uint64> m_clusterOffset;
    std::mutex m_wudMutex;
    std::mutex m_directWudMutex;
    std::mutex m_cacheMutex;
    std::unordered_map<uint64, std::unique_ptr<WUDCacheBlock>> m_cache;
    size_t m_cacheBytes{};
    uint64 m_cacheAccessCounter{};
    std::thread m_prefetchThread;
    std::mutex m_prefetchMutex;
    std::condition_variable m_prefetchCV;
    std::deque<uint64> m_prefetchQueue;
    std::unordered_set<uint64> m_prefetchQueuedBlocks;
    bool m_prefetchShutdown{};
};

static thread_local std::string s_lastMissingContentFile;

static thread_local int s_quietHeaderErrors = 0;

struct QuietHeaderErrors
{
	QuietHeaderErrors() { s_quietHeaderErrors++; }
	~QuietHeaderErrors() { s_quietHeaderErrors--; }
};

std::string FSTVolume::GetLastMissingContentFile()
{
	return s_lastMissingContentFile;
}

class FolderNameResolver
{
public:
	explicit FolderNameResolver(fs::path folder) : m_folder(std::move(folder)) {}

	fs::path Resolve(const std::string& name)
	{
		std::error_code ec;
		fs::path exact = m_folder / name;
		if (fs::exists(exact, ec))
			return exact;
		if (!m_listed)
		{
			m_listed = true;
			for (auto& entry : fs::directory_iterator(m_folder, ec))
			{
				std::string entryName = _pathToUtf8(entry.path().filename());
				for (auto& c : entryName)
					c = _ansiToLower(c);
				m_lowerToReal.emplace(std::move(entryName), entry.path().filename());
			}
		}
		std::string lowerName = name;
		for (auto& c : lowerName)
			c = _ansiToLower(c);
		auto it = m_lowerToReal.find(lowerName);
		if (it == m_lowerToReal.end())
			return exact;
		return m_folder / it->second;
	}

private:
	fs::path m_folder;
	bool m_listed{false};
	std::unordered_map<std::string, fs::path> m_lowerToReal;
};

class FSTDataSourceApp : public FSTDataSource
{
public:
	static FSTDataSourceApp* Open(fs::path path, NCrypto::TMDParser& tmd)
	{
		FolderNameResolver resolver(path);
		std::vector<std::unique_ptr<FileStream>> clusterFile;
		uint32 maxIndex = 0;
		for (auto& itr : tmd.GetContentList())
			maxIndex = std::max(maxIndex, (uint32)itr.index);
		clusterFile.resize(maxIndex + 1);
		for (auto& itr : tmd.GetContentList())
		{
			const std::string appName = fmt::format("{:08x}.app", itr.contentId);
			FileStream* appFile = FileStream::openFile2(resolver.Resolve(appName));
			if (!appFile)
			{
				cemuLog_log(LogType::Force, "FST: content file {} is missing from {}", appName, _pathToUtf8(path));
				s_lastMissingContentFile = appName;
				return nullptr;
			}
			clusterFile[itr.index].reset(appFile);
		}
		FSTDataSourceApp* dsApp = new FSTDataSourceApp(std::move(clusterFile));
		return dsApp;
	}

	uint64 readData(uint16 clusterIndex, uint64 clusterOffset, uint64 offset, void* data, uint64 size) override
	{
		cemu_assert_debug(clusterIndex < m_clusterFile.size());
		cemu_assert_debug(m_clusterFile[clusterIndex].get());
		cemu_assert_debug(size <= 0xFFFFFFFF);
		if (clusterIndex >= m_clusterFile.size() || !m_clusterFile[clusterIndex].get())
			return 0;
		m_clusterFile[clusterIndex].get()->SetPosition(offset);
		return m_clusterFile[clusterIndex].get()->readData(data, (uint32)size);
	}

	~FSTDataSourceApp() override
	{
	}

private:
	FSTDataSourceApp(std::vector<std::unique_ptr<FileStream>>&& clusterFiles)
	{
		m_clusterFile = std::move(clusterFiles);
	}

	std::vector<std::unique_ptr<FileStream>> m_clusterFile;
};

constexpr size_t DISC_SECTOR_SIZE = 0x8000;

struct DiscHeaderA
{
	uint8 productCode[22];
};

struct DiscHeaderB
{
	static constexpr uint32 MAGIC_VALUE = 0xCC549EB9;

	 uint32be magic;
};

static_assert(sizeof(DiscHeaderB) == 0x04);

struct DiscPartitionTableHeader
{
	static constexpr uint32 MAGIC_VALUE = 0xCCA6E67B;

	 uint32be magic;
	 uint32be blockSize;
	 uint8 partitionTableHash[20];
	 uint32be numPartitions;
};

static_assert(sizeof(DiscPartitionTableHeader) == 0x20);

struct DiscPartitionTableEntry
{
	 uint8be partitionName[31];
	 uint8be numAddresses;
	 uint32be partitionAddress;
	 uint8 padding[0x80 - 0x24];
};

static_assert(sizeof(DiscPartitionTableEntry) == 0x80);

struct DiscPartitionHeader
{
	static constexpr uint32 MAGIC_VALUE = 0xCC93A4F5;

	 uint32be magic;
	 uint32be sectorSize;

	 uint32be ukn008;
	 uint32be ukn00C;
	 uint32be h3HashNum;
	 uint32be fstSize;
	 uint32be fstSector;
	 uint32be ukn01C;
	 uint32be ukn020;

	 uint8 fstHashType;
	 uint8 fstEncryptionType;

	 uint8be versionA;
	 uint8be ukn027;

	 uint8be _uknOrPadding028[0x18];
	 uint8be h3HashArray[32];
};

static_assert(sizeof(DiscPartitionHeader) == 0x40+0x20);

bool FSTVolume::FindDiscKey(const fs::path& path, NCrypto::AesKey& discTitleKey)
{
	std::unique_ptr<FSTDataSourceWUD> dataSource(FSTDataSourceWUD::Open(path));
	if (!dataSource)
		return false;

	uint8 header[16*3];
	if (dataSource->readData(0, 0, 0x18000 + 0x100, header, sizeof(header)) != sizeof(header))
		return false;

	uint8 headerDecrypted[sizeof(header)-16];
	for (sint32 i = 0; i < 0x7FFFFFFF; i++)
	{
		uint8* key128 = KeyCache_GetAES128(i);
		if (key128 == NULL)
			break;
		AES128_CBC_decrypt(headerDecrypted, header + 16, sizeof(headerDecrypted), key128, header);
		if (std::all_of(headerDecrypted, headerDecrypted + sizeof(headerDecrypted), [](const uint8 v) {return v == 0; }))
		{
			std::memcpy(discTitleKey.b, key128, 16);
			return true;
		}
	}

	fs::path keyPath = path;
	keyPath.replace_extension("key");
	std::unique_ptr<FileStream> keyFile(FileStream::openFile2(keyPath));
	return keyFile && keyFile->readData(discTitleKey.b, sizeof(discTitleKey.b)) == sizeof(discTitleKey.b);
}

FSTVolume* FSTVolume::OpenFromDiscImage(const fs::path& path, ErrorCode* errorCodeOut)
{
	SET_FST_ERROR(UNKNOWN_ERROR);
	KeyCache_Prepare();
	NCrypto::AesKey discTitleKey;
	if (!FindDiscKey(path, discTitleKey))
	{
		SET_FST_ERROR(DISC_KEY_MISSING);
		return nullptr;
	}
	return OpenFromDiscImage(path, discTitleKey, errorCodeOut);
}

FSTVolume* FSTVolume::OpenFromDiscImage(const fs::path& path, NCrypto::AesKey& discTitleKey, ErrorCode* errorCodeOut)

{
	SET_FST_ERROR(UNKNOWN_ERROR);
	QuietHeaderErrors quietHeaderErrors;
	std::unique_ptr<FSTDataSourceWUD> dataSource(FSTDataSourceWUD::Open(path));
	if (!dataSource)
		return nullptr;
	DiscHeaderA headerA{};
	if (dataSource->readData(0, 0, 0, &headerA, sizeof(headerA)) != sizeof(headerA))
		return nullptr;
	DiscHeaderB headerB{};
	if (dataSource->readData(0, 0, DISC_SECTOR_SIZE * 2, &headerB, sizeof(headerB)) != sizeof(headerB))
		return nullptr;
	if (headerB.magic != headerB.MAGIC_VALUE)
		return nullptr;

	uint8 partitionSector[DISC_SECTOR_SIZE];
	if (dataSource->readData(0, 0, DISC_SECTOR_SIZE * 3, partitionSector, DISC_SECTOR_SIZE) != DISC_SECTOR_SIZE)
		return nullptr;
	uint8 iv[16]{};
	AES128_CBC_decrypt(partitionSector, partitionSector, DISC_SECTOR_SIZE, discTitleKey.b, iv);
	DiscPartitionTableHeader* partitionHeader = (DiscPartitionTableHeader*)partitionSector;
	if (partitionHeader->magic != DiscPartitionTableHeader::MAGIC_VALUE)
	{
		cemuLog_log(LogType::Force, "Disc image rejected because decryption failed");
		return nullptr;
	}
	if (partitionHeader->blockSize != DISC_SECTOR_SIZE)
	{
		cemuLog_log(LogType::Force, "Disc image rejected because partition sector size is invalid");
		return nullptr;
	}
	uint32 numPartitions = partitionHeader->numPartitions;
	if (numPartitions > 30)
	{
		cemuLog_log(LogType::Force, "Disc image rejected due to exceeding the partition limit (has {} partitions)", numPartitions);
		return nullptr;
	}
	DiscPartitionTableEntry* partitionArray = (DiscPartitionTableEntry*)(partitionSector + 0x800);
	uint32 siPartitionIndex = std::numeric_limits<uint32>::max();
	uint32 gmPartitionIndex = std::numeric_limits<uint32>::max();
	for (uint32 i = 0; i < numPartitions; i++)
	{
		if (partitionArray[i].numAddresses != 1)
		{
			cemuLog_log(LogType::Force, "Disc image has unsupported partition with {} addresses", (uint32)partitionArray[i].numAddresses);
			return nullptr;
		}
		auto& name = partitionArray[i].partitionName;
		if (name[0] == 'S' && name[1] == 'I')
		{
			if (siPartitionIndex != std::numeric_limits<uint32>::max())
			{
				cemuLog_log(LogType::Force, "Disc image has multiple SI partitions. Not supported");
				return nullptr;
			}
			siPartitionIndex = i;
		}
		if (name[0] == 'G' && name[1] == 'M')
		{
			if (gmPartitionIndex == std::numeric_limits<uint32>::max())
				gmPartitionIndex = i;
		}
	}
	if (siPartitionIndex == std::numeric_limits<uint32>::max() || gmPartitionIndex == std::numeric_limits<uint32>::max())
	{
		cemuLog_log(LogType::Force, "Disc image has no SI or GM partition. Cannot read game data");
		return nullptr;
	}

	auto readPartitionHeader = [&](DiscPartitionHeader& partitionHeader, uint32 partitionIndex) -> bool
	{
		cemu_assert_debug(dataSource->GetBaseOffset() == 0);
		if (dataSource->readData(0, 0, partitionArray[partitionIndex].partitionAddress * DISC_SECTOR_SIZE, &partitionHeader, sizeof(DiscPartitionHeader)) != sizeof(DiscPartitionHeader))
			return false;
		if (partitionHeader.magic != partitionHeader.MAGIC_VALUE && partitionHeader.sectorSize != DISC_SECTOR_SIZE)
			return false;
		return true;
	};

	DiscPartitionHeader partitionHeaderSI{};
	if (!readPartitionHeader(partitionHeaderSI, siPartitionIndex))
	{
		cemuLog_log(LogType::Force, "Disc image SI partition header is invalid");
		return nullptr;
	}

	cemu_assert_debug(partitionHeaderSI.fstHashType == 0);
	cemu_assert_debug(partitionHeaderSI.fstEncryptionType == 1);

	if(partitionHeaderSI.fstHashType == 0 && partitionHeaderSI.h3HashNum != 0)
		cemuLog_log(LogType::Force, "FST: Partition uses unhashed blocks but stores a non-zero amount of H3 hashes");

	DiscPartitionHeader partitionHeaderGM{};
	if (!readPartitionHeader(partitionHeaderGM, gmPartitionIndex))
	{
		cemuLog_log(LogType::Force, "Disc image GM partition header is invalid");
		return nullptr;
	}
	cemu_assert_debug(partitionHeaderGM.fstHashType == 1);
	cemu_assert_debug(partitionHeaderGM.fstEncryptionType == 2);

	dataSource->SetBaseOffset((uint64)partitionArray[siPartitionIndex].partitionAddress * DISC_SECTOR_SIZE);
	auto siFST = OpenFST(dataSource.get(), (uint64)partitionHeaderSI.fstSector * DISC_SECTOR_SIZE, partitionHeaderSI.fstSize, &discTitleKey, static_cast<FSTVolume::ClusterHashMode>(partitionHeaderSI.fstHashType), nullptr);
	if (!siFST)
		return nullptr;
	cemu_assert_debug(!(siFST->HashIsDisabled() && partitionHeaderSI.h3HashNum != 0));
	NCrypto::ETicketParser ticketParser;
	std::vector<uint8> ticketData = siFST->ExtractFile(fmt::format("{:02x}/title.tik", gmPartitionIndex));
	if (ticketData.empty() || !ticketParser.parse(ticketData.data(), ticketData.size()))
	{
		cemuLog_log(LogType::Force, "Disc image ticket file is invalid");
		return nullptr;
	}
#if 0
	std::vector<uint8> tmdData = siFST->ExtractFile(fmt::format("{:02x}/title.tmd", gmPartitionIndex));
	if (tmdData.empty())
	{
		cemuLog_log(LogType::Force, "Disc image TMD file is missing");
		return nullptr;
	}
	NCrypto::TMDParser tmdParser;
	if (!tmdParser.parse(tmdData.data(), tmdData.size()))
	{
		cemuLog_log(LogType::Force, "Disc image TMD file is invalid");
		return nullptr;
	}
#endif
	delete siFST;
	NCrypto::AesKey gmTitleKey;
	ticketParser.GetTitleKey(gmTitleKey);
	dataSource->SetBaseOffset((uint64)partitionArray[gmPartitionIndex].partitionAddress * DISC_SECTOR_SIZE);
	FSTVolume* r = OpenFST(std::move(dataSource), (uint64)partitionHeaderGM.fstSector * DISC_SECTOR_SIZE, partitionHeaderGM.fstSize, &gmTitleKey, static_cast<FSTVolume::ClusterHashMode>(partitionHeaderGM.fstHashType), nullptr);
	if (!r)
		return nullptr;
	SET_FST_ERROR(OK);
	cemu_assert_debug(!(r->HashIsDisabled() && partitionHeaderGM.h3HashNum != 0));
	return r;
}

FSTVolume* FSTVolume::OpenFromContentFolder(fs::path folderPath, ErrorCode* errorCodeOut)
{
	SET_FST_ERROR(UNKNOWN_ERROR);
	s_lastMissingContentFile.clear();
	QuietHeaderErrors quietHeaderErrors;
	FolderNameResolver resolver(folderPath);
	FileStream* tmdFile = FileStream::openFile2(resolver.Resolve("title.tmd"));
	if (!tmdFile)
	{
		SET_FST_ERROR(BAD_TITLE_TMD);
		return nullptr;
	}
	std::vector<uint8> tmdData;
	tmdFile->extract(tmdData);
	delete tmdFile;
	NCrypto::TMDParser tmdParser;
	if (!tmdParser.parse(tmdData.data(), tmdData.size()))
	{
		SET_FST_ERROR(BAD_TITLE_TMD);
		return nullptr;
	}
	ErrorCode ticketError = ErrorCode::OK;
	bool hasTicketKey = false;
	NCrypto::AesKey titleKey;
	FileStream* ticketFile = FileStream::openFile2(resolver.Resolve("title.tik"));
	if (!ticketFile)
		ticketError = ErrorCode::TITLE_TIK_MISSING;
	else
	{
		std::vector<uint8> ticketData;
		ticketFile->extract(ticketData);
		delete ticketFile;
		NCrypto::ETicketParser ticketParser;
		if (!ticketParser.parse(ticketData.data(), ticketData.size()))
			ticketError = ErrorCode::BAD_TITLE_TIK;
		else
		{
			ticketParser.GetTitleKey(titleKey);
			hasTicketKey = true;
		}
	}
	KeyCache_Prepare();
	if (!hasTicketKey && KeyCache_GetAES128(0) == nullptr)
	{
		if (errorCodeOut)
			*errorCodeOut = ticketError;
		return nullptr;
	}
	std::unique_ptr<FSTDataSource> dataSource(FSTDataSourceApp::Open(folderPath, tmdParser));
	if (!dataSource)
	{
		SET_FST_ERROR(CONTENT_FILE_MISSING);
		return nullptr;
	}
	ClusterHashMode fstHashMode = ClusterHashMode::RAW;
	uint32 fstSize = 0;
	for (auto& itr : tmdParser.GetContentList())
	{
		if (itr.index == 0)
		{
			if (HAS_FLAG(itr.contentFlags, NCrypto::TMDParser::TMDContentFlags::FLAG_HASHED_CONTENT))
				fstHashMode = ClusterHashMode::HASH_INTERLEAVED;
			cemu_assert_debug(itr.size <= 0xFFFFFFFF);
			fstSize = (uint32)itr.size;
		}
	}
	FSTVolume* fstVolume = nullptr;
	if (hasTicketKey)
		fstVolume = FSTVolume::OpenFST(dataSource.get(), 0, fstSize, &titleKey, fstHashMode, &tmdParser);
	uint8 firstBlock[16]{};
	bool haveFirstBlock = fstSize >= sizeof(FSTHeader) && dataSource->readData(0, 0, 0, firstBlock, sizeof(firstBlock)) == sizeof(firstBlock);
	for (sint32 i = 0; i < 0x7FFFFFFF && !fstVolume; i++)
	{
		uint8* key128 = KeyCache_GetAES128(i);
		if (!key128)
			break;
		if (haveFirstBlock)
		{
			uint8 header[16];
			uint8 zeroIv[16]{};
			uint8 cipher[16];
			std::memcpy(cipher, firstBlock, sizeof(cipher));
			AES128_CBC_decrypt(header, cipher, sizeof(header), key128, zeroIv);
			const uint32 magic = ((uint32)header[0] << 24) | ((uint32)header[1] << 16) | ((uint32)header[2] << 8) | (uint32)header[3];
			const uint32 numCluster = ((uint32)header[8] << 24) | ((uint32)header[9] << 16) | ((uint32)header[10] << 8) | (uint32)header[11];
			if (magic != 0x46535400 || numCluster >= 0x1000)
				continue;
		}
		NCrypto::AesKey candidate;
		std::memcpy(candidate.b, key128, 16);
		fstVolume = FSTVolume::OpenFST(dataSource.get(), 0, fstSize, &candidate, fstHashMode, &tmdParser);
	}
	if (!fstVolume)
	{
		if (errorCodeOut)
			*errorCodeOut = hasTicketKey ? ErrorCode::TITLE_KEY_INVALID : ticketError;
		return nullptr;
	}
	fstVolume->m_sourceIsOwned = true;
	dataSource.release();
	SET_FST_ERROR(OK);
	return fstVolume;
}

FSTVolume* FSTVolume::OpenFST(FSTDataSource* dataSource, uint64 fstOffset, uint32 fstSize, NCrypto::AesKey* partitionTitleKey, ClusterHashMode fstHashMode, NCrypto::TMDParser* optionalTMD)
{
	cemu_assert_debug(fstHashMode != ClusterHashMode::RAW || fstHashMode != ClusterHashMode::RAW_STREAM);
	if (fstSize < sizeof(FSTHeader))
		return nullptr;
	if (fstSize > 256u * 1024u * 1024u)
	{
		cemuLog_log(LogType::Force, "FST: size {} is not plausible", fstSize);
		return nullptr;
	}
	constexpr uint64 FST_CLUSTER_OFFSET = 0;
	uint32 fstSizePadded = (fstSize + 15) & ~15;
	std::vector<uint8> fstData(fstSizePadded);
	if (dataSource->readData(0, FST_CLUSTER_OFFSET, fstOffset, fstData.data(), fstSizePadded) != fstSizePadded)
		return nullptr;
	uint8 iv[16]{};
	AES128_CBC_decrypt(fstData.data(), fstData.data(), fstSizePadded, partitionTitleKey->b, iv);
	FSTHeader* fstHeader = (FSTHeader*)fstData.data();
	const void* fstEnd = fstData.data() + fstSize;
	if (fstHeader->magic != 0x46535400 || fstHeader->numCluster >= 0x1000)
	{
		if (!s_quietHeaderErrors)
			cemuLog_log(LogType::Force, "FST has invalid header");
		return nullptr;
	}
	uint32 numCluster = fstHeader->numCluster;
	FSTHeader_ClusterEntry* clusterDataTable = (FSTHeader_ClusterEntry*)(fstData.data() + sizeof(FSTHeader));
	if ((clusterDataTable + numCluster) > fstEnd)
		return nullptr;
	std::vector<FSTCluster> clusterTable;
	clusterTable.resize(numCluster);
	for (size_t i = 0; i < numCluster; i++)
	{
		clusterTable[i].offset = clusterDataTable[i].offset;
		clusterTable[i].size = clusterDataTable[i].size;
		clusterTable[i].hashMode = static_cast<FSTVolume::ClusterHashMode>((uint8)clusterDataTable[i].hashMode);
		clusterTable[i].hasContentHash = false;
	}
	if(optionalTMD)
	{
		if(numCluster != optionalTMD->GetContentList().size())
		{
			cemuLog_log(LogType::Force, "FST: Number of clusters does not match TMD content list");
			return nullptr;
		}
		auto& contentList = optionalTMD->GetContentList();
		for(size_t i=0; i<contentList.size(); i++)
		{
			auto& cluster = clusterTable[i];
			auto& content = contentList[i];
			cluster.hasContentHash = true;
			cluster.contentHashIsSHA1 = HAS_FLAG(contentList[i].contentFlags, NCrypto::TMDParser::TMDContentFlags::FLAG_SHA1);
			cluster.contentSize = content.size;
			static_assert(sizeof(content.hash32) == sizeof(cluster.contentHash32));
			memcpy(cluster.contentHash32, content.hash32, sizeof(cluster.contentHash32));
			if(cluster.hashMode == ClusterHashMode::RAW || cluster.hashMode == ClusterHashMode::RAW_STREAM)
			{
				cluster.singleHashCtx.reset(EVP_MD_CTX_new());
				EVP_DigestInit_ex(cluster.singleHashCtx.get(), cluster.contentHashIsSHA1 ? EVP_sha1() : EVP_sha256(), nullptr);
			}
		}
	}
	FSTHeader_FileEntry* fileTable = (FSTHeader_FileEntry*)(clusterDataTable + numCluster);
	if ((fileTable + 1) > fstEnd)
		return nullptr;
	if (fileTable[0].GetType() != FSTHeader_FileEntry::TYPE::DIRECTORY)
		return nullptr;
	uint32 numFileEntries = fileTable[0].size;
	if (numFileEntries == 0 || (fileTable + numFileEntries) > fstEnd)
		return nullptr;
	ptrdiff_t nameLookupTableSize = ((const uint8*)fstEnd - (const uint8*)(fileTable + numFileEntries));
	if (nameLookupTableSize < 1)
		return nullptr;
	std::vector<char> nameStringTable(nameLookupTableSize);
	std::memcpy(nameStringTable.data(), (fileTable + numFileEntries), nameLookupTableSize);
	std::vector<FSTEntry> fstEntries;
	if (!ProcessFST(fileTable, numFileEntries, numCluster, nameStringTable, fstEntries))
		return nullptr;
	FSTVolume* fstVolume = new FSTVolume();
	fstVolume->m_dataSource = dataSource;
	fstVolume->m_offsetFactor = fstHeader->offsetFactor;
	fstVolume->m_sectorSize = DISC_SECTOR_SIZE;
	fstVolume->m_partitionTitlekey = *partitionTitleKey;
	fstVolume->m_hashIsDisabled = fstHeader->hashIsDisabled != 0;
	fstVolume->m_cluster = std::move(clusterTable);
	fstVolume->m_entries = std::move(fstEntries);
	fstVolume->m_nameStringTable = std::move(nameStringTable);
	return fstVolume;
}

FSTVolume* FSTVolume::OpenFST(std::unique_ptr<FSTDataSource> dataSource, uint64 fstOffset, uint32 fstSize, NCrypto::AesKey* partitionTitleKey, ClusterHashMode fstHashMode, NCrypto::TMDParser* optionalTMD)
{
	FSTDataSource* ds = dataSource.release();
	FSTVolume* fstVolume = OpenFST(ds, fstOffset, fstSize, partitionTitleKey, fstHashMode, optionalTMD);
	if (!fstVolume)
	{
		delete ds;
		return nullptr;
	}
	fstVolume->m_sourceIsOwned = true;
	return fstVolume;
}

bool FSTVolume::ProcessFST(FSTHeader_FileEntry* fileTable, uint32 numFileEntries, uint32 numCluster, std::vector<char>& nameStringTable, std::vector<FSTEntry>& fstEntries)
{
	struct DirHierachyInfo
	{
		DirHierachyInfo(uint32 parentIndex, uint32 endIndex) : parentIndex(parentIndex), endIndex(endIndex) {};

		uint32 parentIndex;
		uint32 endIndex;
	};
	std::vector<DirHierachyInfo> currentDirEnd;
	currentDirEnd.reserve(32);
	currentDirEnd.emplace_back(0, numFileEntries);
	uint32 currentIndex = 0;
	FSTHeader_FileEntry* pFileIn = fileTable + currentIndex;
	fstEntries.resize(numFileEntries);
	FSTEntry* pFileOut = fstEntries.data();
	if (pFileIn->GetType() != FSTHeader_FileEntry::TYPE::DIRECTORY || pFileIn->GetDirectoryEndIndex() != numFileEntries || pFileIn->GetDirectoryParent() != 0)
	{
		cemuLog_log(LogType::Force, "FSTVolume::ProcessFST() - root node is invalid");
		return false;
	}
	for (; currentIndex < numFileEntries; currentIndex++)
	{
		while (currentIndex >= currentDirEnd.back().endIndex)
			currentDirEnd.pop_back();
		uint32 nameOffset = pFileIn->GetNameOffset();
		uint32 pos = nameOffset;
		while (true)
		{
			if (pos >= nameStringTable.size())
				return false;
			if (nameStringTable[pos] == '\0')
				break;
			pos++;
		}
		uint32 nameLen = pos - nameOffset;
		pFileOut->nameOffset = nameOffset;
		pFileOut->nameHash = _QuickNameHash(nameStringTable.data() + nameOffset, nameLen);
		pFileOut->parentDirIndex = currentDirEnd.back().parentIndex;
		auto entryType = pFileIn->GetType();

		uint8 flags = 0;
		if (pFileIn->HasFlagLink())
			flags |= FSTEntry::FLAG_LINK;
		if (pFileIn->HasUknFlag02())
			flags |= FSTEntry::FLAG_UKN02;
		pFileOut->SetFlags((FSTEntry::FLAGS)flags);

		if (entryType == FSTHeader_FileEntry::TYPE::FILE)
		{
			bool isSysLink = entryType == FSTHeader_FileEntry::TYPE::FILE;
			if (pFileIn->clusterIndex >= numCluster)
			{
				cemuLog_log(LogType::Force, "FST: File references cluster out of range");
				return false;
			}
			cemu_assert_debug(pFileIn->flagsOrPermissions != 0x4004);
			pFileOut->SetType(FSTEntry::TYPE::FILE);
			pFileOut->fileInfo.fileOffset = pFileIn->offset;
			pFileOut->fileInfo.fileSize = pFileIn->size;
			pFileOut->fileInfo.clusterIndex = pFileIn->clusterIndex;
		}
		else if (entryType == FSTHeader_FileEntry::TYPE::DIRECTORY)
		{
			cemu_assert_debug(pFileIn->flagsOrPermissions != 0x4004);
			pFileOut->SetType(FSTEntry::TYPE::DIRECTORY);
			uint32 endIndex = pFileIn->GetDirectoryEndIndex();
			uint32 parentIndex = pFileIn->GetDirectoryParent();
			if (endIndex < currentIndex || endIndex > currentDirEnd.back().endIndex)
			{
				cemuLog_log(LogType::Force, "FST: Directory range out of bounds");
				return false;
			}
			if (parentIndex != currentDirEnd.back().parentIndex)
			{
				cemuLog_log(LogType::Force, "FST: Parent index does not match");
				cemu_assert_debug(false);
				return false;
			}
			currentDirEnd.emplace_back(currentIndex, endIndex);
			pFileOut->dirInfo.endIndex = endIndex;
		}
		else
		{
			cemuLog_log(LogType::Force, "FST: Encountered node with unknown type");
			cemu_assert_debug(false);
			return false;
		}
		pFileIn++;
		pFileOut++;
	}
	cemu_assert_debug(currentIndex == numFileEntries);
	while (!currentDirEnd.empty() && currentIndex >= currentDirEnd.back().endIndex)
		currentDirEnd.pop_back();
	cemu_assert_debug(currentDirEnd.empty());
	return true;
}

uint32 FSTVolume::GetFileCount() const
{
	uint32 fileCount = 0;
	for (auto& itr : m_entries)
	{
		if (itr.GetType() == FSTEntry::TYPE::FILE)
			fileCount++;
	}
	return fileCount;
}

bool FSTVolume::OpenFile(std::string_view path, FSTFileHandle& fileHandleOut, bool openOnlyFiles)
{
	FSCPath fscPath(path);
	if (fscPath.GetNodeCount() == 0)
	{
		if(openOnlyFiles)
			return false;
		fileHandleOut.m_fstIndex = 0;
		return true;
	}

	auto findSubentry = [this](size_t firstIndex, size_t lastIndex, std::string_view nodeName) -> sint32
	{
		uint16 nodeHash = _QuickNameHash(nodeName.data(), nodeName.size());
		size_t index = firstIndex;
		while (index < lastIndex)
		{
			if (m_entries[index].nameHash == nodeHash && MatchFSTEntryName(m_entries[index], nodeName))
				return (sint32)index;
			if (m_entries[index].GetType() == FSTEntry::TYPE::DIRECTORY)
				index = m_entries[index].dirInfo.endIndex;
			else
				index++;
		}
		return -1;
	};

	uint32 parentIndex = std::numeric_limits<uint32>::max();
	size_t curDirStart = 1;
	size_t curDirEnd = m_entries[0].dirInfo.endIndex;

	for (size_t nodeIndex = 0; nodeIndex < fscPath.GetNodeCount() - 1; nodeIndex++)
	{
		sint32 fstIndex = findSubentry(curDirStart, curDirEnd, fscPath.GetNodeName(nodeIndex));
		if (fstIndex < 0)
			return false;
		if (m_entries[fstIndex].GetType() != FSTEntry::TYPE::DIRECTORY)
			return false;
		parentIndex = fstIndex;
		curDirStart = fstIndex + 1;
		curDirEnd = m_entries[fstIndex].dirInfo.endIndex;
	}
	sint32 fstIndex = findSubentry(curDirStart, curDirEnd, fscPath.GetNodeName(fscPath.GetNodeCount() - 1));
	if (fstIndex < 0)
		return false;
	if (openOnlyFiles && m_entries[fstIndex].GetType() != FSTEntry::TYPE::FILE)
		return false;
	fileHandleOut.m_fstIndex = fstIndex;
	return true;
}

bool FSTVolume::IsDirectory(const FSTFileHandle& fileHandle) const
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return false;
	return m_entries[fileHandle.m_fstIndex].GetType() == FSTEntry::TYPE::DIRECTORY;
};

bool FSTVolume::IsFile(const FSTFileHandle& fileHandle) const
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return false;
	return m_entries[fileHandle.m_fstIndex].GetType() == FSTEntry::TYPE::FILE;
};

bool FSTVolume::HasLinkFlag(const FSTFileHandle& fileHandle) const
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return false;
	return HAS_FLAG(m_entries[fileHandle.m_fstIndex].GetFlags(), FSTEntry::FLAGS::FLAG_LINK);
};

std::string_view FSTVolume::GetName(const FSTFileHandle& fileHandle) const
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return "";
	const char* entryName = m_nameStringTable.data() + m_entries[fileHandle.m_fstIndex].nameOffset;
	return entryName;
}

std::string FSTVolume::GetPath(const FSTFileHandle& fileHandle) const
{
	std::string path;
	auto& entry = m_entries[fileHandle.m_fstIndex];
	boost::container::small_vector<uint32, 8> parentChain;
	if (entry.HasNonRootNodeParent())
	{
		parentChain.emplace_back(entry.parentDirIndex);
		auto* parentItr = &m_entries[entry.parentDirIndex];
		while (parentItr->HasNonRootNodeParent())
		{
			cemu_assert_debug(parentItr->GetType() == FSTEntry::TYPE::DIRECTORY);
			parentChain.emplace_back(parentItr->parentDirIndex);
			parentItr = &m_entries[parentItr->parentDirIndex];
		}
	}
	cemu_assert_debug(parentChain.size() <= 1);
	for (auto& itr : parentChain | boost::adaptors::reversed)
	{
		const char* name = m_nameStringTable.data() + m_entries[itr].nameOffset;
		path.append(name);
		path.push_back('/');
	}
	const char* name = m_nameStringTable.data() + entry.nameOffset;
	path.append(name);
	return path;
}

uint32 FSTVolume::GetFileSize(const FSTFileHandle& fileHandle) const
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return 0;
	if (m_entries[fileHandle.m_fstIndex].GetType() != FSTEntry::TYPE::FILE)
		return 0;
	return m_entries[fileHandle.m_fstIndex].fileInfo.fileSize;
}

constexpr size_t BLOCK_SIZE = 0x10000;
constexpr size_t BLOCK_HASH_SIZE = 0x0400;
constexpr size_t BLOCK_FILE_SIZE = 0xFC00;

struct FSTRawBlock
{
	std::vector<uint8> rawData;
};

struct FSTHashedBlock
{
	uint8 rawData[BLOCK_SIZE];

	uint8* getHashData()
	{
		return rawData;
	}

	uint8* getH0Hash(uint32 index)
	{
		cemu_assert_debug(index < 16);
		return getHashData() + 20 * index;
	}

	uint8* getH1Hash(uint32 index)
	{
		cemu_assert_debug(index < 16);
		return getHashData() + (20 * 16) * 1 + 20 * index;
	}

	uint8* getH2Hash(uint32 index)
	{
		cemu_assert_debug(index < 16);
		return getHashData() + (20 * 16) * 2 + 20 * index;
	}

	uint8* getFileData()
	{
		return rawData + BLOCK_HASH_SIZE;
	}

	uint8* getH0Hash(size_t index)
	{
		cemu_assert_debug(index < 16);
		return rawData + index * 20;
	}
};

static_assert(sizeof(FSTHashedBlock) == BLOCK_SIZE);

struct FSTCachedRawBlock
{
	FSTRawBlock blockData;
	NCrypto::AesIv ivForNextBlock;
	uint64 lastAccess;
};

struct FSTCachedHashedBlock
{
	FSTHashedBlock blockData;
	uint64 lastAccess;
};

void FSTVolume::TrimCacheIfRequired(FSTCachedRawBlock** droppedRawBlock, FSTCachedHashedBlock** droppedHashedBlock)
{
	size_t cacheSize = 0;
	for (auto& itr : m_cacheDecryptedRawBlocks)
		cacheSize += itr.second->blockData.rawData.size();
	for (auto& itr : m_cacheDecryptedHashedBlocks)
		cacheSize += sizeof(FSTCachedHashedBlock) + sizeof(FSTHashedBlock);
	if (cacheSize < FSTReadCacheBudgetBytes())
		return;
	auto dropRawItr = std::min_element(m_cacheDecryptedRawBlocks.begin(), m_cacheDecryptedRawBlocks.end(), [](const auto& a, const auto& b) -> bool
										{ return a.second->lastAccess < b.second->lastAccess; });
	auto dropHashedItr = std::min_element(m_cacheDecryptedHashedBlocks.begin(), m_cacheDecryptedHashedBlocks.end(), [](const auto& a, const auto& b) -> bool
										{ return a.second->lastAccess < b.second->lastAccess; });
	uint64 lastAccess = std::numeric_limits<uint64>::max();
	if(dropRawItr != m_cacheDecryptedRawBlocks.end())
		lastAccess = dropRawItr->second->lastAccess;
	if(dropHashedItr != m_cacheDecryptedHashedBlocks.end())
		lastAccess = std::min<uint64>(lastAccess, dropHashedItr->second->lastAccess);
	if(dropRawItr != m_cacheDecryptedRawBlocks.end() && dropRawItr->second->lastAccess == lastAccess)
	{
		if (droppedRawBlock)
			*droppedRawBlock = dropRawItr->second;
		else
			delete dropRawItr->second;
		m_cacheDecryptedRawBlocks.erase(dropRawItr);
		return;
	}
	else if(dropHashedItr != m_cacheDecryptedHashedBlocks.end() && dropHashedItr->second->lastAccess == lastAccess)
	{
		if (droppedHashedBlock)
			*droppedHashedBlock = dropHashedItr->second;
		else
			delete dropHashedItr->second;
		m_cacheDecryptedHashedBlocks.erase(dropHashedItr);
	}
}

void FSTVolume::DetermineUnhashedBlockIV(uint32 clusterIndex, uint32 blockIndex, NCrypto::AesIv& ivOut)
{
	ivOut = {};
	if(blockIndex == 0)
	{
		ivOut.iv[0] = (uint8)(clusterIndex >> 8);
		ivOut.iv[1] = (uint8)(clusterIndex >> 0);
	}
	else
	{
		uint32 prevBlockIndex = blockIndex - 1;
		uint64 cacheBlockId = ((uint64)clusterIndex << (64 - 16)) | (uint64)prevBlockIndex;
		auto itr = m_cacheDecryptedRawBlocks.find(cacheBlockId);
		if (itr != m_cacheDecryptedRawBlocks.end())
		{
			ivOut = itr->second->ivForNextBlock;
		}
		else
		{
			cemu_assert(m_sectorSize >= NCrypto::AesIv::SIZE);
			uint64 clusterOffset = (uint64)m_cluster[clusterIndex].offset * m_sectorSize;
			NCrypto::AesIv prevIV{};
			if (m_dataSource->readData(clusterIndex, clusterOffset, blockIndex * m_sectorSize - NCrypto::AesIv::SIZE, prevIV.iv, NCrypto::AesIv::SIZE) != NCrypto::AesIv::SIZE)
			{
				cemuLog_log(LogType::Force, "Failed to read IV for raw FST block");
				m_detectedCorruption = true;
				return;
			}
			ivOut = prevIV;
		}
	}
}

FSTCachedRawBlock* FSTVolume::GetDecryptedRawBlock(uint32 clusterIndex, uint32 blockIndex)
{
	FSTCluster& cluster = m_cluster[clusterIndex];
	uint64 clusterOffset = (uint64)cluster.offset * m_sectorSize;
	uint64 cacheBlockId = ((uint64)clusterIndex << (64 - 16)) | (uint64)blockIndex;
	FSTCachedRawBlock* block = nullptr;
	auto itr = m_cacheDecryptedRawBlocks.find(cacheBlockId);
	if (itr != m_cacheDecryptedRawBlocks.end())
	{
		block = itr->second;
		block->lastAccess = ++m_cacheAccessCounter;
		return block;
	}
	TrimCacheIfRequired(&block, nullptr);
	if (!block)
		block = new FSTCachedRawBlock();
	block->blockData.rawData.resize(m_sectorSize);
	block->lastAccess = ++m_cacheAccessCounter;
	if (m_dataSource->readData(clusterIndex, clusterOffset, blockIndex * m_sectorSize, block->blockData.rawData.data(), m_sectorSize) != m_sectorSize)
	{
		cemuLog_log(LogType::Force, "Failed to read raw FST block");
		delete block;
		m_detectedCorruption = true;
		return nullptr;
	}
	NCrypto::AesIv iv{};
	DetermineUnhashedBlockIV(clusterIndex, blockIndex, iv);
	std::copy(block->blockData.rawData.data() + m_sectorSize - NCrypto::AesIv::SIZE, block->blockData.rawData.data() + m_sectorSize, block->ivForNextBlock.iv);
	AES128_CBC_decrypt(block->blockData.rawData.data(), block->blockData.rawData.data(), m_sectorSize, m_partitionTitlekey.b, iv.iv);
	if(cluster.hasContentHash)
	{
		if(cluster.singleHashNumBlocksHashed == blockIndex)
		{
			cemu_assert_debug(!(cluster.contentSize % m_sectorSize));
			bool isLastBlock = blockIndex == (std::max<uint32>(cluster.contentSize / m_sectorSize, 1) - 1);
			uint32 hashSize = m_sectorSize;
			if(isLastBlock)
				hashSize = cluster.contentSize - (uint64)blockIndex*m_sectorSize;
			EVP_DigestUpdate(cluster.singleHashCtx.get(), block->blockData.rawData.data(), hashSize);
			cluster.singleHashNumBlocksHashed++;
			if(isLastBlock)
			{
				uint8 hash[32];
				EVP_DigestFinal_ex(cluster.singleHashCtx.get(), hash, nullptr);
				if(memcmp(hash, cluster.contentHash32, cluster.contentHashIsSHA1 ? 20 : 32) != 0)
				{
					cemuLog_log(LogType::Force, "FST: Raw section hash mismatch");
					delete block;
					m_detectedCorruption = true;
					return nullptr;
				}
			}
		}
	}
	m_cacheDecryptedRawBlocks.emplace(cacheBlockId, block);
	return block;
}

FSTCachedHashedBlock* FSTVolume::GetDecryptedHashedBlock(uint32 clusterIndex, uint32 blockIndex)
{
	const FSTCluster& cluster = m_cluster[clusterIndex];
	uint64 clusterOffset = (uint64)cluster.offset * m_sectorSize;
	uint64 cacheBlockId = ((uint64)clusterIndex << (64 - 16)) | (uint64)blockIndex;
	FSTCachedHashedBlock* block = nullptr;
	auto itr = m_cacheDecryptedHashedBlocks.find(cacheBlockId);
	if (itr != m_cacheDecryptedHashedBlocks.end())
	{
		block = itr->second;
		block->lastAccess = ++m_cacheAccessCounter;
		return block;
	}
	TrimCacheIfRequired(nullptr, &block);
	if (!block)
		block = new FSTCachedHashedBlock();
	block->lastAccess = ++m_cacheAccessCounter;
	if (m_dataSource->readData(clusterIndex, clusterOffset, blockIndex * BLOCK_SIZE, block->blockData.rawData, BLOCK_SIZE) != BLOCK_SIZE)
	{
		cemuLog_log(LogType::Force, "Failed to read hashed FST block");
		delete block;
		m_detectedCorruption = true;
		return nullptr;
	}
	uint8 iv[16]{};
	AES128_CBC_decrypt(block->blockData.getHashData(), block->blockData.getHashData(), BLOCK_HASH_SIZE, m_partitionTitlekey.b, iv);
	AES128_CBC_decrypt(block->blockData.getFileData(), block->blockData.getFileData(), BLOCK_FILE_SIZE, m_partitionTitlekey.b, block->blockData.getH0Hash(blockIndex%16));
	NCrypto::CHash160 h0;
	SHA1(block->blockData.getFileData(), BLOCK_FILE_SIZE, h0.b);
	uint32 h0Index = (blockIndex % 4096);
	if (memcmp(h0.b, block->blockData.getH0Hash(h0Index & 0xF), sizeof(h0.b)) != 0)
	{
		cemuLog_log(LogType::Force, "FST: Hash H0 mismatch in hashed block (section {} index {})", clusterIndex, blockIndex);
		delete block;
		m_detectedCorruption = true;
		return nullptr;
	}
	m_cacheDecryptedHashedBlocks.emplace(cacheBlockId, block);
	return block;
}

uint32 FSTVolume::ReadFile_HashModeRaw(uint32 clusterIndex, FSTEntry& entry, uint32 readOffset, uint32 readSize, void* dataOut)
{
	uint8* dataOutU8 = (uint8*)dataOut;
	if (readOffset >= entry.fileInfo.fileSize)
		return 0;
	else if ((readOffset + readSize) >= entry.fileInfo.fileSize)
		readSize = (entry.fileInfo.fileSize - readOffset);
	uint64 absFileOffset = entry.fileInfo.fileOffset * m_offsetFactor + readOffset;
	uint32 remainingReadSize = readSize;
	while (remainingReadSize > 0)
	{
		const FSTCachedRawBlock* rawBlock = this->GetDecryptedRawBlock(clusterIndex, absFileOffset/m_sectorSize);
		if (!rawBlock)
			break;
		uint32 blockOffset = (uint32)(absFileOffset % m_sectorSize);
		uint32 bytesToRead = std::min<uint32>(remainingReadSize, m_sectorSize - blockOffset);
		std::memcpy(dataOutU8, rawBlock->blockData.rawData.data() + blockOffset, bytesToRead);
		dataOutU8 += bytesToRead;
		remainingReadSize -= bytesToRead;
		absFileOffset += bytesToRead;
	}
	return readSize - remainingReadSize;
}

uint32 FSTVolume::ReadFile_HashModeHashed(uint32 clusterIndex, FSTEntry& entry, uint32 readOffset, uint32 readSize, void* dataOut)
{

	const FSTCluster& cluster = m_cluster[clusterIndex];
	uint64 fileReadOffset = entry.fileInfo.fileOffset * m_offsetFactor + readOffset;
	uint32 blockIndex = (uint32)(fileReadOffset / BLOCK_FILE_SIZE);
	uint32 bytesRemaining = readSize;
	uint32 offsetWithinBlock = (uint32)(fileReadOffset % BLOCK_FILE_SIZE);
	while (bytesRemaining > 0)
	{
		FSTCachedHashedBlock* block = GetDecryptedHashedBlock(clusterIndex, blockIndex);
		if (!block)
			return 0;
		uint32 bytesToRead = std::min(bytesRemaining, (uint32)BLOCK_FILE_SIZE - offsetWithinBlock);
		std::memcpy(dataOut, block->blockData.getFileData() + offsetWithinBlock, bytesToRead);
		dataOut = (uint8*)dataOut + bytesToRead;
		bytesRemaining -= bytesToRead;
		blockIndex++;
		offsetWithinBlock = 0;
	}
	return readSize - bytesRemaining;
}

uint32 FSTVolume::ReadFile(FSTFileHandle& fileHandle, uint32 offset, uint32 size, void* dataOut)
{
	if (fileHandle.m_fstIndex >= m_entries.size())
		return 0;
	FSTEntry& entry = m_entries[fileHandle.m_fstIndex];
	if (entry.GetType() != FSTEntry::TYPE::FILE)
		return 0;
	cemu_assert_debug(!HAS_FLAG(entry.GetFlags(), FSTEntry::FLAGS::FLAG_LINK));
	FSTCluster& cluster = m_cluster[entry.fileInfo.clusterIndex];
	auto doRead = [&]() -> uint32
	{
		if (cluster.hashMode == ClusterHashMode::RAW || cluster.hashMode == ClusterHashMode::RAW_STREAM)
			return ReadFile_HashModeRaw(entry.fileInfo.clusterIndex, entry, offset, size, dataOut);
		else if (cluster.hashMode == ClusterHashMode::HASH_INTERLEAVED)
			return ReadFile_HashModeHashed(entry.fileInfo.clusterIndex, entry, offset, size, dataOut);
		cemu_assert_debug(false);
		return 0;
	};
	try
	{
		return doRead();
	}
	catch (const std::bad_alloc&)
	{
		cemuLog_log(LogType::Force, "FST: out of memory while reading a file; releasing the read cache and retrying");
		for (auto& itr : m_cacheDecryptedRawBlocks)
			delete itr.second;
		m_cacheDecryptedRawBlocks.clear();
		for (auto& itr : m_cacheDecryptedHashedBlocks)
			delete itr.second;
		m_cacheDecryptedHashedBlocks.clear();
	}
	try
	{
		return doRead();
	}
	catch (const std::bad_alloc&)
	{
		cemuLog_log(LogType::Force, "FST: still out of memory, the read fails");
		return 0;
	}
}

bool FSTVolume::OpenDirectoryIterator(std::string_view path, FSTDirectoryIterator& directoryIteratorOut)
{
	FSTFileHandle fileHandle;
	if (!OpenFile(path, fileHandle, false))
		return false;
	if (!IsDirectory(fileHandle))
		return false;
	auto const& fstEntry = m_entries[fileHandle.m_fstIndex];
	directoryIteratorOut.dirHandle = fileHandle;
	directoryIteratorOut.startIndex = fileHandle.m_fstIndex + 1;
	directoryIteratorOut.endIndex = fstEntry.dirInfo.endIndex;
	directoryIteratorOut.currentIndex = directoryIteratorOut.startIndex;
	return true;
}

bool FSTVolume::Next(FSTDirectoryIterator& directoryIterator, FSTFileHandle& fileHandleOut)
{
	if (directoryIterator.currentIndex >= directoryIterator.endIndex)
		return false;
	auto const& fstEntry = m_entries[directoryIterator.currentIndex];
	fileHandleOut.m_fstIndex = directoryIterator.currentIndex;
	if (fstEntry.GetType() == FSTEntry::TYPE::DIRECTORY)
	{
		cemu_assert_debug(fstEntry.dirInfo.endIndex > directoryIterator.currentIndex);
		directoryIterator.currentIndex = fstEntry.dirInfo.endIndex;
	}
	else
		directoryIterator.currentIndex++;
	return true;
}

FSTVolume::~FSTVolume()
{
	for (auto& itr : m_cacheDecryptedRawBlocks)
		delete itr.second;
	for (auto& itr : m_cacheDecryptedHashedBlocks)
		delete itr.second;
	if (m_sourceIsOwned)
		delete m_dataSource;
}

bool FSTVerifier::VerifyContentFile(FileStream* fileContent, const NCrypto::AesKey* key, uint32 contentIndex, uint32 contentSize, uint32 contentSizePadded, bool isSHA1, const uint8* tmdContentHash)
{
	cemu_assert_debug(isSHA1);
	cemu_assert_debug(((contentSize+0xF)&~0xF) == contentSizePadded);

	std::vector<uint8> buffer;
	buffer.resize(64 * 1024);
	if ((uint32)fileContent->GetSize() != contentSizePadded)
		return false;
	fileContent->SetPosition(0);
	uint8 iv[16]{};
	iv[0] = (contentIndex >> 8) & 0xFF;
	iv[1] = (contentIndex >> 0) & 0xFF;
	uint64 remainingBytes = contentSize;
	uint8 calculatedHash[SHA256_DIGEST_LENGTH];

	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	EVP_DigestInit(ctx, isSHA1 ? EVP_sha1() : EVP_sha256());

	while (remainingBytes > 0)
	{
		uint32 bytesToRead = (uint32)std::min(remainingBytes, (uint64)buffer.size());
		uint32 bytesToReadPadded = ((bytesToRead + 0xF) & ~0xF);
		uint32 bytesRead = fileContent->readData(buffer.data(), bytesToReadPadded);
		if (bytesRead != bytesToReadPadded)
			return false;
		AES128_CBC_decrypt_updateIV(buffer.data(), buffer.data(), bytesToReadPadded, key->b, iv);
		EVP_DigestUpdate(ctx, buffer.data(), bytesToRead);
		remainingBytes -= bytesToRead;
	}
	unsigned int md_len;
	EVP_DigestFinal_ex(ctx, calculatedHash, &md_len);
	EVP_MD_CTX_free(ctx);
	return memcmp(calculatedHash, tmdContentHash, md_len) == 0;
}

bool FSTVerifier::VerifyHashedContentFile(FileStream* fileContent, const NCrypto::AesKey* key, uint32 contentIndex, uint32 contentSize, uint32 contentSizePadded, bool isSHA1, const uint8* tmdContentHash)
{
	if (!isSHA1)
		return false;
	if ((contentSize % sizeof(FSTHashedBlock)) != 0)
		return false;
	if ((uint32)fileContent->GetSize() != contentSize)
		return false;
	fileContent->SetPosition(0);

	std::vector<NCrypto::CHash160> h0List(4096);

	FSTHashedBlock block;
	uint32 numBlocks = contentSize / sizeof(FSTHashedBlock);
	for (uint32 blockIndex = 0; blockIndex < numBlocks; blockIndex++)
	{
		if (fileContent->readData(&block, sizeof(FSTHashedBlock)) != sizeof(FSTHashedBlock))
			return false;
		uint32 h0Index = (blockIndex % 4096);
		uint8 iv[16]{};
		AES128_CBC_decrypt(block.getHashData(), block.getHashData(), BLOCK_HASH_SIZE, key->b, iv);
		AES128_CBC_decrypt(block.getFileData(), block.getFileData(), BLOCK_FILE_SIZE, key->b, block.getH0Hash(blockIndex % 16));

		NCrypto::CHash160 h0;
		SHA1(block.getFileData(), BLOCK_FILE_SIZE, h0.b);
		if (memcmp(h0.b, block.getH0Hash(h0Index & 0xF), sizeof(h0.b)) != 0)
			return false;
		std::memcpy(h0List[h0Index].b, h0.b, sizeof(h0.b));

		if (((h0Index + 1) % 16) == 0 && h0Index > 0)
		{
			uint32 h1Index = ((h0Index - 15) / 16);

			NCrypto::CHash160 h1;
			SHA1((unsigned char *) (h0List.data() + h1Index * 16), sizeof(NCrypto::CHash160) * 16, h1.b);
			if (memcmp(h1.b, block.getH1Hash(h1Index&0xF), sizeof(h1.b)) != 0)
				return false;
		}

	}
	return true;
}

void FSTVolumeTest()
{
	FSTPathUnitTest();
}
