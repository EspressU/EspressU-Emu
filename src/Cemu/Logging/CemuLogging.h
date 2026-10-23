#pragma once

extern uint64 s_loggingFlagMask;

enum class LogType : sint32
{
	Force = 63,
	Placeholder = 62,
	APIErrors = 61,

	CoreinitFile = 0,
	GX2 = 1,
	UnsupportedAPI = 2,
	SoundAPI = 4,
	InputAPI = 5,
	Socket = 6,
	Save = 7,
	H264 = 9,
	OpenGLLogging = 10,
	TextureCache = 11,
	VulkanValidation = 12,
	Patches = 14,
	CoreinitMem = 8,
	CoreinitMP = 15,
	CoreinitThread = 16,
	CoreinitLogging = 17,
	CoreinitMemoryMapping = 18,
	CoreinitAlarm = 22,
	CoreinitThreadSync = 3,

	PPC_IPC = 19,
	NN_AOC = 20,
	NN_PDM = 21,
	NN_OLV = 23,
	NN_NFP = 13,
	NN_FP = 24,
	NN_BOSS = 25,
	NN_SL = 26,

	TextureReadback = 29,
	ProcUi = 39,
	nlibcurl = 41,
	SWKBD = 45,

	PRUDP = 40,

	NFC	= 43,
	NTAG = 44,
	Recompiler = 60,
};

template <>
struct fmt::formatter<std::u8string_view> : formatter<string_view> {
	template <typename FormatContext>
	auto format(std::u8string_view v, FormatContext& ctx)
	{
		string_view s((char*)v.data(), v.size());
		return formatter<string_view>::format(s, ctx);
	}
};

void cemuLog_writeLineToLog(std::string_view text, bool date = true, bool new_line = true);
inline void cemuLog_writePlainToLog(std::string_view text) { cemuLog_writeLineToLog(text, false, false); }

void cemuLog_setActiveLoggingFlags(uint64 flagMask);

inline uint64 cemuLog_getFlag(LogType type)
{
	return 1ULL << (uint64)type;
}

inline bool cemuLog_isLoggingEnabled(LogType type)
{
	return (s_loggingFlagMask & cemuLog_getFlag(type)) != 0;
}

bool cemuLog_log(LogType type, std::string_view text);
bool cemuLog_log(LogType type, std::u8string_view text);
void cemuLog_waitForFlush();

template<typename... TArgs>
bool cemuLog_log(LogType type, fmt::format_string<TArgs...> formatStr, TArgs&&... args)
{
	if (!cemuLog_isLoggingEnabled(type))
		return false;

	cemuLog_log(type, fmt::format(formatStr, std::forward<TArgs>(args)...));

	return true;
}

#define cemuLog_logOnce(...) { static bool _not_first_call = false; if (!_not_first_call) { _not_first_call = true; cemuLog_log(__VA_ARGS__); } }

template<typename ... TArgs>
bool cemuLog_logDebug(LogType type, fmt::format_string<TArgs...> format, TArgs&&... args)
{
#ifdef CEMU_DEBUG_ASSERT
	return cemuLog_log(type, format, std::forward<TArgs>(args)...);
#else
	return false;
#endif
}

inline bool cemuLog_logDebug(LogType type, std::string_view message)
{
#ifdef CEMU_DEBUG_ASSERT
	return cemuLog_log(type, message);
#else
	return false;
#endif
}

#define cemuLog_logDebugOnce(...) { static bool _not_first_call = false; if (!_not_first_call) { _not_first_call = true; cemuLog_logDebug(__VA_ARGS__); } }

void cemuLog_logHexDump(LogType type, const void* data, size_t size, size_t lineSize = 16);

bool cemuLog_advancedPPCLoggingEnabled();

uint64 cemuLog_getFlag(LogType type);

fs::path cemuLog_GetLogFilePath();
// Also hands every written block of log text to sink (for example a combined report file); nullptr removes it.
void cemuLog_setTee(void (*sink)(const char* text, size_t length));
void cemuLog_createLogFile(bool triggeredByCrash);
[[nodiscard]] std::unique_lock<std::recursive_mutex> cemuLog_acquire();

class LoggingCallbacks
{
  public:
	virtual void Log(std::string_view filter, std::string_view message) {};
	virtual void Log(std::string_view filter, std::wstring_view message) {};
	virtual ~LoggingCallbacks() = default;
};

void cemuLog_setCallbacks(LoggingCallbacks* loggingCallbacks);
void cemuLog_clearCallbacks();
