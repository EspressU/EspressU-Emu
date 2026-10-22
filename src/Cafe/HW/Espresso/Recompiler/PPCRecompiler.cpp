#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"
#include "PPCFunctionBoundaryTracker.h"
#include "PPCRecompiler.h"
#include "PPCRecompilerIml.h"
#include "Common/DeviceCapabilities.h"
#include "PPCRecompilerThreadPool.h"
#include "Cafe/OS/RPL/rpl.h"
#include "util/containers/RangeStore.h"
#include "Cafe/OS/libs/coreinit/coreinit_CodeGen.h"
#include "Cafe/OS/libs/coreinit/coreinit_Thread.h"
#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "Common/cpu_features.h"
#include "util/helpers/fspinlock.h"
#include "util/helpers/helpers.h"
#include "util/MemMapper/MemMapper.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#if defined(__APPLE__) && defined(__aarch64__)
#include <pthread.h>
#include <signal.h>
#include <atomic>
#include <libkern/OSCacheControl.h>
#if BOOST_OS_IOS
#include <os/proc.h>
#endif
#include <stdio.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <sys/sysctl.h>
#endif
#endif

#include "IML/IML.h"

#include <map>
#include <mutex>
#include <limits>
#include <string>
#include <vector>
#include "IML/IMLRegisterAllocator.h"
#include "BackendX64/BackendX64.h"
#ifdef __aarch64__
#include "BackendAArch64/BackendAArch64.h"
#endif
#include "util/highresolutiontimer/HighResolutionTimer.h"

#ifndef PPCREC_FORCE_SYNCHRONOUS_COMPILATION
#if defined(__aarch64__)
#define PPCREC_FORCE_SYNCHRONOUS_COMPILATION    1
#else
#define PPCREC_FORCE_SYNCHRONOUS_COMPILATION    0
#endif
#endif
#define PPCREC_LOG_RECOMPILATION_RESULTS        0

static bool s_dualMapJITEnabled = false;
static PPCRecompilerThreadPool s_threadPool;

bool PPCRecompiler_isDualMapJITEnabled()
{
    return s_dualMapJITEnabled;
}

#if defined(__APPLE__)
extern "C" int csops(pid_t pid, int ops, void* useraddr, size_t usersize);

constexpr uint32_t CS_DEBUGGED = 0x10000000;

bool checkDebugged() {
    int flags = 0;

    int result = csops(getpid(), 0, &flags, sizeof(flags));
    return (result == 0) && ((flags & CS_DEBUGGED) != 0);
}
#else
bool checkDebugged() {
    return true;
}
#endif

#if defined(__APPLE__) && defined(__aarch64__)

#include <fcntl.h>
#include <mach/mach.h>

extern "C" {

__attribute__((noinline, optnone, naked))
void* BreakGetJITMapping(void *addr, size_t len) {
    asm("mov x16, #1 \n"
        "brk #0xf00d \n"
        "ret");
}

__attribute__((noinline,optnone,naked))
void JIT26Detach(void) {
    asm("mov x16, #0 \n"
        "brk #0xf00d \n"
        "ret");
}

__attribute__((noinline,optnone,naked))
void* BreakMarkJITMapping(size_t bytes) {
    asm("brk #0x69 \n"
        "ret");
}

}

static int PPCRecompiler_readBoolEnvVar(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return -1;
    }

    if (strcmp(value, "1") == 0)
    {
        return 1;
    }

    if (strcmp(value, "0") == 0)
    {
        return 0;
    }

    return -1;
}

static int PPCRecompiler_getAppleOsMajorVersion()
{
#if BOOST_OS_IOS
    char versionBuffer[32] = {};
    size_t versionLength = sizeof(versionBuffer);

    if (sysctlbyname("kern.osproductversion", versionBuffer, &versionLength, nullptr, 0) != 0)
    {
        return 0;
    }

    versionBuffer[sizeof(versionBuffer) - 1] = '\0';
    return atoi(versionBuffer);
#else
    return 0;
#endif
}

static bool PPCRecompiler_shouldUseDualMapByDefault()
{
#if BOOST_OS_IOS
    return PPCRecompiler_getAppleOsMajorVersion() >= 26;
#else
    return false;
#endif
}

bool PPCRecompiler_readTXMEnvVar()
{
    if (!s_dualMapJITEnabled) { return false; }

    return PPCRecompiler_readBoolEnvVar("HAS_TXM") == 1;
}

// The TXM handshake is a brk that the debugger has to answer. When the debugger is attached but does not serve it
// yet (the JIT helper is still starting, or LiveContainer's debugger does not speak the protocol), the brk comes
// back as SIGTRAP and would kill the app. The handler turns that into "no mapping" so the start can retry or fall
// back to the interpreter.
static std::atomic<bool> s_jitBreakServed{false};
static std::atomic<bool> s_jitBreakGaveUp{false};
static volatile sig_atomic_t s_jitBreakUnanswered = 0;
static struct sigaction s_jitBreakPrevAction;

static void PPCRecompiler_jitBreakHandler(int sig, siginfo_t* info, void* context)
{
    auto* uc = (ucontext_t*)context;
    const uint64_t pc = __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    const uint64_t fn = (uint64_t)(uintptr_t)&BreakGetJITMapping;
    if (pc >= fn && pc < fn + 12)
    {
        s_jitBreakUnanswered = 1;
        uc->uc_mcontext->__ss.__x[0] = 0;
        __darwin_arm_thread_state64_set_pc_fptr(uc->uc_mcontext->__ss, (void*)(uintptr_t)(pc + 4));
        return;
    }
    if ((s_jitBreakPrevAction.sa_flags & SA_SIGINFO) && s_jitBreakPrevAction.sa_sigaction)
    {
        s_jitBreakPrevAction.sa_sigaction(sig, info, context);
        return;
    }
    signal(sig, SIG_DFL);
}

static void* PPCRecompiler_requestJitMapping(size_t size)
{
    if (s_jitBreakGaveUp.load())
        return nullptr;

    constexpr int kAttempts = 10;
    for (int attempt = 1; attempt <= kAttempts; attempt++)
    {
        struct sigaction act = {};
        act.sa_sigaction = PPCRecompiler_jitBreakHandler;
        act.sa_flags = SA_SIGINFO;
        sigemptyset(&act.sa_mask);

        s_jitBreakUnanswered = 0;
        sigaction(SIGTRAP, &act, &s_jitBreakPrevAction);
        void* mapping = BreakGetJITMapping(nullptr, size);
        sigaction(SIGTRAP, &s_jitBreakPrevAction, nullptr);

        if (!s_jitBreakUnanswered)
        {
            s_jitBreakServed = true;
            return mapping;
        }
        cemuLog_log(LogType::Force, "JIT: the debugger did not answer the memory request (try {} of {}), waiting", attempt, kAttempts);
        if (attempt < kAttempts)
            sleep(1);
    }

    s_jitBreakGaveUp = true;
    cemuLog_log(LogType::Force, "JIT: the debugger never answered, so the JIT cannot start. Running the interpreter instead.");
    return nullptr;
}

static void PPCRecompiler_finishJitMappingSession()
{
    static std::atomic<bool> s_txmDetached{false};
    if (PPCRecompiler_readTXMEnvVar() && s_jitBreakServed.load() && !s_txmDetached.exchange(true))
    {
        cemuLog_log(LogType::Force, "Recompiler: detaching TXM debugger after JIT mapping");
        JIT26Detach();
    }
}

