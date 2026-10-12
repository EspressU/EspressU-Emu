#include "FileCache.h"
#include "util/helpers/helpers.h"

#include <mutex>
#include <set>
#include <algorithm>
#include <condition_variable>
#include <unordered_map>
#include "zlib.h"
#include "Common/FileStream.h"

static uint16 _fileCache_checksum(const uint8* data, size_t size)
{
	const uLong crc = crc32(0L, data, (uInt)size);
	return (uint16)((crc ^ (crc >> 16)) & 0xFFFF);
}

struct FileCacheAsyncJob
{
	FileCache* fileCache;
	uint64 name1;
	uint64 name2;
	std::vector<uint8> fileData;
};

struct _FileCacheAsyncWriter
{
	_FileCacheAsyncWriter()
	{
		m_isRunning.store(true);
		m_fileCacheThread = std::thread(&_FileCacheAsyncWriter::FileCacheThread, this);
	}

	~_FileCacheAsyncWriter()
	{
		if (m_isRunning.load())
		{
			m_isRunning.store(false);
			m_fileCacheCondVar.notify_one();
			m_fileCacheThread.join();
			m_jobDoneCondVar.notify_all();
		}
	}

	void AddJob(FileCache* fileCache, const FileCache::FileName& name, const uint8* fileData, sint32 fileSize)
	{
		FileCacheAsyncJob async;
		async.fileCache = fileCache;
		async.name1 = name.name1;
		async.name2 = name.name2;
		async.fileData = { fileData, fileData + fileSize };

		std::unique_lock lock(m_fileCacheMutex);
		m_writeRequests.emplace_back(std::move(async));
		m_pendingJobs[fileCache]++;

		lock.unlock();
		m_fileCacheCondVar.notify_one();
	}

	void WaitForCache(FileCache* fileCache)
	{
		std::unique_lock lock(m_fileCacheMutex);
		m_jobDoneCondVar.wait(lock, [&]() { return !m_pendingJobs.contains(fileCache) || !m_isRunning.load(); });
		m_pendingJobs.erase(fileCache);
	}

private:
	void FileCacheThread()
	{
		SetThreadName("fileCache");
		while (true)
		{
			std::unique_lock lock(m_fileCacheMutex);
			while (m_writeRequests.empty())
			{
				m_fileCacheCondVar.wait(lock);
				if (!m_isRunning.load(std::memory_order::relaxed))
					return;
			}

			std::vector<FileCacheAsyncJob> requestsCopy;
			requestsCopy.swap(m_writeRequests);
			lock.unlock();

			for (const auto& entry : requestsCopy)
			{
				entry.fileCache->AddFile({ entry.name1, entry.name2 }, entry.fileData.data(), (sint32)entry.fileData.size());
				std::unique_lock doneLock(m_fileCacheMutex);
				auto it = m_pendingJobs.find(entry.fileCache);
				if (it != m_pendingJobs.end() && --it->second == 0)
					m_pendingJobs.erase(it);
				doneLock.unlock();
				m_jobDoneCondVar.notify_all();
			}
		}
	}

	std::thread m_fileCacheThread;
	std::mutex m_fileCacheMutex;
	std::condition_variable m_fileCacheCondVar;
	std::condition_variable m_jobDoneCondVar;
	std::vector<FileCacheAsyncJob> m_writeRequests;
	std::unordered_map<FileCache*, uint32> m_pendingJobs;
	std::atomic_bool m_isRunning;
}FileCacheAsyncWriter;

#define FILECACHE_MAGIC_V1					0x8371b694
#define FILECACHE_MAGIC_V2					0x8371b695
#define FILECACHE_MAGIC_V3					0x8371b696
#define FILECACHE_HEADER_RESV				128
#define FILECACHE_FILETABLE_NAME1			0xEFEFEFEFEFEFEFEFULL
#define FILECACHE_FILETABLE_NAME2			0xFEFEFEFEFEFEFEFEULL
#define FILECACHE_FILETABLE_FREE_NAME		0ULL

