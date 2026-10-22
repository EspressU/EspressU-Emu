#include "Common/precompiled.h"
#include "audio/IAudioAPI.h"
#include <cxxabi.h>
#include <typeinfo>
#import "CemuBridge.h"
#import "IOSLiveLog.h"
#import "IOSMotion.h"
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <sys/sysctl.h>
#include <sys/mman.h>
#include <mach/mach.h>
#include <string>
#include <cstring>
#include <atomic>
#include <thread>
#include <chrono>
#include <mutex>
#include <functional>
#include <cstdarg>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <exception>
#include <typeinfo>
#include <filesystem>
#include <set>
#include <mach/mach.h>
#include <os/proc.h>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/ucontext.h>
#include <pthread/qos.h>
#include <malloc/malloc.h>

#include "Cafe/CafeSystem.h"
#include "Cafe/OS/libs/nn_ac/nn_ac_reachability.h"
#include "Cafe/Filesystem/FST/KeyCache.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Core/LatteShader.h"
#include "Cafe/HW/Latte/Core/LatteShaderCache.h"
#include "Cemu/FileCache/FileCache.h"
#include "Cafe/HW/Latte/Core/LatteWaitInfo.h"
void PPCRecompiler_DumpJitMap(const fs::path& path);
#include "StallDetector.h"
#include "Cafe/HW/Latte/Core/PerfTelemetry.h"
#include "Cafe/GameProfile/GameProfile.h"
#include "util/Fiber/Fiber.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cemu/Logging/CemuLogging.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "Common/version.h"
#include "Common/DeviceCapabilities.h"
#include "CemuDeviceCaps.h"
#include "gui/interface/WindowSystem.h"
#include "input/api/iOS/GCControllerProvider.h"
#include "input/emulated/EmulatedController.h"
#include "input/emulated/VPADController.h"
#include "input/InputManager.h"
#include "input/api/Controller.h"
#include "util/crypto/aes128.h"

__attribute__((constructor)) static void ios_crypto_init_at_load() { AES128_init(); }

namespace coreinit { void OSSetThermalThrottleMicros(uint32 micros); }

extern "C" {
bool CemuTakeRendererFallback(char* reasonOut, size_t capacity);
void CemuInitialize(const char* execPath, const char* user_data_path, const char* config_path, const char* cache_path, const char* data_path);
void CemuRun(void);
void CemuPrepareRenderer(void);
void CemuShutdown(void);
void CemuUIKit_SetMainWindow(UIWindow* window);
void CemuUIKit_SetMainView(UIView* view);
void CemuUIKit_SetPadView(UIView* view);
void CemuUIKit_InitializeLayer(bool main);
void CemuUIKit_UpdateMainWindowSize(CGFloat width, CGFloat height, CGFloat scale);

void CemuUIKit_UpdatePadWindowSize(void);
void CemuUIKit_SetVisibleOutputs(bool tv, bool pad);
void CemuUIKit_SetOutputSources(bool mainShowsGamePad, bool padShowsTV);
void CemuUIKit_DescribeMainSurface(char* out, size_t outSize);
void CemuUIKit_SetPadTouch(CGFloat x, CGFloat y, bool down);
void* GCControllerBridge_add(const GCBridgeControllerDesc* desc);
void GCControllerBridge_remove(void* handle);
void GCControllerBridge_notifyChanged(void);
int csops(pid_t pid, unsigned int ops, void* useraddr, size_t usersize);
}

int IOSTitleLaunch_PrepareForegroundTitle(const char* path);
const char* IOSTitleLaunch_LastErrorDetail();
int IOSTitleLaunch_ReloadAndCountKeys();
int IOSTitleLaunch_PrepareForegroundTitleById(uint64_t titleId);
int IOSTitleDecrypt_ExtractToFolder(const char* srcPath, const char* destFolderPath,
    std::atomic_bool& cancelRequested,
    const std::function<void(uint64_t bytesWritten, uint32_t filesWritten)>& progressCallback);
int IOSTitleDecrypt_ExtractToWua(const char* srcPath, const char* destPath,
    std::atomic_bool& cancelRequested,
    const std::function<void(uint64_t bytesWritten, uint32_t filesWritten)>& progressCallback);
int IOSWuaContent_Build(const std::vector<std::string>& srcs, const char* destPath,
    std::atomic_bool& cancelRequested,
    const std::function<void(uint64_t bytesWritten, uint32_t filesWritten)>& progressCallback);
std::string IOSWuaContent_ListTitles(const char* wuaPath);
std::string IOSCoverArt_DeriveGameTdbId(const char* romPath);
std::string IOSCoverArt_GetTitleName(const char* romPath);
bool IOSDlcUpdateImport_DeriveTitleId(const char* romPath, uint64_t* titleIdOut);
bool IOSDlcUpdateImport_ReadTmdTitleId(const char* tmdPath, uint64_t* titleIdOut);
uint64_t IOSDlcUpdateImport_DeriveBaseTitleId(uint64_t titleId);
int IOSDlcUpdateImport_GetTitleType(uint64_t titleId);
void IOSDlcUpdateImport_GetMlcTitlePathComponents(uint64_t titleId, char* outUpperHex, char* outLowerHex);
bool IOSDlcUpdateImport_Inspect(const char* romPath, uint64_t* outTitleId, uint16_t* outVersion,
    int* outRegion, int* outInvalidReason);
uint64_t IOSDlcUpdateImport_DeriveContentTitleId(uint64_t baseTitleId, bool isUpdate);
int IOSEmulatedDevices_SlotCount(int device);
std::string IOSEmulatedDevices_SlotNames(int device);
std::string IOSEmulatedDevices_FigureList(int device, int slot);
std::string IOSEmulatedDevices_Load(int device, int slot, const char* path);
std::string IOSEmulatedDevices_Clear(int device, int slot);
std::string IOSEmulatedDevices_Create(int device, uint32_t figureId, uint16_t variant, const char* path);
std::string IOSEmulatedDevices_MoveDimensions(int fromSlot, int toSlot);
bool nnNfp_isInitialized();
bool nnNfp_touchNfcTagFromFile(const fs::path& filePath, uint32* nfcError);
std::string IOSGraphicPacks_List();
void IOSGraphicPacks_Refresh();
void IOSGraphicPacks_SetEnabled(int index, bool enabled);
void IOSAccounts_Refresh();
std::string IOSAccounts_List();
bool IOSAccounts_HasFreeSlot();
uint32_t IOSAccounts_NextPersistentId();
uint32_t IOSAccounts_MinPersistentId();
bool IOSAccounts_Locked();
bool IOSAccounts_Create(uint32_t persistentId, const char* miiName, uint16_t birthYear,
    uint8_t birthMonth, uint8_t birthDay, int gender, const char* email, int country);
bool IOSAccounts_Delete(uint32_t persistentId);
bool IOSAccounts_SetMiiName(uint32_t persistentId, const char* miiName);
bool IOSAccounts_SetGender(uint32_t persistentId, int gender);
bool IOSAccounts_SetEmail(uint32_t persistentId, const char* email);
bool IOSAccounts_SetCountry(uint32_t persistentId, int country);
bool IOSAccounts_SetBirthdate(uint32_t persistentId, uint16_t year, uint8_t month, uint8_t day);
uint32_t IOSAccounts_ActivePersistentId();
void IOSAccounts_SetActivePersistentId(uint32_t persistentId);
bool IOSAccounts_IsOnlineValid(uint32_t persistentId);
int IOSAccounts_OnlineError(uint32_t persistentId);
std::string IOSAccounts_OnlineHint();
std::string IOSAccounts_CountriesList();
int IOSAccounts_NetworkService(uint32_t persistentId);
void IOSAccounts_SetNetworkService(uint32_t persistentId, int service);
bool IOSAccounts_CustomNetworkServiceAvailable();
bool IOSAccounts_ReloadCustomNetworkService();
bool IOSAccounts_IsValidNetworkServicesXML(const char* path);
bool IOSTitlePause_Pause();
bool IOSTitlePause_Resume();
bool IOSTitlePause_IsPaused();
void IOSTitlePause_Forget();
void IOSSystemImplementation_Install();
bool IOSSystemImplementation_TitleExited(int* statusOut);
bool IOSSystemImplementation_TitleSwitchFailed();
bool IOSSystemImplementation_SystemAppletRequested();
void IOSSystemImplementation_ReportFatal(const char* reason);
const char* IOSSystemImplementation_FatalReason();
void IOSSystemImplementation_ResetExit();

namespace {
    int g_crashLogFd = -1;
    void cemu_crash_open_log();
    char g_crashLogPath[1024] = {0};

    void cemu_crash_write(const char* s) {
        if (g_crashLogFd >= 0 && s) write(g_crashLogFd, s, strlen(s));
    }

    void crash_hex(uint64_t v) {
        char buf[18];
        buf[0] = '0'; buf[1] = 'x';
        for (int i = 0; i < 16; i++) {
            const int nibble = (int)((v >> ((15 - i) * 4)) & 0xF);
            buf[2 + i] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
        }
        if (g_crashLogFd >= 0) write(g_crashLogFd, buf, sizeof(buf));
    }

    void crash_dec(int64_t v) {
        char digits[24];
        int i = 0;
        const bool negative = v < 0;
        uint64_t n = negative ? (uint64_t)(-v) : (uint64_t)v;
        if (n == 0) digits[i++] = '0';
        while (n > 0) { digits[i++] = (char)('0' + (n % 10)); n /= 10; }
        if (negative) digits[i++] = '-';
        char out[24];
        for (int j = 0; j < i; j++) out[j] = digits[i - 1 - j];
        if (g_crashLogFd >= 0) write(g_crashLogFd, out, (size_t)i);
    }

    const char* crash_vm_tag_name(unsigned tag) {
        switch (tag) {
            case 0: return "untagged (direct vm_allocate)";
            case 1: return "malloc";
            case 2: return "malloc small";
            case 3: return "malloc large";
            case 4: return "malloc huge";
            case 6: return "realloc";
            case 7: return "malloc tiny";
            case 11: return "malloc nano";
            case 21: return "IOKit";
            case 30: return "stack";
            case 31: return "guard";
            case 33: return "dylib";
            case 88: return "IOSurface";
            default: return "other";
        }
    }

    void cemu_vm_report(const char* why, uint64_t wantedBytes) {
        cemu_crash_open_log();
        struct Top { uint64_t size; uint64_t addr; unsigned tag; };
        Top top[5] = {};
        uint64_t tagBytes[256] = {};
        uint64_t regions = 0, mapped = 0, gapTotal = 0, gapMax = 0, gapMaxAt = 0, gaps1M = 0, gaps32M = 0, highestEnd = 0, prevEnd = 0;
        vm_address_t addr = 0;
        natural_t depth = 0;
        for (int guard = 0; guard < 200000; guard++) {
            vm_size_t size = 0;
            vm_region_submap_info_data_64_t info;
            mach_msg_type_number_t count = VM_REGION_SUBMAP_INFO_COUNT_64;
            if (vm_region_recurse_64(mach_task_self(), &addr, &size, &depth, (vm_region_recurse_info_t)&info, &count) != KERN_SUCCESS)
                break;
            if (info.is_submap) {
                depth++;
                continue;
            }
            regions++;
            mapped += size;
            if (prevEnd != 0 && addr > prevEnd) {
                const uint64_t gap = (uint64_t)addr - prevEnd;
                gapTotal += gap;
                if (gap >= (1ull << 20)) gaps1M++;
                if (gap >= (32ull << 20)) gaps32M++;
                if (gap > gapMax) { gapMax = gap; gapMaxAt = prevEnd; }
            }
            prevEnd = (uint64_t)addr + size;
            highestEnd = prevEnd;
            const unsigned tag = info.user_tag & 0xFF;
            tagBytes[tag] += size;
            for (int i = 0; i < 5; i++) {
                if (size > top[i].size) {
                    for (int j = 4; j > i; j--) top[j] = top[j - 1];
                    top[i] = { (uint64_t)size, (uint64_t)addr, tag };
                    break;
                }
            }
            addr += size;
        }
        cemu_crash_write("\nADDRESS SPACE at "); cemu_crash_write(why ? why : "?");
        cemu_crash_write(" (wanted "); crash_dec((int64_t)(wantedBytes >> 10)); cemu_crash_write(" KB): ");
        crash_dec((int64_t)regions); cemu_crash_write(" regions, ");
        crash_dec((int64_t)(mapped >> 20)); cemu_crash_write(" MB mapped, highest end "); crash_hex(highestEnd);
        cemu_crash_write(", free holes below it ");  crash_dec((int64_t)(gapTotal >> 20)); cemu_crash_write(" MB in total, ");
        crash_dec((int64_t)gaps1M); cemu_crash_write(" holes of 1 MB or more, "); crash_dec((int64_t)gaps32M);
        cemu_crash_write(" of 32 MB or more, largest "); crash_dec((int64_t)(gapMax >> 20)); cemu_crash_write(" MB at "); crash_hex(gapMaxAt);
        cemu_crash_write("\n");
        for (int i = 0; i < 5 && top[i].size; i++) {
            cemu_crash_write("  region "); crash_hex(top[i].addr); cemu_crash_write(" "); crash_dec((int64_t)(top[i].size >> 20));
            cemu_crash_write(" MB tag "); crash_dec((int64_t)top[i].tag); cemu_crash_write(" "); cemu_crash_write(crash_vm_tag_name(top[i].tag)); cemu_crash_write("\n");
        }
        for (int round = 0; round < 4; round++) {
            unsigned best = 0; uint64_t bestBytes = 0;
            for (unsigned t = 0; t < 256; t++) if (tagBytes[t] > bestBytes) { bestBytes = tagBytes[t]; best = t; }
            if (!bestBytes) break;
            cemu_crash_write("  by type: tag "); crash_dec((int64_t)best); cemu_crash_write(" "); cemu_crash_write(crash_vm_tag_name(best));
            cemu_crash_write(" "); crash_dec((int64_t)(bestBytes >> 20)); cemu_crash_write(" MB\n");
            tagBytes[best] = 0;
        }
        task_vm_info_data_t vi{};
        mach_msg_type_number_t viCount = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &viCount) == KERN_SUCCESS) {
            cemu_crash_write("  footprint "); crash_dec((int64_t)(vi.phys_footprint >> 20)); cemu_crash_write(" MB, resident "); crash_dec((int64_t)(vi.resident_size >> 20));
            cemu_crash_write(" MB, internal "); crash_dec((int64_t)(vi.internal >> 20)); cemu_crash_write(" MB, compressed "); crash_dec((int64_t)(vi.compressed >> 20));
            cemu_crash_write(" MB, virtual "); crash_dec((int64_t)(vi.virtual_size >> 20)); cemu_crash_write(" MB, os_proc_available_memory ");
            crash_dec((int64_t)(os_proc_available_memory() >> 20)); cemu_crash_write(" MB\n");
        }
    }

    void crash_describe_address(uint64_t addr) {
        crash_hex(addr);
        Dl_info di;
        if (addr && dladdr((const void*)addr, &di) && di.dli_fname) {
            const char* base = strrchr(di.dli_fname, '/');
            base = base ? base + 1 : di.dli_fname;
            cemu_crash_write(" ");
            cemu_crash_write(base);
            cemu_crash_write(" +");
            crash_dec((int64_t)(addr - (uint64_t)di.dli_fbase));
            if (di.dli_sname) {
                cemu_crash_write(" ");
                cemu_crash_write(di.dli_sname);
                cemu_crash_write(" +");
                crash_dec((int64_t)(addr - (uint64_t)di.dli_saddr));
            }
        }
    }

    bool crash_read(uint64_t addr, void* out, size_t n) {
        vm_size_t got = 0;
        return vm_read_overwrite(mach_task_self(), (vm_address_t)addr, (vm_size_t)n, (vm_address_t)out, &got) == KERN_SUCCESS && got == n;
    }

    void crash_frames(uint64_t pc, uint64_t lr, uint64_t fp, uint64_t sp, int maxFrames) {
        constexpr uint64_t kAddressMask = 0x00007FFFFFFFFFFFull;
        cemu_crash_write("    #0 pc "); crash_describe_address(pc & kAddressMask); cemu_crash_write("\n");
        if (lr) { cemu_crash_write("    #1 lr "); crash_describe_address(lr & kAddressMask); cemu_crash_write("\n"); }
        uint64_t previous = sp;
        for (int i = 0; i < maxFrames && fp != 0 && (fp & 0xF) == 0 && fp >= previous; i++) {
            uint64_t record[2];
            if (!crash_read(fp, record, sizeof(record)))
                break;
            cemu_crash_write("    #"); crash_dec(i + 2); cemu_crash_write(" "); crash_describe_address(record[1] & kAddressMask); cemu_crash_write("\n");
            if (record[0] <= fp)
                break;
            previous = fp;
            fp = record[0];
        }
    }

    void crash_thread_name(pthread_t thread, char* out, size_t cap) {
        out[0] = '\0';
        if (thread)
            pthread_getname_np(thread, out, cap);
        if (out[0] == '\0')
            snprintf(out, cap, "(unnamed)");
    }

    void crash_dump_other_threads() {
#if defined(__arm64__)
        thread_act_array_t threads = nullptr;
        mach_msg_type_number_t count = 0;
        if (task_threads(mach_task_self(), &threads, &count) != KERN_SUCCESS)
            return;
        const mach_port_t self = mach_thread_self();
        cemu_crash_write("--- other threads ("); crash_dec((int64_t)count - 1); cemu_crash_write(") ---\n");
        for (mach_msg_type_number_t i = 0; i < count && i < 96; i++) {
            const thread_act_t t = threads[i];
            if (t == self)
                continue;
            thread_suspend(t);
            arm_thread_state64_t state;
            mach_msg_type_number_t stateCount = ARM_THREAD_STATE64_COUNT;
            if (thread_get_state(t, ARM_THREAD_STATE64, (thread_state_t)&state, &stateCount) != KERN_SUCCESS)
                continue;
            char name[64];
            crash_thread_name(pthread_from_mach_thread_np(t), name, sizeof(name));
            const uint64_t pc = __darwin_arm_thread_state64_get_pc(state);
            uint32_t insn = 0;
            const bool isBrk = crash_read(pc, &insn, sizeof(insn)) && (insn & 0xFFE0001Fu) == 0xD4200000u;
            cemu_crash_write("  thread '"); cemu_crash_write(name); cemu_crash_write("'");
            if (isBrk) { cemu_crash_write(" STOPPED AT brk #"); crash_dec((int64_t)((insn >> 5) & 0xFFFF)); }
            cemu_crash_write("\n");
            crash_frames(pc, __darwin_arm_thread_state64_get_lr(state), __darwin_arm_thread_state64_get_fp(state),
                         __darwin_arm_thread_state64_get_sp(state), isBrk ? 16 : 6);
        }
#endif
    }

    std::atomic<bool> g_crashInProgress{false};

    void cemu_crash_signal_handler(int signum, siginfo_t* info, void* context) {
        if (g_crashInProgress.exchange(true)) {
            for (;;) sleep(1);
        }
        alarm(5);
        cemu_crash_write("\n=== CEMU CRASH: signal ");
        crash_dec(signum);
        cemu_crash_write(" ===\n");

        pthread_t self = pthread_self();
        char name[64];
        crash_thread_name(self, name, sizeof(name));
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        cemu_crash_write("received on thread '"); cemu_crash_write(name);
        cemu_crash_write("' tid "); crash_dec((int64_t)tid);
        cemu_crash_write(pthread_main_np() ? " (the main thread)\n" : " (not the main thread)\n");
        if (info) {
            cemu_crash_write("si_code "); crash_dec(info->si_code);
            cemu_crash_write(" si_addr "); crash_hex((uint64_t)info->si_addr);
            cemu_crash_write(" si_pid "); crash_dec(info->si_pid);
            cemu_crash_write("\n");
        }

#if defined(__arm64__)
        if (context) {
            ucontext_t* uc = (ucontext_t*)context;
            const auto& ss = uc->uc_mcontext->__ss;
            const uint64_t pc = __darwin_arm_thread_state64_get_pc(ss);
            uint32_t insn = 0;
            if (crash_read(pc, &insn, sizeof(insn)) && (insn & 0xFFE0001Fu) == 0xD4200000u) {
                cemu_crash_write("the interrupted instruction is brk #"); crash_dec((int64_t)((insn >> 5) & 0xFFFF)); cemu_crash_write("\n");
            }
            cemu_crash_write("this thread's own frames:\n");
            crash_frames(pc, __darwin_arm_thread_state64_get_lr(ss), __darwin_arm_thread_state64_get_fp(ss),
                         __darwin_arm_thread_state64_get_sp(ss), 40);
        } else
#endif
        {
            void* frames[64];
            int count = backtrace(frames, 64);
            if (g_crashLogFd >= 0) backtrace_symbols_fd(frames, count, g_crashLogFd);
        }
        crash_dump_other_threads();
        cemu_crash_write("=== end of crash report ===\n");

        signal(signum, SIG_DFL);
        raise(signum);
    }

    void cemu_crash_open_log() {
        if (g_crashLogFd >= 0)
            return;
        const char* home = getenv("HOME");
        if (!home)
            return;
        char path[1024];
        snprintf(path, sizeof(path), "%s/Documents/CemuCrashLog.txt", home);
        g_crashLogFd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        snprintf(g_crashLogPath, sizeof(g_crashLogPath), "%s", path);
        void* warm[4];
        backtrace(warm, 4);
    }

    std::terminate_handler g_previousTerminateHandler = nullptr;

    void cemu_terminate_handler() {
        cemu_crash_open_log();
        cemu_crash_write("\n=== CEMU TERMINATE ===\n");
        if (std::exception_ptr pending = std::current_exception())
        {
            try
            {
                std::rethrow_exception(pending);
            }
            catch (const std::exception& ex)
            {
                static std::atomic_flag s_reportedOnce = ATOMIC_FLAG_INIT;
                if (dynamic_cast<const std::bad_alloc*>(&ex) && !s_reportedOnce.test_and_set())
                    cemu_vm_report("std::bad_alloc escaping a thread", 0);
                cemu_crash_write("uncaught C++ exception, type: ");
                cemu_crash_write(typeid(ex).name());
                cemu_crash_write("\nwhat(): ");
                cemu_crash_write(ex.what() ? ex.what() : "(none)");
                cemu_crash_write("\n");
            }
            catch (...)
            {
                cemu_crash_write("uncaught exception not derived from std::exception\n");
            }
        }
        else
        {
            cemu_crash_write("terminate called with no in-flight exception\n");
        }
        {
            cemu_crash_write("terminate backtrace:\n");
            void* frames[40];
            const int frameCount = backtrace(frames, 40);
            if (g_crashLogFd >= 0)
                backtrace_symbols_fd(frames, frameCount, g_crashLogFd);
        }
        if (g_previousTerminateHandler && g_previousTerminateHandler != cemu_terminate_handler)
            g_previousTerminateHandler();
        abort();
    }
}

extern "C" __attribute__((constructor(101)))
void cemu_bridge_install_early_crash_handler() {
    cemu_crash_open_log();
    cemu_crash_write("=== Cemu process started (early constructor) ===\n");
    int sigs[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGTRAP, SIGFPE};
    for (int s : sigs) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = cemu_crash_signal_handler;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        sigaction(s, &action, nullptr);
    }
    g_previousTerminateHandler = std::set_terminate(cemu_terminate_handler);
}

