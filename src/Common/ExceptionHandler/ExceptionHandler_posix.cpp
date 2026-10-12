#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <string.h>
#include <string>
#include "config/CemuConfig.h"
#include "util/helpers/StringHelpers.h"
#include "ExceptionHandler.h"

#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "Cafe/HW/Espresso/Debugger/GDBBreakpoints.h"

#if BOOST_OS_LINUX
#include "ELFSymbolTable.h"
#endif

#if BOOST_OS_LINUX
void DemangleAndPrintBacktrace(char** backtrace, size_t size)
{
	ELFSymbolTable symTable;
	for (char** i = backtrace; i < backtrace + size; i++)
	{
		std::string_view traceLine{*i};

		size_t parenthesesOpen = traceLine.find_last_of('(');
		size_t parenthesesClose = traceLine.find_last_of(')');
		size_t offsetPlus = traceLine.find_last_of('+');
		if (!parenthesesOpen || !parenthesesClose || !offsetPlus ||
			 offsetPlus < parenthesesOpen || offsetPlus > parenthesesClose)
		{
            CrashLog_WriteLine(traceLine);
			continue;
		}

		uint64 newOffset = -1;
		std::string_view symbolName = traceLine.substr(parenthesesOpen+1, offsetPlus-parenthesesOpen-1);
		if (symbolName.empty())
		{
			uint64 symbolOffset = StringHelpers::ToInt64(traceLine.substr(offsetPlus+1,offsetPlus+1-parenthesesClose-1));
			symbolName = symTable.OffsetToSymbol(symbolOffset, newOffset);
		}

        CrashLog_WriteLine(traceLine.substr(0, parenthesesOpen+1), false);

        CrashLog_WriteLine(boost::core::demangle(symbolName.empty() ? "" : symbolName.data()), false);

        CrashLog_WriteLine("+", false);
		if (newOffset != -1)
		{
            CrashLog_WriteLine(fmt::format("0x{:x}", newOffset), false);
            CrashLog_WriteLine(traceLine.substr(parenthesesClose));
		}
		else
		{
            CrashLog_WriteLine(traceLine.substr(offsetPlus+1));
		}
	}
}
#endif

void handlerDumpingSignal(int sig, siginfo_t *info, void *context)
{
#if defined(ARCH_X86_64) && BOOST_OS_LINUX
	if (info->si_signo == SIGTRAP && info->si_code == TRAP_HWBKPT)
	{
		uint64 dr6 = _ReadDR6();
		g_gdbstub->HandleAccessException(dr6);
		return;
	}
#endif

    if(!CrashLog_Create())
        return;

    char* sigName = strsignal(sig);
	if (sigName)
	{
		printf("%s!\n", sigName);
	}
	else
	{
		printf("Unknown core dumping signal!\n");
	}

	void* backtraceArray[128];
	size_t size;

	size = backtrace(backtraceArray, 128);
#if defined(ARCH_X86_64) && BOOST_OS_LINUX > 0
    ucontext_t *uc = (ucontext_t *)context;
    backtraceArray[0] = (void *)uc->uc_mcontext.gregs[REG_RIP];
#endif

    CrashLog_SetOutputChannels(false, true);
    CrashLog_WriteLine(fmt::format("Error: signal {}:", sig));

#if defined(__aarch64__) && (BOOST_OS_MACOS || BOOST_OS_IOS)
    {
        ucontext_t *uc = (ucontext_t *)context;
        if (uc && uc->uc_mcontext)
        {
            auto* ss = &uc->uc_mcontext->__ss;
            CrashLog_WriteLine(fmt::format("  PC:  {:p}", (void*)ss->__pc));
            CrashLog_WriteLine(fmt::format("  LR:  {:p}", (void*)ss->__lr));
            CrashLog_WriteLine(fmt::format("  SP:  {:p}", (void*)ss->__sp));
            CrashLog_WriteLine(fmt::format("  FP:  {:p}", (void*)ss->__fp));
            for (int i = 0; i < 29; i++)
                CrashLog_WriteLine(fmt::format("  x{}: {:p}", i, (void*)ss->__x[i]));
        }
        if (info)
        {
            CrashLog_WriteLine(fmt::format("  si_addr: {:p}", info->si_addr));
            CrashLog_WriteLine(fmt::format("  si_code: {}", info->si_code));
        }
    }
#endif

#if BOOST_OS_MACOS || BOOST_OS_IOS
	for (size_t i = 0; i < size; i++)
	{
		Dl_info di{};
		if (dladdr(backtraceArray[i], &di) && di.dli_fname)
		{
			const char* image = strrchr(di.dli_fname, '/');
			image = image ? image + 1 : di.dli_fname;
			CrashLog_WriteLine(fmt::format("  bt{}: {:p} {}+0x{:x}", i, backtraceArray[i], image, (uintptr_t)backtraceArray[i] - (uintptr_t)di.dli_fbase));
		}
		else
		{
			CrashLog_WriteLine(fmt::format("  bt{}: {:p}", i, backtraceArray[i]));
		}
	}
#endif

#if BOOST_OS_LINUX
	char** symbol_trace = backtrace_symbols(backtraceArray, size);

	if (symbol_trace)
	{
        DemangleAndPrintBacktrace(symbol_trace, size);
		free(symbol_trace);
	}
	else
	{
        CrashLog_WriteLine("Failed to read backtrace");
	}
#else
    char** symbol_trace = backtrace_symbols(backtraceArray, size);
    if (symbol_trace)
    {
        for (size_t i = 0; i < size; i++)
            CrashLog_WriteLine(symbol_trace[i]);
        free(symbol_trace);
    }
    else
    {
        CrashLog_WriteLine("Failed to read backtrace");
    }
#endif

    std::cerr << fmt::format("\nStacktrace and additional info written to:") << std::endl;
    std::cerr << cemuLog_GetLogFilePath().generic_string() << std::endl;

    ExceptionHandler_LogGeneralInfo();
    CrashLog_SetOutputChannels(true, true);

	if (GetConfig().crash_dump == CrashDump::Enabled)
	{
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	_Exit(1);
}

void handler_SIGINT(int sig)
{
	_Exit(0);
}

void ExceptionHandler_Init()
{
	struct sigaction action;
	action.sa_flags = 0;
	sigfillset(&action.sa_mask);

	action.sa_handler = handler_SIGINT;
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);

    action.sa_flags = SA_SIGINFO;
    action.sa_handler = nullptr;
	action.sa_sigaction = handlerDumpingSignal;
	sigaction(SIGABRT, &action, nullptr);
	sigaction(SIGBUS, &action, nullptr);
	sigaction(SIGFPE, &action, nullptr);
	sigaction(SIGILL, &action, nullptr);
	sigaction(SIGIOT, &action, nullptr);
	sigaction(SIGQUIT, &action, nullptr);
	sigaction(SIGSEGV, &action, nullptr);
	sigaction(SIGSYS, &action, nullptr);
}
