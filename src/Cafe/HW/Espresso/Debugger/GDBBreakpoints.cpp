#include "GDBBreakpoints.h"
#include "Debugger.h"
#include "Cafe/HW/Espresso/Interpreter/PPCInterpreterInternal.h"

#if defined(ARCH_X86_64) && BOOST_OS_LINUX
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>

DRType _GetDR(pid_t tid, int drIndex)
{
	size_t drOffset = offsetof(struct user, u_debugreg) + drIndex * sizeof(user::u_debugreg[0]);

	long v;
	v = ptrace(PTRACE_PEEKUSER, tid, drOffset, nullptr);
	if (v == -1)
		perror("ptrace(PTRACE_PEEKUSER)");

	return (DRType)v;
}

void _SetDR(pid_t tid, int drIndex, DRType newValue)
{
	size_t drOffset = offsetof(struct user, u_debugreg) + drIndex * sizeof(user::u_debugreg[0]);

	long rc = ptrace(PTRACE_POKEUSER, tid, drOffset, newValue);
	if (rc == -1)
		perror("ptrace(PTRACE_POKEUSER)");
}

DRType _ReadDR6()
{
	pid_t tid = gettid();

	pid_t child = fork();
	if (child == -1)
	{
		perror("fork");
		return 0;
	}

	if (child == 0)
	{
		if (ptrace(PTRACE_ATTACH, tid, nullptr, nullptr))
		{
			perror("attach");
			_exit(0);
		}

		waitpid(tid, NULL, 0);

		uint64_t dr6 = _GetDR(tid, 6);

		if (ptrace(PTRACE_DETACH, tid, nullptr, nullptr))
			perror("detach");

		_exit(dr6 & 0xff);
	}

	int wstatus;
	waitpid(child, &wstatus, 0);

	return (DRType)WEXITSTATUS(wstatus);
}
#endif

GDBServer::ExecutionBreakpoint::ExecutionBreakpoint(MPTR address, BreakpointType type, bool visible, std::string reason)
	: m_address(address), m_removedAfterInterrupt(false), m_reason(std::move(reason))
{
	if (type == BreakpointType::BP_SINGLE)
	{
		this->m_pauseThreads = true;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = visible;
	}
	else if (type == BreakpointType::BP_PERSISTENT)
	{
		this->m_pauseThreads = true;
		this->m_restoreAfterInterrupt = true;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = visible;
	}
	else if (type == BreakpointType::BP_RESTORE_POINT)
	{
		this->m_pauseThreads = false;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = false;
		this->m_pauseOnNextInterrupt = false;
		this->m_visible = false;
	}
	else if (type == BreakpointType::BP_STEP_POINT)
	{
		this->m_pauseThreads = false;
		this->m_restoreAfterInterrupt = false;
		this->m_deleteAfterAnyInterrupt = true;
		this->m_pauseOnNextInterrupt = true;
		this->m_visible = false;
	}

	this->m_origOpCode = memory_readU32(address);
	memory_writeU32(address, DEBUGGER_BP_T_GDBSTUB_TW);
	PPCInterpreter_invalidateBlockCacheRange(address, 4);
}

GDBServer::ExecutionBreakpoint::~ExecutionBreakpoint()
{
	memory_writeU32(this->m_address, this->m_origOpCode);
	PPCInterpreter_invalidateBlockCacheRange(this->m_address, 4);
}

uint32 GDBServer::ExecutionBreakpoint::GetVisibleOpCode() const
{
	if (this->m_visible)
		return memory_readU32(this->m_address);
	else
		return this->m_origOpCode;
}

void GDBServer::ExecutionBreakpoint::RemoveTemporarily()
{
	memory_writeU32(this->m_address, this->m_origOpCode);
	PPCInterpreter_invalidateBlockCacheRange(this->m_address, 4);
	this->m_restoreAfterInterrupt = true;
}

void GDBServer::ExecutionBreakpoint::Restore()
{
	memory_writeU32(this->m_address, DEBUGGER_BP_T_GDBSTUB_TW);
	PPCInterpreter_invalidateBlockCacheRange(this->m_address, 4);
	this->m_restoreAfterInterrupt = false;
}

namespace coreinit
{
#if BOOST_OS_LINUX
	std::vector<pid_t>& OSGetSchedulerThreadIds();
#endif

	std::vector<std::thread::native_handle_type>& OSGetSchedulerThreads();
}