FileCache* FileCache::Create(const fs::path& path, uint32 extraVersion)
{
	FileStream* fs = FileStream::createFile2(path);
	if (!fs)
	{
		cemuLog_log(LogType::Force, "Failed to create cache file \"{}\"", _pathToUtf8(path));
		return nullptr;
	}
	auto* fileCache = new FileCache();
	fileCache->fileStream = fs;
	fileCache->filePath = path;
	fileCache->dataOffset = FILECACHE_HEADER_RESV;
	fileCache->fileTableEntryCount = 32;
	fileCache->fileTableOffset = 0;
	fileCache->fileTableSize = sizeof(FileTableEntry) * fileCache->fileTableEntryCount;
	fileCache->fileTableEntries = (FileTableEntry*)malloc(fileCache->fileTableSize);
	fileCache->extraVersion = extraVersion;
	memset(fileCache->fileTableEntries, 0, fileCache->fileTableSize);
	fileCache->fileTableEntries[0].name1 = FILECACHE_FILETABLE_NAME1;
	fileCache->fileTableEntries[0].name2 = FILECACHE_FILETABLE_NAME2;
	fileCache->fileTableEntries[0].fileOffset = fileCache->fileTableOffset;
	fileCache->fileTableEntries[0].fileSize = fileCache->fileTableSize;

	fs->writeU32(FILECACHE_MAGIC_V3);
	fs->writeU32(fileCache->extraVersion);
	fs->writeU64(fileCache->dataOffset);
	fs->writeU64(fileCache->fileTableOffset);
	fs->writeU32(fileCache->fileTableSize);
	fs->SetPosition(fileCache->dataOffset+fileCache->fileTableOffset);
	fs->writeData(fileCache->fileTableEntries, fileCache->fileTableSize);
	return fileCache;
}

FileCache* FileCache::_OpenExisting(const fs::path& path, bool compareExtraVersion, uint32 extraVersion, bool readOnly)
{
	FileStream* fs = FileStream::openFile2(path, !readOnly);
	if (!fs)
		return nullptr;
	uint32 headerMagic = 0;
	fs->readU32(headerMagic);
	bool isV2 = false;
	if (headerMagic != FILECACHE_MAGIC_V1 && headerMagic != FILECACHE_MAGIC_V2 && headerMagic != FILECACHE_MAGIC_V3)
	{
		delete fs;
		return nullptr;
	}
	if (headerMagic == FILECACHE_MAGIC_V1)
	{
		delete fs;
		return nullptr;
	}
	if (headerMagic == FILECACHE_MAGIC_V2)
		isV2 = true;
	uint32 headerExtraVersion = 0xFFFFFFFF;
	fs->readU32(headerExtraVersion);
	if (compareExtraVersion && headerExtraVersion != extraVersion)
	{
		delete fs;
		return nullptr;
	}
	if (!compareExtraVersion)
	{
		extraVersion = headerExtraVersion;
	}

	uint64 headerDataOffset = 0;
	uint64 headerFileTableOffset = 0;
	uint32 headerFileTableSize = 0;
	fs->readU64(headerDataOffset);
	fs->readU64(headerFileTableOffset);
	if (!fs->readU32(headerFileTableSize))
	{
		cemuLog_log(LogType::Force, "\"{}\" is corrupted", _pathToUtf8(path));
		delete fs;
		return nullptr;
	}
	uint32 fileTableEntryCount = 0;
	bool invalidFileTableSize = false;
	fileTableEntryCount = headerFileTableSize / sizeof(FileTableEntry);
	invalidFileTableSize = (headerFileTableSize % sizeof(FileTableEntry)) != 0;
	if (invalidFileTableSize)
	{
		cemuLog_log(LogType::Force, "\"{}\" is corrupted", _pathToUtf8(path));
		delete fs;
		return nullptr;
	}
	auto* fileCache = new FileCache();
	fileCache->fileStream = fs;
	fileCache->filePath = path;
	fileCache->readOnly = readOnly;
	fileCache->extraVersion = extraVersion;
	fileCache->dataOffset = headerDataOffset;
	fileCache->fileTableEntryCount = fileTableEntryCount;
	fileCache->fileTableOffset = headerFileTableOffset;
	fileCache->fileTableSize = fileTableEntryCount * sizeof(FileTableEntry);
	fileCache->fileTableEntries = (FileTableEntry*)malloc(fileTableEntryCount * sizeof(FileTableEntry));
	memset(fileCache->fileTableEntries, 0, fileTableEntryCount * sizeof(FileTableEntry));
	fileCache->fileStream->SetPosition(fileCache->dataOffset + fileCache->fileTableOffset);
	bool incompleteFileTable = false;
	if (isV2)
	{
		incompleteFileTable = fileCache->fileStream->readData(fileCache->fileTableEntries, fileCache->fileTableSize) != fileCache->fileTableSize;
		for (uint32 i = 0; i < fileTableEntryCount; i++)
		{
			fileCache->fileTableEntries[i].flags = FileTableEntry::FLAGS::FLAG_NONE;
			fileCache->fileTableEntries[i].extraReserved1 = 0;
			fileCache->fileTableEntries[i].extraReserved2 = 0;
			fileCache->fileTableEntries[i].extraReserved3 = 0;
		}
	}
	else
	{
		incompleteFileTable = fileCache->fileStream->readData(fileCache->fileTableEntries, fileCache->fileTableSize) != fileCache->fileTableSize;
	}
	if (incompleteFileTable)
	{
		cemuLog_log(LogType::Force, "\"{}\" is corrupted (incomplete file table)", _pathToUtf8(path));
		delete fileCache;
		return nullptr;
	}
	return fileCache;
}