const char* cemu_bridge_crash_log_path(void) {
    cemu_crash_open_log();
    return g_crashLogPath;
}

void cemu_bridge_log_checkpoint(const char* message) {
    cemu_crash_open_log();
    cemu_crash_write(message);
    cemu_crash_write("\n");
    ios_live_log_push(message);
}

extern "C" void cemu_ios_assert_hit(void) {
    static std::atomic<uint32_t> hits{0};
    const uint32_t n = ++hits;
    if (n > 20 && (n % 1000) != 0)
        return;
    cemu_crash_open_log();
    char name[64];
    name[0] = '\0';
    pthread_getname_np(pthread_self(), name, sizeof(name));
    char line[160];
    snprintf(line, sizeof(line), "CEMU ASSERT #%u on thread '%s' - continuing. Backtrace:", n, name[0] ? name : "(unnamed)");
    cemu_bridge_log_checkpoint(line);
    void* frames[24];
    const int count = backtrace(frames, 24);
    if (g_crashLogFd >= 0)
        backtrace_symbols_fd(frames, count, g_crashLogFd);
}

static void ios_bridge_thread_begin(const char* name, bool utilityQoS = true)
{
    pthread_setname_np(name);
    if (utilityQoS)
        pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
}

namespace {
    std::atomic<bool> g_memWatchRunning{false};

    uint64_t cemu_mem_footprint_bytes() {
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
            return 0;
        return (uint64_t)info.phys_footprint;
    }

    uint64_t cemu_mem_virtual_bytes() {
        task_vm_info_data_t info{};
        mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) != KERN_SUCCESS)
            return 0;
        return (uint64_t)info.virtual_size;
    }

    void cemu_mem_write_line(const char* tag, uint64_t availableBytes, uint64_t footprintBytes) {
        char line[720];
        int n = snprintf(line, sizeof(line),
                 "MEM %s: %llu MB still available to this process, %llu MB in use",
                 tag,
                 (unsigned long long)(availableBytes / (1024ull * 1024ull)),
                 (unsigned long long)(footprintBytes / (1024ull * 1024ull)));
        if (n > 0 && n < (int)sizeof(line))
        {
            int m = snprintf(line + n, sizeof(line) - n, " | address space reserved %llu MB", (unsigned long long)(cemu_mem_virtual_bytes() / (1024ull * 1024ull)));
            if (m > 0) n += m;
        }
        auto& w = LatteWait::Get();
        if (n > 0 && n < (int)sizeof(line) && w.memStatsValid.load())
        {
            snprintf(line + n, sizeof(line) - n,
                     " | GPU: device %u MB (host-mapped %u MB), textures %u = %u MB, staging %u MB, index %u MB, snapshots %u MB, "
                     "buffer cache %u MB, streamout %u MB, readback %u MB, command buffers in flight %u, textures evicted %u",
                     (unsigned)w.memDeviceMB.load(), (unsigned)w.memHostMappedMB.load(), (unsigned)w.memTextureCount.load(), (unsigned)w.memTextureMB.load(),
                     (unsigned)w.memStagingMB.load(), (unsigned)w.memIndexMB.load(), (unsigned)w.memSnapshotMB.load(), (unsigned)w.memBufferCacheMB.load(),
                     (unsigned)w.memXfbMB.load(), (unsigned)w.memReadbackMB.load(), (unsigned)w.executingCommandBuffers.load(), (unsigned)w.texturesEvicted.load());
        }
        cemu_bridge_log_checkpoint(line);
    }
}

static std::string cemu_sysctl_string(const char* name)
{
    size_t len = 0;
    if (sysctlbyname(name, nullptr, &len, nullptr, 0) != 0 || len == 0)
        return std::string();
    std::string out(len, '\0');
    if (sysctlbyname(name, out.data(), &len, nullptr, 0) != 0)
        return std::string();
    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

static uint64_t cemu_sysctl_u64(const char* name)
{
    uint64_t v = 0; size_t len = sizeof(v);
    if (sysctlbyname(name, &v, &len, nullptr, 0) == 0) return v;
    uint32_t v32 = 0; len = sizeof(v32);
    if (sysctlbyname(name, &v32, &len, nullptr, 0) == 0) return v32;
    return 0;
}

static std::string cemu_describe_current_exception(void)
{
    try { throw; }
    catch (const std::exception& ex) { return std::string(typeid(ex).name()) + ": " + ex.what(); }
    catch (...)
    {
        const std::type_info* type = abi::__cxa_current_exception_type();
        return std::string("non-standard exception ") + (type ? type->name() : "of unknown type");
    }
}

static std::string cemu_build_device_report(void)
{
    std::string report;
    const std::string model = cemu_sysctl_string("hw.machine");
    const uint64_t memBytes = cemu_sysctl_u64("hw.memsize");
    const uint64_t cores    = cemu_sysctl_u64("hw.ncpu");
    const uint64_t pcores   = cemu_sysctl_u64("hw.perflevel0.logicalcpu");
    const uint64_t ecores   = cemu_sysctl_u64("hw.perflevel1.logicalcpu");

    unsigned long long avail = 0, foot = 0;
    cemu_bridge_memory_status(&avail, &foot);

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "device: %s | iOS %s | RAM %llu MB | cores %llu",
        model.empty() ? "unknown" : model.c_str(),
        [[[NSProcessInfo processInfo] operatingSystemVersionString] UTF8String],
        (unsigned long long)(memBytes / (1024ull * 1024ull)),
        (unsigned long long)cores);
    report = std::string(cemu_device_caps_line()) + "\n" + buf;

    if (pcores && ecores)
    {
        snprintf(buf, sizeof(buf), " (%llu perf + %llu eff)",
                 (unsigned long long)pcores, (unsigned long long)ecores);
        report += buf;
    }
    if (avail)
    {
        snprintf(buf, sizeof(buf), " | %llu MB available to this app before iOS kills it",
                 (unsigned long long)(avail / (1024ull * 1024ull)));
        report += buf;
    }
    report += " | build ";
    report += BUILD_VERSION_STRING;
    return report;
}

extern "C" const char* cemu_bridge_device_report(void)
{
    static const std::string report = cemu_build_device_report();
    return report.c_str();
}

size_t PPCRecompiler_getJitArenaSize();
size_t PPCRecompiler_getJitArenaUsed();

const char* cemu_bridge_memory_headroom_summary(void) {
    static thread_local std::string summary;
    const uint64_t avail = (uint64_t)os_proc_available_memory();
    const uint64_t arena = (uint64_t)PPCRecompiler_getJitArenaSize();

    char buf[256];
    if (arena == 0) {
        snprintf(buf, sizeof(buf),
            "%llu MB headroom - no JIT arena, so this launch is running the interpreter",
            (unsigned long long)(avail / (1024ull * 1024ull)));
    } else {
        const uint64_t used = (uint64_t)PPCRecompiler_getJitArenaUsed();
        snprintf(buf, sizeof(buf), "%llu MB headroom - JIT arena %llu MB reserved, %llu MB in use%s",
            (unsigned long long)(avail / (1024ull * 1024ull)),
            (unsigned long long)(arena / (1024ull * 1024ull)),
            (unsigned long long)(used / (1024ull * 1024ull)),
            arena < ((uint64_t)DeviceCaps::GetBudgets().jitArenaStartMB << 20) ? " (reduced - less room than the JIT asked for)" : "");
    }
    summary = buf;
    return summary.c_str();
}

bool cemu_bridge_memory_status(unsigned long long* availableBytes, unsigned long long* footprintBytes) {
    const uint64_t avail = (uint64_t)os_proc_available_memory();
    const uint64_t foot = cemu_mem_footprint_bytes();
    if (availableBytes) *availableBytes = (unsigned long long)avail;
    if (footprintBytes) *footprintBytes = (unsigned long long)foot;
    return avail != 0 || foot != 0;
}

void cemu_bridge_memory_note(const char* tag) {
    unsigned long long avail = 0, foot = 0;
    cemu_bridge_memory_status(&avail, &foot);
    cemu_mem_write_line(tag && tag[0] ? tag : "checkpoint", avail, foot);
}

bool MetalPipelineCache_FlushArchive(uint32_t timeoutMs);

void cemu_bridge_flush_pipeline_cache(void) {
    if (!MetalPipelineCache_FlushArchive(4000))
        cemu_bridge_log_checkpoint("pipeline cache flush did not finish in 4 s");
}

void cemu_bridge_start_memory_watchdog(void) {
    if (g_memWatchRunning.exchange(true))
        return;

    [[NSNotificationCenter defaultCenter]
        addObserverForName:UIApplicationDidReceiveMemoryWarningNotification
                    object:nil
                     queue:nil
                usingBlock:^(NSNotification* note) {
                    (void)note;
                    unsigned long long avail = 0, foot = 0;
                    cemu_bridge_memory_status(&avail, &foot);
                    cemu_mem_write_line("WARNING - iOS is asking for memory back", avail, foot);
                }];

    cemu_bridge_memory_note("baseline at startup");

    std::thread([] {
        ios_bridge_thread_begin("EspressU MEM watch");
        uint64_t lastFootBucket = 0;
        uint64_t lastAvailBucket = UINT64_MAX;
        bool criticalAnnounced = false;
        auto lastForced = std::chrono::steady_clock::now();
        auto lastWritten = lastForced;
        while (g_memWatchRunning.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            unsigned long long avail = 0, foot = 0;
            if (!cemu_bridge_memory_status(&avail, &foot))
                continue;

            const uint64_t footBucket = foot / (32ull << 20);
            const uint64_t availBucket = avail / (64ull << 20);
            const auto now = std::chrono::steady_clock::now();

            bool report = (footBucket > lastFootBucket || availBucket < lastAvailBucket) && now - lastWritten >= std::chrono::seconds(5);
            if (now - lastForced >= std::chrono::seconds(60)) report = true;

            if (report)
            {
                lastFootBucket = footBucket;
                lastAvailBucket = availBucket;
                cemu_mem_write_line("sample", avail, foot);
                lastForced = now;
                lastWritten = now;
            }

            if (!criticalAnnounced && avail > 0 && avail < (128ull << 20))
            {
                criticalAnnounced = true;
                cemu_mem_write_line("CRITICAL - a kill by iOS is likely imminent", avail, foot);
            }
        }
    }).detach();
}

namespace {
    std::atomic<bool> g_initialized{false};

    std::mutex g_statusMutex;
    std::string g_statusText;

    void setStatus(const char* s) {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        g_statusText = s ? s : "";
    }

    bool statusIsEmpty() {
        std::lock_guard<std::mutex> lock(g_statusMutex);
        return g_statusText.empty();
    }

    const char* getStatus() {
        static thread_local std::string snapshot;
        {
            std::lock_guard<std::mutex> lock(g_statusMutex);
            snapshot = g_statusText;
        }
        return snapshot.c_str();
    }

    constexpr int kCpuModeUndecided   = 0;
    constexpr int kCpuModeInterpreter = 1;
    constexpr int kCpuModeRecompiler  = 2;

    std::atomic<int> g_cpuMode{kCpuModeUndecided};
    std::atomic<bool> g_recompilerRequested{false};
    std::atomic<bool> g_favourAccuracy{false};
    std::atomic<bool> g_favourPerformance{false};
    std::atomic<bool> g_fullSpeedRenders{false};
    std::atomic<int> g_fullSpeedShaderMode{0};
    std::atomic<bool> g_lowPowerMode{false};
    constexpr int kCoreModeAuto   = 0;
    constexpr int kCoreModeSingle = 1;
    constexpr int kCoreModeMulti  = 2;
    std::atomic<int> g_coreModeSetting{kCoreModeAuto};
    std::atomic<bool> g_autoDemoted{false};
    std::atomic<int> g_coresRunning{1};
    std::atomic<bool> g_autoPickedMulti{false};
    std::mutex g_cpuModeDetailMutex;
    std::string g_cpuModeDetail;

    void setCpuModeDetail(std::string detail) {
        std::lock_guard<std::mutex> lock(g_cpuModeDetailMutex);
        g_cpuModeDetail = std::move(detail);
    }

    const char* getCpuModeDetail() {
        static thread_local std::string snapshot;
        {
            std::lock_guard<std::mutex> lock(g_cpuModeDetailMutex);
            snapshot = g_cpuModeDetail;
        }
        return snapshot.c_str();
    }

    std::atomic<bool> g_titleRunning{false};
    std::atomic<bool> g_padRegistered{false};
    std::string g_activeMoltenVK;
}

static void ios_timebase_ladder_start();
static void ios_timebase_ladder_stop();

namespace {

NSString* ios_first_entry_with_length(NSString* dir, NSUInteger length)
{
    NSArray<NSString*>* entries = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil];
    for (NSString* entry in entries)
        if (entry.length == length)
            return [dir stringByAppendingPathComponent:entry];
    return nil;
}

bool ios_has_txm_classic()
{
    if ([NSProcessInfo processInfo].isiOSAppOnMac)
        return false;
    static NSString* const kImg4 = @"usr/standalone/firmware/FUD/Ap,TrustedExecutionMonitor.img4";
    if (NSString* boot = ios_first_entry_with_length(@"/System/Volumes/Preboot", 36))
    {
        if (NSString* file = ios_first_entry_with_length([boot stringByAppendingPathComponent:@"boot"], 96))
            return access([[file stringByAppendingPathComponent:kImg4] fileSystemRepresentation], F_OK) == 0;
    }
    if (NSString* preboot = ios_first_entry_with_length(@"/private/preboot", 96))
        return access([[preboot stringByAppendingPathComponent:kImg4] fileSystemRepresentation], F_OK) == 0;
    return false;
}

bool ios_chip(char& series, int& number)
{
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device)
        return false;
    NSString* name = device.name.uppercaseString;
    NSRegularExpression* re = [NSRegularExpression regularExpressionWithPattern:@"APPLE\\s+([MA])(\\d+)" options:0 error:nil];
    NSTextCheckingResult* m = [re firstMatchInString:name options:0 range:NSMakeRange(0, name.length)];
    if (!m)
        return false;
    series = (char)[[name substringWithRange:[m rangeAtIndex:1]] characterAtIndex:0];
    number = [[name substringWithRange:[m rangeAtIndex:2]] intValue];
    return true;
}

bool ios_os_at_least(NSInteger major, NSInteger minor)
{
    return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:(NSOperatingSystemVersion){major, minor, 0}];
}

bool ios_has_txm()
{
    char series = 0;
    int number = 0;
    const bool known = ios_chip(series, number);
    if (ios_os_at_least(27, 0))
    {
        if (known && series == 'A')
            return number > 12;
        return true;
    }
    if (ios_os_at_least(26, 6) && !ios_has_txm_classic())
    {
        if (!known)
            return false;
        return series == 'M' ? number >= 2 : number >= 15;
    }
    return ios_has_txm_classic();
}

void ios_configure_jit_environment()
{
    if (ios_os_at_least(19, 0))
    {
        const bool txm = ![NSProcessInfo processInfo].isiOSAppOnMac && ios_has_txm();
        setenv("DUAL_MAPPED_JIT", "1", 1);
        setenv("HAS_TXM", txm ? "1" : "0", 1);
    }
    else
    {
        setenv("HAS_TXM", "0", 1);
    }
    cemu_bridge_log_checkpoint((std::string("JIT environment: DUAL_MAPPED_JIT=") + (getenv("DUAL_MAPPED_JIT") ? getenv("DUAL_MAPPED_JIT") : "unset") +
        " HAS_TXM=" + (getenv("HAS_TXM") ? getenv("HAS_TXM") : "unset")).c_str());
}

bool ios_process_is_debugged(uint32_t& flagsOut)
{
    flagsOut = 0;
    return csops(getpid(), 0  , &flagsOut, sizeof(flagsOut)) == 0 && (flagsOut & 0x10000000u) != 0;
}

bool ios_jit_probe_impl()
{
    // CS_DEBUGGED stays set after the JIT helper let go, so the flag alone doesn't prove JIT works right now.
    // Where the recompiler maps its code like PPCRecompiler_allocateDualMap (not on TXM), map a page the same way.
    const char* dual = getenv("DUAL_MAPPED_JIT");
    const char* txm = getenv("HAS_TXM");
    if (!dual || strcmp(dual, "1") != 0 || (txm && strcmp(txm, "1") == 0))
        return true;
    const size_t size = 16384;
    void* rx = mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (rx == MAP_FAILED)
    {
        cemu_bridge_log_checkpoint("JIT probe: the system refused executable memory");
        return false;
    }
    vm_address_t rw = 0;
    vm_prot_t curProt = VM_PROT_NONE, maxProt = VM_PROT_NONE;
    kern_return_t kr = vm_remap(mach_task_self(), &rw, (vm_size_t)size, 0, VM_FLAGS_ANYWHERE,
                                mach_task_self(), (vm_address_t)rx, FALSE, &curProt, &maxProt, VM_INHERIT_NONE);
    bool ok = false;
    if (kr == KERN_SUCCESS)
    {
        ok = vm_protect(mach_task_self(), rw, (vm_size_t)size, FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS;
        vm_deallocate(mach_task_self(), rw, (vm_size_t)size);
    }
    munmap(rx, size);
    if (!ok)
        cemu_bridge_log_checkpoint("JIT probe: couldn't make a writable alias of executable memory");
    return ok;
}

namespace {
    struct IosDeviceFacts
    {
        std::string model;
        std::string gpuName;
        uint64_t perfCores = 0;
        uint64_t effCores = 0;
        uint64_t totalCores = 0;
        uint64_t memoryMB = 0;
    };

    const IosDeviceFacts& ios_device_facts()
    {
        static const IosDeviceFacts facts = [] {
            IosDeviceFacts f;
            f.model = cemu_sysctl_string("hw.machine");
            f.totalCores = cemu_sysctl_u64("hw.ncpu");
            f.perfCores = cemu_sysctl_u64("hw.perflevel0.physicalcpu");
            if (!f.perfCores)
                f.perfCores = cemu_sysctl_u64("hw.perflevel0.logicalcpu");
            f.effCores = cemu_sysctl_u64("hw.perflevel1.physicalcpu");
            if (!f.effCores)
                f.effCores = cemu_sysctl_u64("hw.perflevel1.logicalcpu");
            if (!f.perfCores)
                f.perfCores = f.totalCores;
            f.memoryMB = cemu_sysctl_u64("hw.memsize") / (1024ull * 1024ull);
            id<MTLDevice> device = MTLCreateSystemDefaultDevice();
            if (device && device.name)
                f.gpuName = [device.name UTF8String];
            return f;
        }();
        return facts;
    }

    int ios_thermal_state() { return (int)[[NSProcessInfo processInfo] thermalState]; }

    const char* ios_thermal_name(int state)
    {
        switch (state)
        {
        case 0: return "nominal";
        case 1: return "fair";
        case 2: return "serious";
        case 3: return "critical";
        default: return "unknown";
        }
    }

    bool ios_auto_multicore_viable(const IosDeviceFacts& device, int thermalState)
    {
        return device.perfCores >= 4
            && DeviceCaps::Get().tier() >= DeviceCaps::Tier::High
            && thermalState == 0;
    }

    struct CoreDecision
    {
        bool singleCore = true;
        bool autoPickedMulti = false;
        std::string reason;
    };

    CoreDecision ios_decide_core_count(uint64_t titleId)
    {
        const IosDeviceFacts& device = ios_device_facts();
        const int setting = g_coreModeSetting.load();
        const int thermal = ios_thermal_state();
        char deviceText[96];
        snprintf(deviceText, sizeof(deviceText), "%llu performance core(s), tier %s, thermal %s",
            (unsigned long long)device.perfCores, DeviceCaps::TierName(DeviceCaps::Get().tier()), ios_thermal_name(thermal));
        CoreDecision d;

        if (g_favourAccuracy.load())
        {
            d.reason = "one core because Favour accuracy is on";
            return d;
        }
        if (g_lowPowerMode.load())
        {
            d.reason = "one core because One-core mode is on";
            return d;
        }
        if (setting == kCoreModeSingle)
        {
            d.reason = "one core, chosen in Settings";
            return d;
        }
        if (!DeviceCaps::GetBudgets().multicoreViable)
        {
            d.reason = std::string(setting == kCoreModeMulti ? "one core: three were chosen in Settings, but this device has too little memory or too few cores to run three at once"
                                                              : "one core: this device has too little memory or too few cores to run three at once")
                + " (" + deviceText + ")";
            return d;
        }
        if (setting == kCoreModeMulti)
        {
            d.singleCore = false;
            d.reason = std::string("three cores, chosen in Settings (experimental; ") + deviceText + ")";
            if (device.perfCores < 3)
                d.reason += ". This device has fewer than three performance cores, so expect it to run slower than one core";
            return d;
        }

        if (g_autoDemoted.load())
        {
            d.reason = "Auto picked one core: a three-core run of this title crashed or hung before";
            return d;
        }
        if (thermal >= 2)
        {
            d.reason = std::string("Auto picked one core: the device is already hot (") + deviceText + ")";
            return d;
        }
        if (device.perfCores < 3)
        {
            d.reason = std::string("Auto picked one core: too few performance cores (") + deviceText + ")";
            return d;
        }
        std::optional<CPUMode> profileMode;
        if (titleId != 0)
        {
            GameProfile probe;
            if (probe.Load(titleId))
                profileMode = probe.GetCPUMode();
        }
        if (profileMode && (*profileMode == CPUMode::SinglecoreInterpreter || *profileMode == CPUMode::SinglecoreRecompiler))
        {
            d.reason = std::string("Auto picked one core: this title's game profile asks for single-core (") + deviceText + ")";
            return d;
        }
        if (profileMode && *profileMode == CPUMode::MulticoreRecompiler)
        {
            d.singleCore = false;
            d.autoPickedMulti = true;
            d.reason = std::string("Auto picked three cores: this title's game profile asks for multi-core (") + deviceText + ")";
            return d;
        }
        if (ios_auto_multicore_viable(device, thermal))
        {
            d.singleCore = false;
            d.autoPickedMulti = true;
            d.reason = std::string("Auto picked three cores: this device has the performance cores, memory and thermal room (") + deviceText + ")";
            return d;
        }
        d.reason = std::string("Auto picked one core: this device does not meet the cores, memory tier and thermal state needed for three (") + deviceText + ")";
        return d;
    }
}

void ios_apply_cpu_mode(bool announce = false)
{
    uint32_t csFlags = 0;
    const bool debugged = ios_process_is_debugged(csFlags) && ios_jit_probe_impl();
    uint64_t titleId = 0;
    if (announce)
        titleId = CafeSystem::GetForegroundTitleId();
    const CoreDecision cores = ios_decide_core_count(titleId);
    const bool singleCore = cores.singleCore;
    auto& config = GetConfig();
    char detail[768];

    if (announce)
        cemuLog_log(LogType::Force, "CPU cores: {}", cores.reason);
    g_coresRunning.store(singleCore ? 1 : 3);
    g_autoPickedMulti.store(cores.autoPickedMulti);

    if (!g_recompilerRequested.load() || !debugged)
    {
        if (announce)
        {
            if (!g_recompilerRequested.load())
                cemuLog_log(LogType::Force, "Recompiler not used: the recompiler (JIT) setting is off");
            else
                cemuLog_log(LogType::Force, "Recompiler not used: the recompiler is on in Settings but no JIT enabler is attached to this process (cs_flags 0x{:08x}, CS_DEBUGGED not set)", csFlags);
        }
        config.cpu_mode = singleCore ? CPUMode::SinglecoreInterpreter : CPUMode::MulticoreInterpreter;
        g_cpuMode.store(kCpuModeInterpreter);
        if (!g_recompilerRequested.load())
            snprintf(detail, sizeof(detail), "The recompiler is off in Settings, so the %s interpreter is running. Cores: %s.",
                singleCore ? "single-core" : "multi-core", cores.reason.c_str());
        else
            snprintf(detail, sizeof(detail), "The recompiler is on in Settings, but no JIT enabler is attached (cs_flags 0x%08x), "
                "so the %s interpreter is running. Launch through StikJIT, SideStore or LiveContainer to use the recompiler. Cores: %s.",
                csFlags, singleCore ? "single-core" : "multi-core", cores.reason.c_str());
        setCpuModeDetail(detail);
        return;
    }
    config.cpu_mode = singleCore ? CPUMode::SinglecoreRecompiler : CPUMode::MulticoreRecompiler;
    g_cpuMode.store(kCpuModeRecompiler);
    snprintf(detail, sizeof(detail), "A JIT enabler is attached, so the AArch64 recompiler runs this launch. Cores: %s.",
        cores.reason.c_str());
    setCpuModeDetail(detail);
}

void ios_apply_render_profile()
{
    auto& config = GetConfig();
    const bool accuracy = g_favourAccuracy.load();
    const bool performance = g_favourPerformance.load() && !accuracy;
    config.vk_accurate_barriers = accuracy;
    config.gx2drawdone_sync = accuracy;
    if (accuracy)
        config.async_compile = false;
    if (performance)
        config.async_compile = true;
    g_latteRelaxShaderMul.store(performance, std::memory_order_relaxed);
    const bool fullSpeed = g_fullSpeedRenders.load();
    if (fullSpeed)
        config.async_compile = g_fullSpeedShaderMode.load() == 1;
    g_latteFullSpeedRenders.store(fullSpeed, std::memory_order_relaxed);
    PerfTelemetry::DrawBreadcrumbsEnabled().store(!(performance || fullSpeed), std::memory_order_relaxed);
    if (fullSpeed)
        cemuLog_log(LogType::Force, "iOS: full speed renders - GPU thread user-interactive, presents paced to the game, new shaders {}",
            g_fullSpeedShaderMode.load() == 1 ? "in the background" : "waited for");
    cemuLog_log(LogType::Force, "iOS: {} - async shaders {}, accurate barriers {}, GX2DrawDone sync {}, strict shader mul {}",
        accuracy ? "favouring accuracy" : (performance ? "favouring performance" : "favouring speed"),
        config.async_compile.GetValue(), config.vk_accurate_barriers.GetValue(), config.gx2drawdone_sync.GetValue(),
        performance ? "off" : "per game profile");
}

}