DualMapRegion PPCRecompiler_allocateDualMap(size_t size)
{
    DualMapRegion r;
    r.size = size;

    void* rxPtr = nullptr;

    if (PPCRecompiler_readTXMEnvVar())
    {
        rxPtr = PPCRecompiler_requestJitMapping(size);
        if (s_jitBreakGaveUp.load())
            return r;
    }

    if (!rxPtr)
        rxPtr = mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);

    if (!rxPtr || rxPtr == MAP_FAILED)
        return r;

    vm_address_t rwAddr = 0;
    vm_prot_t curProt = VM_PROT_NONE, maxProt = VM_PROT_NONE;

    kern_return_t kr = vm_remap(
        mach_task_self(), &rwAddr, (vm_size_t)size, 0, VM_FLAGS_ANYWHERE,
        mach_task_self(), (vm_address_t)rxPtr, FALSE,
        &curProt, &maxProt, VM_INHERIT_NONE);

    cemuLog_log(LogType::Force, "[DualMap] vm_remap: kr={} rwAddr={:p} curProt={} maxProt={}",
        kr, (void*)rwAddr, curProt, maxProt);
    if (kr != KERN_SUCCESS)
    {
        munmap(rxPtr, size);
        return r;
    }

    kr = vm_protect(mach_task_self(), rwAddr, (vm_size_t)size, FALSE, VM_PROT_READ | VM_PROT_WRITE);
    cemuLog_log(LogType::Force, "[DualMap] vm_protect(RW): kr={}", kr);
    if (kr != KERN_SUCCESS)
    {
        vm_deallocate(mach_task_self(), rwAddr, (vm_size_t)size);
        munmap(rxPtr, size);
        return r;
    }

    r.rxAlias = rxPtr;
    r.rwAlias = (void*)rwAddr;

    return r;
}

#endif

#if !defined(__APPLE__) || !defined(__aarch64__)

static int PPCRecompiler_readBoolEnvVar(const char* name)
{
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0')
    {
        return -1;
    }

    if (strcmp(value, "1") == 0)
    {
        return 1;
    }

    if (strcmp(value, "0") == 0)
    {
        return 0;
    }

    return -1;
}

static bool PPCRecompiler_shouldUseDualMapByDefault()
{
    return false;
}

DualMapRegion PPCRecompiler_allocateDualMap(size_t size)
{
    (void)size;
    return {};
}

#endif

void PPCRecompiler_flushInstructionCache(void* codePtr, size_t codeSize)
{
#if defined(_WIN32)
    FlushInstructionCache(GetCurrentProcess(), codePtr, codeSize);
#elif defined(__APPLE__) && defined(__aarch64__)
    sys_icache_invalidate(codePtr, codeSize);
#elif !defined(ARCH_X86_64)
    __builtin___clear_cache((char*)codePtr, (char*)codePtr + codeSize);
#else
    (void)codePtr;
    (void)codeSize;
#endif
}

struct DualMapArena
{
    DualMapRegion region;
    std::mutex mutex;
    std::map<size_t, size_t> freeRanges;

    bool init(size_t size)
    {
        region = PPCRecompiler_allocateDualMap(size);
        if (!region.rwAlias || !region.rxAlias)
        {
            cemuLog_log(LogType::Force, "JIT arena: allocation failed (rw={:p} rx={:p} size={}MB)",
                region.rwAlias, region.rxAlias, size / 1024 / 1024);
            region = {};
            return false;
        }
        reset();
        cemuLog_log(LogType::Force, "JIT arena: rw={:p} rx={:p} size={}MB",
            region.rwAlias, region.rxAlias, size / 1024 / 1024);
        return true;
    }

    DualMapRegion alloc(size_t size)
    {
        if (!size || size > std::numeric_limits<size_t>::max() - 15)
            return {};
        size = (size + 15) & ~size_t(15);
        std::lock_guard lock(mutex);
        for (auto it = freeRanges.begin(); it != freeRanges.end(); ++it)
        {
            if (it->second < size)
                continue;
            const size_t pos = it->first;
            auto range = freeRanges.extract(it);
            if (range.mapped() > size)
            {
                range.key() += size;
                range.mapped() -= size;
                freeRanges.insert(std::move(range));
            }
            return {(uint8*)region.rwAlias + pos, (uint8*)region.rxAlias + pos, size};
        }
        PerfTelemetry::Get().jitArenaAllocFails.fetch_add(1, std::memory_order_relaxed);
        cemuLog_log(LogType::Force, "JIT arena: no free range for {} bytes", size);
        return {};
    }

    void release(const DualMapRegion& allocation)
    {
        if (!allocation.rwAlias || !allocation.size)
            return;
        PerfTelemetry::Get().jitArenaReleases.fetch_add(1, std::memory_order_relaxed);
        const size_t pos = (uint8*)allocation.rwAlias - (uint8*)region.rwAlias;
        cemu_assert(pos <= region.size && allocation.size <= region.size - pos);
        std::lock_guard lock(mutex);
        auto next = freeRanges.lower_bound(pos);
        cemu_assert(next == freeRanges.end() || pos + allocation.size <= next->first);
        if (next != freeRanges.begin())
        {
            auto previous = std::prev(next);
            cemu_assert(previous->first + previous->second <= pos);
            if (previous->first + previous->second == pos)
            {
                previous->second += allocation.size;
                if (next != freeRanges.end() && previous->first + previous->second == next->first)
                {
                    previous->second += next->second;
                    freeRanges.erase(next);
                }
                return;
            }
        }
        if (next != freeRanges.end() && pos + allocation.size == next->first)
        {
            auto range = freeRanges.extract(next);
            range.key() = pos;
            range.mapped() += allocation.size;
            freeRanges.insert(std::move(range));
        }
        else
        {
            freeRanges.emplace(pos, allocation.size);
        }
    }

    void reset(bool releasePages = true)
    {
        std::lock_guard lock(mutex);
        freeRanges.clear();
        if (region.size)
            freeRanges.emplace(0, region.size);

#if BOOST_OS_IOS
        if (releasePages && region.rwAlias && region.size)
            madvise(region.rwAlias, region.size, MADV_FREE);
#else
        (void)releasePages;
#endif
    }

    size_t usedBytes()
    {
        std::lock_guard lock(mutex);
        size_t free = 0;
        for (const auto& r : freeRanges)
            free += r.second;
        return region.size > free ? region.size - free : 0;
    }
};

static DualMapArena s_jitArena;

DualMapRegion PPCRecompiler_allocateJitArena(size_t size)
{
    return s_jitArena.alloc(size);
}

void PPCRecompiler_releaseJitArena(const DualMapRegion& region)
{
    s_jitArena.release(region);
}

void PPCRecompiler_freeDualMap(const DualMapRegion& region)
{
#if defined(__APPLE__) && defined(__aarch64__)
    if (region.rwAlias)
        vm_deallocate(mach_task_self(), (vm_address_t)region.rwAlias, region.size);
    if (region.rxAlias)
        munmap(region.rxAlias, region.size);
#else
    (void)region;
#endif
}

void* g_jitArenaRxBase = nullptr;
void* g_jitArenaRxEnd = nullptr;
void* g_jitArenaRwBase = nullptr;

struct PPCInvalidationRange
{
    MPTR startAddress;
    uint32 size;
    PPCInvalidationRange(MPTR _startAddress, uint32 _size) : startAddress(_startAddress), size(_size) {}
};

struct
{
    FSpinlock             recompilerSpinlock;
    std::queue<MPTR>      targetQueue;
    std::vector<PPCInvalidationRange> invalidationRanges;
} PPCRecompilerState;

RangeStore<PPCRecFunction_t*, uint32, 7703, 0x2000> rangeStore_ppcRanges;

void ATTR_MS_ABI (*PPCRecompiler_enterRecompilerCode)(uint64 codeMem, uint64 ppcInterpreterInstance);
void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_visited)();
void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_unvisited)();

PPCRecompilerInstanceData_t* ppcRecompilerInstanceData;

#if PPCREC_FORCE_SYNCHRONOUS_COMPILATION
static std::mutex s_singleRecompilationMutex;
#endif

bool ppcRecompilerEnabled = false;
bool ppcRecompilerInited = false;
static std::atomic_int_fast32_t s_recompilerEnableCount{0};

void PPCRecompiler_recompileAtAddress(uint32 address);

void PPCRecompiler_visitAddressNoBlock(uint32 enterAddress)
{
#if PPCREC_FORCE_SYNCHRONOUS_COMPILATION
    if (ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] != PPCRecompiler_leaveRecompilerCode_unvisited)
        return;
    PPCRecompilerState.recompilerSpinlock.lock();
    if (ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] != PPCRecompiler_leaveRecompilerCode_unvisited)
    {
        PPCRecompilerState.recompilerSpinlock.unlock();
        return;
    }
    ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] = PPCRecompiler_leaveRecompilerCode_visited;
    PPCRecompilerState.recompilerSpinlock.unlock();
    s_singleRecompilationMutex.lock();
    if (ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] == PPCRecompiler_leaveRecompilerCode_visited)
        PPCRecompiler_recompileAtAddress(enterAddress);
    s_singleRecompilationMutex.unlock();
    return;