GDBServer::AccessBreakpoint::AccessBreakpoint(MPTR address, AccessPointType type)
	: m_address(address), m_type(type)
{
#if defined(ARCH_X86_64) && BOOST_OS_WINDOWS
	for (auto& hThreadNH : coreinit::OSGetSchedulerThreads())
	{
		HANDLE hThread = (HANDLE)hThreadNH;
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		SuspendThread(hThread);
		GetThreadContext(hThread, &ctx);

		ctx.Dr2 = (DWORD64)memory_getPointerFromVirtualOffset(address);
		ctx.Dr3 = (DWORD64)memory_getPointerFromVirtualOffset(address);
		SetBits(ctx.Dr7, 4, 1, 1);
		SetBits(ctx.Dr7, 24, 2, 1);
		SetBits(ctx.Dr7, 26, 2, 3);
		SetBits(ctx.Dr7, 6, 1, 1);
		SetBits(ctx.Dr7, 28, 2, 3);
		SetBits(ctx.Dr7, 30, 2, 3);

		SetThreadContext(hThread, &ctx);
		ResumeThread(hThread);
	}
#elif defined(ARCH_X86_64) && BOOST_OS_LINUX
	pid_t child = fork();
	if (child == -1)
	{
		perror("fork");
		return;
	}

	if (child == 0)
	{
		for (pid_t tid : coreinit::OSGetSchedulerThreadIds())
		{
			long rc = ptrace(PTRACE_ATTACH, tid, nullptr, nullptr);
			if (rc == -1)
				perror("ptrace(PTRACE_ATTACH)");

			waitpid(tid, nullptr, 0);

			DRType dr7 = _GetDR(tid, 7);
			DRType dr2 = (uint64)memory_getPointerFromVirtualOffset(address);
			DRType dr3 = (uint64)memory_getPointerFromVirtualOffset(address);
			SetBits(dr7, 4, 1, 1);
			SetBits(dr7, 24, 2, 1);
			SetBits(dr7, 26, 2, 3);
			SetBits(dr7, 6, 1, 1);
			SetBits(dr7, 28, 2, 3);
			SetBits(dr7, 30, 2, 3);

			_SetDR(tid, 2, dr2);
			_SetDR(tid, 3, dr3);
			_SetDR(tid, 7, dr7);

			rc = ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
			if (rc == -1)
				perror("ptrace(PTRACE_DETACH)");
		}

		_exit(0);
	}

	waitpid(child, nullptr, 0);
#else
	cemuLog_log(LogType::Force, "Debugger read/write breakpoints are not supported on non-x86 CPUs yet.");
#endif
}

GDBServer::AccessBreakpoint::~AccessBreakpoint()
{
#if defined(ARCH_X86_64) && BOOST_OS_WINDOWS
	for (auto& hThreadNH : coreinit::OSGetSchedulerThreads())
	{
		HANDLE hThread = (HANDLE)hThreadNH;
		CONTEXT ctx{};
		ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
		SuspendThread(hThread);
		GetThreadContext(hThread, &ctx);

		ctx.Dr2 = (DWORD64)0;
		ctx.Dr3 = (DWORD64)0;
		SetBits(ctx.Dr7, 4, 1, 0);
		SetBits(ctx.Dr7, 24, 2, 0);
		SetBits(ctx.Dr7, 26, 2, 0);
		SetBits(ctx.Dr7, 6, 1, 0);
		SetBits(ctx.Dr7, 28, 2, 0);
		SetBits(ctx.Dr7, 30, 2, 0);
		SetThreadContext(hThread, &ctx);
		ResumeThread(hThread);
	}
#elif defined(ARCH_X86_64) && BOOST_OS_LINUX
	pid_t child = fork();
	if (child == -1)
	{
		perror("fork");
		return;
	}

	if (child == 0)
	{
		for (pid_t tid : coreinit::OSGetSchedulerThreadIds())
		{
			long rc = ptrace(PTRACE_ATTACH, tid, nullptr, nullptr);
			if (rc == -1)
				perror("ptrace(PTRACE_ATTACH)");

			waitpid(tid, nullptr, 0);

			DRType dr7 = _GetDR(tid, 7);
			DRType dr2 = 0;
			DRType dr3 = 0;
			SetBits(dr7, 4, 1, 0);
			SetBits(dr7, 24, 2, 0);
			SetBits(dr7, 26, 2, 0);
			SetBits(dr7, 6, 1, 0);
			SetBits(dr7, 28, 2, 0);
			SetBits(dr7, 30, 2, 0);

			_SetDR(tid, 2, dr2);
			_SetDR(tid, 3, dr3);
			_SetDR(tid, 7, dr7);

			rc = ptrace(PTRACE_DETACH, tid, nullptr, nullptr);
			if (rc == -1)
				perror("ptrace(PTRACE_DETACH)");
		}

		_exit(0);
	}

	waitpid(child, nullptr, 0);
#endif
}
