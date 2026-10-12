#pragma once

#include <stdlib.h>

#include "version.h"
#include "platform.h"

#define FMT_HEADER_ONLY
#define FMT_USE_GRISU 1
#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/ranges.h>
#if FMT_VERSION > 80000
#include <fmt/xchar.h>
#endif

#define SDL_MAIN_HANDLED

#if defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
#define ARCH_X86_64
#endif

#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cassert>

#if defined(ARCH_X86_64)
#include <immintrin.h>
#endif

#include <string>
#include <string_view>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#include <mutex>
#include <shared_mutex>
#include <thread>
#include <future>
#include <atomic>

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <set>
#include <queue>
#include <stack>
#include <array>
#include <bitset>
#include <bit>

#include <filesystem>
#include <memory>
#include <chrono>
#include <ctime>
#include <regex>
#include <type_traits>
#include <optional>
#include <span>
#include <ranges>
#include <variant>

#include <boost/predef.h>
#include <boost/nowide/convert.hpp>
#include <boost/algorithm/string.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace fs
{
using namespace std::filesystem;

inline std::filesystem::path resolvePathCI(const std::filesystem::path& path)
{
#if !BOOST_OS_IOS
	return path;
#else
	if (std::filesystem::exists(path))
		return path;

	const auto fileName = path.filename();
	auto parentPath = path.parent_path();
	if (parentPath.empty())
		parentPath = ".";
	else if (!std::filesystem::exists(parentPath))
		parentPath = resolvePathCI(parentPath);

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(parentPath, ec))
	{
		if (boost::iequals(entry.path().filename().string(), fileName.string()))
			return entry.path();
	}

	return parentPath / fileName;
#endif
}

inline bool exists(const std::filesystem::path& path)
{
	return std::filesystem::exists(resolvePathCI(path));
}

inline bool exists(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::exists(resolvePathCI(path), ec);
}

inline bool exists(std::filesystem::file_status status)
{
	return std::filesystem::exists(status);
}

inline bool is_directory(const std::filesystem::path& path)
{
	return std::filesystem::is_directory(resolvePathCI(path));
}

inline bool is_directory(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::is_directory(resolvePathCI(path), ec);
}

inline bool is_directory(std::filesystem::file_status status)
{
	return std::filesystem::is_directory(status);
}

inline bool is_regular_file(const std::filesystem::path& path)
{
	return std::filesystem::is_regular_file(resolvePathCI(path));
}

inline bool is_regular_file(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::is_regular_file(resolvePathCI(path), ec);
}

inline bool is_regular_file(std::filesystem::file_status status)
{
	return std::filesystem::is_regular_file(status);
}

inline bool is_empty(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::is_empty(resolvePathCI(path), ec);
}

inline bool is_empty(const std::filesystem::path& path)
{
	return std::filesystem::is_empty(resolvePathCI(path));
}

inline bool is_symlink(const std::filesystem::path& path)
{
	return std::filesystem::is_symlink(resolvePathCI(path));
}

inline bool is_symlink(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::is_symlink(resolvePathCI(path), ec);
}

inline bool is_symlink(std::filesystem::file_status status)
{
	return std::filesystem::is_symlink(status);
}

inline std::filesystem::file_status status(const std::filesystem::path& path)
{
	return std::filesystem::status(resolvePathCI(path));
}

inline std::filesystem::file_status status(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::status(resolvePathCI(path), ec);
}

inline std::filesystem::file_status symlink_status(const std::filesystem::path& path)
{
	return std::filesystem::symlink_status(resolvePathCI(path));
}

inline std::filesystem::file_status symlink_status(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::symlink_status(resolvePathCI(path), ec);
}

inline std::uintmax_t file_size(const std::filesystem::path& path)
{
	return std::filesystem::file_size(resolvePathCI(path));
}

inline std::uintmax_t file_size(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::file_size(resolvePathCI(path), ec);
}

inline bool create_directory(const std::filesystem::path& path)
{
	return std::filesystem::create_directory(resolvePathCI(path));
}

inline bool create_directory(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::create_directory(resolvePathCI(path), ec);
}

inline bool create_directories(const std::filesystem::path& path)
{
	return std::filesystem::create_directories(resolvePathCI(path));
}

inline bool create_directories(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::create_directories(resolvePathCI(path), ec);
}