namespace {
    std::atomic<bool> g_logTailRunning{false};

    void ios_log_tail_start()
    {
        if (g_logTailRunning.exchange(true))
            return;
        const std::string path = _pathToUtf8(ActiveSettings::GetUserDataPath("log.txt"));
        std::thread([path] {
            ios_bridge_thread_begin("EspressU log tail");
            long offset = 0;
            std::string partial;
            char chunk[4096];
            while (g_logTailRunning.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
                FILE* f = fopen(path.c_str(), "rb");
                if (!f)
                    continue;
                fseek(f, 0, SEEK_END);
                const long size = ftell(f);
                if (size < offset)
                {
                    offset = 0;
                    partial.clear();
                }
                fseek(f, offset, SEEK_SET);
                size_t n;
                while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
                {
                    offset += (long)n;
                    partial.append(chunk, n);
                    size_t nl;
                    while ((nl = partial.find('\n')) != std::string::npos)
                    {
                        std::string line = partial.substr(0, nl);
                        if (!line.empty() && line.back() == '\r')
                            line.pop_back();
                        ios_live_log_push(line.c_str());
                        partial.erase(0, nl + 1);
                    }
                }
                fclose(f);
            }
        }).detach();
    }
}

namespace {
    std::atomic<double> g_framesPerSecond{0.0};
    std::atomic<bool> g_statsRunning{false};

    void ios_stats_start()
    {
        if (g_statsRunning.exchange(true))
            return;
        std::thread([] {
            ios_bridge_thread_begin("EspressU FPS sampler");
            uint32 lastFrames = 0;
            auto lastTime = std::chrono::steady_clock::now();
            bool haveBaseline = false;
            while (g_statsRunning.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!g_titleRunning.load() || IOSTitlePause_IsPaused())
                {
                    g_framesPerSecond.store(0.0);
                    haveBaseline = false;
                    continue;
                }
                const uint32 frames = LatteGPUState.frameCounter;
                const auto now = std::chrono::steady_clock::now();
                if (haveBaseline && frames >= lastFrames)
                {
                    const double dt = std::chrono::duration<double>(now - lastTime).count();
                    if (dt > 0.0)
                        g_framesPerSecond.store((double)(frames - lastFrames) / dt);
                }
                lastFrames = frames;
                lastTime = now;
                haveBaseline = true;
            }
        }).detach();
    }
}

namespace {
    std::mutex g_perfLineMutex;
    std::string g_perfLine;

    struct PerfSnapshot
    {
        std::chrono::steady_clock::time_point at;
        uint32_t frames = 0, flips = 0;
        uint64_t ppcIdleNs = 0, gpuIdleNs = 0, gpuSyncNs = 0, drawableWaitNs = 0, mtlGpuNs = 0;
        uint32_t mtlCommandBuffers = 0, tvPresents = 0, padPresents = 0;
        uint32_t shaderCompiles = 0, pipelineCompiles = 0, pipelineSyncCompiles = 0;
        uint64_t shaderCompileNs = 0, pipelineCompileNs = 0;
        uint32_t textureDecodes = 0;
        uint64_t textureDecodeNs = 0;
        uint32_t textureDecodesByFormat[64] = {};
        uint64_t textureDecodeNsByFormat[64] = {};
        uint32_t renderPassBreaks[16] = {};
        uint32_t renderPassesStarted = 0;
        uint32_t imageCopyNotTouchingPass = 0, passReturnsToPrevious = 0;
        uint64_t guestWaitTimestampNs = 0, guestWaitVsyncNs = 0, guestWaitFlipNs = 0;
        uint32_t coreWakeups = 0;
        uint64_t coreWakeLatencyNs = 0;
        const char* waitReason[32] = {};
        uint64_t waitNs[32] = {};
        uint32_t waitCount[32] = {};
        uint32_t jitBlocks = 0, jitInvalidations = 0, jitArenaReleases = 0, jitArenaAllocFails = 0;
        uint64_t jitCompileNs = 0;
        uint32_t gpuDraws = 0, gpuTexHashes = 0;
        uint64_t gpuBeginNs = 0, gpuTexUpdateNs = 0, gpuTexHashNs = 0, gpuTexHashBytes = 0;
        uint64_t gpuExecNs = 0, gpuIndexNs = 0, gpuFlushNs = 0, gpuBufferNs = 0, gpuSnapshotCompareBytes = 0, gpuSnapshotCopyBytes = 0;
        uint64_t gpuPassNs = 0, gpuPipelineNs = 0, gpuBindNs = 0, gpuEndNs = 0;
    };

    PerfSnapshot ios_perf_take()
    {
        auto& c = PerfTelemetry::Get();
        PerfSnapshot s;
        s.at = std::chrono::steady_clock::now();
        s.frames = LatteGPUState.frameCounter;
        s.flips = LatteGPUState.flipCounter;
        s.ppcIdleNs = c.ppcIdleNs.load(std::memory_order_relaxed);
        s.gpuIdleNs = c.gpuIdleNs.load(std::memory_order_relaxed);
        s.gpuSyncNs = c.gpuSyncNs.load(std::memory_order_relaxed);
        s.drawableWaitNs = c.drawableWaitNs.load(std::memory_order_relaxed);
        s.mtlGpuNs = c.mtlGpuNs.load(std::memory_order_relaxed);
        s.mtlCommandBuffers = c.mtlCommandBuffers.load(std::memory_order_relaxed);
        s.tvPresents = c.tvPresents.load(std::memory_order_relaxed);
        s.padPresents = c.padPresents.load(std::memory_order_relaxed);
        s.shaderCompiles = c.shaderCompiles.load(std::memory_order_relaxed);
        s.shaderCompileNs = c.shaderCompileNs.load(std::memory_order_relaxed);
        s.pipelineCompiles = c.pipelineCompiles.load(std::memory_order_relaxed);
        s.pipelineCompileNs = c.pipelineCompileNs.load(std::memory_order_relaxed);
        s.pipelineSyncCompiles = c.pipelineSyncCompiles.load(std::memory_order_relaxed);
        s.textureDecodes = c.textureDecodes.load(std::memory_order_relaxed);
        s.textureDecodeNs = c.textureDecodeNs.load(std::memory_order_relaxed);
        for (int i = 0; i < 16; i++)
            s.renderPassBreaks[i] = c.renderPassBreaks[i].load(std::memory_order_relaxed);
        s.renderPassesStarted = c.renderPassesStarted.load(std::memory_order_relaxed);
        s.imageCopyNotTouchingPass = c.imageCopyNotTouchingPass.load(std::memory_order_relaxed);
        s.passReturnsToPrevious = c.passReturnsToPrevious.load(std::memory_order_relaxed);
        s.guestWaitTimestampNs = c.guestWaitTimestampNs.load(std::memory_order_relaxed);
        s.guestWaitVsyncNs = c.guestWaitVsyncNs.load(std::memory_order_relaxed);
        s.guestWaitFlipNs = c.guestWaitFlipNs.load(std::memory_order_relaxed);
        s.coreWakeups = c.coreWakeups.load(std::memory_order_relaxed);
        s.coreWakeLatencyNs = c.coreWakeLatencyNs.load(std::memory_order_relaxed);
        for (int i = 0; i < 32; i++)
        {
            const auto& t = LatteWait::WaitTotals()[i];
            s.waitReason[i] = t.reason.load(std::memory_order_relaxed);
            s.waitNs[i] = t.ns.load(std::memory_order_relaxed);
            s.waitCount[i] = t.count.load(std::memory_order_relaxed);
        }
        for (int i = 0; i < 64; i++)
        {
            s.textureDecodesByFormat[i] = c.textureDecodesByFormat[i].load(std::memory_order_relaxed);
            s.textureDecodeNsByFormat[i] = c.textureDecodeNsByFormat[i].load(std::memory_order_relaxed);
        }
        s.jitBlocks = c.jitBlocks.load(std::memory_order_relaxed);
        s.jitCompileNs = c.jitCompileNs.load(std::memory_order_relaxed);
        s.jitInvalidations = c.jitInvalidations.load(std::memory_order_relaxed);
        s.jitArenaReleases = c.jitArenaReleases.load(std::memory_order_relaxed);
        s.jitArenaAllocFails = c.jitArenaAllocFails.load(std::memory_order_relaxed);
        s.gpuDraws = c.gpuDraws.load(std::memory_order_relaxed);
        s.gpuTexHashes = c.gpuTexHashes.load(std::memory_order_relaxed);
        s.gpuBeginNs = c.gpuBeginNs.load(std::memory_order_relaxed);
        s.gpuTexUpdateNs = c.gpuTexUpdateNs.load(std::memory_order_relaxed);
        s.gpuTexHashNs = c.gpuTexHashNs.load(std::memory_order_relaxed);
        s.gpuTexHashBytes = c.gpuTexHashBytes.load(std::memory_order_relaxed);
        s.gpuExecNs = c.gpuExecNs.load(std::memory_order_relaxed);
        s.gpuIndexNs = c.gpuIndexNs.load(std::memory_order_relaxed);
        s.gpuFlushNs = c.gpuFlushNs.load(std::memory_order_relaxed);
        s.gpuBufferNs = c.gpuBufferNs.load(std::memory_order_relaxed);
        s.gpuSnapshotCompareBytes = c.gpuSnapshotCompareBytes.load(std::memory_order_relaxed);
        s.gpuSnapshotCopyBytes = c.gpuSnapshotCopyBytes.load(std::memory_order_relaxed);
        s.gpuPassNs = c.gpuPassNs.load(std::memory_order_relaxed);
        s.gpuPipelineNs = c.gpuPipelineNs.load(std::memory_order_relaxed);
        s.gpuBindNs = c.gpuBindNs.load(std::memory_order_relaxed);
        s.gpuEndNs = c.gpuEndNs.load(std::memory_order_relaxed);
        return s;
    }

    double ios_clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

    bool ios_fps_on_a_cap(double fps)
    {
        for (int divisor = 1; divisor <= 3; divisor++)
        {
            const double target = 60.0 / divisor;
            if (fps >= target * 0.95 && fps <= target * 1.05)
                return true;
        }
        return false;
    }

    void ios_perf_log_window(const PerfSnapshot& a, const PerfSnapshot& b, bool logHeader)
    {
        const double dt = std::chrono::duration<double>(b.at - a.at).count();
        if (dt <= 0.0)
            return;
        const double wallNs = dt * 1e9;
        const double frames = (double)(uint32_t)(b.frames - a.frames);
        const double flips = (double)(uint32_t)(b.flips - a.flips);
        const double hostPresents = (double)(uint32_t)(b.tvPresents - a.tvPresents);
        const double hostThreads = std::max<double>(1.0, PerfTelemetry::Get().ppcHostThreads.load(std::memory_order_relaxed));

        const double hostFps = hostPresents / dt;
        const double guestFps = frames / dt;
        const double vsyncRate = flips / dt;
        const double speedPct = vsyncRate / 60.0 * 100.0;
        const double ppcWait = ios_clamp01((double)(b.ppcIdleNs - a.ppcIdleNs) / (wallNs * hostThreads));
        const double ppcRun = 1.0 - ppcWait;
        const double gpuIdle = ios_clamp01((double)(b.gpuIdleNs - a.gpuIdleNs) / wallNs);
        const double gpuSync = ios_clamp01((double)(b.gpuSyncNs - a.gpuSyncNs) / wallNs);
        const double gpuThreadBusy = ios_clamp01(1.0 - gpuIdle - gpuSync);
        const double mtlBusy = ios_clamp01((double)(b.mtlGpuNs - a.mtlGpuNs) / wallNs);
        const double mtlMsPerFrame = frames > 0.0 ? (double)(b.mtlGpuNs - a.mtlGpuNs) / 1e6 / frames : 0.0;
        const double cbPerFrame = frames > 0.0 ? (double)(b.mtlCommandBuffers - a.mtlCommandBuffers) / frames : 0.0;
        const uint32_t shaders = b.shaderCompiles - a.shaderCompiles;
        const uint32_t pipelines = b.pipelineCompiles - a.pipelineCompiles;
        const uint32_t pipelinesOnGpuThread = b.pipelineSyncCompiles - a.pipelineSyncCompiles;
        const double shaderMs = (double)(b.shaderCompileNs - a.shaderCompileNs) / 1e6;
        const double pipelineMs = (double)(b.pipelineCompileNs - a.pipelineCompileNs) / 1e6;
        const uint32_t textureDecodes = b.textureDecodes - a.textureDecodes;
        const double textureDecodeMs = (double)(b.textureDecodeNs - a.textureDecodeNs) / 1e6;
        const uint32_t jitBlocks = b.jitBlocks - a.jitBlocks;
        const double jitMs = (double)(b.jitCompileNs - a.jitCompileNs) / 1e6;
        const int thermal = ios_thermal_state();

        int verdict;
        if (frames < 1.0)
            verdict = 7;
        else if (pipelinesOnGpuThread > 0 && pipelineMs > wallNs / 1e6 * 0.2)
            verdict = 6;
        else if (speedPct >= 97.0 && ios_fps_on_a_cap(guestFps))
            verdict = 1;
        else if (mtlBusy >= 0.85)
            verdict = 2;
        else if (ppcRun >= 0.85)
            verdict = 3;
        else if (gpuThreadBusy >= 0.85)
            verdict = 4;
        else
            verdict = 5;

        auto& summary = PerfTelemetry::GetSummary();
        summary.hostFps.store((float)hostFps, std::memory_order_relaxed);
        summary.guestFps.store((float)guestFps, std::memory_order_relaxed);
        summary.vsyncRate.store((float)vsyncRate, std::memory_order_relaxed);
        summary.ppcExecPct.store((float)(ppcRun * 100.0), std::memory_order_relaxed);
        summary.gpuThreadBusyPct.store((float)(gpuThreadBusy * 100.0), std::memory_order_relaxed);
        summary.mtlGpuMsPerFrame.store((float)mtlMsPerFrame, std::memory_order_relaxed);
        summary.mtlGpuBusyPct.store((float)(mtlBusy * 100.0), std::memory_order_relaxed);
        summary.bottleneck.store(verdict, std::memory_order_relaxed);
        summary.valid.store(true, std::memory_order_relaxed);

        if (logHeader)
        {
            cemuLog_log(LogType::Force, "perf: device {} | emulated cores on {} host thread(s) | async shaders {} | recompiler arena {} MB",
                cemu_bridge_device_summary(), (uint32_t)hostThreads, GetConfig().async_compile.GetValue() ? "on" : "off",
                (uint64_t)(PPCRecompiler_getJitArenaSize() / (1024 * 1024)));
        }
        if (logHeader)
        {
            std::string cpuDetail = getCpuModeDetail();
            cemuLog_log(LogType::Force, "perf: cpu path {} | {}", g_cpuMode.load() == kCpuModeRecompiler ? "recompiler" : "interpreter", cpuDetail);
        }
        cemuLog_log(LogType::Force,
            "perf {:.0f}s: fps host {:.1f} game {:.1f} vsync {:.1f} (speed {:.0f}%) | ppc thread run {:.0f}% wait {:.0f}% | "
            "gpu thread busy {:.0f}% idle {:.0f}% sync {:.0f}% (drawable {:.0f} ms) | metal gpu {:.1f} ms/frame ({:.0f}% busy, {:.1f} cb/frame) | "
            "compiled shaders {} ({:.0f} ms) pipelines {} ({:.0f} ms, {} on the gpu thread) | texture decodes {} ({:.0f} ms) | "
            "jit blocks {} ({:.0f} ms) invalidated {} arena released {} alloc-failed {} used {}/{} MB | thermal {} | limit: {}",
            dt, hostFps, guestFps, vsyncRate, speedPct, ppcRun * 100.0, ppcWait * 100.0,
            gpuThreadBusy * 100.0, gpuIdle * 100.0, gpuSync * 100.0, (double)(b.drawableWaitNs - a.drawableWaitNs) / 1e6,
            mtlMsPerFrame, mtlBusy * 100.0, cbPerFrame,
            shaders, shaderMs, pipelines, pipelineMs, pipelinesOnGpuThread, textureDecodes, textureDecodeMs,
            jitBlocks, jitMs, b.jitInvalidations - a.jitInvalidations, b.jitArenaReleases - a.jitArenaReleases, b.jitArenaAllocFails - a.jitArenaAllocFails,
            (uint64_t)(PPCRecompiler_getJitArenaUsed() / (1024 * 1024)), (uint64_t)(PPCRecompiler_getJitArenaSize() / (1024 * 1024)),
            ios_thermal_name(thermal), PerfTelemetry::BottleneckName(verdict));

        if (textureDecodeMs >= 50.0)
        {
            int order[64];
            for (int i = 0; i < 64; i++)
                order[i] = i;
            std::sort(order, order + 64, [&](int x, int y) {
                return (b.textureDecodeNsByFormat[x] - a.textureDecodeNsByFormat[x]) > (b.textureDecodeNsByFormat[y] - a.textureDecodeNsByFormat[y]);
            });
            std::string formats;
            for (int k = 0; k < 4; k++)
            {
                const int f = order[k];
                const uint64_t ns = b.textureDecodeNsByFormat[f] - a.textureDecodeNsByFormat[f];
                if (ns < 1000000)
                    break;
                formats += fmt::format("{}fmt {:02x} x{} {:.0f} ms", formats.empty() ? "" : " | ", f,
                    b.textureDecodesByFormat[f] - a.textureDecodesByFormat[f], (double)ns / 1e6);
            }
            cemuLog_log(LogType::Force, "perf textures {:.0f}s: {}", dt, formats);
        }

        if (frames >= 1.0)
        {
            auto br = [&](int i) { return (double)(b.renderPassBreaks[i] - a.renderPassBreaks[i]) / frames; };
            cemuLog_log(LogType::Force,
                "perf passes {:.0f}s: {:.0f} render passes/frame, ended by: attachments {:.0f} | texture upload {:.0f} | buffer upload {:.0f} | other blit {:.0f} | compute {:.0f} | copy pass {:.0f} | forced {:.0f} | commit {:.0f} | image copy {:.0f} | uniform resize {:.0f} | buffer copy {:.0f} | cache copy {:.0f} | streamout {:.0f} | readback {:.0f} | transcode copy {:.0f}",
                dt, (double)(b.renderPassesStarted - a.renderPassesStarted) / frames, br(0), br(1), br(2), br(3), br(4), br(5), br(6), br(7), br(8), br(9), br(10), br(11), br(12), br(13), br(14));
            {
                std::string waits;
                int order[32];
                for (int i = 0; i < 32; i++)
                    order[i] = i;
                std::sort(order, order + 32, [&](int x, int y) { return (b.waitNs[x] - a.waitNs[x]) > (b.waitNs[y] - a.waitNs[y]); });
                for (int k = 0; k < 6; k++)
                {
                    const int i = order[k];
                    const uint64_t ns = b.waitNs[i] - a.waitNs[i];
                    if (!b.waitReason[i] || ns < 1000000)
                        break;
                    waits += fmt::format("{}{} {:.1f} ms/frame (x{:.1f})", waits.empty() ? "" : " | ", b.waitReason[i], (double)ns / 1e6 / frames, (double)(b.waitCount[i] - a.waitCount[i]) / frames);
                }
                {
                    const uint32_t wakes = b.coreWakeups - a.coreWakeups;
                    const uint64_t wakeNs = b.coreWakeLatencyNs - a.coreWakeLatencyNs;
                    cemuLog_log(LogType::Force, "perf wakeups: {:.0f} emulated-core wake-ups/frame, {:.1f} us average, {:.2f} ms/frame in total, max since start {:.0f} us",
                        (double)wakes / frames, wakes ? (double)wakeNs / wakes / 1e3 : 0.0, (double)wakeNs / 1e6 / frames,
                        (double)PerfTelemetry::Get().coreWakeLatencyMaxNs.load(std::memory_order_relaxed) / 1e3);
                }
                cemuLog_log(LogType::Force, "perf waits: guest GX2WaitTimeStamp {:.1f} ms/frame, vsync {:.1f}, flip {:.1f} | gpu thread: {}",
                    (double)(b.guestWaitTimestampNs - a.guestWaitTimestampNs) / 1e6 / frames, (double)(b.guestWaitVsyncNs - a.guestWaitVsyncNs) / 1e6 / frames,
                    (double)(b.guestWaitFlipNs - a.guestWaitFlipNs) / 1e6 / frames, waits.empty() ? std::string("none") : waits);
            }
            cemuLog_log(LogType::Force, "perf passes detail: image copies not touching the open pass {:.0f}/frame | new passes returning to the attachments of the pass before last {:.0f}/frame",
                (double)(b.imageCopyNotTouchingPass - a.imageCopyNotTouchingPass) / frames, (double)(b.passReturnsToPrevious - a.passReturnsToPrevious) / frames);
        }

        {
            static double s_sinceJitMap = 0.0;
            s_sinceJitMap += dt;
            if (s_sinceJitMap >= 30.0)
            {
                s_sinceJitMap = 0.0;
                PPCRecompiler_DumpJitMap(ActiveSettings::GetUserDataPath("jitmap.txt"));
            }
        }

        {
            const double perFrame = frames > 0.0 ? 1.0 / (frames * 1e6) : 0.0;
            auto ms = [&](uint64_t x, uint64_t y) { return (double)(x - y) * perFrame; };
            const double busyMs = gpuThreadBusy * wallNs * perFrame;
            const double beginMs = ms(b.gpuBeginNs, a.gpuBeginNs);
            const double execMs = ms(b.gpuExecNs, a.gpuExecNs);
            const double endMs = ms(b.gpuEndNs, a.gpuEndNs);
            const double otherMs = std::max(0.0, busyMs - beginMs - execMs - endMs);
            const uint32_t draws = b.gpuDraws - a.gpuDraws;
            const double mbPerFrame = frames > 0.0 ? 1.0 / (frames * 1048576.0) : 0.0;
            cemuLog_log(LogType::Force,
                "perf gpu thread ms/frame {:.1f}: draws {:.0f}/frame | begin {:.1f} (textures {:.1f}, hashing {:.1f} = {} textures {:.1f} MB/frame) | "
                "execute {:.1f} (index {:.1f}, flush queue {:.1f}, buffers {:.1f} [compared {:.1f} MB/frame, copied {:.1f} MB/frame], render pass {:.1f}, pipeline {:.1f}, resource bind {:.1f}) | "
                "end/commit {:.1f} | other {:.1f}",
                busyMs, frames > 0.0 ? draws / frames : 0.0, beginMs, ms(b.gpuTexUpdateNs, a.gpuTexUpdateNs), ms(b.gpuTexHashNs, a.gpuTexHashNs),
                frames > 0.0 ? (uint32_t)((b.gpuTexHashes - a.gpuTexHashes) / frames) : 0u, (double)(b.gpuTexHashBytes - a.gpuTexHashBytes) * mbPerFrame,
                execMs, ms(b.gpuIndexNs, a.gpuIndexNs), ms(b.gpuFlushNs, a.gpuFlushNs), ms(b.gpuBufferNs, a.gpuBufferNs),
                (double)(b.gpuSnapshotCompareBytes - a.gpuSnapshotCompareBytes) * mbPerFrame, (double)(b.gpuSnapshotCopyBytes - a.gpuSnapshotCopyBytes) * mbPerFrame,
                ms(b.gpuPassNs, a.gpuPassNs), ms(b.gpuPipelineNs, a.gpuPipelineNs), ms(b.gpuBindNs, a.gpuBindNs), endMs, otherMs);
        }

        char line[200];
        snprintf(line, sizeof(line), "host %.1f game %.1f fps | ppc %.0f%% gpu thread %.0f%% metal %.1f ms | %s%s",
            hostFps, guestFps, ppcRun * 100.0, gpuThreadBusy * 100.0, mtlMsPerFrame, PerfTelemetry::BottleneckName(verdict),
            g_cpuMode.load() == kCpuModeRecompiler ? "" : (g_recompilerRequested.load() ? " | interpreter (no JIT enabler)" : " | interpreter (recompiler off)"));
        std::lock_guard<std::mutex> lock(g_perfLineMutex);
        g_perfLine = line;
    }