#endif
    if (ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] != PPCRecompiler_leaveRecompilerCode_unvisited)
        return;
    if (!PPCRecompilerState.recompilerSpinlock.try_lock())
        return;
    auto funcPtr = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4];
    if (funcPtr != PPCRecompiler_leaveRecompilerCode_unvisited)
    {
        PPCRecompilerState.recompilerSpinlock.unlock();
        return;
    }
    PPCRecompilerState.targetQueue.emplace(enterAddress);
    ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4] = PPCRecompiler_leaveRecompilerCode_visited;
    PPCRecompilerState.recompilerSpinlock.unlock();
    s_threadPool.Notify();
}

void PPCRecompiler_notifyWorkers()
{
    s_threadPool.Notify();
}

bool PPCRecompiler_drainQueue()
{
    PPCRecompilerState.recompilerSpinlock.lock();
    if (PPCRecompilerState.targetQueue.empty())
    {
        PPCRecompilerState.recompilerSpinlock.unlock();
        return false;
    }
    uint32 enterAddress = PPCRecompilerState.targetQueue.front();
    PPCRecompilerState.targetQueue.pop();
    auto funcPtr = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4];

    if (funcPtr != PPCRecompiler_leaveRecompilerCode_visited)
    {
        PPCRecompilerState.recompilerSpinlock.unlock();
        return true;
    }

    PPCRecompilerState.recompilerSpinlock.unlock();
    PPCRecompiler_recompileAtAddress(enterAddress);
    return true;
}

void PPCRecompiler_recompileIfUnvisited(uint32 enterAddress)
{
    if (!ppcRecompilerEnabled)
        return;
    PPCRecompiler_visitAddressNoBlock(enterAddress);
}

static std::atomic<int> s_enterLogCount{0};

static bool isInArena(void* ptr)
{
    uintptr_t p = (uintptr_t)ptr;
    uintptr_t rxBase = (uintptr_t)s_jitArena.region.rxAlias;
    uintptr_t rxEnd  = rxBase + s_jitArena.region.size;
    return p >= rxBase && p < rxEnd;
}

static bool isInterfaceFunc(PPCREC_JUMP_ENTRY funcPtr)
{
    return funcPtr == PPCRecompiler_leaveRecompilerCode_unvisited ||
           funcPtr == PPCRecompiler_leaveRecompilerCode_visited;
}

void PPCRecompiler_enter(PPCInterpreter_t* hCPU, PPCREC_JUMP_ENTRY funcPtr)
{
#if BOOST_OS_WINDOWS
	uint32 prevState = _controlfp(0, 0);
	_controlfp(_RC_NEAR, _MCW_RC);
	PPCRecompiler_enterRecompilerCode((uint64)funcPtr, (uint64)hCPU);
	_controlfp(prevState, _MCW_RC);
	#ifdef CEMU_DEBUG_ASSERT
	if (hCPU->remainingCycles > 0 && GetAsyncKeyState(VK_F4))
	{
		auto t = std::chrono::high_resolution_clock::now();
		auto dur = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
		cemuLog_log(LogType::Force, "Recompiler exit: 0x{:08x} LR: 0x{:08x} Timestamp {}.{:04}", hCPU->instructionPointer, hCPU->spr.LR, dur / 1000LL, (dur % 1000LL));
	}
	#endif
#else
	PPCRecompiler_enterRecompilerCode((uint64)funcPtr, (uint64)hCPU);
#endif
	if (hCPU->remainingCycles > 0)
	{
		PPCRecompiler_visitAddressNoBlock(hCPU->instructionPointer);
	}
}

void PPCRecompiler_attemptEnterWithoutRecompile(PPCInterpreter_t* hCPU, uint32 enterAddress)
{
    cemu_assert_debug(hCPU->instructionPointer == enterAddress);
    if (!ppcRecompilerEnabled)
        return;
    auto funcPtr = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4];
    if (funcPtr != PPCRecompiler_leaveRecompilerCode_unvisited && funcPtr != PPCRecompiler_leaveRecompilerCode_visited)
    {
        cemu_assert_debug(ppcRecompilerInstanceData != nullptr);
        PPCRecompiler_enter(hCPU, funcPtr);
    }
}

static std::atomic<int> s_attemptLogCount{0};

void PPCRecompiler_attemptEnter(PPCInterpreter_t* hCPU, uint32 enterAddress)
{
    cemu_assert_debug(hCPU->instructionPointer == enterAddress);
    if (!ppcRecompilerEnabled)
        return;
    if (hCPU->remainingCycles <= 0)
        return;
    auto funcPtr = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[enterAddress / 4];
    if (funcPtr == PPCRecompiler_leaveRecompilerCode_unvisited)
    {
        PPCRecompiler_visitAddressNoBlock(enterAddress);
    }
    else if (funcPtr != PPCRecompiler_leaveRecompilerCode_visited)
    {
        cemu_assert_debug(ppcRecompilerInstanceData != nullptr);
        PPCRecompiler_enter(hCPU, funcPtr);
    }
}

bool PPCRecompiler_enabled()
{
    return ppcRecompilerEnabled;
}

void PPCRecompiler_Enable()
{
    if (!ppcRecompilerInited)
        return;

    s_recompilerEnableCount++;
    ppcRecompilerEnabled = true;
    PPCCore_InitializePointer(true);
}

void PPCRecompiler_Disable()
{
    if (!ppcRecompilerInited)
        return;

    const auto currentCount = s_recompilerEnableCount.load();
    if (currentCount > 0)
        s_recompilerEnableCount--;
    ppcRecompilerEnabled = s_recompilerEnableCount.load() > 0;
    PPCCore_InitializePointer(ppcRecompilerEnabled);
}

void PPCRecompiler_reprotectAsExecutable(void* codePtr, size_t codeSize)
{
    if (codePtr == nullptr || codeSize == 0)
        return;

    if (s_dualMapJITEnabled)
    {
        PPCRecompiler_flushInstructionCache(codePtr, codeSize);
        return;
    }

    static const uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t base = (uintptr_t)codePtr & ~(pageSize - 1);
    size_t alignedSize = (codeSize + ((uintptr_t)codePtr - base) + pageSize - 1) & ~(pageSize - 1);
    if (mprotect((void*)base, alignedSize, PROT_READ | PROT_EXEC) != 0)
        cemuLog_log(LogType::Force, "Recompiler: mprotect RX failed for {:p} ({} bytes)", codePtr, codeSize);

    PPCRecompiler_flushInstructionCache(codePtr, codeSize);
}

void PPCRecompiler_reprotectAsWriteable(void* codePtr, size_t codeSize)
{
    if (codePtr == nullptr || codeSize == 0)
        return;

    if (s_dualMapJITEnabled)
        return;

    static const uintptr_t pageSize = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t base = (uintptr_t)codePtr & ~(pageSize - 1);
    size_t alignedSize = (codeSize + ((uintptr_t)codePtr - base) + pageSize - 1) & ~(pageSize - 1);
    if (mprotect((void*)base, alignedSize, PROT_READ | PROT_WRITE) != 0)
        cemuLog_log(LogType::Force, "Recompiler: mprotect RW failed for {:p} ({} bytes)", codePtr, codeSize);
}

void PPCRecompiler_commitDualMapCode(PPCRecFunction_t* func)
{
    if (!s_dualMapJITEnabled)
        return;
    PPCRecompiler_reprotectAsExecutable(func->x86Code, func->x86Size);
}

static bool PPCRecompiler_rangesOverlap(MPTR startA, MPTR endA, MPTR startB, MPTR endB)
{
    return startA < endB && endA > startB;
}

