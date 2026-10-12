#include "Fiber.h"
#include <boost/context/detail/fcontext.hpp>
#include <cstdlib>
#include <cstring>
#include <cassert>

using namespace boost::context::detail;

struct FiberImpl {
    fcontext_t       ctx{nullptr};
    void           (*entryPoint)(void*){nullptr};
    void*            userParam{nullptr};
};

thread_local Fiber* sCurrentFiber{};

static void (*sRelieveMemoryHook)() = nullptr;
static void (*sStackFailedHook)() = nullptr;

void Fiber::SetStackFailureHandlers(void (*relieveMemory)(), void (*failed)())
{
    sRelieveMemoryHook = relieveMemory;
    sStackFailedHook = failed;
}

static void fiberTrampoline(transfer_t from)
{
    FiberImpl* fromImpl = static_cast<FiberImpl*>(from.data);
    fromImpl->ctx = from.fctx;

    FiberImpl* self = static_cast<FiberImpl*>(sCurrentFiber->m_implData);

    self->entryPoint(self->userParam);

    assert(false && "Fiber entry point returned, switch away before returning");
    std::abort();
}

Fiber::Fiber(void(*FiberEntryPoint)(void*), void* userParam, void* privateData)
    : m_privateData(privateData)
{
    FiberImpl* impl  = new FiberImpl();
    impl->entryPoint = FiberEntryPoint;
    impl->userParam  = userParam;
    m_implData       = impl;

    const size_t stackSize = 2 * 1024 * 1024;
    m_stackPtr = std::malloc(stackSize);
    if (!m_stackPtr && sRelieveMemoryHook)
    {
        sRelieveMemoryHook();
        m_stackPtr = std::malloc(stackSize);
    }
    if (!m_stackPtr)
    {
        m_valid = false;
        if (sStackFailedHook)
            sStackFailedHook();
        return;
    }

    void* stackTop = static_cast<char*>(m_stackPtr) + stackSize;

    impl->ctx = make_fcontext(stackTop, stackSize, fiberTrampoline);
}

Fiber::Fiber(void* privateData)
    : m_privateData(privateData)
{
    m_implData = new FiberImpl();
    m_stackPtr = nullptr;
}

Fiber::~Fiber()
{
    if (m_stackPtr) std::free(m_stackPtr);
    delete static_cast<FiberImpl*>(m_implData);
}

Fiber* Fiber::PrepareCurrentThread(void* privateData)
{
    if (sCurrentFiber != nullptr) return sCurrentFiber;
    sCurrentFiber = new Fiber(privateData);
    return sCurrentFiber;
}

void Fiber::Switch(Fiber& targetFiber)
{
    Fiber* leaving = sCurrentFiber;
    if (leaving == &targetFiber) return;
    if (!targetFiber.IsValid()) return;

    sCurrentFiber = &targetFiber;

    FiberImpl* fromImpl = static_cast<FiberImpl*>(leaving->m_implData);
    FiberImpl* toImpl   = static_cast<FiberImpl*>(targetFiber.m_implData);

    transfer_t ret = jump_fcontext(toImpl->ctx, fromImpl);

    static_cast<FiberImpl*>(ret.data)->ctx = ret.fctx;
}

void* Fiber::GetFiberPrivateData()
{
    return sCurrentFiber ? sCurrentFiber->m_privateData : nullptr;
}
