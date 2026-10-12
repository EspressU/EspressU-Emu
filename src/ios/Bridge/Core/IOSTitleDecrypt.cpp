#include "Cafe/Filesystem/FST/FST.h"
#include "Cafe/Filesystem/fsc.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cemu/Logging/CemuLogging.h"

#include <zarchive/zarchivereader.h>
#include <zarchive/zarchivewriter.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

enum
{
	IOS_DECRYPT_OK = 0,
	IOS_DECRYPT_UNABLE_TO_MOUNT = 1,
	IOS_DECRYPT_NO_DISC_KEY = 2,
	IOS_DECRYPT_DEST_NOT_WRITABLE = 3,
	IOS_DECRYPT_CANCELLED = 4,
	IOS_DECRYPT_INCOMPLETE = 5,
	IOS_DECRYPT_NO_TITLE_TIK = 6,
	IOS_DECRYPT_TITLE_KEY_INVALID = 7,
	IOS_DECRYPT_MISSING_CONTENT = 8,
};

constexpr uint32 kDecryptChunkSize = 4 * 1024 * 1024;

static bool IsSafeEntryName(const std::string& name)
{
	if (name.empty() || name == "." || name == "..")
		return false;
	for (char c : name)
	{
		if (c == '/' || c == '\\' || c == '\0')
			return false;
	}
	return !fs::path(name).is_absolute();
}

static bool IsInsideRoot(const fs::path& root, const fs::path& child)
{
	const fs::path r = root.lexically_normal();
	const fs::path c = child.lexically_normal();
	auto ri = r.begin();
	auto ci = c.begin();
	for (; ri != r.end(); ++ri, ++ci)
	{
		if (ri->empty())
			continue;
		if (ci == c.end() || *ri != *ci)
			return false;
	}
	return true;
}