static void PPCRecompiler_resetVisitedTableRange(uint32 startAddress, uint32 endAddress)
{
    cemu_assert_debug(PPCRecompilerState.recompilerSpinlock.is_locked());
    if (ppcRecompilerInstanceData == nullptr)
        return;

    uint64 start = (uint64)startAddress & ~3ULL;
    uint64 end = ((uint64)endAddress + 3ULL) & ~3ULL;
    for (uint64 currentAddr = start; currentAddr < end; currentAddr += 4)
    {
        auto& jumpTableEntry = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[currentAddr / 4];
        if (jumpTableEntry == PPCRecompiler_leaveRecompilerCode_visited)
            jumpTableEntry = PPCRecompiler_leaveRecompilerCode_unvisited;
    }
}

static void PPCRecompiler_resetVisitedMarkersForFunction(PPCRecFunction_t* func)
{
    cemu_assert_debug(PPCRecompilerState.recompilerSpinlock.is_locked());
    for (auto& recFuncRange : func->list_ranges)
        PPCRecompiler_resetVisitedTableRange(recFuncRange.ppcAddress, recFuncRange.ppcAddress + recFuncRange.ppcSize);
}

bool PPCRecompiler_ApplyIMLPasses(ppcImlGenContext_t& ppcImlGenContext);

PPCRecFunction_t* PPCRecompiler_recompileFunction(PPCFunctionBoundaryTracker::PPCRange_t range, std::set<uint32>& entryAddresses, std::vector<std::pair<MPTR, uint32>>& entryPointsOut, PPCFunctionBoundaryTracker& boundaryTracker)
{
    if (range.startAddress >= PPC_REC_CODE_AREA_END)
    {
        cemuLog_log(LogType::Force, "Attempting to recompile function outside of allowed code area");
        return nullptr;
    }

    uint32 codeGenRangeStart, codeGenRangeSize = 0;
    coreinit::OSGetCodegenVirtAddrRangeInternal(codeGenRangeStart, codeGenRangeSize);
    if (codeGenRangeSize != 0)
    {
        if (range.startAddress >= codeGenRangeStart && range.startAddress < (codeGenRangeStart + codeGenRangeSize))
        {
            if (coreinit::codeGenShouldAvoid())
                return nullptr;
        }
    }

    PPCRecFunction_t* ppcRecFunc = new PPCRecFunction_t();
    ppcRecFunc->ppcAddress = range.startAddress;
    ppcRecFunc->ppcSize = range.length;

#if PPCREC_LOG_RECOMPILATION_RESULTS
    BenchmarkTimer bt;
    bt.Start();
#endif

    ppcImlGenContext_t ppcImlGenContext = { 0 };
    ppcImlGenContext.debug_entryPPCAddress = range.startAddress;
    if (!PPCRecompiler_generateIntermediateCode(ppcImlGenContext, ppcRecFunc, entryAddresses, boundaryTracker))
    {
        delete ppcRecFunc;
        return nullptr;
    }

    uint32 ppcRecLowerAddr = LaunchSettings::GetPPCRecLowerAddr();
    uint32 ppcRecUpperAddr = LaunchSettings::GetPPCRecUpperAddr();
    if (ppcRecLowerAddr != 0 && ppcRecUpperAddr != 0)
    {
        if (ppcRecFunc->ppcAddress < ppcRecLowerAddr || ppcRecFunc->ppcAddress > ppcRecUpperAddr)
        {
            delete ppcRecFunc;
            return nullptr;
        }
    }

    if (!PPCRecompiler_ApplyIMLPasses(ppcImlGenContext))
    {
        delete ppcRecFunc;
        return nullptr;
    }

#if defined(ARCH_X86_64)
    if (!PPCRecompiler_generateX64Code(ppcRecFunc, &ppcImlGenContext))
    {
        delete ppcRecFunc;
        return nullptr;
    }

    if (s_dualMapJITEnabled && ppcRecFunc->x86Code && ppcRecFunc->x86Size > 0)
    {
        DualMapRegion region = s_jitArena.alloc(ppcRecFunc->x86Size);
        if (region.rwAlias && region.rxAlias)
        {
            memcpy(region.rwAlias, ppcRecFunc->x86Code, ppcRecFunc->x86Size);
#if defined(_WIN32)
            VirtualFree(ppcRecFunc->x86Code, 0, MEM_RELEASE);
#else
            munmap(ppcRecFunc->x86Code, ppcRecFunc->x86Size);
#endif
            ppcRecFunc->x86Code         = region.rxAlias;
            ppcRecFunc->x86CodeWritable = region.rwAlias;
            ppcRecFunc->dualMapRegion   = region;
            PPCRecompiler_flushInstructionCache(region.rxAlias, region.size);
        }
    }

    if (!s_dualMapJITEnabled)
        PPCRecompiler_reprotectAsExecutable(ppcRecFunc->x86Code, ppcRecFunc->x86Size);

#elif defined(__aarch64__)
    if (!PPCRecompiler_generateAArch64Code(ppcRecFunc, &ppcImlGenContext))
    {
        delete ppcRecFunc;
        return nullptr;
    }
#endif

    if (ActiveSettings::DumpRecompilerFunctionsEnabled())
    {
        const void* dumpPtr = (s_dualMapJITEnabled && ppcRecFunc->x86CodeWritable)
            ? ppcRecFunc->x86CodeWritable
            : ppcRecFunc->x86Code;
        FileStream* fs = FileStream::createFile2(ActiveSettings::GetUserDataPath(
            fmt::format("dump/recompiler/ppc_{:08x}.bin", ppcRecFunc->ppcAddress)));
        if (fs)
        {
            fs->writeData(dumpPtr, ppcRecFunc->x86Size);
            delete fs;
        }
    }

    entryPointsOut.clear();
    for (IMLSegment* imlSegment : ppcImlGenContext.segmentList2)
    {
        if (!imlSegment->isEnterable)
            continue;
        entryPointsOut.emplace_back(imlSegment->enterPPCAddress, imlSegment->x64Offset);
    }

#if PPCREC_LOG_RECOMPILATION_RESULTS
    bt.Stop();
    const void* hashPtr = (s_dualMapJITEnabled && ppcRecFunc->x86CodeWritable)
        ? ppcRecFunc->x86CodeWritable
        : ppcRecFunc->x86Code;
    uint32 codeHash = 0;
    for (uint32 i = 0; i < ppcRecFunc->x86Size; i++)
    {
        codeHash = _rotr(codeHash, 3);
        codeHash += ((uint8*)hashPtr)[i];
    }
    cemuLog_log(LogType::Force, "[Recompiler] PPC 0x{:08x} -> native: 0x{:x} Took {:.4}ms | Size {:04x} CodeHash {:08x}",
        (uint32)ppcRecFunc->ppcAddress, (uint64)(uintptr_t)ppcRecFunc->x86Code,
        bt.GetElapsedMilliseconds(), ppcRecFunc->x86Size, codeHash);
#endif

    return ppcRecFunc;
}

void PPCRecompiler_NativeRegisterAllocatorPass(ppcImlGenContext_t& ppcImlGenContext)
{
    IMLRegisterAllocatorParameters raParam;

    for (auto& it : ppcImlGenContext.mappedRegs)
        raParam.regIdToName.try_emplace(it.second.GetRegID(), it.first);

#if defined(ARCH_X86_64)
    auto& gprPhysPool = raParam.GetPhysRegPool(IMLRegFormat::I64);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RAX);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RDX);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RBX);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RBP);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RSI);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RDI);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R8);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R9);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R10);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_R11);
    gprPhysPool.SetAvailable(IMLArchX86::PHYSREG_GPR_BASE + X86_REG_RCX);

    auto& fprPhysPool = raParam.GetPhysRegPool(IMLRegFormat::F64);
    for (int i = 0; i < 15; i++)
        fprPhysPool.SetAvailable(IMLArchX86::PHYSREG_FPR_BASE + i);

