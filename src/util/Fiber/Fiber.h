#pragma once

#if BOOST_OS_WINDOWS

#endif

class Fiber
{
public:
	Fiber(void(*FiberEntryPoint)(void* userParam), void* userParam, void* privateData);
	~Fiber();

	static Fiber* PrepareCurrentThread(void* privateData = nullptr);
	static void Switch(Fiber& targetFiber);
	static void* GetFiberPrivateData();

	static void SetStackFailureHandlers(void (*relieveMemory)(), void (*failed)());
	bool IsValid() const { return m_valid; }

    void* m_implData{nullptr};
private:
	Fiber(void* privateData);

	void* m_privateData;
	void* m_stackPtr{ nullptr };
	bool m_valid{ true };
};