static std::mutex s_backupDirMutex;
static fs::path s_backupDir;
static std::atomic_uint32_t s_damagedEntryTotal{0};

uint32 FileCache::GetDamagedEntryTotal()
{
	return s_damagedEntryTotal.load();
}

void FileCache::SetBackupDirectory(const fs::path& dir)
{
	std::unique_lock lock(s_backupDirMutex);
	s_backupDir = dir;
}

bool FileCache::_handleDamagedEntry(FileTableEntry* entry, std::vector<uint8>& dataOut)
{
	damagedEntryCount++;
	if (readOnly)
	{
		dataOut.clear();
		return false;
	}
	const uint64 name1 = entry->name1;
	const uint64 name2 = entry->name2;
	fs::path backupPath;
	bool isBackupFile = false;
	{
		std::unique_lock lock(s_backupDirMutex);
		if (!s_backupDir.empty() && !filePath.empty())
			backupPath = s_backupDir / filePath.filename();
		isBackupFile = !s_backupDir.empty() && filePath.parent_path() == s_backupDir;
	}
	if (!isBackupFile)
		s_damagedEntryTotal++;
	std::error_code ec;
	if (!backupPath.empty() && backupPath != filePath && fs::exists(backupPath, ec))
	{
		if (FileCache* backup = _OpenExisting(backupPath, false))
		{
			std::vector<uint8> good;
			const bool found = backup->GetFile({ name1, name2 }, good);
			delete backup;
			if (found)
			{
				_addFileInternal(name1, name2, good.data(), (sint32)good.size(), false);
				dataOut = std::move(good);
				cemuLog_log(LogType::Force, "\"{}\": damaged entry {:016x}{:016x} restored from the backup", _pathToUtf8(filePath.filename()), name1, name2);
				return true;
			}
		}
	}
	DeleteFile({ name1, name2 });
	dataOut.clear();
	cemuLog_log(LogType::Force, "\"{}\": damaged entry {:016x}{:016x} deleted (no good copy in the backup)", _pathToUtf8(filePath.filename()), name1, name2);
	return false;
}

bool FileCache::Verify(const fs::path& path, bool checkEntries)
{
	std::error_code ec;
	if (!fs::exists(path, ec))
		return false;
	FileCache* fc = _OpenExisting(path, false);
	if (!fc)
		return false;
	bool ok = true;
	if (fc->fileTableEntryCount == 0 ||
		fc->fileTableEntries[0].name1 != FILECACHE_FILETABLE_NAME1 || fc->fileTableEntries[0].name2 != FILECACHE_FILETABLE_NAME2 ||
		fc->fileTableEntries[0].fileOffset != fc->fileTableOffset)
		ok = false;
	if (ok && checkEntries)
	{
		const uint64 fileSize = fc->fileStream->GetSize();
		std::vector<std::pair<uint64, uint64>> spans;
		for (uint32 i = 0; ok && i < fc->fileTableEntryCount; i++)
		{
			const FileTableEntry& e = fc->fileTableEntries[i];
			if (e.name1 == FILECACHE_FILETABLE_FREE_NAME && e.name2 == FILECACHE_FILETABLE_FREE_NAME)
				continue;
			const uint64 end = e.fileOffset + e.fileSize;
			if (end < e.fileOffset || fc->dataOffset + end > fileSize)
				ok = false;
			spans.emplace_back(e.fileOffset, end);
		}
		if (ok)
		{
			std::sort(spans.begin(), spans.end());
			for (size_t i = 1; ok && i < spans.size(); i++)
				if (spans[i].first < spans[i - 1].second)
					ok = false;
		}
		std::vector<uint8> data;
		for (uint32 i = 1; ok && i < fc->fileTableEntryCount; i++)
		{
			const FileTableEntry& e = fc->fileTableEntries[i];
			if (e.name1 == FILECACHE_FILETABLE_FREE_NAME && e.name2 == FILECACHE_FILETABLE_FREE_NAME)
				continue;
			if ((e.flags & FileTableEntry::FLAG_CHECKSUM) != 0 && !fc->_readEntryRaw(&e, data))
				ok = false;
		}
	}
	delete fc;
	return ok;
}

