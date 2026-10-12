#pragma once

template<int TBitsX, int TBitsY, int TBitsZ, typename T>
class LookupTableL3
{
	struct TableZ
	{
		T arr[1 << TBitsZ]{};
	};

	struct TableY
	{
		TableZ* arr[1 << TBitsY];
	};

	TableY* m_placeholderTableY{};
	TableZ* m_placeholderTableZ{};

public:
	LookupTableL3()
	{
		m_placeholderTableZ = GenerateNewTableZ();
		m_placeholderTableY = GenerateNewTableY();
		for (auto& itr : m_tableXArr)
			itr = m_placeholderTableY;
	}

	~LookupTableL3()
	{
		delete m_placeholderTableY;
		delete m_placeholderTableZ;
	}

	T lookup(uint32 offset)
	{
		uint32 indexZ = offset & ((1u << TBitsZ) - 1);
		offset >>= TBitsZ;
		uint32 indexY = offset & ((1u << TBitsY) - 1);
		offset >>= TBitsY;
		uint32 indexX = offset & ((1u << TBitsX) - 1);
		return m_tableXArr[indexX]->arr[indexY]->arr[indexZ];
	}

	void store(uint32 offset, T& t)
	{
		uint32 indexZ = offset & ((1u << TBitsZ) - 1);
		offset >>= TBitsZ;
		uint32 indexY = offset & ((1u << TBitsY) - 1);
		offset >>= TBitsY;
		uint32 indexX = offset & ((1u << TBitsX) - 1);
		if (m_tableXArr[indexX] == m_placeholderTableY)
			m_tableXArr[indexX] = GenerateNewTableY();
		TableY* lookupY = m_tableXArr[indexX];
		if (lookupY->arr[indexY] == m_placeholderTableZ)
			lookupY->arr[indexY] = GenerateNewTableZ();
		TableZ* lookupZ = lookupY->arr[indexY];
		lookupZ->arr[indexZ] = t;
	}

private:
	TableY* GenerateNewTableY()
	{
		TableY* tableY = new TableY();
		for (auto& itr : tableY->arr)
			itr = m_placeholderTableZ;
		return tableY;
	}

	TableZ* GenerateNewTableZ()
	{
		TableZ* tableZ = new TableZ();
		return tableZ;
	}

	TableY* m_tableXArr[1 << TBitsX];
};
