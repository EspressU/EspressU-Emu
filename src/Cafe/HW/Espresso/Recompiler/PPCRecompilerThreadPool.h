
#pragma once

#include <atomic>
#include <semaphore>
#include <thread>
#include <vector>
#include <algorithm>
#include "util/helpers/fspinlock.h"
#include "util/helpers/helpers.h"
#include "PPCRecompiler.h"

#ifndef PPCREC_FORCE_SYNCHRONOUS_COMPILATION
#if defined(__aarch64__)
#define PPCREC_FORCE_SYNCHRONOUS_COMPILATION 0
#else
#define PPCREC_FORCE_SYNCHRONOUS_COMPILATION 1
#endif
#endif

void PPCRecompiler_recompileAtAddress(uint32 address);
extern PPCRecompilerInstanceData_t* ppcRecompilerInstanceData;
extern void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_visited)();
extern void ATTR_MS_ABI (*PPCRecompiler_leaveRecompilerCode_unvisited)();

bool PPCRecompiler_drainQueue();

class PPCRecompilerThreadPool
{
public:
    void SetWorkerCount(uint32 count)
    {
        m_requestedWorkerCount = count;
    }

    void Start()
    {
#if PPCREC_FORCE_SYNCHRONOUS_COMPILATION
        return;
#endif
        if (m_running.exchange(true))
            return;

        uint32 nWorkers = m_requestedWorkerCount;
        if (nWorkers == 0)
        {
            uint32 hw = std::thread::hardware_concurrency();
            nWorkers = std::max(1u, std::min(hw / 2u, 4u));
        }

        m_stopFlag.store(false, std::memory_order_relaxed);
        m_workers.reserve(nWorkers);
        for (uint32 i = 0; i < nWorkers; ++i)
            m_workers.emplace_back(&PPCRecompilerThreadPool::WorkerLoop, this, i);

        cemuLog_log(LogType::Force, "PPCRecompiler thread pool: {} worker(s)", nWorkers);
    }

    void Stop()
    {
        if (!m_running.load(std::memory_order_relaxed))
            return;

        m_stopFlag.store(true, std::memory_order_release);

        for (size_t i = 0; i < m_workers.size(); ++i)
            m_semaphore.release();

        for (auto& t : m_workers)
            if (t.joinable())
                t.join();

        m_workers.clear();
        m_running.store(false, std::memory_order_relaxed);
    }

    void Notify()
    {
        m_semaphore.release();
    }

    ~PPCRecompilerThreadPool()
    {
        Stop();
    }

private:
    void WorkerLoop(uint32 workerIndex)
    {
        SetThreadName(fmt::format("PPCRecompiler-Thread{}", workerIndex).c_str());

        while (true)
        {
            m_semaphore.acquire();

            if (m_stopFlag.load(std::memory_order_acquire))
                return;

            while (true)
            {
                bool more = false;
                try
                {
                    more = PPCRecompiler_drainQueue();
                }
                catch (...)
                {
                    more = true;
                }
                if (!more)
                    break;
                if (m_stopFlag.load(std::memory_order_acquire))
                    return;
            }
        }
    }

    std::counting_semaphore<32767> m_semaphore{0};
    std::atomic<bool>              m_stopFlag{false};
    std::atomic<bool>              m_running{false};
    std::vector<std::thread>       m_workers;
    uint32                         m_requestedWorkerCount = 0;
};
