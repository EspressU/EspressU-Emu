#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/LatteConst.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteShaderCache.h"
#include "Cafe/HW/Latte/LegacyShaderDecompiler/LatteDecompiler.h"
#include "Cafe/HW/Latte/Core/FetchShader.h"
#include "Cemu/FileCache/FileCache.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "WindowSystem.h"

#include "Cafe/HW/Latte/Renderer/Renderer.h"
#ifdef ENABLE_OPENGL
#include "Cafe/HW/Latte/Renderer/OpenGL/RendererShaderGL.h"
#endif
#ifdef ENABLE_VULKAN
#include "Cafe/HW/Latte/Renderer/Vulkan/RendererShaderVk.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanPipelineStableCache.h"
#endif
#ifdef ENABLE_METAL
#include "Cafe/HW/Latte/Renderer/Metal/RendererShaderMtl.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalPipelineCache.h"
#endif

#include <imgui.h>
#include "imgui/imgui_extension.h"

#include "config/ActiveSettings.h"
#include "Cafe/TitleList/GameInfo.h"

#include "util/helpers/SystemException.h"
#include "Cafe/HW/Latte/Common/RegisterSerializer.h"
#include "Cafe/HW/Latte/Common/ShaderSerializer.h"
#include "util/helpers/Serializer.h"

#include <audio/IAudioAPI.h>
#include <util/bootSound/BootSoundReader.h>
#include <thread>
#include <set>
#include <map>
#include <openssl/sha.h>
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"
#if __APPLE__
#include <sys/clonefile.h>
#endif

#if BOOST_OS_WINDOWS
#include <psapi.h>
#endif

#define SHADER_CACHE_COMPILE_QUEUE_SIZE		(32)

struct
{
	sint32 compiledShaderCount;
	sint32 vertexShaderCount;
	sint32 geometryShaderCount;
	sint32 pixelShaderCount;
}shaderCacheScreenStats;

struct
{
	ImTextureID textureTVId;
	ImTextureID textureDRCId;
	sint32 loadedShaderFiles;
	sint32 shaderFileCount;
	uint32 loadedPipelines;
	sint32 pipelineFileCount;
}g_shaderCacheLoaderState;

FileCache* s_shaderCacheGeneric = nullptr;

#define SHADER_CACHE_GENERIC_EXTRA_VERSION		2

#define SHADER_CACHE_TYPE_VERTEX				(0)
#define SHADER_CACHE_TYPE_GEOMETRY				(1)
#define SHADER_CACHE_TYPE_PIXEL					(2)

bool LatteShaderCache_readSeparableShader(uint8* shaderInfoData, sint32 shaderInfoSize, uint64* currentAuxHashOut = nullptr);

struct ShaderRekey
{
	uint64 name1;
	uint64 storedAuxHash;
	uint64 currentAuxHash;
};
static std::vector<ShaderRekey> s_pendingShaderRekeys;

static std::mutex s_auxRekeyMutex;
static std::map<std::tuple<uint8, uint64, uint64>, uint64> s_auxRekeyMap;

static void LatteShaderCache_recordAuxRekey(LatteConst::ShaderType type, uint64 baseHash, uint64 storedAuxHash, uint64 currentAuxHash)
{
	if (storedAuxHash == currentAuxHash)
		return;
	std::lock_guard lock(s_auxRekeyMutex);
	s_auxRekeyMap[std::make_tuple((uint8)type, baseHash, storedAuxHash)] = currentAuxHash;
}

static bool LatteShaderCache_translateAuxHash(LatteConst::ShaderType type, uint64 baseHash, uint64& auxHash)
{
	std::lock_guard lock(s_auxRekeyMutex);
	auto it = s_auxRekeyMap.find(std::make_tuple((uint8)type, baseHash, auxHash));
	if (it == s_auxRekeyMap.end())
		return false;
	auxHash = it->second;
	return true;
}

static bool LatteShaderCache_discardIfAlreadyLoaded(LatteDecompilerShader* shader, uint64 baseHash, uint64 auxHash)
{
	LatteDecompilerShader* existing = nullptr;
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
		existing = LatteSHRC_FindVertexShader(baseHash, auxHash);
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
		existing = LatteSHRC_FindGeometryShader(baseHash, auxHash);
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
		existing = LatteSHRC_FindPixelShader(baseHash, auxHash);
	if (!existing)
		return false;
	LatteShader_CleanupAfterCompile(shader);
	delete shader;
	return true;
}

static void LatteShaderCache_applyShaderRekeys()
{
	if (s_pendingShaderRekeys.empty() || !s_shaderCacheGeneric)
	{
		s_pendingShaderRekeys.clear();
		return;
	}
	std::vector<FileCache::RekeyJob> jobs;
	jobs.reserve(s_pendingShaderRekeys.size());
	for (const ShaderRekey& r : s_pendingShaderRekeys)
	{
		const uint64 currentAuxHash = r.currentAuxHash;
		jobs.push_back({ { r.name1, r.storedAuxHash }, { r.name1, currentAuxHash }, [currentAuxHash](std::vector<uint8>& entry)
		{
			for (sint32 i = 0; i < 8; i++)
				entry[9 + i] = (uint8)(currentAuxHash >> (56 - 8 * i));
		} });
	}
	const uint32 moved = s_shaderCacheGeneric->RekeyEntries(jobs);
	if (moved != jobs.size())
		cemuLog_log(LogType::Force, "Shader cache: {} of {} entries could not be rewritten under the hash this renderer uses, the old entries were kept", jobs.size() - moved, jobs.size());
	s_pendingShaderRekeys.clear();
}

void LatteShaderCache_TranslatePipelineEntry(uint64 name1, uint64 name2, std::vector<uint8>& blob, std::vector<LatteShaderCachePipelineRekey>& rekeys)
{
	if (blob.size() < 2 || blob[0] != 1)
		return;
	const uint8 presentMask = blob[1];
	const LatteConst::ShaderType types[3] = { LatteConst::ShaderType::Vertex, LatteConst::ShaderType::Geometry, LatteConst::ShaderType::Pixel };
	size_t hashCount = 0;
	for (sint32 i = 0; i < 3; i++)
		hashCount += (presentMask >> i) & 1;
	if (blob.size() < 2 + hashCount * 16)
		return;
	auto readBE = [&](size_t pos) { uint64 v = 0; for (sint32 i = 0; i < 8; i++) v = (v << 8) | blob[pos + i]; return v; };
	std::vector<uint8> patched = blob;
	bool changed = false;
	size_t pos = 2;
	for (sint32 i = 0; i < 3; i++)
	{
		if (!((presentMask >> i) & 1))
			continue;
		uint64 auxHash = readBE(pos + 8);
		if (LatteShaderCache_translateAuxHash(types[i], readBE(pos), auxHash))
		{
			for (sint32 b = 0; b < 8; b++)
				patched[pos + 8 + b] = (uint8)(auxHash >> (56 - 8 * b));
			changed = true;
		}
		pos += 16;
	}
	if (!changed)
		return;
	uint8 hash[SHA256_DIGEST_LENGTH];
	SHA256(patched.data(), patched.size(), hash);
	LatteShaderCachePipelineRekey rekey;
	rekey.fromName1 = name1;
	rekey.fromName2 = name2;
	rekey.toName1 = *(uint64be*)(hash + 0);
	rekey.toName2 = *(uint64be*)(hash + 8);
	rekey.data = patched;
	rekeys.push_back(std::move(rekey));
	blob = std::move(patched);
}

