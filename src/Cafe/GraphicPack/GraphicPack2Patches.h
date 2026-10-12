#pragma once

class PatchGroup;

#include "GraphicPackError.h"

struct PatchContext_t
{
	struct UnresolvedSymbol
	{
		sint32 lineNumber;
		PatchGroup* patchGroup;
		std::string symbolName;

		UnresolvedSymbol(sint32 _lineNumber, PatchGroup* _patchGroup, std::string _symbolName) : lineNumber(_lineNumber), patchGroup(_patchGroup), symbolName(_symbolName) {};

		bool operator < (const UnresolvedSymbol &other) const
		{
			if (lineNumber == other.lineNumber)
				return symbolName.compare(other.symbolName);
			return lineNumber < other.lineNumber;
		}
	};

	class GraphicPack2* graphicPack;
	const RPLModule* matchedModule;
	std::unordered_map<std::string, uint32> map_values;
	std::set<UnresolvedSymbol> unresolvedSymbols;
	PatchErrorHandler errorHandler{};
};

enum class PATCH_RESOLVE_RESULT
{
	RESOLVED,
	EXPRESSION_ERROR,
	VALUE_ERROR,
	UNKNOWN_VARIABLE,
	VARIABLE_CONFLICT,
	INVALID_ADDRESS,
	UNDEFINED_ERROR,
};

enum class EXPRESSION_RESOLVE_RESULT
{
	AVAILABLE,
	EXPRESSION_ERROR,
	UNKNOWN_VARIABLE
};

class PatchEntry
{
public:
	PatchEntry() {};
	virtual ~PatchEntry() {};

	virtual sint32 getLineNumber() { return -1; }

	virtual PATCH_RESOLVE_RESULT resolve(PatchContext_t& ctx) = 0;
};

class PatchEntryCemuhookSymbolValue : public PatchEntry
{
public:
	PatchEntryCemuhookSymbolValue(sint32 lineNumber, const char* symbolName, const sint32 symbolNameLen, const char* expressionStr, const sint32 expressionLen) : PatchEntry(), m_lineNumber(lineNumber)
	{
		m_symbolName.assign(symbolName, symbolNameLen);
		m_expressionString.assign(expressionStr, expressionLen);
	}

	sint32 getLineNumber() override { return m_lineNumber; }

	PATCH_RESOLVE_RESULT resolve(PatchContext_t& ctx) override;

	std::string& getSymbolName() { return m_symbolName; }

private:
	sint32 m_lineNumber;
	std::string m_symbolName;
	std::string m_expressionString;
	uint32 m_resolvedValue;
	bool m_isResolved{};
};

enum class PATCHVARTYPE
{
	DOUBLE,
	INT,
	UINT,
};

class PatchEntryVariableValue : public PatchEntry
{
public:

	PatchEntryVariableValue(sint32 lineNumber, const char* symbolName, const sint32 symbolNameLen, PATCHVARTYPE varType, const char* expressionStr, const sint32 expressionLen) : PatchEntry(), m_lineNumber(lineNumber), m_varType(varType)
	{
		m_symbolName.assign(symbolName, symbolNameLen);
		m_expressionString.assign(expressionStr, expressionLen);
	}

	sint32 getLineNumber() override { return m_lineNumber; }

	PATCH_RESOLVE_RESULT resolve(PatchContext_t& ctx) override;

	std::string& getSymbolName() { return m_symbolName; }

private:
	sint32 m_lineNumber;
	std::string m_symbolName;
	std::string m_expressionString;
	PATCHVARTYPE m_varType;
	std::variant<sint32, uint32, double> m_resolvedValue;
	bool m_isResolved{};
};

class PatchEntryLabel : public PatchEntry
{
public:
	PatchEntryLabel(sint32 lineNumber, const char* symbolName, const sint32 symbolNameLen) : PatchEntry(), m_lineNumber(lineNumber)
	{
		m_symbolName.assign(symbolName, symbolNameLen);
	}

	sint32 getLineNumber() override { return m_lineNumber; }

	PATCH_RESOLVE_RESULT resolve(PatchContext_t& ctx) override;

	std::string& getSymbolName() { return m_symbolName; }
	uint32 getSymbolValue() { return m_relocatedAddress; }

	void setAssignedVA(uint32 virtualAddress)
	{
		m_address = virtualAddress;
	}

private:
	sint32 m_lineNumber;
	std::string m_symbolName;
	uint32 m_address;
	uint32 m_relocatedAddress;
	bool m_isResolved{};
};

class PatchEntryInstruction : public PatchEntry
{
public:
	PatchEntryInstruction(sint32 lineNumber, uint32 patchAddr, std::span<uint8> data, std::vector<PPCAssemblerReloc>& list_relocs) : PatchEntry(), m_lineNumber(lineNumber), m_addr(patchAddr), m_size((uint32)data.size()), m_relocs(list_relocs)
	{
		sint32 dataLength = (sint32)data.size();
		m_length = dataLength;
		m_data = new uint8[dataLength];
		m_dataWithRelocs = new uint8[dataLength];
		m_dataBackup = new uint8[dataLength];
		memcpy(m_data, data.data(), dataLength);
		memcpy(m_dataWithRelocs, data.data(), dataLength);
	}

	~PatchEntryInstruction()
	{
		if (m_data)
			delete[] m_data;
		if (m_dataWithRelocs)
			delete[] m_dataWithRelocs;
		if (m_dataBackup)
			delete[] m_dataBackup;
	}

	sint32 getLineNumber() override { return m_lineNumber; }

	uint32 getAddr() const
	{
		return m_addr;
	}

	uint32 getRelocatedAddr()
	{
		return m_relocatedAddr;
	}
	uint32 getSize() const
	{
		return m_size;
	}

	PATCH_RESOLVE_RESULT resolve(PatchContext_t& ctx) override;
	PATCH_RESOLVE_RESULT resolveReloc(PatchContext_t& ctx, PPCAssemblerReloc* reloc);

	void applyPatch();
	void undoPatch();

private:
	uint8* m_data{};
	uint8* m_dataWithRelocs{};
	uint8* m_dataBackup{};
	sint32 m_length{};
	std::vector<PPCAssemblerReloc> m_relocs;
	uint32 m_lineNumber;
	uint32 m_addr;
	uint32 m_size;
	uint32 m_relocatedAddr;
	bool m_addrRelocated{};
};

enum class GPCallbackType {
    Entry
};

class PatchGroup
{
	friend class GraphicPack2;

public:
	PatchGroup(class GraphicPack2* gp, const char* nameStr, sint32 nameLength) : graphicPack(gp)
	{
		name = std::string(nameStr, nameLength);
	}

	bool matchesCRC(uint32 crc)
	{
		for (auto& chk : list_moduleMatches)
		{
			if (chk == crc)
				return true;
		}
		return false;
	}

	uint32 getCodeCaveBase()
	{
		return codeCaveMem.GetMPTR();
	}

	uint32 getCodeCaveSize()
	{
		return codeCaveSize;
	}

	std::string_view getName()
	{
		return name;
	}

	void setApplied() { m_isApplied = true; }
	void resetApplied() { m_isApplied = false; }
	bool isApplied() const { return m_isApplied; }

private:
	class GraphicPack2* graphicPack;
	std::string name;
	std::vector<uint32> list_moduleMatches;
	std::vector<PatchEntry*> list_patches;
	std::vector<std::pair<std::string, GPCallbackType>> list_callbacks;
	uint32 codeCaveSize;
	MEMPTR<void> codeCaveMem;
	bool m_isApplied{};
	bool m_isRpxOnlyTarget{};
};