FileCache* FileCache::Open(const fs::path& path, bool allowCreate, uint32 extraVersion, bool replaceOnVersionChange)
{
	FileCache* fileCache = _OpenExisting(path, true, extraVersion);
	if (fileCache)
		return fileCache;
	if (!allowCreate)
		return nullptr;
	std::error_code ec;
	bool unreadable = fs::exists(path, ec) && fs::file_size(path, ec) > 0 && !ec;
	if (unreadable && replaceOnVersionChange)
	{
		if (FileCache* readable = _OpenExisting(path, false))
		{
			delete readable;
			unreadable = false;
		}
	}
	if (unreadable)
	{
		fs::path aside = path;
		aside += ".unreadable";
		fs::remove(aside, ec);
		fs::rename(path, aside, ec);
		if (ec)
			cemuLog_log(LogType::Force, "Cache file \"{}\" could not be opened or set aside: {}", _pathToUtf8(path), ec.message());
		else
			cemuLog_log(LogType::Force, "Cache file \"{}\" could not be read; kept as \"{}\" and started a new one", _pathToUtf8(path), _pathToUtf8(aside));
	}
	return Create(path, extraVersion);
}

FileCache* FileCache::Open(const fs::path& path)
{
	return _OpenExisting(path, false, 0);
}

FileCache* FileCache::OpenReadOnly(const fs::path& path)
{
	return _OpenExisting(path, false, 0, true);
}

FileCache::~FileCache()
{
	FileCacheAsyncWriter.WaitForCache(this);
	free(this->fileTableEntries);
	delete fileStream;
}

bool FileCache::_flushStream()
{
#ifdef __APPLE__
	if (deferFlush)
		return true;
	return fileStream->Flush();
#else
	return true;
#endif
}

bool FileCache::fileCache_updateFiletable(sint32 extraEntriesToAllocate)
{
	const FileTableEntry tableEntryBefore = this->fileTableEntries[0];
	const sint32 entryCountBefore = this->fileTableEntryCount;
	this->fileTableEntries[0].name1 = FILECACHE_FILETABLE_FREE_NAME;
	this->fileTableEntries[0].name2 = FILECACHE_FILETABLE_FREE_NAME;
	sint32 newFileTableEntryCount = this->fileTableEntryCount + extraEntriesToAllocate;
	this->fileTableEntries = (FileTableEntry*)realloc(this->fileTableEntries, sizeof(FileTableEntry)*newFileTableEntryCount);
	for (sint32 f = this->fileTableEntryCount; f < newFileTableEntryCount; f++)
	{
		this->fileTableEntries[f].name1 = FILECACHE_FILETABLE_FREE_NAME;
		this->fileTableEntries[f].name2 = FILECACHE_FILETABLE_FREE_NAME;
		this->fileTableEntries[f].fileOffset = 0;
		this->fileTableEntries[f].fileSize = 0;
		this->fileTableEntries[f].flags = FileTableEntry::FLAGS::FLAG_NONE;
		this->fileTableEntries[f].extraReserved1 = 0;
		this->fileTableEntries[f].extraReserved2 = 0;
		this->fileTableEntries[f].extraReserved3 = 0;
	}
	this->fileTableEntryCount = newFileTableEntryCount;
	if (!this->_addFileInternal(FILECACHE_FILETABLE_NAME1, FILECACHE_FILETABLE_NAME2, (uint8*)this->fileTableEntries, sizeof(FileTableEntry)*newFileTableEntryCount, true))
	{
		this->fileTableEntryCount = entryCountBefore;
		this->fileTableEntries[0] = tableEntryBefore;
		return false;
	}
	if (this->fileTableEntries[0].name1 != FILECACHE_FILETABLE_NAME1 || this->fileTableEntries[0].name2 != FILECACHE_FILETABLE_NAME2)
	{
		cemuLog_log(LogType::Force, "Corruption in cache file detected");
		assert_dbg();
	}
	this->fileTableOffset = this->fileTableEntries[0].fileOffset;
	this->fileTableSize = this->fileTableEntries[0].fileSize;
	fileStream->SetPosition(0);
	fileStream->writeU32(FILECACHE_MAGIC_V3);
	fileStream->writeU32(this->extraVersion);
	fileStream->writeU64(this->dataOffset);
	fileStream->writeU64(this->fileTableOffset);
	fileStream->writeU32(this->fileTableSize);
	_flushStream();
	return true;
}