void LatteShaderCache_ApplyPipelineRekeys(FileCache* pipelineCache, std::vector<LatteShaderCachePipelineRekey>& rekeys)
{
	if (pipelineCache && !rekeys.empty())
	{
		std::vector<FileCache::RekeyJob> jobs;
		jobs.reserve(rekeys.size());
		for (auto& r : rekeys)
		{
			auto data = std::make_shared<std::vector<uint8>>(std::move(r.data));
			jobs.push_back({ { r.fromName1, r.fromName2 }, { r.toName1, r.toName2 }, [data](std::vector<uint8>& entry) { entry = *data; } });
		}
		const uint32 moved = pipelineCache->RekeyEntries(jobs);
		if (moved != jobs.size())
			cemuLog_log(LogType::Force, "Pipeline cache: {} of {} entries could not be rewritten under the current shader hashes, the old entries were kept", jobs.size() - moved, jobs.size());
	}
	rekeys.clear();
}

void LatteShaderCache_LoadPipelineCache(uint64 cacheTitleId);
bool LatteShaderCache_updatePipelineLoadingProgress();
void LatteShaderCache_ShowProgress(const std::function <bool(void)>& loadUpdateFunc, bool isPipelines);

struct
{
	struct
	{
		LatteDecompilerShader* shader;
	}entry[SHADER_CACHE_COMPILE_QUEUE_SIZE];
	sint32 count;
}shaderCompileQueue;

void LatteShaderCache_initCompileQueue()
{
	shaderCompileQueue.count = 0;
}

void LatteShaderCache_addToCompileQueue(LatteDecompilerShader* shader)
{
	cemu_assert(shaderCompileQueue.count < SHADER_CACHE_COMPILE_QUEUE_SIZE);
	shaderCompileQueue.entry[shaderCompileQueue.count].shader = shader;
	shaderCompileQueue.count++;
}

void LatteShaderCache_removeFromCompileQueue(sint32 index)
{
	for (sint32 i = index; i<shaderCompileQueue.count-1; i++)
		shaderCompileQueue.entry[i].shader = shaderCompileQueue.entry[i + 1].shader;
	shaderCompileQueue.count--;
}

void LatteShaderCache_updateCompileQueue(sint32 maxRemainingEntries)
{
	while (true)
	{
		if (shaderCompileQueue.count <= maxRemainingEntries)
			break;
		auto shader = shaderCompileQueue.entry[0].shader;
		if (shader)
			LatteShader_FinishCompilation(shader);
		LatteShaderCache_removeFromCompileQueue(0);
	}
}

typedef struct
{
	unsigned char imageTypeCode;
	short int imageWidth;
	short int imageHeight;
	unsigned char bitCount;
	std::vector<uint8> imageData;
} TGAFILE;

bool LoadTGAFile(const std::vector<uint8>& buffer, TGAFILE *tgaFile)
{
	if (buffer.size() <= 18)
		return false;

	tgaFile->imageTypeCode = buffer[2];
	if (tgaFile->imageTypeCode != 2 && tgaFile->imageTypeCode != 3)
		return false;

	tgaFile->imageWidth = *(uint16*)(buffer.data() + 12);
	tgaFile->imageHeight = *(uint16*)(buffer.data() + 14);
	tgaFile->bitCount = buffer[16];

	const uint8 colorMode = tgaFile->bitCount / 8;
	if (colorMode != 3)
		return false;

	const uint32 imageSize = tgaFile->imageWidth * tgaFile->imageHeight * colorMode;
	if (imageSize + 18 >= buffer.size())
		return false;

	tgaFile->imageData.resize(imageSize);
	std::copy(buffer.data() + 18, buffer.data() + 18 + imageSize, tgaFile->imageData.begin());
	for (uint32 imageIdx = 0; imageIdx < imageSize; imageIdx += colorMode)
	{
		std::swap(tgaFile->imageData[imageIdx], tgaFile->imageData[imageIdx + 2]);
	}

	return true;
}