#elif defined(__aarch64__)
    auto& gprPhysPool = raParam.GetPhysRegPool(IMLRegFormat::I64);
    for (auto i = IMLArchAArch64::PHYSREG_GPR_BASE; i < IMLArchAArch64::PHYSREG_GPR_BASE + IMLArchAArch64::PHYSREG_GPR_COUNT; i++)
    {
        if (i == IMLArchAArch64::PHYSREG_GPR_BASE + 18)
            continue;
        gprPhysPool.SetAvailable(i);
    }

    auto& fprPhysPool = raParam.GetPhysRegPool(IMLRegFormat::F64);
    for (auto i = IMLArchAArch64::PHYSREG_FPR_BASE; i < IMLArchAArch64::PHYSREG_FPR_BASE + IMLArchAArch64::PHYSREG_FPR_COUNT; i++)
        fprPhysPool.SetAvailable(i);
#endif

    IMLRegisterAllocator_AllocateRegisters(&ppcImlGenContext, raParam);
}

bool PPCRecompiler_ApplyIMLPasses(ppcImlGenContext_t& ppcImlGenContext)
{
	PPCRecompilerIML_isolateEnterableSegments(&ppcImlGenContext);

	IMLOptimizer_OptimizeDirectFloatCopies(&ppcImlGenContext);
	IMLOptimizer_OptimizeDirectIntegerCopies(&ppcImlGenContext);

	IMLOptimizer_StandardOptimizationPass(ppcImlGenContext);

	PPCRecompiler_NativeRegisterAllocatorPass(ppcImlGenContext);

	return true;
}

struct JitMapEntry { uint64 hostStart; uint32 hostSize; uint32 ppcStart; uint32 ppcSize; };
static std::mutex s_jitMapMutex;
static std::vector<JitMapEntry> s_jitMap;

void PPCRecompiler_DumpJitMap(const fs::path& path)
{
	std::vector<JitMapEntry> copy;
	{
		std::lock_guard lock(s_jitMapMutex);
		copy = s_jitMap;
	}
	FileStream* f = FileStream::createFile2(path);
	if (!f)
		return;
	std::string text;
	text.reserve(copy.size() * 40);
	for (const auto& e : copy)
		text += fmt::format("{:x} {:x} {:08x} {:x}\n", e.hostStart, e.hostSize, e.ppcStart, e.ppcSize);
	f->writeData(text.data(), text.size());
	delete f;
}

bool PPCRecompiler_makeRecompiledFunctionActive(uint32 initialEntryPoint, PPCFunctionBoundaryTracker::PPCRange_t& range, PPCRecFunction_t* ppcRecFunc, std::vector<std::pair<MPTR, uint32>>& entryPoints)
{
    PPCRecompilerState.recompilerSpinlock.lock();

    if (ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[initialEntryPoint / 4] != PPCRecompiler_leaveRecompilerCode_visited)
    {
        PPCRecompilerState.recompilerSpinlock.unlock();
        delete ppcRecFunc;
        return false;
    }

    bool isInvalidated = false;
    for (auto& invRange : PPCRecompilerState.invalidationRanges)
    {
        MPTR rStartAddr = invRange.startAddress;
        MPTR rEndAddr = rStartAddr + invRange.size;
        for (auto& recFuncRange : ppcRecFunc->list_ranges)
        {
            if (PPCRecompiler_rangesOverlap(recFuncRange.ppcAddress, recFuncRange.ppcAddress + recFuncRange.ppcSize, rStartAddr, rEndAddr))
            {
                isInvalidated = true;
                break;
            }
        }
    }
    PPCRecompilerState.invalidationRanges.clear();
    if (isInvalidated)
    {
        PPCRecompiler_resetVisitedMarkersForFunction(ppcRecFunc);
        PPCRecompilerState.recompilerSpinlock.unlock();
        delete ppcRecFunc;
        return false;
    }


    size_t registeredRanges = 0;
    try
    {
        for (auto& r : ppcRecFunc->list_ranges)
        {
            r.storedRange = rangeStore_ppcRanges.storeRange(ppcRecFunc, r.ppcAddress, r.ppcAddress + r.ppcSize);
            registeredRanges++;
        }
    }
    catch (const std::bad_alloc&)
    {
        size_t undone = 0;
        for (auto& r : ppcRecFunc->list_ranges)
        {
            if (undone++ >= registeredRanges)
                break;
            rangeStore_ppcRanges.deleteRange(r.storedRange);
            r.storedRange = nullptr;
        }
        for (uint32 v = range.startAddress; v < (range.startAddress + range.length); v += 4)
        {
            if (v != initialEntryPoint && ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[v / 4] == PPCRecompiler_leaveRecompilerCode_visited)
                ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[v / 4] = PPCRecompiler_leaveRecompilerCode_unvisited;
        }
        PPCRecompilerState.recompilerSpinlock.unlock();
        delete ppcRecFunc;
        throw;
    }

    for (auto& itr : entryPoints)
    {
        ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[itr.first / 4] = (PPCREC_JUMP_ENTRY)((uint8*)ppcRecFunc->x86Code + itr.second);
    }

    for (uint32 v = range.startAddress; v < (range.startAddress + range.length); v += 4)
    {
        auto funcPtr = ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[v / 4];
        if (funcPtr == PPCRecompiler_leaveRecompilerCode_visited)
            ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[v / 4] = PPCRecompiler_leaveRecompilerCode_unvisited;
    }

    PPCRecompilerState.recompilerSpinlock.unlock();
    {
        std::lock_guard lock(s_jitMapMutex);
        s_jitMap.push_back({(uint64)(uintptr_t)ppcRecFunc->x86Code, (uint32)ppcRecFunc->x86Size, ppcRecFunc->ppcAddress, ppcRecFunc->ppcSize});
    }
    return true;
}

static void PPCRecompiler_recompileAtAddressImpl(uint32 address)
{
	cemu_assert_debug(ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[address / 4] == PPCRecompiler_leaveRecompilerCode_visited);

	PPCFunctionBoundaryTracker funcBoundaries;
	funcBoundaries.trackStartPoint(address);
	PPCFunctionBoundaryTracker::PPCRange_t range;
	if (funcBoundaries.getRangeForAddress(address, range) == false)
	{
		cemu_assert_debug(false);
	}

	PPCRecompilerState.recompilerSpinlock.lock();

	std::set<uint32> entryAddresses;

	entryAddresses.emplace(address);

	PPCRecompilerState.recompilerSpinlock.unlock();

	std::vector<std::pair<MPTR, uint32>> functionEntryPoints;
	const uint64 compileStartNs = PerfTelemetry::NowNs();
	PPCRecFunction_t* func = nullptr;
	try
	{
		func = PPCRecompiler_recompileFunction(range, entryAddresses, functionEntryPoints, funcBoundaries);
	}
	catch (const std::bad_alloc&)
	{
		static std::atomic<uint32> s_outOfMemoryCount{0};
		if (s_outOfMemoryCount.fetch_add(1) < 3)
			cemuLog_log(LogType::Force, "Recompiler: out of memory compiling the function at {:08x}; it runs in the interpreter", address);
		return;
	}

	if (!func)
	{
		return;
	}
	try
	{
		PPCRecompiler_makeRecompiledFunctionActive(address, range, func, functionEntryPoints);
	}
	catch (const std::bad_alloc&)
	{
		static std::atomic<uint32> s_activateOomCount{0};
		if (s_activateOomCount.fetch_add(1) < 3)
			cemuLog_log(LogType::Force, "Recompiler: out of memory activating the function at {:08x}; it runs in the interpreter", address);
		return;
	}
	PerfTelemetry::Get().jitBlocks.fetch_add(1, std::memory_order_relaxed);
	PerfTelemetry::Get().jitCompileNs.fetch_add(PerfTelemetry::NowNs() - compileStartNs, std::memory_order_relaxed);
}


void PPCRecompiler_recompileAtAddress(uint32 address)
{
	try
	{
		PPCRecompiler_recompileAtAddressImpl(address);
	}
	catch (const std::bad_alloc&)
	{
		static std::atomic<uint32> s_outerOomCount{0};
		if (s_outerOomCount.fetch_add(1) < 3)
			cemuLog_log(LogType::Force, "Recompiler: out of memory preparing the function at {:08x}; it runs in the interpreter", address);
	}
	catch (const std::exception& e)
	{
		cemuLog_log(LogType::Force, "Recompiler: exception while compiling the function at {:08x} ({}); it runs in the interpreter", address, e.what());
	}
}

#define PPC_REC_ALLOC_BLOCK_SIZE    (4*1024*1024)

