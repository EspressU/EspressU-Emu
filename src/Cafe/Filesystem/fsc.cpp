#include "Cafe/Filesystem/fsc.h"
#include "Cafe/Filesystem/FST/fstUtil.h"

struct FSCMountPathNode
{
	std::string path;
	std::vector<FSCMountPathNode*> subnodes;
	FSCMountPathNode* parent;
	fscDeviceC* device{ nullptr };
	void* ctx{ nullptr };
	std::string deviceTargetPath;
	sint32 priority{};

	FSCMountPathNode(FSCMountPathNode* parent) : parent(parent)
	{
	}

    void AssignDevice(fscDeviceC* device, void* ctx, std::string_view deviceBasePath)
    {
        this->device = device;
        this->ctx = ctx;
        this->deviceTargetPath = deviceBasePath;
    }

    void UnassignDevice()
    {
        this->device = nullptr;
        this->ctx = nullptr;
        this->deviceTargetPath.clear();
    }

    bool IsRootNode() const
    {
        return !parent;
    }

	~FSCMountPathNode()
	{
		for (auto& itr : subnodes)
			delete itr;
		subnodes.clear();
	}
};

bool FSA_CompareNodeName(std::string_view a, std::string_view b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++)
	{
		uint8 ac = (uint8)a[i];
		uint8 bc = (uint8)b[i];
		if (ac >= (uint8)'A' && ac <= (uint8)'Z')
			ac -= ((uint8)'A' - (uint8)'a');
		if (bc >= (uint8)'A' && bc <= (uint8)'Z')
			bc -= ((uint8)'A' - (uint8)'a');
		if (ac != bc)
			return false;
	}
	return true;
}

FSCMountPathNode* s_fscRootNodePerPrio[FSC_PRIORITY_COUNT]{};

std::recursive_mutex s_fscMutex;

#define fscEnter() s_fscMutex.lock();
#define fscLeave() s_fscMutex.unlock();

FSCMountPathNode* fsc_lookupPathVirtualNode(const char* path, sint32 priority = FSC_PRIORITY_BASE);

void fsc_reset()
{
	for (auto& itr : s_fscRootNodePerPrio)
	{
		delete itr;
		itr = nullptr;
	}
	for (sint32 i = 0; i < FSC_PRIORITY_COUNT; i++)
		s_fscRootNodePerPrio[i] = new FSCMountPathNode(nullptr);
}

static void fsc_ensureRootNodes()
{
	fscEnter();
	for (sint32 i = 0; i < FSC_PRIORITY_COUNT; i++)
	{
		if (!s_fscRootNodePerPrio[i])
			s_fscRootNodePerPrio[i] = new FSCMountPathNode(nullptr);
	}
	fscLeave();
}

FSCMountPathNode* fsc_createMountPath(const FSCPath& mountPath, sint32 priority)
{
	cemu_assert(priority >= 0 && priority < FSC_PRIORITY_COUNT);
	fsc_ensureRootNodes();
	fscEnter();
	FSCMountPathNode* nodeParent = s_fscRootNodePerPrio[priority];
	for (size_t i=0; i< mountPath.GetNodeCount(); i++)
	{
		FSCMountPathNode* nodeSub = nullptr;
		for (auto& nodeItr : nodeParent->subnodes)
		{
			if (mountPath.MatchNodeName(i, nodeItr->path))
			{
				nodeSub = nodeItr;
				break;
			}
		}
		if (nodeSub)
		{
			nodeParent = nodeSub;
			continue;
		}
		nodeSub = new FSCMountPathNode(nodeParent);
		nodeSub->path = mountPath.GetNodeName(i);
		nodeSub->priority = priority;
		nodeParent->subnodes.emplace_back(nodeSub);
		if (i == (mountPath.GetNodeCount() - 1))
		{
			fscLeave();
			return nodeSub;
		}
		nodeParent = nodeSub;
	}
	fscLeave();
	if (mountPath.GetNodeCount() == 0)
		return nodeParent;
	return nullptr;
}