    std::atomic<bool> g_perfRunning{false};

    void ios_perf_start()
    {
        if (g_perfRunning.exchange(true))
            return;
        std::thread([] {
            ios_bridge_thread_begin("EspressU perf line");
            constexpr double kWindowSeconds = 5.0;
            PerfSnapshot baseline;
            bool haveBaseline = false;
            bool headerLogged = false;
            while (g_perfRunning.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                if (!g_titleRunning.load() || IOSTitlePause_IsPaused())
                {
                    haveBaseline = false;
                    headerLogged = false;
                    PerfTelemetry::GetSummary().valid.store(false, std::memory_order_relaxed);
                    continue;
                }
                if (!haveBaseline)
                {
                    baseline = ios_perf_take();
                    haveBaseline = true;
                    continue;
                }
                const PerfSnapshot now = ios_perf_take();
                if (std::chrono::duration<double>(now.at - baseline.at).count() < kWindowSeconds)
                    continue;
                ios_perf_log_window(baseline, now, !headerLogged);
                headerLogged = true;
                baseline = now;
            }
        }).detach();
    }
}

namespace {
    std::atomic<bool> g_videoStalled{false};
    std::atomic<int> g_videoStallKind{0};
    std::atomic<bool> g_stallWatchRunning{false};
    std::atomic<bool> g_appIsActive{true};
    std::atomic<uint32_t> g_stallResetGen{0};

    std::atomic<bool> g_cleanStartRequired{false};
    std::mutex g_cleanStartMutex;
    std::string g_cleanStartReason;

    void ios_require_clean_start(const std::string& reason)
    {
        std::lock_guard<std::mutex> lock(g_cleanStartMutex);
        if (g_cleanStartRequired.load())
            return;
        g_cleanStartReason = reason;
        g_cleanStartRequired.store(true);
        cemuLog_log(LogType::Force, "clean slate: a clean start is needed before the next title: {}", reason);
    }

    void ios_note_gpu_fault_before_reset()
    {
        auto& w = LatteWait::Get();
        if (w.gpuError.load())
            ios_require_clean_start("the GPU stopped running this app's work (error code " + std::to_string((int)w.gpuErrorCode.load()) + ") during the last game");
        else if (w.gpuPresumedLost.load())
            ios_require_clean_start("the GPU stopped responding during the last game");
    }

    void ios_check_clean_slate_after_stop()
    {
        if (!CafeSystem::CleanSlateHasDangerousLeftover())
            return;
        const std::vector<std::string> leftovers = CafeSystem::GetCleanSlateLeftovers();
        std::string reason = "the emulator could not fully reset after the last game";
        if (!leftovers.empty())
        {
            reason += " (" + leftovers.front();
            if (leftovers.size() > 1)
                reason += " and " + std::to_string(leftovers.size() - 1) + " more";
            reason += ")";
        }
        ios_require_clean_start(reason);
    }

    void ios_reset_video_stall_state()
    {
        ios_note_gpu_fault_before_reset();
        g_videoStalled.store(false);
        g_videoStallKind.store(0);
        auto& w = LatteWait::Get();
        w.gpuError.store(false);
        w.gpuErrorCode.store(0);
        w.gpuPresumedLost.store(false);
        w.memStatsValid.store(false);
        w.drawableFailuresInARow.store(0);
        w.cbErrorStreak.store(0);
        g_stallResetGen.fetch_add(1);
    }

    double ios_stall_thermal_scale(int* stateOut)
    {
        const NSInteger state = [[NSProcessInfo processInfo] thermalState];
        if (stateOut)
            *stateOut = (int)state;
        switch (state)
        {
        case NSProcessInfoThermalStateSerious: return 1.5;
        case NSProcessInfoThermalStateCritical: return 2.0;
        default: return 1.0;
        }
    }

    StallDetect::Sample ios_stall_make_sample(int* thermalState)
    {
        auto& w = LatteWait::Get();
        auto& info = WindowSystem::GetWindowInfo();
        StallDetect::Sample s;
        s.nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        s.titleRunning = g_titleRunning.load() && CafeSystem::IsTitleRunning() && !CafeSystem::IsTitleSwitchInProgress();
        s.gx2Init = LatteGPUState.gx2InitCalled > 0;
        s.paused = IOSTitlePause_IsPaused();
        s.appActive = g_appIsActive.load();
        uint32_t stamp = 17;
        for (uint32_t v : { (uint32_t)(int)info.width, (uint32_t)(int)info.height, (uint32_t)(int)info.phys_width, (uint32_t)(int)info.phys_height,
                            (uint32_t)((double)info.dpi_scale.load() * 100.0), (uint32_t)info.visible_outputs.load() })
            stamp = stamp * 31u + v;
        s.layoutStamp = stamp;
        s.windowScale = ios_stall_thermal_scale(thermalState);
        s.frames = LatteGPUState.frameCounter;
        s.presented = w.presentedFrames.load();
        s.drawableFailures = w.drawableFailures.load();
        s.drawableFailuresInARow = w.drawableFailuresInARow.load();
        s.cbRetired = w.cbRetired.load();
        s.cbErrorStreak = w.cbErrorStreak.load();
        s.pm4 = w.pm4Count.load();
        s.draws = (uint32_t)LatteGPUState.drawCallCounter;
        s.flipRequests = (uint32_t)LatteGPUState.flipRequestCount.load();
        s.evictionPasses = w.evictionPasses.load();
        s.gpuError = w.gpuError.load();
        s.gpuErrorCode = w.gpuErrorCode.load();
        s.lastCbErrorCode = w.cbLastErrorCode.load();
        s.gpuPresumedLost = w.gpuPresumedLost.load();
        s.waitClass = w.reason.load() ? (StallDetect::WaitClass)w.reasonKind.load() : StallDetect::WaitClass::None;
        static uint64_t s_memLimitBytes = 0;
        const uint64_t availBytes = (uint64_t)os_proc_available_memory();
        if (availBytes != 0)
        {
            uint64_t limit = availBytes + cemu_mem_footprint_bytes();
            static const uint64_t physBytes = cemu_sysctl_u64("hw.memsize");
            if (physBytes != 0 && limit > physBytes)
                limit = physBytes;
            if (limit > s_memLimitBytes)
                s_memLimitBytes = limit;
        }
        s.availMemMB = (uint32_t)(availBytes >> 20);
        s.memLimitMB = availBytes != 0 ? (uint32_t)(s_memLimitBytes >> 20) : 0;
        return s;
    }

    void ios_stall_log_snapshot(const StallDetect::Sample& s, int thermalState)
    {
        auto& w = LatteWait::Get();
        const char* reason = w.reason.load();
        const int64_t reasonMs = reason ? (LatteWait::NowMs() - w.reasonSinceMs.load()) : 0;
        const char* lastTimeout = w.lastTimeoutReason.load();
        auto& info = WindowSystem::GetWindowInfo();

        char surface[640];
        CemuUIKit_DescribeMainSurface(surface, sizeof(surface));

        if (reason)
            cemuLog_log(LogType::Force, "VIDEO STALL: GPU thread is blocked: {} (for {} ms, class {})", reason, reasonMs, StallDetect::WaitName(s.waitClass));
        else
            cemuLog_log(LogType::Force, "VIDEO STALL: GPU thread is not in a known wait (it is running, or stuck somewhere without a breadcrumb)");
        cemuLog_log(LogType::Force, "VIDEO STALL: frame {}, flips {}, draw calls {}, last PM4 opcode 0x{:02x}, GPU packets {}, guest flip requests {}, GX2Init calls {}",
            (uint32)LatteGPUState.frameCounter, (uint32)LatteGPUState.flipCounter, (uint32)LatteGPUState.drawCallCounter,
            w.lastPM4Opcode.load(), w.pm4Count.load(), (uint64)LatteGPUState.flipRequestCount.load(), (uint32)LatteGPUState.gx2InitCalled);
        cemuLog_log(LogType::Force, "VIDEO STALL: pending: {} occlusion queries, {} texture readbacks, {} command buffers on the GPU (submitted {}, finished {}, failed {}, failing in a row {}, last error code {})",
            w.queriesInFlight.load(), w.readbacksPending.load(), w.executingCommandBuffers.load(), w.cbSubmitted.load(), w.cbRetired.load(),
            w.erroredCommandBuffers.load(), w.cbErrorStreak.load(), w.cbLastErrorCode.load());
        cemuLog_log(LogType::Force, "VIDEO STALL: waits that gave up: {} (last: {}), GPU presumed lost: {}",
            w.timeouts.load(), lastTimeout ? lastTimeout : "none", w.gpuPresumedLost.load() ? "yes" : "no");
        cemuLog_log(LogType::Force, "VIDEO STALL: presented frames {}, TV drawable held: {}, drawable failures {} ({} in a row), TV drawable size {}x{}, layer device {}",
            w.presentedFrames.load(), w.tvDrawableHeld.load() ? "yes" : "no", w.drawableFailures.load(), w.drawableFailuresInARow.load(),
            w.tvDrawableWidth.load(), w.tvDrawableHeight.load(), w.tvLayerHasDevice.load() ? "set" : "unknown/missing");
        cemuLog_log(LogType::Force, "VIDEO STALL: free memory {} MB of a {} MB process limit, thermal state {} (x{:.1f}), app active {}, paused {}",
            s.availMemMB, s.memLimitMB, thermalState, s.windowScale, s.appActive ? "yes" : "no", s.paused ? "yes" : "no");
        cemuLog_log(LogType::Force, "VIDEO STALL: window {}x{} points at {:.2f}x scale ({}x{} px), visible outputs mask {}",
            (int)info.width, (int)info.height, (double)info.dpi_scale.load(), (int)info.phys_width, (int)info.phys_height, (uint32)info.visible_outputs.load());
        cemuLog_log(LogType::Force, "VIDEO STALL: TV view: {}", surface);
    }

    void ios_stall_log_decision(const StallDetect::Decision& d, const StallDetect::Sample& s, int thermalState)
    {
        using StallDetect::Action;
        switch (d.action)
        {
        case Action::Suspect:
            cemuLog_log(LogType::Force, "VIDEO STALL: suspected (not shown yet) - {}", d.text);
            break;
        case Action::SuspectDropped:
            cemuLog_log(LogType::Force, "VIDEO STALL: suspicion dropped - {}", d.text);
            break;
        case Action::Raise:
            cemuLog_log(LogType::Force, "VIDEO STALL: card shown (kind {}) - {}", (int)d.kind, d.text);
            ios_stall_log_snapshot(s, thermalState);
            break;
        case Action::Clear:
            cemuLog_log(LogType::Force, "VIDEO STALL: card dismissed - {}", d.text);
            break;
        case Action::GuestNote:
            cemuLog_log(LogType::Force, "VIDEO STALL: not a video freeze - {}", d.text);
            break;
        default:
            break;
        }
    }

    void ios_stall_watchdog_start()
    {
        if (g_stallWatchRunning.exchange(true))
            return;

        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidEnterBackgroundNotification object:nil queue:nil
            usingBlock:^(NSNotification*) { g_appIsActive.store(false); PerfTelemetry::AppInBackground().store(true); }];
        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:nil
            usingBlock:^(NSNotification*) { g_appIsActive.store(false); }];
        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:nil
            usingBlock:^(NSNotification*) { g_appIsActive.store(true); PerfTelemetry::AppInBackground().store(false); }];
        [[NSNotificationCenter defaultCenter] addObserverForName:UIApplicationWillEnterForegroundNotification object:nil queue:nil
            usingBlock:^(NSNotification*) { PerfTelemetry::AppInBackground().store(false); }];

        std::thread([] {
            ios_bridge_thread_begin("EspressU stall watch");
            StallDetect::Detector detector;
            uint32_t seenResetGen = g_stallResetGen.load();
            int64_t lastStillLogMs = 0;
            while (g_stallWatchRunning.load())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));

                int thermalState = 0;
                const StallDetect::Sample sample = ios_stall_make_sample(&thermalState);
                const uint32_t resetGen = g_stallResetGen.load();
                if (resetGen != seenResetGen)
                {
                    seenResetGen = resetGen;
                    detector.Reset(sample.nowMs, true);
                }

                const StallDetect::Decision stallDecision = detector.Update(sample);
                ios_stall_log_decision(stallDecision, sample, thermalState);
                const StallDetect::Decision memoryDecision = detector.UpdateMemory(sample);
                ios_stall_log_decision(memoryDecision, sample, thermalState);

                const int kind = (int)detector.kind();
                g_videoStallKind.store(kind);
                g_videoStalled.store(kind != 0);

                if (detector.stallRaised())
                {
                    if (lastStillLogMs == 0 || stallDecision.action == StallDetect::Action::Raise)
                        lastStillLogMs = sample.nowMs;
                    else if (sample.nowMs - lastStillLogMs >= 15000)
                    {
                        lastStillLogMs = sample.nowMs;
                        const char* reason = LatteWait::Get().reason.load();
                        cemuLog_log(LogType::Force, "VIDEO STALL: rule={} still in effect after {:.0f} s ({})", StallDetect::RuleName(detector.rule()),
                            (double)(sample.nowMs - detector.raisedAtMs()) / 1000.0, reason ? reason : "no known wait");
                    }
                }
                else
                {
                    lastStillLogMs = 0;
                }
            }
        }).detach();
    }
}

bool cemu_bridge_video_stalled(void) {
    return g_videoStalled.load();
}

int cemu_bridge_video_stall_kind(void) {
    return g_videoStalled.load() ? g_videoStallKind.load() : 0;
}

namespace {
    constexpr int kBitA = 0, kBitB = 1, kBitX = 2, kBitY = 3;
    constexpr int kBitL = 4, kBitR = 5, kBitZL = 6, kBitZR = 7;
    constexpr int kBitMinus = 8, kBitPlus = 9, kBitStickL = 10, kBitStickR = 11;
    constexpr int kBitUp = 16, kBitDown = 17, kBitLeft = 18, kBitRight = 19;

    std::mutex g_inputMutex;
    uint32_t g_touchButtons = 0;

    constexpr auto kMinTouchHold = std::chrono::milliseconds(50);
    uint32_t g_touchSeen = 0;
    uint32_t g_touchPendingRelease = 0;
    uint32_t g_touchRepress = 0;
    uint32_t g_touchRepressQueued = 0;
    uint32_t g_touchRepressReleased = 0;
    std::chrono::steady_clock::time_point g_touchPressedAt[32] = {};

    bool ios_touch_release_due(int bit, std::chrono::steady_clock::time_point now)
    {
        return (g_touchSeen & (1u << bit)) && now - g_touchPressedAt[bit] >= kMinTouchHold;
    }
    GCBridgeVec2 g_touchSticks[2] = {};
    uint32_t g_physicalButtons = 0;

    // Custom button mapping for the physical controller only. It is applied here, before touch and controller
    // input are merged, so the on-screen controls always keep the built-in layout. Indexed by
    // CemuBridgePadMapButton; each entry is the bit of the physical button that drives it, or -1 for the default.
    constexpr int kPadDefaultBit[17] = {
        -1, kBitA, kBitB, kBitX, kBitY, kBitL, kBitR, kBitZL, kBitZR, kBitPlus, kBitMinus,
        kBitUp, kBitDown, kBitLeft, kBitRight, kBitStickL, kBitStickR,
    };
    int g_padRemap[17] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
    bool g_padRemapActive = false;

    uint32_t ios_remap_physical(uint32_t physical)
    {
        if (!g_padRemapActive)
            return physical;
        uint32_t out = physical;
        for (int pad = 1; pad < 17; ++pad)
            out &= ~(1u << kPadDefaultBit[pad]);
        for (int pad = 1; pad < 17; ++pad)
        {
            const int source = g_padRemap[pad] >= 0 ? g_padRemap[pad] : kPadDefaultBit[pad];
            if (physical & (1u << source))
                out |= 1u << kPadDefaultBit[pad];
        }
        return out;
    }
    GCBridgeVec2 g_physicalSticks[2] = {};
    float g_physicalTriggers[2] = {};

    void* g_inputHandle = nullptr;
    GCController* g_boundController = nil;

    struct ExtraPad
    {
        GCController* controller = nil;
        void* handle = nullptr;
        GCBridgeControllerState state{};
    };
    std::vector<std::unique_ptr<ExtraPad>> g_extraPads;
    constexpr size_t kMaxExtraPads = 3;
    bool g_homeWarned = false;

    std::atomic<CemuMenuInputCallback> g_menuCallback{nullptr};
    std::atomic<bool> g_menuCapture{false};
    uint32_t g_physicalSuppress = 0;
    constexpr uint32_t kMenuUp = 1u << 0, kMenuDown = 1u << 1, kMenuConfirm = 1u << 2, kMenuBack = 1u << 3;
    constexpr float kMenuStickThreshold = 0.6f;
    uint32_t g_menuHeld = 0;
    bool g_homeHeld = false;

    int ios_button_bit(CemuBridgeButton button)
    {
        switch (button)
        {
        case CEMU_BRIDGE_BUTTON_A: return kBitA;
        case CEMU_BRIDGE_BUTTON_B: return kBitB;
        case CEMU_BRIDGE_BUTTON_X: return kBitX;
        case CEMU_BRIDGE_BUTTON_Y: return kBitY;
        case CEMU_BRIDGE_BUTTON_L: return kBitL;
        case CEMU_BRIDGE_BUTTON_R: return kBitR;
        case CEMU_BRIDGE_BUTTON_ZL: return kBitZL;
        case CEMU_BRIDGE_BUTTON_ZR: return kBitZR;
        case CEMU_BRIDGE_BUTTON_PLUS: return kBitPlus;
        case CEMU_BRIDGE_BUTTON_MINUS: return kBitMinus;
        case CEMU_BRIDGE_BUTTON_UP: return kBitUp;
        case CEMU_BRIDGE_BUTTON_DOWN: return kBitDown;
        case CEMU_BRIDGE_BUTTON_LEFT: return kBitLeft;
        case CEMU_BRIDGE_BUTTON_RIGHT: return kBitRight;
        case CEMU_BRIDGE_BUTTON_STICK_L: return kBitStickL;
        case CEMU_BRIDGE_BUTTON_STICK_R: return kBitStickR;
        default: return -1;
        }
    }

    GCBridgeControllerState ios_poll_state(void* context)
    {
        (void)context;
        std::lock_guard lock(g_inputMutex);
        const auto now = std::chrono::steady_clock::now();
        if (g_touchRepressQueued)
        {
            for (int bit = 0; bit < 32; ++bit)
                if (g_touchRepressQueued & (1u << bit))
                    g_touchPressedAt[bit] = now;
            g_touchButtons |= g_touchRepressQueued;
            g_touchSeen &= ~g_touchRepressQueued;
            g_touchPendingRelease |= g_touchRepressReleased & g_touchRepressQueued;
            g_touchRepressReleased &= ~g_touchRepressQueued;
            g_touchRepressQueued = 0;
        }
        if (g_touchPendingRelease)
        {
            for (int bit = 0; bit < 32; ++bit)
            {
                const uint32_t mask = 1u << bit;
                if ((g_touchPendingRelease & mask) && ios_touch_release_due(bit, now))
                {
                    g_touchButtons &= ~mask;
                    g_touchPendingRelease &= ~mask;
                    g_touchSeen &= ~mask;
                    if (g_touchRepress & mask)
                    {
                        g_touchRepress &= ~mask;
                        g_touchRepressQueued |= mask;
                    }
                }
            }
        }
        GCBridgeControllerState s{};
        const uint32_t physical = ios_remap_physical(g_physicalButtons);
        s.buttons = g_touchButtons | physical;
        g_touchSeen |= g_touchButtons;
        for (int i = 0; i < 2; i++)
        {
            const GCBridgeVec2& touch = g_touchSticks[i];
            const GCBridgeVec2 chosen = (touch.x != 0.0f || touch.y != 0.0f) ? touch : g_physicalSticks[i];
            (i == 0 ? s.leftStick : s.rightStick) = chosen;
        }
        const float leftTrigger = g_padRemap[CEMU_BRIDGE_PAD_ZL] >= 0 ? 0.0f : g_physicalTriggers[0];
        const float rightTrigger = g_padRemap[CEMU_BRIDGE_PAD_ZR] >= 0 ? 0.0f : g_physicalTriggers[1];
        s.leftTrigger = std::max(leftTrigger, ((g_touchButtons | physical) & (1u << kBitZL)) ? 1.0f : 0.0f);
        s.rightTrigger = std::max(rightTrigger, ((g_touchButtons | physical) & (1u << kBitZR)) ? 1.0f : 0.0f);
        return s;
    }