inline bool remove(const std::filesystem::path& path)
{
	return std::filesystem::remove(resolvePathCI(path));
}

inline bool remove(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::remove(resolvePathCI(path), ec);
}

inline std::uintmax_t remove_all(const std::filesystem::path& path)
{
	return std::filesystem::remove_all(resolvePathCI(path));
}

inline std::uintmax_t remove_all(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::remove_all(resolvePathCI(path), ec);
}

inline void rename(const std::filesystem::path& from, const std::filesystem::path& to)
{
	std::filesystem::rename(resolvePathCI(from), resolvePathCI(to));
}

inline void rename(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec)
{
	std::filesystem::rename(resolvePathCI(from), resolvePathCI(to), ec);
}

inline std::filesystem::space_info space(const std::filesystem::path& path)
{
	return std::filesystem::space(resolvePathCI(path));
}

inline std::filesystem::space_info space(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::space(resolvePathCI(path), ec);
}

inline std::filesystem::path read_symlink(const std::filesystem::path& path)
{
	return std::filesystem::read_symlink(resolvePathCI(path));
}

inline std::filesystem::path read_symlink(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::read_symlink(resolvePathCI(path), ec);
}

inline bool equivalent(const std::filesystem::path& left, const std::filesystem::path& right, std::error_code& ec)
{
	return std::filesystem::equivalent(resolvePathCI(left), resolvePathCI(right), ec);
}

inline std::filesystem::path weakly_canonical(const std::filesystem::path& path, std::error_code& ec)
{
	return std::filesystem::weakly_canonical(resolvePathCI(path), ec);
}

inline std::filesystem::path relative(const std::filesystem::path& path, const std::filesystem::path& base)
{
	return std::filesystem::relative(resolvePathCI(path), resolvePathCI(base));
}

inline std::filesystem::path relative(const std::filesystem::path& path, const std::filesystem::path& base, std::error_code& ec)
{
	return std::filesystem::relative(resolvePathCI(path), resolvePathCI(base), ec);
}

inline void permissions(const std::filesystem::path& path, std::filesystem::perms permissions,
	std::filesystem::perm_options options = std::filesystem::perm_options::replace)
{
	std::filesystem::permissions(resolvePathCI(path), permissions, options);
}

inline void permissions(const std::filesystem::path& path, std::filesystem::perms permissions,
	std::filesystem::perm_options options, std::error_code& ec)
{
	std::filesystem::permissions(resolvePathCI(path), permissions, options, ec);
}

inline bool copy_file(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec)
{
	return std::filesystem::copy_file(resolvePathCI(from), resolvePathCI(to), ec);
}

inline bool copy_file(const std::filesystem::path& from, const std::filesystem::path& to,
	std::filesystem::copy_options options, std::error_code& ec)
{
	return std::filesystem::copy_file(resolvePathCI(from), resolvePathCI(to), options, ec);
}

class directory_iterator : public std::filesystem::directory_iterator
{
using base = std::filesystem::directory_iterator;

public:
	using base::base;
	directory_iterator() = default;
	explicit directory_iterator(const std::filesystem::path& path) : base(resolvePathCI(path)) {}
	directory_iterator(const std::filesystem::path& path, std::error_code& ec) : base(resolvePathCI(path), ec) {}
};

class recursive_directory_iterator : public std::filesystem::recursive_directory_iterator
{
using base = std::filesystem::recursive_directory_iterator;

public:
	using base::base;
	recursive_directory_iterator() = default;
	explicit recursive_directory_iterator(const std::filesystem::path& path) : base(resolvePathCI(path)) {}
	recursive_directory_iterator(const std::filesystem::path& path, std::error_code& ec) : base(resolvePathCI(path), ec) {}
};
}

#include "enumFlags.h"

using uint64 = uint64_t;
using uint32 = uint32_t;
using uint16 = uint16_t;
using uint8 = uint8_t;

using sint64 = int64_t;
using sint32 = int32_t;
using sint16 = int16_t;
using sint8 = int8_t;

#if _MSC_VER
#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#endif
#endif

#include "betype.h"

using uint64le = uint64_t;
using uint32le = uint32_t;
using uint16le = uint16_t;
using uint8le = uint8_t;

#include "Cemu/Logging/CemuDebugLogging.h"
#include "Cemu/Logging/CemuLogging.h"

namespace
{
	std::function<std::string(std::string_view)> g_translate;
}