sint32 fsc_mount(std::string_view mountPath, std::string_view targetPath, fscDeviceC* fscDevice, void* ctx, sint32 priority)
{
	cemu_assert(fscDevice);
	std::string mountPathTmp(mountPath);
	std::string targetPathWithSlash(targetPath);
	if (!targetPathWithSlash.empty() && (targetPathWithSlash.back() != '/' && targetPathWithSlash.back() != '\\'))
		targetPathWithSlash.push_back('/');

	FSCPath parsedMountPath(mountPathTmp);
	fscEnter();
	FSCMountPathNode* node = fsc_createMountPath(parsedMountPath, priority);
	if( !node )
	{
		cemuLog_log(LogType::Force, "fsc_mount failed (virtual path: {})", mountPath);
		fscLeave();
		return FSC_STATUS_INVALID_PATH;
	}
    node->AssignDevice(fscDevice, ctx, targetPathWithSlash);
	fscLeave();
	return FSC_STATUS_OK;
}

bool fsc_unmount(std::string_view mountPath, sint32 priority)
{
	std::string _tmp(mountPath);
	fscEnter();
	FSCMountPathNode* mountPathNode = fsc_lookupPathVirtualNode(_tmp.c_str(), priority);
	if (!mountPathNode)
	{
		fscLeave();
		return false;
	}
	cemu_assert(mountPathNode->priority == priority);
	cemu_assert(mountPathNode->device);
    mountPathNode->UnassignDevice();
	while (mountPathNode && !mountPathNode->IsRootNode() && mountPathNode->subnodes.empty() && !mountPathNode->device)
	{
		FSCMountPathNode* parent = mountPathNode->parent;
        std::erase(parent->subnodes, mountPathNode);
		delete mountPathNode;
		mountPathNode = parent;
	}
	fscLeave();
	return true;
}

static size_t fsc_countDeviceNodes(const FSCMountPathNode* node)
{
	size_t count = node->device ? 1 : 0;
	for (const FSCMountPathNode* sub : node->subnodes)
		count += fsc_countDeviceNodes(sub);
	return count;
}

size_t fsc_getMountCount(sint32 priority)
{
	if (priority < 0 || priority >= FSC_PRIORITY_COUNT)
		return 0;
	fscEnter();
	size_t count = s_fscRootNodePerPrio[priority] ? fsc_countDeviceNodes(s_fscRootNodePerPrio[priority]) : 0;
	fscLeave();
	return count;
}

void fsc_unmountAll()
{
	fscEnter();
	fsc_reset();
	fscLeave();
}

bool fsc_lookupPath(const char* path, std::string& devicePathOut, fscDeviceC** fscDeviceOut, void** ctxOut, sint32 priority = FSC_PRIORITY_BASE)
{
	FSCPath parsedPath(path);
	fsc_ensureRootNodes();
	FSCMountPathNode* nodeParent = s_fscRootNodePerPrio[priority];
	size_t i;
	fscEnter();
	for (i = 0; i < parsedPath.GetNodeCount(); i++)
	{
		FSCMountPathNode* nodeSub = nullptr;
		for(auto& nodeItr : nodeParent->subnodes)
		{
			if (parsedPath.MatchNodeName(i, nodeItr->path))
			{
				nodeSub = nodeItr;
				break;
			}
		}
		if (nodeSub)
		{
			nodeParent = nodeSub;
			continue;
		}
		break;
	}
	while (nodeParent)
	{
		if (nodeParent->device)
		{
			devicePathOut = nodeParent->deviceTargetPath;
			for (size_t f = i; f < parsedPath.GetNodeCount(); f++)
			{
				auto nodeName = parsedPath.GetNodeName(f);
				devicePathOut.append(nodeName);
				if (f < (parsedPath.GetNodeCount() - 1))
					devicePathOut.push_back('/');
			}
			*fscDeviceOut = nodeParent->device;
			*ctxOut = nodeParent->ctx;
			fscLeave();
			return true;
		}
		nodeParent = nodeParent->parent;
		i--;
	}
	fscLeave();
	return false;
}

FSCMountPathNode* fsc_lookupPathVirtualNode(const char* path, sint32 priority)
{
	FSCPath parsedPath(path);
	fsc_ensureRootNodes();
	FSCMountPathNode* nodeCurrentDir = s_fscRootNodePerPrio[priority];
	fscEnter();
	for (size_t i = 0; i < parsedPath.GetNodeCount(); i++)
	{
		FSCMountPathNode* nodeSub = nullptr;
		for (auto& nodeItr : nodeCurrentDir->subnodes)
		{
			if (parsedPath.MatchNodeName(i, nodeItr->path))
			{
				nodeSub = nodeItr;
				break;
			}
		}
		if (nodeSub)
		{
			nodeCurrentDir = nodeSub;
			continue;
		}
		fscLeave();
		return nullptr;
	}
	fscLeave();
	return nodeCurrentDir;
}

