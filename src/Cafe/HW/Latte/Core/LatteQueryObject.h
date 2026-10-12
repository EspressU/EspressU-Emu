#pragma once

class LatteQueryObject
{
public:
	virtual bool getResult(uint64& numSamplesPassed) = 0;
	virtual void begin() = 0;
	virtual void end() = 0;

	uint32 index{};
	bool queryEnded{};
	uint64 queryEventStart{};
	uint64 queryEventEnd{};
};