inline void SetTranslationCallback(std::function<std::string(std::string_view)> translate)
{
	g_translate = translate;
}

#define TR_NOOP(str) str

inline std::string _tr(std::string_view text)
{
	if (g_translate)
		return g_translate(text);

	return std::string{text};
}

template<typename... TArgs>
inline std::string _tr(fmt::format_string<TArgs...> text, TArgs... args)
{
	if (g_translate)
	{
		std::string_view textSV{text.get().data(), text.get().size()};
		return fmt::format(fmt::runtime(g_translate(textSV)), std::forward<TArgs>(args)...);
	}

	return fmt::format(text, std::forward<TArgs>(args)...);
}

#if _MSC_VER
inline uint64 _swapEndianU64(uint64 v)
{
	return _byteswap_uint64(v);
}

inline uint32 _swapEndianU32(uint32 v)
{
	return _byteswap_ulong(v);
}

inline sint32 _swapEndianS32(sint32 v)
{
	return (sint32)_byteswap_ulong((uint32)v);
}

inline uint16 _swapEndianU16(uint16 v)
{
	return (v >> 8) | (v << 8);
}

inline sint16 _swapEndianS16(sint16 v)
{
	return (sint16)(((uint16)v >> 8) | ((uint16)v << 8));
}
#else
inline uint64 _swapEndianU64(uint64 v)
{
#if BOOST_OS_MACOS || BOOST_OS_IOS
    return OSSwapInt64(v);
#elif BOOST_OS_BSD
#ifdef __OpenBSD__
    return swap64(v);
#else
    return bswap64(v);
#endif
#else
    return bswap_64(v);
#endif
}

inline uint32 _swapEndianU32(uint32 v)
{
#if BOOST_OS_MACOS || BOOST_OS_IOS
    return OSSwapInt32(v);
#elif BOOST_OS_BSD
#ifdef __OpenBSD__
    return swap32(v);
#else
    return bswap32(v);
#endif
#else
    return bswap_32(v);
#endif
}

inline sint32 _swapEndianS32(sint32 v)
{
#if BOOST_OS_MACOS || BOOST_OS_IOS
    return (sint32)OSSwapInt32((uint32)v);
#elif BOOST_OS_BSD
#ifdef __OpenBSD__
    return (sint32)swap32((uint32)v);
#else
    return (sint32)bswap32((uint32)v);
#endif
#else
    return (sint32)bswap_32((uint32)v);
#endif
}

inline uint16 _swapEndianU16(uint16 v)
{
    return (v >> 8) | (v << 8);
}

inline sint16 _swapEndianS16(sint16 v)
{
    return (sint16)(((uint16)v >> 8) | ((uint16)v << 8));
}

inline uint64 _umul128(uint64 multiplier, uint64 multiplicand, uint64 *highProduct) {
    unsigned __int128 x = (unsigned __int128)multiplier * (unsigned __int128)multiplicand;
    *highProduct = (x >> 64);
    return x & 0xFFFFFFFFFFFFFFFF;
}

typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef int64_t LONGLONG;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        LONG  HighPart;
    };
    struct {
        DWORD LowPart;
        LONG  HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

#define DEFINE_ENUM_FLAG_OPERATORS(T)                                                                                                                                            \
    inline T operator~ (T a) { return static_cast<T>( ~static_cast<std::underlying_type<T>::type>(a) ); }                                                                       \
    inline T operator| (T a, T b) { return static_cast<T>( static_cast<std::underlying_type<T>::type>(a) | static_cast<std::underlying_type<T>::type>(b) ); }                   \
    inline T operator& (T a, T b) { return static_cast<T>( static_cast<std::underlying_type<T>::type>(a) & static_cast<std::underlying_type<T>::type>(b) ); }                   \
    inline T operator^ (T a, T b) { return static_cast<T>( static_cast<std::underlying_type<T>::type>(a) ^ static_cast<std::underlying_type<T>::type>(b) ); }                   \
    inline T& operator|= (T& a, T b) { return reinterpret_cast<T&>( reinterpret_cast<std::underlying_type<T>::type&>(a) |= static_cast<std::underlying_type<T>::type>(b) ); }   \
    inline T& operator&= (T& a, T b) { return reinterpret_cast<T&>( reinterpret_cast<std::underlying_type<T>::type&>(a) &= static_cast<std::underlying_type<T>::type>(b) ); }   \
    inline T& operator^= (T& a, T b) { return reinterpret_cast<T&>( reinterpret_cast<std::underlying_type<T>::type&>(a) ^= static_cast<std::underlying_type<T>::type>(b) ); }
#endif

template<typename T>
inline T GetBits(T value, uint32 index, uint32 numBits)
{
	T mask = (1<<numBits)-1;
	return (value>>index) & mask;
}

template<typename T>
inline void SetBits(T& value, uint32 index, uint32 numBits, uint32 bitValue)
{
	T mask = (1<<numBits)-1;
	value &= ~(mask << index);
	value |= (bitValue << index);
}

#if !defined(_MSC_VER) || defined(__clang__)
inline uint64 _udiv128(uint64 highDividend, uint64 lowDividend, uint64 divisor, uint64 *remainder)
{
    unsigned __int128 dividend = (((unsigned __int128)highDividend) << 64) | ((unsigned __int128)lowDividend);
    *remainder = (uint64)((dividend % divisor) & 0xFFFFFFFFFFFFFFFF);
    return       (uint64)((dividend / divisor) & 0xFFFFFFFFFFFFFFFF);
}
#endif

#if defined(_MSC_VER)
    #define UNREACHABLE __assume(false)
	#define ASSUME(__cond) __assume(__cond)
	#define TLS_WORKAROUND_NOINLINE
#elif defined(__GNUC__) && !defined(__llvm__)
    #define UNREACHABLE __builtin_unreachable()
	#define ASSUME(__cond) __attribute__((assume(__cond)))
	#define TLS_WORKAROUND_NOINLINE __attribute__((noinline))
#elif defined(__clang__)
	#define UNREACHABLE __builtin_unreachable()
	#define ASSUME(__cond) __builtin_assume(__cond)
	#define TLS_WORKAROUND_NOINLINE __attribute__((noinline))
#else
    #error Unknown compiler
#endif

#if defined(_MSC_VER)
    #define DEBUG_BREAK __debugbreak()
#elif BOOST_OS_IOS
    #include <csignal>
    extern "C" void cemu_ios_assert_hit(void);
    #define DEBUG_BREAK cemu_ios_assert_hit()
#else
    #include <csignal>
    #define DEBUG_BREAK raise(SIGTRAP)
#endif

#if defined(_MSC_VER)
    #define DLLEXPORT __declspec(dllexport)
#elif defined(__GNUC__)
    #if BOOST_OS_WINDOWS
        #define DLLEXPORT __attribute__((dllexport))
    #else
        #define DLLEXPORT
    #endif
#else
    #error No definition for DLLEXPORT
#endif

#if BOOST_OS_WINDOWS
	#define NOEXPORT
#elif defined(__GNUC__)
	#define NOEXPORT __attribute__ ((visibility ("hidden")))
#endif

#if defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define FORCE_INLINE inline __attribute__((always_inline))
#else
#define FORCE_INLINE inline
#endif

FORCE_INLINE int BSF(uint32 v)
{
#if defined(_MSC_VER)
	return _tzcnt_u32(v);
#elif defined(__GNUC__) || defined(__clang__)
	return __builtin_ctz(v);
#else
	return std::countr_zero(v);
#endif
}

#if defined(__aarch64__)

inline void _mm_pause()
{
    asm volatile("yield");
}

inline uint64 __rdtsc()
{
    uint64 t;
    asm volatile("mrs %0, cntvct_el0" : "=r" (t));
    return t;
}

inline void _mm_mfence()
{
	asm volatile("" ::: "memory");
	std::atomic_thread_fence(std::memory_order_seq_cst);
}

inline unsigned char _addcarry_u64(unsigned char carry, unsigned long long a, unsigned long long b, unsigned long long *result)
{
    *result = a + b + (unsigned long long)carry;
    if (*result < a)
        return 1;
    return 0;
}

#endif

inline void cemu_assert(bool _condition)
{
    if ((_condition) == false)
    {
        DEBUG_BREAK;
    }
}

#ifndef CEMU_DEBUG_ASSERT

inline void cemu_assert_debug(bool _condition)
{
}

inline void cemu_assert_unimplemented()
{
}

inline void cemu_assert_suspicious()
{
}

inline void cemu_assert_error()
{
	DEBUG_BREAK;
}
#else
inline void cemu_assert_debug(bool _condition)
{
    if ((_condition) == false)
        DEBUG_BREAK;
}

inline void cemu_assert_unimplemented()
{
    DEBUG_BREAK;
}

inline void cemu_assert_suspicious()
{
    DEBUG_BREAK;
}

inline void cemu_assert_error()
{
    DEBUG_BREAK;
}
#endif

#define assert_dbg() DEBUG_BREAK

#include "Common/MemPtr.h"

template <typename T1, typename T2>
constexpr bool HAS_FLAG(T1 flags, T2 test_flag) { return (flags & (T1)test_flag) == (T1)test_flag; }
template <typename T1, typename T2>
constexpr bool HAS_BIT(T1 value, T2 index) { return (value & ((T1)1 << index)) != 0; }

template <typename T>
constexpr uint32_t ppcsizeof() { return (uint32_t) sizeof(T); }

template <typename T>
void vectorAppendUnique(std::vector<T>& vec, const T& val)
{
    if (std::find(vec.begin(), vec.end(), val) != vec.end())
        return;
    vec.emplace_back(val);
}

template <typename T>
void vectorRemoveByValue(std::vector<T>& vec, const T& val)
{
    vec.erase(std::remove(vec.begin(), vec.end(), val), vec.end());
}

template <typename T>
void vectorRemoveByIndex(std::vector<T>& vec, const size_t index)
{
    vec.erase(vec.begin() + index);
}

template<typename T1, typename... Types>
bool match_any_of(T1&& value, Types&&... others)
{
    return ((value == others) || ...);
}

[[nodiscard]] static std::chrono::high_resolution_clock::time_point now_cached() noexcept
{
#ifdef _WIN32
	static const long long _Freq = _Query_perf_frequency();
	const long long _Ctr = _Query_perf_counter();
	static_assert(std::nano::num == 1, "This assumes period::num == 1.");
	const long long _Whole = (_Ctr / _Freq) * std::nano::den;
	const long long _Part = (_Ctr % _Freq) * std::nano::den / _Freq;
	return (std::chrono::high_resolution_clock::time_point(std::chrono::nanoseconds(_Whole + _Part)));
#else
    return std::chrono::high_resolution_clock::now();
#endif
}

[[nodiscard]] static std::chrono::steady_clock::time_point tick_cached() noexcept
{
#if BOOST_OS_WINDOWS
	static const long long _Freq = _Query_perf_frequency();
	const long long _Ctr = _Query_perf_counter();
	static_assert(std::nano::num == 1, "This assumes period::num == 1.");
	const long long _Whole = (_Ctr / _Freq) * std::nano::den;
	const long long _Part = (_Ctr % _Freq) * std::nano::den / _Freq;
	return (std::chrono::steady_clock::time_point(std::chrono::nanoseconds(_Whole + _Part)));
#elif BOOST_OS_LINUX
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC_RAW, &tp);
	return std::chrono::steady_clock::time_point(
		std::chrono::seconds(tp.tv_sec) + std::chrono::nanoseconds(tp.tv_nsec));
#elif BOOST_OS_MACOS || BOOST_OS_IOS
	return std::chrono::steady_clock::time_point(
		std::chrono::nanoseconds(clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW)));