class BootSoundPlayer
{
  public:
	BootSoundPlayer() = default;
	~BootSoundPlayer()
	{
		m_stopRequested = true;
		if (m_bootSndPlayThread.joinable())
		{
			for (int i = 0; i < 50 && !m_threadFinished; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (m_threadFinished)
				m_bootSndPlayThread.join();
			else
				m_bootSndPlayThread.detach();
		}
	}

	void StartSound()
	{
		if (!m_bootSndPlayThread.joinable())
		{
			m_fadeOutRequested = false;
			m_stopRequested = false;
			m_threadFinished = false;
			m_bootSndPlayThread = std::thread{[this]() {
				StreamBootSound();
				m_threadFinished = true;
			}};
		}
	}

	void FadeOutSound()
	{
		m_fadeOutRequested = true;
	}

	void ApplyFadeOutEffect(std::span<sint16> samples, uint64& fadeOutSample, uint64 fadeOutDuration)
	{
		for (size_t i = 0; i < samples.size(); i += 2)
		{
			const float decibel = (float)fadeOutSample / fadeOutDuration * -60.0f;
			const float volumeFactor = pow(10, decibel / 20);
			samples[i] *= volumeFactor;
			samples[i + 1] *= volumeFactor;
			fadeOutSample++;
		}
	}

	void StreamBootSound()
	{
		SetThreadName("bootsnd");
		constexpr sint32 sampleRate = 48'000;
		constexpr sint32 bitsPerSample = 16;
		constexpr sint32 samplesPerBlock = sampleRate / 10;
		constexpr sint32 nChannels = 2;
		static_assert(bitsPerSample % 8 == 0, "bits per sample is not a multiple of 8");

		AudioAPIPtr bootSndAudioDev;

		try
		{
			bootSndAudioDev = IAudioAPI::CreateDeviceFromConfig(IAudioAPI::AudioType::TV, sampleRate, nChannels, samplesPerBlock, bitsPerSample);
			if(!bootSndAudioDev)
				return;
		}
		catch (const std::runtime_error& ex)
		{
			cemuLog_log(LogType::Force, "Failed to initialise audio device for bootup sound");
			return;
		}
		bootSndAudioDev->SetAudioDelayOverride(4);
		bootSndAudioDev->Play();

		std::string sndPath = fmt::format("{}/meta/{}", CafeSystem::GetMlcStoragePath(CafeSystem::GetForegroundTitleId()), "bootSound.btsnd");
		sint32 fscStatus = FSC_STATUS_UNDEFINED;

		if(!fsc_doesFileExist(sndPath.c_str()))
			return;

		FSCVirtualFile* bootSndFileHandle = fsc_open(sndPath.c_str(), FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &fscStatus);
		if(!bootSndFileHandle)
		{
			cemuLog_log(LogType::Force, "failed to open bootSound.btsnd");
			return;
		}

		constexpr sint32 audioBlockSize = samplesPerBlock * (bitsPerSample/8) * nChannels;
		BootSoundReader bootSndFileReader(bootSndFileHandle, audioBlockSize);

		uint64 fadeOutSample = 0;
		constexpr uint64 fadeOutDuration = sampleRate * 2;
		while(fadeOutSample < fadeOutDuration && !m_stopRequested)
		{
			while (bootSndAudioDev->NeedAdditionalBlocks())
			{
				sint16* data = bootSndFileReader.getSamples();
				if(data == nullptr)
				{
					m_stopRequested = true;
					break;
				}
				if(m_fadeOutRequested)
					ApplyFadeOutEffect({data, samplesPerBlock * nChannels}, fadeOutSample, fadeOutDuration);

				bootSndAudioDev->FeedBlock(data);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(samplesPerBlock / (sampleRate/ 1'000)));
		}

		if(bootSndFileHandle)
			fsc_close(bootSndFileHandle);
	}

  private:
	std::thread m_bootSndPlayThread;
	std::atomic_bool m_fadeOutRequested = false;
	std::atomic_bool m_stopRequested = false;
	std::atomic_bool m_threadFinished = false;
};
static BootSoundPlayer g_bootSndPlayer;

void LatteShaderCache_finish()
{
	switch (g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
		RendererShaderVk::ShaderCacheLoading_end();
		return;
#endif
#ifdef ENABLE_OPENGL
	case RendererAPI::OpenGL:
		RendererShaderGL::ShaderCacheLoading_end();
		return;
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		RendererShaderMtl::ShaderCacheLoading_end();
		return;
#endif
	}
}

uint32 LatteShaderCache_getShaderCacheExtraVersion(uint64 titleId)
{
	const uint32 cacheFileVersion = 1;
	uint32 extraVersion = ((uint32)(titleId >> 32) + ((uint32)titleId) * 3) + cacheFileVersion + 0xe97af1ad;
	return extraVersion;
}

uint32 LatteShaderCache_getPipelineCacheExtraVersion(uint64 titleId)
{
	const uint32 cacheFileVersion = 1;
	uint32 extraVersion = ((uint32)(titleId >> 32) + ((uint32)titleId) * 3) + cacheFileVersion;
	return extraVersion;
}

void LatteShaderCache_drawBackgroundImage(ImTextureID texture, int width, int height)
{
	const auto kPopupFlags =
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize |
			ImGuiWindowFlags_NoBringToFrontOnFocus;
	auto& io = ImGui::GetIO();
	ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
	ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
	if (ImGui::Begin("Background texture", nullptr, kPopupFlags))
	{
		if (texture)
		{
			float imageDisplayWidth = io.DisplaySize.x;
			float imageDisplayHeight = height * imageDisplayWidth / width;

			float paddingLeftAndRight = 0.0f;
			float paddingTopAndBottom = (io.DisplaySize.y - imageDisplayHeight) / 2.0f;
			if (imageDisplayHeight > io.DisplaySize.y)
			{
				imageDisplayHeight = io.DisplaySize.y;
				imageDisplayWidth = width * imageDisplayHeight / height;
				paddingLeftAndRight = (io.DisplaySize.x - imageDisplayWidth) / 2.0f;
				paddingTopAndBottom = 0.0f;
			}

			ImGui::GetWindowDrawList()->AddImage(texture, ImVec2(paddingLeftAndRight, paddingTopAndBottom),
												 ImVec2(io.DisplaySize.x - paddingLeftAndRight,
														io.DisplaySize.y - paddingTopAndBottom), {0, 1}, {1, 0});
		}
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

static std::vector<fs::path> ShaderCacheGuard_List(const fs::path& dir, uint64 titleId)
{
	std::vector<fs::path> files;
	std::error_code ec;
	const std::string prefix = fmt::format("{:016x}_", titleId);
	for (const auto& it : fs::directory_iterator(dir, ec))
	{
		std::error_code fileEc;
		if (!it.is_regular_file(fileEc) || it.path().extension() != ".bin")
			continue;
		if (_pathToUtf8(it.path().filename()).rfind(prefix, 0) == 0)
			files.push_back(it.path());
	}
	return files;
}

static bool ShaderCacheGuard_Copy(const fs::path& src, const fs::path& dst)
{
	std::error_code ec;
	fs::path tmp = dst;
	tmp += ".tmp";
	fs::remove(tmp, ec);
	bool copied = false;
#if __APPLE__
	copied = clonefile(src.c_str(), tmp.c_str(), 0) == 0;
#endif
	if (!copied)
		copied = fs::copy_file(src, tmp, fs::copy_options::overwrite_existing, ec) && !ec;
	if (!copied || !FileCache::Verify(tmp))
	{
		fs::remove(tmp, ec);
		return false;
	}
	fs::rename(tmp, dst, ec);
	if (ec)
	{
		fs::remove(tmp, ec);
		return false;
	}
	return true;
}

static uint64 s_guardTitleId = 0;
static bool s_guardLoadClean = false;
static uint32 s_guardDamageBase = 0;

static fs::path ShaderCacheGuard_RestoreMarker(uint64 titleId)
{
	return ActiveSettings::GetCachePath("shaderCache/backup/{:016x}.restore", titleId);
}

static void ShaderCacheGuard_CheckRepair(uint64 titleId)
{
	for (const fs::path& live : ShaderCacheGuard_List(ActiveSettings::GetCachePath("shaderCache/transferable"), titleId))
	{
		if (FileCache::Verify(live))
			continue;
		FileStream* marker = FileStream::createFile2(ShaderCacheGuard_RestoreMarker(titleId));
		delete marker;
		cemuLog_log(LogType::Force, "Shader cache guard: \"{}\" still fails the check after repair; its backup will be restored at the next start", _pathToUtf8(live.filename()));
		return;
	}
}

static void ShaderCacheGuard_BeforeLoad(uint64 titleId)
{
	std::error_code ec;
	const fs::path liveDir = ActiveSettings::GetCachePath("shaderCache/transferable");
	const fs::path backupDir = ActiveSettings::GetCachePath("shaderCache/backup");
	fs::create_directories(backupDir, ec);
	FileCache::SetBackupDirectory(backupDir);
	const fs::path restoreMarker = ShaderCacheGuard_RestoreMarker(titleId);
	const bool repairFailed = fs::exists(restoreMarker, ec);
	for (const fs::path& backup : ShaderCacheGuard_List(backupDir, titleId))
	{
		const fs::path live = liveDir / backup.filename();
		const bool liveExists = fs::exists(live, ec);
		if (liveExists && FileCache::Verify(live, false) && (!repairFailed || FileCache::Verify(live)))
			continue;
		if (!FileCache::Verify(backup))
		{
			cemuLog_log(LogType::Force, "Shader cache guard: backup \"{}\" is not usable, leaving the live file alone", _pathToUtf8(backup.filename()));
			continue;
		}
		if (liveExists)
		{
			fs::path aside = live;
			aside += ".unreadable";
			fs::remove(aside, ec);
			fs::rename(live, aside, ec);
		}
		if (ShaderCacheGuard_Copy(backup, live))
			cemuLog_log(LogType::Force, "Shader cache guard: restored \"{}\" from its backup ({})", _pathToUtf8(live.filename()),
				!liveExists ? "it was missing" : (repairFailed ? "repairing its damaged entries didn't fix it" : "its header or file table could not be read"));
		else
			cemuLog_log(LogType::Force, "Shader cache guard: could not restore \"{}\"", _pathToUtf8(live.filename()));
	}
	fs::remove(restoreMarker, ec);
}

static void ShaderCacheGuard_AfterLoad(uint64 titleId, bool clean)
{
	std::error_code ec;
	s_guardTitleId = titleId;
	s_guardLoadClean = clean;
	if (!clean)
	{
		cemuLog_log(LogType::Force, "Shader cache guard: damaged entries were repaired or removed during loading; backup left as it was");
		ShaderCacheGuard_CheckRepair(titleId);
		return;
	}
	const fs::path backupDir = ActiveSettings::GetCachePath("shaderCache/backup");
	for (const fs::path& live : ShaderCacheGuard_List(ActiveSettings::GetCachePath("shaderCache/transferable"), titleId))
	{
		if (!ShaderCacheGuard_Copy(live, backupDir / live.filename()))
			cemuLog_log(LogType::Force, "Shader cache guard: could not back up \"{}\"; the previous backup is kept", _pathToUtf8(live.filename()));
	}
}

static void ShaderCacheGuard_AfterSession()
{
	const uint64 titleId = s_guardTitleId;
	s_guardTitleId = 0;
	if (titleId == 0)
		return;
	if (FileCache::GetDamagedEntryTotal() != s_guardDamageBase)
		s_guardLoadClean = false;
	if (!s_guardLoadClean)
	{
		cemuLog_log(LogType::Force, "Shader cache guard: damaged entries this session, keeping the backup from before it");
		ShaderCacheGuard_CheckRepair(titleId);
		return;
	}
	if (LatteWait::Get().gpuError.load(std::memory_order_relaxed))
	{
		cemuLog_log(LogType::Force, "Shader cache guard: session ended on a GPU error, keeping the backup from before it");
		return;
	}
	const fs::path backupDir = ActiveSettings::GetCachePath("shaderCache/backup");
	int saved = 0, kept = 0;
	for (const fs::path& live : ShaderCacheGuard_List(ActiveSettings::GetCachePath("shaderCache/transferable"), titleId))
	{
		if (ShaderCacheGuard_Copy(live, backupDir / live.filename()))
			saved++;
		else
			kept++;
	}
	cemuLog_log(LogType::Force, "Shader cache guard: session ended cleanly; {} cache file(s) saved as the new backup{}", saved,
		kept ? fmt::format(", {} failed the check and kept their previous backup", kept) : std::string());
	if (kept)
		ShaderCacheGuard_CheckRepair(titleId);

}

void LatteShaderCache_Load()
{
	shaderCacheScreenStats.compiledShaderCount = 0;
	shaderCacheScreenStats.vertexShaderCount = 0;
	shaderCacheScreenStats.geometryShaderCount = 0;
	shaderCacheScreenStats.pixelShaderCount = 0;

	uint64 cacheTitleId = CafeSystem::GetForegroundTitleId();

	const auto timeLoadStart = now_cached();
#if BOOST_OS_WINDOWS
	PROCESS_MEMORY_COUNTERS pmc1;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc1, sizeof(PROCESS_MEMORY_COUNTERS));
	LONGLONG totalMem1 = pmc1.PagefileUsage;
#endif
	LatteShaderCache_initCompileQueue();
	std::error_code ec;
	fs::create_directories(ActiveSettings::GetCachePath("shaderCache/transferable"), ec);
	fs::create_directories(ActiveSettings::GetCachePath("shaderCache/precompiled"), ec);
	ShaderCacheGuard_BeforeLoad(cacheTitleId);
	s_pendingShaderRekeys.clear();
	{
		std::lock_guard lock(s_auxRekeyMutex);
		s_auxRekeyMap.clear();
	}
	s_guardDamageBase = FileCache::GetDamagedEntryTotal();
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
		RendererShaderVk::ShaderCacheLoading_begin(cacheTitleId);
		break;
#endif
#ifdef ENABLE_OPENGL
	case RendererAPI::OpenGL:
		RendererShaderGL::ShaderCacheLoading_begin(cacheTitleId);
		break;
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		RendererShaderMtl::ShaderCacheLoading_begin(cacheTitleId);
		break;
#endif
	}

	fs::path pathGeneric;
	switch(g_renderer->GetType())
	{
	case RendererAPI::Metal:
	    pathGeneric = ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_mtlshaders.bin", cacheTitleId);
		break;
	default:
	    pathGeneric = ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_shaders.bin", cacheTitleId);
		break;
	}

	uint32 transferableExtraVersion = SHADER_CACHE_GENERIC_EXTRA_VERSION;
    s_shaderCacheGeneric = FileCache::Open(pathGeneric, false, transferableExtraVersion);
	if(!s_shaderCacheGeneric)
        s_shaderCacheGeneric = FileCache::Open(pathGeneric, true, LatteShaderCache_getShaderCacheExtraVersion(cacheTitleId));
	if(!s_shaderCacheGeneric)
	{
		cemuLog_log(LogType::Force, "Unable to open or create shader cache file \"{}\"", _pathToUtf8(pathGeneric));
		LatteShaderCache_finish();
		return;
	}
	s_shaderCacheGeneric->UseCompression(false);

	sint32 entryCount = s_shaderCacheGeneric->GetMaximumFileIndex();
	g_shaderCacheLoaderState.shaderFileCount = s_shaderCacheGeneric->GetFileCount();
	g_shaderCacheLoaderState.loadedShaderFiles = 0;

	auto loadBackgroundTexture = [](bool isTV, ImTextureID& out)
	{
		TGAFILE file{};
		out = nullptr;

		std::string fileName = isTV ? "bootTvTex.tga" : "bootDRCTex.tga";

		std::string texPath = fmt::format("{}/meta/{}", CafeSystem::GetMlcStoragePath(CafeSystem::GetForegroundTitleId()), fileName);
		sint32 status;
		auto fscfile = fsc_open(texPath.c_str(), FSC_ACCESS_FLAG::OPEN_FILE | FSC_ACCESS_FLAG::READ_PERMISSION, &status);
		if (fscfile)
		{
			uint32 size = fsc_getFileSize(fscfile);
			if (size > 0)
			{
				std::vector<uint8> tmpData(size);
				fsc_readFile(fscfile, tmpData.data(), size);
				const bool backgroundLoaded = LoadTGAFile(tmpData, &file);

				if (backgroundLoaded)
					out = g_renderer->GenerateTexture(file.imageData, { file.imageWidth, file.imageHeight });
			}

			fsc_close(fscfile);
		}
	};

	loadBackgroundTexture(true, g_shaderCacheLoaderState.textureTVId);
	loadBackgroundTexture(false, g_shaderCacheLoaderState.textureDRCId);

	if(GetConfig().play_boot_sound)
		g_bootSndPlayer.StartSound();

	sint32 numLoadedShaders = 0;
	sint32 numDamagedShaders = 0;
	uint32 loadIndex = 0;

	auto LoadShadersUpdate = [&]() -> bool
	{
		if (loadIndex >= (uint32)s_shaderCacheGeneric->GetMaximumFileIndex())
			return false;
		LatteShaderCache_updateCompileQueue(SHADER_CACHE_COMPILE_QUEUE_SIZE - 2);
		uint64 name1;
		uint64 name2;
		std::vector<uint8> fileData;
		if (!s_shaderCacheGeneric->GetFileByIndex(loadIndex, &name1, &name2, fileData))
		{
			loadIndex++;
			return true;
		}
		g_shaderCacheLoaderState.loadedShaderFiles++;
		uint64 currentAuxHash = name2;
		if (LatteShaderCache_readSeparableShader(fileData.data(), fileData.size(), &currentAuxHash) == false)
		{
			cemuLog_log(LogType::Force, "Shader cache entry {} invalid, deleting...", loadIndex);
			s_shaderCacheGeneric->DeleteFile({name1, name2 });
			numDamagedShaders++;
		}
		else if (currentAuxHash != name2 && fileData.size() >= 17)
			s_pendingShaderRekeys.push_back({ name1, name2, currentAuxHash });
		numLoadedShaders++;
		loadIndex++;
		return true;
	};

	LatteShaderCache_ShowProgress(LoadShadersUpdate, false);

	LatteShaderCache_updateCompileQueue(0);
	LatteShaderCache_applyShaderRekeys();
#if BOOST_OS_WINDOWS
	const auto timeLoadEnd = now_cached();
	const auto timeLoad = std::chrono::duration_cast<std::chrono::milliseconds>(timeLoadEnd - timeLoadStart).count();
	PROCESS_MEMORY_COUNTERS pmc2;
	GetProcessMemoryInfo(GetCurrentProcess(), &pmc2, sizeof(PROCESS_MEMORY_COUNTERS));
	LONGLONG totalMem2 = pmc2.PagefileUsage;
	LONGLONG memCommited = totalMem2 - totalMem1;
	cemuLog_log(LogType::Force, "Shader cache loaded with {} shaders. Commited mem {}MB. Took {}ms", numLoadedShaders, (sint32)(memCommited/1024/1024), timeLoad);
#endif
	LatteShaderCache_finish();
#if defined(ENABLE_VULKAN) || defined(ENABLE_METAL)
	if (g_renderer->GetType() == RendererAPI::Vulkan || g_renderer->GetType() == RendererAPI::Metal)
        LatteShaderCache_LoadPipelineCache(cacheTitleId);
#endif
	ShaderCacheGuard_AfterLoad(cacheTitleId, numDamagedShaders == 0 && s_shaderCacheGeneric->GetDamagedEntryCount() == 0 && FileCache::GetDamagedEntryTotal() == s_guardDamageBase);

	g_renderer->BeginFrame(true);
	if (g_renderer->ImguiBegin(true))
	{
		LatteShaderCache_drawBackgroundImage(g_shaderCacheLoaderState.textureTVId, 1280, 720);
		g_renderer->ImguiEnd();
	}
	g_renderer->BeginFrame(false);
	if (g_renderer->ImguiBegin(false))
	{
		LatteShaderCache_drawBackgroundImage(g_shaderCacheLoaderState.textureDRCId, 854, 480);
		g_renderer->ImguiEnd();
	}

	g_renderer->SwapBuffers(true, true);

	if (g_shaderCacheLoaderState.textureTVId)
		g_renderer->DeleteTexture(g_shaderCacheLoaderState.textureTVId);
	if (g_shaderCacheLoaderState.textureDRCId)
		g_renderer->DeleteTexture(g_shaderCacheLoaderState.textureDRCId);

	g_bootSndPlayer.FadeOutSound();

	if(Latte_GetStopSignal())
		LatteThread_Exit();
}

void LatteShaderCache_ShowProgress(const std::function <bool(void)>& loadUpdateFunc, bool isPipelines)
{
	const auto kPopupFlags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
	const auto textColor = 0xFF888888;

	auto lastFrameUpdate = tick_cached();

	while (true)
	{
        if (Latte_GetStopSignal())
            break;
		bool r = loadUpdateFunc();
		if (!r)
			break;

		if ((tick_cached() - lastFrameUpdate) < std::chrono::milliseconds(1000 / 20))
			continue;

		int w, h;
		WindowSystem::GetWindowPhysSize(w, h);
		const Vector2f window_size{ (float)w,(float)h };

		ImGui_GetFont(window_size.y / 32.0f);
		ImGui_GetFont(window_size.y / 48.0f);

		g_renderer->BeginFrame(true);
		if (g_renderer->ImguiBegin(true))
		{
			auto& io = ImGui::GetIO();

			LatteShaderCache_drawBackgroundImage(g_shaderCacheLoaderState.textureTVId, 1280, 720);

			const auto progress_font = ImGui_GetFont(window_size.y / 32.0f);
			const auto shader_count_font = ImGui_GetFont(window_size.y / 48.0f);

			ImVec2 position = { window_size.x / 2.0f, window_size.y / 2.0f };
			ImVec2 pivot = { 0.5f, 0.5f };
			ImVec2 progress_size = { io.DisplaySize.x * 0.5f, 0 };
			ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
			ImGui::SetNextWindowSize(progress_size, ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.8f);
			ImGui::PushStyleColor(ImGuiCol_PlotHistogram, textColor);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
			ImGui::PushFont(progress_font);

			std::string titleText = "Shader progress";

			if (ImGui::Begin(titleText.c_str(), nullptr, kPopupFlags))
			{
				const float width = ImGui::GetWindowSize().x / 2.0f;

				std::string text;
				if (isPipelines)
				{
					text = "Loading cached pipelines...";
				}
				else
				{
					if (shaderCacheScreenStats.compiledShaderCount >= 3)
						text = "Compiling cached shaders...";
					else
						text = "Loading cached shaders...";
				}

				ImGui::SetCursorPosX(width - ImGui::CalcTextSize(text.c_str()).x / 2);
				ImGui::Text("%s", text.c_str());

				float percentLoaded;
				if(isPipelines)
					percentLoaded = (float)g_shaderCacheLoaderState.loadedPipelines / (float)g_shaderCacheLoaderState.pipelineFileCount;
				else
					percentLoaded = (float)g_shaderCacheLoaderState.loadedShaderFiles / (float)g_shaderCacheLoaderState.shaderFileCount;
				ImGui::ProgressBar(percentLoaded, { -1, 0 }, "");

				if (isPipelines)
					text = fmt::format("{}/{} ({}%)", g_shaderCacheLoaderState.loadedPipelines, g_shaderCacheLoaderState.pipelineFileCount, (int)(percentLoaded * 100));
				else
					text = fmt::format("{}/{} ({}%)", g_shaderCacheLoaderState.loadedShaderFiles, g_shaderCacheLoaderState.shaderFileCount, (int)(percentLoaded * 100));
				ImGui::SetCursorPosX(width - ImGui::CalcTextSize(text.c_str()).x / 2);
				ImGui::Text("%s", text.c_str());
			}
			ImGui::End();
			ImGui::PopFont();
			ImGui::PopStyleColor(2);

			if (!isPipelines)
			{
				position = { 10, window_size.y - 10 };
				pivot = { 0, 1 };
				ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
				ImGui::SetNextWindowBgAlpha(0.8f);
				ImGui::PushStyleColor(ImGuiCol_WindowBg, 0);
				ImGui::PushFont(shader_count_font);
				if (ImGui::Begin("Shader count", nullptr, kPopupFlags))
				{
					const float offset = shader_count_font->FallbackAdvanceX * 25.f;
					ImGui::Text("Vertex shaders");
					ImGui::SameLine(offset);
					ImGui::Text("%d", shaderCacheScreenStats.vertexShaderCount);

					ImGui::Text("Pixel shaders");
					ImGui::SameLine(offset);
					ImGui::Text("%d", shaderCacheScreenStats.pixelShaderCount);

					ImGui::Text("Geometry shaders");
					ImGui::SameLine(offset);
					ImGui::Text("%d", shaderCacheScreenStats.geometryShaderCount);
				}
				ImGui::End();
				ImGui::PopStyleColor();
				ImGui::PopFont();
			}
			g_renderer->ImguiEnd();
			lastFrameUpdate = tick_cached();
		}

		g_renderer->BeginFrame(false);
		if (g_renderer->ImguiBegin(false))
		{
			LatteShaderCache_drawBackgroundImage(g_shaderCacheLoaderState.textureDRCId, 854, 480);
			g_renderer->ImguiEnd();
		}

		g_renderer->SwapBuffers(true, true);
	}
}

void LatteShaderCache_LoadPipelineCache(uint64 cacheTitleId)
{
	switch(g_renderer->GetType())
	{
	#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
	    g_shaderCacheLoaderState.pipelineFileCount = VulkanPipelineStableCache::GetInstance().BeginLoading(cacheTitleId);
	    break;
	#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		g_shaderCacheLoaderState.pipelineFileCount = MetalPipelineCache::GetInstance().BeginLoading(cacheTitleId);
		break;
#endif
	}

	g_shaderCacheLoaderState.loadedPipelines = 0;
	LatteShaderCache_ShowProgress(LatteShaderCache_updatePipelineLoadingProgress, true);

	switch(g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
	    VulkanPipelineStableCache::GetInstance().EndLoading();
	    break;
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		MetalPipelineCache::GetInstance().EndLoading();
		break;
#endif
	}
}

bool LatteShaderCache_updatePipelineLoadingProgress()
{
	uint32 pipelinesMissingShaders = 0;
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
	    return VulkanPipelineStableCache::GetInstance().UpdateLoading(g_shaderCacheLoaderState.loadedPipelines, pipelinesMissingShaders);
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		return MetalPipelineCache::GetInstance().UpdateLoading(g_shaderCacheLoaderState.loadedPipelines, pipelinesMissingShaders);
#endif
	}

	return false;
}

uint64 LatteShaderCache_getShaderNameInTransferableCache(uint64 baseHash, uint32 shaderType)
{
	baseHash &= ~(7ULL << 61ULL);
	baseHash |= ((uint64)shaderType << 61ULL);
	return baseHash;
}

void LatteShaderCache_writeSeparableVertexShader(uint64 shaderBaseHash, uint64 shaderAuxHash, uint8* fetchShader, uint32 fetchShaderSize, uint8* vertexShader, uint32 vertexShaderSize, uint32* contextRegisters, bool usesGeometryShader)
{
	if (!s_shaderCacheGeneric)
		return;
	MemStreamWriter streamWriter(128 * 1024);
	streamWriter.writeBE<uint8>(1 | (SHADER_CACHE_TYPE_VERTEX << 4));
	streamWriter.writeBE<uint64>(shaderBaseHash);
	streamWriter.writeBE<uint64>(shaderAuxHash);
	streamWriter.writeBE<uint8>(usesGeometryShader ? 1 : 0);
	Latte::GPUCompactedRegisterState compactRegState;
	Latte::StoreGPURegisterState(*(LatteContextRegister*)contextRegisters, compactRegState);
	Latte::SerializeRegisterState(compactRegState, streamWriter);
	Latte::SerializeShaderProgram(fetchShader, fetchShaderSize, streamWriter);
	Latte::SerializeShaderProgram(vertexShader, vertexShaderSize, streamWriter);
	uint64 shaderCacheName = LatteShaderCache_getShaderNameInTransferableCache(shaderBaseHash, SHADER_CACHE_TYPE_VERTEX);
	std::span<uint8> dataBlob = streamWriter.getResult();
	s_shaderCacheGeneric->AddFileAsync({shaderCacheName, shaderAuxHash }, dataBlob.data(), dataBlob.size());
}

void LatteShaderCache_writeSeparableGeometryShader(uint64 shaderBaseHash, uint64 shaderAuxHash, uint8* geometryShader, uint32 geometryShaderSize, uint8* gsCopyShader, uint32 gsCopyShaderSize, uint32* contextRegisters, uint32* hleSpecialState, uint32 vsRingParameterCount)
{
	if (!s_shaderCacheGeneric)
		return;
	MemStreamWriter streamWriter(128 * 1024);
	streamWriter.writeBE<uint8>(1 | (SHADER_CACHE_TYPE_GEOMETRY << 4));
	streamWriter.writeBE<uint64>(shaderBaseHash);
	streamWriter.writeBE<uint64>(shaderAuxHash);
	cemu_assert_debug(vsRingParameterCount < 0x10000);
	streamWriter.writeBE<uint16>(vsRingParameterCount);
	Latte::GPUCompactedRegisterState compactRegState;
	Latte::StoreGPURegisterState(*(LatteContextRegister*)contextRegisters, compactRegState);
	Latte::SerializeRegisterState(compactRegState, streamWriter);
	Latte::SerializeShaderProgram(gsCopyShader, gsCopyShaderSize, streamWriter);
	Latte::SerializeShaderProgram(geometryShader, geometryShaderSize, streamWriter);
	uint64 shaderCacheName = LatteShaderCache_getShaderNameInTransferableCache(shaderBaseHash, SHADER_CACHE_TYPE_GEOMETRY);
	std::span<uint8> dataBlob = streamWriter.getResult();
	s_shaderCacheGeneric->AddFileAsync({shaderCacheName, shaderAuxHash }, dataBlob.data(), dataBlob.size());
}

void LatteShaderCache_writeSeparablePixelShader(uint64 shaderBaseHash, uint64 shaderAuxHash, uint8* pixelShader, uint32 pixelShaderSize, uint32* contextRegisters, bool usesGeometryShader)
{
	if (!s_shaderCacheGeneric)
		return;
	MemStreamWriter streamWriter(128 * 1024);
	streamWriter.writeBE<uint8>(1 | (SHADER_CACHE_TYPE_PIXEL << 4));
	streamWriter.writeBE<uint64>(shaderBaseHash);
	streamWriter.writeBE<uint64>(shaderAuxHash);
	streamWriter.writeBE<uint8>(usesGeometryShader ? 1 : 0);
	Latte::GPUCompactedRegisterState compactRegState;
	Latte::StoreGPURegisterState(*(LatteContextRegister*)contextRegisters, compactRegState);
	Latte::SerializeRegisterState(compactRegState, streamWriter);
	Latte::SerializeShaderProgram(pixelShader, pixelShaderSize, streamWriter);
	uint64 shaderCacheName = LatteShaderCache_getShaderNameInTransferableCache(shaderBaseHash, SHADER_CACHE_TYPE_PIXEL);
	std::span<uint8> dataBlob = streamWriter.getResult();
	s_shaderCacheGeneric->AddFileAsync({shaderCacheName, shaderAuxHash }, dataBlob.data(), dataBlob.size());
}

void LatteShaderCache_loadOrCompileSeparableShader(LatteDecompilerShader* shader, uint64 shaderBaseHash, uint64 shaderAuxHash)
{
	RendererShader::ShaderType shaderType;
	if (shader->shaderType == LatteConst::ShaderType::Vertex)
	{
		shaderType = RendererShader::ShaderType::kVertex;
		shaderCacheScreenStats.vertexShaderCount++;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Geometry)
	{
		shaderType = RendererShader::ShaderType::kGeometry;
		shaderCacheScreenStats.geometryShaderCount++;
	}
	else if (shader->shaderType == LatteConst::ShaderType::Pixel)
	{
		shaderType = RendererShader::ShaderType::kFragment;
		shaderCacheScreenStats.pixelShaderCount++;
	}
	shaderCacheScreenStats.compiledShaderCount++;
	LatteShader_CreateRendererShader(shader, true);
	if (shader->shader == nullptr)
		return;
	LatteShaderCache_addToCompileQueue(shader);
}

bool LatteShaderCache_readSeparableVertexShader(MemStreamReader& streamReader, uint8 version, uint64& currentAuxHashOut)
{
	auto lcr = std::make_unique<LatteContextRegister>();
	if (version != 1)
		return false;
	uint64 shaderBaseHash = streamReader.readBE<uint64>();
	uint64 shaderAuxHash = streamReader.readBE<uint64>();
	bool usesGeometryShader = streamReader.readBE<uint8>() != 0;
	Latte::GPUCompactedRegisterState regState;
	if (!Latte::DeserializeRegisterState(regState, streamReader))
		return false;
	Latte::LoadGPURegisterState(*lcr, regState);
	if (streamReader.hasError())
		return false;
	std::vector<uint8> fetchShaderData;
	if (!Latte::DeserializeShaderProgram(fetchShaderData, streamReader))
		return false;
	if (streamReader.hasError())
		return false;
	std::vector<uint8> vertexShaderData;
	if (!Latte::DeserializeShaderProgram(vertexShaderData, streamReader))
		return false;
	if (streamReader.hasError() || !streamReader.isEndOfStream())
		return false;
	LatteShader_UpdatePSInputs(lcr->GetRawView());
	LatteFetchShader::CacheHash fsHash = LatteFetchShader::CalculateCacheHash((uint32*)fetchShaderData.data(), fetchShaderData.size());
	LatteFetchShader* fetchShader = LatteShaderRecompiler_createFetchShader(fsHash, lcr->GetRawView(), (uint32*)fetchShaderData.data(), fetchShaderData.size());
	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Vertex, usesGeometryShader);
	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompileVertexShader(shaderBaseHash, lcr->GetRawView(), vertexShaderData.data(), vertexShaderData.size(), fetchShader, options, &decompilerOutput);
	LatteDecompilerShader* vertexShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, shaderBaseHash, true, 0, lcr->GetRawView());
	const uint64 currentAuxHash = vertexShader->auxHash;
	currentAuxHashOut = currentAuxHash;
	LatteShaderCache_recordAuxRekey(LatteConst::ShaderType::Vertex, shaderBaseHash, shaderAuxHash, currentAuxHash);
	if (LatteShaderCache_discardIfAlreadyLoaded(vertexShader, shaderBaseHash, currentAuxHash))
		return true;
	LatteShader_DumpShader(shaderBaseHash, currentAuxHash, vertexShader);
	LatteShader_DumpRawShader(shaderBaseHash, currentAuxHash, SHADER_DUMP_TYPE_VERTEX, vertexShaderData.data(), vertexShaderData.size());
	LatteShaderCache_loadOrCompileSeparableShader(vertexShader, shaderBaseHash, currentAuxHash);
	LatteSHRC_RegisterShader(vertexShader, shaderBaseHash, currentAuxHash);
	return true;
}

bool LatteShaderCache_readSeparableGeometryShader(MemStreamReader& streamReader, uint8 version, uint64& currentAuxHashOut)
{
	if (version != 1)
		return false;
	auto lcr = std::make_unique<LatteContextRegister>();
	uint64 shaderBaseHash = streamReader.readBE<uint64>();
	uint64 shaderAuxHash = streamReader.readBE<uint64>();
	uint32 vsRingParameterCount = streamReader.readBE<uint16>();
	Latte::GPUCompactedRegisterState regState;
	if (!Latte::DeserializeRegisterState(regState, streamReader))
		return false;
	Latte::LoadGPURegisterState(*lcr, regState);
	if (streamReader.hasError())
		return false;
	std::vector<uint8> geometryCopyShaderData;
	if (!Latte::DeserializeShaderProgram(geometryCopyShaderData, streamReader))
		return false;
	std::vector<uint8> geometryShaderData;
	if (!Latte::DeserializeShaderProgram(geometryShaderData, streamReader))
		return false;
	if (streamReader.hasError() || !streamReader.isEndOfStream())
		return false;
	LatteShader_UpdatePSInputs(lcr->GetRawView());
	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Geometry, true);
	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompileGeometryShader(shaderBaseHash, lcr->GetRawView(), geometryShaderData.data(), geometryShaderData.size(), geometryCopyShaderData.data(), geometryCopyShaderData.size(), vsRingParameterCount, options, &decompilerOutput);
	LatteDecompilerShader* geometryShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, shaderBaseHash, true, 0, lcr->GetRawView());
	const uint64 currentAuxHash = geometryShader->auxHash;
	currentAuxHashOut = currentAuxHash;
	LatteShaderCache_recordAuxRekey(LatteConst::ShaderType::Geometry, shaderBaseHash, shaderAuxHash, currentAuxHash);
	if (LatteShaderCache_discardIfAlreadyLoaded(geometryShader, shaderBaseHash, currentAuxHash))
		return true;
	LatteShader_DumpShader(shaderBaseHash, currentAuxHash, geometryShader);
	LatteShader_DumpRawShader(shaderBaseHash, currentAuxHash, SHADER_DUMP_TYPE_GEOMETRY, geometryShaderData.data(), geometryShaderData.size());
	LatteShaderCache_loadOrCompileSeparableShader(geometryShader, shaderBaseHash, currentAuxHash);
	LatteSHRC_RegisterShader(geometryShader, shaderBaseHash, currentAuxHash);
	return true;
}