class FSCVirtualFileDirectoryIterator : public FSCVirtualFile
{
public:
	sint32 fscGetType() override
	{
		return FSC_TYPE_DIRECTORY;
	}

	FSCVirtualFileDirectoryIterator(std::string_view path, std::span<FSCVirtualFile*> mappedFolders)
		: m_path(path), m_folders(mappedFolders.begin(), mappedFolders.end())
	{
		dirIterator = nullptr;
	}

	~FSCVirtualFileDirectoryIterator()
	{
		for (auto& itr : m_folders)
			delete itr;
	}

	bool fscDirNext(FSCDirEntry* dirEntry) override
	{
		if (!dirIterator)
		{
			PopulateIterationList();
			cemu_assert_debug(dirIterator);
		}
		if (dirIterator->index >= dirIterator->dirEntries.size())
			return false;
		*dirEntry = dirIterator->dirEntries[dirIterator->index];
		dirIterator->index++;
		return true;
	}

	bool fscRewindDir() override
	{
		if (!dirIterator)
			return true;

		dirIterator->index  = 0;
		return true;
	}

	void addUniqueDirEntry(const FSCDirEntry& dirEntry)
	{
		for (auto& itr : dirIterator->dirEntries)
		{
			if (FSA_CompareNodeName(dirEntry.path, itr.path))
				return;
		}
		dirIterator->dirEntries.emplace_back(dirEntry);
	}

private:
	void PopulateIterationList()
	{
		cemu_assert_debug(!dirIterator);
		dirIterator = new FSCVirtualFile::FSCDirIteratorState();
		FSCDirEntry dirEntry;
		fscEnter();
		for (auto& itr : m_folders)
		{
			while (itr->fscDirNext(&dirEntry))
				addUniqueDirEntry(dirEntry);
		}
		for (sint32 prio = FSC_PRIORITY_COUNT - 1; prio >= 0; prio--)
		{
			FSCMountPathNode* nodeVirtualPath = fsc_lookupPathVirtualNode(m_path.c_str(), prio);
			if (nodeVirtualPath)
			{
				for (auto& itr : nodeVirtualPath->subnodes)
				{
					dirEntry = {};
					dirEntry.isDirectory = true;
					strncpy(dirEntry.path, itr->path.c_str(), sizeof(dirEntry.path) - 1);
					dirEntry.path[sizeof(dirEntry.path) - 1] = '\0';
					dirEntry.fileSize = 0;
					addUniqueDirEntry(dirEntry);
				}
			}
		}
		fscLeave();
	}

private:
	std::string m_path;
	std::vector<FSCVirtualFile*> m_folders;
};

FSCVirtualFile* fsc_open(const char* path, FSC_ACCESS_FLAG accessFlags, sint32* fscStatus, sint32 maxPriority)
{
	cemu_assert_debug(HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::OPEN_FILE) || HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::OPEN_DIR));
	FSCVirtualFile* dirList[FSC_PRIORITY_COUNT];
	uint8 dirListCount = 0;
	std::string devicePath;
	fscDeviceC* fscDevice = NULL;
	*fscStatus = FSC_STATUS_UNDEFINED;
	void* ctx;
	fscEnter();
	for (sint32 prio = maxPriority; prio >= 0; prio--)
	{
		if (fsc_lookupPath(path, devicePath, &fscDevice, &ctx, prio))
		{
			FSCVirtualFile* fscVirtualFile = fscDevice->fscDeviceOpenByPath(devicePath, accessFlags, ctx, fscStatus);
			if (fscVirtualFile)
			{
				if (fscVirtualFile->fscGetType() == FSC_TYPE_DIRECTORY)
				{
					cemu_assert_debug(HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::OPEN_DIR));
					dirList[dirListCount] = fscVirtualFile;
					dirListCount++;
				}
				else
				{
					cemu_assert_debug(HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::OPEN_FILE));
					fscVirtualFile->m_isAppend = HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::IS_APPEND);
					fscLeave();
					return fscVirtualFile;
				}
			}
		}
	}
	if (HAS_FLAG(accessFlags, FSC_ACCESS_FLAG::OPEN_DIR))
	{
		bool folderExists = dirListCount > 0;
		for (sint32 prio = FSC_PRIORITY_COUNT - 1; prio >= 0; prio--)
		{
			if (folderExists)
				break;
			folderExists |= (fsc_lookupPathVirtualNode(path, prio) != 0);
		}
		if (folderExists)
		{
			FSCVirtualFileDirectoryIterator* dirIteratorFile = new FSCVirtualFileDirectoryIterator(path, { dirList, dirListCount});
			*fscStatus = FSC_STATUS_OK;
			fscLeave();
			return dirIteratorFile;
		}
	}
	else
	{
		cemu_assert_debug(dirListCount == 0);
	}
	fscLeave();
	*fscStatus = FSC_STATUS_FILE_NOT_FOUND;
	return nullptr;
}