#elif BOOST_OS_BSD
	struct timespec tp;
	clock_gettime(CLOCK_MONOTONIC, &tp);
	return std::chrono::steady_clock::time_point(
		std::chrono::seconds(tp.tv_sec) + std::chrono::nanoseconds(tp.tv_nsec));
#endif
}

inline const char* _utf8WrapperPtr(const char8_t* input)
{
    return (const char*)input;
}

inline std::string_view _utf8Wrapper(std::u8string_view input)
{
    std::basic_string_view<char> v((char*)input.data(), input.size());
    return v;
}

inline std::string _pathToUtf8(const fs::path& path)
{
    std::u8string strU8 = path.generic_u8string();
    std::string v((const char*)strU8.data(), strU8.size());
    return v;
}

inline fs::path _utf8ToPath(std::string_view input)
{
    std::basic_string_view<char8_t> v((char8_t*)input.data(), input.size());
    return fs::path(v);
}

inline char _ansiToLower(char c)
{
	if (c >= 'A' && c <= 'Z')
		c -= ('A' - 'a');
	return c;
}

class RunAtCemuBoot
{
public:
    RunAtCemuBoot(void(*f)())
    {
        f();
    }
};

template<typename T>
bool future_is_ready(std::future<T>& f)
{
#if defined(__GNUC__)
    return f.wait_for(std::chrono::nanoseconds(0)) == std::future_status::ready;
#else
	return f._Is_ready();
#endif
}

