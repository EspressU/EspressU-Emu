#pragma once

struct wuxHeader_t
{
	unsigned int		magic0;
	unsigned int		magic1;
	unsigned int		sectorSize;
	unsigned long long	uncompressedSize;
	unsigned int		flags;
};

struct wud_t
{
	class FileStream* fs;
	long long		uncompressedSize;
	bool			isCompressed;
	unsigned int	sectorSize;
	unsigned int	indexTableEntryCount;
	unsigned int*	indexTable;
	long long		offsetIndexTable;
	long long		offsetSectorArray;
};

#define WUX_MAGIC_0	'0XUW'
#define WUX_MAGIC_1	0x1099d02e

wud_t* wud_open(const fs::path& path);
void wud_close(wud_t* wud);

bool wud_isWUXCompressed(wud_t* wud);
unsigned int wud_readData(wud_t* wud, void* buffer, unsigned int length, long long offset);
long long wud_getWUDSize(wud_t* wud);