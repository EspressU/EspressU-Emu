#include "Cafe/Filesystem/fsc.h"
#include "Cafe/TitleList/TitleId.h"
#include "Cafe/TitleList/TitleInfo.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cemu/Logging/CemuLogging.h"

#include <zarchive/zarchivereader.h>
#include <zarchive/zarchivewriter.h>

#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

namespace
{
enum
{
	WUA_OK = 0,
	WUA_UNABLE_TO_MOUNT = 1,
	WUA_DEST_NOT_WRITABLE = 3,
	WUA_CANCELLED = 4,
	WUA_INCOMPLETE = 5,
	WUA_MIXED_GAMES = 9,
	WUA_DUPLICATE_TITLE = 10,
	WUA_NOTHING_TO_WRITE = 11,
};

constexpr uint32 kChunk = 4 * 1024 * 1024;

struct Writer
{
	int fd;
	bool failed = false;
	static void NewFile(int32_t, void*) {}
	static void Write(const void* data, size_t length, void* ctx)
	{
		Writer* self = (Writer*)ctx;
		size_t done = 0;
		const uint8* p = (const uint8*)data;
		while (done < length)
		{
			ssize_t n = write(self->fd, p + done, length - done);
			if (n <= 0)
			{
				self->failed = true;
				return;
			}
			done += (size_t)n;
		}
	}
};

bool IsSafeName(const std::string& name)
{
	if (name.empty() || name == "." || name == "..")
		return false;
	for (char c : name)
		if (c == '/' || c == '\\' || c == '\0')
			return false;
	return true;
}

bool IsJunkName(const std::string& name)
{
	return name == ".DS_Store" || name == "__MACOSX" || (name.size() >= 2 && name[0] == '.' && name[1] == '_');
}

bool Walk(ZArchiveWriter& writer, const std::string& archivePath, const std::string& fscPath, uint32& failures,
	std::atomic_bool& cancel, uint64& bytes, uint32& files,
	const std::function<void(uint64, uint32)>& progress, std::vector<uint8>& buffer)
{
	sint32 status;
	std::unique_ptr<FSCVirtualFile, void (*)(FSCVirtualFile*)> it(fsc_openDirIterator(fscPath.c_str(), &status), fsc_close);
	if (!it)
	{
		cemuLog_log(LogType::Force, "WUA build: could not open directory '{}'", fscPath);
		failures++;
		return true;
	}
	writer.MakeDir(archivePath.c_str(), false);

	FSCDirEntry entry;
	while (fsc_nextDir(it.get(), &entry))
	{
		if (cancel.load())
			return false;
		std::string name(entry.GetPath());
		if (!IsSafeName(name))
		{
			failures++;
			continue;
		}
		if (IsJunkName(name))
			continue;
		if (entry.isDirectory)
		{
			if (!Walk(writer, archivePath + name + "/", fscPath + name + "/", failures, cancel, bytes, files, progress, buffer))
				return false;
			continue;
		}
		if (!entry.isFile)
			continue;

		sint32 openStatus;
		std::unique_ptr<FSCVirtualFile, void (*)(FSCVirtualFile*)> file(
			fsc_open((fscPath + name).c_str(), FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &openStatus), fsc_close);
		if (!file)
		{
			cemuLog_log(LogType::Force, "WUA build: could not open '{}'", fscPath + name);
			failures++;
			continue;
		}
		writer.StartNewFile((archivePath + name).c_str());
		if (buffer.size() < kChunk)
			buffer.resize(kChunk);
		uint32 got;
		while ((got = file->fscReadData(buffer.data(), (uint32)buffer.size())) != 0)
		{
			if (cancel.load())
				return false;
			writer.AppendData(buffer.data(), got);
			bytes += got;
		}
		files++;
		if (progress)
			progress(bytes, files);
	}
	return true;
}

struct Source
{
	std::string path;
	std::unique_ptr<TitleInfo> info;
	std::string root;
	uint64 baseId = 0;
};
}