    GCBridgeMotionState ios_poll_motion(void* context)
    {
        (void)context;
        IOSMotionSample sample;
        IOSMotion_Poll(&sample);
        GCBridgeMotionState s{};
        s.accelerometer = GCBridgeVec3{sample.accelerometer[0], sample.accelerometer[1], sample.accelerometer[2]};
        s.gyroscope = GCBridgeVec3{sample.gyroscope[0], sample.gyroscope[1], sample.gyroscope[2]};
        s.timestamp = sample.timestamp;
        s.recenterCount = sample.recenterCount;
        return s;
    }

    void ios_update_physical(GCExtendedGamepad* pad)
    {
        uint32_t buttons = 0;
        auto bit = [&](BOOL pressed, int b) { if (pressed) buttons |= (1u << b); };
        bit(pad.buttonA.isPressed, kBitA);
        bit(pad.buttonB.isPressed, kBitB);
        bit(pad.buttonX.isPressed, kBitX);
        bit(pad.buttonY.isPressed, kBitY);
        bit(pad.leftShoulder.isPressed, kBitL);
        bit(pad.rightShoulder.isPressed, kBitR);
        bit(pad.leftTrigger.isPressed, kBitZL);
        bit(pad.rightTrigger.isPressed, kBitZR);
        bit(pad.buttonOptions ? pad.buttonOptions.isPressed : NO, kBitMinus);
        bit(pad.buttonMenu.isPressed, kBitPlus);
        bit(pad.leftThumbstickButton ? pad.leftThumbstickButton.isPressed : NO, kBitStickL);
        bit(pad.rightThumbstickButton ? pad.rightThumbstickButton.isPressed : NO, kBitStickR);
        bit(pad.dpad.up.isPressed, kBitUp);
        bit(pad.dpad.down.isPressed, kBitDown);
        bit(pad.dpad.left.isPressed, kBitLeft);
        bit(pad.dpad.right.isPressed, kBitRight);

        const bool capture = g_menuCapture.load();
        const float stickY = pad.leftThumbstick.yAxis.value;
        uint32_t menuNow = 0;
        if (pad.dpad.up.isPressed || stickY > kMenuStickThreshold) menuNow |= kMenuUp;
        if (pad.dpad.down.isPressed || stickY < -kMenuStickThreshold) menuNow |= kMenuDown;
        if (pad.buttonA.isPressed) menuNow |= kMenuConfirm;
        if (pad.buttonB.isPressed) menuNow |= kMenuBack;
        const uint32_t menuNew = menuNow & ~g_menuHeld;
        g_menuHeld = menuNow;
        bool touchpadNow = false;
        if (@available(iOS 14.5, *))
        {
            if ([pad isKindOfClass:[GCDualSenseGamepad class]])
                touchpadNow = ((GCDualSenseGamepad*)pad).touchpadButton.isPressed;
        }
        if ([pad isKindOfClass:[GCDualShockGamepad class]])
            touchpadNow = touchpadNow || ((GCDualShockGamepad*)pad).touchpadButton.isPressed;
        const bool comboNow = pad.buttonOptions && pad.buttonOptions.isPressed && pad.buttonMenu.isPressed;
        const bool homeNow = (pad.buttonHome ? pad.buttonHome.isPressed : NO) || touchpadNow || comboNow;
        const bool homeNew = homeNow && !g_homeHeld;
        g_homeHeld = homeNow;

        {
            std::lock_guard lock(g_inputMutex);
            if (capture)
            {
                g_physicalSuppress = buttons;
                g_physicalButtons = 0;
                g_physicalSticks[0] = g_physicalSticks[1] = GCBridgeVec2{};
                g_physicalTriggers[0] = g_physicalTriggers[1] = 0.0f;
            }
            else
            {
                g_physicalSuppress &= buttons;
                if (comboNow)
                    g_physicalSuppress |= buttons & ((1u << kBitMinus) | (1u << kBitPlus));
                g_physicalButtons = buttons & ~g_physicalSuppress;
                g_physicalSticks[0] = GCBridgeVec2{pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value};
                g_physicalSticks[1] = GCBridgeVec2{pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value};
                g_physicalTriggers[0] = (g_physicalSuppress & (1u << kBitZL)) ? 0.0f : pad.leftTrigger.value;
                g_physicalTriggers[1] = (g_physicalSuppress & (1u << kBitZR)) ? 0.0f : pad.rightTrigger.value;
            }
        }

        if (CemuMenuInputCallback callback = g_menuCallback.load())
        {
            if (homeNew)
                callback(CEMU_BRIDGE_MENU_HOME);
            if (capture)
            {
                if (menuNew & kMenuUp) callback(CEMU_BRIDGE_MENU_UP);
                if (menuNew & kMenuDown) callback(CEMU_BRIDGE_MENU_DOWN);
                if (menuNew & kMenuConfirm) callback(CEMU_BRIDGE_MENU_CONFIRM);
                if (menuNew & kMenuBack) callback(CEMU_BRIDGE_MENU_BACK);
            }
        }
    }

    void ios_clear_physical()
    {
        g_menuHeld = 0;
        g_homeHeld = false;
        std::lock_guard lock(g_inputMutex);
        g_physicalButtons = 0;
        g_physicalSuppress = 0;
        g_physicalSticks[0] = g_physicalSticks[1] = GCBridgeVec2{};
        g_physicalTriggers[0] = g_physicalTriggers[1] = 0.0f;
    }

    uint32_t ios_pad_buttons(GCExtendedGamepad* pad)
    {
        uint32_t buttons = 0;
        auto bit = [&](BOOL pressed, int b) { if (pressed) buttons |= (1u << b); };
        bit(pad.buttonA.isPressed, kBitA);
        bit(pad.buttonB.isPressed, kBitB);
        bit(pad.buttonX.isPressed, kBitX);
        bit(pad.buttonY.isPressed, kBitY);
        bit(pad.leftShoulder.isPressed, kBitL);
        bit(pad.rightShoulder.isPressed, kBitR);
        bit(pad.leftTrigger.isPressed, kBitZL);
        bit(pad.rightTrigger.isPressed, kBitZR);
        bit(pad.buttonOptions ? pad.buttonOptions.isPressed : NO, kBitMinus);
        bit(pad.buttonMenu.isPressed, kBitPlus);
        bit(pad.leftThumbstickButton ? pad.leftThumbstickButton.isPressed : NO, kBitStickL);
        bit(pad.rightThumbstickButton ? pad.rightThumbstickButton.isPressed : NO, kBitStickR);
        bit(pad.dpad.up.isPressed, kBitUp);
        bit(pad.dpad.down.isPressed, kBitDown);
        bit(pad.dpad.left.isPressed, kBitLeft);
        bit(pad.dpad.right.isPressed, kBitRight);
        return buttons;
    }

    GCBridgeControllerState ios_poll_extra(void* context)
    {
        std::lock_guard lock(g_inputMutex);
        return static_cast<ExtraPad*>(context)->state;
    }

    void ios_update_extra(ExtraPad* extra, GCExtendedGamepad* pad)
    {
        GCBridgeControllerState s{};
        s.buttons = ios_pad_buttons(pad);
        s.leftStick = GCBridgeVec2{pad.leftThumbstick.xAxis.value, pad.leftThumbstick.yAxis.value};
        s.rightStick = GCBridgeVec2{pad.rightThumbstick.xAxis.value, pad.rightThumbstick.yAxis.value};
        s.leftTrigger = pad.leftTrigger.value;
        s.rightTrigger = pad.rightTrigger.value;
        std::lock_guard lock(g_inputMutex);
        extra->state = s;
    }

    ExtraPad* ios_find_extra(GCController* controller)
    {
        for (auto& extra : g_extraPads)
            if (extra->controller == controller)
                return extra.get();
        return nullptr;
    }

    void ios_remove_extra(GCController* controller)
    {
        for (auto it = g_extraPads.begin(); it != g_extraPads.end(); ++it)
        {
            if ((*it)->controller != controller)
                continue;
            if ((*it)->controller.extendedGamepad)
                (*it)->controller.extendedGamepad.valueChangedHandler = nil;
            GCControllerBridge_remove((*it)->handle);
            g_extraPads.erase(it);
            GCControllerBridge_notifyChanged();
            return;
        }
    }

    void ios_bind_extra_controllers()
    {
        NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
        if (!g_inputHandle || ([defaults objectForKey:@"espressu.input.extraPads"] &&
                               ![defaults boolForKey:@"espressu.input.extraPads"]))
            return;
        const uint8_t type = [defaults integerForKey:@"espressu.input.extraPadType"] == 1
            ? (uint8_t)EmulatedController::Type::Classic : (uint8_t)EmulatedController::Type::Pro;
        bool added = false;
        for (GCController* controller in [GCController controllers])
        {
            if (controller == g_boundController || ios_find_extra(controller) || g_extraPads.size() >= kMaxExtraPads)
                continue;
            GCExtendedGamepad* pad = controller.extendedGamepad;
            if (!pad)
                continue;
            auto extra = std::make_unique<ExtraPad>();
            extra->controller = controller;
            GCBridgeControllerDesc desc{};
            desc.context = extra.get();
            desc.display_name = "EspressU Controller";
            desc.controllerType = type;
            desc.poll_state = ios_poll_extra;
            extra->handle = GCControllerBridge_add(&desc);
            if (!extra->handle)
                continue;
            ExtraPad* raw = extra.get();
            if (pad.buttonHome)
                pad.buttonHome.preferredSystemGestureState = GCSystemGestureStateDisabled;
            pad.valueChangedHandler = ^(GCExtendedGamepad* gamepad, GCControllerElement* element) {
                (void)element;
                ios_update_extra(raw, gamepad);
            };
            ios_update_extra(raw, pad);
            cemuLog_log(LogType::Force, "iOS input: extra controller {} bound as player {} ({})",
                controller.vendorName ? controller.vendorName.UTF8String : "MFi controller",
                g_extraPads.size() + 2, type == (uint8_t)EmulatedController::Type::Classic ? "Classic" : "Pro");
            g_extraPads.push_back(std::move(extra));
            added = true;
        }
        if (added)
            GCControllerBridge_notifyChanged();
    }

    void ios_bind_first_controller()
    {
        if (g_boundController)
            return;
        for (GCController* controller in [GCController controllers])
        {
            GCExtendedGamepad* pad = controller.extendedGamepad;
            if (!pad)
                continue;
            if (ios_find_extra(controller))
                ios_remove_extra(controller);
            g_boundController = controller;
            if (pad.buttonHome)
                pad.buttonHome.preferredSystemGestureState = GCSystemGestureStateDisabled;
            pad.valueChangedHandler = ^(GCExtendedGamepad* gamepad, GCControllerElement* element) {
                (void)element;
                ios_update_physical(gamepad);
            };
            ios_update_physical(pad);
            cemuLog_log(LogType::Force, "iOS input: physical controller bound to the GamePad: {}",
                controller.vendorName ? controller.vendorName.UTF8String : "MFi controller");
            ios_bind_extra_controllers();
            return;
        }
    }

    void ios_input_start()
    {
        if (g_inputHandle)
            return;
        GCBridgeControllerDesc desc{};
        desc.context = nullptr;
        desc.display_name = "EspressU GamePad";
        desc.controllerType = (uint8)EmulatedController::Type::VPAD;
        desc.poll_state = ios_poll_state;
        desc.poll_motion = ios_poll_motion;
        desc.rumble = nullptr;
        desc.release = nullptr;
        g_inputHandle = GCControllerBridge_add(&desc);
        if (!g_inputHandle)
        {
            cemu_bridge_log_checkpoint("iOS input: the core's GameController provider refused the GamePad - no input will reach titles");
            return;
        }
        GCControllerBridge_notifyChanged();
        IOSMotion_Start();
        cemu_bridge_log_checkpoint("iOS input: GamePad registered with the core (on-screen pad + first physical controller)");

        dispatch_async(dispatch_get_main_queue(), ^{
            NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
            [center addObserverForName:GCControllerDidConnectNotification object:nil queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification* note) {
                                (void)note;
                                ios_bind_first_controller();
                                ios_bind_extra_controllers();
                            }];
            [center addObserverForName:GCControllerDidDisconnectNotification object:nil queue:[NSOperationQueue mainQueue]
                            usingBlock:^(NSNotification* note) {
                                if ([note.object isKindOfClass:[GCController class]])
                                    ios_remove_extra((GCController*)note.object);
                                if (note.object == g_boundController)
                                {
                                    g_boundController = nil;
                                    ios_clear_physical();
                                    ios_bind_first_controller();
                                }
                            }];
            ios_bind_first_controller();
        });
    }

    UIWindow* ios_key_window()
    {
        for (UIScene* scene in [UIApplication sharedApplication].connectedScenes)
        {
            if (![scene isKindOfClass:[UIWindowScene class]])
                continue;
            for (UIWindow* window in ((UIWindowScene*)scene).windows)
                if (window.isKeyWindow)
                    return window;
        }
        return nil;
    }
}

namespace {

bool IOSShaderCacheFileMatches(const std::filesystem::path& file, unsigned long long titleId)
{
    if (titleId == 0)
        return true;
    char prefix[32];
    snprintf(prefix, sizeof(prefix), "%016llx", titleId);
    return file.filename().string().rfind(prefix, 0) == 0;
}

long long IOSShaderCacheSweep(const std::filesystem::path& dir, unsigned long long titleId, bool deleteThem)
{
    namespace sfs = std::filesystem;
    std::error_code ec;
    if (!sfs::exists(dir, ec))
        return 0;
    long long bytes = 0;
    for (auto& entry : sfs::directory_iterator(dir, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file(ec))
            continue;
        if (!IOSShaderCacheFileMatches(entry.path(), titleId))
            continue;
        std::error_code sizeEc;
        const auto size = sfs::file_size(entry.path(), sizeEc);
        if (sizeEc)
            continue;
        if (deleteThem)
        {
            std::error_code rmEc;
            if (!sfs::remove(entry.path(), rmEc) || rmEc)
                continue;
        }
        bytes += (long long)size;
    }
    return bytes;
}

}

long long cemu_bridge_clear_shader_cache(unsigned long long titleId, bool includeLearned) {
    if (cemu_bridge_is_title_running()) {
        cemuLog_log(LogType::Force, "Shader cache: refusing to clear while a title is running");
        return -1;
    }
    long long freed = IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/precompiled"), titleId, true);
    freed += IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/driver/vk"), titleId, true);
    if (includeLearned)
    {
        freed += IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/transferable"), titleId, true);
        freed += IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/backup"), titleId, true);
    }
    cemuLog_log(LogType::Force, "Shader cache: cleared {} bytes ({})", freed, includeLearned ? "compiled and learned" : "compiled only");
    return freed;
}

int cemu_bridge_shader_cache_stats(unsigned long long titleId, long long* outLearnedBytes, long long* outCompiledBytes) {
    if (outLearnedBytes)
        *outLearnedBytes = IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/transferable"), titleId, false);
    if (outCompiledBytes)
        *outCompiledBytes = IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/precompiled"), titleId, false)
            + IOSShaderCacheSweep(ActiveSettings::GetCachePath("shaderCache/driver/vk"), titleId, false);
    return 0;
}

namespace {

constexpr uint32_t kLegacyShaderCacheStamp = 2;

bool IOSCacheEntryIsShader(const std::vector<uint8>& data)
{
    return data.size() >= 17 && (data[0] & 0xF) == 1 && (data[0] >> 4) <= 2;
}

void IOSCacheInspect(unsigned long long titleId, const char* path, CemuCacheFileInfo* info)
{
    *info = CemuCacheFileInfo{};
    info->status = CEMU_CACHE_STATUS_NOT_A_CACHE;
    if (!path)
        return;
    FileCache* cache = FileCache::OpenReadOnly(std::filesystem::path(path));
    if (!cache)
        return;
    info->stamp = cache->GetExtraVersion();
    const uint32_t shaderStamp = LatteShaderCache_getShaderCacheExtraVersion(titleId);
    const uint32_t pipelineStamp = LatteShaderCache_getPipelineCacheExtraVersion(titleId);
    if (info->stamp == shaderStamp || info->stamp == kLegacyShaderCacheStamp)
        info->kind = CEMU_CACHE_FILE_SHADERS;
    else if (info->stamp == pipelineStamp)
        info->kind = CEMU_CACHE_FILE_PIPELINE;
    else
    {
        info->status = CEMU_CACHE_STATUS_OTHER_GAME;
        delete cache;
        return;
    }
    int legacyFormat = 0;
    std::vector<uint8> data;
    const int maxIndex = cache->GetMaximumFileIndex();
    for (int i = 0; i < maxIndex; i++)
    {
        uint64 name1 = 0, name2 = 0;
        if (!cache->GetFileByIndex(i, &name1, &name2, data))
            continue;
        info->entryCount++;
        if (info->kind == CEMU_CACHE_FILE_PIPELINE)
            info->usableCount++;
        else if (IOSCacheEntryIsShader(data))
            info->usableCount++;
        else if (!data.empty() && data[0] == 0x08)
            legacyFormat++;
    }
    info->damagedCount = (int)cache->GetDamagedEntryCount();
    if (info->usableCount > 0)
        info->status = CEMU_CACHE_STATUS_OK;
    else
        info->status = legacyFormat > 0 ? CEMU_CACHE_STATUS_OLD_FORMAT : CEMU_CACHE_STATUS_NOTHING_USABLE;
    delete cache;
}

}

void cemu_bridge_cache_file_inspect(unsigned long long titleId, const char* path, CemuCacheFileInfo* outInfo) {
    if (!outInfo)
        return;
    IOSCacheInspect(titleId, path, outInfo);
}

int cemu_bridge_cache_stamp_kind(unsigned long long titleId, unsigned int stamp) {
    if (stamp == LatteShaderCache_getShaderCacheExtraVersion(titleId))
        return CEMU_CACHE_FILE_SHADERS;
    if (stamp == LatteShaderCache_getPipelineCacheExtraVersion(titleId))
        return CEMU_CACHE_FILE_PIPELINE;
    return CEMU_CACHE_FILE_UNKNOWN;
}

int cemu_bridge_cache_file_import(unsigned long long titleId, const char* path, int kind, int renderer, bool pipelineIsVulkan,
                                  int* outAdded, int* outAlreadyThere, int* outSkipped) {
    if (outAdded) *outAdded = 0;
    if (outAlreadyThere) *outAlreadyThere = 0;
    if (outSkipped) *outSkipped = 0;
    if (cemu_bridge_is_title_running()) {
        cemuLog_log(LogType::Force, "Cache import: refusing while a title is running");
        return CEMU_CACHE_STATUS_TITLE_RUNNING;
    }
    CemuCacheFileInfo info;
    IOSCacheInspect(titleId, path, &info);
    if (info.status != CEMU_CACHE_STATUS_OK)
        return info.status;
    if (info.kind != kind)
        return CEMU_CACHE_STATUS_NOT_A_CACHE;

    std::filesystem::path destPath;
    uint32_t destStamp;
    if (kind == CEMU_CACHE_FILE_SHADERS) {
        destPath = renderer == 1 ? ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_shaders.bin", titleId)
                                 : ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_mtlshaders.bin", titleId);
        destStamp = LatteShaderCache_getShaderCacheExtraVersion(titleId);
    } else {
        destPath = pipelineIsVulkan ? ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_vkpipeline.bin", titleId)
                                    : ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_mtlpipeline.bin", titleId);
        destStamp = LatteShaderCache_getPipelineCacheExtraVersion(titleId);
    }
    std::error_code ec;
    std::filesystem::create_directories(destPath.parent_path(), ec);

    FileCache* dest = nullptr;
    if (std::filesystem::exists(destPath, ec)) {
        dest = FileCache::Open(destPath);
        if (!dest) {
            cemuLog_log(LogType::Force, "Cache import: \"{}\" can't be opened, leaving it as it is", destPath.filename().string());
            return -1;
        }
        const uint32_t have = dest->GetExtraVersion();
        const bool ok = have == destStamp || (kind == CEMU_CACHE_FILE_SHADERS && have == kLegacyShaderCacheStamp);
        if (!ok) {
            delete dest;
            return -2;
        }
    } else {
        dest = FileCache::Create(destPath, destStamp);
        if (!dest)
            return -1;
    }
    dest->UseCompression(false);

    FileCache* source = FileCache::OpenReadOnly(std::filesystem::path(path));
    if (!source) {
        delete dest;
        return CEMU_CACHE_STATUS_NOT_A_CACHE;
    }
    int added = 0, already = 0, skipped = 0;
    std::vector<uint8> data;
    const int maxIndex = source->GetMaximumFileIndex();
    for (int i = 0; i < maxIndex; i++) {
        uint64 name1 = 0, name2 = 0;
        if (!source->GetFileByIndex(i, &name1, &name2, data))
            continue;
        if (kind == CEMU_CACHE_FILE_SHADERS && !IOSCacheEntryIsShader(data)) {
            skipped++;
            continue;
        }
        if (dest->HasFile({ name1, name2 })) {
            already++;
            continue;
        }
        if (!dest->AddFile({ name1, name2 }, data.data(), (sint32)data.size())) {
            skipped++;
            continue;
        }
        added++;
    }
    skipped += (int)source->GetDamagedEntryCount() + info.damagedCount;
    delete source;
    delete dest;
    cemuLog_log(LogType::Force, "Cache import: {} added, {} already there, {} skipped ({})", added, already, skipped, destPath.filename().string());
    if (outAdded) *outAdded = added;
    if (outAlreadyThere) *outAlreadyThere = already;
    if (outSkipped) *outSkipped = skipped;
    return CEMU_CACHE_STATUS_OK;
}

void cemu_bridge_set_async_shader_compile(bool enabled) {
    GetConfig().async_compile = enabled;
}

bool cemu_bridge_async_shader_compile(void) {
    return GetConfig().async_compile.GetValue();
}

void cemu_bridge_set_stretch_to_fill(bool enabled) {
    GetConfig().fullscreen_scaling = enabled ? (sint32)kStretch : (sint32)kKeepAspectRatio;
}

static std::mutex g_launchNoticeMutex;
static std::string g_launchNotice;

static void ios_set_launch_notice(const std::string& notice) {
    std::lock_guard<std::mutex> lock(g_launchNoticeMutex);
    g_launchNotice = notice;
}

const char* cemu_bridge_take_launch_notice(void) {
    static thread_local std::string snapshot;
    std::lock_guard<std::mutex> lock(g_launchNoticeMutex);
    snapshot = g_launchNotice;
    g_launchNotice.clear();
    return snapshot.c_str();
}