uint8* _fileCache_compressFileData(const uint8* fileData, uint32 fileSize, sint32& compressedSize)
{
	Bytef* uncompressedInput = (Bytef*)fileData;
	uLongf uncompressedLen = fileSize;
	uLongf compressedLen = compressBound(fileSize);
	Bytef* compressedData = (Bytef*)malloc(4 + compressedLen);
	int zret = compress2(compressedData + 4, &compressedLen, uncompressedInput, uncompressedLen, 4);
	if (zret != Z_OK)
	{
		free(compressedData);
		return nullptr;
	}
	compressedData[0] = ((uint32)fileSize >> 24) & 0xFF;
	compressedData[1] = ((uint32)fileSize >> 16) & 0xFF;
	compressedData[2] = ((uint32)fileSize >> 8) & 0xFF;
	compressedData[3] = ((uint32)fileSize >> 0) & 0xFF;
	compressedSize = 4 + compressedLen;
	return compressedData;
}

bool _uncompressFileData(const uint8* rawData, size_t rawSize, std::vector<uint8>& dataOut)
{
	if (rawSize < 4)
	{
		dataOut.clear();
		return false;
	}
	uint32 fileSize = 0;
	fileSize |= ((uint32)rawData[0] << 24);
	fileSize |= ((uint32)rawData[1] << 16);
	fileSize |= ((uint32)rawData[2] << 8);
	fileSize |= ((uint32)rawData[3] << 0);
	Bytef* compressedInput = (Bytef*)rawData + 4;
	uLongf compressedLen = (uLongf)(rawSize - 4);
	uLongf uncompressedLen = fileSize;
	dataOut.resize(fileSize);
	int zret = uncompress2(dataOut.data(), &uncompressedLen, compressedInput, &compressedLen);
	if (zret != Z_OK)
	{
		dataOut.clear();
		return false;
	}
	if (uncompressedLen != fileSize || compressedLen != (rawSize - 4))
	{
		dataOut.clear();
		return false;
	}
	return true;
}