constexpr uint32 PPCRecompiler_GetNumAddressSpaceBlocks()
{
    return (MEMORY_CODEAREA_ADDR + MEMORY_CODEAREA_SIZE + PPC_REC_ALLOC_BLOCK_SIZE - 1) / PPC_REC_ALLOC_BLOCK_SIZE;
}

std::bitset<PPCRecompiler_GetNumAddressSpaceBlocks()> ppcRecompiler_reservedBlockMask;

static bool PPCRecompiler_isLookupTableBlockReserved(uint32 address)
{
	uint32 blockIndex = address / PPC_REC_ALLOC_BLOCK_SIZE;
	return blockIndex < ppcRecompiler_reservedBlockMask.size() && ppcRecompiler_reservedBlockMask[blockIndex];
}

static uint32 PPCRecompiler_lookupTableBlockSize()
{
	return PPC_REC_ALLOC_BLOCK_SIZE;
}

void PPCRecompiler_reserveLookupTableBlock(uint32 offset)
{
	uint32 blockIndex = offset / PPC_REC_ALLOC_BLOCK_SIZE;
	offset = blockIndex * PPC_REC_ALLOC_BLOCK_SIZE;

	if (ppcRecompiler_reservedBlockMask[blockIndex])
		return;
	ppcRecompiler_reservedBlockMask[blockIndex] = true;

	void* p3 = MemMapper::AllocateMemory(&(ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[offset/4]), (PPC_REC_ALLOC_BLOCK_SIZE/4)*sizeof(void*), MemMapper::PAGE_PERMISSION::P_RW, true);
	if( !p3 )
	{
		cemuLog_log(LogType::Force, "Failed to allocate memory for recompiler (0x{:08x})", offset);
		cemu_assert(false);
		return;
	}
	for(uint32 i=0; i<PPC_REC_ALLOC_BLOCK_SIZE/4; i++)
	{
		ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[offset/4+i] = PPCRecompiler_leaveRecompilerCode_unvisited;
	}
}

void PPCRecompiler_allocateRange(uint32 startAddress, uint32 size)
{
	if (ppcRecompilerInstanceData == nullptr)
		return;
	uint32 endAddress = (startAddress + size + PPC_REC_ALLOC_BLOCK_SIZE - 1) & ~(PPC_REC_ALLOC_BLOCK_SIZE-1);
	startAddress = (startAddress) & ~(PPC_REC_ALLOC_BLOCK_SIZE-1);
	startAddress = std::min(startAddress, (uint32)MEMORY_CODEAREA_ADDR + MEMORY_CODEAREA_SIZE);
	endAddress = std::min(endAddress, (uint32)MEMORY_CODEAREA_ADDR + MEMORY_CODEAREA_SIZE);
	for (uint32 i = startAddress; i < endAddress; i += PPC_REC_ALLOC_BLOCK_SIZE)
	{
		PPCRecompiler_reserveLookupTableBlock(i);
	}
}

struct ppcRecompilerFuncRange_t
{
	MPTR	ppcStart;
	uint32  ppcSize;
	void*   x86Start;
	size_t  x86Size;
};

bool PPCRecompiler_findFuncRanges(uint32 addr, ppcRecompilerFuncRange_t* rangesOut, size_t* countInOut)
{
	PPCRecompilerState.recompilerSpinlock.lock();
	size_t countIn = *countInOut;
	size_t countOut = 0;

	rangeStore_ppcRanges.findRanges(addr, addr + 4, [rangesOut, countIn, &countOut](uint32 start, uint32 end, PPCRecFunction_t* func)
	{
		if (countOut < countIn)
		{
			rangesOut[countOut].ppcStart = start;
			rangesOut[countOut].ppcSize = (end-start);
			rangesOut[countOut].x86Start = func->x86Code;
			rangesOut[countOut].x86Size = func->x86Size;
		}
		countOut++;
	}
	);
	PPCRecompilerState.recompilerSpinlock.unlock();
	*countInOut = countOut;
	if (countOut > countIn)
		return false;
	return true;
}

extern "C" DLLEXPORT uintptr_t * PPCRecompiler_getJumpTableBase()
{
	if (ppcRecompilerInstanceData == nullptr)
		return nullptr;
	return (uintptr_t*)ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable;
}

void PPCRecompiler_invalidateTableRange(uint32 offset, uint32 size)
{
	if (ppcRecompilerInstanceData == nullptr)
		return;
	for (uint32 i = 0; i < size / 4; i++)
	{
		ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[offset / 4 + i] = PPCRecompiler_leaveRecompilerCode_unvisited;
	}
}

void PPCRecompiler_deleteFunction(PPCRecFunction_t* func)
{
    cemu_assert_debug(PPCRecompilerState.recompilerSpinlock.is_locked());
    PerfTelemetry::Get().jitInvalidations.fetch_add(1, std::memory_order_relaxed);
    for (auto& r : func->list_ranges)
    {
        PPCRecompiler_invalidateTableRange(r.ppcAddress, r.ppcSize);
        if (r.storedRange)
            rangeStore_ppcRanges.deleteRange(r.storedRange);
        r.storedRange = nullptr;
    }

    if (s_dualMapJITEnabled && func->dualMapRegion.rwAlias)
    {
        func->dualMapRegion.size  = 0;
        func->x86Code             = nullptr;
        func->x86CodeWritable     = nullptr;
    }
}

static bool PPCRecompiler_isLookupTableBlockReserved(uint32 address);
static uint32 PPCRecompiler_lookupTableBlockSize();

namespace
{
void PPCRecompiler_invalidateRangeInternal(uint32 startAddr, uint32 endAddr)
{
	if (ppcRecompilerEnabled == false)
		return;
	if (startAddr >= PPC_REC_CODE_AREA_SIZE)
		return;
	cemu_assert_debug(endAddr >= startAddr);

	PPCRecompilerState.recompilerSpinlock.lock();

	uint32 rStart;
	uint32 rEnd;
	PPCRecFunction_t* rFunc;

	const uint64 blockSize = PPCRecompiler_lookupTableBlockSize();
	const uint64 rangeEnd = (uint64)(endAddr & ~3);
	for (uint64 blockStart = (uint64)startAddr & ~3; blockStart < rangeEnd; )
	{
		uint64 blockEnd = std::min<uint64>((blockStart / blockSize + 1) * blockSize, rangeEnd);
		if (PPCRecompiler_isLookupTableBlockReserved((uint32)blockStart))
		{
			for (uint64 currentAddr = blockStart; currentAddr < blockEnd; currentAddr += 4)
				ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[currentAddr / 4] = PPCRecompiler_leaveRecompilerCode_unvisited;
		}
		blockStart = blockEnd;
	}

	PPCRecompilerState.invalidationRanges.emplace_back(startAddr, endAddr-startAddr);

	while (rangeStore_ppcRanges.findFirstRange(startAddr, endAddr, rStart, rEnd, rFunc) )
	{
		PPCRecompiler_deleteFunction(rFunc);
	}

	PPCRecompilerState.recompilerSpinlock.unlock();
}
}

void PPCRecompiler_invalidateRange(uint32 startAddr, uint32 endAddr)
{
	PPCRecompiler_invalidateRangeInternal(startAddr, endAddr);
}

void PPCRecompiler_invalidateRangeFromICBI(uint32 startAddr, uint32 endAddr)
{
	PPCRecompiler_invalidateRangeInternal(startAddr, endAddr);
}