int IOSWuaContent_Build(const std::vector<std::string>& srcs, const char* destPath, std::atomic_bool& cancel,
	const std::function<void(uint64_t bytesWritten, uint32_t filesWritten)>& progress)
{
	if (srcs.empty() || !destPath || destPath[0] == '\0')
		return WUA_NOTHING_TO_WRITE;

	std::vector<Source> sources;
	std::set<std::string> roots;
	for (const std::string& s : srcs)
	{
		Source src;
		src.path = s;
		src.info = std::make_unique<TitleInfo>(fs::path(s));
		if (!src.info->IsValid())
		{
			cemuLog_log(LogType::Force, "WUA build: '{}' is not a recognizable title", s);
			return WUA_UNABLE_TO_MOUNT;
		}
		TitleId tid = src.info->GetAppTitleId();
		src.root = fmt::format("{:016x}_v{}/", (uint64)tid, src.info->GetAppTitleVersion());
		TitleId base = tid;
		CafeTitleList::FindBaseTitleId(tid, base);
		src.baseId = (uint64)base;
		if (!sources.empty() && src.baseId != sources.front().baseId)
		{
			cemuLog_log(LogType::Force, "WUA build: '{}' belongs to a different game", s);
			return WUA_MIXED_GAMES;
		}
		if (!roots.insert(src.root).second)
		{
			cemuLog_log(LogType::Force, "WUA build: duplicate title {}", src.root);
			return WUA_DUPLICATE_TITLE;
		}
		sources.push_back(std::move(src));
	}

	fs::path dest(destPath);
	std::error_code ec;
	fs::create_directories(dest.parent_path(), ec);
	for (const Source& s : sources)
	{
		std::error_code eq;
		if (fs::exists(dest, eq) && fs::equivalent(fs::path(s.path), dest, eq))
			return WUA_DEST_NOT_WRITABLE;
	}

	const std::string partPath = dest.string() + ".part";
	int fd = open(partPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return WUA_DEST_NOT_WRITABLE;

	auto discard = [&]() {
		std::error_code rm;
		fs::remove(fs::path(partPath), rm);
	};

	Writer ctx{fd};
	uint64 bytes = 0;
	uint32 files = 0;
	uint32 failures = 0;
	bool completed = true;
	{
		ZArchiveWriter writer(&Writer::NewFile, &Writer::Write, &ctx);
		std::vector<uint8> buffer;
		for (Source& s : sources)
		{
			std::string mount = TitleInfo::GetUniqueTempMountingPath();
			if (!s.info->Mount(mount, "", FSC_PRIORITY_BASE))
			{
				cemuLog_log(LogType::Force, "WUA build: failed to mount '{}'", s.path);
				failures++;
				continue;
			}
			bool ok = Walk(writer, s.root, mount, failures, cancel, bytes, files, progress, buffer);
			s.info->Unmount(mount);
			if (!ok)
			{
				completed = false;
				break;
			}
		}
		if (completed)
			writer.Finalize();
	}
	const bool closeFailed = close(fd) != 0;

	if (!completed)
	{
		discard();
		return WUA_CANCELLED;
	}
	if (ctx.failed || closeFailed)
	{
		discard();
		return WUA_DEST_NOT_WRITABLE;
	}
	if (failures != 0)
	{
		discard();
		return WUA_INCOMPLETE;
	}
	{
		std::unique_ptr<ZArchiveReader> verify(ZArchiveReader::OpenFromFile(fs::path(partPath)));
		if (!verify)
		{
			discard();
			return WUA_DEST_NOT_WRITABLE;
		}
		for (const Source& s : sources)
		{
			if (verify->LookUp(s.root.substr(0, s.root.size() - 1), false, true) == ZARCHIVE_INVALID_NODE)
			{
				discard();
				return WUA_INCOMPLETE;
			}
		}
	}
	std::error_code mv;
	fs::rename(fs::path(partPath), dest, mv);
	if (mv)
	{
		discard();
		return WUA_DEST_NOT_WRITABLE;
	}
	cemuLog_log(LogType::Force, "WUA build: {} titles, {} files, {} bytes -> '{}'", sources.size(), files, bytes, dest.string());
	return WUA_OK;
}

std::string IOSWuaContent_ListTitles(const char* wuaPath)
{
	std::string out;
	if (!wuaPath)
		return out;
	std::unique_ptr<ZArchiveReader> zar(ZArchiveReader::OpenFromFile(fs::path(wuaPath)));
	if (!zar)
		return out;
	ZArchiveNodeHandle root = zar->LookUp("", false, true);
	if (root == ZARCHIVE_INVALID_NODE)
		return out;
	for (uint32 i = 0; i < zar->GetDirEntryCount(root); i++)
	{
		ZArchiveReader::DirEntry e;
		if (!zar->GetDirEntry(root, i, e) || !e.isDirectory)
			continue;
		TitleId id;
		uint16 version;
		if (!TitleInfo::ParseWuaTitleFolderName(e.name, id, version))
			continue;
		out += fmt::format("{:016x} {}\n", (uint64)id, version);
	}
	return out;
}