static NSString* const kVulkanFailedBuildKey = @"espressu.render.vulkanFailedBuild";
static NSString* const kGraphicsAPIKey = @"espressu.render.graphicsAPI";
static NSString* const kVulkanFailureReasonKey = @"espressu.render.vulkanFailureReason";

static void ios_migrate_renderer_setting_to_metal() {
    [[NSUserDefaults standardUserDefaults] setInteger:(NSInteger)kMetal forKey:kGraphicsAPIKey];
}

static void ios_report_renderer_fallback() {
    char why[256];
    if (!CemuTakeRendererFallback(why, sizeof(why)))
        return;
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[NSString stringWithUTF8String:g_activeMoltenVK.c_str()] forKey:kVulkanFailedBuildKey];
    [defaults setObject:[NSString stringWithFormat:@"could not start: %s", why] forKey:kVulkanFailureReasonKey];
    ios_migrate_renderer_setting_to_metal();
    cemu_bridge_log_checkpoint((std::string("Renderer: Vulkan could not start (") + why + "). This launch uses Metal, and the saved renderer is now Metal.").c_str());
    ios_set_launch_notice("Vulkan couldn't start on this iPad, so this game is using Metal. The renderer setting is back on Metal.");
}

void cemu_bridge_set_graphics_api(int api) {
    if (api == (int)kVulkan) {
        NSString* failedBuild = [[NSUserDefaults standardUserDefaults] stringForKey:kVulkanFailedBuildKey];
        if (failedBuild.length > 0 && failedBuild.UTF8String && g_activeMoltenVK == failedBuild.UTF8String) {
            cemu_bridge_log_checkpoint(("Renderer: Vulkan failed to start on this device with MoltenVK " + g_activeMoltenVK
                + " earlier, so the saved Vulkan choice is replaced with Metal").c_str());
            ios_migrate_renderer_setting_to_metal();
            ios_set_launch_notice("Vulkan didn't start on this iPad last time, so EspressU is using Metal. You can try Vulkan again in Settings > Graphics.");
            api = (int)kMetal;
        }
    }
    GetConfig().graphic_api = (api == (int)kVulkan) ? kVulkan : kMetal;
}

int cemu_bridge_graphics_api(void) {
    return (int)GetConfig().graphic_api.GetValue();
}

void cemu_bridge_set_upscale_filter(int filter) {
    if (filter >= kLinearFilter && filter <= kNearestNeighborFilter)
        GetConfig().upscale_filter = (sint32)filter;
}

void cemu_bridge_set_downscale_filter(int filter) {
    if (filter >= kLinearFilter && filter <= kNearestNeighborFilter)
        GetConfig().downscale_filter = (sint32)filter;
}

void cemu_bridge_set_framebuffer_fetch(bool enabled) {
    GetConfig().framebuffer_fetch = enabled;
}

bool cemu_bridge_framebuffer_fetch(void) {
    return GetConfig().framebuffer_fetch.GetValue();
}

void cemu_bridge_set_render_upside_down(bool enabled) {
    GetConfig().render_upside_down = enabled;
}

bool cemu_bridge_render_upside_down(void) {
    return GetConfig().render_upside_down.GetValue();
}

void cemu_bridge_set_display_gamma(float gamma) {
    if (gamma <= 0.0f) {
        GetConfig().userDisplayGamma = 0.0f;
        return;
    }
    if (gamma < 1.0f)
        gamma = 1.0f;
    else if (gamma > 3.0f)
        gamma = 3.0f;
    GetConfig().userDisplayGamma = gamma;
}

float cemu_bridge_display_gamma(void) {
    return GetConfig().userDisplayGamma.GetValue();
}

void cemu_bridge_set_override_app_gamma(bool enabled) {
    GetConfig().overrideAppGammaPreference = enabled;
}

bool cemu_bridge_override_app_gamma(void) {
    return GetConfig().overrideAppGammaPreference.GetValue();
}

void cemu_bridge_set_override_gamma_value(float gamma) {
    if (gamma < 0.0f)
        gamma = 2.2f;
    GetConfig().overrideGammaValue = gamma;
}

float cemu_bridge_override_gamma_value(void) {
    return GetConfig().overrideGammaValue.GetValue();
}

void cemu_bridge_set_overlay_position(int position) {
    if (position >= (int)ScreenPosition::kDisabled && position <= (int)ScreenPosition::kBottomRight)
        GetConfig().overlay.position = (ScreenPosition)position;
}

int cemu_bridge_overlay_position(void) {
    return (int)GetConfig().overlay.position;
}

void cemu_bridge_set_overlay_fps(bool enabled) {
    GetConfig().overlay.fps = enabled;
}

bool cemu_bridge_overlay_fps(void) {
    return GetConfig().overlay.fps;
}

void cemu_bridge_set_overlay_cpu_usage(bool enabled) {
    GetConfig().overlay.cpu_usage = enabled;
}

bool cemu_bridge_overlay_cpu_usage(void) {
    return GetConfig().overlay.cpu_usage;
}

void cemu_bridge_set_overlay_ram_usage(bool enabled) {
    GetConfig().overlay.ram_usage = enabled;
}

bool cemu_bridge_overlay_ram_usage(void) {
    return GetConfig().overlay.ram_usage;
}

void cemu_bridge_set_overlay_text_color(uint32_t color) {
    GetConfig().overlay.text_color = color;
}

uint32_t cemu_bridge_overlay_text_color(void) {
    return GetConfig().overlay.text_color;
}

void cemu_bridge_set_overlay_text_scale(int scale) {
    GetConfig().overlay.text_scale = (sint32)std::clamp(scale, 50, 200);
}

int cemu_bridge_overlay_text_scale(void) {
    return GetConfig().overlay.text_scale;
}

void cemu_bridge_set_overlay_cpu_mode(bool enabled) {
    GetConfig().overlay.cpu_mode = enabled;
}

bool cemu_bridge_overlay_cpu_mode(void) {
    return GetConfig().overlay.cpu_mode;
}

void cemu_bridge_set_overlay_drawcalls(bool enabled) {
    GetConfig().overlay.drawcalls = enabled;
}

bool cemu_bridge_overlay_drawcalls(void) {
    return GetConfig().overlay.drawcalls;
}

void cemu_bridge_set_overlay_cpu_per_core_usage(bool enabled) {
    GetConfig().overlay.cpu_per_core_usage = enabled;
}

bool cemu_bridge_overlay_cpu_per_core_usage(void) {
    return GetConfig().overlay.cpu_per_core_usage;
}

void cemu_bridge_set_overlay_vram_usage(bool enabled) {
    GetConfig().overlay.vram_usage = enabled;
}

bool cemu_bridge_overlay_vram_usage(void) {
    return GetConfig().overlay.vram_usage;
}

void cemu_bridge_set_overlay_debug(bool enabled) {
    GetConfig().overlay.debug = enabled;
}

bool cemu_bridge_overlay_debug(void) {
    return GetConfig().overlay.debug;
}

void cemu_bridge_set_notification_position(int position) {
    if (position >= (int)ScreenPosition::kDisabled && position <= (int)ScreenPosition::kBottomRight)
        GetConfig().notification.position = (ScreenPosition)position;
}

int cemu_bridge_notification_position(void) {
    return (int)GetConfig().notification.position;
}

void cemu_bridge_set_notification_text_color(uint32_t color) {
    GetConfig().notification.text_color = color;
}

uint32_t cemu_bridge_notification_text_color(void) {
    return GetConfig().notification.text_color;
}

void cemu_bridge_set_notification_text_scale(int scale) {
    GetConfig().notification.text_scale = (sint32)std::clamp(scale, 50, 200);
}

int cemu_bridge_notification_text_scale(void) {
    return GetConfig().notification.text_scale;
}

void cemu_bridge_set_notification_controller_profiles(bool enabled) {
    GetConfig().notification.controller_profiles = enabled;
}

bool cemu_bridge_notification_controller_profiles(void) {
    return GetConfig().notification.controller_profiles;
}

void cemu_bridge_set_notification_controller_battery(bool enabled) {
    GetConfig().notification.controller_battery = enabled;
}

bool cemu_bridge_notification_controller_battery(void) {
    return GetConfig().notification.controller_battery;
}

void cemu_bridge_set_notification_shader_compiling(bool enabled) {
    GetConfig().notification.shader_compiling = enabled;
}

bool cemu_bridge_notification_shader_compiling(void) {
    return GetConfig().notification.shader_compiling;
}

void cemu_bridge_set_notification_friends(bool enabled) {
    GetConfig().notification.friends = enabled;
}

bool cemu_bridge_notification_friends(void) {
    return GetConfig().notification.friends;
}

void cemu_bridge_set_native_overlay(bool enabled) {
    g_overlayNativeOnIOS.store(enabled, std::memory_order_relaxed);
}

bool cemu_bridge_native_overlay(void) {
    return g_overlayNativeOnIOS.load(std::memory_order_relaxed);
}

const char* cemu_bridge_native_overlay_text(void) {
    static thread_local std::string text;
    text.clear();
    if (!g_overlayNativeOnIOS.load(std::memory_order_relaxed))
        return text.c_str();

    std::vector<std::string> stats;
    std::vector<std::string> notifications;
    LatteOverlay_CollectNativeText(stats, notifications);

    for (size_t i = 0; i < stats.size(); ++i) {
        if (i != 0)
            text += '\n';
        text += stats[i];
    }
    for (const std::string& notification : notifications) {
        text += '\x1e';
        text += notification;
    }
    return text.c_str();
}

void cemu_bridge_set_tv_audio_enabled(bool enabled) {
    GetConfig().tv_audio_enabled = enabled;
}

bool cemu_bridge_tv_audio_enabled(void) {
    return GetConfig().tv_audio_enabled;
}

void cemu_bridge_set_tv_volume(int volume) {
    GetConfig().tv_volume = std::clamp(volume, 0, 100);
    std::shared_lock lock(g_audioMutex);
    if (g_tvAudio)
        g_tvAudio->SetVolume(GetConfig().tv_volume);
}

int cemu_bridge_tv_volume(void) {
    return GetConfig().tv_volume;
}

void cemu_bridge_set_tv_channels(int channels) {
    if (channels >= kMono && channels <= kSurround)
        GetConfig().tv_channels = (AudioChannels)channels;
}

int cemu_bridge_tv_channels(void) {
    return (int)GetConfig().tv_channels;
}

void cemu_bridge_set_pad_audio_enabled(bool enabled) {
    GetConfig().pad_audio_enabled = enabled;
}

bool cemu_bridge_pad_audio_enabled(void) {
    return GetConfig().pad_audio_enabled;
}

void cemu_bridge_set_pad_volume(int volume) {
    GetConfig().pad_volume = std::clamp(volume, 0, 100);
    std::shared_lock lock(g_audioMutex);
    if (g_padAudio)
        g_padAudio->SetVolume(GetConfig().pad_volume);
}

int cemu_bridge_pad_volume(void) {
    return GetConfig().pad_volume;
}

void cemu_bridge_set_pad_channels(int channels) {
    if (channels >= kMono && channels <= kSurround)
        GetConfig().pad_channels = (AudioChannels)channels;
}

int cemu_bridge_pad_channels(void) {
    return (int)GetConfig().pad_channels;
}

void cemu_bridge_set_microphone_enabled(bool enabled) {
    GetConfig().microphone_enabled = enabled;
}

bool cemu_bridge_microphone_enabled(void) {
    return GetConfig().microphone_enabled;
}

void cemu_bridge_set_input_volume(int volume) {
    GetConfig().input_volume = std::clamp(volume, 0, 100);
}

int cemu_bridge_input_volume(void) {
    return GetConfig().input_volume;
}

void mic_setBlow(bool isBlowing);
bool mic_isBlowing();

void cemu_bridge_set_mic_blow(bool blowing) {
    mic_setBlow(blowing);
}

bool cemu_bridge_mic_blow(void) {
    return mic_isBlowing();
}

void cemu_bridge_set_vsync_enabled(bool enabled) {
    GetConfig().vsync = enabled ? 1 : 0;
}

bool cemu_bridge_vsync_enabled(void) {
    return GetConfig().vsync.GetValue() != 0;
}

void cemu_bridge_set_recompiler_enabled(bool enabled) {
    g_recompilerRequested.store(enabled);
    if (g_initialized.load())
        ios_apply_cpu_mode();
}

bool cemu_bridge_recompiler_enabled(void) {
    return g_recompilerRequested.load();
}

void cemu_bridge_set_favour_accuracy(bool enabled) {
    g_favourAccuracy.store(enabled);
    if (g_initialized.load())
        ios_apply_cpu_mode();
}

bool cemu_bridge_favour_accuracy(void) {
    return g_favourAccuracy.load();
}

void cemu_bridge_set_favour_performance(bool enabled) {
    g_favourPerformance.store(enabled);
}

bool cemu_bridge_favour_performance(void) {
    return g_favourPerformance.load();
}

void cemu_bridge_set_full_speed_renders(bool enabled, int shaderMode) {
    g_fullSpeedRenders.store(enabled);
    g_fullSpeedShaderMode.store(shaderMode == 1 ? 1 : 0);
}

bool cemu_bridge_full_speed_renders(void) {
    return g_fullSpeedRenders.load();
}

double cemu_bridge_device_temperature_celsius(void) {
    static dispatch_once_t once;
    static void* ioKitHandle = nullptr;
    static uint32_t (*fnServiceGetMatchingService)(uint32_t, CFDictionaryRef) = nullptr;
    static CFMutableDictionaryRef (*fnServiceMatching)(const char*) = nullptr;
    static CFTypeRef (*fnRegistryEntryCreateCFProperty)(uint32_t, CFStringRef, CFAllocatorRef, uint32_t) = nullptr;
    static int (*fnObjectRelease)(uint32_t) = nullptr;

    dispatch_once(&once, ^{
        ioKitHandle = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit", RTLD_LAZY);
        if (!ioKitHandle)
            return;
        fnServiceGetMatchingService = (uint32_t (*)(uint32_t, CFDictionaryRef))dlsym(ioKitHandle, "IOServiceGetMatchingService");
        fnServiceMatching = (CFMutableDictionaryRef (*)(const char*))dlsym(ioKitHandle, "IOServiceMatching");
        fnRegistryEntryCreateCFProperty = (CFTypeRef (*)(uint32_t, CFStringRef, CFAllocatorRef, uint32_t))dlsym(ioKitHandle, "IORegistryEntryCreateCFProperty");
        fnObjectRelease = (int (*)(uint32_t))dlsym(ioKitHandle, "IOObjectRelease");
    });

    if (!fnServiceGetMatchingService || !fnServiceMatching || !fnRegistryEntryCreateCFProperty)
        return NAN;

    static const char* kServices[] = { "AppleSmartBattery", "IOPMPowerSource" };
    for (const char* service : kServices)
    {
        CFMutableDictionaryRef match = fnServiceMatching(service);
        if (!match)
            continue;
        uint32_t entry = fnServiceGetMatchingService(0  , match);
        if (!entry)
            continue;
        CFTypeRef value = fnRegistryEntryCreateCFProperty(entry, CFSTR("Temperature"), kCFAllocatorDefault, 0);
        if (fnObjectRelease)
            fnObjectRelease(entry);
        if (!value)
            continue;
        double celsius = NAN;
        if (CFGetTypeID(value) == CFNumberGetTypeID())
        {
            int32_t raw = 0;
            if (CFNumberGetValue((CFNumberRef)value, kCFNumberSInt32Type, &raw))
            {
                if (raw > 1000)      celsius = raw / 100.0;
                else if (raw > 100)  celsius = raw / 10.0;
                else                 celsius = (double)raw;
            }
        }
        CFRelease(value);
        if (!std::isnan(celsius) && celsius > -20.0 && celsius < 120.0)
            return celsius;
    }
    return NAN;
}

void cemu_bridge_set_thermal_throttle_micros(uint32_t micros) {
    coreinit::OSSetThermalThrottleMicros(micros);
}

void cemu_bridge_set_multicore_enabled(bool enabled) {
    g_coreModeSetting.store(enabled ? kCoreModeMulti : kCoreModeSingle);
}

void cemu_bridge_set_cpu_core_mode(int mode) {
    g_coreModeSetting.store(mode == 1 ? kCoreModeSingle : (mode == 2 ? kCoreModeMulti : kCoreModeAuto));
    if (g_initialized.load())
        ios_apply_cpu_mode();
}

void cemu_bridge_set_cpu_auto_demoted(bool demoted) {
    g_autoDemoted.store(demoted);
}

int cemu_bridge_cpu_cores_running(void) {
    return g_coresRunning.load();
}

bool cemu_bridge_cpu_auto_picked_multicore(void) {
    return g_autoPickedMulti.load();
}

void cemu_bridge_set_draw_breadcrumbs(bool enabled) {
    PerfTelemetry::DrawBreadcrumbsEnabled().store(enabled, std::memory_order_relaxed);
}

const char* cemu_bridge_perf_line(void) {
    static thread_local std::string snapshot;
    {
        std::lock_guard<std::mutex> lock(g_perfLineMutex);
        snapshot = g_perfLine;
    }
    return snapshot.c_str();
}

const char* cemu_bridge_device_summary(void) {
    static const std::string summary = [] {
        const IosDeviceFacts& d = ios_device_facts();
        char buf[320];
        snprintf(buf, sizeof(buf), "%s, %s, %llu performance + %llu efficiency cores, %llu MB RAM",
            d.model.empty() ? "unknown device" : d.model.c_str(),
            d.gpuName.empty() ? "unknown GPU" : d.gpuName.c_str(),
            (unsigned long long)d.perfCores, (unsigned long long)d.effCores, (unsigned long long)d.memoryMB);
        return std::string(buf);
    }();
    return summary.c_str();
}

void cemu_bridge_set_low_power_mode(bool enabled) {
    g_lowPowerMode.store(enabled);
    if (g_initialized.load())
        ios_apply_cpu_mode();
}

bool cemu_bridge_low_power_mode(void) {
    return g_lowPowerMode.load();
}

bool cemu_bridge_has_txm(void) {
    return ![NSProcessInfo processInfo].isiOSAppOnMac && ios_has_txm();
}

int cemu_bridge_cpu_mode(void) {
    return g_cpuMode.load();
}

bool cemu_bridge_jit_probe(void) {
    return ios_jit_probe_impl();
}

const char* cemu_bridge_cpu_mode_detail(void) {
    const char* detail = getCpuModeDetail();
    if (detail[0] == '\0')
        return "Not decided yet - the CPU path is chosen when the engine initializes, on the first launch.";
    return detail;
}

const char* cemu_bridge_active_moltenvk(void) {
    return g_activeMoltenVK.c_str();
}

bool cemu_bridge_core_available(void) {
    return true;
}

void cemu_bridge_initialize(const char* mlcPath) {
    if (g_initialized.exchange(true))
        return;
    {
        std::string where = "Crash log and checkpoints are being written to: ";
        const char* crashPath = cemu_bridge_crash_log_path();
        where += (crashPath && crashPath[0]) ? crashPath : "(nowhere - $HOME was not set, so no file could be opened)";
        cemu_bridge_log_checkpoint(where.c_str());
    }
    cemu_device_caps_initialize();
    cemu_bridge_log_checkpoint(cemu_device_caps_line());
    cemu_bridge_start_memory_watchdog();
    ios_configure_jit_environment();
    setenv("MVK_CONFIG_SYNCHRONOUS_QUEUE_SUBMITS", "0", 1);
    setenv("MVK_CONFIG_DEBUG", "0", 1);
    setenv("MVK_CONFIG_MAX_ACTIVE_METAL_COMMAND_BUFFERS_PER_QUEUE", "128", 1);

    fs::path userDataPath = (mlcPath && mlcPath[0] != '\0') ? fs::path(mlcPath) : fs::path(".");
    std::error_code ec;
    fs::create_directories(userDataPath / "cache", ec);
    fs::path dataPath = userDataPath;
    NSString* bundleResourcePath = [[NSBundle mainBundle] resourcePath];
    if (bundleResourcePath.length > 0)
        dataPath = fs::path(bundleResourcePath.fileSystemRepresentation) / "CemuData";
    NSString* executablePath = [[NSBundle mainBundle] executablePath] ?: @"";

    const std::string userData = userDataPath.string();
    const std::string cache = (userDataPath / "cache").string();
    const std::string data = dataPath.string();

    {
        NSString* choice = [[NSUserDefaults standardUserDefaults] stringForKey:@"espressu.render.moltenVK"];
        const bool legacy = [choice isEqualToString:@"1.2.8"];
        NSString* path = [[[NSBundle mainBundle] privateFrameworksPath] stringByAppendingPathComponent:
            legacy ? @"MoltenVK128.framework/MoltenVK128" : @"MoltenVK.framework/MoltenVK"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:path])
        {
            setenv("ESPRESSU_MOLTENVK_PATH", path.fileSystemRepresentation, 1);
            g_activeMoltenVK = legacy ? "1.2.8" : "1.4.3";
            cemu_bridge_log_checkpoint(("MoltenVK: " + g_activeMoltenVK + " is the build for this launch (loaded only if the Vulkan renderer is selected)").c_str());
        }
        else
        {
            cemu_bridge_log_checkpoint((std::string("MoltenVK: ") + path.fileSystemRepresentation +
                " is missing from the bundle - the core falls back to its own search").c_str());
        }
    }

    KeyCache_ResetForNewPaths();

    cemu_bridge_log_checkpoint("initialize: about to call CemuInitialize()");
    try
    {
        CemuInitialize(executablePath.fileSystemRepresentation, userData.c_str(), userData.c_str(), cache.c_str(), data.c_str());
    }
    catch (...)
    {
        std::string message = "initialize: CemuInitialize() threw: " + cemu_describe_current_exception();
        cemu_bridge_log_checkpoint(message.c_str());
        setStatus("The emulator core failed to start (see the crash log).");
        g_initialized.store(false);
        return;
    }
    cemu_bridge_log_checkpoint("initialize: CemuInitialize() returned");
    IOSSystemImplementation_Install();

    cemuLog_setActiveLoggingFlags(cemuLog_getFlag(LogType::CoreinitLogging) | cemuLog_getFlag(LogType::APIErrors));
    {
        const char* report = cemu_bridge_device_report();
        const char* split = strchr(report, '\n');
        cemuLog_log(LogType::Force, "{}", std::string(report, split ? (size_t)(split - report) : strlen(report)));
        cemuLog_log(LogType::Force, "iOS {}", split ? split + 1 : report);
    }
    {
        std::error_code fontsEc, profilesEc;
        const bool haveFonts = fs::exists(dataPath / "resources" / "sharedFonts" / "CafeStd.ttf", fontsEc);
        const bool haveProfiles = fs::exists(dataPath / "gameProfiles" / "default", profilesEc);
        cemuLog_log(LogType::Force, "iOS data path: {} (shared fonts present: {}, default game profiles present: {})",
            _pathToUtf8(dataPath), haveFonts, haveProfiles);
    }

    ios_apply_cpu_mode();
    cemu_bridge_set_timebase_shift(3);

    ios_input_start();
    ios_stats_start();
    ios_perf_start();
    ios_stall_watchdog_start();
    Fiber::SetStackFailureHandlers(
        [] {
            auto& w = LatteWait::Get();
            const uint32 passes = w.evictionPasses.load();
            w.evictionRequested.store(true);
            for (int i = 0; i < 150 && w.evictionPasses.load() == passes; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        },
        [] {
            cemuLog_log(LogType::Force, "out of address space creating a game thread");
            cemu_bridge_log_checkpoint("out of address space creating a game thread");
            IOSSystemImplementation_ReportFatal("Out of memory address space creating a game thread. Restart the app and try again.");
        });
    ios_log_tail_start();
    dispatch_async(dispatch_get_main_queue(), ^{
        if (UIWindow* window = ios_key_window())
            CemuUIKit_SetMainWindow(window);
    });

    setStatus("Cemu core initialized.");
}

