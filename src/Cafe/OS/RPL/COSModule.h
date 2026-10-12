#pragma once

namespace coreinit
{
	enum class RplEntryReason;
};

class COSModule
{
public:
	virtual std::string_view GetName() = 0;

	virtual std::vector<std::string_view> GetDependencies() { return {}; };

	virtual void RPLMapped() {};
	virtual void RPLUnmapped() {};

	virtual void rpl_entry(uint32 moduleHandle, coreinit::RplEntryReason reason) {};
};

std::span<COSModule*> GetCOSModules();