bool LatteShaderCache_readSeparablePixelShader(MemStreamReader& streamReader, uint8 version, uint64& currentAuxHashOut)
{
	if (version != 1)
		return false;
	auto lcr = std::make_unique<LatteContextRegister>();
	uint64 shaderBaseHash = streamReader.readBE<uint64>();
	uint64 shaderAuxHash = streamReader.readBE<uint64>();
	bool usesGeometryShader = streamReader.readBE<uint8>() != 0;
	Latte::GPUCompactedRegisterState regState;
	if (!Latte::DeserializeRegisterState(regState, streamReader))
		return false;
	Latte::LoadGPURegisterState(*lcr, regState);
	if (streamReader.hasError())
		return false;
	std::vector<uint8> pixelShaderData;
	if (!Latte::DeserializeShaderProgram(pixelShaderData, streamReader))
		return false;
	if (streamReader.hasError() || !streamReader.isEndOfStream())
		return false;
	LatteShader_UpdatePSInputs(lcr->GetRawView());
	LatteDecompilerOptions options;
	LatteShader_GetDecompilerOptions(options, LatteConst::ShaderType::Pixel, usesGeometryShader);
	LatteDecompilerOutput_t decompilerOutput{};
	LatteDecompiler_DecompilePixelShader(shaderBaseHash, lcr->GetRawView(), pixelShaderData.data(), pixelShaderData.size(), options, &decompilerOutput);
	LatteDecompilerShader* pixelShader = LatteShader_CreateShaderFromDecompilerOutput(decompilerOutput, shaderBaseHash, true, 0, lcr->GetRawView());
	const uint64 currentAuxHash = pixelShader->auxHash;
	currentAuxHashOut = currentAuxHash;
	LatteShaderCache_recordAuxRekey(LatteConst::ShaderType::Pixel, shaderBaseHash, shaderAuxHash, currentAuxHash);
	if (LatteShaderCache_discardIfAlreadyLoaded(pixelShader, shaderBaseHash, currentAuxHash))
		return true;
	LatteShader_DumpShader(shaderBaseHash, currentAuxHash, pixelShader);
	LatteShader_DumpRawShader(shaderBaseHash, currentAuxHash, SHADER_DUMP_TYPE_PIXEL, pixelShaderData.data(), pixelShaderData.size());
	LatteShaderCache_loadOrCompileSeparableShader(pixelShader, shaderBaseHash, currentAuxHash);
	LatteSHRC_RegisterShader(pixelShader, shaderBaseHash, currentAuxHash);
	return true;
}

