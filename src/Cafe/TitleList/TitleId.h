#pragma once

using TitleId = uint64;

static_assert(sizeof(TitleId) == 8);

class TitleIdParser
{
public:
	enum class TITLE_TYPE
	{
		 UNKNOWN = 0xFF,
		 BASE_TITLE = 0x00,
		 BASE_TITLE_DEMO = 0x02,
		 BASE_TITLE_UPDATE = 0x0E,
		 HOMEBREW = 0x0F,
		 AOC = 0x0C,
		 SYSTEM_TITLE = 0x10,
		 SYSTEM_DATA = 0x1B,
		 SYSTEM_OVERLAY_TITLE = 0x30,
	};

	TitleIdParser(uint64 titleId) : m_titleId(titleId) {};

	bool IsSystemTitle() const
	{
		return (GetTypeByte() & 0x10) != 0;
	};

	bool IsBaseTitleUpdate() const
	{
		return GetType() == TITLE_TYPE::BASE_TITLE_UPDATE;
	}

	TITLE_TYPE GetType() const
	{
		uint8 b = GetTypeByte();
		switch (b)
		{
		case 0x00:
			return TITLE_TYPE::BASE_TITLE;
		case 0x02:
			return TITLE_TYPE::BASE_TITLE_DEMO;
		case 0x0E:
			return TITLE_TYPE::BASE_TITLE_UPDATE;
		case 0x0F:
			return TITLE_TYPE::HOMEBREW;
		case 0x0C:
			return TITLE_TYPE::AOC;
		case 0x10:
			return TITLE_TYPE::SYSTEM_TITLE;
		case 0x1B:
			return TITLE_TYPE::SYSTEM_DATA;
		case 0x30:
			return TITLE_TYPE::SYSTEM_OVERLAY_TITLE;
		}
		cemuLog_log(LogType::Force, "Unknown title type ({0:016x})", m_titleId);
		return TITLE_TYPE::UNKNOWN;
	}

	bool IsPlatformCafe() const
	{
		return GetPlatformWord() == 0x0005;
	}

	bool CanHaveSeparateUpdateTitleId() const
	{
		return GetType() == TITLE_TYPE::BASE_TITLE;
	}

	TitleId GetSeparateUpdateTitleId() const
	{
		cemu_assert_debug(CanHaveSeparateUpdateTitleId());
		return MakeTitleIdWithType(TITLE_TYPE::BASE_TITLE_UPDATE);
	}

	static TitleId MakeBaseTitleId(TitleId titleId)
	{
		TitleIdParser titleIdParser(titleId);
		if (titleIdParser.GetType() == TITLE_TYPE::BASE_TITLE_UPDATE)
			return titleIdParser.MakeTitleIdWithType(TITLE_TYPE::BASE_TITLE);
		return titleId;
	}

	static bool ParseFromStr(std::string_view strView, TitleId& titleIdOut)
	{
		if (strView.size() < 16)
			return false;
		uint64 tmp = 0;
		for (size_t i = 0; i < 8*2; i++)
		{
			tmp <<= 4;
			char c = strView[i];
			if (c >= 'A' && c <= 'F')
				tmp += (uint64)(c - 'A' + 10);
			else if (c >= 'a' && c <= 'f')
				tmp += (uint64)(c - 'a' + 10);
			else if (c >= '0' && c <= '9')
				tmp += (uint64)(c - '0');
			else
				return false;
		}
		titleIdOut = tmp;
		return true;
	}

private:
	uint8 GetTypeByte() const
	{
		return (m_titleId >> 32) & 0xFF;
	}

	TitleId MakeTitleIdWithType(TITLE_TYPE newType) const
	{
		TitleId t = m_titleId;
		t &= ~(0xFFull << 32);
		t |= ((uint64)newType << 32);
		return t;
	}

	uint16 GetPlatformWord() const
	{
		return (m_titleId >> 48) & 0xFFFF;
	}

	TitleId m_titleId;
};