bool FileCache::_addFileInternal(uint64 name1, uint64 name2, const uint8* fileData, sint32 fileSize, bool noCompression)
{
	if (fileSize < 0 || readOnly)
		return false;
	if (!enableCompression)
		noCompression = true;
	sint32 rawSize = 0;
	uint8* rawData = nullptr;
	bool isCompressed = false;
	if (noCompression)
	{
		rawData = (uint8*)fileData;
		rawSize = fileSize;
	}
	else
	{
		rawData = _fileCache_compressFileData(fileData, fileSize, rawSize);
		if (rawData)
		{
			isCompressed = true;
		}
		else
		{
			rawData = (uint8*)fileData;
			rawSize = fileSize;
		}
	}
	std::unique_lock lock(this->mutex);
	sint32 entryIndex = -1;
	for (sint32 i = 0; i < this->fileTableEntryCount; i++)
	{
		if (this->fileTableEntries[i].name1 == name1 && this->fileTableEntries[i].name2 == name2)
		{
			entryIndex = i;
			break;
		}
	}
	if (entryIndex == -1)
	{
		while (true)
		{
			for (sint32 i = 0; i < this->fileTableEntryCount; i++)
			{
				if (this->fileTableEntries[i].name1 == FILECACHE_FILETABLE_FREE_NAME && this->fileTableEntries[i].name2 == FILECACHE_FILETABLE_FREE_NAME)
				{
					entryIndex = i;
					break;
				}
			}
			if (entryIndex == -1)
			{
				if (name1 == FILECACHE_FILETABLE_NAME1 && name2 == FILECACHE_FILETABLE_NAME2)
				{
					cemuLog_log(LogType::Force, "Error in cache file");
					cemu_assert_debug(false);
				}
				if (!fileCache_updateFiletable(64))
				{
					if (isCompressed)
						free(rawData);
					return false;
				}
				continue;
			}
			else
				break;
		}
	}
	sint64 currentStartOffset = 0;
	while (true)
	{
		bool hasCollision = false;
		sint64 currentEndOffset = currentStartOffset + rawSize;
		FileTableEntry* entry = this->fileTableEntries;
		FileTableEntry* entryLast = this->fileTableEntries + this->fileTableEntryCount;
		while (entry < entryLast)
		{
			if (entry->name1 == FILECACHE_FILETABLE_FREE_NAME && entry->name2 == FILECACHE_FILETABLE_FREE_NAME)
			{
				entry++;
				continue;
			}
			if (currentEndOffset >= (sint64)entry->fileOffset && currentStartOffset < (sint64)(entry->fileOffset + entry->fileSize))
			{
				currentStartOffset = entry->fileOffset + entry->fileSize;
				hasCollision = true;
				break;
			}
			entry++;
		}
		if (hasCollision && (entry + 1) < entryLast)
		{
			entry++;
			while (entry < entryLast)
			{
				if (entry->name1 == FILECACHE_FILETABLE_FREE_NAME && entry->name2 == FILECACHE_FILETABLE_FREE_NAME)
				{
					entry++;
					continue;
				}
				if (entry->fileOffset == currentStartOffset)
				{
					currentStartOffset = entry->fileOffset + entry->fileSize;
					entry++;
					continue;
				}
				break;
			}
		}
		if (hasCollision == false)
			break;
	}
	const FileTableEntry entryBefore = this->fileTableEntries[entryIndex];
	this->fileTableEntries[entryIndex].name1 = name1;
	this->fileTableEntries[entryIndex].name2 = name2;
	this->fileTableEntries[entryIndex].fileOffset = currentStartOffset;
	this->fileTableEntries[entryIndex].fileSize = rawSize;
	this->fileTableEntries[entryIndex].flags = isCompressed ? FileTableEntry::FLAGS::FLAG_COMPRESSED : FileTableEntry::FLAGS::FLAG_NONE;
	this->fileTableEntries[entryIndex].extraReserved1 = 0;
	this->fileTableEntries[entryIndex].extraReserved2 = 0;
	this->fileTableEntries[entryIndex].extraReserved3 = 0;
	if (!(name1 == FILECACHE_FILETABLE_NAME1 && name2 == FILECACHE_FILETABLE_NAME2))
	{
		const uint16 checksum = _fileCache_checksum(rawData, (size_t)rawSize);
		this->fileTableEntries[entryIndex].flags = (FileTableEntry::FLAGS)(this->fileTableEntries[entryIndex].flags | FileTableEntry::FLAGS::FLAG_CHECKSUM);
		this->fileTableEntries[entryIndex].extraReserved1 = (uint8)(checksum & 0xFF);
		this->fileTableEntries[entryIndex].extraReserved2 = (uint8)(checksum >> 8);
	}
	fileStream->SetPosition(this->dataOffset + currentStartOffset);
	bool writeOk = fileStream->writeData(rawData, rawSize) == rawSize;
	writeOk = _flushStream() && writeOk;
	if (writeOk)
	{
		fileStream->SetPosition(this->dataOffset + this->fileTableOffset + (uint64)(sizeof(FileTableEntry)*entryIndex));
		writeOk = fileStream->writeData(this->fileTableEntries + entryIndex, sizeof(FileTableEntry)) == sizeof(FileTableEntry);
		writeOk = _flushStream() && writeOk;
	}
	if (isCompressed)
		free(rawData);
	if (!writeOk)
	{
		this->fileTableEntries[entryIndex] = entryBefore;
		if (!(name1 == FILECACHE_FILETABLE_NAME1 && name2 == FILECACHE_FILETABLE_NAME2))
		{
			fileStream->SetPosition(this->dataOffset + this->fileTableOffset + (uint64)(sizeof(FileTableEntry)*entryIndex));
			fileStream->writeData(this->fileTableEntries + entryIndex, sizeof(FileTableEntry));
		}
		cemuLog_log(LogType::Force, "\"{}\": could not write a cache entry (storage full?)", _pathToUtf8(filePath.filename()));
		return false;
	}
	return true;
}

bool FileCache::AddFile(const FileName&& name, const uint8* fileData, sint32 fileSize)
{
	return this->_addFileInternal(name.name1, name.name2, fileData, fileSize, false);
}

bool FileCache::DeleteFile(const FileName&& name)
{
	if( name.name1 == FILECACHE_FILETABLE_NAME1 && name.name2 == FILECACHE_FILETABLE_NAME2 )
		return false;
	if (readOnly)
		return false;
	std::unique_lock lock(this->mutex);
	FileTableEntry* entry = this->fileTableEntries;
	FileTableEntry* entryLast = this->fileTableEntries+this->fileTableEntryCount;
	while( entry < entryLast )
	{
		if( entry->name1 == name.name1 && entry->name2 == name.name2 )
		{
			entry->name1 = FILECACHE_FILETABLE_FREE_NAME;
			entry->name2 = FILECACHE_FILETABLE_FREE_NAME;
			entry->fileOffset = 0;
			entry->fileSize = 0;
			size_t entryIndex = entry - this->fileTableEntries;
			fileStream->SetPosition(this->dataOffset+this->fileTableOffset+(uint64)(sizeof(FileTableEntry)*entryIndex));
			fileStream->writeData(this->fileTableEntries+entryIndex, sizeof(FileTableEntry));
			_flushStream();
			return true;
		}
		entry++;
	}
	return false;
}