#if defined(ARCH_X86_64)
void PPCRecompiler_initPlatform()
{
    ppcRecompilerInstanceData->_x64XMM_xorNegateMaskBottom[0] = 1ULL << 63ULL;
    ppcRecompilerInstanceData->_x64XMM_xorNegateMaskBottom[1] = 0ULL;
    ppcRecompilerInstanceData->_x64XMM_xorNegateMaskPair[0] = 1ULL << 63ULL;
    ppcRecompilerInstanceData->_x64XMM_xorNegateMaskPair[1] = 1ULL << 63ULL;
    ppcRecompilerInstanceData->_x64XMM_xorNOTMask[0] = 0xFFFFFFFFFFFFFFFFULL;
    ppcRecompilerInstanceData->_x64XMM_xorNOTMask[1] = 0xFFFFFFFFFFFFFFFFULL;
    ppcRecompilerInstanceData->_x64XMM_andAbsMaskBottom[0] = ~(1ULL << 63ULL);
    ppcRecompilerInstanceData->_x64XMM_andAbsMaskBottom[1] = ~0ULL;
    ppcRecompilerInstanceData->_x64XMM_andAbsMaskPair[0] = ~(1ULL << 63ULL);
    ppcRecompilerInstanceData->_x64XMM_andAbsMaskPair[1] = ~(1ULL << 63ULL);
    ppcRecompilerInstanceData->_x64XMM_andFloatAbsMaskBottom[0] = ~(1 << 31);
    ppcRecompilerInstanceData->_x64XMM_andFloatAbsMaskBottom[1] = 0xFFFFFFFF;
    ppcRecompilerInstanceData->_x64XMM_andFloatAbsMaskBottom[2] = 0xFFFFFFFF;
    ppcRecompilerInstanceData->_x64XMM_andFloatAbsMaskBottom[3] = 0xFFFFFFFF;
    ppcRecompilerInstanceData->_x64XMM_singleWordMask[0] = 0xFFFFFFFFULL;
    ppcRecompilerInstanceData->_x64XMM_singleWordMask[1] = 0ULL;
    ppcRecompilerInstanceData->_x64XMM_constDouble1_1[0] = 1.0;
    ppcRecompilerInstanceData->_x64XMM_constDouble1_1[1] = 1.0;
    ppcRecompilerInstanceData->_x64XMM_constDouble0_0[0] = 0.0;
    ppcRecompilerInstanceData->_x64XMM_constDouble0_0[1] = 0.0;
    ppcRecompilerInstanceData->_x64XMM_constFloat0_0[0] = 0.0f;
    ppcRecompilerInstanceData->_x64XMM_constFloat0_0[1] = 0.0f;
    ppcRecompilerInstanceData->_x64XMM_constFloat1_1[0] = 1.0f;
    ppcRecompilerInstanceData->_x64XMM_constFloat1_1[1] = 1.0f;
    *(uint32*)&ppcRecompilerInstanceData->_x64XMM_constFloatMin[0] = 0x00800000;
    *(uint32*)&ppcRecompilerInstanceData->_x64XMM_constFloatMin[1] = 0x00800000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMask1[0] = 0x7F800000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMask1[1] = 0x7F800000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMask1[2] = 0x7F800000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMask1[3] = 0x7F800000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMaskResetSignBits[0] = ~0x80000000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMaskResetSignBits[1] = ~0x80000000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMaskResetSignBits[2] = ~0x80000000;
    ppcRecompilerInstanceData->_x64XMM_flushDenormalMaskResetSignBits[3] = ~0x80000000;
    ppcRecompilerInstanceData->_x64XMM_mxCsr_ftzOn  = 0x1F80 | 0x8000;
    ppcRecompilerInstanceData->_x64XMM_mxCsr_ftzOff = 0x1F80;
}
#else
void PPCRecompiler_initPlatform() {}
#endif

static bool PPCRecompiler_readDualMapEnvVar()
{
    int envValue = PPCRecompiler_readBoolEnvVar("DUAL_MAPPED_JIT");
    if (envValue != -1)
    {
        return envValue == 1;
    }

    return PPCRecompiler_shouldUseDualMapByDefault();
}

#if defined(__APPLE__) && BOOST_OS_IOS
extern "C" void cemu_bridge_log_checkpoint(const char* message);
#endif

// One line in the crash log saying what the JIT start-up saw, so a "JIT doesn't work" report says why.
static void PPCRecompiler_logJitState(const char* outcome)
{
#if defined(__APPLE__) && BOOST_OS_IOS
    char line[256];
    snprintf(line, sizeof(line), "JIT state: debugger attached=%d, cpu mode=%d, DUAL_MAPPED_JIT=%d, HAS_TXM=%d, LiveContainer=%d -> %s",
             checkDebugged() ? 1 : 0, (int)ActiveSettings::GetCPUMode(), PPCRecompiler_readBoolEnvVar("DUAL_MAPPED_JIT"),
             PPCRecompiler_readBoolEnvVar("HAS_TXM"), std::getenv("LC_HOME_PATH") != nullptr ? 1 : 0, outcome);
    cemu_bridge_log_checkpoint(line);
#endif
}

// Without a debugger attached, the TXM JIT handshake (brk #0xf00d) raises SIGTRAP and kills the app,
// whatever the CPU mode is set to. Only the Auto mode used to check for this.
static bool PPCRecompiler_jitNeedsDebugger()
{
    if (checkDebugged())
        return false;
    return ActiveSettings::GetCPUMode() == CPUMode::Auto || PPCRecompiler_readBoolEnvVar("HAS_TXM") == 1;
}

bool PPCRecompilerInitialized()
{
    return ppcRecompilerInited;
}

size_t PPCRecompiler_getJitArenaSize()
{
    return s_jitArena.region.rxAlias ? s_jitArena.region.size : 0;
}

size_t PPCRecompiler_getJitArenaUsed()
{
    return s_jitArena.region.rxAlias ? s_jitArena.usedBytes() : 0;
}

bool PPCRecompiler_Init26() {
    if (PPCRecompiler_jitNeedsDebugger()) {
        PPCRecompiler_logJitState("no debugger, running the interpreter");
        cemuLog_log(LogType::Force, "Debugger not attached, JIT cannot continue.");
        ppcRecompilerEnabled = false;
        PPCCore_InitializePointer(ppcRecompilerEnabled);
        return false;
    }

    s_dualMapJITEnabled = PPCRecompiler_readDualMapEnvVar();
    PPCRecompiler_logJitState(s_dualMapJITEnabled ? "starting the JIT" : "dual map off");

    if (!s_dualMapJITEnabled)
        return true;

    if (s_jitArena.region.rxAlias)
        return true;

    try
    {
        constexpr size_t kMB = 1024ull * 1024ull;
        constexpr size_t kArenaSizes[] = {
            512 * kMB, 384 * kMB, 256 * kMB, 128 * kMB, 64 * kMB
        };
        const size_t arenaStart = (size_t)DeviceCaps::GetBudgets().jitArenaStartMB * kMB;
        size_t chosenArena = 0;
        size_t firstCandidate = 0;
        while (firstCandidate + 1 < std::size(kArenaSizes) && kArenaSizes[firstCandidate] > arenaStart)
            firstCandidate++;
#if BOOST_OS_IOS
        const uint64 availableBytes = os_proc_available_memory();
        if (availableBytes != 0)
        {
            const size_t budget = (size_t)(availableBytes / 8);
            while (firstCandidate + 1 < std::size(kArenaSizes) && kArenaSizes[firstCandidate] > budget && kArenaSizes[firstCandidate] > 128 * kMB)
                firstCandidate++;
            cemuLog_log(LogType::Force, "JIT arena: {} MB of memory available to this app, starting at {} MB",
                (uint64)(availableBytes / kMB), kArenaSizes[firstCandidate] / kMB);
        }
#endif
        for (size_t candidateIndex = firstCandidate; candidateIndex < std::size(kArenaSizes); candidateIndex++)
        {
            const size_t candidate = kArenaSizes[candidateIndex];
            if (s_jitArena.init(candidate))
            {
                chosenArena = candidate;
                break;
            }
            cemuLog_log(LogType::Force,
                "JIT arena: {}MB unavailable, trying smaller before falling back to the interpreter",
                candidate / kMB);
        }
        if (!chosenArena)
        {
            cemuLog_log(LogType::Force, "JIT arena allocation failed at every size, disabling JIT");
            ppcRecompilerEnabled = false;
            ppcRecompilerInited = false;
        }
        else
        {
            if (chosenArena < 1024 * kMB)
            {
                cemuLog_log(LogType::Force,
                    "JIT arena: running the recompiler with {}MB. Blocks are recycled more often than with a "
                    "full arena, which is slower than that and very much faster than the interpreter.",
                    chosenArena / kMB);
            }
            else
            {
                cemuLog_log(LogType::Force, "JIT arena: reserved {}MB of address space; pages are backed as "
                    "translated code is written and returned on flush.", chosenArena / kMB);
            }
            g_jitArenaRxBase = s_jitArena.region.rxAlias;
            g_jitArenaRxEnd  = (uint8*)s_jitArena.region.rxAlias + s_jitArena.region.size;
            g_jitArenaRwBase = s_jitArena.region.rwAlias;
            ppcRecompilerInited = true;

            if (!ppcRecompilerInstanceData)
            {
            debug_printf("Allocating %dMB for recompiler instance data...\n", (sint32)(sizeof(PPCRecompilerInstanceData_t) / 1024 / 1024));
            ppcRecompilerInstanceData = (PPCRecompilerInstanceData_t*)MemMapper::ReserveMemory(nullptr, sizeof(PPCRecompilerInstanceData_t), MemMapper::PAGE_PERMISSION::P_RW);
            MemMapper::AllocateMemory(&(ppcRecompilerInstanceData->_x64XMM_xorNegateMaskBottom), sizeof(PPCRecompilerInstanceData_t) - offsetof(PPCRecompilerInstanceData_t, _x64XMM_xorNegateMaskBottom), MemMapper::PAGE_PERMISSION::P_RW, true);
            PPCRecompilerAArch64Gen_generateRecompilerInterfaceFunctions();
            }
        }
    }
    catch (const std::exception& e)
    {
        if (ActiveSettings::GetCPUMode() == CPUMode::Auto) {
            PPCCore_InitializePointer(false);
            ppcRecompilerEnabled = false;
        } else { throw e; }
    }

    PPCRecompiler_finishJitMappingSession();

    return ppcRecompilerInited;
}