void cemu_bridge_register_render_surface(void* uiView, int width, int height, double dpiScale) {
    ios_live_log_begin_run();
    if (!uiView)
        return;
    UIView* view = (__bridge UIView*)uiView;
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        cemu_bridge_log_checkpoint("register_render_surface: the TV view is not CAMetalLayer-backed - the core renders into the view's own layer, so nothing can be drawn");
        setStatus("Render surface registration failed (see crash log).");
        return;
    }
    CemuUIKit_SetMainView(view);
    ((CAMetalLayer*)view.layer).contentsScale = dpiScale;
    CemuUIKit_UpdateMainWindowSize(width, height, dpiScale);
    setStatus("Render surface registered.");
}

void cemu_bridge_register_pad_render_surface(void* uiView, int width, int height, double dpiScale) {
    if (!uiView || width <= 0 || height <= 0)
        return;
    UIView* view = (__bridge UIView*)uiView;
    if (![view.layer isKindOfClass:[CAMetalLayer class]])
    {
        cemuLog_log(LogType::Force, "iOS: cannot register the GamePad surface - the view is not CAMetalLayer-backed");
        return;
    }
    CemuUIKit_SetPadView(view);
    ((CAMetalLayer*)view.layer).contentsScale = dpiScale;
    g_padRegistered.store(true);
    try
    {
        CemuUIKit_InitializeLayer(false);
    }
    catch (const std::exception& ex)
    {
        cemuLog_log(LogType::Force, "iOS: GamePad surface could not be initialized: {}", ex.what());
    }
    catch (...)
    {
        cemuLog_log(LogType::Force, "iOS: GamePad surface could not be initialized (unknown error)");
    }
    CemuUIKit_SetVisibleOutputs(true, true);
    cemuLog_log(LogType::Force, "iOS: GamePad (DRC) screen surface registered, {}x{} points at {}x scale", width, height, dpiScale);
}

void cemu_bridge_release_pad_render_surface(void) {
    g_padRegistered.store(false);
    CemuUIKit_SetVisibleOutputs(true, false);
    if (!g_titleRunning.load())
        CemuUIKit_SetPadView(nil);
    cemuLog_log(LogType::Force, "iOS: GamePad (DRC) screen output hidden");
}

bool cemu_bridge_has_pad_render_surface(void) {
    return g_padRegistered.load();
}

void cemu_bridge_set_visible_outputs(bool tv, bool pad) {
    CemuUIKit_SetVisibleOutputs(tv, pad);
}

void cemu_bridge_set_output_sources(bool mainShowsGamePad, bool padShowsTV) {
    CemuUIKit_SetOutputSources(mainShowsGamePad, padShowsTV);
}

bool cemu_bridge_pad_layer_active(void) {
    return g_renderer && g_renderer->IsPadWindowActive();
}

void cemu_bridge_ensure_pad_layer(void) {
    if (g_padRegistered.load() && !cemu_bridge_pad_layer_active())
        CemuUIKit_InitializeLayer(false);
}

uint32_t cemu_bridge_pad_present_count(void) {
    return PerfTelemetry::Get().padPresents.load(std::memory_order_relaxed);
}

void cemu_bridge_set_pad_touch(double x, double y, bool down) {
    CemuUIKit_SetPadTouch((CGFloat)x, (CGFloat)y, down);
}

void cemu_bridge_resize_render_surface(int width, int height, double dpiScale, bool mainWindow) {
    if (width <= 0 || height <= 0)
        return;
    if (mainWindow)
    {
        CemuUIKit_UpdateMainWindowSize(width, height, dpiScale);
    }
    else
    {
        if (!g_padRegistered.load())
            return;
        CemuUIKit_UpdatePadWindowSize();
    }
    cemuLog_log(LogType::Force, "iOS: {} surface resized to {}x{} points at {}x scale", mainWindow ? "TV" : "GamePad", width, height, dpiScale);
}

void cemu_bridge_log_line(const char* message) {
    if (!message)
        return;
    cemuLog_log(LogType::Force, std::string_view(message));
}

static CemuBridgeStatus ios_boot_prepared_title(int prepared);
static void ios_report_renderer_fallback();

static int IOSTitleLaunch_PrepareForegroundTitle_Guarded(const char* path) {
    try
    {
        return IOSTitleLaunch_PrepareForegroundTitle(path);
    }
    catch (...)
    {
        cemu_bridge_log_checkpoint(("boot_title: preparing the title threw: " + cemu_describe_current_exception()).c_str());
        CafeSystem::AbortPreparedTitle();
        return 13;
    }
}

CemuBridgeStatus cemu_bridge_boot_title(const char* path) {
    if (!path || path[0] == '\0') {
        setStatus("boot_title: empty path.");
        return CEMU_BRIDGE_BAD_ARG;
    }
    if (!g_initialized.load()) {
        setStatus("The emulator core is not initialized.");
        return CEMU_BRIDGE_CORE_NOT_BUILT;
    }

    static const char kMlcTitlePrefix[] = "mlc-title:";
    if (strncmp(path, kMlcTitlePrefix, sizeof(kMlcTitlePrefix) - 1) == 0) {
        char* end = nullptr;
        const unsigned long long titleId = strtoull(path + sizeof(kMlcTitlePrefix) - 1, &end, 16);
        if (end == path + sizeof(kMlcTitlePrefix) - 1 || *end != '\0' || titleId == 0) {
            setStatus("boot_title: bad title id.");
            return CEMU_BRIDGE_BAD_ARG;
        }
        return cemu_bridge_boot_title_id(titleId);
    }

    cemu_bridge_log_checkpoint("boot_title: about to prepare title");
    int prepared = IOSTitleLaunch_PrepareForegroundTitle_Guarded(path);
    cemu_bridge_log_checkpoint("boot_title: prepare returned");
    return ios_boot_prepared_title(prepared);
}

CemuBridgeStatus cemu_bridge_boot_title_id(uint64_t titleId) {
    if (titleId == 0) {
        setStatus("boot_title_id: empty title id.");
        return CEMU_BRIDGE_BAD_ARG;
    }
    if (!g_initialized.load()) {
        setStatus("The emulator core is not initialized.");
        return CEMU_BRIDGE_CORE_NOT_BUILT;
    }
    cemu_bridge_log_checkpoint("boot_title_id: about to prepare title");
    int prepared = 13;
    try
    {
        prepared = IOSTitleLaunch_PrepareForegroundTitleById(titleId);
    }
    catch (...)
    {
        cemu_bridge_log_checkpoint(("boot_title_id: preparing the title threw: " + cemu_describe_current_exception()).c_str());
        CafeSystem::AbortPreparedTitle();
    }
    cemu_bridge_log_checkpoint("boot_title_id: prepare returned");
    return ios_boot_prepared_title(prepared);
}

static CemuBridgeStatus ios_boot_prepared_title(int prepared) {
    switch (prepared) {
        case 0:
            break;
        case 1:
            setStatus("Invalid RPX.");
            return CEMU_BRIDGE_INVALID_RPX;
        case 2:
            setStatus("Unable to mount title (bad/outdated path).");
            return CEMU_BRIDGE_UNABLE_TO_MOUNT;
        case 3:
            setStatus("This game is encrypted and no key in keys.txt opens it. Put the keys.txt you dumped from your own Wii U in EspressU's \"keys\" folder in the Files app (or import it in Settings), then relaunch EspressU and try again.");
            return CEMU_BRIDGE_NO_DISC_KEY;
        case 4:
            setStatus("This game folder is missing title.tik (the ticket), which EspressU needs to decrypt it. Copy title.tik into the folder next to title.tmd, or add this game's title key to keys.txt.");
            return CEMU_BRIDGE_NO_TITLE_TIK;
        case 7:
            setStatus("This game folder's title.tmd couldn't be read. The file may be damaged or incomplete - copy the whole folder again.");
            return CEMU_BRIDGE_BAD_TITLE_TMD;
        case 8:
            setStatus("This game folder's title.tik (the ticket) couldn't be read, so EspressU can't decrypt it. The file may be damaged - copy it again, or add this game's title key to keys.txt.");
            return CEMU_BRIDGE_BAD_TITLE_TIK;
        case 9:
            setStatus("EspressU couldn't decrypt this game folder. Its title.tik doesn't unlock the .app files (the ticket may belong to another console, or the files are damaged). Check that title.tmd, title.tik and the .app files are from the same download.");
            return CEMU_BRIDGE_TITLE_KEY_INVALID;
        case 10: {
            const std::string missingFile = IOSTitleLaunch_LastErrorDetail();
            setStatus(("This game folder is missing " + (missingFile.empty() ? std::string("a .app file") : missingFile) + ", which title.tmd lists. Copy every .app file from the download into the folder.").c_str());
            return CEMU_BRIDGE_MISSING_CONTENT;
        }
        case 6:
            setStatus("That looks like an update or DLC. Launch the base game instead.");
            return CEMU_BRIDGE_BASE_NOT_FOUND;
        case 11:
            setStatus("That system title isn't installed. Import it in Settings > Wii U Menu.");
            return CEMU_BRIDGE_UNABLE_TO_MOUNT;
        case 13:
            setStatus("EspressU hit an internal error while opening this game. The files may be damaged. Details are in the crash log.");
            return CEMU_BRIDGE_UNABLE_TO_MOUNT;
        default:
            setStatus("Not a Wii U title this build can launch.");
            return CEMU_BRIDGE_UNSUPPORTED;
    }

    {
        const uint64_t needed = DeviceCaps::GetBudgets().minBootHeadroomBytes;
        const uint64_t available = (uint64_t)os_proc_available_memory();
        if (needed != 0 && available != 0 && available < needed)
        {
            char message[512];
            snprintf(message, sizeof(message),
                "This game can't start: only %llu MB of memory is free and it needs about %llu MB. Close other apps and try again. "
                "If it keeps happening, this device may not have enough memory for this game.",
                (unsigned long long)(available >> 20), (unsigned long long)(needed >> 20));
            cemu_bridge_log_checkpoint((std::string("boot_title: not enough memory - ") + message).c_str());
            CafeSystem::AbortPreparedTitle();
            setStatus(message);
            return CEMU_BRIDGE_UNABLE_TO_MOUNT;
        }
    }

    if (!g_padRegistered.load())
        CemuUIKit_SetPadView(nil);
    CemuUIKit_SetVisibleOutputs(true, g_padRegistered.load());
    ios_apply_cpu_mode(true);
    ios_apply_render_profile();

    IOSSystemImplementation_ResetExit();
    cemu_bridge_log_checkpoint("boot_title: about to call CemuRun()");
    try
    {
        CemuRun();
    }
    catch (...)
    {
        std::string message = "boot_title: CemuRun() threw: " + cemu_describe_current_exception();
        cemu_bridge_log_checkpoint(message.c_str());
        CafeSystem::AbortPreparedTitle();
        g_renderer.reset();
        setStatus("The title failed to start: the graphics renderer could not be created. Try the other renderer in Settings > Graphics. Details are in the crash log.");
        return CEMU_BRIDGE_UNABLE_TO_MOUNT;
    }
    cemu_bridge_log_checkpoint("boot_title: CemuRun() returned");
    ios_report_renderer_fallback();
    ios_reset_video_stall_state();
    g_titleRunning.store(true);
    ios_timebase_ladder_start();
    setStatus("Title launched.");
    return CEMU_BRIDGE_OK;
}

CemuBridgeStatus cemu_bridge_boot_rpx(const char* rpxPath) {
    return cemu_bridge_boot_title(rpxPath);
}

int cemu_bridge_reload_and_count_keys(void) {
    if (!g_initialized.load())
        return -1;
    return IOSTitleLaunch_ReloadAndCountKeys();
}

void cemu_bridge_prepare_keys_before_init(const char* mlcPath) {
    if (!mlcPath || mlcPath[0] == '\0')
        return;
    const fs::path userDataRoot(mlcPath);
    KeyCache_SetPreInitKeyFiles(userDataRoot.parent_path() / "keys" / "keys.txt", userDataRoot / "keys.txt");
}

double cemu_bridge_get_fps(void) {
    return g_framesPerSecond.load();
}

void cemu_bridge_get_progress(CemuBridgeProgress* out) {
    if (!out)
        return;
    *out = CemuBridgeProgress{};
    if (!g_titleRunning.load() || !CafeSystem::IsTitleRunning())
        return;
    out->gx2_init_reached = LatteGPUState.gx2InitCalled > 0;
    out->gx2_frame_count = LatteGPUState.frameCounter;
    out->gx2_frames_per_second = g_framesPerSecond.load();
    out->os_screen_scanouts = 0;
    out->guest_flip_requests = (unsigned int)LatteGPUState.flipRequestCount.load();
}

static std::atomic<bool> g_decryptRunning{false};
static std::atomic<bool> g_decryptCompleted{false};
static std::atomic<bool> g_decryptCancelRequested{false};
static std::atomic<int> g_decryptResultStatus{0};
static std::atomic<uint64_t> g_decryptBytesWritten{0};
static std::atomic<uint32_t> g_decryptFilesWritten{0};
static std::thread g_decryptThread;
static std::mutex g_decryptThreadMutex;

static bool StartDecryptJob(const std::function<int(const std::function<void(uint64_t, uint32_t)>&)>& job) {
    if (g_decryptRunning.exchange(true))
        return false;

    std::lock_guard lock{g_decryptThreadMutex};
    if (g_decryptThread.joinable())
        g_decryptThread.join();
    g_decryptCompleted.store(false);
    g_decryptCancelRequested.store(false);
    g_decryptBytesWritten.store(0);
    g_decryptFilesWritten.store(0);

    g_decryptThread = std::thread([job]() {
        ios_bridge_thread_begin("EspressU decrypt", false);
        auto progress = [](uint64_t bytesWritten, uint32_t filesWritten) {
            g_decryptBytesWritten.store(bytesWritten);
            g_decryptFilesWritten.store(filesWritten);
        };
        int status = 5;
        try {
            status = job(progress);
        } catch (...) {
            std::string message = "decrypt: threw: " + cemu_describe_current_exception();
            cemu_bridge_log_checkpoint(message.c_str());
        }
        g_decryptResultStatus.store(status);
        g_decryptCompleted.store(true);
        g_decryptRunning.store(false);
    });
    return true;
}

bool cemu_bridge_start_decrypt(const char* srcPath, const char* destPath, bool toWua) {
    if (!srcPath || srcPath[0] == '\0' || !destPath || destPath[0] == '\0')
        return false;
    std::string src(srcPath);
    std::string dest(destPath);
    return StartDecryptJob([src, dest, toWua](const std::function<void(uint64_t, uint32_t)>& progress) {
        return toWua
            ? IOSTitleDecrypt_ExtractToWua(src.c_str(), dest.c_str(), g_decryptCancelRequested, progress)
            : IOSTitleDecrypt_ExtractToFolder(src.c_str(), dest.c_str(), g_decryptCancelRequested, progress);
    });
}

bool cemu_bridge_start_wua_build(const char* sourcePathsNewlineSeparated, const char* destPath) {
    if (!sourcePathsNewlineSeparated || !destPath || destPath[0] == '\0')
        return false;
    std::vector<std::string> sources;
    std::string list(sourcePathsNewlineSeparated);
    size_t start = 0;
    while (start <= list.size()) {
        size_t end = list.find('\n', start);
        if (end == std::string::npos)
            end = list.size();
        if (end > start)
            sources.push_back(list.substr(start, end - start));
        start = end + 1;
    }
    if (sources.empty())
        return false;
    std::string dest(destPath);
    return StartDecryptJob([sources, dest](const std::function<void(uint64_t, uint32_t)>& progress) {
        return IOSWuaContent_Build(sources, dest.c_str(), g_decryptCancelRequested, progress);
    });
}

int cemu_bridge_wua_list_titles(const char* wuaPath, char* outLines, size_t outSize) {
    if (!wuaPath || !outLines || outSize == 0)
        return -1;
    std::string lines = IOSWuaContent_ListTitles(wuaPath);
    int count = 0;
    for (char c : lines)
        if (c == '\n')
            count++;
    size_t length = std::min(lines.size(), outSize - 1);
    memcpy(outLines, lines.data(), length);
    outLines[length] = '\0';
    return count;
}

void cemu_bridge_get_decrypt_progress(CemuBridgeDecryptProgress* out) {
    if (!out)
        return;
    *out = CemuBridgeDecryptProgress{};
    out->is_running = g_decryptRunning.load();
    out->completed = g_decryptCompleted.load();
    out->result_status = g_decryptResultStatus.load();
    out->bytes_written = g_decryptBytesWritten.load();
    out->files_written = g_decryptFilesWritten.load();
}

void cemu_bridge_cancel_decrypt(void) {
    g_decryptCancelRequested.store(true);
}

bool cemu_bridge_derive_gametdb_id(const char* romPath, char* outGameID, size_t outGameIDSize) {
    if (!romPath || !outGameID || outGameIDSize < 7)
        return false;
    std::string id = IOSCoverArt_DeriveGameTdbId(romPath);
    if (id.size() != 6)
        return false;
    memcpy(outGameID, id.c_str(), 7);
    return true;
}

bool cemu_bridge_get_title_name(const char* romPath, char* outName, size_t outNameSize) {
    if (!romPath || !outName || outNameSize == 0)
        return false;
    const std::string name = IOSCoverArt_GetTitleName(romPath);
    if (name.empty())
        return false;
    size_t length = std::min(name.size(), outNameSize - 1);
    while (length > 0 && length < name.size() && ((unsigned char)name[length] & 0xC0) == 0x80)
        length--;
    memcpy(outName, name.data(), length);
    outName[length] = '\0';
    return true;
}

bool cemu_bridge_derive_title_id(const char* romPath, uint64_t* outTitleId) {
    if (!romPath || !outTitleId)
        return false;
    return IOSDlcUpdateImport_DeriveTitleId(romPath, outTitleId);
}

bool cemu_bridge_read_tmd_title_id(const char* tmdPath, uint64_t* outTitleId) {
    return IOSDlcUpdateImport_ReadTmdTitleId(tmdPath, outTitleId);
}

uint64_t cemu_bridge_derive_base_title_id(uint64_t titleId) {
    return IOSDlcUpdateImport_DeriveBaseTitleId(titleId);
}

int cemu_bridge_get_title_type(uint64_t titleId) {
    return IOSDlcUpdateImport_GetTitleType(titleId);
}

void cemu_bridge_get_mlc_title_path_components(uint64_t titleId, char* outUpperHex, char* outLowerHex) {
    if (!outUpperHex || !outLowerHex)
        return;
    IOSDlcUpdateImport_GetMlcTitlePathComponents(titleId, outUpperHex, outLowerHex);
}

bool cemu_bridge_inspect_title(const char* romPath, uint64_t* outTitleId, uint16_t* outVersion,
    int* outRegion, int* outInvalidReason) {
    return IOSDlcUpdateImport_Inspect(romPath, outTitleId, outVersion, outRegion, outInvalidReason);
}

uint64_t cemu_bridge_derive_content_title_id(uint64_t baseTitleId, bool isUpdate) {
    return IOSDlcUpdateImport_DeriveContentTitleId(baseTitleId, isUpdate);
}

void cemu_bridge_graphic_packs_refresh(void) {
    IOSGraphicPacks_Refresh();
}

const char* cemu_bridge_graphic_packs_list(void) {
    static thread_local std::string g_graphicPacksList;
    g_graphicPacksList = IOSGraphicPacks_List();
    return g_graphicPacksList.c_str();
}

void cemu_bridge_graphic_pack_set_enabled(int index, bool enabled) {
    IOSGraphicPacks_SetEnabled(index, enabled);
}

const char* cemu_bridge_accounts_list(void) {
    static thread_local std::string g_accountsList;
    g_accountsList = IOSAccounts_List();
    return g_accountsList.c_str();
}

void cemu_bridge_accounts_refresh(void) {
    IOSAccounts_Refresh();
}

bool cemu_bridge_accounts_has_free_slot(void) {
    return IOSAccounts_HasFreeSlot();
}

uint32_t cemu_bridge_accounts_next_persistent_id(void) {
    return IOSAccounts_NextPersistentId();
}

uint32_t cemu_bridge_accounts_min_persistent_id(void) {
    return IOSAccounts_MinPersistentId();
}

bool cemu_bridge_accounts_locked(void) {
    return IOSAccounts_Locked();
}

bool cemu_bridge_account_create(uint32_t persistentId, const char* miiName, uint16_t birthYear,
    uint8_t birthMonth, uint8_t birthDay, int gender, const char* email, int country) {
    return IOSAccounts_Create(persistentId, miiName, birthYear, birthMonth, birthDay, gender, email, country);
}

bool cemu_bridge_account_delete(uint32_t persistentId) {
    return IOSAccounts_Delete(persistentId);
}

bool cemu_bridge_account_set_mii_name(uint32_t persistentId, const char* miiName) {
    return IOSAccounts_SetMiiName(persistentId, miiName);
}

bool cemu_bridge_account_set_gender(uint32_t persistentId, int gender) {
    return IOSAccounts_SetGender(persistentId, gender);
}

bool cemu_bridge_account_set_email(uint32_t persistentId, const char* email) {
    return IOSAccounts_SetEmail(persistentId, email);
}

bool cemu_bridge_account_set_country(uint32_t persistentId, int country) {
    return IOSAccounts_SetCountry(persistentId, country);
}

bool cemu_bridge_account_set_birthdate(uint32_t persistentId, uint16_t year, uint8_t month, uint8_t day) {
    return IOSAccounts_SetBirthdate(persistentId, year, month, day);
}

uint32_t cemu_bridge_active_account_persistent_id(void) {
    return IOSAccounts_ActivePersistentId();
}

void cemu_bridge_set_active_account_persistent_id(uint32_t persistentId) {
    IOSAccounts_SetActivePersistentId(persistentId);
}

int cemu_bridge_account_online_error(uint32_t persistentId) {
    return IOSAccounts_OnlineError(persistentId);
}

bool cemu_bridge_account_is_online_valid(uint32_t persistentId) {
    return IOSAccounts_IsOnlineValid(persistentId);
}

const char* cemu_bridge_countries_list(void) {
    static thread_local std::string g_countriesList;
    g_countriesList = IOSAccounts_CountriesList();
    return g_countriesList.c_str();
}

CemuBridgeNetworkService cemu_bridge_network_service(uint32_t persistentId) {
    return (CemuBridgeNetworkService)IOSAccounts_NetworkService(persistentId);
}

void cemu_bridge_set_network_service(uint32_t persistentId, CemuBridgeNetworkService service) {
    IOSAccounts_SetNetworkService(persistentId, (int)service);
}

bool cemu_bridge_custom_network_service_available(void) {
    return IOSAccounts_CustomNetworkServiceAvailable();
}