uint32 FileCache::RekeyEntries(const std::vector<RekeyJob>& jobs)
{
	if (readOnly || jobs.empty())
		return 0;
	std::unique_lock lock(this->mutex);
	std::set<std::pair<uint64, uint64>> oldNames;
	for (const RekeyJob& job : jobs)
		oldNames.insert({ job.from.name1, job.from.name2 });
	std::vector<const RekeyJob*> written;
	deferFlush = true;
	for (const RekeyJob& job : jobs)
	{
		if ((job.from.name1 == job.to.name1 && job.from.name2 == job.to.name2) || oldNames.contains({ job.to.name1, job.to.name2 }))
			continue;
		if (HasFile({ job.to.name1, job.to.name2 }))
		{
			written.push_back(&job);
			continue;
		}
		std::vector<uint8> data;
		if (!GetFile({ job.from.name1, job.from.name2 }, data))
			continue;
		if (job.patch)
			job.patch(data);
		if (!AddFile({ job.to.name1, job.to.name2 }, data.data(), (sint32)data.size()))
			continue;
		std::vector<uint8> readBack;
		if (!GetFile({ job.to.name1, job.to.name2 }, readBack) || readBack != data)
			continue;
		written.push_back(&job);
	}
	deferFlush = false;
	if (!_flushStream())
		return 0;
	deferFlush = true;
	for (const RekeyJob* job : written)
		DeleteFile({ job->from.name1, job->from.name2 });
	deferFlush = false;
	_flushStream();
	return (uint32)written.size();
}

void FileCache::AddFileAsync(const FileName& name, const uint8* fileData, sint32 fileSize)
{
	FileCacheAsyncWriter.AddJob(this, name, fileData, fileSize);
}

bool FileCache::_readEntryRaw(const FileTableEntry* entry, std::vector<uint8>& rawOut)
{
	const uint64 start = this->dataOffset + entry->fileOffset;
	const uint64 end = start + entry->fileSize;
	if (start < this->dataOffset || end < start || end > fileStream->GetSize())
		return false;
	rawOut.resize(entry->fileSize);
	fileStream->SetPosition(start);
	if (fileStream->readData(rawOut.data(), entry->fileSize) != entry->fileSize)
		return false;
	if ((entry->flags & FileTableEntry::FLAG_CHECKSUM) != 0)
	{
		const uint16 stored = (uint16)(entry->extraReserved1 | ((uint16)entry->extraReserved2 << 8));
		if (_fileCache_checksum(rawOut.data(), rawOut.size()) != stored)
			return false;
	}
	return true;
}

bool FileCache::_getFileDataInternal(const FileTableEntry* entry, std::vector<uint8>& dataOut)
{
	std::vector<uint8> rawData;
	if (!_readEntryRaw(entry, rawData))
		return false;

	if ((entry->flags&FileTableEntry::FLAG_COMPRESSED) == 0)
	{
		std::swap(rawData, dataOut);
		return true;
	}
	sint32 uncompressedSize = 0;
	if (!_uncompressFileData(rawData.data(), rawData.size(), dataOut))
	{
		dataOut.clear();
		return false;
	}
	return true;
}

bool FileCache::GetFile(const FileName&& name, std::vector<uint8>& dataOut)
{
	std::unique_lock lock(this->mutex);
	FileTableEntry* entry = this->fileTableEntries;
	FileTableEntry* entryLast = this->fileTableEntries+this->fileTableEntryCount;
	while( entry < entryLast )
	{
		if( entry->name1 == name.name1 && entry->name2 == name.name2 )
		{
			if (_getFileDataInternal(entry, dataOut))
				return true;
			return _handleDamagedEntry(entry, dataOut);
		}
		entry++;
	}
	dataOut.clear();
	return false;
}