static bool DecryptWalkDirectory(FSTVolume* volume, const std::string& fstPath, const fs::path& destPath,
	const fs::path& destRoot, uint32& failures,
	uint64& bytesWritten, uint32& filesWritten, std::atomic_bool& cancelRequested,
	const std::function<void(uint64 bytesWritten, uint32 filesWritten)>& progressCallback,
	std::vector<uint8>& scratchBuffer)
{
	FSTDirectoryIterator dirIterator;
	if (!volume->OpenDirectoryIterator(fstPath, dirIterator))
	{
		cemuLog_log(LogType::Force, "Decrypt: could not open directory '{}'", fstPath);
		failures++;
		return true;
	}

	FSTFileHandle entry;
	while (volume->Next(dirIterator, entry))
	{
		if (cancelRequested.load())
			return false;

		std::string name(volume->GetName(entry));
		if (!IsSafeEntryName(name))
		{
			cemuLog_log(LogType::Force, "Decrypt: refusing unsafe entry name in '{}'", fstPath);
			failures++;
			continue;
		}
		std::string childFstPath = fstPath.empty() ? name : (fstPath + "/" + name);
		fs::path childDestPath = destPath / name;
		if (!IsInsideRoot(destRoot, childDestPath))
		{
			cemuLog_log(LogType::Force, "Decrypt: refusing entry that would leave the destination: '{}'", childFstPath);
			failures++;
			continue;
		}

		if (volume->IsDirectory(entry))
		{
			std::error_code ec;
			fs::create_directories(childDestPath, ec);
			if (ec)
			{
				cemuLog_log(LogType::Force, "Decrypt: could not create directory '{}' ({})", childDestPath.string(), ec.message());
				failures++;
				continue;
			}
			if (!DecryptWalkDirectory(volume, childFstPath, childDestPath, destRoot, failures, bytesWritten, filesWritten, cancelRequested, progressCallback, scratchBuffer))
				return false;
			continue;
		}

		if (!volume->IsFile(entry))
			continue;

		FSTFileHandle fileHandle;
		if (!volume->OpenFile(childFstPath, fileHandle, true))
		{
			cemuLog_log(LogType::Force, "Decrypt: could not open '{}' for reading", childFstPath);
			failures++;
			continue;
		}
		uint32 fileSize = volume->GetFileSize(fileHandle);

		std::ofstream out(childDestPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
		{
			cemuLog_log(LogType::Force, "Decrypt: could not create output file '{}'", childDestPath.string());
			failures++;
			continue;
		}
		bool fileOk = true;

		if (scratchBuffer.size() < std::min(fileSize, kDecryptChunkSize))
			scratchBuffer.resize(std::min(fileSize, kDecryptChunkSize));

		uint32 offset = 0;
		while (offset < fileSize)
		{
			if (cancelRequested.load())
				return false;
			uint32 toRead = std::min((uint32)scratchBuffer.size(), fileSize - offset);
			uint32 got = volume->ReadFile(fileHandle, offset, toRead, scratchBuffer.data());
			if (got == 0)
			{
				cemuLog_log(LogType::Force, "Decrypt: read failed at offset {} of '{}'", offset, childFstPath);
				fileOk = false;
				break;
			}
			out.write((const char*)scratchBuffer.data(), got);
			if (!out)
			{
				cemuLog_log(LogType::Force, "Decrypt: write failed for '{}' (disk full?)", childDestPath.string());
				fileOk = false;
				break;
			}
			offset += got;
			bytesWritten += got;
		}
		out.close();
		if (out.fail())
			fileOk = false;
		if (!fileOk)
		{
			failures++;
			continue;
		}
		filesWritten++;
		if (progressCallback)
			progressCallback(bytesWritten, filesWritten);
	}
	return true;
}

int IOSTitleDecrypt_ExtractToFolder(const char* srcPath, const char* destFolderPath,
	std::atomic_bool& cancelRequested,
	const std::function<void(uint64 bytesWritten, uint32 filesWritten)>& progressCallback)
{
	if (!srcPath || srcPath[0] == '\0' || !destFolderPath || destFolderPath[0] == '\0')
		return IOS_DECRYPT_UNABLE_TO_MOUNT;

	fs::path dest(destFolderPath);
	std::error_code ec;
	fs::create_directories(dest, ec);
	if (ec)
	{
		cemuLog_log(LogType::Force, "Decrypt: destination '{}' is not writable ({})", dest.string(), ec.message());
		return IOS_DECRYPT_DEST_NOT_WRITABLE;
	}

	const fs::path srcFsPath(srcPath);
	std::error_code srcEc;
	const bool srcIsFolder = fs::is_directory(srcFsPath, srcEc);
	const bool srcIsNus = srcIsFolder || boost::iequals(_pathToUtf8(srcFsPath.filename()), "title.tmd");

	FSTVolume::ErrorCode fstError = FSTVolume::ErrorCode::UNKNOWN_ERROR;
	FSTVolume* volume = srcIsNus
		? FSTVolume::OpenFromContentFolder(srcIsFolder ? srcFsPath : srcFsPath.parent_path(), &fstError)
		: FSTVolume::OpenFromDiscImage(srcFsPath, &fstError);
	if (!volume)
	{
		cemuLog_log(LogType::Force, "Decrypt: could not open '{}' ({})", srcPath, (sint32)fstError);
		switch (fstError)
		{
		case FSTVolume::ErrorCode::DISC_KEY_MISSING:
			return IOS_DECRYPT_NO_DISC_KEY;
		case FSTVolume::ErrorCode::TITLE_TIK_MISSING:
		case FSTVolume::ErrorCode::BAD_TITLE_TMD:
		case FSTVolume::ErrorCode::BAD_TITLE_TIK:
			return IOS_DECRYPT_NO_TITLE_TIK;
		case FSTVolume::ErrorCode::TITLE_KEY_INVALID:
			return IOS_DECRYPT_TITLE_KEY_INVALID;
		case FSTVolume::ErrorCode::CONTENT_FILE_MISSING:
			return IOS_DECRYPT_MISSING_CONTENT;
		default:
			return IOS_DECRYPT_UNABLE_TO_MOUNT;
		}
	}

	uint64 bytesWritten = 0;
	uint32 filesWritten = 0;
	uint32 failures = 0;
	std::vector<uint8> scratchBuffer;
	bool completed = DecryptWalkDirectory(volume, "", dest, dest, failures, bytesWritten, filesWritten, cancelRequested, progressCallback, scratchBuffer);
	delete volume;

	cemuLog_log(LogType::Force, "Decrypt: {} - {} files, {} bytes written to '{}', {} failures",
		completed ? "finished" : "cancelled", filesWritten, bytesWritten, dest.string(), failures);

	if (!completed)
		return IOS_DECRYPT_CANCELLED;
	return failures == 0 ? IOS_DECRYPT_OK : IOS_DECRYPT_INCOMPLETE;
}

namespace
{
struct WuaWriterContext
{
	int fd;
	bool writeFailed = false;

	static void NewOutputFile(const int32_t  , void*  )
	{
	}

	static void WriteOutputData(const void* data, size_t length, void* ctx)
	{
		WuaWriterContext* self = (WuaWriterContext*)ctx;
		size_t written = 0;
		const uint8* p = (const uint8*)data;
		while (written < length)
		{
			ssize_t n = write(self->fd, p + written, length - written);
			if (n <= 0)
			{
				self->writeFailed = true;
				break;
			}
			written += (size_t)n;
		}
	}
};

bool WuaWalkDirectory(ZArchiveWriter& writer, const std::string& archivePath, const std::string& fscPath,
	uint32& failures, std::atomic_bool& cancelRequested, uint64& bytesWritten, uint32& filesWritten,
	const std::function<void(uint64 bytesWritten, uint32 filesWritten)>& progressCallback,
	std::vector<uint8>& scratchBuffer)
{
	sint32 fscStatus;
	std::unique_ptr<FSCVirtualFile, void(*)(FSCVirtualFile*)> dirIterator(
		fsc_openDirIterator(fscPath.c_str(), &fscStatus), fsc_close);
	if (!dirIterator)
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: could not open directory '{}'", fscPath);
		failures++;
		return true;
	}

	writer.MakeDir(archivePath.c_str(), false);

	FSCDirEntry dirEntry;
	while (fsc_nextDir(dirIterator.get(), &dirEntry))
	{
		if (cancelRequested.load())
			return false;

		std::string name(dirEntry.GetPath());
		if (!IsSafeEntryName(name))
		{
			cemuLog_log(LogType::Force, "Decrypt-to-WUA: refusing unsafe entry name in '{}'", fscPath);
			failures++;
			continue;
		}

		if (dirEntry.isDirectory)
		{
			if (!WuaWalkDirectory(writer, archivePath + name + "/", fscPath + name + "/",
					failures, cancelRequested, bytesWritten, filesWritten, progressCallback, scratchBuffer))
				return false;
			continue;
		}

		if (!dirEntry.isFile)
			continue;

		sint32 openStatus;
		std::unique_ptr<FSCVirtualFile, void(*)(FSCVirtualFile*)> file(
			fsc_open((fscPath + name).c_str(), FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &openStatus),
			fsc_close);
		if (!file)
		{
			cemuLog_log(LogType::Force, "Decrypt-to-WUA: could not open '{}'", fscPath + name);
			failures++;
			continue;
		}

		writer.StartNewFile((archivePath + name).c_str());
		if (scratchBuffer.size() < kDecryptChunkSize)
			scratchBuffer.resize(kDecryptChunkSize);

		uint32 got;
		while ((got = file->fscReadData(scratchBuffer.data(), (uint32)scratchBuffer.size())) != 0)
		{
			if (cancelRequested.load())
				return false;
			writer.AppendData(scratchBuffer.data(), got);
			bytesWritten += got;
		}
		filesWritten++;
		if (progressCallback)
			progressCallback(bytesWritten, filesWritten);
	}
	return true;
}
}

