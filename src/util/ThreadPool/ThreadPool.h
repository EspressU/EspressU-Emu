#include <thread>

class ThreadPool
{
public:
	template<class TFunction, class... TArgs>
	static void FireAndForget(TFunction&& f, TArgs&&... args)
	{
		std::thread t(std::forward<TFunction>(f), std::forward<TArgs>(args)...);
		t.detach();
	}

};