FSCVirtualFile* fsc_openDirIterator(const char* path, sint32* fscStatus)
{
	return fsc_open(path, FSC_ACCESS_FLAG::OPEN_DIR, fscStatus);
}

bool fsc_nextDir(FSCVirtualFile* fscFile, FSCDirEntry* dirEntry)
{
	fscEnter();
	if (fscFile->fscGetType() != FSC_TYPE_DIRECTORY)
	{
		cemu_assert_suspicious();
		fscLeave();
		return false;
	}
	bool r = fscFile->fscDirNext(dirEntry);
	fscLeave();
	return r;
}

bool fsc_createDir(const char* path, sint32* fscStatus)
{
	fscDeviceC* fscDevice = NULL;
	*fscStatus = FSC_STATUS_UNDEFINED;
	void* ctx;
	std::string devicePath;
	fscEnter();
	if( fsc_lookupPath(path, devicePath, &fscDevice, &ctx) )
	{
		sint32 status = fscDevice->fscDeviceCreateDir(devicePath, ctx, fscStatus);
		fscLeave();
		return status;
	}
	fscLeave();
	return false;
}

bool fsc_rename(const char* srcPath, const char* dstPath, sint32* fscStatus)
{
	std::string srcDevicePath;
	std::string dstDevicePath;
	void* srcCtx;
	void* dstCtx;
	fscDeviceC* fscSrcDevice = NULL;
	fscDeviceC* fscDstDevice = NULL;
	*fscStatus = FSC_STATUS_UNDEFINED;
	if( fsc_lookupPath(srcPath, srcDevicePath, &fscSrcDevice, &srcCtx) && fsc_lookupPath(dstPath, dstDevicePath, &fscDstDevice, &dstCtx) )
	{
		if( fscSrcDevice == fscDstDevice )
			return fscSrcDevice->fscDeviceRename(srcDevicePath, dstDevicePath, srcCtx, fscStatus);
	}
	return false;
}

bool fsc_remove(const char* path, sint32* fscStatus)
{
	std::string devicePath;
	fscDeviceC* fscDevice = NULL;
	*fscStatus = FSC_STATUS_UNDEFINED;
	void* ctx;
	if( fsc_lookupPath(path, devicePath, &fscDevice, &ctx) )
	{
		return fscDevice->fscDeviceRemoveFileOrDir(devicePath, ctx, fscStatus);
	}
	return false;
}

void fsc_close(FSCVirtualFile* fscFile)
{
	fscEnter();
	delete fscFile;
	fscLeave();
}

uint32 fsc_getFileSize(FSCVirtualFile* fscFile)
{
	return (uint32)fscFile->fscQueryValueU64(FSC_QUERY_SIZE);
}

uint32 fsc_getFileSeek(FSCVirtualFile* fscFile)
{
	return (uint32)fscFile->fscGetSeek();
}

void fsc_setFileSeek(FSCVirtualFile* fscFile, uint32 newSeek)
{
	fscEnter();
	uint32 fileSize = fsc_getFileSize(fscFile);
	if (fsc_isWritable(fscFile) == false)
		newSeek = std::min(newSeek, fileSize);
	fscFile->fscSetSeek((uint64)newSeek);
	fscLeave();
}

void fsc_setFileLength(FSCVirtualFile* fscFile, uint32 newEndOffset)
{
	fscEnter();
	uint32 fileSize = fsc_getFileSize(fscFile);
	if (!fsc_isWritable(fscFile))
	{
		cemuLog_log(LogType::Force, "TruncateFile called on read-only file");
	}
	else
	{
		fscFile->fscSetFileLength((uint64)newEndOffset);
	}
	fscLeave();
}

