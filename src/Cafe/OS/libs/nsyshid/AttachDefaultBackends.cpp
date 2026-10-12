#include "nsyshid.h"
#include "Backend.h"
#include "BackendEmulated.h"
#ifdef HAS_LIBUSB
#include "BackendLibusb.h"
#endif

namespace nsyshid::backend
{
	void AttachDefaultBackends()
	{
	#ifdef HAS_LIBUSB
		{
			auto backendLibusb = std::make_shared<backend::libusb::BackendLibusb>();
			if (backendLibusb->IsInitialisedOk())
			{
				AttachBackend(backendLibusb);
			}
		}
	#endif
		{
			auto backendEmulated = std::make_shared<backend::emulated::BackendEmulated>();
			if (backendEmulated->IsInitialisedOk())
			{
				AttachBackend(backendEmulated);
			}
		}
	}
}
