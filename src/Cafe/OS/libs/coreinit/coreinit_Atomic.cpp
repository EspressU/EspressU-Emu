#include "Cafe/OS/common/OSCommon.h"
#include <atomic>
#include <cstring>
#include <mutex>
#include "coreinit_Atomic.h"

namespace coreinit
{

	uint32 OSSwapAtomic(std::atomic<uint32be>* mem, uint32 newValue)
	{
		uint32be _newValue = newValue;
		uint32be previousValue = mem->exchange(_newValue);
		return previousValue;
	}

	bool OSCompareAndSwapAtomic(std::atomic<uint32be>* mem, uint32 compareValue, uint32 swapValue)
	{
		uint32be _compareValue = compareValue;
		uint32be _swapValue = swapValue;
		return mem->compare_exchange_strong(_compareValue, _swapValue);
	}

	bool OSCompareAndSwapAtomicEx(std::atomic<uint32be>* mem, uint32 compareValue, uint32 swapValue, uint32be* previousValue)
	{
		uint32be _compareValue = compareValue;
		uint32be _swapValue = swapValue;
		bool r = mem->compare_exchange_strong(_compareValue, _swapValue);
		*previousValue = _compareValue;
		return r;
	}

	uint32 OSAddAtomic(std::atomic<uint32be>* mem, uint32 adder)
	{
		uint32be knownValue;
		while (true)
		{
			knownValue = mem->load();
			uint32be newValue = knownValue + adder;
			if (mem->compare_exchange_strong(knownValue, newValue))
				break;
		}
		return knownValue;
	}

	static std::mutex s_unalignedAtomic64Mutex;

	static inline bool IsAtomic64Aligned(const void* mem)
	{
		return (reinterpret_cast<uintptr_t>(mem) & 7) == 0;
	}

	static inline uint64 LoadUnaligned64(const std::atomic<uint64be>* mem)
	{
		uint64be value;
		std::memcpy(&value, mem, sizeof(value));
		return value;
	}

	static inline void StoreUnaligned64(std::atomic<uint64be>* mem, uint64 value)
	{
		uint64be beValue = value;
		std::memcpy(mem, &beValue, sizeof(beValue));
	}

	uint64 OSSwapAtomic64(std::atomic<uint64be>* mem, uint64 newValue)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			const uint64 previous = LoadUnaligned64(mem);
			StoreUnaligned64(mem, newValue);
			return previous;
		}
		uint64be _newValue = newValue;
		uint64be previousValue = mem->exchange(_newValue);
		return previousValue;
	}

	uint64 OSSetAtomic64(std::atomic<uint64be>* mem, uint64 newValue)
	{
		return OSSwapAtomic64(mem, newValue);
	}

	uint64 OSGetAtomic64(std::atomic<uint64be>* mem)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			return LoadUnaligned64(mem);
		}
		return mem->load();
	}

	uint64 OSAddAtomic64(std::atomic<uint64be>* mem, uint64 adder)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			const uint64 previous = LoadUnaligned64(mem);
			StoreUnaligned64(mem, previous + adder);
			return previous;
		}
		uint64be knownValue;
		while (true)
		{
			knownValue = mem->load();
			uint64be newValue = knownValue + adder;
			if (mem->compare_exchange_strong(knownValue, newValue))
				break;
		}
		return knownValue;
	}

	uint64 OSAndAtomic64(std::atomic<uint64be>* mem, uint64 val)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			const uint64 previous = LoadUnaligned64(mem);
			StoreUnaligned64(mem, previous & val);
			return previous;
		}
		uint64be knownValue;
		while (true)
		{
			knownValue = mem->load();
			uint64be newValue = knownValue & val;
			if (mem->compare_exchange_strong(knownValue, newValue))
				break;
		}
		return knownValue;
	}

	uint64 OSOrAtomic64(std::atomic<uint64be>* mem, uint64 val)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			const uint64 previous = LoadUnaligned64(mem);
			StoreUnaligned64(mem, previous | val);
			return previous;
		}
		uint64be knownValue;
		while (true)
		{
			knownValue = mem->load();
			uint64be newValue = knownValue | val;
			if (mem->compare_exchange_strong(knownValue, newValue))
				break;
		}
		return knownValue;
	}

	bool OSCompareAndSwapAtomic64(std::atomic<uint64be>* mem, uint64 compareValue, uint64 swapValue)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			if (LoadUnaligned64(mem) != compareValue)
				return false;
			StoreUnaligned64(mem, swapValue);
			return true;
		}
		uint64be _compareValue = compareValue;
		uint64be _swapValue = swapValue;
		return mem->compare_exchange_strong(_compareValue, _swapValue);
	}

	bool OSCompareAndSwapAtomicEx64(std::atomic<uint64be>* mem, uint64 compareValue, uint64 swapValue, uint64be* previousValue)
	{
		if (!IsAtomic64Aligned(mem))
		{
			std::lock_guard lock(s_unalignedAtomic64Mutex);
			const uint64 previous = LoadUnaligned64(mem);
			*previousValue = previous;
			if (previous != compareValue)
				return false;
			StoreUnaligned64(mem, swapValue);
			return true;
		}
		uint64be _compareValue = compareValue;
		uint64be _swapValue = swapValue;
		bool r = mem->compare_exchange_strong(_compareValue, _swapValue);
		*previousValue = _compareValue;
		return r;
	}

	void InitializeAtomic()
	{
		cafeExportRegister("coreinit", OSSwapAtomic, LogType::Placeholder);
		cafeExportRegister("coreinit", OSCompareAndSwapAtomic, LogType::Placeholder);
		cafeExportRegister("coreinit", OSCompareAndSwapAtomicEx, LogType::Placeholder);
		cafeExportRegister("coreinit", OSAddAtomic, LogType::Placeholder);

		cafeExportRegister("coreinit", OSSetAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSGetAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSSwapAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSAddAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSAndAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSOrAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSCompareAndSwapAtomic64, LogType::Placeholder);
		cafeExportRegister("coreinit", OSCompareAndSwapAtomicEx64, LogType::Placeholder);
	}
}