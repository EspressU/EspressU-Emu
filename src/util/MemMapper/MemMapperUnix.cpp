#include "util/MemMapper/MemMapper.h"

#include <unistd.h>
#include <sys/mman.h>

#if BOOST_OS_MACOS || BOOST_OS_IOS
#include <mach/mach.h>
#endif

namespace MemMapper
{

#if BOOST_OS_MACOS || BOOST_OS_IOS

static const size_t sPageSize = vm_page_size;

size_t GetPageSize()
{
    return sPageSize;
}

vm_prot_t GetProt(PAGE_PERMISSION flags)
{
    vm_prot_t p = VM_PROT_NONE;

    if (HAS_FLAG(flags, PAGE_PERMISSION::P_READ))
        p |= VM_PROT_READ;

    if (HAS_FLAG(flags, PAGE_PERMISSION::P_WRITE))
        p |= VM_PROT_WRITE;

    if (HAS_FLAG(flags, PAGE_PERMISSION::P_EXECUTE))
        p |= VM_PROT_EXECUTE;

    return p;
}

void* ReserveMemory(void* baseAddr, size_t size, PAGE_PERMISSION)
{
    vm_address_t addr = (vm_address_t)baseAddr;

    kern_return_t kr = vm_allocate(
        mach_task_self(),
        &addr,
        size,
        baseAddr ? VM_FLAGS_FIXED : VM_FLAGS_ANYWHERE
    );

    if (kr != KERN_SUCCESS)
        return nullptr;

    kr = vm_protect(
        mach_task_self(),
        addr,
        size,
        FALSE,
        VM_PROT_NONE
    );
    if (kr != KERN_SUCCESS)
    {
        vm_deallocate(mach_task_self(), addr, size);
        return nullptr;
    }

    return (void*)addr;
}

void FreeReservation(void* baseAddr, size_t size)
{
    vm_deallocate(
        mach_task_self(),
        (vm_address_t)baseAddr,
        size
    );
}

void* AllocateMemory(void* baseAddr, size_t size, PAGE_PERMISSION flags, bool fromReservation)
{
    vm_address_t addr = (vm_address_t)baseAddr;

    if (fromReservation)
    {
        kern_return_t kr = vm_protect(
            mach_task_self(),
            addr,
            size,
            FALSE,
            GetProt(flags)
        );

        return kr == KERN_SUCCESS ? baseAddr : nullptr;
    }

    kern_return_t kr = vm_allocate(
        mach_task_self(),
        &addr,
        size,
        baseAddr ? VM_FLAGS_FIXED : VM_FLAGS_ANYWHERE
    );

    if (kr != KERN_SUCCESS)
        return nullptr;

    kr = vm_protect(
        mach_task_self(),
        addr,
        size,
        FALSE,
        GetProt(flags)
    );
    if (kr != KERN_SUCCESS)
    {
        vm_deallocate(mach_task_self(), addr, size);
        return nullptr;
    }

    return (void*)addr;
}

void FreeMemoryKeepPages(void* baseAddr, size_t size)
{
    vm_protect(
        mach_task_self(),
        (vm_address_t)baseAddr,
        size,
        FALSE,
        VM_PROT_NONE
    );
}

void FreeMemory(void* baseAddr, size_t size, bool fromReservation)
{
    if (fromReservation)
    {
        void* remapped = mmap(baseAddr, size, PROT_NONE, MAP_FIXED | MAP_PRIVATE | MAP_ANON, -1, 0);
        if (remapped != baseAddr)
        {
            vm_protect(
                mach_task_self(),
                (vm_address_t)baseAddr,
                size,
                FALSE,
                VM_PROT_NONE
            );
        }
    }
    else
    {
        vm_deallocate(
            mach_task_self(),
            (vm_address_t)baseAddr,
            size
        );
    }
}

#else

	const size_t sPageSize{ []()
		{
		return (size_t)getpagesize();
	}()
	};

	size_t GetPageSize()
	{
		return sPageSize;
	}

	int GetProt(PAGE_PERMISSION permissionFlags)
	{
		int  p = 0;
		if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ | PROT_WRITE | PROT_EXEC;
		else if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ | PROT_WRITE;
		else if (HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_READ) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_WRITE) && !HAS_FLAG(permissionFlags, PAGE_PERMISSION::P_EXECUTE))
			p = PROT_READ;
		else
			cemu_assert_unimplemented();
		return p;
	}

	void* ReserveMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags)
	{
		return mmap(baseAddr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	}

	void FreeReservation(void* baseAddr, size_t size)
	{
		munmap(baseAddr, size);
	}

	void* AllocateMemory(void* baseAddr, size_t size, PAGE_PERMISSION permissionFlags, bool fromReservation)
	{
		void* r;
		if(fromReservation)
		{
		    uint64 page_size = sysconf(_SC_PAGESIZE);
		    void* page = baseAddr;
		    if ( (uint64) baseAddr % page_size != 0 )
		        page = (void*) ((uint64)baseAddr & ~(page_size - 1));
			if( mprotect(page, size, GetProt(permissionFlags)) == 0 )
                r = baseAddr;
			else
                r = nullptr;
		}
		else
			r = mmap(baseAddr, size, GetProt(permissionFlags), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		return r;
	}

	void FreeMemoryKeepPages(void* baseAddr, size_t size)
	{
		mprotect(baseAddr, size, PROT_NONE);
	}

	void FreeMemory(void* baseAddr, size_t size, bool fromReservation)
	{
		if (fromReservation)
			mprotect(baseAddr, size, PROT_NONE);
		else
			munmap(baseAddr, size);
	}

#endif

}