bool FileCache::GetFileByIndex(sint32 index, uint64* name1, uint64* name2, std::vector<uint8>& dataOut)
{
	std::unique_lock lock(this->mutex);
	if (index < 0 || index >= this->fileTableEntryCount)
		return false;
	FileTableEntry* entry = this->fileTableEntries + index;
	if (this->fileTableEntries == nullptr)
	{
		cemuLog_log(LogType::Force, "GetFileByIndex() fileTable is NULL");
		return false;
	}
	if (entry->name1 == FILECACHE_FILETABLE_FREE_NAME && entry->name2 == FILECACHE_FILETABLE_FREE_NAME)
		return false;
	if (entry->name1 == FILECACHE_FILETABLE_NAME1 && entry->name2 == FILECACHE_FILETABLE_NAME2)
		return false;

	if(name1)
		*name1 = entry->name1;
	if(name2)
		*name2 = entry->name2;
	if (_getFileDataInternal(entry, dataOut))
		return true;
	return _handleDamagedEntry(entry, dataOut);
}

bool FileCache::HasFile(const FileName&& name)
{
	std::unique_lock lock(this->mutex);
	FileTableEntry* entry = this->fileTableEntries;
	FileTableEntry* entryLast = this->fileTableEntries + this->fileTableEntryCount;
	while (entry < entryLast)
	{
		if (entry->name1 == name.name1 && entry->name2 == name.name2)
			return true;
		entry++;
	}
	return false;
}

sint32 FileCache::GetMaximumFileIndex()
{
	return this->fileTableEntryCount;
}

sint32 FileCache::GetFileCount()
{
	std::unique_lock lock(this->mutex);
	sint32 fileCount = 0;
	FileTableEntry* entry = this->fileTableEntries;
	FileTableEntry* entryLast = this->fileTableEntries+this->fileTableEntryCount;
	while( entry < entryLast )
	{
		if( entry->name1 == FILECACHE_FILETABLE_FREE_NAME && entry->name2 == FILECACHE_FILETABLE_FREE_NAME )
		{
			entry++;
			continue;
		}
		if( entry->name1 == FILECACHE_FILETABLE_NAME1 && entry->name2 == FILECACHE_FILETABLE_NAME2 )
		{
			entry++;
			continue;
		}
		fileCount++;
		entry++;
	}

	return fileCount;
}

void fileCache_test()
{
	FileCache* fc = FileCache::Create("testCache.bin", 0);
	uint32 time1 = GetTickCount();

	char* testString1 = (char*)malloc(1024 * 1024 * 8);
	char* testString2 = (char*)malloc(1024 * 1024 * 8);
	char* testString3 = (char*)malloc(1024 * 1024 * 8);
	for (sint32 f = 0; f < 1024 * 1024 * 8; f++)
	{
		testString1[f] = 'a' + (f & 7);
		testString2[f] = 'd' + (f & 3);
		testString3[f] = 'f' + (f & 3);
	}

	for(sint32 i=0; i<2200; i++)
	{
		fc->AddFile({ 0x1000001ULL, (uint64)i }, (uint8*)testString1, 1024 * 1024 * 1);
		fc->AddFile({ 0x1000002ULL, (uint64)i }, (uint8*)testString2, 1024 * 1024 * 1);
		fc->AddFile({ 0x1000003ULL, (uint64)i }, (uint8*)testString3, 1024 * 1024 * 1);
	}
	uint32 time2 = GetTickCount();
	debug_printf("Writing took %dms\n", time2-time1);
	delete fc;
	FileCache* fcRead = FileCache::Open("testCache.bin", 0);
	uint32 time3 = GetTickCount();
	for(sint32 i=0; i<2200; i++)
	{
		std::vector<uint8> fileData;
		bool r = fcRead->GetFile({ 0x1000001ULL, (uint64)i }, fileData);
		if (!r || fileData.size() != 1024 * 1024 * 1 || memcmp(fileData.data(), testString1, 1024 * 1024 * 1) != 0)
			cemu_assert_debug(false);
		r = fcRead->GetFile({ 0x1000002ULL, (uint64)i }, fileData);
		if( !r || fileData.size() != 1024 * 1024 * 1 || memcmp(fileData.data(), testString2, 1024 * 1024 * 1) != 0 )
			cemu_assert_debug(false);
		r = fcRead->GetFile({ 0x1000003ULL, (uint64)i }, fileData);
		if( !r || fileData.size() != 1024 * 1024 * 1 || memcmp(fileData.data(), testString3, 1024 * 1024 * 1) != 0 )
			cemu_assert_debug(false);
	}
	uint32 time4 = GetTickCount();
	debug_printf("Reading took %dms\n", time4-time3);
	delete fcRead;
	cemu_assert_debug(false);
	exit(0);
}