bool fsc_isDirectory(FSCVirtualFile* fscFile)
{
	return fscFile->fscGetType() == FSC_TYPE_DIRECTORY;
}

bool fsc_isFile(FSCVirtualFile* fscFile)
{
	return fscFile->fscGetType() == FSC_TYPE_FILE;
}

bool fsc_isWritable(FSCVirtualFile* fscFile)
{
	return fscFile->fscQueryValueU64(FSC_QUERY_WRITEABLE) != 0;
}

uint32 fsc_readFile(FSCVirtualFile* fscFile, void* buffer, uint32 size)
{
	fscEnter();
	uint32 fscStatus = fscFile->fscReadData(buffer, size);
	fscLeave();
	return fscStatus;
}

uint32 fsc_writeFile(FSCVirtualFile* fscFile, void* buffer, uint32 size)
{
	fscEnter();
	if (fsc_isWritable(fscFile) == false)
	{
		fscLeave();
		return 0;
	}
	if (fscFile->m_isAppend)
		fsc_setFileSeek(fscFile, fsc_getFileSize(fscFile));

	uint32 fscStatus = fscFile->fscWriteData(buffer, size);
	fscLeave();
	return fscStatus;
}

uint8* fsc_extractFile(const char* path, uint32* fileSize, sint32 maxPriority)
{
	fscDeviceC* fscDevice = nullptr;
	sint32 fscStatus = FSC_STATUS_UNDEFINED;
	fscEnter();
	FSCVirtualFile* fscFile = fsc_open(path, FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &fscStatus, maxPriority);
	if( !fscFile )
	{
		*fileSize = 0;
		fscLeave();
		return nullptr;
	}
	uint32 fscFileSize = fsc_getFileSize(fscFile);
	*fileSize = fscFileSize;
	uint8* fileMem = (uint8*)malloc(fscFileSize);
	if( fsc_readFile(fscFile, fileMem, fscFileSize) != fscFileSize )
	{
		free(fileMem);
		fsc_close(fscFile);
		*fileSize = 0;
		fscLeave();
		return nullptr;
	}
	fsc_close(fscFile);
	fscLeave();
	return fileMem;
}

std::optional<std::vector<uint8>> fsc_extractFile(const char* path, sint32 maxPriority)
{
	fscDeviceC* fscDevice = nullptr;
	sint32 fscStatus = FSC_STATUS_UNDEFINED;
	fscEnter();
	FSCVirtualFile* fscFile = fsc_open(path, FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &fscStatus, maxPriority);
	if (!fscFile)
	{
		fscLeave();
		return std::nullopt;
	}
	std::vector<uint8> fileData;
	uint32 fscFileSize = fsc_getFileSize(fscFile);
	fileData.resize(fscFileSize);

	uint32 readOffset = 0;
	while (readOffset < fscFileSize)
	{
		uint32 stepReadSize = std::min(fscFileSize - readOffset, (uint32)1024 * 1024 * 32);
		uint32 numBytesRead = fsc_readFile(fscFile, fileData.data() + readOffset, stepReadSize);
		if (numBytesRead != stepReadSize)
		{
			fsc_close(fscFile);
			fscLeave();
			return std::nullopt;
		}
		readOffset += stepReadSize;
	}
	fsc_close(fscFile);
	fscLeave();
	return fileData;
}

bool fsc_doesFileExist(const char* path, sint32 maxPriority)
{
	fscDeviceC* fscDevice = nullptr;
	sint32 fscStatus = FSC_STATUS_UNDEFINED;
	fscEnter();
	FSCVirtualFile* fscFile = fsc_open(path, FSC_ACCESS_FLAG::OPEN_FILE, &fscStatus, maxPriority);
	if (!fscFile)
	{
		fscLeave();
		return false;
	}
	fsc_close(fscFile);
	fscLeave();
	return true;
}

bool fsc_doesDirectoryExist(const char* path, sint32 maxPriority)
{
	fscDeviceC* fscDevice = nullptr;
	sint32 fscStatus = FSC_STATUS_UNDEFINED;
	fscEnter();
	FSCVirtualFile* fscFile = fsc_open(path, FSC_ACCESS_FLAG::OPEN_DIR, &fscStatus, maxPriority);
	if (!fscFile)
	{
		fscLeave();
		return false;
	}
	fsc_close(fscFile);
	fscLeave();
	return true;
}

void fsc_init()
{
	fsc_reset();
}