template<typename T>
std::atomic<T>* _rawPtrToAtomic(T* ptr)
{
	static_assert(sizeof(T) == sizeof(std::atomic<T>));
	cemu_assert_debug((reinterpret_cast<std::uintptr_t>(ptr) % alignof(std::atomic<T>)) == 0);
    return reinterpret_cast<std::atomic<T>*>(ptr);
}

#if defined(__GNUC__)
#define ATTR_MS_ABI __attribute__((ms_abi))
#else
#define ATTR_MS_ABI
#endif

#if !defined(TRUE) or !defined(FALSE)
#define TRUE (1)
#define FALSE (0)
#endif

inline uint32 GetTitleIdHigh(uint64 titleId)
{
	return titleId >> 32;
}

inline uint32 GetTitleIdLow(uint64 titleId)
{
	return titleId & 0xFFFFFFFF;
}

#if defined(__GNUC__)
#define memcpy_dwords(__dest, __src, __numDwords) memcpy((__dest), (__src), (__numDwords) * sizeof(uint32))
#define memcpy_qwords(__dest, __src, __numQwords) memcpy((__dest), (__src), (__numQwords) * sizeof(uint64))
#else
#define memcpy_dwords(__dest, __src, __numDwords) __movsd((unsigned long*)(__dest), (const unsigned long*)(__src), __numDwords)
#define memcpy_qwords(__dest, __src, __numQwords) __movsq((unsigned long long*)(__dest), (const unsigned long long*)(__src), __numQwords)
#endif

#include "Cafe/HW/MMU/MMU.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Espresso/PPCCallback.h"

void DebugLogStackTrace(struct OSThread_t* thread, MPTR sp);

template <typename Enum>
	requires std::is_enum_v<Enum>
struct fmt::formatter<Enum> : fmt::formatter<underlying_t<Enum>>
{
	auto format(const Enum& e, format_context& ctx) const
	{

		return formatter<underlying_t<Enum>>::format(fmt::underlying(e), ctx);
	}
};

template <typename T>
struct fmt::formatter<betype<T>> : fmt::formatter<T>
{
	auto format(const betype<T>& e, format_context& ctx) const
	{
		return formatter<T>::format(static_cast<T>(e), ctx);
	}
};

namespace stdx
{
    template <typename EnumT>
		requires (std::is_enum_v<EnumT>)
        constexpr std::underlying_type_t<EnumT> to_underlying(EnumT e) noexcept {
        return static_cast<std::underlying_type_t<EnumT>>(e);
    };

	template <typename Fn>
	class scope_exit
	{
		Fn m_func;
		bool m_released = false;
	public:
		explicit scope_exit(Fn&& f) noexcept
			: m_func(std::forward<Fn>(f))
		{}
		~scope_exit()
		{
			if (!m_released) m_func();
		}
		scope_exit(scope_exit&& other) noexcept
			: m_func(std::move(other.m_func)), m_released(std::exchange(other.m_released, true))
		{}
		scope_exit(const scope_exit&) = delete;
		scope_exit& operator=(scope_exit) = delete;
		void release() { m_released = true;}
	};

#ifdef __cpp_lib_atomic_ref
	#include <atomic>
	template<typename T>
	using atomic_ref = std::atomic_ref<T>;
#else
	template<typename T>
	class atomic_ref
	{
		static_assert(std::is_trivially_copyable_v<T>, "atomic_ref requires trivially copyable types");
	public:
		using value_type = T;

		explicit atomic_ref(T& obj) noexcept : ptr_(std::addressof(obj)) {}

		T load(std::memory_order order = std::memory_order_seq_cst) const noexcept
		{
			auto aptr = reinterpret_cast<std::atomic<T>*>(ptr_);
			return aptr->load(order);
		}

		void store(T desired, std::memory_order order = std::memory_order_seq_cst) const noexcept
		{
			auto aptr = reinterpret_cast<std::atomic<T>*>(ptr_);
			aptr->store(desired, order);
		}

	private:
		T* ptr_;
	};
#endif
}