bool LatteShaderCache_readSeparableShader(uint8* shaderInfoData, sint32 shaderInfoSize, uint64* currentAuxHashOut)
{
	if (shaderInfoSize < 8)
		return false;
	uint64 dummyAuxHash;
	uint64& currentAuxHash = currentAuxHashOut ? *currentAuxHashOut : dummyAuxHash;
	MemStreamReader streamReader(shaderInfoData, shaderInfoSize);
	uint8 versionAndType = streamReader.readBE<uint8>();
	uint8 version = versionAndType & 0xF;
	uint8 type = (versionAndType >> 4) & 0xF;
	if (type == SHADER_CACHE_TYPE_VERTEX)
		return LatteShaderCache_readSeparableVertexShader(streamReader, version, currentAuxHash);
	else if (type == SHADER_CACHE_TYPE_GEOMETRY)
		return LatteShaderCache_readSeparableGeometryShader(streamReader, version, currentAuxHash);
	else if (type == SHADER_CACHE_TYPE_PIXEL)
		return LatteShaderCache_readSeparablePixelShader(streamReader, version, currentAuxHash);
	return false;
}

void LatteShaderCache_Close()
{
    if(s_shaderCacheGeneric)
    {
        if (s_shaderCacheGeneric->GetDamagedEntryCount() != 0)
            s_guardLoadClean = false;
        delete s_shaderCacheGeneric;
        s_shaderCacheGeneric = nullptr;
    }
	switch(g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
		RendererShaderVk::ShaderCacheLoading_Close();
		break;
#endif
#ifdef ENABLE_OPENGL
	case RendererAPI::OpenGL:
		RendererShaderGL::ShaderCacheLoading_Close();
		break;
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
		RendererShaderMtl::ShaderCacheLoading_Close();
		break;
#endif
	}

	switch(g_renderer->GetType())
	{
#ifdef ENABLE_VULKAN
	case RendererAPI::Vulkan:
	    VulkanPipelineStableCache::GetInstance().Close();
		break;
#endif
#ifdef ENABLE_METAL
	case RendererAPI::Metal:
	    MetalPipelineCache::GetInstance().Close();
		break;
#endif
	}
	ShaderCacheGuard_AfterSession();
}