void PPCRecompiler_init()
{
    s_recompilerEnableCount = 0;
    if (ActiveSettings::GetCPUMode() == CPUMode::SinglecoreInterpreter || ActiveSettings::GetCPUMode() == CPUMode::MulticoreInterpreter)
    {
        ppcRecompilerEnabled = false;
        PPCCore_InitializePointer(ppcRecompilerEnabled);
        return;
    }
    if (LaunchSettings::ForceInterpreter() || LaunchSettings::ForceMultiCoreInterpreter())
    {
        cemuLog_log(LogType::Force, "Recompiler disabled. Command line --force-interpreter or force-multicore-interpreter was passed");
        ppcRecompilerEnabled = false;
        PPCCore_InitializePointer(false);
        return;
    }

#if defined(__APPLE__) && BOOST_OS_IOS
    if (PPCRecompiler_jitNeedsDebugger()) {
        PPCRecompiler_logJitState("no debugger, running the interpreter");
        cemuLog_log(LogType::Force, "Debugger not attached, JIT cannot continue.");
        ppcRecompilerEnabled = false;
        PPCCore_InitializePointer(ppcRecompilerEnabled);
        return;
    }
#endif

    s_dualMapJITEnabled = PPCRecompiler_readDualMapEnvVar();

#if defined(__APPLE__) && defined(__aarch64__)
    const int appleOsMajorVersion = PPCRecompiler_getAppleOsMajorVersion();
    if (s_dualMapJITEnabled)
    {
        if (appleOsMajorVersion >= 26)
        {
            cemuLog_log(LogType::Force, PPCRecompiler_readTXMEnvVar()
                        ? "Recompiler: using iOS 26+ dual-mapped JIT via TXM/vm_remap"
                        : "Recompiler: using iOS 26+ dual-mapped JIT via vm_remap");
        }
        else
        {
            cemuLog_log(LogType::Force, "Recompiler: dual-mapped JIT enabled");
        }
    }
    else if (appleOsMajorVersion > 0 && appleOsMajorVersion <= 18)
    {
        cemuLog_log(LogType::Force, "Recompiler: using W^X for iOS {}", appleOsMajorVersion);
    }
    else
#endif
    {
        cemuLog_log(LogType::Force, s_dualMapJITEnabled
                    ? "Recompiler: dual-mapped JIT enabled"
                    : "Recompiler: using W^X");
    }

    bool init26 = false;
    const bool attemptedInit26 = s_dualMapJITEnabled && !ppcRecompilerInited;
    if (attemptedInit26)
    {
        init26 = PPCRecompiler_Init26();
    }

    if (attemptedInit26 && !init26)
    {
        cemuLog_log(LogType::Force, "Recompiler disabled: the JIT memory arena could not be allocated");
        ppcRecompilerEnabled = false;
        PPCCore_InitializePointer(false);
        return;
    }

    if (!init26) {
        if (!ppcRecompilerInstanceData)
        {
        debug_printf("Allocating %dMB for recompiler instance data...\n", (sint32)(sizeof(PPCRecompilerInstanceData_t) / 1024 / 1024));
        ppcRecompilerInstanceData = (PPCRecompilerInstanceData_t*)MemMapper::ReserveMemory(nullptr, sizeof(PPCRecompilerInstanceData_t), MemMapper::PAGE_PERMISSION::P_RW);
        MemMapper::AllocateMemory(&(ppcRecompilerInstanceData->_x64XMM_xorNegateMaskBottom), sizeof(PPCRecompilerInstanceData_t) - offsetof(PPCRecompilerInstanceData_t, _x64XMM_xorNegateMaskBottom), MemMapper::PAGE_PERMISSION::P_RW, true);
#ifdef ARCH_X86_64
        PPCRecompilerX64Gen_generateRecompilerInterfaceFunctions();
#elif defined(__aarch64__)
        PPCRecompilerAArch64Gen_generateRecompilerInterfaceFunctions();
#endif
        }
    }

    PPCRecompiler_allocateRange(0, 0x1000);
    PPCRecompiler_allocateRange(mmuRange_TRAMPOLINE_AREA.getBase(), mmuRange_TRAMPOLINE_AREA.getSize());
    PPCRecompiler_allocateRange(mmuRange_CODECAVE.getBase(), mmuRange_CODECAVE.getSize());

    PPCRecompiler_initPlatform();

    cemuLog_log(LogType::Force, "Recompiler initialized");
    ppcRecompilerEnabled = true;
    s_recompilerEnableCount = 1;
    PPCCore_InitializePointer(ppcRecompilerEnabled);
    ppcRecompilerInited = true;

    s_threadPool.Start();

    PPCRecompiler_finishJitMappingSession();

}

size_t PPCRecompiler_GetReservedLookupBlockCount()
{
    return ppcRecompiler_reservedBlockMask.count();
}

size_t PPCRecompiler_GetQueuedTargetCount()
{
    PPCRecompilerState.recompilerSpinlock.lock();
    size_t count = PPCRecompilerState.targetQueue.size() + PPCRecompilerState.invalidationRanges.size();
    PPCRecompilerState.recompilerSpinlock.unlock();
    return count;
}

void PPCRecompiler_Shutdown()
{
    s_threadPool.Stop();

    while (!PPCRecompilerState.targetQueue.empty())
        PPCRecompilerState.targetQueue.pop();
    PPCRecompilerState.invalidationRanges.clear();

    rangeStore_ppcRanges.clear();

    uint32 numBlocks = PPCRecompiler_GetNumAddressSpaceBlocks();
    for (uint32 i = 0; i < numBlocks; i++)
    {
        if (!ppcRecompiler_reservedBlockMask[i])
            continue;
        uint64 offset = i * PPC_REC_ALLOC_BLOCK_SIZE;
        MemMapper::FreeMemory(&(ppcRecompilerInstanceData->ppcRecompilerDirectJumpTable[offset/4]), (PPC_REC_ALLOC_BLOCK_SIZE/4)*sizeof(void*), true);
        ppcRecompiler_reservedBlockMask[i] = false;
    }

    const bool schedulerStopped = !coreinit::OSIsSchedulerActive();
    if (!schedulerStopped)
        cemuLog_log(LogType::Force, "JIT arena: PPC scheduler still active at shutdown, not releasing arena pages");
    s_jitArena.reset(schedulerStopped);
    ppcRecompilerEnabled = false;
    ppcRecompilerInited = false;
    s_recompilerEnableCount = 0;
    s_dualMapJITEnabled = false;
}