int IOSTitleDecrypt_ExtractToWua(const char* srcPath, const char* destWuaPath,
	std::atomic_bool& cancelRequested,
	const std::function<void(uint64 bytesWritten, uint32 filesWritten)>& progressCallback)
{
	if (!srcPath || srcPath[0] == '\0' || !destWuaPath || destWuaPath[0] == '\0')
		return IOS_DECRYPT_UNABLE_TO_MOUNT;

	fs::path dest(destWuaPath);
	std::error_code ec;
	fs::create_directories(dest.parent_path(), ec);

	std::error_code eqEc;
	if (fs::exists(dest, eqEc) && fs::equivalent(fs::path(srcPath), dest, eqEc))
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: destination is the source file '{}'", srcPath);
		return IOS_DECRYPT_DEST_NOT_WRITABLE;
	}

	TitleInfo titleInfo{fs::path(srcPath)};
	if (!titleInfo.IsValid())
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: could not recognize '{}' as a title", srcPath);
		return IOS_DECRYPT_UNABLE_TO_MOUNT;
	}

	int fd = open(destWuaPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: destination '{}' is not writable", dest.string());
		return IOS_DECRYPT_DEST_NOT_WRITABLE;
	}

	WuaWriterContext writerCtx{fd};
	ZArchiveWriter archiveWriter(&WuaWriterContext::NewOutputFile, &WuaWriterContext::WriteOutputData, &writerCtx);

	std::string mountPath = TitleInfo::GetUniqueTempMountingPath();
	if (!titleInfo.Mount(mountPath, "", FSC_PRIORITY_BASE))
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: failed to mount '{}' for reading", srcPath);
		close(fd);
		return IOS_DECRYPT_UNABLE_TO_MOUNT;
	}

	std::string archiveRoot = fmt::format("{:016x}_v{}/", titleInfo.GetAppTitleId(), titleInfo.GetAppTitleVersion());

	uint64 bytesWritten = 0;
	uint32 filesWritten = 0;
	std::vector<uint8> scratchBuffer;
	uint32 failures = 0;
	bool completed = WuaWalkDirectory(archiveWriter, archiveRoot, mountPath, failures, cancelRequested,
		bytesWritten, filesWritten, progressCallback, scratchBuffer);

	titleInfo.Unmount(mountPath);

	if (!completed)
	{
		close(fd);
		std::error_code rmEc;
		fs::remove(dest, rmEc);
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: cancelled - '{}' removed", dest.string());
		return IOS_DECRYPT_CANCELLED;
	}

	archiveWriter.Finalize();
	const bool closeFailed = close(fd) != 0;

	if (writerCtx.writeFailed || closeFailed)
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: writing '{}' failed (disk full?) - removed", dest.string());
		std::error_code rmEc;
		fs::remove(dest, rmEc);
		return IOS_DECRYPT_DEST_NOT_WRITABLE;
	}

	ZArchiveReader* verify = ZArchiveReader::OpenFromFile(dest);
	if (!verify)
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: '{}' failed verification after writing", dest.string());
		std::error_code rmEc;
		fs::remove(dest, rmEc);
		return IOS_DECRYPT_DEST_NOT_WRITABLE;
	}
	delete verify;

	if (failures != 0)
	{
		cemuLog_log(LogType::Force, "Decrypt-to-WUA: {} entries failed - '{}' removed as incomplete", failures, dest.string());
		std::error_code rmEc;
		fs::remove(dest, rmEc);
		return IOS_DECRYPT_INCOMPLETE;
	}

	cemuLog_log(LogType::Force, "Decrypt-to-WUA: finished - {} files, {} bytes written to '{}'",
		filesWritten, bytesWritten, dest.string());

	return IOS_DECRYPT_OK;
}