bool cemu_bridge_reload_custom_network_service(void) {
    return IOSAccounts_ReloadCustomNetworkService();
}

bool cemu_bridge_network_services_xml_is_valid(const char* path) {
    return IOSAccounts_IsValidNetworkServicesXML(path);
}

void cemu_bridge_set_device_network(CemuBridgeDeviceNetwork kind) {
    nn_ac::SetDeviceReachable(kind != CEMU_BRIDGE_DEVICE_NETWORK_OFFLINE);
}

bool cemu_bridge_console_appears_connected(void) {
    return nn_ac::IsConsoleConnected();
}

bool cemu_bridge_online_play_enabled(void) {
    return ActiveSettings::IsOnlineEnabled();
}

void cemu_bridge_set_emulate_skylander_portal(bool enabled) {
    GetConfig().emulated_usb_devices.emulate_skylander_portal = enabled;
}

bool cemu_bridge_emulate_skylander_portal(void) {
    return GetConfig().emulated_usb_devices.emulate_skylander_portal.GetValue();
}

void cemu_bridge_set_emulate_infinity_base(bool enabled) {
    GetConfig().emulated_usb_devices.emulate_infinity_base = enabled;
}

bool cemu_bridge_emulate_infinity_base(void) {
    return GetConfig().emulated_usb_devices.emulate_infinity_base.GetValue();
}

void cemu_bridge_set_emulate_dimensions_toypad(bool enabled) {
    GetConfig().emulated_usb_devices.emulate_dimensions_toypad = enabled;
}

bool cemu_bridge_emulate_dimensions_toypad(void) {
    return GetConfig().emulated_usb_devices.emulate_dimensions_toypad.GetValue();
}

int cemu_bridge_usb_device_slot_count(CemuBridgeUSBDevice device) {
    return IOSEmulatedDevices_SlotCount((int)device);
}

const char* cemu_bridge_usb_device_slot_names(CemuBridgeUSBDevice device) {
    static thread_local std::string g_usbDeviceSlotNames;
    g_usbDeviceSlotNames = IOSEmulatedDevices_SlotNames((int)device);
    return g_usbDeviceSlotNames.c_str();
}

const char* cemu_bridge_usb_device_figure_list(CemuBridgeUSBDevice device, int slot) {
    static thread_local std::string g_usbDeviceFigureList;
    g_usbDeviceFigureList = IOSEmulatedDevices_FigureList((int)device, slot);
    return g_usbDeviceFigureList.c_str();
}

const char* cemu_bridge_usb_device_load(CemuBridgeUSBDevice device, int slot, const char* path) {
    static thread_local std::string g_usbDeviceLoadError;
    g_usbDeviceLoadError = IOSEmulatedDevices_Load((int)device, slot, path);
    return g_usbDeviceLoadError.empty() ? nullptr : g_usbDeviceLoadError.c_str();
}

const char* cemu_bridge_usb_device_clear(CemuBridgeUSBDevice device, int slot) {
    static thread_local std::string g_usbDeviceClearError;
    g_usbDeviceClearError = IOSEmulatedDevices_Clear((int)device, slot);
    return g_usbDeviceClearError.empty() ? nullptr : g_usbDeviceClearError.c_str();
}

const char* cemu_bridge_usb_device_create(CemuBridgeUSBDevice device, uint32_t figureId, uint16_t variant, const char* path) {
    static thread_local std::string g_usbDeviceCreateError;
    g_usbDeviceCreateError = IOSEmulatedDevices_Create((int)device, figureId, variant, path);
    return g_usbDeviceCreateError.empty() ? nullptr : g_usbDeviceCreateError.c_str();
}

const char* cemu_bridge_usb_device_move_dimensions(int fromSlot, int toSlot) {
    static thread_local std::string g_usbDeviceMoveError;
    g_usbDeviceMoveError = IOSEmulatedDevices_MoveDimensions(fromSlot, toSlot);
    return g_usbDeviceMoveError.empty() ? nullptr : g_usbDeviceMoveError.c_str();
}

const char* cemu_bridge_touch_amiibo(const char* path) {
    static thread_local std::string g_amiiboError;
    if (!path || path[0] == '\0') {
        g_amiiboError = "No amiibo file was chosen.";
        return g_amiiboError.c_str();
    }
    if (!g_titleRunning.load() || !nnNfp_isInitialized()) {
        g_amiiboError = "No game is waiting for an amiibo right now. Start a game that uses amiibo, get to the point where it asks you to tap one, then scan again.";
        return g_amiiboError.c_str();
    }
    uint32_t nfcError = 0;
    if (nnNfp_touchNfcTagFromFile(fs::path(path), &nfcError)) {
        g_amiiboError.clear();
        return nullptr;
    }
    g_amiiboError = (nfcError == 2)
        ? "That file isn't a valid amiibo dump. EspressU needs a full amiibo dump (a .bin or .nfc file of 532 bytes or more, usually 540 or 572)."
        : "EspressU couldn't read that amiibo file.";
    return g_amiiboError.c_str();
}

static constexpr int kTimebaseShiftMin = 0;
static constexpr int kTimebaseShiftMax = 10;

void cemu_bridge_set_timebase_shift(int shift) {
    if (shift < kTimebaseShiftMin) shift = kTimebaseShiftMin;
    if (shift > kTimebaseShiftMax) shift = kTimebaseShiftMax;
    ActiveSettings::SetTimerShiftFactor((uint8)shift);
    cemuLog_log(LogType::Force, "Emulated timebase: shift {} ({:.4g}x real time)",
        shift, 8.0 / (double)(1u << shift));
}

int cemu_bridge_get_timebase_shift(void) {
    return (int)ActiveSettings::GetTimerShiftFactor();
}

static constexpr int kLadderFloorShift = 9;
static constexpr int kLadderStepSeconds = 12;

static std::atomic<bool> g_timebaseAutoEnabled{true};
static std::atomic<bool> g_timebaseLadderRunning{false};
static std::thread g_timebaseLadderThread;
static std::mutex g_timebaseLadderMutex;

void cemu_bridge_set_timebase_auto_enabled(bool enabled) {
    const bool was = g_timebaseAutoEnabled.exchange(enabled);
    if (was == enabled)
        return;
    cemuLog_log(LogType::Force, "Emulated timebase: automatic clock ladder {}",
        enabled ? "enabled" : "disabled - a value was chosen by hand, so it stands");
}

bool cemu_bridge_timebase_auto_enabled(void) {
    return g_timebaseAutoEnabled.load();
}

static void ios_timebase_ladder_entry() {
    const auto start = std::chrono::steady_clock::now();
    const int startShift = cemu_bridge_get_timebase_shift();
    ios_bridge_thread_begin("EspressU clock ladder");
    auto lastStep = start;
    bool baselineTaken = false;
    unsigned long long baseGX2Frames = 0;
    unsigned int baseGuestFlipRequests = 0;
    while (g_timebaseLadderRunning.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if (!g_timebaseLadderRunning.load() || !g_timebaseAutoEnabled.load() || !CafeSystem::IsTitleRunning())
            return;
        if (IOSTitlePause_IsPaused())
            continue;

        CemuBridgeProgress progress{};
        cemu_bridge_get_progress(&progress);
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - start).count();

        if (!baselineTaken) {
            baselineTaken = true;
            baseGX2Frames = progress.gx2_frame_count;
            baseGuestFlipRequests = progress.guest_flip_requests;
        }

        const bool advancing = progress.gx2_init_reached ||
                               progress.gx2_frame_count > baseGX2Frames ||
                               progress.guest_flip_requests > baseGuestFlipRequests;
        if (advancing) {
            const int shift = cemu_bridge_get_timebase_shift();
            if (shift != startShift) {
                cemuLog_log(LogType::Force,
                    "Emulated timebase: the title is advancing ({}) after {:.1f}s. Restoring the clock from "
                    "shift {} ({:.4g}x real time) to shift {} ({:.4g}x) - the ladder was searching for a way "
                    "past the stall, not choosing a speed to run at.",
                    progress.gx2_init_reached ? "GX2Init reached" : "guest output moving", elapsed,
                    shift, 8.0 / (double)(1u << shift),
                    startShift, 8.0 / (double)(1u << startShift));
                cemu_bridge_set_timebase_shift(startShift);
            } else {
                cemuLog_log(LogType::Force,
                    "Emulated timebase: the title is advancing ({}) after {:.1f}s with the clock untouched at "
                    "shift {} ({:.4g}x real time). Ladder stopped without having had to step.",
                    progress.gx2_init_reached ? "GX2Init reached" : "guest output moving",
                    elapsed, shift, 8.0 / (double)(1u << shift));
            }
            return;
        }

        if (now - lastStep < std::chrono::seconds(kLadderStepSeconds))
            continue;
        lastStep = now;

        const int shift = cemu_bridge_get_timebase_shift();
        if (shift >= kLadderFloorShift) {
            cemuLog_log(LogType::Force,
                "Emulated timebase: the ladder is at its floor - shift {} ({:.4g}x real time) - and after "
                "{:.1f}s the title still has not reached GX2Init. The guest's clock is not what is holding "
                "this title, so every step taken was wrong; restoring shift {} ({:.4g}x) and stopping.",
                shift, 8.0 / (double)(1u << shift), elapsed,
                startShift, 8.0 / (double)(1u << startShift));
            if (shift != startShift)
                cemu_bridge_set_timebase_shift(startShift);
            return;
        }

        cemuLog_log(LogType::Force,
            "Emulated timebase: no GX2Init after {:.1f}s, stepping the guest's clock down to shift {} "
            "({:.4g}x real time). This is the ladder searching, not a value anyone chose.",
            elapsed, shift + 1, 8.0 / (double)(1u << (shift + 1)));
        cemu_bridge_set_timebase_shift(shift + 1);
    }
}

static void ios_timebase_ladder_stop() {
    std::lock_guard lock{g_timebaseLadderMutex};
    g_timebaseLadderRunning.store(false);
    if (g_timebaseLadderThread.joinable())
        g_timebaseLadderThread.join();
}

static void ios_timebase_ladder_start() {
    ios_timebase_ladder_stop();
    if (!g_timebaseAutoEnabled.load())
        return;
    if (g_cpuMode.load() != kCpuModeInterpreter)
        return;
    std::lock_guard lock{g_timebaseLadderMutex};
    g_timebaseLadderRunning.store(true);
    g_timebaseLadderThread = std::thread(ios_timebase_ladder_entry);
    cemuLog_log(LogType::Force,
        "Emulated timebase: automatic clock ladder armed - if the title has not reached GX2Init after "
        "{}s the clock steps down one notch, to a floor of 1/64 real time.", kLadderStepSeconds);
}

bool cemu_bridge_is_title_running(void) {
    if (g_titleRunning.load() && CafeSystem::IsTitleSwitchInProgress())
        return true;
    return g_titleRunning.load() && CafeSystem::IsTitleRunning() && !IOSSystemImplementation_TitleExited(nullptr);
}

bool IOSBridge_RecreateRenderSurface() {
    struct State {
        std::atomic_bool claimed{false};
        std::atomic_bool ok{true};
    };
    auto state = std::make_shared<State>();
    auto work = [state]() {
        if (state->claimed.exchange(true))
            return;
        try
        {
            ios_check_clean_slate_after_stop();
            ios_reset_video_stall_state();
            ios_apply_render_profile();
            CemuPrepareRenderer();
            ios_report_renderer_fallback();
        }
        catch (...)
        {
            state->ok.store(false);
            std::string message = "title switch: rebuilding the renderer threw: " + cemu_describe_current_exception();
            cemu_bridge_log_checkpoint(message.c_str());
        }
    };
    if ([NSThread isMainThread]) {
        work();
        return state->ok.load();
    }
    dispatch_group_t group = dispatch_group_create();
    dispatch_group_async(group, dispatch_get_main_queue(), ^{ work(); });
    if (dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) != 0) {
        cemuLog_log(LogType::Force, "iOS: title switch - the main thread did not respond in 10s, rebuilding the renderer on the launcher thread");
        work();
        dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
    }
    return state->ok.load();
}

void IOSBridge_VulkanDeviceLost(const char* why) {
    NSUserDefaults* defaults = [NSUserDefaults standardUserDefaults];
    [defaults setObject:[NSString stringWithUTF8String:g_activeMoltenVK.c_str()] forKey:kVulkanFailedBuildKey];
    [defaults setObject:[NSString stringWithFormat:@"stopped while running: %s", (why && *why) ? why : "unknown"] forKey:kVulkanFailureReasonKey];
    ios_migrate_renderer_setting_to_metal();
    cemu_bridge_log_checkpoint((std::string("Renderer: Vulkan failed while running with MoltenVK ") + g_activeMoltenVK
        + " (" + (why ? why : "unknown") + "). The title is being stopped and the saved renderer is now Metal.").c_str());
    IOSSystemImplementation_ReportFatal("Vulkan stopped working while this game was running (the GPU stopped responding or ran out of memory), so the game was stopped. The renderer is back on Metal for the next launch; you can try Vulkan again in Settings > Graphics.");
}

void IOSBridge_LogAllocationFailure(const char* what, uint64_t wantedBytes) {
    cemu_vm_report(what, wantedBytes);
}

void IOSBridge_GPUThreadException(const char* type, const char* what) {
    cemu_crash_open_log();
    cemu_bridge_log_checkpoint("\n=== GPU THREAD EXCEPTION ===");
    cemu_bridge_log_checkpoint((std::string("uncaught C++ exception on the GPU thread, type: ") + (type && *type ? type : "unknown")).c_str());
    cemu_bridge_log_checkpoint((std::string("what(): ") + (what && *what ? what : "(none)")).c_str());
    cemu_bridge_log_checkpoint("GPU thread backtrace (where it was caught):");
    void* frames[32];
    const int count = backtrace(frames, 32);
    if (g_crashLogFd >= 0)
        backtrace_symbols_fd(frames, count, g_crashLogFd);
    cemu_bridge_log_checkpoint("=== the title is being stopped ===");
    IOSSystemImplementation_ReportFatal("The graphics engine hit an error and the game was stopped. The details are in CemuCrashLog.txt.");
}

void cemu_bridge_pause(void) {
    IOSTitlePause_Pause();
}

void cemu_bridge_resume(void) {
    IOSTitlePause_Resume();
}

void cemu_bridge_shutdown_title(void) {
    cemu_bridge_memory_note("before title shutdown");
    ios_timebase_ladder_stop();
    IOSTitlePause_Resume();
    IOSTitlePause_Forget();
    if (CafeSystem::IsTitleRunning())
        CafeSystem::ShutdownTitle();
    ios_check_clean_slate_after_stop();
    g_renderer.reset();
    g_titleRunning.store(false);
    g_framesPerSecond.store(0.0);
    ios_reset_video_stall_state();
    cemu_bridge_release_all_buttons();
    cemu_bridge_memory_note("after title shutdown");
    setStatus("Title shut down.");
}

static std::atomic<CemuTitleSwitchCallback> g_titleSwitchCallback{nullptr};

void cemu_bridge_set_title_switch_callback(CemuTitleSwitchCallback callback) {
    g_titleSwitchCallback.store(callback);
}

void IOSBridge_TitleSwitching(uint64_t titleId) {
    if (CemuTitleSwitchCallback callback = g_titleSwitchCallback.load())
        callback(titleId);
}

bool cemu_bridge_clean_start_required(void) {
    return g_cleanStartRequired.load();
}

const char* cemu_bridge_clean_start_reason(void) {
    static thread_local std::string reason;
    {
        std::lock_guard<std::mutex> lock(g_cleanStartMutex);
        reason = g_cleanStartReason;
    }
    return reason.c_str();
}

void cemu_bridge_shutdown(void) {
    cemu_bridge_shutdown_title();
    CemuShutdown();
    g_initialized.store(false);
    setStatus("Cemu core shut down.");
}

void cemu_bridge_set_menu_input_callback(CemuMenuInputCallback callback) {
    g_menuCallback.store(callback);
}

void cemu_bridge_set_menu_capture(bool capture) {
    g_menuCapture.store(capture);
    dispatch_async(dispatch_get_main_queue(), ^{
        if (GCExtendedGamepad* pad = g_boundController.extendedGamepad)
            ios_update_physical(pad);
    });
}

void cemu_bridge_refresh_input_devices(void) {
    dispatch_async(dispatch_get_main_queue(), ^{
        ios_bind_first_controller();
    });
}

void cemu_bridge_set_button_state(CemuBridgeButton button, bool pressed) {
    const int bit = ios_button_bit(button);
    if (bit < 0)
    {
        if (button == CEMU_BRIDGE_BUTTON_HOME && !g_homeWarned)
        {
            g_homeWarned = true;
            cemuLog_log(LogType::Force, "iOS input: HOME has no binding in the core's GamePad mapping, so it is ignored (the app opens its HOME menu from HOME and should not send it here)");
        }
        return;
    }
    const uint32_t mask = 1u << bit;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard lock(g_inputMutex);
    if (pressed)
    {
        if ((g_touchRepress | g_touchRepressQueued) & mask)
        {
            g_touchRepressReleased &= ~mask;
            return;
        }
        if (!(g_touchButtons & mask))
        {
            g_touchButtons |= mask;
            g_touchSeen &= ~mask;
            g_touchPendingRelease &= ~mask;
            g_touchPressedAt[bit] = now;
            return;
        }
        if (g_touchPendingRelease & mask)
        {
            if (g_touchSeen & mask)
                g_touchRepress |= mask;
            else
                g_touchPendingRelease &= ~mask;
        }
    }
    else
    {
        if ((g_touchRepress | g_touchRepressQueued) & mask)
        {
            g_touchRepressReleased |= mask;
            return;
        }
        if (!(g_touchButtons & mask))
            return;
        if (ios_touch_release_due(bit, now))
        {
            g_touchButtons &= ~mask;
            g_touchSeen &= ~mask;
            g_touchPendingRelease &= ~mask;
        }
        else
        {
            g_touchPendingRelease |= mask;
        }
    }
}

int cemu_bridge_input_button_mapping_count(void) {
    auto vpad = InputManager::instance().get_vpad_controller(0);
    if (!vpad)
        return -1;
    int count = 0;
    for (uint64 id = VPADController::kButtonId_A; id < VPADController::kButtonId_StickL_Up; ++id)
    {
        if (vpad->get_mapping_controller(id))
            count++;
    }
    return count;
}

const char* cemu_bridge_input_profile_name(void) {
    static thread_local std::string name;
    auto vpad = InputManager::instance().get_vpad_controller(0);
    name = vpad ? vpad->get_profile_name() : std::string("<no controller>");
    return name.c_str();
}

bool cemu_bridge_reset_controller_bindings(void) {
    InputManager::instance().delete_controller(0,   true);
    InputManager::instance().load_gc_controllers();
    const int bindings = cemu_bridge_input_button_mapping_count();
    cemuLog_log(LogType::Force, "iOS input: controller bindings reset; GamePad now has {} button mappings", bindings);
    return bindings > 0;
}

static_assert(CEMU_BRIDGE_PAD_A == VPADController::kButtonId_A && CEMU_BRIDGE_PAD_Y == VPADController::kButtonId_Y
              && CEMU_BRIDGE_PAD_PLUS == VPADController::kButtonId_Plus && CEMU_BRIDGE_PAD_UP == VPADController::kButtonId_Up
              && CEMU_BRIDGE_PAD_STICK_R == VPADController::kButtonId_StickR,
              "CemuBridgePadMapButton must match VPADController::ButtonId");

// Physical button ids used by the settings UI: 0 A, 1 B, 2 X, 3 Y, 4 left bumper, 5 right bumper, 6 left trigger,
// 7 right trigger, 8 options, 9 menu, 10 and 11 stick clicks, 12 to 15 D-pad up, down, left, right.
static int ios_pad_ui_button(int coreButton) {
    if (coreButton >= 0 && coreButton <= 11)
        return coreButton;
    if (coreButton == kButtonUp) return 12;
    if (coreButton == kButtonDown) return 13;
    if (coreButton == kButtonLeft) return 14;
    if (coreButton == kButtonRight) return 15;
    return -1;
}

void cemu_bridge_pad_mapping_set(int padButton, int deviceButton) {
    if (padButton < 1 || padButton > 16)
        return;
    int bit = -1;
    if (deviceButton >= 0 && deviceButton <= 11)
        bit = deviceButton;
    else if (deviceButton >= 12 && deviceButton <= 15)
        bit = kBitUp + (deviceButton - 12);
    std::lock_guard lock(g_inputMutex);
    g_padRemap[padButton] = bit;
    g_padRemapActive = false;
    for (int pad = 1; pad < 17; ++pad)
        g_padRemapActive = g_padRemapActive || g_padRemap[pad] >= 0;
}

void cemu_bridge_pad_mapping_clear_all(void) {
    std::lock_guard lock(g_inputMutex);
    for (int pad = 1; pad < 17; ++pad)
        g_padRemap[pad] = -1;
    g_padRemapActive = false;
}

int cemu_bridge_pad_mapping_default(int padButton) {
    return ios_pad_ui_button(InputManager::default_pad_device_button((uint64)padButton));
}

void cemu_bridge_set_stick_axis(CemuBridgeStick stick, float x, float y) {
    const float magnitude = std::sqrt(x * x + y * y);
    if (magnitude > 1.0f) {
        x /= magnitude;
        y /= magnitude;
    }
    if (std::isnan(x) || std::isnan(y))
        return;
    std::lock_guard lock(g_inputMutex);
    g_touchSticks[stick == CEMU_BRIDGE_STICK_RIGHT ? 1 : 0] = GCBridgeVec2{x, y};
}

void cemu_bridge_release_all_buttons(void) {
    std::lock_guard lock(g_inputMutex);
    g_touchButtons = 0;
    g_touchSeen = 0;
    g_touchPendingRelease = 0;
    g_touchRepress = 0;
    g_touchRepressQueued = 0;
    g_touchRepressReleased = 0;
    g_touchSticks[0] = g_touchSticks[1] = GCBridgeVec2{};
}

const char* cemu_bridge_status_text(void) {
    int exitStatus = 0;
    if (const char* fatal = IOSSystemImplementation_FatalReason())
    {
        setStatus(fatal);
    }
    else if (g_titleRunning.load() && IOSSystemImplementation_TitleExited(&exitStatus))
    {
        char line[96];
        snprintf(line, sizeof(line), "The game closed itself (exit status %d).", exitStatus);
        const std::string hint = IOSAccounts_OnlineHint();
        setStatus((hint.empty() ? std::string(line) : std::string(line) + " If it closed after an online account prompt: " + hint).c_str());
    }
    if (g_titleRunning.load() && IOSSystemImplementation_TitleSwitchFailed() && IOSSystemImplementation_SystemAppletRequested())
        setStatus("The game tried to open the Wii U's account or system settings, which EspressU can't run. A Pretendo ID can't be created or linked inside EspressU: link it on a real Wii U, then import that console's account.dat, otp.bin and seeprom.bin in Settings > Network Service.");
    else if (g_titleRunning.load() && IOSSystemImplementation_TitleSwitchFailed())
        setStatus("The Wii U Menu tried to open another title and it couldn't be started. Check that game is in your library and its keys are installed, then launch it from the library instead.");
    if (statusIsEmpty())
        setStatus(cemu_bridge_is_title_running() ? "Title running." : "Core ready (no title running).");
    return getStatus();
}
